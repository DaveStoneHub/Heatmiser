/* The UI's hooks into the board, Wi-Fi and storage. */
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "display.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "net.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "ui.h"

void ui_platform_sys_info(ui_sys_info_t *o)
{
    memset(o, 0, sizeof *o);
    net_status_t ns;
    net_status(&ns);
    o->wifi_configured = ns.configured;
    o->wifi_up = ns.up;
    snprintf(o->ssid, sizeof o->ssid, "%s", ns.ssid);
    snprintf(o->ip, sizeof o->ip, "%s", ns.ip);
    o->rssi = ns.rssi;
    o->time_set = net_time_set();
    o->uptime_s = (uint32_t)(esp_timer_get_time() / 1000000);
    o->heap_int_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    o->heap_int_min = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    o->heap_psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    o->frames = board_flush_count();
    o->version = esp_app_get_description()->version;
}

void ui_platform_backlight(bool on)
{
    board_backlight(on);
}

void ui_platform_soak(bool on)
{
    display_soak(on);
}

void ui_platform_drift_seen(void)
{
    display_soak_drift_seen();
}

void ui_platform_save_blank_timeout(uint32_t s)
{
    nvs_handle_t h;
    if (nvs_open("ui", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u32(h, "blank", s);
        nvs_commit(h);
        nvs_close(h);
    }
}

uint32_t display_saved_blank_timeout(void)
{
    uint32_t s = 600;
    nvs_handle_t h;
    if (nvs_open("ui", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u32(h, "blank", &s);
        nvs_close(h);
    }
    return s;
}

const char *ui_platform_tz(void)
{
    return net_tz();
}

void ui_platform_set_tz(const char *posix)
{
    esp_err_t e = net_set_tz(posix);
    if (e != ESP_OK) {
        ESP_LOGW("ui", "timezone not saved: %s", esp_err_to_name(e));
    }
}

void ui_platform_factory_reset(void)
{
    ESP_LOGW("ui", "factory reset from the screen");
    board_backlight(false);
    nvs_flash_deinit();
    nvs_flash_erase();
    esp_restart();
}
