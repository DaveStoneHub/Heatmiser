/* System: Wi-Fi, clock and date, memory, display and firmware. */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "ui_internal.h"

enum { I_WIFI, I_CLOCK, I_DATE, I_UPTIME, I_MEM, I_DISPLAY, I_FW, I_COUNT };
static const char *const s_names[I_COUNT] = { "Wi-Fi", "Clock", "Date", "Running for", "Memory", "Display",
                                              "Firmware" };

static lv_obj_t *s_scr, *s_val[I_COUNT];

static void back_event(lv_event_t *e)
{
    (void)e;
    ui_show_settings();
}

static void system_tick(void)
{
    ui_sys_info_t si;
    ui_platform_sys_info(&si);
    char buf[96], clock[32];
    if (!si.wifi_configured) {
        snprintf(buf, sizeof buf, "Not set up. Use 'wifi join' on the console.");
    } else if (si.wifi_up) {
        snprintf(buf, sizeof buf, "%s, %s, %d dBm", si.ssid, si.ip, si.rssi);
    } else {
        snprintf(buf, sizeof buf, "Connecting to %s" ELLIPSIS, si.ssid);
    }
    lv_label_set_text(s_val[I_WIFI], buf);
    ui_clock_text(clock, sizeof clock, true);
    lv_label_set_text_fmt(s_val[I_CLOCK], "%s%s", clock, si.time_set ? "" : " (not synced yet)");
    if (si.time_set) {
        time_t now = time(NULL);
        struct tm tm;
        localtime_r(&now, &tm);
        char day[16], month[24];
        strftime(day, sizeof day, "%A", &tm);
        strftime(month, sizeof month, "%B %Y", &tm);
        lv_label_set_text_fmt(s_val[I_DATE], "%s %d %s", day, tm.tm_mday, month); /* Sunday 22 November 2026 */
    } else {
        lv_label_set_text(s_val[I_DATE], "--");
    }
    lv_label_set_text_fmt(s_val[I_UPTIME], "%u d %u h %02u min", (unsigned)(si.uptime_s / 86400),
                          (unsigned)(si.uptime_s / 3600 % 24), (unsigned)(si.uptime_s / 60 % 60));
    lv_label_set_text_fmt(s_val[I_MEM], "%u KB internal free (lowest %u KB), %u KB PSRAM",
                          (unsigned)(si.heap_int_free / 1024), (unsigned)(si.heap_int_min / 1024),
                          (unsigned)(si.heap_psram_free / 1024));
    lv_label_set_text_fmt(s_val[I_DISPLAY], "%u frames drawn", (unsigned)si.frames);
    lv_label_set_text(s_val[I_FW], si.version ? si.version : "?");
}

static void system_refresh(uint32_t dirty)
{
    (void)dirty;
}

static lv_obj_t *system_create(void)
{
    s_scr = ui_screen_new();
    ui_header(s_scr, "System", back_event);

    lv_obj_t *body = ui_body(s_scr);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(body, GAP, 0);

    lv_obj_t *p = ui_panel(body);
    lv_obj_set_width(p, LV_PCT(100));
    lv_obj_set_flex_grow(p, 1);
    lv_obj_set_style_pad_hor(p, 16, 0);
    lv_obj_set_style_pad_ver(p, 6, 0);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    for (int i = 0; i < I_COUNT; i++) {
        lv_obj_t *r = ui_row(p, GAP);
        lv_obj_set_height(r, 46);
        if (i < I_COUNT - 1) {
            lv_obj_set_style_border_side(r, LV_BORDER_SIDE_BOTTOM, 0);
            lv_obj_set_style_border_color(r, C_BORDER, 0);
            lv_obj_set_style_border_width(r, 1, 0);
        }
        lv_obj_t *n = ui_label(r, F_CAP, C_MUTED, s_names[i]);
        lv_obj_set_width(n, 160);
        s_val[i] = ui_label(r, F_LABEL, C_TEXT, "");
        lv_obj_set_flex_grow(s_val[i], 1);
        ui_label_dots(s_val[i]);
    }
    return s_scr;
}

const ui_screen_ops_t ui_system_ops = { system_create, system_refresh, system_tick };
