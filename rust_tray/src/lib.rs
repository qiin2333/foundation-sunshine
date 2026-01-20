//! Sunshine Tray - Rust implementation of the system tray
//!
//! This library provides a complete system tray implementation with:
//! - Multi-language support (Chinese, English, Japanese)
//! - Menu management
//! - Notification support
//! - Icon management

#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]

pub mod i18n;
pub mod actions;
pub mod config;

use std::ffi::CStr;
use std::os::raw::{c_char, c_int};
use std::path::Path;
use std::sync::atomic::{AtomicBool, Ordering};

#[cfg(not(target_os = "windows"))]
use image::ImageReader;
use muda::{Menu, MenuEvent, MenuItem, PredefinedMenuItem, Submenu, CheckMenuItem};
use once_cell::sync::OnceCell;
use parking_lot::Mutex;
use tray_icon::{Icon, TrayIcon, TrayIconBuilder};

use i18n::{StringKey, get_string, set_locale_str};
use actions::{MenuAction, trigger_action, register_callback, ActionCallback, open_url, urls};

#[cfg(target_os = "windows")]
use windows_sys::Win32::UI::WindowsAndMessaging::{
    DispatchMessageW, GetMessageW, PeekMessageW, PostQuitMessage, TranslateMessage, 
    MSG, WM_QUIT, PM_REMOVE, WM_DPICHANGED,
};

#[cfg(target_os = "windows")]
use std::sync::atomic::AtomicI32;

/// Tray state
#[allow(dead_code)]  // Fields are needed for lifetime management
struct TrayState {
    icon: TrayIcon,
    menu: Menu,
    // Submenu references
    vdd_submenu: Submenu,
    advanced_settings_submenu: Submenu,
    language_submenu: Submenu,
    visit_project_submenu: Submenu,
    // All menu items for rebuilding
    menu_items: MenuItems,
}

struct MenuItems {
    open_sunshine: MenuItem,
    // VDD submenu items
    vdd_create: CheckMenuItem,
    vdd_close: CheckMenuItem,
    vdd_persistent: CheckMenuItem,
    // Advanced Settings submenu items
    import_config: MenuItem,
    export_config: MenuItem,
    reset_config: MenuItem,
    close_app: MenuItem,
    #[cfg(target_os = "windows")]
    reset_display: MenuItem,
    // Language submenu items
    lang_chinese: MenuItem,
    lang_english: MenuItem,
    lang_japanese: MenuItem,
    // Other items
    star_project: MenuItem,
    visit_sunshine: MenuItem,
    visit_moonlight: MenuItem,
    restart: MenuItem,
    quit: MenuItem,
}

// Safety: TrayState is only accessed from the main thread
unsafe impl Send for TrayState {}
unsafe impl Sync for TrayState {}

/// Global state
static TRAY_STATE: OnceCell<Mutex<Option<TrayState>>> = OnceCell::new();
static SHOULD_EXIT: AtomicBool = AtomicBool::new(false);

/// Current icon type (0=normal, 1=playing, 2=pausing, 3=locked)
/// Used to refresh icon when DPI changes
#[cfg(target_os = "windows")]
static CURRENT_ICON_TYPE: AtomicI32 = AtomicI32::new(0);

/// Cached DPI value to detect DPI changes
#[cfg(target_os = "windows")]
static CACHED_DPI_SIZE: AtomicI32 = AtomicI32::new(0);

/// Config file path storage (set from C++)
static CONFIG_FILE_PATH: OnceCell<String> = OnceCell::new();

/// Get the config file path (set from C++)
pub fn get_config_file_path_from_cpp() -> Option<&'static str> {
    CONFIG_FILE_PATH.get().map(|s| s.as_str())
}

/// Icon paths storage
static ICON_PATHS: OnceCell<IconPaths> = OnceCell::new();

struct IconPaths {
    normal: String,
    playing: String,
    pausing: String,
    locked: String,
}

/// Convert C string to Rust string
unsafe fn c_str_to_string(ptr: *const c_char) -> Option<String> {
    if ptr.is_null() {
        return None;
    }
    CStr::from_ptr(ptr).to_str().ok().map(|s| s.to_string())
}

/// Get the system small icon size (used for notification area icons)
/// This size is DPI-aware and matches what Windows expects for tray icons
#[cfg(target_os = "windows")]
fn get_system_small_icon_size() -> (u32, u32) {
    use windows_sys::Win32::UI::WindowsAndMessaging::{GetSystemMetrics, SM_CXSMICON, SM_CYSMICON};

    unsafe {
        let width = GetSystemMetrics(SM_CXSMICON);
        let height = GetSystemMetrics(SM_CYSMICON);

        // Fallback to 16x16 if GetSystemMetrics fails (returns 0)
        let width = if width > 0 { width as u32 } else { 16 };
        let height = if height > 0 { height as u32 } else { 16 };

        // Cache the current size for DPI change detection
        CACHED_DPI_SIZE.store(width as i32, Ordering::SeqCst);

        (width, height)
    }
}

/// Check if DPI has changed since last icon load
/// Returns true if DPI changed and icon needs refresh
#[cfg(target_os = "windows")]
fn check_dpi_changed() -> bool {
    use windows_sys::Win32::UI::WindowsAndMessaging::{GetSystemMetrics, SM_CXSMICON};

    unsafe {
        let current_size = GetSystemMetrics(SM_CXSMICON);
        let cached_size = CACHED_DPI_SIZE.load(Ordering::SeqCst);

        // If cached is 0, we haven't loaded yet - not a change
        if cached_size == 0 {
            return false;
        }

        current_size != cached_size
    }
}

/// Refresh the current icon with new DPI settings
#[cfg(target_os = "windows")]
fn refresh_icon_for_dpi() {
    let icon_type = CURRENT_ICON_TYPE.load(Ordering::SeqCst);

    let icon_paths = match ICON_PATHS.get() {
        Some(p) => p,
        None => return,
    };

    let icon_path = match icon_type {
        0 => &icon_paths.normal,
        1 => &icon_paths.playing,
        2 => &icon_paths.pausing,
        3 => &icon_paths.locked,
        _ => &icon_paths.normal,
    };

    if let Some(icon) = load_icon(icon_path) {
        if let Some(state_mutex) = TRAY_STATE.get() {
            let state_guard = state_mutex.lock();
            if let Some(ref state) = *state_guard {
                let _ = state.icon.set_icon(Some(icon));
            }
        }
    }
}

/// Load icon from ICO file path using native Windows API
/// This properly handles DPI scaling by requesting the correct icon size
/// based on SM_CXSMICON/SM_CYSMICON system metrics
#[cfg(target_os = "windows")]
fn load_icon_from_path(path: &str) -> Option<Icon> {
    // Get the correct icon size for the notification area based on system DPI
    let size = get_system_small_icon_size();

    // Request the specific size - Windows will select the best matching
    // icon from the ICO file's multiple resolutions
    match Icon::from_path(path, Some(size)) {
        Ok(icon) => Some(icon),
        Err(e) => {
            eprintln!("Failed to load icon '{}' with size {:?}: {}", path, size, e);
            None
        }
    }
}

/// Load icon from file path on non-Windows platforms
/// On Linux/macOS, ICO files are decoded using the image crate
#[cfg(not(target_os = "windows"))]
fn load_icon_from_path(path: &str) -> Option<Icon> {
    let path = Path::new(path);
    
    let img = match ImageReader::open(path) {
        Ok(reader) => match reader.decode() {
            Ok(img) => img.into_rgba8(),
            Err(e) => {
                eprintln!("Failed to decode icon '{}': {}", path.display(), e);
                return None;
            }
        },
        Err(e) => {
            eprintln!("Failed to open icon '{}': {}", path.display(), e);
            return None;
        }
    };

    let (width, height) = img.dimensions();
    let rgba = img.into_raw();

    Icon::from_rgba(rgba, width, height).ok()
}

#[cfg(target_os = "linux")]
fn load_icon_by_name(name: &str) -> Option<Icon> {
    let paths = [
        format!("/usr/share/icons/hicolor/256x256/apps/{}.png", name),
        format!("/usr/share/icons/hicolor/128x128/apps/{}.png", name),
        format!("/usr/share/icons/hicolor/64x64/apps/{}.png", name),
        format!("/usr/share/pixmaps/{}.png", name),
    ];

    for path in &paths {
        if Path::new(path).exists() {
            if let Some(icon) = load_icon_from_path(path) {
                return Some(icon);
            }
        }
    }
    None
}

/// Load icon
///
/// On Windows: expects .ico file path (supports multi-resolution)
/// On Linux: can be either a file path or an icon name (searches system dirs)
/// On macOS: expects file path
fn load_icon(icon_str: &str) -> Option<Icon> {
    // First, try as a direct file path
    if Path::new(icon_str).exists() {
        return load_icon_from_path(icon_str);
    }

    // On Linux, try searching by icon name
    #[cfg(target_os = "linux")]
    {
        return load_icon_by_name(icon_str);
    }

    #[cfg(not(target_os = "linux"))]
    {
        eprintln!("Icon not found: {}", icon_str);
        None
    }
}

/// Build the tray menu with current language
fn build_menu() -> (Menu, MenuItems, Submenu, Submenu, Submenu, Submenu) {
    let menu = Menu::new();
    
    // Open Sunshine
    let open_sunshine = MenuItem::new(get_string(StringKey::OpenSunshine), true, None);
    let _ = menu.append(&open_sunshine);
    
    // Separator
    let _ = menu.append(&PredefinedMenuItem::separator());
    
    // VDD submenu (Windows only, but we keep it for structure consistency)
    let vdd_create = CheckMenuItem::new(get_string(StringKey::VddCreate), true, false, None);
    let vdd_close = CheckMenuItem::new(get_string(StringKey::VddClose), true, false, None);
    let vdd_persistent = CheckMenuItem::new(get_string(StringKey::VddPersistent), true, false, None);
    
    let vdd_submenu = Submenu::new(get_string(StringKey::VddBaseDisplay), true);
    let _ = vdd_submenu.append(&vdd_create);
    let _ = vdd_submenu.append(&vdd_close);
    let _ = vdd_submenu.append(&vdd_persistent);
    
    #[cfg(target_os = "windows")]
    let _ = menu.append(&vdd_submenu);
    
    // Advanced Settings submenu (Windows only)
    let import_config = MenuItem::new(get_string(StringKey::ImportConfig), true, None);
    let export_config = MenuItem::new(get_string(StringKey::ExportConfig), true, None);
    let reset_config = MenuItem::new(get_string(StringKey::ResetToDefault), true, None);
    let close_app = MenuItem::new(get_string(StringKey::CloseApp), true, None);
    
    #[cfg(target_os = "windows")]
    let reset_display = MenuItem::new(get_string(StringKey::ResetDisplayDeviceConfig), true, None);
    
    let advanced_settings_submenu = Submenu::new(get_string(StringKey::AdvancedSettings), true);
    let _ = advanced_settings_submenu.append(&import_config);
    let _ = advanced_settings_submenu.append(&export_config);
    let _ = advanced_settings_submenu.append(&reset_config);
    let _ = advanced_settings_submenu.append(&PredefinedMenuItem::separator());
    let _ = advanced_settings_submenu.append(&close_app);
    #[cfg(target_os = "windows")]
    let _ = advanced_settings_submenu.append(&reset_display);
    
    #[cfg(target_os = "windows")]
    let _ = menu.append(&advanced_settings_submenu);
    
    // Separator
    let _ = menu.append(&PredefinedMenuItem::separator());
    
    // Language submenu
    let lang_chinese = MenuItem::new(get_string(StringKey::Chinese), true, None);
    let lang_english = MenuItem::new(get_string(StringKey::English), true, None);
    let lang_japanese = MenuItem::new(get_string(StringKey::Japanese), true, None);
    
    let language_submenu = Submenu::new(get_string(StringKey::Language), true);
    let _ = language_submenu.append(&lang_chinese);
    let _ = language_submenu.append(&lang_english);
    let _ = language_submenu.append(&lang_japanese);
    let _ = menu.append(&language_submenu);
    
    // Separator
    let _ = menu.append(&PredefinedMenuItem::separator());
    
    // Star Project
    let star_project = MenuItem::new(get_string(StringKey::StarProject), true, None);
    let _ = menu.append(&star_project);
    
    // Visit Project submenu
    let visit_sunshine = MenuItem::new(get_string(StringKey::VisitProjectSunshine), true, None);
    let visit_moonlight = MenuItem::new(get_string(StringKey::VisitProjectMoonlight), true, None);
    
    let visit_project_submenu = Submenu::new(get_string(StringKey::VisitProject), true);
    let _ = visit_project_submenu.append(&visit_sunshine);
    let _ = visit_project_submenu.append(&visit_moonlight);
    let _ = menu.append(&visit_project_submenu);
    
    // Separator
    let _ = menu.append(&PredefinedMenuItem::separator());
    
    // Restart
    let restart = MenuItem::new(get_string(StringKey::Restart), true, None);
    let _ = menu.append(&restart);
    
    // Quit
    let quit = MenuItem::new(get_string(StringKey::Quit), true, None);
    let _ = menu.append(&quit);
    
    let menu_items = MenuItems {
        open_sunshine,
        vdd_create,
        vdd_close,
        vdd_persistent,
        import_config,
        export_config,
        reset_config,
        close_app,
        #[cfg(target_os = "windows")]
        reset_display,
        lang_chinese,
        lang_english,
        lang_japanese,
        star_project,
        visit_sunshine,
        visit_moonlight,
        restart,
        quit,
    };
    
    (menu, menu_items, vdd_submenu, advanced_settings_submenu, language_submenu, visit_project_submenu)
}

/// Identify which action corresponds to the menu event
/// Returns the action to perform (if any) and whether menu rebuild is needed
fn identify_menu_action(event: &MenuEvent, state: &TrayState) -> (Option<MenuAction>, bool) {
    let items = &state.menu_items;
    
    if event.id == items.open_sunshine.id() {
        (Some(MenuAction::OpenUI), false)
    } else if event.id == items.vdd_create.id() {
        (Some(MenuAction::VddCreate), false)
    } else if event.id == items.vdd_close.id() {
        (Some(MenuAction::VddClose), false)
    } else if event.id == items.vdd_persistent.id() {
        (Some(MenuAction::VddPersistent), false)
    } else if event.id == items.import_config.id() {
        (Some(MenuAction::ImportConfig), false)
    } else if event.id == items.export_config.id() {
        (Some(MenuAction::ExportConfig), false)
    } else if event.id == items.reset_config.id() {
        (Some(MenuAction::ResetConfig), false)
    } else if event.id == items.close_app.id() {
        (Some(MenuAction::CloseApp), false)
    } else if event.id == items.lang_chinese.id() {
        (Some(MenuAction::LanguageChinese), true)
    } else if event.id == items.lang_english.id() {
        (Some(MenuAction::LanguageEnglish), true)
    } else if event.id == items.lang_japanese.id() {
        (Some(MenuAction::LanguageJapanese), true)
    } else if event.id == items.star_project.id() {
        (Some(MenuAction::StarProject), false)
    } else if event.id == items.visit_sunshine.id() {
        (Some(MenuAction::VisitProjectSunshine), false)
    } else if event.id == items.visit_moonlight.id() {
        (Some(MenuAction::VisitProjectMoonlight), false)
    } else if event.id == items.restart.id() {
        (Some(MenuAction::Restart), false)
    } else if event.id == items.quit.id() {
        (Some(MenuAction::Quit), false)
    } else {
        #[cfg(target_os = "windows")]
        if event.id == items.reset_display.id() {
            return (Some(MenuAction::ResetDisplayDeviceConfig), false);
        }
        (None, false)
    }
}

/// Execute the identified action
/// This is called AFTER releasing the state lock to avoid deadlocks
fn execute_action(action: MenuAction, needs_menu_rebuild: bool) {
    match action {
        MenuAction::ImportConfig => {
            // Handle config import in Rust
            std::thread::spawn(|| {
                if let Err(e) = config::import_config() {
                    match e {
                        config::ConfigError::DialogCancelled => {}
                        _ => {
                            config::show_message_box(
                                i18n::get_string(i18n::StringKey::ImportErrorTitle),
                                &format!("{}", e),
                                true,
                            );
                        }
                    }
                }
            });
            trigger_action(action);
        }
        MenuAction::ExportConfig => {
            // Handle config export in Rust
            std::thread::spawn(|| {
                if let Err(e) = config::export_config() {
                    match e {
                        config::ConfigError::DialogCancelled => {}
                        _ => {
                            config::show_message_box(
                                i18n::get_string(i18n::StringKey::ExportErrorTitle),
                                &format!("{}", e),
                                true,
                            );
                        }
                    }
                }
            });
            trigger_action(action);
        }
        MenuAction::ResetConfig => {
            // Handle config reset in Rust
            std::thread::spawn(|| {
                if let Err(e) = config::reset_config() {
                    match e {
                        config::ConfigError::DialogCancelled => {}
                        _ => {
                            config::show_message_box(
                                i18n::get_string(i18n::StringKey::ResetErrorTitle),
                                &format!("{}", e),
                                true,
                            );
                        }
                    }
                }
            });
            trigger_action(action);
        }
        MenuAction::LanguageChinese => {
            set_locale_str("zh");
            let _ = config::save_tray_locale("zh");
            trigger_action(action);
            if needs_menu_rebuild {
                update_menu_texts();
            }
        }
        MenuAction::LanguageEnglish => {
            set_locale_str("en");
            let _ = config::save_tray_locale("en");
            trigger_action(action);
            if needs_menu_rebuild {
                update_menu_texts();
            }
        }
        MenuAction::LanguageJapanese => {
            set_locale_str("ja");
            let _ = config::save_tray_locale("ja");
            trigger_action(action);
            if needs_menu_rebuild {
                update_menu_texts();
            }
        }
        MenuAction::StarProject => {
            open_url(urls::GITHUB_PROJECT);
            trigger_action(action);
        }
        MenuAction::VisitProjectSunshine => {
            open_url(urls::PROJECT_SUNSHINE);
            trigger_action(action);
        }
        MenuAction::VisitProjectMoonlight => {
            open_url(urls::PROJECT_MOONLIGHT);
            trigger_action(action);
        }
        _ => {
            // For all other actions (VddCreate, VddClose, VddPersistent, CloseApp, ResetDisplayDeviceConfig, Restart, Quit), 
            // just trigger the callback to let C++ handle them
            trigger_action(action);
        }
    }
}

/// Process a menu event - identifies the action while holding the lock,
/// then releases the lock before executing to avoid deadlocks
fn process_menu_event(event: &MenuEvent) {
    let (action, needs_rebuild) = {
        // Hold lock only while identifying the action
        if let Some(state_mutex) = TRAY_STATE.get() {
            let state_guard = state_mutex.lock();
            if let Some(ref state) = *state_guard {
                identify_menu_action(event, state)
            } else {
                (None, false)
            }
        } else {
            (None, false)
        }
        // Lock is released here
    };
    
    // Execute action without holding the lock
    if let Some(action) = action {
        execute_action(action, needs_rebuild);
    }
}

/// Update menu texts after language change by rebuilding the menu
/// 
/// On Windows, simply updating menu item texts with set_text() and calling set_menu()
/// can cause issues with the menu event handling. The safest approach is to rebuild
/// the entire menu with the new texts.
fn update_menu_texts() {
    if let Some(state_mutex) = TRAY_STATE.get() {
        let mut state_guard = state_mutex.lock();
        if let Some(ref mut state) = *state_guard {
            // Get the current VDD states before rebuilding
            let vdd_create_checked = state.menu_items.vdd_create.is_checked();
            let vdd_close_checked = state.menu_items.vdd_close.is_checked();
            let vdd_persistent_checked = state.menu_items.vdd_persistent.is_checked();
            let vdd_create_enabled = state.menu_items.vdd_create.is_enabled();
            let vdd_close_enabled = state.menu_items.vdd_close.is_enabled();
            
            // Build a completely new menu with the updated language
            let (new_menu, new_menu_items, new_vdd_submenu, new_advanced_submenu, new_language_submenu, new_visit_submenu) = build_menu();
            
            // Restore the VDD states
            new_menu_items.vdd_create.set_checked(vdd_create_checked);
            new_menu_items.vdd_close.set_checked(vdd_close_checked);
            new_menu_items.vdd_persistent.set_checked(vdd_persistent_checked);
            new_menu_items.vdd_create.set_enabled(vdd_create_enabled);
            new_menu_items.vdd_close.set_enabled(vdd_close_enabled);
            
            // Set the new menu on the tray icon
            // This properly detaches the old menu subclass and attaches the new one
            let _ = state.icon.set_menu(Some(Box::new(new_menu.clone())));
            
            // Update the state with the new menu and items
            state.menu = new_menu;
            state.menu_items = new_menu_items;
            state.vdd_submenu = new_vdd_submenu;
            state.advanced_settings_submenu = new_advanced_submenu;
            state.language_submenu = new_language_submenu;
            state.visit_project_submenu = new_visit_submenu;
        }
    }
}

// ============================================================================
// C FFI Interface
// ============================================================================

/// Initialize the tray with icon paths
/// 
/// # Arguments
/// * `icon_normal` - Path to normal icon
/// * `icon_playing` - Path to playing icon
/// * `icon_pausing` - Path to pausing icon
/// * `icon_locked` - Path to locked icon
/// * `tooltip` - Tooltip text
/// * `locale` - Initial locale (e.g., "zh", "en", "ja")
/// * `config_file` - Path to the Sunshine configuration file (sunshine.conf)
/// * `callback` - Callback function for menu actions
/// 
/// # Returns
/// 0 on success, -1 on error
#[no_mangle]
pub unsafe extern "C" fn tray_init_ex(
    icon_normal: *const c_char,
    icon_playing: *const c_char,
    icon_pausing: *const c_char,
    icon_locked: *const c_char,
    tooltip: *const c_char,
    locale: *const c_char,
    config_file: *const c_char,
    callback: ActionCallback,
) -> c_int {
    // Store config file path (from C++)
    if let Some(cfg_path) = c_str_to_string(config_file) {
        let _ = CONFIG_FILE_PATH.set(cfg_path);
    }

    // Store icon paths
    let normal = c_str_to_string(icon_normal).unwrap_or_default();
    let playing = c_str_to_string(icon_playing).unwrap_or_default();
    let pausing = c_str_to_string(icon_pausing).unwrap_or_default();
    let locked = c_str_to_string(icon_locked).unwrap_or_default();
    
    let _ = ICON_PATHS.set(IconPaths {
        normal: normal.clone(),
        playing,
        pausing,
        locked,
    });
    
    // Set locale
    if let Some(loc) = c_str_to_string(locale) {
        set_locale_str(&loc);
    }
    
    // Register callback
    register_callback(callback);
    
    // Initialize global state
    let _ = TRAY_STATE.get_or_init(|| Mutex::new(None));
    
    // Reset exit flag
    SHOULD_EXIT.store(false, Ordering::SeqCst);
    
    // Load icon
    let icon = match load_icon(&normal) {
        Some(i) => i,
        None => {
            eprintln!("Failed to load tray icon");
            return -1;
        }
    };
    
    // Get tooltip
    let tooltip_str = c_str_to_string(tooltip).unwrap_or_else(|| "Sunshine".to_string());
    
    // Build menu
    let (menu, menu_items, vdd_submenu, advanced_settings_submenu, language_submenu, visit_project_submenu) = build_menu();
    
    // Create tray icon
    let tray_icon = match TrayIconBuilder::new()
        .with_icon(icon)
        .with_tooltip(&tooltip_str)
        .with_menu(Box::new(menu.clone()))
        .build()
    {
        Ok(t) => t,
        Err(e) => {
            eprintln!("Failed to create tray icon: {}", e);
            return -1;
        }
    };
    
    // Store state
    let state = TrayState {
        icon: tray_icon,
        menu,
        vdd_submenu,
        advanced_settings_submenu,
        language_submenu,
        visit_project_submenu,
        menu_items,
    };
    
    if let Some(state_mutex) = TRAY_STATE.get() {
        *state_mutex.lock() = Some(state);
    }
    
    0
}

/// Run one iteration of the event loop
/// 
/// # Arguments
/// * `blocking` - If non-zero, block until an event is available
/// 
/// # Returns
/// 0 on success, -1 if exit was requested
#[no_mangle]
pub extern "C" fn tray_loop(blocking: c_int) -> c_int {
    if SHOULD_EXIT.load(Ordering::SeqCst) {
        return -1;
    }

    #[cfg(target_os = "windows")]
    {
        unsafe {
            let mut msg: MSG = std::mem::zeroed();

            if blocking != 0 {
                if GetMessageW(&mut msg, std::ptr::null_mut(), 0, 0) <= 0 {
                    return -1;
                }
            } else {
                if PeekMessageW(&mut msg, std::ptr::null_mut(), 0, 0, PM_REMOVE) == 0 {
                    return 0;
                }
            }

            if msg.message == WM_QUIT {
                return -1;
            }

            // Handle DPI change - refresh icon with new size
            if msg.message == WM_DPICHANGED || check_dpi_changed() {
                refresh_icon_for_dpi();
            }

            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        // Process menu events - use process_menu_event to avoid deadlocks
        if let Ok(event) = MenuEvent::receiver().try_recv() {
            process_menu_event(&event);
        }

        if SHOULD_EXIT.load(Ordering::SeqCst) {
            return -1;
        }

        0
    }

    #[cfg(target_os = "linux")]
    {
        // GTK event loop
        while gtk::events_pending() {
            gtk::main_iteration();
        }

        if blocking != 0 {
            std::thread::sleep(std::time::Duration::from_millis(100));
        }

        // Process menu events - use process_menu_event to avoid deadlocks
        if let Ok(event) = MenuEvent::receiver().try_recv() {
            process_menu_event(&event);
        }

        if SHOULD_EXIT.load(Ordering::SeqCst) {
            return -1;
        }

        0
    }

    #[cfg(target_os = "macos")]
    {
        use objc2::rc::autoreleasepool;
        use objc2_foundation::{NSDate, NSRunLoop};

        autoreleasepool(|_| {
            unsafe {
                let run_loop = NSRunLoop::currentRunLoop();
                let interval = if blocking != 0 { 0.1 } else { 0.0 };
                let date = NSDate::dateWithTimeIntervalSinceNow(interval);
                run_loop.runUntilDate(&date);
            }
        });

        // Process menu events - use process_menu_event to avoid deadlocks
        if let Ok(event) = MenuEvent::receiver().try_recv() {
            process_menu_event(&event);
        }

        if SHOULD_EXIT.load(Ordering::SeqCst) {
            return -1;
        }

        0
    }

    #[cfg(not(any(target_os = "windows", target_os = "linux", target_os = "macos")))]
    {
        -1
    }
}

/// Exit the tray event loop
#[no_mangle]
pub extern "C" fn tray_exit() {
    SHOULD_EXIT.store(true, Ordering::SeqCst);

    #[cfg(target_os = "windows")]
    unsafe {
        PostQuitMessage(0);
    }

    #[cfg(target_os = "linux")]
    {
        gtk::main_quit();
    }

    // Clean up state
    if let Some(state_mutex) = TRAY_STATE.get() {
        *state_mutex.lock() = None;
    }
}

/// Set the tray icon
/// 
/// # Arguments
/// * `icon_type` - 0=normal, 1=playing, 2=pausing, 3=locked
#[no_mangle]
pub extern "C" fn tray_set_icon(icon_type: c_int) {
    // Store current icon type for DPI change refresh
    #[cfg(target_os = "windows")]
    CURRENT_ICON_TYPE.store(icon_type, Ordering::SeqCst);

    let icon_paths = match ICON_PATHS.get() {
        Some(p) => p,
        None => return,
    };
    
    let icon_path = match icon_type {
        0 => &icon_paths.normal,
        1 => &icon_paths.playing,
        2 => &icon_paths.pausing,
        3 => &icon_paths.locked,
        _ => &icon_paths.normal,
    };
    
    if let Some(icon) = load_icon(icon_path) {
        if let Some(state_mutex) = TRAY_STATE.get() {
            let state_guard = state_mutex.lock();
            if let Some(ref state) = *state_guard {
                let _ = state.icon.set_icon(Some(icon));
            }
        }
    }
}

/// Set the tray tooltip
#[no_mangle]
pub unsafe extern "C" fn tray_set_tooltip(tooltip: *const c_char) {
    if let Some(tip) = c_str_to_string(tooltip) {
        if let Some(state_mutex) = TRAY_STATE.get() {
            let state_guard = state_mutex.lock();
            if let Some(ref state) = *state_guard {
                let _ = state.icon.set_tooltip(Some(&tip));
            }
        }
    }
}

/// Update the VDD create menu item state
#[no_mangle]
pub extern "C" fn tray_set_vdd_create_state(checked: c_int, enabled: c_int) {
    if let Some(state_mutex) = TRAY_STATE.get() {
        let state_guard = state_mutex.lock();
        if let Some(ref state) = *state_guard {
            state.menu_items.vdd_create.set_checked(checked != 0);
            state.menu_items.vdd_create.set_enabled(enabled != 0);
        }
    }
}

/// Update the VDD close menu item state
#[no_mangle]
pub extern "C" fn tray_set_vdd_close_state(checked: c_int, enabled: c_int) {
    if let Some(state_mutex) = TRAY_STATE.get() {
        let state_guard = state_mutex.lock();
        if let Some(ref state) = *state_guard {
            state.menu_items.vdd_close.set_checked(checked != 0);
            state.menu_items.vdd_close.set_enabled(enabled != 0);
        }
    }
}

/// Update the VDD persistent menu item state
#[no_mangle]
pub extern "C" fn tray_set_vdd_persistent_state(checked: c_int) {
    if let Some(state_mutex) = TRAY_STATE.get() {
        let state_guard = state_mutex.lock();
        if let Some(ref state) = *state_guard {
            state.menu_items.vdd_persistent.set_checked(checked != 0);
        }
    }
}

/// Set the current locale
#[no_mangle]
pub unsafe extern "C" fn tray_set_locale(locale: *const c_char) {
    if let Some(loc) = c_str_to_string(locale) {
        set_locale_str(&loc);
        update_menu_texts();
    }
}

/// Show a notification (placeholder - needs platform-specific implementation)
#[no_mangle]
pub unsafe extern "C" fn tray_show_notification(
    title: *const c_char,
    text: *const c_char,
    _icon_type: c_int,
) {
    let title_str = c_str_to_string(title).unwrap_or_default();
    let text_str = c_str_to_string(text).unwrap_or_default();
    
    // Log for now - proper notification support needs platform-specific implementation
    eprintln!("Notification: {} - {}", title_str, text_str);
}

// ============================================================================
// Legacy C API compatibility (for existing C++ code)
// ============================================================================

/// Legacy tray structure (for compatibility)
#[repr(C)]
pub struct tray {
    pub icon: *const c_char,
    pub tooltip: *const c_char,
    pub notification_icon: *const c_char,
    pub notification_text: *const c_char,
    pub notification_title: *const c_char,
    pub notification_cb: Option<extern "C" fn()>,
    pub menu: *mut tray_menu,
    pub iconPathCount: c_int,
    pub allIconPaths: [*const c_char; 4],
}

/// Legacy tray menu structure (for compatibility)
#[repr(C)]
pub struct tray_menu {
    pub text: *const c_char,
    pub disabled: c_int,
    pub checked: c_int,
    pub checkbox: c_int,
    pub cb: Option<extern "C" fn(*mut tray_menu)>,
    pub context: *mut std::ffi::c_void,
    pub submenu: *mut tray_menu,
}

/// Legacy tray_init - not recommended, use tray_init_ex instead
#[no_mangle]
pub unsafe extern "C" fn tray_init(_tray: *mut tray) -> c_int {
    eprintln!("Warning: tray_init is deprecated, use tray_init_ex instead");
    -1
}

/// Legacy tray_update - partially supported
#[no_mangle]
pub unsafe extern "C" fn tray_update(t: *mut tray) {
    if t.is_null() {
        return;
    }
    
    let tray_ref = &*t;
    
    // Update icon if changed
    if !tray_ref.icon.is_null() {
        if let Some(icon_str) = c_str_to_string(tray_ref.icon) {
            if let Some(icon) = load_icon(&icon_str) {
                if let Some(state_mutex) = TRAY_STATE.get() {
                    let state_guard = state_mutex.lock();
                    if let Some(ref state) = *state_guard {
                        let _ = state.icon.set_icon(Some(icon));
                    }
                }
            }
        }
    }
    
    // Update tooltip
    if !tray_ref.tooltip.is_null() {
        if let Some(tip) = c_str_to_string(tray_ref.tooltip) {
            if let Some(state_mutex) = TRAY_STATE.get() {
                let state_guard = state_mutex.lock();
                if let Some(ref state) = *state_guard {
                    let _ = state.icon.set_tooltip(Some(&tip));
                }
            }
        }
    }
    
    // Handle notifications
    if !tray_ref.notification_title.is_null() && !tray_ref.notification_text.is_null() {
        let title = c_str_to_string(tray_ref.notification_title).unwrap_or_default();
        let text = c_str_to_string(tray_ref.notification_text).unwrap_or_default();
        if !title.is_empty() || !text.is_empty() {
            tray_show_notification(
                tray_ref.notification_title,
                tray_ref.notification_text,
                0,
            );
        }
    }
}
