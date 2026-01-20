//! Sunshine Tray - Rust implementation of the system tray (Windows only)
//!
//! This library provides a complete system tray implementation with:
//! - Multi-language support (Chinese, English, Japanese)
//! - Menu management
//! - Notification support
//! - Icon management
//!
//! Note: This crate is Windows-only.

#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]
#![cfg(target_os = "windows")]

pub mod i18n;
pub mod actions;
pub mod config;
pub mod menu;
pub mod menu_items;

use std::ffi::CStr;
use std::os::raw::{c_char, c_int};
use std::path::Path;
use std::sync::atomic::{AtomicBool, AtomicI32, Ordering};

use muda::{Menu, MenuEvent};
use once_cell::sync::OnceCell;
use parking_lot::Mutex;
use tray_icon::{Icon, TrayIcon, TrayIconBuilder};
use windows_sys::Win32::UI::WindowsAndMessaging::{
    DispatchMessageW, GetMessageW, PeekMessageW, PostQuitMessage, TranslateMessage, 
    MSG, WM_QUIT, PM_REMOVE, WM_DPICHANGED,
};

use i18n::set_locale_str;
use actions::{register_callback, ActionCallback};

/// Tray state - simplified to use menu registry
#[allow(dead_code)]  // Fields are needed for lifetime management
struct TrayState {
    icon: TrayIcon,
    menu: Menu,
}

// Safety: TrayState is only accessed from the main thread
unsafe impl Send for TrayState {}
unsafe impl Sync for TrayState {}

/// Global state
static TRAY_STATE: OnceCell<Mutex<Option<TrayState>>> = OnceCell::new();
static SHOULD_EXIT: AtomicBool = AtomicBool::new(false);

/// Current icon type (0=normal, 1=playing, 2=pausing, 3=locked)
/// Used to refresh icon when DPI changes
static CURRENT_ICON_TYPE: AtomicI32 = AtomicI32::new(0);

/// Cached DPI value to detect DPI changes
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

/// Load icon from ICO file path
fn load_icon(icon_str: &str) -> Option<Icon> {
    if Path::new(icon_str).exists() {
        return load_icon_from_path(icon_str);
    }

    eprintln!("Icon not found: {}", icon_str);
    None
}

/// Identify which action corresponds to the menu event
/// Returns the item_id (if any)
fn identify_menu_item(event: &MenuEvent) -> Option<String> {
    menu::identify_item_id(event)
}

/// Execute the identified action by item_id
/// Uses menu_items module for centralized handling
fn execute_action_by_id(item_id: &str) {
    let (handled, needs_rebuild) = menu_items::execute_handler(item_id);
    
    if handled && needs_rebuild {
        update_menu_texts();
    }
}

/// Process a menu event - identifies the item and executes its handler
fn process_menu_event(event: &MenuEvent) {
    if let Some(item_id) = identify_menu_item(event) {
        execute_action_by_id(&item_id);
    }
}

/// Update menu texts after language change by rebuilding the menu
/// 
/// On Windows, simply updating menu item texts with set_text() and calling set_menu()
/// can cause issues with the menu event handling. The safest approach is to rebuild
/// the entire menu with the new texts.
fn update_menu_texts() {
    use menu_items::ids;
    
    // Save current VDD states
    let vdd_create_checked = menu::get_check_state_by_id(ids::VDD_CREATE).unwrap_or(false);
    let vdd_close_checked = menu::get_check_state_by_id(ids::VDD_CLOSE).unwrap_or(false);
    let vdd_persistent_checked = menu::get_check_state_by_id(ids::VDD_PERSISTENT).unwrap_or(false);
    
    // Build new menu using the menu module
    let new_menu = menu::rebuild_menu();
    
    // Restore VDD states
    menu::set_check_state_by_id(ids::VDD_CREATE, vdd_create_checked);
    menu::set_check_state_by_id(ids::VDD_CLOSE, vdd_close_checked);
    menu::set_check_state_by_id(ids::VDD_PERSISTENT, vdd_persistent_checked);
    
    // Update tray state
    if let Some(state_mutex) = TRAY_STATE.get() {
        let mut state_guard = state_mutex.lock();
        if let Some(ref mut state) = *state_guard {
            let _ = state.icon.set_menu(Some(Box::new(new_menu.clone())));
            state.menu = new_menu;
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
    
    // Build menu using the menu module
    let menu = menu::rebuild_menu();
    
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

/// Exit the tray event loop
#[no_mangle]
pub extern "C" fn tray_exit() {
    SHOULD_EXIT.store(true, Ordering::SeqCst);

    unsafe {
        PostQuitMessage(0);
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

/// Update VDD menu item states
/// 
/// This unified function is called from C++ to update all VDD menu states at once.
/// The C++ side is responsible for:
/// - Tracking VDD active state
/// - Managing 10-second cooldown
/// - Determining which operations are allowed
/// 
/// # Parameters
/// * `can_create` - 1 if Create item should be enabled, 0 otherwise
/// * `can_close` - 1 if Close item should be enabled, 0 otherwise  
/// * `is_persistent` - 1 if Keep Enabled is checked, 0 otherwise
/// * `is_active` - 1 if VDD is currently active, 0 otherwise
#[no_mangle]
pub extern "C" fn tray_update_vdd_menu(
    can_create: c_int,
    can_close: c_int,
    is_persistent: c_int,
    is_active: c_int,
) {
    menu::update_vdd_menu_state(
        can_create != 0,
        can_close != 0,
        is_persistent != 0,
        is_active != 0,
    );
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
