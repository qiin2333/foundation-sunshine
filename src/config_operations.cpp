/**
 * @file src/config_operations.cpp
 * @brief 配置文件操作（导入/导出/重置）
 * 
 * 该模块提供安全的配置文件操作，可以从 Rust 托盘和 C++ 代码中调用。
 * 
 * 注意：由于 Sunshine 以 SYSTEM 用户身份运行，无法访问普通用户的桌面和快速访问位置。
 * 因此我们使用 FOS_HIDEPINNEDPLACES 隐藏快速访问栏，并手动添加常用导航位置。
 */

#include "config_operations.h"

#include <ctime>
#include <filesystem>
#include <string>
#include <vector>

#if defined(_WIN32)
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  #include <commdlg.h>
  #include <shellapi.h>
  #include <shlobj.h>
  #include <shobjidl.h>
  #include <wtsapi32.h>
#endif

#include <boost/filesystem.hpp>

#include "config.h"
#include "file_handler.h"
#include "logging.h"
#include "platform/common.h"
#include "platform/windows/misc.h"
#include "system_tray_i18n.h"

using namespace std::literals;

namespace config_operations {

  // ============================================================================
  // 常量和全局变量
  // ============================================================================

  /**
   * @brief 文件对话框打开标志，防止多个对话框同时打开
   */
  static bool s_file_dialog_open = false;
  
  /**
   * @brief 最大配置文件大小限制（1MB）
   * 
   * 这是一个安全限制，防止导入超大的配置文件。
   */
  static constexpr size_t MAX_CONFIG_SIZE = 1024 * 1024;

  // ============================================================================
  // 安全验证函数
  // ============================================================================

  /**
   * @brief 验证文件路径是否安全用于导入配置
   * 
   * 进行以下安全检查：
   * - 文件必须存在
   * - 文件扩展名必须是 .conf
   * - 文件不能是符号链接（防止符号链接攻击）
   * - 文件必须是普通文件
   * 
   * @param path 要验证的文件路径
   * @return 如果路径安全返回 true
   */
  static bool is_safe_config_path(const std::string &path) {
    try {
      std::filesystem::path p(path);
      
      // 检查文件是否存在
      if (!std::filesystem::exists(p)) {
        BOOST_LOG(warning) << "[config_ops] 文件不存在: " << path;
        return false;
      }
      
      // 获取规范路径（解析所有符号链接）
      auto canonical_path = std::filesystem::canonical(p);
      
      // 检查扩展名
      if (canonical_path.extension() != ".conf") {
        BOOST_LOG(warning) << "[config_ops] 无效的文件扩展名: " << canonical_path.extension().string();
        return false;
      }
      
      // 检查是否为符号链接
      if (std::filesystem::is_symlink(p)) {
        BOOST_LOG(warning) << "[config_ops] 文件是符号链接，拒绝导入: " << path;
        return false;
      }
      
      // 检查是否为普通文件
      if (!std::filesystem::is_regular_file(canonical_path)) {
        BOOST_LOG(warning) << "[config_ops] 不是普通文件: " << path;
        return false;
      }
      
      return true;
    }
    catch (const std::exception &e) {
      BOOST_LOG(error) << "[config_ops] 路径验证时发生异常: " << e.what();
      return false;
    }
  }

  /**
   * @brief 验证配置文件内容是否安全
   * 
   * 进行以下检查：
   * - 内容大小不能超过 MAX_CONFIG_SIZE
   * - 内容不能为空
   * - 内容必须是有效的配置格式（通过尝试解析验证）
   * 
   * @param content 配置文件内容
   * @return 如果内容安全返回 true
   */
  static bool is_safe_config_content(const std::string &content) {
    // 检查大小限制
    if (content.size() > MAX_CONFIG_SIZE) {
      BOOST_LOG(warning) << "[config_ops] 配置文件过大: " << content.size() << " bytes (最大: " << MAX_CONFIG_SIZE << ")";
      return false;
    }
    
    // 检查是否为空
    if (content.empty()) {
      BOOST_LOG(warning) << "[config_ops] 配置文件为空";
      return false;
    }
    
    // 尝试解析配置以验证格式
    try {
      config::parse_config(content);
      return true;
    }
    catch (const std::exception &e) {
      BOOST_LOG(warning) << "[config_ops] 配置文件格式无效: " << e.what();
      return false;
    }
  }

#ifdef _WIN32
  // ============================================================================
  // Windows 平台特定函数
  // ============================================================================

  /**
   * @brief 获取当前控制台会话登录用户的令牌
   * 
   * 由于 Sunshine 以 SYSTEM 用户运行，我们需要获取实际登录用户的令牌
   * 才能访问其桌面等用户文件夹。
   * 
   * @return 用户令牌句柄，失败返回 NULL。调用者需要调用 CloseHandle 释放。
   */
  static HANDLE get_console_user_token() {
    // 获取活动的控制台会话 ID
    DWORD session_id = WTSGetActiveConsoleSessionId();
    if (session_id == 0xFFFFFFFF) {
      BOOST_LOG(debug) << "[config_ops] 无法获取活动控制台会话";
      return NULL;
    }

    HANDLE user_token = NULL;
    if (!WTSQueryUserToken(session_id, &user_token)) {
      BOOST_LOG(debug) << "[config_ops] 无法获取用户令牌，错误码: " << GetLastError();
      return NULL;
    }

    return user_token;
  }

  /**
   * @brief 为文件对话框添加导航位置
   * 
   * 由于以 SYSTEM 用户运行，快速访问栏无法正常工作（会尝试访问 SYSTEM 用户的
   * 桌面，但该位置不存在）。我们使用 FOS_HIDEPINNEDPLACES 隐藏快速访问栏，
   * 并手动添加有用的导航位置：
   * 
   * 1. 当前登录用户的桌面（如果可以获取）
   * 2. 公共桌面
   * 3. 此电脑
   * 4. 所有驱动器
   * 5. 网络
   * 
   * @param pDialog 文件对话框接口指针
   */
  static void add_dialog_places(IFileDialog *pDialog) {
    // 尝试获取当前登录用户的桌面
    HANDLE user_token = get_console_user_token();
    if (user_token != NULL) {
      PWSTR user_desktop = NULL;
      // 使用用户令牌获取其桌面路径
      if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, KF_FLAG_DEFAULT, user_token, &user_desktop))) {
        IShellItem *psiDesktop = NULL;
        if (SUCCEEDED(SHCreateItemFromParsingName(user_desktop, NULL, IID_PPV_ARGS(&psiDesktop)))) {
          pDialog->AddPlace(psiDesktop, FDAP_TOP);
          psiDesktop->Release();
          BOOST_LOG(debug) << "[config_ops] 已添加用户桌面到导航栏";
        }
        CoTaskMemFree(user_desktop);
      }
      
      // 获取用户的下载文件夹
      PWSTR user_downloads = NULL;
      if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, KF_FLAG_DEFAULT, user_token, &user_downloads))) {
        IShellItem *psiDownloads = NULL;
        if (SUCCEEDED(SHCreateItemFromParsingName(user_downloads, NULL, IID_PPV_ARGS(&psiDownloads)))) {
          pDialog->AddPlace(psiDownloads, FDAP_TOP);
          psiDownloads->Release();
          BOOST_LOG(debug) << "[config_ops] 已添加用户下载文件夹到导航栏";
        }
        CoTaskMemFree(user_downloads);
      }
      
      // 获取用户的文档文件夹
      PWSTR user_documents = NULL;
      if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_DEFAULT, user_token, &user_documents))) {
        IShellItem *psiDocuments = NULL;
        if (SUCCEEDED(SHCreateItemFromParsingName(user_documents, NULL, IID_PPV_ARGS(&psiDocuments)))) {
          pDialog->AddPlace(psiDocuments, FDAP_TOP);
          psiDocuments->Release();
          BOOST_LOG(debug) << "[config_ops] 已添加用户文档文件夹到导航栏";
        }
        CoTaskMemFree(user_documents);
      }
      
      CloseHandle(user_token);
    }
    else {
      BOOST_LOG(debug) << "[config_ops] 无法获取用户令牌，将添加公共桌面作为替代";
      
      // 如果无法获取用户令牌，添加公共桌面
      IShellItem *psiPublicDesktop = NULL;
      if (SUCCEEDED(SHGetKnownFolderItem(FOLDERID_PublicDesktop, KF_FLAG_DEFAULT, NULL, IID_PPV_ARGS(&psiPublicDesktop)))) {
        pDialog->AddPlace(psiPublicDesktop, FDAP_TOP);
        psiPublicDesktop->Release();
        BOOST_LOG(debug) << "[config_ops] 已添加公共桌面到导航栏";
      }
    }

    // 添加"此电脑"到导航栏
    IShellItem *psiComputer = NULL;
    if (SUCCEEDED(SHGetKnownFolderItem(FOLDERID_ComputerFolder, KF_FLAG_DEFAULT, NULL, IID_PPV_ARGS(&psiComputer)))) {
      pDialog->AddPlace(psiComputer, FDAP_TOP);
      psiComputer->Release();
      BOOST_LOG(debug) << "[config_ops] 已添加\"此电脑\"到导航栏";
    }

    // 枚举并添加所有驱动器
    DWORD dwSize = GetLogicalDriveStringsW(0, NULL);
    if (dwSize > 0) {
      std::vector<wchar_t> buffer(dwSize + 1);
      if (GetLogicalDriveStringsW(dwSize, buffer.data())) {
        for (wchar_t* pDrive = buffer.data(); *pDrive; pDrive += wcslen(pDrive) + 1) {
          IShellItem *psiDrive = NULL;
          if (SUCCEEDED(SHCreateItemFromParsingName(pDrive, NULL, IID_PPV_ARGS(&psiDrive)))) {
            pDialog->AddPlace(psiDrive, FDAP_BOTTOM);
            psiDrive->Release();
          }
        }
      }
    }

    // 添加"网络"到导航栏
    IShellItem *psiNetwork = NULL;
    if (SUCCEEDED(SHGetKnownFolderItem(FOLDERID_NetworkFolder, KF_FLAG_DEFAULT, NULL, IID_PPV_ARGS(&psiNetwork)))) {
      pDialog->AddPlace(psiNetwork, FDAP_BOTTOM);
      psiNetwork->Release();
      BOOST_LOG(debug) << "[config_ops] 已添加\"网络\"到导航栏";
    }
  }

  /**
   * @brief 显示文件打开对话框
   * 
   * 使用 Windows IFileOpenDialog COM 接口显示现代文件打开对话框。
   * 
   * @return 用户选择的文件路径（宽字符串），如果取消则返回空字符串
   */
  static std::wstring show_open_file_dialog() {
    std::wstring result;
    
    // 初始化 COM
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    bool com_initialized = SUCCEEDED(hr);

    // 创建文件打开对话框
    IFileOpenDialog *pFileOpen = nullptr;
    hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL,
                          IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen));
    
    if (FAILED(hr)) {
      BOOST_LOG(error) << "[config_ops] 创建文件打开对话框失败，HRESULT: " << std::hex << hr;
      if (com_initialized) CoUninitialize();
      return result;
    }

    // 设置对话框选项
    // FOS_HIDEPINNEDPLACES: 隐藏快速访问栏（因为以 SYSTEM 用户运行无法访问）
    // FOS_FORCEFILESYSTEM: 只允许选择文件系统中的项目
    // FOS_DONTADDTORECENT: 不添加到最近使用列表
    // FOS_NOCHANGEDIR: 不改变当前工作目录
    // FOS_NOVALIDATE: 允许选择不存在的文件名（我们自己验证）
    DWORD dwFlags;
    pFileOpen->GetOptions(&dwFlags);
    pFileOpen->SetOptions(dwFlags | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_FILEMUSTEXIST | 
                          FOS_DONTADDTORECENT | FOS_NOCHANGEDIR | FOS_HIDEPINNEDPLACES | FOS_NOVALIDATE);

    // 设置文件类型过滤器
    std::wstring config_label = system_tray_i18n::utf8_to_wstring(
        system_tray_i18n::get_localized_string(system_tray_i18n::KEY_FILE_DIALOG_CONFIG_FILES));
    std::wstring all_files_label = system_tray_i18n::utf8_to_wstring(
        system_tray_i18n::get_localized_string(system_tray_i18n::KEY_FILE_DIALOG_ALL_FILES));
    
    COMDLG_FILTERSPEC fileTypes[] = {
      { config_label.c_str(), L"*.conf" },
      { all_files_label.c_str(), L"*.*" }
    };
    pFileOpen->SetFileTypes(2, fileTypes);
    pFileOpen->SetFileTypeIndex(1);  // 默认选择 .conf 过滤器

    // 设置对话框标题
    std::wstring dialog_title = system_tray_i18n::utf8_to_wstring(
        system_tray_i18n::get_localized_string(system_tray_i18n::KEY_FILE_DIALOG_SELECT_IMPORT));
    pFileOpen->SetTitle(dialog_title.c_str());

    // 设置默认文件夹为应用程序数据目录
    IShellItem *psiDefault = NULL;
    std::wstring default_path = platf::appdata().wstring();
    if (SUCCEEDED(SHCreateItemFromParsingName(default_path.c_str(), NULL, IID_PPV_ARGS(&psiDefault)))) {
      pFileOpen->SetFolder(psiDefault);
      psiDefault->Release();
    }

    // 添加导航位置（驱动器、此电脑、网络等）
    add_dialog_places(pFileOpen);

    // 显示对话框
    hr = pFileOpen->Show(NULL);
    if (SUCCEEDED(hr)) {
      // 获取用户选择的文件
      IShellItem *pItem = nullptr;
      hr = pFileOpen->GetResult(&pItem);
      if (SUCCEEDED(hr)) {
        PWSTR pszFilePath = nullptr;
        hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
        if (SUCCEEDED(hr)) {
          result = pszFilePath;
          CoTaskMemFree(pszFilePath);
        }
        pItem->Release();
      }
    }

    pFileOpen->Release();
    if (com_initialized) CoUninitialize();
    
    return result;
  }

  /**
   * @brief 显示文件保存对话框
   * 
   * 使用 Windows IFileSaveDialog COM 接口显示现代文件保存对话框。
   * 
   * @return 用户选择的保存路径（宽字符串），如果取消则返回空字符串
   */
  static std::wstring show_save_file_dialog() {
    std::wstring result;
    
    // 初始化 COM
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    bool com_initialized = SUCCEEDED(hr);

    // 创建文件保存对话框
    IFileSaveDialog *pFileSave = nullptr;
    hr = CoCreateInstance(CLSID_FileSaveDialog, NULL, CLSCTX_ALL,
                          IID_IFileSaveDialog, reinterpret_cast<void**>(&pFileSave));
    
    if (FAILED(hr)) {
      BOOST_LOG(error) << "[config_ops] 创建文件保存对话框失败，HRESULT: " << std::hex << hr;
      if (com_initialized) CoUninitialize();
      return result;
    }

    // 设置对话框选项
    DWORD dwFlags;
    pFileSave->GetOptions(&dwFlags);
    pFileSave->SetOptions(dwFlags | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_OVERWRITEPROMPT |
                          FOS_DONTADDTORECENT | FOS_NOCHANGEDIR | FOS_HIDEPINNEDPLACES | FOS_NOVALIDATE);

    // 设置文件类型过滤器
    std::wstring config_label = system_tray_i18n::utf8_to_wstring(
        system_tray_i18n::get_localized_string(system_tray_i18n::KEY_FILE_DIALOG_CONFIG_FILES));
    std::wstring all_files_label = system_tray_i18n::utf8_to_wstring(
        system_tray_i18n::get_localized_string(system_tray_i18n::KEY_FILE_DIALOG_ALL_FILES));
    
    COMDLG_FILTERSPEC fileTypes[] = {
      { config_label.c_str(), L"*.conf" },
      { all_files_label.c_str(), L"*.*" }
    };
    pFileSave->SetFileTypes(2, fileTypes);
    pFileSave->SetFileTypeIndex(1);

    // 设置默认扩展名
    pFileSave->SetDefaultExtension(L"conf");

    // 生成默认文件名（带时间戳）
    std::string default_name = "sunshine_config_" + std::to_string(std::time(nullptr)) + ".conf";
    std::wstring wdefault_name(default_name.begin(), default_name.end());
    pFileSave->SetFileName(wdefault_name.c_str());

    // 设置对话框标题
    std::wstring dialog_title = system_tray_i18n::utf8_to_wstring(
        system_tray_i18n::get_localized_string(system_tray_i18n::KEY_FILE_DIALOG_SAVE_EXPORT));
    pFileSave->SetTitle(dialog_title.c_str());

    // 设置默认文件夹为应用程序数据目录
    IShellItem *psiDefault = NULL;
    std::wstring default_path = platf::appdata().wstring();
    if (SUCCEEDED(SHCreateItemFromParsingName(default_path.c_str(), NULL, IID_PPV_ARGS(&psiDefault)))) {
      pFileSave->SetFolder(psiDefault);
      psiDefault->Release();
    }

    // 添加导航位置
    add_dialog_places(pFileSave);

    // 显示对话框
    hr = pFileSave->Show(NULL);
    if (SUCCEEDED(hr)) {
      // 获取用户选择的保存路径
      IShellItem *pItem = nullptr;
      hr = pFileSave->GetResult(&pItem);
      if (SUCCEEDED(hr)) {
        PWSTR pszFilePath = nullptr;
        hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
        if (SUCCEEDED(hr)) {
          result = pszFilePath;
          CoTaskMemFree(pszFilePath);
        }
        pItem->Release();
      }
    }

    pFileSave->Release();
    if (com_initialized) CoUninitialize();
    
    return result;
  }

  /**
   * @brief 显示消息框
   * 
   * @param title_key 标题的本地化键
   * @param msg_key 消息的本地化键
   * @param is_error 是否为错误消息（决定图标类型）
   */
  static void show_message(const std::string &title_key, const std::string &msg_key, bool is_error) {
    std::wstring title = system_tray_i18n::utf8_to_wstring(
        system_tray_i18n::get_localized_string(title_key));
    std::wstring message = system_tray_i18n::utf8_to_wstring(
        system_tray_i18n::get_localized_string(msg_key));
    
    UINT type = is_error ? (MB_OK | MB_ICONERROR) : (MB_OK | MB_ICONINFORMATION);
    MessageBoxW(NULL, message.c_str(), title.c_str(), type);
  }

  /**
   * @brief 显示带有自定义消息的消息框
   * 
   * @param title_key 标题的本地化键
   * @param message 自定义消息（宽字符串）
   * @param is_error 是否为错误消息
   */
  static void show_message_custom(const std::string &title_key, const std::wstring &message, bool is_error) {
    std::wstring title = system_tray_i18n::utf8_to_wstring(
        system_tray_i18n::get_localized_string(title_key));
    
    UINT type = is_error ? (MB_OK | MB_ICONERROR) : (MB_OK | MB_ICONINFORMATION);
    MessageBoxW(NULL, message.c_str(), title.c_str(), type);
  }

  /**
   * @brief 显示确认对话框
   * 
   * @param title_key 标题的本地化键
   * @param msg_key 消息的本地化键
   * @return 如果用户点击"是"返回 true
   */
  static bool show_confirm(const std::string &title_key, const std::string &msg_key) {
    std::wstring title = system_tray_i18n::utf8_to_wstring(
        system_tray_i18n::get_localized_string(title_key));
    std::wstring message = system_tray_i18n::utf8_to_wstring(
        system_tray_i18n::get_localized_string(msg_key));
    
    int result = MessageBoxW(NULL, message.c_str(), title.c_str(), MB_YESNO | MB_ICONQUESTION);
    return result == IDYES;
  }

  /**
   * @brief 显示带有自定义消息的确认对话框
   * 
   * @param title_key 标题的本地化键
   * @param message 自定义消息（宽字符串）
   * @return 如果用户点击"是"返回 true
   */
  static bool show_confirm_custom(const std::string &title_key, const std::wstring &message) {
    std::wstring title = system_tray_i18n::utf8_to_wstring(
        system_tray_i18n::get_localized_string(title_key));
    
    int result = MessageBoxW(NULL, message.c_str(), title.c_str(), MB_YESNO | MB_ICONQUESTION);
    return result == IDYES;
  }
#endif

  // ============================================================================
  // 公共接口函数
  // ============================================================================

  /**
   * @brief 导入配置文件
   * 
   * 显示文件选择对话框让用户选择要导入的配置文件。
   * 导入前会进行安全验证，并创建当前配置的备份。
   * 导入成功后询问用户是否重启 Sunshine 以应用新配置。
   */
  void import_config() {
    BOOST_LOG(info) << "[config_ops] ========== import_config() 被调用 =========="sv;
#ifdef _WIN32
    // 检查是否已有文件对话框打开
    if (s_file_dialog_open) {
      BOOST_LOG(warning) << "[config_ops] 已有文件对话框打开，跳过此次调用";
      return;
    }
    s_file_dialog_open = true;
    BOOST_LOG(debug) << "[config_ops] 设置文件对话框标志为 true"sv;

    BOOST_LOG(info) << "[config_ops] 准备显示文件打开对话框..."sv;

    // 显示文件打开对话框
    std::wstring file_path_wide = show_open_file_dialog();
    
    // 重置文件对话框标志
    s_file_dialog_open = false;
    BOOST_LOG(debug) << "[config_ops] 重置文件对话框标志为 false"sv;

    // 检查用户是否取消
    if (file_path_wide.empty()) {
      BOOST_LOG(info) << "[config_ops] 用户取消了文件对话框"sv;
      return;
    }

    std::string file_path = platf::to_utf8(file_path_wide);

    // 安全验证：检查文件路径
    if (!is_safe_config_path(file_path)) {
      BOOST_LOG(error) << "[config_ops] 配置导入被拒绝: 不安全的文件路径: " << file_path;
      show_message_custom(system_tray_i18n::KEY_IMPORT_ERROR_TITLE, 
                          L"文件路径不安全或文件类型无效。\n只允许 .conf 文件，不允许符号链接。", true);
      return;
    }

    try {
      // 读取配置文件内容
      std::string config_content = file_handler::read_file(file_path.c_str());

      // 安全验证：检查配置内容
      if (!is_safe_config_content(config_content)) {
        BOOST_LOG(error) << "[config_ops] 配置导入被拒绝: 不安全的内容: " << file_path;
        show_message_custom(system_tray_i18n::KEY_IMPORT_ERROR_TITLE,
                            L"配置文件内容无效、太大或格式错误。\n最大文件大小：1MB", true);
        return;
      }

      // 备份当前配置（检查是否成功）
      std::string backup_path = config::sunshine.config_file + ".backup";
      std::string current_config = file_handler::read_file(config::sunshine.config_file.c_str());
      int backup_result = file_handler::write_file(backup_path.c_str(), current_config);
      
      if (backup_result != 0) {
        BOOST_LOG(error) << "[config_ops] 创建备份失败，中止导入";
        show_message_custom(system_tray_i18n::KEY_IMPORT_ERROR_TITLE,
                            L"无法创建配置备份，导入操作已中止。", true);
        return;
      }

      BOOST_LOG(info) << "[config_ops] 配置备份已创建: " << backup_path;

      // 使用临时文件确保原子性写入
      std::string temp_path = config::sunshine.config_file + ".tmp";
      int temp_result = file_handler::write_file(temp_path.c_str(), config_content);
      
      if (temp_result != 0) {
        BOOST_LOG(error) << "[config_ops] 写入临时配置文件失败";
        show_message(system_tray_i18n::KEY_IMPORT_ERROR_TITLE, system_tray_i18n::KEY_IMPORT_ERROR_WRITE, true);
        return;
      }

      // 原子性替换：重命名临时文件为实际配置文件
      try {
        std::filesystem::rename(temp_path, config::sunshine.config_file);
        BOOST_LOG(info) << "[config_ops] 配置导入成功: " << file_path;
        
        // 询问用户是否重启 Sunshine 以应用新配置
        if (show_confirm_custom(system_tray_i18n::KEY_IMPORT_SUCCESS_TITLE,
                                L"配置导入成功！\n\n是否立即重启 Sunshine 以应用新配置？")) {
          BOOST_LOG(info) << "[config_ops] 用户选择重启 Sunshine"sv;
          platf::restart();
        }
        else {
          BOOST_LOG(info) << "[config_ops] 用户选择不重启 Sunshine"sv;
        }
      }
      catch (const std::exception &e) {
        BOOST_LOG(error) << "[config_ops] 重命名临时文件失败: " << e.what();
        // 清理临时文件
        std::filesystem::remove(temp_path);
        show_message(system_tray_i18n::KEY_IMPORT_ERROR_TITLE, system_tray_i18n::KEY_IMPORT_ERROR_WRITE, true);
      }
    }
    catch (const std::exception &e) {
      BOOST_LOG(error) << "[config_ops] 配置导入时发生异常: " << e.what();
      show_message(system_tray_i18n::KEY_IMPORT_ERROR_TITLE, system_tray_i18n::KEY_IMPORT_ERROR_EXCEPTION, true);
    }
#else
    // 非 Windows 平台的实现（可以后续添加）
    BOOST_LOG(info) << "[config_ops] 该平台尚未实现配置导入功能";
#endif
  }

  /**
   * @brief 导出配置文件
   * 
   * 显示文件保存对话框让用户选择导出位置。
   * 导出时会进行基本的安全验证。
   * 使用原子性写入确保文件完整性。
   */
  void export_config() {
    BOOST_LOG(info) << "[config_ops] ========== export_config() 被调用 =========="sv;
#ifdef _WIN32
    // 检查是否已有文件对话框打开
    if (s_file_dialog_open) {
      BOOST_LOG(warning) << "[config_ops] 已有文件对话框打开，跳过此次调用";
      return;
    }
    s_file_dialog_open = true;
    BOOST_LOG(debug) << "[config_ops] 设置文件对话框标志为 true"sv;

    BOOST_LOG(info) << "[config_ops] 准备显示文件保存对话框..."sv;

    // 显示文件保存对话框
    std::wstring file_path_wide = show_save_file_dialog();
    
    // 重置文件对话框标志
    s_file_dialog_open = false;
    BOOST_LOG(debug) << "[config_ops] 重置文件对话框标志为 false"sv;

    // 检查用户是否取消
    if (file_path_wide.empty()) {
      BOOST_LOG(info) << "[config_ops] 用户取消了文件对话框"sv;
      return;
    }

    std::string file_path = platf::to_utf8(file_path_wide);
    BOOST_LOG(info) << "[config_ops] 用户选择的导出路径: " << file_path;

    // 安全验证：检查输出文件路径（基本检查）
    try {
      std::filesystem::path p(file_path);
      
      // 检查文件扩展名
      if (p.extension() != ".conf") {
        BOOST_LOG(warning) << "[config_ops] 配置导出被拒绝: 无效的扩展名: " << p.extension().string();
        show_message_custom(system_tray_i18n::KEY_EXPORT_ERROR_TITLE,
                            L"只允许导出为 .conf 文件。", true);
        return;
      }

      // 如果文件已存在，检查是否为符号链接
      if (std::filesystem::exists(p) && std::filesystem::is_symlink(p)) {
        BOOST_LOG(warning) << "[config_ops] 配置导出被拒绝: 目标是符号链接: " << file_path;
        show_message_custom(system_tray_i18n::KEY_EXPORT_ERROR_TITLE,
                            L"不允许导出到符号链接。", true);
        return;
      }
    }
    catch (const std::exception &e) {
      BOOST_LOG(error) << "[config_ops] 导出时路径验证错误: " << e.what();
      show_message_custom(system_tray_i18n::KEY_EXPORT_ERROR_TITLE,
                          L"文件路径无效。", true);
      return;
    }

    try {
      // 读取当前配置
      std::string config_content = file_handler::read_file(config::sunshine.config_file.c_str());
      if (config_content.empty()) {
        BOOST_LOG(error) << "[config_ops] 没有可导出的配置";
        show_message(system_tray_i18n::KEY_EXPORT_ERROR_TITLE, system_tray_i18n::KEY_EXPORT_ERROR_NO_CONFIG, true);
        return;
      }

      // 使用临时文件确保原子性写入
      std::string temp_path = file_path + ".tmp";
      int temp_result = file_handler::write_file(temp_path.c_str(), config_content);
      
      if (temp_result != 0) {
        BOOST_LOG(error) << "[config_ops] 写入临时导出文件失败";
        show_message(system_tray_i18n::KEY_EXPORT_ERROR_TITLE, system_tray_i18n::KEY_EXPORT_ERROR_WRITE, true);
        return;
      }

      // 原子性替换
      try {
        std::filesystem::rename(temp_path, file_path);
        BOOST_LOG(info) << "[config_ops] 配置导出成功: " << file_path;
        show_message(system_tray_i18n::KEY_EXPORT_SUCCESS_TITLE, system_tray_i18n::KEY_EXPORT_SUCCESS_MSG, false);
      }
      catch (const std::exception &e) {
        BOOST_LOG(error) << "[config_ops] 重命名临时导出文件失败: " << e.what();
        // 清理临时文件
        std::filesystem::remove(temp_path);
        show_message(system_tray_i18n::KEY_EXPORT_ERROR_TITLE, system_tray_i18n::KEY_EXPORT_ERROR_WRITE, true);
      }
    }
    catch (const std::exception &e) {
      BOOST_LOG(error) << "[config_ops] 配置导出时发生异常: " << e.what();
      show_message(system_tray_i18n::KEY_EXPORT_ERROR_TITLE, system_tray_i18n::KEY_EXPORT_ERROR_EXCEPTION, true);
    }
#else
    // 非 Windows 平台的实现
    BOOST_LOG(info) << "[config_ops] 该平台尚未实现配置导出功能";
#endif
  }

  /**
   * @brief 重置配置为默认值
   * 
   * 显示确认对话框，如果用户确认则：
   * 1. 备份当前配置
   * 2. 写入默认配置
   * 3. 询问是否重启 Sunshine
   */
  void reset_config() {
    BOOST_LOG(info) << "[config_ops] ========== reset_config() 被调用 =========="sv;
#ifdef _WIN32
    BOOST_LOG(info) << "[config_ops] 准备显示重置确认对话框..."sv;

    // 显示确认对话框
    if (!show_confirm(system_tray_i18n::KEY_RESET_CONFIRM_TITLE, system_tray_i18n::KEY_RESET_CONFIRM_MSG)) {
      BOOST_LOG(info) << "[config_ops] 用户取消了配置重置"sv;
      return;
    }

    BOOST_LOG(info) << "[config_ops] 用户确认重置配置"sv;

    try {
      // 先创建备份
      std::string backup_path = config::sunshine.config_file + ".backup";
      std::string current_config = file_handler::read_file(config::sunshine.config_file.c_str());
      file_handler::write_file(backup_path.c_str(), current_config);
      BOOST_LOG(info) << "[config_ops] 配置备份已创建: " << backup_path;

      // 写入默认配置
      std::string default_config = "# Sunshine Configuration\n# Reset to default\n";
      if (file_handler::write_file(config::sunshine.config_file.c_str(), default_config) != 0) {
        BOOST_LOG(error) << "[config_ops] 重置配置失败";
        show_message(system_tray_i18n::KEY_RESET_ERROR_TITLE, system_tray_i18n::KEY_RESET_ERROR_MSG, true);
        return;
      }

      BOOST_LOG(info) << "[config_ops] 配置重置成功";
      
      // 询问用户是否重启
      if (show_confirm(system_tray_i18n::KEY_RESET_SUCCESS_TITLE, system_tray_i18n::KEY_RESET_SUCCESS_MSG)) {
        BOOST_LOG(info) << "[config_ops] 用户选择重启 Sunshine"sv;
        platf::restart();
      }
    }
    catch (const std::exception &e) {
      BOOST_LOG(error) << "[config_ops] 配置重置时发生异常: " << e.what();
      show_message(system_tray_i18n::KEY_RESET_ERROR_TITLE, system_tray_i18n::KEY_RESET_ERROR_EXCEPTION, true);
    }
#else
    // 非 Windows 平台的实现
    BOOST_LOG(info) << "[config_ops] 该平台尚未实现配置重置功能";
#endif
  }

}  // namespace config_operations
