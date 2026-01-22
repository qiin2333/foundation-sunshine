/**
 * @file rust_tray.h
 * @brief C API for the Rust tray library
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Menu action identifiers (must match Rust MenuAction enum)
 */
typedef enum {
    TRAY_ACTION_OPEN_UI = 1,
    // VDD submenu actions
    TRAY_ACTION_VDD_CREATE = 2,
    TRAY_ACTION_VDD_CLOSE = 3,
    TRAY_ACTION_VDD_PERSISTENT = 4,
    // Reserved: 5, 6, 7 (removed import/export/reset config)
    TRAY_ACTION_CLOSE_APP = 8,
    // Language actions
    TRAY_ACTION_LANGUAGE_CHINESE = 9,
    TRAY_ACTION_LANGUAGE_ENGLISH = 10,
    TRAY_ACTION_LANGUAGE_JAPANESE = 11,
    TRAY_ACTION_STAR_PROJECT = 12,
    // Visit Project actions
    TRAY_ACTION_VISIT_PROJECT_SUNSHINE = 13,
    TRAY_ACTION_VISIT_PROJECT_MOONLIGHT = 14,
    TRAY_ACTION_RESET_DISPLAY_DEVICE_CONFIG = 15,
    TRAY_ACTION_RESTART = 16,
    TRAY_ACTION_QUIT = 17,
    TRAY_ACTION_NOTIFICATION_CLICKED = 18,
} TrayAction;

/**
 * @brief Icon types for tray_set_icon
 */
typedef enum {
    TRAY_ICON_TYPE_NORMAL = 0,
    TRAY_ICON_TYPE_PLAYING = 1,
    TRAY_ICON_TYPE_PAUSING = 2,
    TRAY_ICON_TYPE_LOCKED = 3,
} TrayIconType;

/**
 * @brief Callback function type for menu actions
 * @param action The action identifier
 */
typedef void (*TrayActionCallback)(uint32_t action);

/**
 * @brief Initialize the tray with extended options
 * @param icon_normal Path to normal icon
 * @param icon_playing Path to playing icon
 * @param icon_pausing Path to pausing icon
 * @param icon_locked Path to locked icon
 * @param tooltip Tooltip text
 * @param locale Initial locale (e.g., "zh", "en", "ja")
 * @param config_file Path to the Sunshine configuration file (sunshine.conf)
 * @param callback Callback function for menu actions
 * @return 0 on success, -1 on error
 */
int tray_init_ex(
    const char* icon_normal,
    const char* icon_playing,
    const char* icon_pausing,
    const char* icon_locked,
    const char* tooltip,
    const char* locale,
    const char* config_file,
    TrayActionCallback callback
);

/**
 * @brief Run one iteration of the event loop
 * @param blocking If non-zero, block until an event is available
 * @return 0 on success, -1 if exit was requested
 */
int tray_loop(int blocking);

/**
 * @brief Exit the tray event loop
 */
void tray_exit(void);

/**
 * @brief Set the tray icon
 * @param icon_type Icon type (0=normal, 1=playing, 2=pausing, 3=locked)
 */
void tray_set_icon(int icon_type);

/**
 * @brief Set the tray tooltip
 * @param tooltip Tooltip text
 */
void tray_set_tooltip(const char* tooltip);

/**
 * @brief Update VDD menu item states
 * 
 * This unified function updates all VDD menu states at once.
 * The C++ side is responsible for:
 * - Tracking VDD active state
 * - Managing 10-second cooldown
 * - Determining which operations are allowed
 * 
 * @param can_create Non-zero if "Create" item should be enabled
 * @param can_close Non-zero if "Close" item should be enabled
 * @param is_persistent Non-zero if "Keep Enabled" is checked
 * @param is_active Non-zero if VDD is currently active (for checked states)
 */
void tray_update_vdd_menu(int can_create, int can_close, int is_persistent, int is_active);

/**
 * @brief Set the current locale
 * @param locale Locale string (e.g., "zh", "en", "ja")
 */
void tray_set_locale(const char* locale);

/**
 * @brief Notification types for localized notifications
 */
typedef enum {
    TRAY_NOTIFICATION_STREAM_STARTED = 0,
    TRAY_NOTIFICATION_STREAM_PAUSED = 1,
    TRAY_NOTIFICATION_APP_STOPPED = 2,
    TRAY_NOTIFICATION_PAIRING_REQUEST = 3,
} TrayNotificationType;

/**
 * @brief Show a notification
 * @param title Notification title
 * @param text Notification text
 * @param icon_type Icon type for the notification
 */
void tray_show_notification(const char* title, const char* text, int icon_type);

/**
 * @brief Show a localized notification
 * @param notification_type Type of notification (see TrayNotificationType)
 * @param app_name Application name for formatting (can be NULL)
 */
void tray_show_localized_notification(int notification_type, const char* app_name);

/**
 * @brief Enable dark mode for context menus (follow system setting)
 * 
 * Call this before creating menus. The menu will automatically
 * follow the system's dark/light mode setting.
 * Note: Only effective on Windows 10 1903+ and Windows 11.
 */
void tray_enable_dark_mode(void);

/**
 * @brief Force dark mode for context menus
 */
void tray_force_dark_mode(void);

/**
 * @brief Force light mode for context menus
 */
void tray_force_light_mode(void);

#ifdef __cplusplus
}
#endif
