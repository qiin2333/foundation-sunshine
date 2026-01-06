// local includes
#include "session_listener.h"
#include "src/logging.h"

// standard includes
#include <condition_variable>

namespace display_device {

  // Static member initialization
  HWND SessionEventListener::hidden_window_ = nullptr;
  SessionEventListener::UnlockCallback SessionEventListener::pending_callback_;
  std::mutex SessionEventListener::callback_mutex_;
  bool SessionEventListener::initialized_ = false;
  bool SessionEventListener::event_based_ = false;
  std::thread SessionEventListener::message_thread_;
  bool SessionEventListener::thread_running_ = false;

  namespace {
    const wchar_t *WINDOW_CLASS_NAME = L"SunshineSessionListener";
    std::condition_variable init_cv_;
    std::mutex init_mutex_;
    bool init_complete_ = false;
    bool init_success_ = false;
  }

  LRESULT CALLBACK
  SessionEventListener::window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_WTSSESSION_CHANGE) {
      switch (wparam) {
        case WTS_SESSION_UNLOCK:
          BOOST_LOG(info) << "检测到会话解锁事件";
          {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            if (pending_callback_) {
              BOOST_LOG(info) << "执行解锁回调";
              auto callback = std::move(pending_callback_);
              pending_callback_ = nullptr;
              // 在新线程中执行回调，避免阻塞消息循环
              std::thread([callback = std::move(callback)]() {
                callback();
              }).detach();
            }
          }
          break;
        case WTS_SESSION_LOCK:
          BOOST_LOG(debug) << "检测到会话锁定事件";
          break;
        default:
          // 其他事件（远程连接/断开、控制台连接/断开等）不单独处理
          // RDP断开后会进入锁屏状态，等待 WTS_SESSION_UNLOCK 触发回调
          break;
      }
    }
    else if (message == WM_DESTROY) {
      PostQuitMessage(0);
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
  }

  void
  SessionEventListener::message_loop() {
    // 在消息线程中创建窗口
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = window_proc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = WINDOW_CLASS_NAME;

    if (!RegisterClassExW(&wc)) {
      DWORD last_error = GetLastError();
      if (last_error != ERROR_CLASS_ALREADY_EXISTS) {
        BOOST_LOG(error) << "注册窗口类失败: " << last_error;
        {
          std::lock_guard<std::mutex> lock(init_mutex_);
          init_complete_ = true;
          init_success_ = false;
        }
        init_cv_.notify_one();
        return;
      }
    }

    // 创建消息窗口
    hidden_window_ = CreateWindowExW(
      0,
      WINDOW_CLASS_NAME,
      L"SunshineSessionListenerWindow",
      0,
      0, 0, 0, 0,
      HWND_MESSAGE,
      nullptr,
      GetModuleHandle(nullptr),
      nullptr
    );

    if (!hidden_window_) {
      BOOST_LOG(error) << "创建隐藏窗口失败: " << GetLastError();
      {
        std::lock_guard<std::mutex> lock(init_mutex_);
        init_complete_ = true;
        init_success_ = false;
      }
      init_cv_.notify_one();
      return;
    }

    // 注册会话通知
    if (!WTSRegisterSessionNotification(hidden_window_, NOTIFY_FOR_THIS_SESSION)) {
      BOOST_LOG(warning) << "注册会话通知失败: " << GetLastError();
      DestroyWindow(hidden_window_);
      hidden_window_ = nullptr;
      {
        std::lock_guard<std::mutex> lock(init_mutex_);
        init_complete_ = true;
        init_success_ = false;
      }
      init_cv_.notify_one();
      return;
    }

    BOOST_LOG(info) << "会话事件监听器初始化成功（事件驱动）";
    
    {
      std::lock_guard<std::mutex> lock(init_mutex_);
      init_complete_ = true;
      init_success_ = true;
    }
    init_cv_.notify_one();

    // 运行消息循环
    MSG msg;
    while (thread_running_ && GetMessage(&msg, nullptr, 0, 0)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }

    // 清理
    if (hidden_window_) {
      WTSUnRegisterSessionNotification(hidden_window_);
      DestroyWindow(hidden_window_);
      hidden_window_ = nullptr;
    }
    UnregisterClassW(WINDOW_CLASS_NAME, GetModuleHandle(nullptr));
  }

  bool
  SessionEventListener::init(UnlockCallback on_unlock) {
    if (initialized_) {
      return event_based_;
    }

    // 重置初始化状态
    {
      std::lock_guard<std::mutex> lock(init_mutex_);
      init_complete_ = false;
      init_success_ = false;
    }

    // 启动消息线程
    thread_running_ = true;
    message_thread_ = std::thread(message_loop);

    // 等待初始化完成
    {
      std::unique_lock<std::mutex> lock(init_mutex_);
      init_cv_.wait(lock, [] { return init_complete_; });
    }

    initialized_ = true;
    event_based_ = init_success_;

    if (!event_based_) {
      BOOST_LOG(warning) << "事件监听器初始化失败，将使用轮询机制";
      thread_running_ = false;
      if (message_thread_.joinable()) {
        message_thread_.join();
      }
    }

    // 保存初始回调
    if (on_unlock && event_based_) {
      std::lock_guard<std::mutex> lock(callback_mutex_);
      pending_callback_ = std::move(on_unlock);
    }

    return event_based_;
  }

  void
  SessionEventListener::deinit() {
    if (!initialized_) {
      return;
    }

    // 停止消息线程
    thread_running_ = false;
    
    if (hidden_window_) {
      // 发送 WM_QUIT 消息退出消息循环
      PostMessage(hidden_window_, WM_QUIT, 0, 0);
    }

    if (message_thread_.joinable()) {
      message_thread_.join();
    }

    {
      std::lock_guard<std::mutex> lock(callback_mutex_);
      pending_callback_ = nullptr;
    }

    initialized_ = false;
    event_based_ = false;
    BOOST_LOG(info) << "会话事件监听器已清理";
  }

  bool
  SessionEventListener::is_event_based() {
    return event_based_;
  }

  void
  SessionEventListener::register_unlock_callback(UnlockCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    pending_callback_ = std::move(callback);
    BOOST_LOG(debug) << "已注册解锁回调";
  }

  void
  SessionEventListener::clear_unlock_callback() {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    pending_callback_ = nullptr;
    BOOST_LOG(debug) << "已清除解锁回调";
  }

}  // namespace display_device
