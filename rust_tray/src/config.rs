//! Configuration file operations module (Windows only)
//! 
//! 提供读取、写入配置文件的功能。
//! 
//! 配置文件路径通过 C++ 的 tray_init_ex 函数获取，
//! 确保与主 Sunshine 应用程序使用相同的配置路径。
//! 
//! 注意：导入/导出/重置配置的功能已迁移到 C++ 的 config_operations.cpp 中实现。

use std::collections::HashMap;
use std::ffi::OsStr;
use std::fs;
use std::os::windows::ffi::OsStrExt;
use std::path::PathBuf;

use crate::get_config_file_path_from_cpp;

/// 配置操作的结果类型
pub type ConfigResult<T> = Result<T, ConfigError>;

/// 配置操作的错误类型
#[derive(Debug, Clone)]
pub enum ConfigError {
    PathNotFound,
    ReadFailed(String),
    WriteFailed(String),
    DialogCancelled,
}

impl std::fmt::Display for ConfigError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            ConfigError::PathNotFound => write!(f, "Configuration path not found"),
            ConfigError::ReadFailed(msg) => write!(f, "Read failed: {}", msg),
            ConfigError::WriteFailed(msg) => write!(f, "Write failed: {}", msg),
            ConfigError::DialogCancelled => write!(f, "Dialog cancelled"),
        }
    }
}

/// 获取 Sunshine 配置文件路径
/// 
/// 路径由 C++ 通过 tray_init_ex 提供，确保与主 Sunshine 应用程序的配置路径一致。
pub fn get_config_file_path() -> Option<PathBuf> {
    get_config_file_path_from_cpp()
        .map(PathBuf::from)
        .filter(|p| p.exists())
}

/// 将配置文件内容解析为键值对映射
pub fn parse_config(content: &str) -> HashMap<String, String> {
    let mut vars = HashMap::new();
    
    for line in content.lines() {
        let trimmed = line.trim();
        
        // 跳过空行和注释
        if trimmed.is_empty() || trimmed.starts_with('#') {
            continue;
        }
        
        // 解析 key = value
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

/// 将配置映射序列化为字符串
pub fn serialize_config(vars: &HashMap<String, String>) -> String {
    let mut config_str = String::new();
    
    for (key, value) in vars {
        if !value.is_empty() && value != "null" {
            config_str.push_str(&format!("{} = {}\n", key, value));
        }
    }
    
    config_str
}

/// 读取配置文件
pub fn read_config() -> ConfigResult<HashMap<String, String>> {
    let path = get_config_file_path().ok_or(ConfigError::PathNotFound)?;
    
    let content = fs::read_to_string(&path)
        .map_err(|e| ConfigError::ReadFailed(e.to_string()))?;
    
    Ok(parse_config(&content))
}

/// 写入配置到文件
pub fn write_config(vars: &HashMap<String, String>) -> ConfigResult<()> {
    let path = get_config_file_path().ok_or(ConfigError::PathNotFound)?;
    
    let content = serialize_config(vars);
    
    fs::write(&path, content)
        .map_err(|e| ConfigError::WriteFailed(e.to_string()))
}

/// 保存单个配置值
pub fn save_config_value(key: &str, value: &str) -> ConfigResult<()> {
    let mut vars = read_config().unwrap_or_default();
    vars.insert(key.to_string(), value.to_string());
    write_config(&vars)
}

/// 显示确认对话框
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

/// 保存托盘语言设置到配置文件
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
