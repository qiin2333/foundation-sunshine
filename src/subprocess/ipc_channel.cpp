/**
 * @file src/subprocess/ipc_channel.cpp
 * @brief Implementation of IPC channel for subprocess communication.
 */

#include "ipc_channel.h"

#include "src/logging.h"

#include <sstream>
#include <chrono>

#ifdef _WIN32
  #include <windows.h>
#else
  #include <sys/socket.h>
  #include <sys/un.h>
  #include <unistd.h>
  #include <fcntl.h>
  #include <poll.h>
#endif

using namespace std::literals;

namespace subprocess {
namespace ipc {

  std::string
  make_pipe_name(uint32_t session_id) {
    std::ostringstream ss;
    ss << PIPE_NAME_PREFIX << session_id;
    return ss.str();
  }

  ipc_channel_t::ipc_channel_t(uint32_t session_id)
    : session_id_(session_id)
    , pipe_name_(make_pipe_name(session_id))
#ifdef _WIN32
    , pipe_handle_(INVALID_HANDLE_VALUE)
#else
    , pipe_fd_(-1)
#endif
    , connected_(false)
    , receive_loop_running_(false) {
  }

  ipc_channel_t::~ipc_channel_t() {
    close();
  }

  ipc_channel_t::ipc_channel_t(ipc_channel_t&& other) noexcept
    : session_id_(other.session_id_)
    , pipe_name_(std::move(other.pipe_name_))
#ifdef _WIN32
    , pipe_handle_(other.pipe_handle_)
#else
    , pipe_fd_(other.pipe_fd_)
#endif
    , connected_(other.connected_.load())
    , receive_loop_running_(false) {
#ifdef _WIN32
    other.pipe_handle_ = INVALID_HANDLE_VALUE;
#else
    other.pipe_fd_ = -1;
#endif
    other.connected_ = false;
  }

  ipc_channel_t&
  ipc_channel_t::operator=(ipc_channel_t&& other) noexcept {
    if (this != &other) {
      close();
      session_id_ = other.session_id_;
      pipe_name_ = std::move(other.pipe_name_);
#ifdef _WIN32
      pipe_handle_ = other.pipe_handle_;
      other.pipe_handle_ = INVALID_HANDLE_VALUE;
#else
      pipe_fd_ = other.pipe_fd_;
      other.pipe_fd_ = -1;
#endif
      connected_ = other.connected_.load();
      other.connected_ = false;
    }
    return *this;
  }

  bool
  ipc_channel_t::create_server() {
#ifdef _WIN32
    // Create a named pipe for the server
    pipe_handle_ = CreateNamedPipeA(
      pipe_name_.c_str(),
      PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
      PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
      1,  // Maximum instances
      MAX_MESSAGE_SIZE,  // Output buffer size
      MAX_MESSAGE_SIZE,  // Input buffer size
      0,  // Default timeout
      nullptr  // Default security attributes
    );

    if (pipe_handle_ == INVALID_HANDLE_VALUE) {
      BOOST_LOG(error) << "Failed to create named pipe: " << GetLastError();
      return false;
    }

    BOOST_LOG(debug) << "Created IPC server pipe: " << pipe_name_;
    return true;
#else
    // Create a Unix domain socket
    pipe_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (pipe_fd_ < 0) {
      BOOST_LOG(error) << "Failed to create Unix socket: " << errno;
      return false;
    }

    // Remove any existing socket file
    unlink(pipe_name_.c_str());

    struct sockaddr_un addr {};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, pipe_name_.c_str(), sizeof(addr.sun_path) - 1);

    if (bind(pipe_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
      BOOST_LOG(error) << "Failed to bind Unix socket: " << errno;
      ::close(pipe_fd_);
      pipe_fd_ = -1;
      return false;
    }

    if (listen(pipe_fd_, 1) < 0) {
      BOOST_LOG(error) << "Failed to listen on Unix socket: " << errno;
      ::close(pipe_fd_);
      pipe_fd_ = -1;
      return false;
    }

    BOOST_LOG(debug) << "Created IPC server socket: " << pipe_name_;
    return true;
#endif
  }

  bool
  ipc_channel_t::connect_client(uint32_t timeout_ms) {
#ifdef _WIN32
    auto start = std::chrono::steady_clock::now();
    while (true) {
      pipe_handle_ = CreateFileA(
        pipe_name_.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED,
        nullptr
      );

      if (pipe_handle_ != INVALID_HANDLE_VALUE) {
        // Set to message-read mode
        DWORD mode = PIPE_READMODE_MESSAGE;
        if (!SetNamedPipeHandleState(pipe_handle_, &mode, nullptr, nullptr)) {
          BOOST_LOG(warning) << "Failed to set pipe mode: " << GetLastError();
        }
        connected_ = true;
        BOOST_LOG(debug) << "Connected to IPC server pipe: " << pipe_name_;
        return true;
      }

      DWORD err = GetLastError();
      if (err != ERROR_PIPE_BUSY) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - start
        ).count();

        if (elapsed >= timeout_ms) {
          BOOST_LOG(error) << "Timeout connecting to IPC pipe: " << pipe_name_;
          return false;
        }

        // Pipe not yet created, wait a bit
        std::this_thread::sleep_for(100ms);
        continue;
      }

      // Wait for the pipe to become available
      if (!WaitNamedPipeA(pipe_name_.c_str(), timeout_ms)) {
        BOOST_LOG(error) << "Timeout waiting for IPC pipe: " << GetLastError();
        return false;
      }
    }
#else
    pipe_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (pipe_fd_ < 0) {
      BOOST_LOG(error) << "Failed to create Unix socket: " << errno;
      return false;
    }

    struct sockaddr_un addr {};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, pipe_name_.c_str(), sizeof(addr.sun_path) - 1);

    // Set non-blocking for timeout support
    int flags = fcntl(pipe_fd_, F_GETFL, 0);
    fcntl(pipe_fd_, F_SETFL, flags | O_NONBLOCK);

    auto start = std::chrono::steady_clock::now();
    while (true) {
      int result = connect(pipe_fd_, (struct sockaddr*)&addr, sizeof(addr));
      if (result == 0) {
        // Set back to blocking mode
        fcntl(pipe_fd_, F_SETFL, flags);
        connected_ = true;
        BOOST_LOG(debug) << "Connected to IPC server socket: " << pipe_name_;
        return true;
      }

      if (errno == EINPROGRESS || errno == ENOENT || errno == ECONNREFUSED) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - start
        ).count();

        if (elapsed >= timeout_ms) {
          BOOST_LOG(error) << "Timeout connecting to IPC socket: " << pipe_name_;
          ::close(pipe_fd_);
          pipe_fd_ = -1;
          return false;
        }

        std::this_thread::sleep_for(100ms);
        continue;
      }

      BOOST_LOG(error) << "Failed to connect to Unix socket: " << errno;
      ::close(pipe_fd_);
      pipe_fd_ = -1;
      return false;
    }
#endif
  }

  bool
  ipc_channel_t::wait_for_client(uint32_t timeout_ms) {
#ifdef _WIN32
    OVERLAPPED overlapped {};
    overlapped.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);

    if (!overlapped.hEvent) {
      BOOST_LOG(error) << "Failed to create event: " << GetLastError();
      return false;
    }

    BOOL result = ConnectNamedPipe(pipe_handle_, &overlapped);
    DWORD err = GetLastError();

    if (!result && err != ERROR_IO_PENDING && err != ERROR_PIPE_CONNECTED) {
      CloseHandle(overlapped.hEvent);
      BOOST_LOG(error) << "Failed to wait for client: " << err;
      return false;
    }

    if (err == ERROR_PIPE_CONNECTED) {
      CloseHandle(overlapped.hEvent);
      connected_ = true;
      BOOST_LOG(debug) << "Client already connected to IPC pipe";
      return true;
    }

    DWORD wait_result = WaitForSingleObject(overlapped.hEvent, timeout_ms);
    CloseHandle(overlapped.hEvent);

    if (wait_result == WAIT_TIMEOUT) {
      CancelIo(pipe_handle_);
      BOOST_LOG(error) << "Timeout waiting for IPC client";
      return false;
    }

    if (wait_result != WAIT_OBJECT_0) {
      BOOST_LOG(error) << "Error waiting for IPC client: " << GetLastError();
      return false;
    }

    connected_ = true;
    BOOST_LOG(debug) << "Client connected to IPC pipe";
    return true;
#else
    struct pollfd pfd;
    pfd.fd = pipe_fd_;
    pfd.events = POLLIN;

    int result = poll(&pfd, 1, timeout_ms);
    if (result <= 0) {
      if (result == 0) {
        BOOST_LOG(error) << "Timeout waiting for IPC client";
      } else {
        BOOST_LOG(error) << "Error waiting for IPC client: " << errno;
      }
      return false;
    }

    int client_fd = accept(pipe_fd_, nullptr, nullptr);
    if (client_fd < 0) {
      BOOST_LOG(error) << "Failed to accept client: " << errno;
      return false;
    }

    // Close the listening socket and use the client socket
    ::close(pipe_fd_);
    pipe_fd_ = client_fd;
    connected_ = true;
    BOOST_LOG(debug) << "Client connected to IPC socket";
    return true;
#endif
  }

  void
  ipc_channel_t::close() {
    stop_receive_loop();

    connected_ = false;

#ifdef _WIN32
    if (pipe_handle_ != INVALID_HANDLE_VALUE) {
      DisconnectNamedPipe(pipe_handle_);
      CloseHandle(pipe_handle_);
      pipe_handle_ = INVALID_HANDLE_VALUE;
    }
#else
    if (pipe_fd_ >= 0) {
      ::close(pipe_fd_);
      // Remove the socket file
      unlink(pipe_name_.c_str());
      pipe_fd_ = -1;
    }
#endif

    BOOST_LOG(debug) << "Closed IPC channel: " << pipe_name_;
  }

  bool
  ipc_channel_t::is_connected() const {
    return connected_;
  }

  bool
  ipc_channel_t::send(const message_header_t& header, const void* payload) {
    if (!connected_) {
      BOOST_LOG(warning) << "Cannot send: IPC channel not connected";
      return false;
    }

    std::lock_guard<std::mutex> lock(send_mutex_);

#ifdef _WIN32
    OVERLAPPED overlapped {};
    overlapped.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);

    if (!overlapped.hEvent) {
      BOOST_LOG(error) << "Failed to create event for send: " << GetLastError();
      return false;
    }

    // Send header
    DWORD bytes_written;
    BOOL result = WriteFile(pipe_handle_, &header, sizeof(header), &bytes_written, &overlapped);

    if (!result && GetLastError() == ERROR_IO_PENDING) {
      WaitForSingleObject(overlapped.hEvent, INFINITE);
      GetOverlappedResult(pipe_handle_, &overlapped, &bytes_written, FALSE);
    }

    if (bytes_written != sizeof(header)) {
      CloseHandle(overlapped.hEvent);
      BOOST_LOG(error) << "Failed to send IPC header";
      connected_ = false;
      return false;
    }

    // Send payload if present
    if (header.payload_size > 0 && payload) {
      ResetEvent(overlapped.hEvent);
      result = WriteFile(pipe_handle_, payload, header.payload_size, &bytes_written, &overlapped);

      if (!result && GetLastError() == ERROR_IO_PENDING) {
        WaitForSingleObject(overlapped.hEvent, INFINITE);
        GetOverlappedResult(pipe_handle_, &overlapped, &bytes_written, FALSE);
      }

      if (bytes_written != header.payload_size) {
        CloseHandle(overlapped.hEvent);
        BOOST_LOG(error) << "Failed to send IPC payload";
        connected_ = false;
        return false;
      }
    }

    CloseHandle(overlapped.hEvent);
    return true;
#else
    // Send header
    ssize_t bytes_written = write(pipe_fd_, &header, sizeof(header));
    if (bytes_written != sizeof(header)) {
      BOOST_LOG(error) << "Failed to send IPC header: " << errno;
      connected_ = false;
      return false;
    }

    // Send payload if present
    if (header.payload_size > 0 && payload) {
      bytes_written = write(pipe_fd_, payload, header.payload_size);
      if (bytes_written != header.payload_size) {
        BOOST_LOG(error) << "Failed to send IPC payload: " << errno;
        connected_ = false;
        return false;
      }
    }

    return true;
#endif
  }

  bool
  ipc_channel_t::receive(message_header_t& header, std::vector<uint8_t>& payload, uint32_t timeout_ms) {
    if (!connected_) {
      return false;
    }

#ifdef _WIN32
    OVERLAPPED overlapped {};
    overlapped.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);

    if (!overlapped.hEvent) {
      BOOST_LOG(error) << "Failed to create event for receive: " << GetLastError();
      return false;
    }

    DWORD bytes_read;
    BOOL result = ReadFile(pipe_handle_, &header, sizeof(header), &bytes_read, &overlapped);

    if (!result) {
      DWORD err = GetLastError();
      if (err == ERROR_IO_PENDING) {
        DWORD wait_result = WaitForSingleObject(overlapped.hEvent, timeout_ms);
        if (wait_result == WAIT_TIMEOUT) {
          CancelIo(pipe_handle_);
          CloseHandle(overlapped.hEvent);
          return false;
        }
        GetOverlappedResult(pipe_handle_, &overlapped, &bytes_read, FALSE);
      } else {
        CloseHandle(overlapped.hEvent);
        if (err != ERROR_PIPE_NOT_CONNECTED && err != ERROR_BROKEN_PIPE) {
          BOOST_LOG(error) << "Failed to receive IPC header: " << err;
        }
        connected_ = false;
        return false;
      }
    }

    if (bytes_read != sizeof(header) || !validate_header(header)) {
      CloseHandle(overlapped.hEvent);
      BOOST_LOG(error) << "Invalid IPC header received";
      return false;
    }

    // Read payload if present
    if (header.payload_size > 0) {
      payload.resize(header.payload_size);
      ResetEvent(overlapped.hEvent);
      result = ReadFile(pipe_handle_, payload.data(), header.payload_size, &bytes_read, &overlapped);

      if (!result && GetLastError() == ERROR_IO_PENDING) {
        WaitForSingleObject(overlapped.hEvent, INFINITE);
        GetOverlappedResult(pipe_handle_, &overlapped, &bytes_read, FALSE);
      }

      if (bytes_read != header.payload_size) {
        CloseHandle(overlapped.hEvent);
        BOOST_LOG(error) << "Failed to receive IPC payload";
        return false;
      }
    } else {
      payload.clear();
    }

    CloseHandle(overlapped.hEvent);
    return true;
#else
    struct pollfd pfd;
    pfd.fd = pipe_fd_;
    pfd.events = POLLIN;

    int poll_result = poll(&pfd, 1, timeout_ms);
    if (poll_result <= 0) {
      return false;
    }

    // Read header
    ssize_t bytes_read = read(pipe_fd_, &header, sizeof(header));
    if (bytes_read != sizeof(header) || !validate_header(header)) {
      if (bytes_read <= 0) {
        connected_ = false;
      }
      return false;
    }

    // Read payload if present
    if (header.payload_size > 0) {
      payload.resize(header.payload_size);
      bytes_read = read(pipe_fd_, payload.data(), header.payload_size);
      if (bytes_read != header.payload_size) {
        BOOST_LOG(error) << "Failed to receive IPC payload";
        return false;
      }
    } else {
      payload.clear();
    }

    return true;
#endif
  }

  void
  ipc_channel_t::start_receive_loop(message_callback_t callback) {
    if (receive_loop_running_) {
      return;
    }

    receive_loop_running_ = true;
    receive_thread_ = std::thread([this, callback]() {
      while (receive_loop_running_ && connected_) {
        message_header_t header;
        std::vector<uint8_t> payload;

        if (receive(header, payload, 100)) {
          callback(header, payload);
        }
      }
    });
  }

  void
  ipc_channel_t::stop_receive_loop() {
    receive_loop_running_ = false;
    if (receive_thread_.joinable()) {
      receive_thread_.join();
    }
  }

  std::string
  ipc_channel_t::get_pipe_name() const {
    return pipe_name_;
  }

}  // namespace ipc
}  // namespace subprocess
