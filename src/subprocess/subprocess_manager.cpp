/**
 * @file src/subprocess/subprocess_manager.cpp
 * @brief Implementation of the subprocess manager.
 */

#include "subprocess_manager.h"

#include "src/logging.h"
#include "src/platform/common.h"

#include <chrono>
#include <sstream>
#include <cstring>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <userenv.h>
  #include "src/platform/windows/misc.h"
#else
  #include <sys/wait.h>
  #include <signal.h>
  #include <arpa/inet.h>
#endif

using namespace std::literals;

namespace subprocess {

  // Global subprocess configuration
  static subprocess_config_t g_subprocess_config;

  subprocess_config_t&
  get_subprocess_config() {
    return g_subprocess_config;
  }

  bool
  is_subprocess_mode_enabled() {
    return g_subprocess_config.enabled;
  }

  subprocess_manager_t::subprocess_manager_t(const subprocess_config_t& config)
    : config_(config)
    , running_(false)
    , session_id_(0)
    , sequence_(0)
#ifdef _WIN32
    , process_handle_(nullptr)
    , thread_handle_(nullptr)
#else
    , process_pid_(-1)
#endif
    , heartbeat_running_(false) {
  }

  subprocess_manager_t::~subprocess_manager_t() {
    stop_session();
  }

  bool
  subprocess_manager_t::start_session(
    const stream::config_t& stream_config,
    const rtsp_stream::launch_session_t& launch_session
  ) {
    if (running_) {
      BOOST_LOG(warning) << "Subprocess session already running";
      return false;
    }

    session_id_ = launch_session.id;
    sequence_ = 0;

    // Create IPC channel first (as server)
    ipc_channel_ = std::make_unique<ipc::ipc_channel_t>(session_id_);
    if (!ipc_channel_->create_server()) {
      BOOST_LOG(error) << "Failed to create IPC channel server";
      return false;
    }

    // Launch the subprocess
    if (!launch_subprocess(session_id_)) {
      BOOST_LOG(error) << "Failed to launch sender subprocess";
      ipc_channel_->close();
      ipc_channel_.reset();
      return false;
    }

    // Wait for subprocess to connect
    if (!ipc_channel_->wait_for_client(config_.startup_timeout_ms)) {
      BOOST_LOG(error) << "Timeout waiting for subprocess to connect";
      terminate_subprocess();
      ipc_channel_->close();
      ipc_channel_.reset();
      return false;
    }

    // Send initialization message
    ipc::init_payload_t init_payload {};
    init_payload.width = stream_config.monitor.width;
    init_payload.height = stream_config.monitor.height;
    init_payload.framerate = stream_config.monitor.framerate;
    init_payload.framerate_num = stream_config.monitor.frameRateNum;
    init_payload.framerate_den = stream_config.monitor.frameRateDen;
    init_payload.bitrate = stream_config.monitor.bitrate;
    init_payload.slices_per_frame = stream_config.monitor.slicesPerFrame;
    init_payload.num_ref_frames = stream_config.monitor.numRefFrames;
    init_payload.video_format = stream_config.monitor.videoFormat;
    init_payload.dynamic_range = stream_config.monitor.dynamicRange;
    init_payload.chroma_sampling = stream_config.monitor.chromaSamplingType;
    init_payload.encoder_csc_mode = stream_config.monitor.encoderCscMode;

    init_payload.audio_channels = stream_config.audio.channels;

    init_payload.packet_size = stream_config.packetsize;
    init_payload.fec_percentage = 0;  // Will be set from config
    init_payload.min_fec_packets = stream_config.minRequiredFecPackets;

    init_payload.encryption_flags = stream_config.encryptionFlagsEnabled;

    // Copy encryption key and IV
    std::memcpy(init_payload.gcm_key, launch_session.gcm_key.data(),
                std::min(sizeof(init_payload.gcm_key), launch_session.gcm_key.size()));
    std::memcpy(init_payload.iv, launch_session.iv.data(),
                std::min(sizeof(init_payload.iv), launch_session.iv.size()));

    // Copy display name
    strncpy(init_payload.display_name, stream_config.monitor.display_name.c_str(),
            sizeof(init_payload.display_name) - 1);
    init_payload.display_name[sizeof(init_payload.display_name) - 1] = '\0';

    // Copy client name
    strncpy(init_payload.client_name, launch_session.client_name.c_str(),
            sizeof(init_payload.client_name) - 1);
    init_payload.client_name[sizeof(init_payload.client_name) - 1] = '\0';

    init_payload.session_id = launch_session.id;

    if (!ipc_channel_->send_message(
          static_cast<uint16_t>(ipc::command_type_e::INIT),
          sequence_++,
          init_payload)) {
      BOOST_LOG(error) << "Failed to send init message to subprocess";
      terminate_subprocess();
      ipc_channel_->close();
      ipc_channel_.reset();
      return false;
    }

    // Wait for OK response
    ipc::message_header_t response_header;
    std::vector<uint8_t> response_payload;
    if (!ipc_channel_->receive(response_header, response_payload, 5000)) {
      BOOST_LOG(error) << "No response from subprocess after init";
      terminate_subprocess();
      ipc_channel_->close();
      ipc_channel_.reset();
      return false;
    }

    if (response_header.type != static_cast<uint16_t>(ipc::response_type_e::OK)) {
      BOOST_LOG(error) << "Subprocess init failed with error code: " << response_header.type;
      terminate_subprocess();
      ipc_channel_->close();
      ipc_channel_.reset();
      return false;
    }

    running_ = true;

    // Start message receive loop
    ipc_channel_->start_receive_loop([this](const ipc::message_header_t& header,
                                            const std::vector<uint8_t>& payload) {
      handle_ipc_message(header, payload);
    });

    // Start heartbeat thread
    heartbeat_running_ = true;
    heartbeat_thread_ = std::thread(&subprocess_manager_t::heartbeat_thread_func, this);

    BOOST_LOG(info) << "Subprocess streaming session started (session_id=" << session_id_ << ")";
    return true;
  }

  void
  subprocess_manager_t::stop_session() {
    if (!running_) {
      return;
    }

    // Stop heartbeat
    heartbeat_running_ = false;
    if (heartbeat_thread_.joinable()) {
      heartbeat_thread_.join();
    }

    // Send stop command
    if (ipc_channel_ && ipc_channel_->is_connected()) {
      ipc_channel_->send_command(static_cast<uint16_t>(ipc::command_type_e::STOP), sequence_++);

      // Give subprocess time to cleanup
      std::this_thread::sleep_for(500ms);
    }

    // Stop IPC
    if (ipc_channel_) {
      ipc_channel_->stop_receive_loop();
      ipc_channel_->close();
      ipc_channel_.reset();
    }

    // Terminate subprocess
    terminate_subprocess();

    running_ = false;
    session_id_ = 0;

    BOOST_LOG(info) << "Subprocess streaming session stopped";
  }

  void
  subprocess_manager_t::request_idr_frame() {
    if (!running_ || !ipc_channel_) {
      return;
    }

    ipc_channel_->send_command(static_cast<uint16_t>(ipc::command_type_e::REQUEST_IDR), sequence_++);
  }

  void
  subprocess_manager_t::invalidate_ref_frames(int64_t first_frame, int64_t last_frame) {
    if (!running_ || !ipc_channel_) {
      return;
    }

    ipc::invalidate_ref_frames_t payload;
    payload.first_frame = first_frame;
    payload.last_frame = last_frame;

    ipc_channel_->send_message(
      static_cast<uint16_t>(ipc::command_type_e::INVALIDATE_REF_FRAMES),
      sequence_++,
      payload
    );
  }

  void
  subprocess_manager_t::change_bitrate(int bitrate_kbps) {
    if (!running_ || !ipc_channel_) {
      return;
    }

    ipc::bitrate_change_t payload;
    payload.new_bitrate_kbps = bitrate_kbps;
    payload.max_bitrate_kbps = 0;  // No max

    ipc_channel_->send_message(
      static_cast<uint16_t>(ipc::command_type_e::CHANGE_BITRATE),
      sequence_++,
      payload
    );
  }

  bool
  subprocess_manager_t::pass_socket_info(
    int socket_type,
    uintptr_t socket_handle,
    const std::string& remote_addr,
    uint16_t remote_port,
    const std::string& local_addr,
    uint16_t local_port
  ) {
    if (!running_ || !ipc_channel_) {
      return false;
    }

    ipc::socket_info_t socket_info {};
    socket_info.socket_type = socket_type;
    socket_info.local_port = local_port;
    socket_info.remote_port = remote_port;

    // Parse addresses
    struct in6_addr addr6;
    struct in_addr addr4;

    if (inet_pton(AF_INET6, remote_addr.c_str(), &addr6) == 1) {
      socket_info.address_family = AF_INET6;
      std::memcpy(socket_info.remote_addr, &addr6, sizeof(addr6));
    } else if (inet_pton(AF_INET, remote_addr.c_str(), &addr4) == 1) {
      socket_info.address_family = AF_INET;
      // Store as IPv4-mapped IPv6
      std::memset(socket_info.remote_addr, 0, 10);
      socket_info.remote_addr[10] = 0xFF;
      socket_info.remote_addr[11] = 0xFF;
      std::memcpy(socket_info.remote_addr + 12, &addr4, sizeof(addr4));
    } else {
      BOOST_LOG(error) << "Invalid remote address: " << remote_addr;
      return false;
    }

    if (inet_pton(AF_INET6, local_addr.c_str(), &addr6) == 1) {
      std::memcpy(socket_info.local_addr, &addr6, sizeof(addr6));
    } else if (inet_pton(AF_INET, local_addr.c_str(), &addr4) == 1) {
      std::memset(socket_info.local_addr, 0, 10);
      socket_info.local_addr[10] = 0xFF;
      socket_info.local_addr[11] = 0xFF;
      std::memcpy(socket_info.local_addr + 12, &addr4, sizeof(addr4));
    }

#ifdef _WIN32
    // Use WSADuplicateSocket to make the socket accessible from the subprocess
    WSAPROTOCOL_INFOW protocol_info;
    if (WSADuplicateSocketW((SOCKET)socket_handle, GetProcessId(process_handle_), &protocol_info) != 0) {
      BOOST_LOG(error) << "WSADuplicateSocket failed: " << WSAGetLastError();
      return false;
    }
    std::memcpy(socket_info.protocol_info, &protocol_info, sizeof(protocol_info));
#else
    // On Unix, we would use SCM_RIGHTS to pass the file descriptor
    // For now, just note that this requires additional implementation
    socket_info.reserved = static_cast<int32_t>(socket_handle);
#endif

    return ipc_channel_->send_message(
      static_cast<uint16_t>(ipc::command_type_e::SOCKET_INFO),
      sequence_++,
      socket_info
    );
  }

  bool
  subprocess_manager_t::is_running() const {
    return running_;
  }

  void
  subprocess_manager_t::set_event_callback(event_callback_t callback) {
    event_callback_ = std::move(callback);
  }

  uint32_t
  subprocess_manager_t::get_session_id() const {
    return session_id_;
  }

  bool
  subprocess_manager_t::launch_subprocess(uint32_t session_id) {
#ifdef _WIN32
    // Build command line
    std::ostringstream cmd;
    cmd << "\"" << config_.sender_path << "\" --session " << session_id;
    std::string cmd_str = cmd.str();

    BOOST_LOG(debug) << "Launching subprocess: " << cmd_str;

    // Get user token if running as SYSTEM
    HANDLE user_token = nullptr;
    bool running_as_system = platf::is_running_as_system();

    if (running_as_system) {
      user_token = platf::retrieve_users_token(false);
      if (!user_token) {
        BOOST_LOG(error) << "Failed to retrieve user token for subprocess";
        return false;
      }
    }

    STARTUPINFOW si {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi {};

    // Convert command line to wide string
    int wlen = MultiByteToWideChar(CP_UTF8, 0, cmd_str.c_str(), -1, nullptr, 0);
    std::vector<wchar_t> wcmd(wlen);
    MultiByteToWideChar(CP_UTF8, 0, cmd_str.c_str(), -1, wcmd.data(), wlen);

    BOOL result;
    if (running_as_system && user_token) {
      // Create process as user
      result = CreateProcessAsUserW(
        user_token,
        nullptr,
        wcmd.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP,
        nullptr,
        nullptr,
        &si,
        &pi
      );
      CloseHandle(user_token);
    } else {
      // Create process normally
      result = CreateProcessW(
        nullptr,
        wcmd.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP,
        nullptr,
        nullptr,
        &si,
        &pi
      );
    }

    if (!result) {
      BOOST_LOG(error) << "Failed to create subprocess: " << GetLastError();
      return false;
    }

    process_handle_ = pi.hProcess;
    thread_handle_ = pi.hThread;

    BOOST_LOG(info) << "Subprocess launched (PID=" << GetProcessId(process_handle_) << ")";
    return true;
#else
    // Fork and exec
    process_pid_ = fork();

    if (process_pid_ < 0) {
      BOOST_LOG(error) << "Fork failed: " << errno;
      return false;
    }

    if (process_pid_ == 0) {
      // Child process
      std::ostringstream session_arg;
      session_arg << session_id;

      execl(config_.sender_path.c_str(),
            config_.sender_path.c_str(),
            "--session",
            session_arg.str().c_str(),
            nullptr);

      // If exec returns, it failed
      _exit(1);
    }

    BOOST_LOG(info) << "Subprocess launched (PID=" << process_pid_ << ")";
    return true;
#endif
  }

  void
  subprocess_manager_t::terminate_subprocess() {
#ifdef _WIN32
    if (process_handle_) {
      // Try graceful termination first
      DWORD exit_code;
      if (GetExitCodeProcess(process_handle_, &exit_code) && exit_code == STILL_ACTIVE) {
        // Give it a moment to exit gracefully
        if (WaitForSingleObject(process_handle_, 2000) == WAIT_TIMEOUT) {
          BOOST_LOG(warning) << "Subprocess did not exit gracefully, terminating";
          TerminateProcess(process_handle_, 1);
        }
      }

      CloseHandle(process_handle_);
      process_handle_ = nullptr;
    }

    if (thread_handle_) {
      CloseHandle(thread_handle_);
      thread_handle_ = nullptr;
    }
#else
    if (process_pid_ > 0) {
      // Send SIGTERM for graceful shutdown
      kill(process_pid_, SIGTERM);

      // Wait for process to exit
      int status;
      int result = waitpid(process_pid_, &status, WNOHANG);

      if (result == 0) {
        // Process still running, wait a bit
        std::this_thread::sleep_for(2s);
        result = waitpid(process_pid_, &status, WNOHANG);

        if (result == 0) {
          // Force kill
          BOOST_LOG(warning) << "Subprocess did not exit gracefully, killing";
          kill(process_pid_, SIGKILL);
          waitpid(process_pid_, &status, 0);
        }
      }

      process_pid_ = -1;
    }
#endif
  }

  void
  subprocess_manager_t::handle_ipc_message(const ipc::message_header_t& header,
                                           const std::vector<uint8_t>& payload) {
    auto response_type = static_cast<ipc::response_type_e>(header.type);

    switch (response_type) {
      case ipc::response_type_e::OK:
        BOOST_LOG(verbose) << "Subprocess: OK (seq=" << header.sequence << ")";
        break;

      case ipc::response_type_e::ERROR:
        if (payload.size() >= sizeof(ipc::status_payload_t)) {
          auto* status = reinterpret_cast<const ipc::status_payload_t*>(payload.data());
          BOOST_LOG(error) << "Subprocess error: " << status->message
                           << " (code=" << status->error_code << ")";

          if (event_callback_) {
            event_callback_(response_type, status->message);
          }
        }
        break;

      case ipc::response_type_e::ENCODER_ERROR:
        BOOST_LOG(error) << "Subprocess encoder error";
        if (event_callback_) {
          event_callback_(response_type, "Encoder error");
        }
        break;

      case ipc::response_type_e::CAPTURE_ERROR:
        BOOST_LOG(error) << "Subprocess capture error";
        if (event_callback_) {
          event_callback_(response_type, "Capture error");
        }
        break;

      default:
        BOOST_LOG(debug) << "Subprocess message type: " << static_cast<int>(header.type);
        break;
    }
  }

  void
  subprocess_manager_t::heartbeat_thread_func() {
    while (heartbeat_running_ && running_) {
      std::this_thread::sleep_for(std::chrono::milliseconds(config_.heartbeat_interval_ms));

      if (!heartbeat_running_ || !running_) {
        break;
      }

      // Check if subprocess is still running
#ifdef _WIN32
      if (process_handle_) {
        DWORD exit_code;
        if (GetExitCodeProcess(process_handle_, &exit_code) && exit_code != STILL_ACTIVE) {
          BOOST_LOG(error) << "Subprocess exited unexpectedly (exit_code=" << exit_code << ")";
          running_ = false;
          if (event_callback_) {
            event_callback_(ipc::response_type_e::ERROR, "Subprocess exited unexpectedly");
          }
          break;
        }
      }
#else
      if (process_pid_ > 0) {
        int status;
        int result = waitpid(process_pid_, &status, WNOHANG);
        if (result != 0) {
          BOOST_LOG(error) << "Subprocess exited unexpectedly";
          process_pid_ = -1;
          running_ = false;
          if (event_callback_) {
            event_callback_(ipc::response_type_e::ERROR, "Subprocess exited unexpectedly");
          }
          break;
        }
      }
#endif

      // Send heartbeat ping
      if (ipc_channel_ && ipc_channel_->is_connected()) {
        ipc_channel_->send_command(static_cast<uint16_t>(ipc::command_type_e::PING), sequence_++);
      }
    }
  }

}  // namespace subprocess
