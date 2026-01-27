/**
 * @file src/subprocess/ipc_channel.h
 * @brief IPC channel for communication between main process and subprocess.
 *
 * This provides a named pipe-based communication channel for the control plane
 * to send commands to the data plane subprocess.
 */
#pragma once

#include "ipc_protocol.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace subprocess {
namespace ipc {

  /**
   * @brief Callback type for receiving IPC messages.
   */
  using message_callback_t = std::function<void(const message_header_t&, const std::vector<uint8_t>&)>;

  /**
   * @brief IPC channel for subprocess communication.
   *
   * The main process creates a server channel that listens for connections.
   * The subprocess creates a client channel that connects to the server.
   */
  class ipc_channel_t {
  public:
    /**
     * @brief Construct an IPC channel.
     * @param session_id Unique identifier for this streaming session.
     */
    explicit ipc_channel_t(uint32_t session_id);

    /**
     * @brief Destructor - closes the channel.
     */
    ~ipc_channel_t();

    // Non-copyable
    ipc_channel_t(const ipc_channel_t&) = delete;
    ipc_channel_t& operator=(const ipc_channel_t&) = delete;

    // Movable
    ipc_channel_t(ipc_channel_t&&) noexcept;
    ipc_channel_t& operator=(ipc_channel_t&&) noexcept;

    /**
     * @brief Create a server-side channel (for main process).
     * @return true if the channel was created successfully.
     */
    bool create_server();

    /**
     * @brief Connect to an existing channel (for subprocess).
     * @param timeout_ms Timeout in milliseconds.
     * @return true if connected successfully.
     */
    bool connect_client(uint32_t timeout_ms = 5000);

    /**
     * @brief Wait for a client to connect (for server).
     * @param timeout_ms Timeout in milliseconds.
     * @return true if a client connected.
     */
    bool wait_for_client(uint32_t timeout_ms = 30000);

    /**
     * @brief Close the channel.
     */
    void close();

    /**
     * @brief Check if the channel is connected.
     * @return true if connected.
     */
    bool is_connected() const;

    /**
     * @brief Send a message through the channel.
     * @param header The message header.
     * @param payload Optional payload data.
     * @return true if sent successfully.
     */
    bool send(const message_header_t& header, const void* payload = nullptr);

    /**
     * @brief Send a message with a typed payload.
     * @tparam T The payload type.
     * @param type The message type.
     * @param sequence The sequence number.
     * @param payload The payload data.
     * @return true if sent successfully.
     */
    template <typename T>
    bool send_message(uint16_t type, uint32_t sequence, const T& payload) {
      auto header = make_header(type, sequence, sizeof(T));
      return send(header, &payload);
    }

    /**
     * @brief Send a message without payload.
     * @param type The message type.
     * @param sequence The sequence number.
     * @return true if sent successfully.
     */
    bool send_command(uint16_t type, uint32_t sequence = 0) {
      auto header = make_header(type, sequence, 0);
      return send(header, nullptr);
    }

    /**
     * @brief Receive a message from the channel.
     * @param header Output: the message header.
     * @param payload Output: the payload data (if any).
     * @param timeout_ms Timeout in milliseconds.
     * @return true if a message was received.
     */
    bool receive(message_header_t& header, std::vector<uint8_t>& payload, uint32_t timeout_ms = 1000);

    /**
     * @brief Start an asynchronous receive loop.
     * @param callback Callback for received messages.
     */
    void start_receive_loop(message_callback_t callback);

    /**
     * @brief Stop the receive loop.
     */
    void stop_receive_loop();

    /**
     * @brief Get the pipe name for this channel.
     * @return The pipe name.
     */
    std::string get_pipe_name() const;

  private:
    uint32_t session_id_;
    std::string pipe_name_;

#ifdef _WIN32
    void* pipe_handle_;  // HANDLE
#else
    int pipe_fd_;
#endif

    std::atomic<bool> connected_;
    std::atomic<bool> receive_loop_running_;
    std::thread receive_thread_;
    std::mutex send_mutex_;
  };

  /**
   * @brief Create a unique pipe name for a session.
   * @param session_id The session ID.
   * @return The pipe name.
   */
  std::string make_pipe_name(uint32_t session_id);

}  // namespace ipc
}  // namespace subprocess
