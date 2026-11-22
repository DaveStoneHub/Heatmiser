/* System: Wi-Fi, clock and date, memory, display and firmware, with the
 * firmware update (check for a new release, install it). */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "ui_internal.h"

enum { I_WIFI, I_CLOCK, I_DATE, I_UPTIME, I_MEM, I_DISPLAY, I_FW, I_COUNT };
static const char *const s_names[I_COUNT] = { "Wi-Fi", "Clock", "Date", "Running for", "Memory", "Display",
                                              "Firmware" };

static lv_obj_t *s_scr, *s_val[I_COUNT], *s_check, *s_install;

static void fw_text(char *buf, size_t cap, const ui_ota_info_t *o)
{
    const char *trial = o->on_trial ? " (new, on trial)" : "";
    switch (o->state) {
    case UI_OTA_CHECKING:
        snprintf(buf, cap, "%s%s, checking for updates" ELLIPSIS, o->running, trial);
        break;
    case UI_OTA_UP_TO_DATE:
        snprintf(buf, cap, "%s%s, up to date", o->running, trial);
        break;
    case UI_OTA_AVAILABLE:
        snprintf(buf, cap, "%s%s, %s available", o->running, trial, o->latest);
        break;
    case UI_OTA_DOWNLOADING:
        if (o->percent >= 0) {
            snprintf(buf, cap, "Updating, %d%% downloaded", o->percent);
        } else {
            snprintf(buf, cap, "Updating, downloading" ELLIPSIS);
        }
        break;
    case UI_OTA_RESTARTING:
        snprintf(buf, cap, "Updated to %s, restarting" ELLIPSIS, o->latest);
        break;
    case UI_OTA_FAILED:
        snprintf(buf, cap, "%s%s. %s", o->running, trial, o->error);
        break;
    default:
        snprintf(buf, cap, "%s%s%s%s", o->running, trial, o->error[0] ? ". " : "", o->error);
        break;
    }
}

static void check_event(lv_event_t *e)
{
    (void)e;
    if (!ui_platform_ota_check()) {
        ui_toast("An update is already running");
    }
}

static void do_install(void *arg)
{
    (void)arg;
    if (!ui_platform_ota_install()) {
        ui_toast("Check for the update first");
    }
}

static void install_event(lv_event_t *e)
{
    (void)e;
    ui_ota_info_t o;
    ui_platform_ota_info(&o);
    static char msg[320];
    snprintf(msg, sizeof msg,
             "The bridge downloads %s from GitHub and restarts, which takes a few minutes. The thermostats "
             "keep running on their own meanwhile. If the new firmware does not work, the bridge goes back to %s.",
             o.latest, o.running);
    ui_confirm("Install the update?", msg, "Install", do_install, NULL);
}

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
    ui_ota_info_t o;
    ui_platform_ota_info(&o);
    fw_text(buf, sizeof buf, &o);
    lv_label_set_text(s_val[I_FW], buf);
    lv_obj_set_style_text_color(s_val[I_FW], o.state == UI_OTA_FAILED ? C_FAULT
                                : o.state == UI_OTA_AVAILABLE ? C_COPPER : C_TEXT, 0);
    const bool busy = o.state == UI_OTA_CHECKING || o.state == UI_OTA_DOWNLOADING || o.state == UI_OTA_RESTARTING;
    ui_enable(s_check, !busy);
    ui_enable(s_install, o.state == UI_OTA_AVAILABLE);
    if (o.state == UI_OTA_AVAILABLE) {
        lv_label_set_text_fmt(ui_btn_label(s_install), HM_ICON_CHECK "  Install %s", o.latest);
    } else {
        lv_label_set_text(ui_btn_label(s_install), HM_ICON_CHECK "  Install update");
    }
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
        lv_obj_set_height(r, 42);
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
    lv_obj_t *row = ui_row(body, GAP);
    s_check = ui_btn(row, HM_ICON_REFRESH "  Check for update", check_event, NULL);
    lv_obj_set_flex_grow(s_check, 1);
    s_install = ui_btn(row, "", install_event, NULL);
    lv_obj_set_flex_grow(s_install, 1);
    ui_btn_kind(s_install, UI_BTN_SOLID);
    return s_scr;
}

const ui_screen_ops_t ui_system_ops = { system_create, system_refresh, system_tick };
