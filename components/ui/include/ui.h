/*
 * The touchscreen: dashboard of zone tiles, zone detail, and the setup
 * screen (scan, rename, remove, system info). Pure LVGL 9 on top of
 * zone_store.h, so the same code runs on the board and in the desktop
 * preview (tools/ui_preview).
 *
 * Call everything here with the LVGL lock held (lvgl_port_lock on the board).
 */
#pragma once
#include <stdbool.h>
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
typedef enum { UI_SCREEN_DASH = 0, UI_SCREEN_ZONE, UI_SCREEN_SETUP } ui_screen_t;
ui_screen_t ui_screen(void);
void ui_show_dash(void);
void ui_show_zone(uint8_t addr);
void ui_show_setup(void);

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
/* Screen timeout chosen on the setup screen: save it. */
void ui_platform_save_blank_timeout(uint32_t seconds);

#ifdef __cplusplus
}
#endif
