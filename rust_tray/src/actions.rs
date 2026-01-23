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
    // Reserved: 5, 6, 7 (removed import/export/reset config)
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
            // 5, 6, 7 reserved (removed import/export/reset config)
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
