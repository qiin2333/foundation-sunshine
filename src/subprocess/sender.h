/**
 * @file src/subprocess/sender.h
 * @brief Sender subprocess - the data plane component.
 *
 * This implements the data plane portion of the separated streaming architecture.
 * The sender subprocess handles:
 * - Desktop capture (WGC on Windows)
 * - Hardware encoding (NVENC/AMF/QSV)
 * - RTP packet sending directly to clients
 * - Audio capture and encoding
 */
#pragma once

#include "ipc_channel.h"
#include "ipc_protocol.h"

#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace subprocess {

  /**
   * @brief The sender subprocess main class.
   *
   * This class implements the "streaming engine" that runs in a separate
   * process with user permissions, handling the entire capture -> encode -> send
   * pipeline without cross-process data transfer.
   */
  class sender_t {
  public:
    /**
     * @brief Construct a sender.
     * @param session_id The session ID for IPC communication.
     */
    explicit sender_t(uint32_t session_id);

    /**
     * @brief Destructor.
     */
    ~sender_t();

    // Non-copyable
    sender_t(const sender_t&) = delete;
    sender_t& operator=(const sender_t&) = delete;

    /**
     * @brief Run the sender main loop.
     * @return Exit code (0 for success).
     */
    int run();

    /**
     * @brief Request shutdown.
     */
    void shutdown();

  private:
    /**
     * @brief Connect to the main process via IPC.
     * @return true if connected successfully.
     */
    bool connect_to_main_process();

    /**
     * @brief Handle incoming IPC commands.
     * @param header The message header.
     * @param payload The message payload.
     */
    void handle_command(const ipc::message_header_t& header, const std::vector<uint8_t>& payload);

    /**
     * @brief Initialize the streaming session with the given configuration.
     * @param init The initialization payload.
     * @return true if initialized successfully.
     */
    bool initialize_session(const ipc::init_payload_t& init);

    /**
     * @brief Start the capture and streaming pipeline.
     * @return true if started successfully.
     */
    bool start_streaming();

    /**
     * @brief Stop the streaming pipeline.
     */
    void stop_streaming();

    /**
     * @brief Send an OK response.
     * @param sequence The sequence number to respond to.
     */
    void send_ok(uint32_t sequence);

    /**
     * @brief Send an error response.
     * @param sequence The sequence number to respond to.
     * @param error_code The error code.
     * @param message The error message.
     */
    void send_error(uint32_t sequence, int error_code, const std::string& message);

    /**
     * @brief Video capture and encoding thread.
     */
    void video_thread_func();

    /**
     * @brief Audio capture and encoding thread.
     */
    void audio_thread_func();

    /**
     * @brief Network send thread.
     */
    void network_thread_func();

    uint32_t session_id_;
    std::unique_ptr<ipc::ipc_channel_t> ipc_channel_;

    std::atomic<bool> running_;
    std::atomic<bool> streaming_;

    // Session configuration (populated from init command)
    ipc::init_payload_t session_config_;

    // Worker threads
    std::thread video_thread_;
    std::thread audio_thread_;
    std::thread network_thread_;

    // Socket handles for direct sending
    uintptr_t video_socket_;
    uintptr_t audio_socket_;
  };

  /**
   * @brief Entry point for the sender subprocess.
   * @param argc Argument count.
   * @param argv Argument values.
   * @return Exit code.
   */
  int sender_main(int argc, char* argv[]);

}  // namespace subprocess
