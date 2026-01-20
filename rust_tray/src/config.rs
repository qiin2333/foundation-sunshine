//! Configuration file operations module (Windows only)
//! 
//! Provides functionality for reading, writing, importing, exporting,
//! and resetting the Sunshine configuration file.
//! 
//! The configuration file path is obtained from C++ via the tray_init_ex function,
//! which provides the exact path used by the main Sunshine application.

use std::collections::HashMap;
use std::ffi::OsStr;
use std::fs;
use std::os::windows::ffi::OsStrExt;
use std::path::PathBuf;

use crate::i18n::{get_string, StringKey};
use crate::get_config_file_path_from_cpp;

/// Result type for config operations
pub type ConfigResult<T> = Result<T, ConfigError>;

/// Error types for configuration operations
#[derive(Debug, Clone)]
pub enum ConfigError {
    PathNotFound,
    ReadFailed(String),
    WriteFailed(String),
    NoConfigFound,
    DialogCancelled,
    NoUserSession,
}

impl std::fmt::Display for ConfigError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            ConfigError::PathNotFound => write!(f, "Configuration path not found"),
            ConfigError::ReadFailed(msg) => write!(f, "Read failed: {}", msg),
            ConfigError::WriteFailed(msg) => write!(f, "Write failed: {}", msg),
            ConfigError::NoConfigFound => write!(f, "No configuration found"),
            ConfigError::DialogCancelled => write!(f, "Dialog cancelled"),
            ConfigError::NoUserSession => write!(f, "No active user session"),
        }
    }
}

/// Get the Sunshine configuration file path
/// 
/// The path is provided by C++ via tray_init_ex, ensuring consistency
/// with the main Sunshine application's configuration path.
/// with the main Sunshine application's configuration path.
pub fn get_config_file_path() -> Option<PathBuf> {
    get_config_file_path_from_cpp()
        .map(PathBuf::from)
        .filter(|p| p.exists())
}

/// Parse configuration file content into a key-value map
pub fn parse_config(content: &str) -> HashMap<String, String> {
    let mut vars = HashMap::new();
    
    for line in content.lines() {
        let trimmed = line.trim();
        
        // Skip empty lines and comments
        if trimmed.is_empty() || trimmed.starts_with('#') {
            continue;
        }
        
        // Parse key = value
        if let Some(pos) = trimmed.find('=') {
            let key = trimmed[..pos].trim().to_string();
            let value = trimmed[pos + 1..].trim().to_string();
            if !key.is_empty() {
                vars.insert(key, value);
            }
        }
    }
    
    vars
}

/// Write configuration map to string
pub fn serialize_config(vars: &HashMap<String, String>) -> String {
    let mut config_str = String::new();
    
    for (key, value) in vars {
        if !value.is_empty() && value != "null" {
            config_str.push_str(&format!("{} = {}\n", key, value));
        }
    }
    
    config_str
}

/// Read the configuration file
pub fn read_config() -> ConfigResult<HashMap<String, String>> {
    let path = get_config_file_path().ok_or(ConfigError::PathNotFound)?;
    
    let content = fs::read_to_string(&path)
        .map_err(|e| ConfigError::ReadFailed(e.to_string()))?;
    
    Ok(parse_config(&content))
}

/// Write configuration to file
pub fn write_config(vars: &HashMap<String, String>) -> ConfigResult<()> {
    let path = get_config_file_path().ok_or(ConfigError::PathNotFound)?;
    
    let content = serialize_config(vars);
    
    fs::write(&path, content)
        .map_err(|e| ConfigError::WriteFailed(e.to_string()))
}

/// Save a single configuration value
pub fn save_config_value(key: &str, value: &str) -> ConfigResult<()> {
    let mut vars = read_config().unwrap_or_default();
    vars.insert(key.to_string(), value.to_string());
    write_config(&vars)
}

/// Show message box
pub fn show_message_box(title: &str, message: &str, is_error: bool) {
    use windows_sys::Win32::UI::WindowsAndMessaging::{MessageBoxW, MB_OK, MB_ICONERROR, MB_ICONINFORMATION};
    
    let wide_title: Vec<u16> = OsStr::new(title)
        .encode_wide()
        .chain(std::iter::once(0))
        .collect();
    
    let wide_message: Vec<u16> = OsStr::new(message)
        .encode_wide()
        .chain(std::iter::once(0))
        .collect();
    
    let icon = if is_error { MB_ICONERROR } else { MB_ICONINFORMATION };
    
    unsafe {
        MessageBoxW(
            std::ptr::null_mut(),
            wide_message.as_ptr(),
            wide_title.as_ptr(),
            MB_OK | icon,
        );
    }
}

/// Show confirmation dialog
pub fn show_confirm_dialog(title: &str, message: &str) -> bool {
    use windows_sys::Win32::UI::WindowsAndMessaging::{MessageBoxW, MB_YESNO, MB_ICONQUESTION, IDYES};
    
    let wide_title: Vec<u16> = OsStr::new(title)
        .encode_wide()
        .chain(std::iter::once(0))
        .collect();
    
    let wide_message: Vec<u16> = OsStr::new(message)
        .encode_wide()
        .chain(std::iter::once(0))
        .collect();
    
    unsafe {
        let result = MessageBoxW(
            std::ptr::null_mut(),
            wide_message.as_ptr(),
            wide_title.as_ptr(),
            MB_YESNO | MB_ICONQUESTION,
        );
        result == IDYES
    }
}

/// Open file dialog for importing config
pub fn open_import_dialog() -> ConfigResult<PathBuf> {
    use windows_sys::Win32::UI::Controls::Dialogs::{
        GetOpenFileNameW, OPENFILENAMEW, OFN_EXPLORER, OFN_FILEMUSTEXIST, OFN_PATHMUSTEXIST,
    };
    
    let mut file_name: [u16; 260] = [0; 260];
    
    // Build filter string: "Config Files (*.conf)\0*.conf\0All Files (*.*)\0*.*\0\0"
    let filter_label1 = get_string(StringKey::FileDialogConfigFiles);
    let filter_label2 = get_string(StringKey::FileDialogAllFiles);
    let filter = format!("{} (*.conf)\0*.conf\0{} (*.*)\0*.*\0\0", filter_label1, filter_label2);
    let filter_wide: Vec<u16> = filter.encode_utf16().collect();
    
    let title = get_string(StringKey::FileDialogSelectImport);
    let title_wide: Vec<u16> = OsStr::new(title)
        .encode_wide()
        .chain(std::iter::once(0))
        .collect();
    
    let mut ofn: OPENFILENAMEW = unsafe { std::mem::zeroed() };
    ofn.lStructSize = std::mem::size_of::<OPENFILENAMEW>() as u32;
    ofn.lpstrFilter = filter_wide.as_ptr();
    ofn.lpstrFile = file_name.as_mut_ptr();
    ofn.nMaxFile = 260;
    ofn.lpstrTitle = title_wide.as_ptr();
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    
    unsafe {
        if GetOpenFileNameW(&mut ofn) != 0 {
            let len = file_name.iter().position(|&c| c == 0).unwrap_or(260);
            let path_str = String::from_utf16_lossy(&file_name[..len]);
            Ok(PathBuf::from(path_str))
        } else {
            Err(ConfigError::DialogCancelled)
        }
    }
}

/// Open file dialog for exporting config
pub fn open_export_dialog() -> ConfigResult<PathBuf> {
    use windows_sys::Win32::UI::Controls::Dialogs::{
        GetSaveFileNameW, OPENFILENAMEW, OFN_EXPLORER, OFN_OVERWRITEPROMPT,
    };
    
    let mut file_name: [u16; 260] = [0; 260];
    
    // Default filename
    let default_name = "sunshine_backup.conf";
    for (i, c) in default_name.encode_utf16().enumerate() {
        if i < 259 {
            file_name[i] = c;
        }
    }
    
    // Build filter string
    let filter_label1 = get_string(StringKey::FileDialogConfigFiles);
    let filter_label2 = get_string(StringKey::FileDialogAllFiles);
    let filter = format!("{} (*.conf)\0*.conf\0{} (*.*)\0*.*\0\0", filter_label1, filter_label2);
    let filter_wide: Vec<u16> = filter.encode_utf16().collect();
    
    let title = get_string(StringKey::FileDialogSaveExport);
    let title_wide: Vec<u16> = OsStr::new(title)
        .encode_wide()
        .chain(std::iter::once(0))
        .collect();
    
    let mut ofn: OPENFILENAMEW = unsafe { std::mem::zeroed() };
    ofn.lStructSize = std::mem::size_of::<OPENFILENAMEW>() as u32;
    ofn.lpstrFilter = filter_wide.as_ptr();
    ofn.lpstrFile = file_name.as_mut_ptr();
    ofn.nMaxFile = 260;
    ofn.lpstrTitle = title_wide.as_ptr();
    ofn.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT;
    
    unsafe {
        if GetSaveFileNameW(&mut ofn) != 0 {
            let len = file_name.iter().position(|&c| c == 0).unwrap_or(260);
            let path_str = String::from_utf16_lossy(&file_name[..len]);
            let mut path = PathBuf::from(path_str);
            
            // Ensure .conf extension
            if path.extension().map_or(true, |ext| ext != "conf") {
                path.set_extension("conf");
            }
            
            Ok(path)
        } else {
            Err(ConfigError::DialogCancelled)
        }
    }
}

/// Import configuration from file
pub fn import_config() -> ConfigResult<()> {
    // Open file dialog
    let source_path = open_import_dialog()?;
    
    // Read source file
    let content = fs::read_to_string(&source_path)
        .map_err(|e| ConfigError::ReadFailed(e.to_string()))?;
    
    // Get destination path
    let dest_path = get_config_file_path().ok_or(ConfigError::PathNotFound)?;
    
    // Write to config file
    fs::write(&dest_path, content)
        .map_err(|e| ConfigError::WriteFailed(e.to_string()))?;
    
    show_message_box(
        get_string(StringKey::ImportSuccessTitle),
        get_string(StringKey::ImportSuccessMsg),
        false,
    );
    
    Ok(())
}

/// Export configuration to file
pub fn export_config() -> ConfigResult<()> {
    // Get source config path
    let source_path = get_config_file_path().ok_or(ConfigError::NoConfigFound)?;
    
    // Read current config
    let content = fs::read_to_string(&source_path)
        .map_err(|e| ConfigError::ReadFailed(e.to_string()))?;
    
    // Open save dialog
    let dest_path = open_export_dialog()?;
    
    // Write to destination
    fs::write(&dest_path, content)
        .map_err(|e| ConfigError::WriteFailed(e.to_string()))?;
    
    show_message_box(
        get_string(StringKey::ExportSuccessTitle),
        get_string(StringKey::ExportSuccessMsg),
        false,
    );
    
    Ok(())
}

/// Reset configuration to default
pub fn reset_config() -> ConfigResult<()> {
    // Show confirmation dialog
    if !show_confirm_dialog(
        get_string(StringKey::ResetConfirmTitle),
        get_string(StringKey::ResetConfirmMsg),
    ) {
        return Err(ConfigError::DialogCancelled);
    }
    
    // Get config path
    let config_path = get_config_file_path().ok_or(ConfigError::PathNotFound)?;
    
    // Create empty config (or minimal default)
    let default_config = "# Sunshine Configuration\n# Reset to default\n";
    
    fs::write(&config_path, default_config)
        .map_err(|e| ConfigError::WriteFailed(e.to_string()))?;
    
    show_message_box(
        get_string(StringKey::ResetSuccessTitle),
        get_string(StringKey::ResetSuccessMsg),
        false,
    );
    
    Ok(())
}

/// Save tray locale to configuration file
pub fn save_tray_locale(locale: &str) -> ConfigResult<()> {
    save_config_value("tray_locale", locale)
}

#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_parse_config() {
        let content = r#"
# Comment line
key1 = value1
key2 = value2

key3=value3
"#;
        let vars = parse_config(content);
        assert_eq!(vars.get("key1"), Some(&"value1".to_string()));
        assert_eq!(vars.get("key2"), Some(&"value2".to_string()));
        assert_eq!(vars.get("key3"), Some(&"value3".to_string()));
    }
    
    #[test]
    fn test_serialize_config() {
        let mut vars = HashMap::new();
        vars.insert("key1".to_string(), "value1".to_string());
        vars.insert("key2".to_string(), "value2".to_string());
        
        let result = serialize_config(&vars);
        assert!(result.contains("key1 = value1"));
        assert!(result.contains("key2 = value2"));
    }
}
