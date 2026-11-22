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
#include "matter_bridge.h"
#include "net.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_random.h"
#include "ota.h"
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

void ui_platform_matter_info(ui_matter_info_t *o)
{
    matter_status_t st;
    matter_bridge_status(&st);
    memset(o, 0, sizeof *o);
    o->running = st.running;
    o->window_open = st.window_open;
    o->window_left_s = st.window_left_s;
    snprintf(o->qr, sizeof o->qr, "%s", st.qr);
    snprintf(o->manual, sizeof o->manual, "%s", st.manual);
    o->endpoints = st.endpoints;
    o->hw_endpoints = st.hw_endpoints;
    o->fabric_count = st.fabric_count < UI_MATTER_FABRICS ? st.fabric_count : UI_MATTER_FABRICS;
    for (int i = 0; i < o->fabric_count; i++) {
        o->fabrics[i].index = st.fabrics[i].index;
        o->fabrics[i].vendor = st.fabrics[i].vendor;
        snprintf(o->fabrics[i].label, sizeof o->fabrics[i].label, "%s", st.fabrics[i].label);
    }
}

bool ui_platform_matter_open_window(void)
{
    return matter_bridge_open_window(15 * 60) == ESP_OK;
}

bool ui_platform_matter_remove_fabric(uint8_t index)
{
    return matter_bridge_remove_fabric(index) == ESP_OK;
}

/* ---- the settings PIN ----------------------------------------------------------
 * Saved as "ui"/"pin": an 8-byte random salt and a 64-bit hash of salt and
 * PIN (FNV-1a, run 4096 times). Four digits are only 10,000 guesses, so no
 * hash can protect them from someone with the flash; this keeps the PIN out
 * of plain sight, and the keypad's lockout slows guessing at the screen. */
#define PIN_SALT 8

static uint64_t pin_hash(const uint8_t salt[PIN_SALT], const char *pin)
{
    uint64_t h = 1469598103934665603ull;
    for (int r = 0; r < 4096; r++) {
        for (int i = 0; i < PIN_SALT; i++) {
            h = (h ^ salt[i]) * 1099511628211ull;
        }
        for (const char *p = pin; *p; p++) {
            h = (h ^ (uint8_t)*p) * 1099511628211ull;
        }
    }
    return h;
}

static bool pin_load(uint8_t blob[PIN_SALT + 8])
{
    nvs_handle_t h;
    if (nvs_open("ui", NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    size_t len = PIN_SALT + 8;
    const bool ok = nvs_get_blob(h, "pin", blob, &len) == ESP_OK && len == PIN_SALT + 8;
    nvs_close(h);
    return ok;
}

bool ui_platform_pin_enabled(void)
{
    uint8_t b[PIN_SALT + 8];
    return pin_load(b);
}

bool ui_platform_pin_check(const char *pin)
{
    uint8_t b[PIN_SALT + 8];
    if (!pin_load(b)) {
        return true;
    }
    const uint64_t want = pin_hash(b, pin);
    return memcmp(&want, b + PIN_SALT, 8) == 0;
}

bool ui_platform_pin_save(const char *pin)
{
    nvs_handle_t h;
    if (nvs_open("ui", NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    esp_err_t err;
    if (pin && pin[0]) {
        uint8_t b[PIN_SALT + 8];
        esp_fill_random(b, PIN_SALT);
        const uint64_t hv = pin_hash(b, pin);
        memcpy(b + PIN_SALT, &hv, 8);
        err = nvs_set_blob(h, "pin", b, sizeof b);
    } else {
        err = nvs_erase_key(h, "pin");
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            err = ESP_OK;
        }
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    ESP_LOGI("ui", "settings PIN %s", pin && pin[0] ? "set" : "turned off");
    return err == ESP_OK;
}

/* ---- firmware updates ------------------------------------------------------------ */

void ui_platform_ota_info(ui_ota_info_t *o)
{
    ota_status_t st;
    ota_status(&st);
    memset(o, 0, sizeof *o);
    o->state = (ui_ota_state_t)st.state;
    snprintf(o->running, sizeof o->running, "%s", st.running);
    snprintf(o->latest, sizeof o->latest, "%s", st.latest);
    o->percent = st.percent;
    snprintf(o->error, sizeof o->error, "%s", st.error);
    o->on_trial = st.on_trial;
}

bool ui_platform_ota_check(void)
{
    return ota_check() == ESP_OK;
}

bool ui_platform_ota_install(void)
{
    return ota_install(NULL) == ESP_OK;
}
