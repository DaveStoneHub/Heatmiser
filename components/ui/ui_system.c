/* System: Wi-Fi, clock, memory, display and firmware, and the drift test
 * pattern for the display soak. */
#include <stdio.h>
#include <string.h>

#include "ui_internal.h"

enum { I_WIFI, I_CLOCK, I_UPTIME, I_MEM, I_DISPLAY, I_FW, I_COUNT };
static const char *const s_names[I_COUNT] = { "Wi-Fi", "Clock", "Running for", "Memory", "Display", "Firmware" };

static lv_obj_t *s_scr, *s_val[I_COUNT], *s_soak_btn;

static void back_event(lv_event_t *e)
{
    (void)e;
    ui_show_settings();
}

static void soak_event(lv_event_t *e)
{
    (void)e;
    ui_set_soak(!ui_soak_on());
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
    lv_label_set_text_fmt(s_val[I_UPTIME], "%u d %u h %02u min", (unsigned)(si.uptime_s / 86400),
                          (unsigned)(si.uptime_s / 3600 % 24), (unsigned)(si.uptime_s / 60 % 60));
    lv_label_set_text_fmt(s_val[I_MEM], "%u KB internal free (lowest %u KB), %u KB PSRAM",
                          (unsigned)(si.heap_int_free / 1024), (unsigned)(si.heap_int_min / 1024),
                          (unsigned)(si.heap_psram_free / 1024));
    lv_label_set_text_fmt(s_val[I_DISPLAY], "%u frames drawn", (unsigned)si.frames);
    lv_label_set_text(s_val[I_FW], si.version ? si.version : "?");
    ui_btn_kind(s_soak_btn, ui_soak_on() ? UI_BTN_TINT : UI_BTN_PLAIN);
    lv_label_set_text(ui_btn_label(s_soak_btn), ui_soak_on() ? HM_ICON_MONITOR "  Drift test pattern on"
                                                             : HM_ICON_MONITOR "  Drift test pattern");
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
        lv_label_set_long_mode(s_val[i], LV_LABEL_LONG_DOT);
    }

    lv_obj_t *row = ui_row(body, GAP);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    s_soak_btn = ui_btn(row, "", soak_event, NULL);
    lv_obj_set_width(s_soak_btn, 300);
    lv_obj_t *note = ui_label(row, F_BODY, C_MUTED, "A frame and corner marks at the panel edges, for the 24 h display soak.");
    lv_obj_set_flex_grow(note, 1);
    lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
    return s_scr;
}

const ui_screen_ops_t ui_system_ops = { system_create, system_refresh, system_tick };
