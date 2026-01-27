/**
 * @file src/subprocess/subprocess_manager.h
 * @brief Manager for launching and controlling the sender subprocess.
 *
 * This implements the control plane portion of the separated streaming architecture.
 * The main process uses this to launch the subprocess and send control commands.
 */
#pragma once

#include "ipc_channel.h"
#include "ipc_protocol.h"

#include "src/rtsp.h"
#include "src/stream.h"
#include "src/video.h"

#include <memory>
#include <string>
#include <functional>
#include <atomic>
#include <thread>

#ifdef _WIN32
  #include <windows.h>
#endif

namespace subprocess {

  /**
   * @brief Configuration for the subprocess manager.
   */
  struct subprocess_config_t {
    // Path to the sender executable
    std::string sender_path;

    // Whether subprocess mode is enabled
    bool enabled = false;

    // Timeout for subprocess startup (ms)
    uint32_t startup_timeout_ms = 10000;

    // Heartbeat interval (ms)
    uint32_t heartbeat_interval_ms = 1000;
  };

  /**
   * @brief Manages a streaming subprocess (sender).
   *
   * The subprocess manager is responsible for:
   * - Launching the sender subprocess with user token
   * - Establishing IPC communication
   * - Forwarding control commands (IDR requests, bitrate changes)
   * - Monitoring subprocess health
   * - Socket handle handover
   */
  class subprocess_manager_t {
  public:
    /**
     * @brief Callback for subprocess events.
     */
    using event_callback_t = std::function<void(ipc::response_type_e, const std::string&)>;

    /**
     * @brief Construct a subprocess manager.
     * @param config Configuration for the manager.
     */
    explicit subprocess_manager_t(const subprocess_config_t& config);

    /**
     * @brief Destructor - terminates the subprocess if running.
     */
    ~subprocess_manager_t();

    // Non-copyable
    subprocess_manager_t(const subprocess_manager_t&) = delete;
    subprocess_manager_t& operator=(const subprocess_manager_t&) = delete;

    /**
     * @brief Start a streaming session in the subprocess.
     * @param stream_config The stream configuration.
     * @param launch_session The launch session information.
     * @return true if the subprocess was started successfully.
     */
    bool start_session(
      const stream::config_t& stream_config,
      const rtsp_stream::launch_session_t& launch_session
    );

    /**
     * @brief Stop the current streaming session.
     */
    void stop_session();

    /**
     * @brief Request an IDR frame from the encoder.
     */
    void request_idr_frame();

    /**
     * @brief Invalidate reference frames in the encoder.
     * @param first_frame First frame to invalidate.
     * @param last_frame Last frame to invalidate.
     */
    void invalidate_ref_frames(int64_t first_frame, int64_t last_frame);

    /**
     * @brief Change the encoding bitrate dynamically.
     * @param bitrate_kbps New bitrate in Kbps.
     */
    void change_bitrate(int bitrate_kbps);

    /**
     * @brief Pass socket information to the subprocess for RTP sending.
     * @param socket_type Type of socket (0=video, 1=audio).
     * @param socket_handle The native socket handle.
     * @param remote_addr Remote address.
     * @param remote_port Remote port.
     * @param local_addr Local address.
     * @param local_port Local port.
     * @return true if socket info was passed successfully.
     */
    bool pass_socket_info(
      int socket_type,
      uintptr_t socket_handle,
      const std::string& remote_addr,
      uint16_t remote_port,
      const std::string& local_addr,
      uint16_t local_port
    );

    /**
     * @brief Check if the subprocess is running.
     * @return true if the subprocess is active.
     */
    bool is_running() const;

    /**
     * @brief Set callback for subprocess events.
     * @param callback The callback function.
     */
    void set_event_callback(event_callback_t callback);

    /**
     * @brief Get the current session ID.
     * @return The session ID, or 0 if no session is active.
     */
    uint32_t get_session_id() const;

  private:
    /**
     * @brief Launch the sender subprocess.
     * @param session_id The session ID for IPC.
     * @return true if launched successfully.
     */
    bool launch_subprocess(uint32_t session_id);

    /**
     * @brief Terminate the subprocess.
     */
    void terminate_subprocess();

    /**
     * @brief Handle incoming IPC messages from the subprocess.
     * @param header The message header.
     * @param payload The message payload.
     */
    void handle_ipc_message(const ipc::message_header_t& header, const std::vector<uint8_t>& payload);

    /**
     * @brief Heartbeat thread function.
     */
    void heartbeat_thread_func();

    subprocess_config_t config_;
    std::unique_ptr<ipc::ipc_channel_t> ipc_channel_;

    std::atomic<bool> running_;
    std::atomic<uint32_t> session_id_;
    std::atomic<uint32_t> sequence_;

#ifdef _WIN32
    HANDLE process_handle_;
    HANDLE thread_handle_;
#else
    pid_t process_pid_;
#endif

    std::thread heartbeat_thread_;
    std::atomic<bool> heartbeat_running_;

    event_callback_t event_callback_;
  };

  /**
   * @brief Get the global subprocess configuration.
   * @return Reference to the configuration.
   */
  subprocess_config_t& get_subprocess_config();

  /**
   * @brief Check if subprocess mode is enabled.
   * @return true if subprocess mode is enabled.
   */
  bool is_subprocess_mode_enabled();

}  // namespace subprocess
