/* Render each screen of the UI to a PNG, driving it with simulated touches.
 * Usage: ui_preview <out-dir> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fake.h"
#include "lvgl.h"
#include "ui.h"

#define W 800
#define H 480
#define SCHED_FIELD_X 105
#define SCHED_FIELD_Y 193

static uint16_t s_fb[W * H];
static uint32_t s_ms;
static struct { int x, y; bool down; } s_touch;
static const char *s_out = ".";
static bool s_soak;

/* ---- platform hooks ---- */

void ui_platform_sys_info(ui_sys_info_t *o)
{
    memset(o, 0, sizeof *o);
    o->wifi_configured = true;
    o->wifi_up = true;
    snprintf(o->ssid, sizeof o->ssid, "HomeNet");
    snprintf(o->ip, sizeof o->ip, "192.168.1.40");
    o->rssi = -58;
    o->time_set = true;
    o->uptime_s = 3 * 3600 + 25 * 60 + s_ms / 1000;
    o->heap_int_free = 96 * 1024;
    o->heap_int_min = 71 * 1024;
    o->heap_psram_free = 5900 * 1024;
    o->frames = 41230;
    o->version = "m3-preview";
}
void ui_platform_backlight(bool on) { (void)on; }
void ui_platform_soak(bool on) { s_soak = on; }
void ui_platform_drift_seen(void) {}
void ui_platform_save_blank_timeout(uint32_t s) { (void)s; }
static const char *s_tz = "GMT0BST,M3.5.0/1,M10.5.0";
const char *ui_platform_tz(void) { return s_tz; }
void ui_platform_set_tz(const char *posix) { s_tz = posix; }
void ui_platform_factory_reset(void) { printf("factory reset\n"); }

/* ---- LVGL glue ---- */

static uint32_t tick_cb(void) { return s_ms; }

static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
    (void)a; (void)px;
    lv_display_flush_ready(d);
}

static void touch_cb(lv_indev_t *in, lv_indev_data_t *data)
{
    (void)in;
    data->point.x = s_touch.x;
    data->point.y = s_touch.y;
    data->state = s_touch.down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void run(uint32_t ms)
{
    for (uint32_t t = 0; t < ms; t += 5) {
        s_ms += 5;
        lv_timer_handler();
    }
}

static void tap(int x, int y)
{
    s_touch.x = x; s_touch.y = y; s_touch.down = true;
    run(80);
    s_touch.down = false;
    run(120);
}

static void long_press(int x, int y)
{
    s_touch.x = x; s_touch.y = y; s_touch.down = true;
    run(700);
    s_touch.down = false;
    run(120);
}

static void shot(const char *name)
{
    run(300);
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(NULL);
    uint8_t *rgb = malloc(W * H * 3);
    for (int i = 0; i < W * H; i++) {
        uint16_t c = s_fb[i];
        rgb[i * 3] = (uint8_t)(((c >> 11) & 0x1F) * 255 / 31);
        rgb[i * 3 + 1] = (uint8_t)(((c >> 5) & 0x3F) * 255 / 63);
        rgb[i * 3 + 2] = (uint8_t)((c & 0x1F) * 255 / 31);
    }
    char path[512];
    snprintf(path, sizeof path, "%s/%s.png", s_out, name);
    if (png_write(path, rgb, W, H) == 0) {
        printf("wrote %s\n", path);
    }
    free(rgb);
}

int main(int argc, char **argv)
{
    if (argc > 1) {
        s_out = argv[1];
    }
    setenv("TZ", "GMT0BST,M3.5.0/1,M10.5.0", 1);
    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_display_t *d = lv_display_create(W, H);
    lv_display_set_color_format(d, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(d, s_fb, NULL, sizeof s_fb, LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(d, flush_cb);
    lv_indev_t *in = lv_indev_create();
    lv_indev_set_type(in, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(in, touch_cb);

    fake_clear();
    ui_init();
    shot("01-dashboard-empty");

    fake_load_house();
    run(400);
    shot("02-dashboard");

    long_press(110, 120);          /* Living room tile */
    tap(600, 234);                 /* + */
    tap(600, 234);
    shot("03-quick-adjust");
    run(6000);                     /* popup times out, change sent */


    ui_show_zone(1);
    shot("04-zone-heating");
    tap(196, 404);                 /* + under the dial */
    shot("05-zone-setting");
    run(1000);

    ui_show_zone(7);
    shot("06-zone-frost");
    ui_show_zone(2);
    shot("07-zone-offline");
    ui_show_zone(10);
    shot("08-zone-floor-hold");
    ui_show_zone(12);
    shot("09-zone-fault");
    ui_show_zone(6);
    shot("10-zone-hold");

    ui_show_dash();
    run(200);
    fake_notice("Kitchen: setpoint 21\xC2\xB0 not taken (no reply)");
    run(400);
    shot("11-dashboard-toast");
    run(5000);

    ui_show_hotwater(3);
    shot("12-hot-water");
    fake_set_boost(3, 42);
    run(400);
    shot("13-hot-water-boost");
    fake_set_boost(3, 0);

    ui_show_schedule(1, false);
    shot("14-schedule");
    tap(SCHED_FIELD_X, SCHED_FIELD_Y);   /* level 1 time */
    shot("15-schedule-edit");
    ui_show_schedule(3, true);
    shot("16-hot-water-timer");

    ui_show_rename(5);
    shot("17-rename");

    ui_show_settings();
    shot("18-settings");
    tap(140, 110);                 /* Timezone */
    shot("19-settings-timezone");
    ui_show_settings();

    ui_show_zones();
    fake_set_scan(true, 1, 5, 4);
    run(1200);
    shot("20-zones-scanning");
    fake_set_scan(false, 0, 0, 12);
    run(1200);
    shot("21-zones");

    ui_show_diag();
    run(1200);
    shot("22-bus-diagnostics");

    ui_show_system();
    run(1200);
    shot("23-system");

    ui_set_soak(true);
    ui_show_dash();
    run(1500);
    shot("24-soak-pattern");
    ui_set_soak(false);
    return 0;
}
