/**
 * @file src/subprocess/sender.cpp
 * @brief Implementation of the sender subprocess.
 */

#include "sender.h"

#include "src/logging.h"

#include <chrono>
#include <iostream>

#ifdef _WIN32
  #include <winsock2.h>
  #include <windows.h>
#else
  #include <signal.h>
#endif

using namespace std::literals;

namespace subprocess {

  // Global sender instance for signal handling
  static sender_t* g_sender = nullptr;

#ifdef _WIN32
  static BOOL WINAPI
  console_ctrl_handler(DWORD ctrl_type) {
    if (g_sender) {
      g_sender->shutdown();
    }
    return TRUE;
  }
#else
  static void
  signal_handler(int sig) {
    if (g_sender) {
      g_sender->shutdown();
    }
  }
#endif

  sender_t::sender_t(uint32_t session_id)
    : session_id_(session_id)
    , running_(false)
    , streaming_(false)
    , video_socket_(0)
    , audio_socket_(0) {
    std::memset(&session_config_, 0, sizeof(session_config_));
  }

  sender_t::~sender_t() {
    shutdown();
  }

  int
  sender_t::run() {
    BOOST_LOG(info) << "Sender subprocess starting (session_id=" << session_id_ << ")";

    // Set up signal handling
    g_sender = this;
#ifdef _WIN32
    SetConsoleCtrlHandler(console_ctrl_handler, TRUE);
#else
    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);
#endif

    // Connect to main process
    if (!connect_to_main_process()) {
      BOOST_LOG(error) << "Failed to connect to main process";
      return 1;
    }

    running_ = true;

    // Main command loop
    while (running_) {
      ipc::message_header_t header;
      std::vector<uint8_t> payload;

      if (ipc_channel_->receive(header, payload, 100)) {
        handle_command(header, payload);
      }

      // Check for shutdown
      if (!running_) {
        break;
      }
    }

    // Cleanup
    stop_streaming();
    ipc_channel_->close();

    BOOST_LOG(info) << "Sender subprocess exiting";
    return 0;
  }

  void
  sender_t::shutdown() {
    BOOST_LOG(info) << "Sender subprocess shutdown requested";
    running_ = false;
    streaming_ = false;
  }

  bool
  sender_t::connect_to_main_process() {
    ipc_channel_ = std::make_unique<ipc::ipc_channel_t>(session_id_);

    // Connect to the server (main process) with timeout
    if (!ipc_channel_->connect_client(10000)) {
      BOOST_LOG(error) << "Failed to connect to IPC server";
      return false;
    }

    BOOST_LOG(info) << "Connected to main process IPC channel";
    return true;
  }

  void
  sender_t::handle_command(const ipc::message_header_t& header, const std::vector<uint8_t>& payload) {
    auto cmd_type = static_cast<ipc::command_type_e>(header.type);

    switch (cmd_type) {
      case ipc::command_type_e::INIT: {
        if (payload.size() < sizeof(ipc::init_payload_t)) {
          send_error(header.sequence, -1, "Invalid init payload size");
          return;
        }

        std::memcpy(&session_config_, payload.data(), sizeof(session_config_));

        if (initialize_session(session_config_)) {
          send_ok(header.sequence);
        } else {
          send_error(header.sequence, -1, "Failed to initialize session");
        }
        break;
      }

      case ipc::command_type_e::START: {
        if (start_streaming()) {
          send_ok(header.sequence);
        } else {
          send_error(header.sequence, -1, "Failed to start streaming");
        }
        break;
      }

      case ipc::command_type_e::STOP: {
        stop_streaming();
        send_ok(header.sequence);
        break;
      }

      case ipc::command_type_e::SHUTDOWN: {
        send_ok(header.sequence);
        shutdown();
        break;
      }

      case ipc::command_type_e::REQUEST_IDR: {
        // TODO: Signal encoder to generate IDR frame
        BOOST_LOG(debug) << "IDR frame requested";
        send_ok(header.sequence);
        break;
      }

      case ipc::command_type_e::INVALIDATE_REF_FRAMES: {
        if (payload.size() >= sizeof(ipc::invalidate_ref_frames_t)) {
          auto* frames = reinterpret_cast<const ipc::invalidate_ref_frames_t*>(payload.data());
          BOOST_LOG(debug) << "Invalidate ref frames: " << frames->first_frame
                           << " - " << frames->last_frame;
          // TODO: Signal encoder to invalidate reference frames
        }
        send_ok(header.sequence);
        break;
      }

      case ipc::command_type_e::CHANGE_BITRATE: {
        if (payload.size() >= sizeof(ipc::bitrate_change_t)) {
          auto* bitrate = reinterpret_cast<const ipc::bitrate_change_t*>(payload.data());
          BOOST_LOG(debug) << "Bitrate change: " << bitrate->new_bitrate_kbps << " Kbps";
          // TODO: Signal encoder to change bitrate
        }
        send_ok(header.sequence);
        break;
      }

      case ipc::command_type_e::SOCKET_INFO: {
        if (payload.size() >= sizeof(ipc::socket_info_t)) {
          auto* socket_info = reinterpret_cast<const ipc::socket_info_t*>(payload.data());

#ifdef _WIN32
          // Recreate socket from WSAPROTOCOL_INFO
          WSAPROTOCOL_INFOW protocol_info;
          std::memcpy(&protocol_info, socket_info->protocol_info, sizeof(protocol_info));

          SOCKET sock = WSASocketW(
            socket_info->address_family,
            SOCK_DGRAM,
            IPPROTO_UDP,
            &protocol_info,
            0,
            WSA_FLAG_OVERLAPPED
          );

          if (sock == INVALID_SOCKET) {
            BOOST_LOG(error) << "WSASocket failed: " << WSAGetLastError();
            send_error(header.sequence, WSAGetLastError(), "Failed to duplicate socket");
            return;
          }

          if (socket_info->socket_type == 0) {
            video_socket_ = (uintptr_t)sock;
            BOOST_LOG(info) << "Video socket received";
          } else {
            audio_socket_ = (uintptr_t)sock;
            BOOST_LOG(info) << "Audio socket received";
          }
#else
          // On Unix, socket was passed via SCM_RIGHTS
          if (socket_info->socket_type == 0) {
            video_socket_ = socket_info->reserved;
          } else {
            audio_socket_ = socket_info->reserved;
          }
#endif
        }
        send_ok(header.sequence);
        break;
      }

      case ipc::command_type_e::PING: {
        // Respond with PONG
        auto response = ipc::make_header(
          static_cast<uint16_t>(ipc::command_type_e::PONG),
          header.sequence
        );
        ipc_channel_->send(response);
        break;
      }

      default:
        BOOST_LOG(warning) << "Unknown command type: " << static_cast<int>(header.type);
        break;
    }
  }

  bool
  sender_t::initialize_session(const ipc::init_payload_t& init) {
    BOOST_LOG(info) << "Initializing session: " << init.width << "x" << init.height
                    << "@" << init.framerate << "fps"
                    << ", bitrate=" << init.bitrate << "Kbps"
                    << ", format=" << init.video_format;

    // TODO: Initialize encoder and capture
    // This is where we would set up:
    // 1. Display capture (WGC on Windows)
    // 2. Hardware encoder (NVENC/AMF/QSV)
    // 3. Audio capture (WASAPI on Windows)

    return true;
  }

  bool
  sender_t::start_streaming() {
    if (streaming_) {
      BOOST_LOG(warning) << "Streaming already started";
      return true;
    }

    BOOST_LOG(info) << "Starting capture and streaming";

    streaming_ = true;

    // Start worker threads
    video_thread_ = std::thread(&sender_t::video_thread_func, this);
    audio_thread_ = std::thread(&sender_t::audio_thread_func, this);
    network_thread_ = std::thread(&sender_t::network_thread_func, this);

    return true;
  }

  void
  sender_t::stop_streaming() {
    if (!streaming_) {
      return;
    }

    BOOST_LOG(info) << "Stopping streaming";
    streaming_ = false;

    // Wait for worker threads to finish
    if (video_thread_.joinable()) {
      video_thread_.join();
    }
    if (audio_thread_.joinable()) {
      audio_thread_.join();
    }
    if (network_thread_.joinable()) {
      network_thread_.join();
    }

#ifdef _WIN32
    // Close duplicated sockets
    if (video_socket_) {
      closesocket((SOCKET)video_socket_);
      video_socket_ = 0;
    }
    if (audio_socket_) {
      closesocket((SOCKET)audio_socket_);
      audio_socket_ = 0;
    }
#else
    if (video_socket_) {
      close(video_socket_);
      video_socket_ = 0;
    }
    if (audio_socket_) {
      close(audio_socket_);
      audio_socket_ = 0;
    }
#endif
  }

  void
  sender_t::send_ok(uint32_t sequence) {
    auto header = ipc::make_header(
      static_cast<uint16_t>(ipc::response_type_e::OK),
      sequence
    );
    ipc_channel_->send(header);
  }

  void
  sender_t::send_error(uint32_t sequence, int error_code, const std::string& message) {
    ipc::status_payload_t status {};
    status.error_code = error_code;
    strncpy(status.message, message.c_str(), sizeof(status.message) - 1);
    status.message[sizeof(status.message) - 1] = '\0';

    ipc_channel_->send_message(
      static_cast<uint16_t>(ipc::response_type_e::ERROR),
      sequence,
      status
    );
  }

  void
  sender_t::video_thread_func() {
    BOOST_LOG(debug) << "Video thread started";

    // TODO: Implement video capture and encoding loop
    // This is where we would:
    // 1. Capture frames using WGC/DXGI
    // 2. Encode using NVENC/AMF/QSV
    // 3. Queue encoded packets for network thread

    while (streaming_) {
      // Placeholder: simulate capture loop
      std::this_thread::sleep_for(16ms);  // ~60fps
    }

    BOOST_LOG(debug) << "Video thread stopped";
  }

  void
  sender_t::audio_thread_func() {
    BOOST_LOG(debug) << "Audio thread started";

    // TODO: Implement audio capture and encoding loop
    // This is where we would:
    // 1. Capture audio using WASAPI loopback
    // 2. Encode using Opus
    // 3. Queue encoded packets for network thread

    while (streaming_) {
      // Placeholder: simulate audio loop
      std::this_thread::sleep_for(10ms);  // 100fps audio
    }

    BOOST_LOG(debug) << "Audio thread stopped";
  }

  void
  sender_t::network_thread_func() {
    BOOST_LOG(debug) << "Network thread started";

    // TODO: Implement RTP packet sending
    // This is where we would:
    // 1. Dequeue encoded packets
    // 2. Packetize with RTP headers
    // 3. Apply FEC
    // 4. Send via UDP directly to client

    while (streaming_) {
      // Placeholder: simulate network loop
      std::this_thread::sleep_for(1ms);
    }

    BOOST_LOG(debug) << "Network thread stopped";
  }

  int
  sender_main(int argc, char* argv[]) {
    // Parse arguments
    uint32_t session_id = 0;

    for (int i = 1; i < argc; ++i) {
      std::string arg = argv[i];
      if (arg == "--session" && i + 1 < argc) {
        session_id = std::stoul(argv[++i]);
      }
    }

    if (session_id == 0) {
      std::cerr << "Usage: " << argv[0] << " --session <session_id>" << std::endl;
      return 1;
    }

#ifdef _WIN32
    // Initialize Winsock
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
      std::cerr << "WSAStartup failed" << std::endl;
      return 1;
    }
#endif

    // Create and run sender
    sender_t sender(session_id);
    int result = sender.run();

#ifdef _WIN32
    WSACleanup();
#endif

    return result;
  }

}  // namespace subprocess
