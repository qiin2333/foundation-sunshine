//! Menu actions module
//! 
//! Defines all menu actions and their identifiers.
//! C++ side will register callbacks for these actions.

use parking_lot::RwLock;
use once_cell::sync::Lazy;

/// Menu action identifiers
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[repr(u32)]
pub enum MenuAction {
    OpenUI = 1,
    // VDD submenu actions
    VddCreate = 2,
    VddClose = 3,
    VddPersistent = 4,
    // Config actions
    ImportConfig = 5,
    ExportConfig = 6,
    ResetConfig = 7,
    CloseApp = 8,
    // Language actions
    LanguageChinese = 9,
    LanguageEnglish = 10,
    LanguageJapanese = 11,
    StarProject = 12,
    // Visit Project actions
    VisitProjectSunshine = 13,
    VisitProjectMoonlight = 14,
    ResetDisplayDeviceConfig = 15,
    Restart = 16,
    Quit = 17,
    NotificationClicked = 18,
}

impl TryFrom<u32> for MenuAction {
    type Error = ();
    
    fn try_from(value: u32) -> Result<Self, Self::Error> {
        match value {
            1 => Ok(MenuAction::OpenUI),
            2 => Ok(MenuAction::VddCreate),
            3 => Ok(MenuAction::VddClose),
            4 => Ok(MenuAction::VddPersistent),
            5 => Ok(MenuAction::ImportConfig),
            6 => Ok(MenuAction::ExportConfig),
            7 => Ok(MenuAction::ResetConfig),
            8 => Ok(MenuAction::CloseApp),
            9 => Ok(MenuAction::LanguageChinese),
            10 => Ok(MenuAction::LanguageEnglish),
            11 => Ok(MenuAction::LanguageJapanese),
            12 => Ok(MenuAction::StarProject),
            13 => Ok(MenuAction::VisitProjectSunshine),
            14 => Ok(MenuAction::VisitProjectMoonlight),
            15 => Ok(MenuAction::ResetDisplayDeviceConfig),
            16 => Ok(MenuAction::Restart),
            17 => Ok(MenuAction::Quit),
            18 => Ok(MenuAction::NotificationClicked),
            _ => Err(()),
        }
    }
}

/// Callback function type for menu actions
pub type ActionCallback = extern "C" fn(action: u32);

/// Global callback storage
static ACTION_CALLBACK: Lazy<RwLock<Option<ActionCallback>>> = Lazy::new(|| RwLock::new(None));

/// Register the callback for menu actions
pub fn register_callback(callback: ActionCallback) {
    *ACTION_CALLBACK.write() = Some(callback);
}

/// Trigger a menu action
pub fn trigger_action(action: MenuAction) {
    if let Some(callback) = *ACTION_CALLBACK.read() {
        callback(action as u32);
    }
}

/// URLs for opening in browser
pub mod urls {
    pub const GITHUB_PROJECT: &str = "https://sunshine-foundation.vercel.app/";
    pub const PROJECT_SUNSHINE: &str = "https://github.com/qiin2333/Sunshine-Foundation";
    pub const PROJECT_MOONLIGHT: &str = "https://github.com/qiin2333/moonlight-vplus";
}

/// Open URL in default browser
#[cfg(target_os = "windows")]
pub fn open_url(url: &str) {
    use std::ffi::OsStr;
    use std::os::windows::ffi::OsStrExt;
    use std::ptr::null_mut;
    
    let wide_url: Vec<u16> = OsStr::new(url)
        .encode_wide()
        .chain(std::iter::once(0))
        .collect();
    
    unsafe {
        windows_sys::Win32::UI::Shell::ShellExecuteW(
            null_mut(),
            ['o' as u16, 'p' as u16, 'e' as u16, 'n' as u16, 0].as_ptr(),
            wide_url.as_ptr(),
            null_mut(),
            null_mut(),
            windows_sys::Win32::UI::WindowsAndMessaging::SW_SHOWNORMAL as i32,
        );
    }
}

#[cfg(target_os = "linux")]
pub fn open_url(url: &str) {
    let _ = std::process::Command::new("xdg-open")
        .arg(url)
        .spawn();
}

#[cfg(target_os = "macos")]
pub fn open_url(url: &str) {
    let _ = std::process::Command::new("open")
        .arg(url)
        .spawn();
}
