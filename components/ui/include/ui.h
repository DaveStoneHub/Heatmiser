/*
 * The touchscreen in the Copper design: dashboard of zone tiles, zone detail,
 * schedule, hot water, rename, settings, zones and scan, bus diagnostics and
 * system. Pure LVGL 9 on top of zone_store.h, so the same code runs on the
 * board and in the desktop preview (tools/ui_preview).
 *
 * Call everything here with the LVGL lock held (lvgl_port_lock on the board).
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Build the screens and load the dashboard. */
void ui_init(void);

/* Blank the screen after this many seconds without a touch (0 = never).
 * The touch that wakes it is not passed on. */
void ui_set_blank_timeout(uint32_t seconds);
uint32_t ui_blank_timeout(void);

/* The drift test pattern for the display soak: a 1 px frame and corner marks
 * at the panel edges, a moving bar that keeps LVGL drawing, and a status
 * line. Screen blanking is off while it shows. */
void ui_set_soak(bool on);
bool ui_soak_on(void);

/* Show a short message at the bottom of the screen. */
void ui_toast(const char *msg);

/* Which screen is showing, for the console and tests. */
typedef enum {
    UI_SCREEN_DASH = 0,
    UI_SCREEN_ZONE,
    UI_SCREEN_SCHEDULE,
    UI_SCREEN_HOTWATER,
    UI_SCREEN_RENAME,
    UI_SCREEN_SETTINGS,
    UI_SCREEN_ZONES,
    UI_SCREEN_DIAG,
    UI_SCREEN_SYSTEM,
    UI_SCREEN_MATTER,
    UI_SCREEN_COUNT
} ui_screen_t;
ui_screen_t ui_screen(void);
void ui_show_dash(void);
void ui_show_zone(uint8_t addr);
void ui_show_schedule(uint8_t addr, bool hot_water);
void ui_show_hotwater(uint8_t addr);
void ui_show_rename(uint8_t addr);
void ui_show_settings(void);
void ui_show_zones(void);
void ui_show_diag(void);
void ui_show_system(void);
void ui_show_matter(void);

/* ---- provided by the platform (main/ui_platform.c, or the preview) ---------- */

typedef struct {
    bool     wifi_configured;
    bool     wifi_up;
    char     ssid[33];
    char     ip[16];
    int      rssi;
    bool     time_set;           /* SNTP has synced */
    uint32_t uptime_s;
    uint32_t heap_int_free, heap_int_min, heap_psram_free;
    uint32_t frames;             /* display refreshes */
    const char *version;
} ui_sys_info_t;

void ui_platform_sys_info(ui_sys_info_t *out);
void ui_platform_backlight(bool on);
/* Soak mode switched on the screen: start or stop the Wi-Fi and flash load. */
void ui_platform_soak(bool on);
/* "Picture shifted" pressed during the soak: log it and restart the panel DMA. */
void ui_platform_drift_seen(void);
/* Screen timeout chosen on the settings screen: save it. */
void ui_platform_save_blank_timeout(uint32_t seconds);

/* Timezones offered on the settings screen. */
typedef struct {
    const char *name;            /* "Europe/London" */
    const char *posix;           /* "GMT0BST,M3.5.0/1,M10.5.0" */
} ui_tz_t;
extern const ui_tz_t ui_timezones[];
extern const size_t ui_timezone_count;
/* The POSIX TZ string in use, and set (and save) a new one. */
const char *ui_platform_tz(void);
void ui_platform_set_tz(const char *posix);

/* Factory reset confirmed on the screen: forget everything and restart. */
void ui_platform_factory_reset(void);

/* Matter, for the Matter screen. */
#define UI_MATTER_FABRICS 5
typedef struct {
    uint8_t  index;              /* the fabric's index on the bridge */
    uint16_t vendor;             /* the controller's vendor ID */
    char     label[33];          /* fabric label, often empty */
} ui_matter_fabric_t;

typedef struct {
    bool     running;            /* the Matter stack started */
    bool     window_open;        /* a controller can pair now */
    uint32_t window_left_s;
    char     qr[48];             /* "MT:..." */
    char     manual[24];         /* 11 digits */
    uint16_t endpoints;          /* bridged thermostats */
    uint8_t  fabric_count;
    ui_matter_fabric_t fabrics[UI_MATTER_FABRICS];
} ui_matter_info_t;

void ui_platform_matter_info(ui_matter_info_t *out);
/* Let another controller pair, for 15 minutes. False if it could not. */
bool ui_platform_matter_open_window(void);
/* Forget one controller (by fabric index). */
bool ui_platform_matter_remove_fabric(uint8_t index);

#ifdef __cplusplus
}
#endif
