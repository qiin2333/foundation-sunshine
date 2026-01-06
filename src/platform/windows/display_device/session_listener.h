#pragma once

// Windows includes must come first
#include <windows.h>

// standard includes
#include <functional>
#include <mutex>
#include <thread>

// lib includes
#include <wtsapi32.h>

namespace display_device {

  /**
   * @brief Listens for Windows session events (lock/unlock).
   * Uses WTSRegisterSessionNotification for event-based detection.
   * Falls back to polling if event registration fails.
   */
  class SessionEventListener {
  public:
    using UnlockCallback = std::function<void()>;

    /**
     * @brief Initialize the session event listener.
     * @param on_unlock Callback to execute when session is unlocked.
     * @returns True if event-based listening was successfully registered,
     *          false if fallback to polling is needed.
     */
    static bool
    init(UnlockCallback on_unlock);

    /**
     * @brief Cleanup and unregister the session event listener.
     */
    static void
    deinit();

    /**
     * @brief Check if event-based listening is active.
     * @returns True if using event-based listening, false if using polling fallback.
     */
    static bool
    is_event_based();

    /**
     * @brief Register a one-time callback to be called on next unlock.
     * @param callback Function to call when unlocked.
     */
    static void
    register_unlock_callback(UnlockCallback callback);

    /**
     * @brief Clear any pending unlock callback.
     */
    static void
    clear_unlock_callback();

  private:
    static HWND hidden_window_;
    static UnlockCallback pending_callback_;
    static std::mutex callback_mutex_;
    static bool initialized_;
    static bool event_based_;
    static std::thread message_thread_;
    static bool thread_running_;

    static LRESULT CALLBACK
    window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    
    static void
    message_loop();
  };

}  // namespace display_device
