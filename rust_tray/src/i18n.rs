//! Internationalization (i18n) module for tray icon
//! 
//! Supports Chinese, English, and Japanese translations.

use std::collections::HashMap;
use parking_lot::RwLock;
use once_cell::sync::Lazy;

/// Supported locales
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum Locale {
    English,
    Chinese,
    Japanese,
}

impl Default for Locale {
    fn default() -> Self {
        Locale::English
    }
}

impl From<&str> for Locale {
    fn from(s: &str) -> Self {
        match s.to_lowercase().as_str() {
            "zh" | "zh_cn" | "zh_tw" | "chinese" => Locale::Chinese,
            "ja" | "ja_jp" | "japanese" => Locale::Japanese,
            _ => Locale::English,
        }
    }
}

/// String keys for localization
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum StringKey {
    // Menu items
    OpenSunshine,
    // VDD submenu
    VddBaseDisplay,
    VddCreate,
    VddClose,
    VddPersistent,
    VddPersistentConfirmTitle,
    VddPersistentConfirmMsg,
    // Advanced Settings submenu
    AdvancedSettings,
    ImportConfig,
    ExportConfig,
    ResetToDefault,
    CloseApp,
    CloseAppConfirmTitle,
    CloseAppConfirmMsg,
    // Language submenu
    Language,
    Chinese,
    English,
    Japanese,
    StarProject,
    // Visit Project submenu
    VisitProject,
    VisitProjectSunshine,
    VisitProjectMoonlight,
    ResetDisplayDeviceConfig,
    ResetDisplayConfirmTitle,
    ResetDisplayConfirmMsg,
    Restart,
    Quit,
    
    // Notifications
    StreamStarted,
    StreamingStartedFor,
    StreamPaused,
    StreamingPausedFor,
    ApplicationStopped,
    ApplicationStoppedMsg,
    IncomingPairingRequest,
    ClickToCompletePairing,
    
    // Dialog messages
    QuitTitle,
    QuitMessage,
    ErrorTitle,
    ErrorNoUserSession,
    ImportSuccessTitle,
    ImportSuccessMsg,
    ImportErrorTitle,
    ImportErrorWrite,
    ImportErrorRead,
    ImportErrorException,
    ExportSuccessTitle,
    ExportSuccessMsg,
    ExportErrorTitle,
    ExportErrorWrite,
    ExportErrorNoConfig,
    ExportErrorException,
    ResetConfirmTitle,
    ResetConfirmMsg,
    ResetSuccessTitle,
    ResetSuccessMsg,
    ResetErrorTitle,
    ResetErrorMsg,
    ResetErrorException,
    FileDialogSelectImport,
    FileDialogSaveExport,
    FileDialogConfigFiles,
    FileDialogAllFiles,
}

/// Current locale storage
static CURRENT_LOCALE: Lazy<RwLock<Locale>> = Lazy::new(|| RwLock::new(Locale::English));

/// Translation tables
static TRANSLATIONS: Lazy<HashMap<(Locale, StringKey), &'static str>> = Lazy::new(|| {
    let mut m = HashMap::new();
    
    // English translations
    m.insert((Locale::English, StringKey::OpenSunshine), "Open Sunshine");
    // VDD submenu
    m.insert((Locale::English, StringKey::VddBaseDisplay), "Foundation Display");
    m.insert((Locale::English, StringKey::VddCreate), "Create");
    m.insert((Locale::English, StringKey::VddClose), "Close");
    m.insert((Locale::English, StringKey::VddPersistent), "Keep Enabled");
    m.insert((Locale::English, StringKey::VddPersistentConfirmTitle), "Enable Keep VDD Mode");
    m.insert((Locale::English, StringKey::VddPersistentConfirmMsg), "Enabling this mode will keep the virtual display active at all times.\n\nAre you sure you want to enable it?");
    // Advanced Settings submenu
    m.insert((Locale::English, StringKey::AdvancedSettings), "Advanced Settings");
    m.insert((Locale::English, StringKey::ImportConfig), "Import Config");
    m.insert((Locale::English, StringKey::ExportConfig), "Export Config");
    m.insert((Locale::English, StringKey::ResetToDefault), "Reset to Default");
    m.insert((Locale::English, StringKey::CloseApp), "Clear Cache");
    m.insert((Locale::English, StringKey::CloseAppConfirmTitle), "Clear Cache");
    m.insert((Locale::English, StringKey::CloseAppConfirmMsg), "This will terminate the current streaming application.\n\nAre you sure you want to continue?");
    m.insert((Locale::English, StringKey::Language), "Language");
    m.insert((Locale::English, StringKey::Chinese), "中文");
    m.insert((Locale::English, StringKey::English), "English");
    m.insert((Locale::English, StringKey::Japanese), "日本語");
    m.insert((Locale::English, StringKey::StarProject), "Star Project");
    m.insert((Locale::English, StringKey::VisitProject), "Visit Project");
    m.insert((Locale::English, StringKey::VisitProjectSunshine), "Sunshine-Foundation");
    m.insert((Locale::English, StringKey::VisitProjectMoonlight), "Moonlight-vplus");
    m.insert((Locale::English, StringKey::ResetDisplayDeviceConfig), "Reset Display Memory");
    m.insert((Locale::English, StringKey::ResetDisplayConfirmTitle), "Reset Display Configuration");
    m.insert((Locale::English, StringKey::ResetDisplayConfirmMsg), "This will reset all display device configuration.\n\nAre you sure you want to continue?");
    m.insert((Locale::English, StringKey::Restart), "Restart");
    m.insert((Locale::English, StringKey::Quit), "Quit");
    m.insert((Locale::English, StringKey::StreamStarted), "Stream Started");
    m.insert((Locale::English, StringKey::StreamingStartedFor), "Streaming started for %s");
    m.insert((Locale::English, StringKey::StreamPaused), "Stream Paused");
    m.insert((Locale::English, StringKey::StreamingPausedFor), "Streaming paused for %s");
    m.insert((Locale::English, StringKey::ApplicationStopped), "Application Stopped");
    m.insert((Locale::English, StringKey::ApplicationStoppedMsg), "Application %s successfully stopped");
    m.insert((Locale::English, StringKey::IncomingPairingRequest), "Incoming PIN Request From: %s");
    m.insert((Locale::English, StringKey::ClickToCompletePairing), "Click here to enter PIN");
    m.insert((Locale::English, StringKey::QuitTitle), "Wait! Don't Leave Me! T_T");
    m.insert((Locale::English, StringKey::QuitMessage), "Nooo! You can't just quit like that!\nAre you really REALLY sure you want to leave?\nI'll miss you... but okay, if you must...\n\n(This will also close the Sunshine GUI application.)");
    m.insert((Locale::English, StringKey::ErrorTitle), "Error");
    m.insert((Locale::English, StringKey::ErrorNoUserSession), "Cannot open file dialog: No active user session found.");
    m.insert((Locale::English, StringKey::ImportSuccessTitle), "Import Success");
    m.insert((Locale::English, StringKey::ImportSuccessMsg), "Configuration imported successfully!\nPlease restart Sunshine to apply changes.");
    m.insert((Locale::English, StringKey::ImportErrorTitle), "Import Error");
    m.insert((Locale::English, StringKey::ImportErrorWrite), "Failed to import configuration file.");
    m.insert((Locale::English, StringKey::ImportErrorRead), "Failed to read the selected configuration file.");
    m.insert((Locale::English, StringKey::ImportErrorException), "An error occurred while importing configuration.");
    m.insert((Locale::English, StringKey::ExportSuccessTitle), "Export Success");
    m.insert((Locale::English, StringKey::ExportSuccessMsg), "Configuration exported successfully!");
    m.insert((Locale::English, StringKey::ExportErrorTitle), "Export Error");
    m.insert((Locale::English, StringKey::ExportErrorWrite), "Failed to export configuration file.");
    m.insert((Locale::English, StringKey::ExportErrorNoConfig), "No configuration found to export.");
    m.insert((Locale::English, StringKey::ExportErrorException), "An error occurred while exporting configuration.");
    m.insert((Locale::English, StringKey::ResetConfirmTitle), "Reset Configuration");
    m.insert((Locale::English, StringKey::ResetConfirmMsg), "This will reset all configuration to default values.\nThis action cannot be undone.\n\nDo you want to continue?");
    m.insert((Locale::English, StringKey::ResetSuccessTitle), "Reset Success");
    m.insert((Locale::English, StringKey::ResetSuccessMsg), "Configuration has been reset to default values.\nPlease restart Sunshine to apply changes.");
    m.insert((Locale::English, StringKey::ResetErrorTitle), "Reset Error");
    m.insert((Locale::English, StringKey::ResetErrorMsg), "Failed to reset configuration file.");
    m.insert((Locale::English, StringKey::ResetErrorException), "An error occurred while resetting configuration.");
    m.insert((Locale::English, StringKey::FileDialogSelectImport), "Select Configuration File to Import");
    m.insert((Locale::English, StringKey::FileDialogSaveExport), "Save Configuration File As");
    m.insert((Locale::English, StringKey::FileDialogConfigFiles), "Configuration Files");
    m.insert((Locale::English, StringKey::FileDialogAllFiles), "All Files");

    // Chinese translations
    m.insert((Locale::Chinese, StringKey::OpenSunshine), "打开 Sunshine");
    // VDD submenu
    m.insert((Locale::Chinese, StringKey::VddBaseDisplay), "基地显示器");
    m.insert((Locale::Chinese, StringKey::VddCreate), "创建");
    m.insert((Locale::Chinese, StringKey::VddClose), "关闭");
    m.insert((Locale::Chinese, StringKey::VddPersistent), "保持启用");
    m.insert((Locale::Chinese, StringKey::VddPersistentConfirmTitle), "启用保持虚拟显示器模式");
    m.insert((Locale::Chinese, StringKey::VddPersistentConfirmMsg), "启用此模式将使虚拟显示器始终保持活动状态。\n\n确定要启用吗？");
    // Advanced Settings submenu
    m.insert((Locale::Chinese, StringKey::AdvancedSettings), "高级设置");
    m.insert((Locale::Chinese, StringKey::ImportConfig), "导入配置");
    m.insert((Locale::Chinese, StringKey::ExportConfig), "导出配置");
    m.insert((Locale::Chinese, StringKey::ResetToDefault), "恢复默认");
    m.insert((Locale::Chinese, StringKey::CloseApp), "清理应用缓存");
    m.insert((Locale::Chinese, StringKey::CloseAppConfirmTitle), "清理应用缓存");
    m.insert((Locale::Chinese, StringKey::CloseAppConfirmMsg), "这将终止当前正在串流的应用程序。\n\n确定要继续吗？");
    m.insert((Locale::Chinese, StringKey::Language), "语言");
    m.insert((Locale::Chinese, StringKey::Chinese), "中文");
    m.insert((Locale::Chinese, StringKey::English), "English");
    m.insert((Locale::Chinese, StringKey::Japanese), "日本語");
    m.insert((Locale::Chinese, StringKey::StarProject), "Star项目");
    m.insert((Locale::Chinese, StringKey::VisitProject), "访问项目");
    m.insert((Locale::Chinese, StringKey::VisitProjectSunshine), "Sunshine-Foundation");
    m.insert((Locale::Chinese, StringKey::VisitProjectMoonlight), "Moonlight-vplus");
    m.insert((Locale::Chinese, StringKey::ResetDisplayDeviceConfig), "重置显示器记忆");
    m.insert((Locale::Chinese, StringKey::ResetDisplayConfirmTitle), "重置显示器配置");
    m.insert((Locale::Chinese, StringKey::ResetDisplayConfirmMsg), "这将重置所有显示器设备配置。\n\n确定要继续吗？");
    m.insert((Locale::Chinese, StringKey::Restart), "重新启动");
    m.insert((Locale::Chinese, StringKey::Quit), "退出");
    m.insert((Locale::Chinese, StringKey::StreamStarted), "串流已开始");
    m.insert((Locale::Chinese, StringKey::StreamingStartedFor), "已开始串流：%s");
    m.insert((Locale::Chinese, StringKey::StreamPaused), "串流已暂停");
    m.insert((Locale::Chinese, StringKey::StreamingPausedFor), "已暂停串流：%s");
    m.insert((Locale::Chinese, StringKey::ApplicationStopped), "应用已停止");
    m.insert((Locale::Chinese, StringKey::ApplicationStoppedMsg), "应用 %s 已成功停止");
    m.insert((Locale::Chinese, StringKey::IncomingPairingRequest), "来自 %s 的PIN请求");
    m.insert((Locale::Chinese, StringKey::ClickToCompletePairing), "点击此处完成PIN验证");
    m.insert((Locale::Chinese, StringKey::QuitTitle), "真的要退出吗");
    m.insert((Locale::Chinese, StringKey::QuitMessage), "你不能退出!\n那么想退吗? 真拿你没办法呢, 继续点一下吧~\n\n这将同时关闭Sunshine GUI应用程序。");
    m.insert((Locale::Chinese, StringKey::ErrorTitle), "错误");
    m.insert((Locale::Chinese, StringKey::ErrorNoUserSession), "无法打开文件对话框：未找到活动的用户会话。");
    m.insert((Locale::Chinese, StringKey::ImportSuccessTitle), "导入成功");
    m.insert((Locale::Chinese, StringKey::ImportSuccessMsg), "配置已成功导入！\n请重新启动 Sunshine 以应用更改。");
    m.insert((Locale::Chinese, StringKey::ImportErrorTitle), "导入失败");
    m.insert((Locale::Chinese, StringKey::ImportErrorWrite), "无法写入配置文件。");
    m.insert((Locale::Chinese, StringKey::ImportErrorRead), "无法读取所选的配置文件。");
    m.insert((Locale::Chinese, StringKey::ImportErrorException), "导入配置时发生错误。");
    m.insert((Locale::Chinese, StringKey::ExportSuccessTitle), "导出成功");
    m.insert((Locale::Chinese, StringKey::ExportSuccessMsg), "配置已成功导出！");
    m.insert((Locale::Chinese, StringKey::ExportErrorTitle), "导出失败");
    m.insert((Locale::Chinese, StringKey::ExportErrorWrite), "无法导出配置文件。");
    m.insert((Locale::Chinese, StringKey::ExportErrorNoConfig), "未找到可导出的配置。");
    m.insert((Locale::Chinese, StringKey::ExportErrorException), "导出配置时发生错误。");
    m.insert((Locale::Chinese, StringKey::ResetConfirmTitle), "重置配置");
    m.insert((Locale::Chinese, StringKey::ResetConfirmMsg), "这将把所有配置重置为默认值。\n此操作无法撤销。\n\n确定要继续吗？");
    m.insert((Locale::Chinese, StringKey::ResetSuccessTitle), "重置成功");
    m.insert((Locale::Chinese, StringKey::ResetSuccessMsg), "配置已重置为默认值。\n请重新启动 Sunshine 以应用更改。");
    m.insert((Locale::Chinese, StringKey::ResetErrorTitle), "重置失败");
    m.insert((Locale::Chinese, StringKey::ResetErrorMsg), "无法重置配置文件。");
    m.insert((Locale::Chinese, StringKey::ResetErrorException), "重置配置时发生错误。");
    m.insert((Locale::Chinese, StringKey::FileDialogSelectImport), "选择要导入的配置文件");
    m.insert((Locale::Chinese, StringKey::FileDialogSaveExport), "配置文件另存为");
    m.insert((Locale::Chinese, StringKey::FileDialogConfigFiles), "配置文件");
    m.insert((Locale::Chinese, StringKey::FileDialogAllFiles), "所有文件");

    // Japanese translations
    m.insert((Locale::Japanese, StringKey::OpenSunshine), "Sunshineを開く");
    // VDD submenu
    m.insert((Locale::Japanese, StringKey::VddBaseDisplay), "仮想ディスプレイ");
    m.insert((Locale::Japanese, StringKey::VddCreate), "作成");
    m.insert((Locale::Japanese, StringKey::VddClose), "閉じる");
    m.insert((Locale::Japanese, StringKey::VddPersistent), "常時有効");
    m.insert((Locale::Japanese, StringKey::VddPersistentConfirmTitle), "仮想ディスプレイの常時有効モード");
    m.insert((Locale::Japanese, StringKey::VddPersistentConfirmMsg), "このモードを有効にすると、仮想ディスプレイは常にアクティブな状態を維持します。\n\n有効にしますか？");
    // Advanced Settings submenu
    m.insert((Locale::Japanese, StringKey::AdvancedSettings), "詳細設定");
    m.insert((Locale::Japanese, StringKey::ImportConfig), "設定をインポート");
    m.insert((Locale::Japanese, StringKey::ExportConfig), "設定をエクスポート");
    m.insert((Locale::Japanese, StringKey::ResetToDefault), "デフォルトに戻す");
    m.insert((Locale::Japanese, StringKey::CloseApp), "キャッシュをクリア");
    m.insert((Locale::Japanese, StringKey::CloseAppConfirmTitle), "キャッシュをクリア");
    m.insert((Locale::Japanese, StringKey::CloseAppConfirmMsg), "現在ストリーミング中のアプリケーションを終了します。\n\n続行しますか？");
    m.insert((Locale::Japanese, StringKey::Language), "言語");
    m.insert((Locale::Japanese, StringKey::Chinese), "中文");
    m.insert((Locale::Japanese, StringKey::English), "English");
    m.insert((Locale::Japanese, StringKey::Japanese), "日本語");
    m.insert((Locale::Japanese, StringKey::StarProject), "スターを付ける");
    m.insert((Locale::Japanese, StringKey::VisitProject), "プロジェクトを訪問");
    m.insert((Locale::Japanese, StringKey::VisitProjectSunshine), "Sunshine-Foundation");
    m.insert((Locale::Japanese, StringKey::VisitProjectMoonlight), "Moonlight-vplus");
    m.insert((Locale::Japanese, StringKey::ResetDisplayDeviceConfig), "ディスプレイメモリをリセット");
    m.insert((Locale::Japanese, StringKey::ResetDisplayConfirmTitle), "ディスプレイ設定をリセット");
    m.insert((Locale::Japanese, StringKey::ResetDisplayConfirmMsg), "すべてのディスプレイデバイス設定をリセットします。\n\n続行しますか？");
    m.insert((Locale::Japanese, StringKey::Restart), "再起動");
    m.insert((Locale::Japanese, StringKey::Quit), "終了");
    m.insert((Locale::Japanese, StringKey::StreamStarted), "ストリーム開始");
    m.insert((Locale::Japanese, StringKey::StreamingStartedFor), "%s のストリーミングを開始しました");
    m.insert((Locale::Japanese, StringKey::StreamPaused), "ストリーム一時停止");
    m.insert((Locale::Japanese, StringKey::StreamingPausedFor), "%s のストリーミングを一時停止しました");
    m.insert((Locale::Japanese, StringKey::ApplicationStopped), "アプリケーション停止");
    m.insert((Locale::Japanese, StringKey::ApplicationStoppedMsg), "アプリケーション %s が正常に停止しました");
    m.insert((Locale::Japanese, StringKey::IncomingPairingRequest), "%s からのPIN要求");
    m.insert((Locale::Japanese, StringKey::ClickToCompletePairing), "クリックしてPIN認証を完了");
    m.insert((Locale::Japanese, StringKey::QuitTitle), "本当に終了しますか？");
    m.insert((Locale::Japanese, StringKey::QuitMessage), "終了できません！\n本当に終了したいですか？\n\nこれによりSunshine GUIアプリケーションも閉じられます。");
    m.insert((Locale::Japanese, StringKey::ErrorTitle), "エラー");
    m.insert((Locale::Japanese, StringKey::ErrorNoUserSession), "ファイルダイアログを開けません：アクティブなユーザーセッションが見つかりません。");
    m.insert((Locale::Japanese, StringKey::ImportSuccessTitle), "インポート成功");
    m.insert((Locale::Japanese, StringKey::ImportSuccessMsg), "設定のインポートに成功しました！\n変更を適用するにはSunshineを再起動してください。");
    m.insert((Locale::Japanese, StringKey::ImportErrorTitle), "インポート失敗");
    m.insert((Locale::Japanese, StringKey::ImportErrorWrite), "設定ファイルを書き込めませんでした。");
    m.insert((Locale::Japanese, StringKey::ImportErrorRead), "選択した設定ファイルを読み取れませんでした。");
    m.insert((Locale::Japanese, StringKey::ImportErrorException), "設定のインポート中にエラーが発生しました。");
    m.insert((Locale::Japanese, StringKey::ExportSuccessTitle), "エクスポート成功");
    m.insert((Locale::Japanese, StringKey::ExportSuccessMsg), "設定のエクスポートに成功しました！");
    m.insert((Locale::Japanese, StringKey::ExportErrorTitle), "エクスポート失敗");
    m.insert((Locale::Japanese, StringKey::ExportErrorWrite), "設定ファイルをエクスポートできませんでした。");
    m.insert((Locale::Japanese, StringKey::ExportErrorNoConfig), "エクスポートする設定が見つかりません。");
    m.insert((Locale::Japanese, StringKey::ExportErrorException), "設定のエクスポート中にエラーが発生しました。");
    m.insert((Locale::Japanese, StringKey::ResetConfirmTitle), "設定のリセット");
    m.insert((Locale::Japanese, StringKey::ResetConfirmMsg), "すべての設定をデフォルト値にリセットします。\nこの操作は元に戻せません。\n\n続行しますか？");
    m.insert((Locale::Japanese, StringKey::ResetSuccessTitle), "リセット成功");
    m.insert((Locale::Japanese, StringKey::ResetSuccessMsg), "設定をデフォルト値にリセットしました。\n変更を適用するにはSunshineを再起動してください。");
    m.insert((Locale::Japanese, StringKey::ResetErrorTitle), "リセット失敗");
    m.insert((Locale::Japanese, StringKey::ResetErrorMsg), "設定ファイルをリセットできませんでした。");
    m.insert((Locale::Japanese, StringKey::ResetErrorException), "設定のリセット中にエラーが発生しました。");
    m.insert((Locale::Japanese, StringKey::FileDialogSelectImport), "インポートする設定ファイルを選択");
    m.insert((Locale::Japanese, StringKey::FileDialogSaveExport), "設定ファイルに名前を付けて保存");
    m.insert((Locale::Japanese, StringKey::FileDialogConfigFiles), "設定ファイル");
    m.insert((Locale::Japanese, StringKey::FileDialogAllFiles), "すべてのファイル");

    m
});

/// Get current locale
pub fn get_locale() -> Locale {
    *CURRENT_LOCALE.read()
}

/// Set current locale
pub fn set_locale(locale: Locale) {
    *CURRENT_LOCALE.write() = locale;
}

/// Set locale from string
pub fn set_locale_str(locale_str: &str) {
    set_locale(Locale::from(locale_str));
}

/// Get localized string
pub fn get_string(key: StringKey) -> &'static str {
    let locale = get_locale();
    
    // Try current locale first
    if let Some(s) = TRANSLATIONS.get(&(locale, key)) {
        return s;
    }
    
    // Fallback to English
    if let Some(s) = TRANSLATIONS.get(&(Locale::English, key)) {
        return s;
    }
    
    // Return empty string if not found
    ""
}

/// Get localized string with format substitution
pub fn get_string_fmt(key: StringKey, arg: &str) -> String {
    let template = get_string(key);
    template.replace("%s", arg)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_locale_from_str() {
        assert_eq!(Locale::from("zh"), Locale::Chinese);
        assert_eq!(Locale::from("zh_CN"), Locale::Chinese);
        assert_eq!(Locale::from("ja"), Locale::Japanese);
        assert_eq!(Locale::from("en"), Locale::English);
        assert_eq!(Locale::from("unknown"), Locale::English);
    }

    #[test]
    fn test_get_string() {
        set_locale(Locale::English);
        assert_eq!(get_string(StringKey::OpenSunshine), "Open Sunshine");
        
        set_locale(Locale::Chinese);
        assert_eq!(get_string(StringKey::OpenSunshine), "打开 Sunshine");
        
        set_locale(Locale::Japanese);
        assert_eq!(get_string(StringKey::OpenSunshine), "Sunshineを開く");
    }

    #[test]
    fn test_get_string_fmt() {
        set_locale(Locale::English);
        assert_eq!(get_string_fmt(StringKey::StreamingStartedFor, "TestApp"), "Streaming started for TestApp");
    }
}
