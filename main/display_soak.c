/*
 * Display soak (milestone 3 gate: no drift in 24 h with Wi-Fi on). While the
 * drift test pattern shows, this keeps the things that disturb an RGB panel
 * busy: Wi-Fi traffic (a ping to the gateway every second), flash writes (an
 * NVS commit every 30 s) and LVGL drawing (the pattern's moving bar), with
 * the bus poller running as usual. A DSOAK line goes to the log every 10
 * minutes so a captured log shows the whole run.
 */
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "display.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lwip/inet.h"
#include "net.h"
#include "nvs.h"
#include "ping/ping_sock.h"

static const char *TAG = "dsoak";

#define NVS_EVERY_S  30
#define LOG_EVERY_S  600

static esp_timer_handle_t s_timer;
static esp_ping_handle_t s_ping;
static bool s_on;
static int64_t s_start_us;
static uint32_t s_ticks, s_nvs_writes, s_nvs_fail, s_drift_marks;
static volatile uint32_t s_ping_ok, s_ping_lost;
static uint32_t s_frames0;

static void ping_ok(esp_ping_handle_t h, void *arg)
{
    (void)h; (void)arg;
    s_ping_ok++;
}

static void ping_lost(esp_ping_handle_t h, void *arg)
{
    (void)h; (void)arg;
    s_ping_lost++;
}

static void ping_start(void)
{
    if (s_ping) {
        return;
    }
    net_status_t ns;
    net_status(&ns);
    if (!ns.up || !ns.gw[0]) {
        return; /* try again on the next tick */
    }
    esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
    ip_addr_t gw;
    if (!ipaddr_aton(ns.gw, &gw)) {
        return;
    }
    cfg.target_addr = gw;
    cfg.count = ESP_PING_COUNT_INFINITE;
    cfg.interval_ms = 1000;
    cfg.task_stack_size = 3072;
    const esp_ping_callbacks_t cbs = { .on_ping_success = ping_ok, .on_ping_timeout = ping_lost };
    if (esp_ping_new_session(&cfg, &cbs, &s_ping) == ESP_OK) {
        esp_ping_start(s_ping);
        ESP_LOGI(TAG, "pinging the gateway %s every second", ns.gw);
    }
}

static void ping_stop(void)
{
    if (s_ping) {
        esp_ping_stop(s_ping);
        esp_ping_delete_session(s_ping);
        s_ping = NULL;
    }
}

static void nvs_poke(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("dsoak", NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_u32(h, "n", ++s_nvs_writes);
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    if (err != ESP_OK) {
        s_nvs_fail++;
    }
}

void display_soak_log(const char *why)
{
    net_status_t ns;
    net_status(&ns);
    uint32_t t = s_on ? (uint32_t)((esp_timer_get_time() - s_start_us) / 1000000) : 0;
    printf("DSOAK %s t=%" PRIu32 "s wifi=%s rssi=%d disc=%u ping_ok=%" PRIu32 " ping_lost=%" PRIu32
           " nvs=%" PRIu32 "/%" PRIu32 "fail frames=%" PRIu32 " int_free=%u int_min=%u psram_free=%u"
           " drift_marks=%" PRIu32 "\n",
           why, t, ns.up ? "up" : "down", ns.rssi, ns.disconnects, s_ping_ok, s_ping_lost, s_nvs_writes,
           s_nvs_fail, board_flush_count() - s_frames0,
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM), s_drift_marks);
}

static void tick(void *arg)
{
    (void)arg;
    s_ticks++;
    ping_start();
    if (s_ticks % NVS_EVERY_S == 0) {
        nvs_poke();
    }
    if (s_ticks % LOG_EVERY_S == 0) {
        display_soak_log("status");
    }
}

void display_soak(bool on)
{
    if (on == s_on) {
        return;
    }
    if (!s_timer) {
        const esp_timer_create_args_t a = { .callback = tick, .name = "dsoak" };
        ESP_ERROR_CHECK(esp_timer_create(&a, &s_timer));
    }
    if (on) {
        s_on = true;
        s_start_us = esp_timer_get_time();
        s_ticks = s_nvs_writes = s_nvs_fail = s_drift_marks = 0;
        s_ping_ok = s_ping_lost = 0;
        s_frames0 = board_flush_count();
        ping_start();
        esp_timer_start_periodic(s_timer, 1000000);
        display_soak_log("start");
    } else {
        esp_timer_stop(s_timer);
        ping_stop();
        display_soak_log("end");
        s_on = false;
    }
}

bool display_soak_running(void)
{
    return s_on;
}

void display_soak_drift_seen(void)
{
    s_drift_marks++;
    display_soak_log("DRIFT-REPORTED");
    board_lcd_restart();
}
