/* Settings: six setting tiles (timezone, units, poll interval, master
 * address, display timeout, PIN), the other screens, and actions for every
 * zone (sync clocks, holiday, factory reset). */
#include <stdio.h>
#include <string.h>

#include "ui_internal.h"

/* The common zones for a UK install first, then the rest of Europe and a
 * few others for anyone abroad. */
const ui_tz_t ui_timezones[] = {
    { "Europe/London", "GMT0BST,M3.5.0/1,M10.5.0" },
    { "Europe/Dublin", "IST-1GMT0,M10.5.0,M3.5.0/1" },
    { "Europe/Lisbon", "WET0WEST,M3.5.0/1,M10.5.0" },
    { "Europe/Paris", "CET-1CEST,M3.5.0,M10.5.0/3" },
    { "Europe/Berlin", "CET-1CEST,M3.5.0,M10.5.0/3" },
    { "Europe/Madrid", "CET-1CEST,M3.5.0,M10.5.0/3" },
    { "Europe/Athens", "EET-2EEST,M3.5.0/3,M10.5.0/4" },
    { "UTC", "UTC0" },
    { "America/New_York", "EST5EDT,M3.2.0,M11.1.0" },
    { "Australia/Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3" },
};
const size_t ui_timezone_count = sizeof ui_timezones / sizeof ui_timezones[0];

enum { T_TZ, T_UNITS, T_POLL, T_MASTER, T_BLANK, T_PIN, T_COUNT };

static lv_obj_t *s_scr, *s_wifi, *s_val[T_COUNT];

static const uint32_t s_polls[] = { 2000, 5000, 10000, 30000, 60000, 120000 };
static const char *const s_poll_names[] = { "2 s", "5 s", "10 s", "30 s", "1 min", "2 min" };
#define N_POLLS (sizeof s_polls / sizeof s_polls[0])
static const uint32_t s_blanks[] = { 0, 60, 300, 600, 1800 };
static const char *const s_blank_names[] = { "Never", "1 min", "5 min", "10 min", "30 min" };
#define N_BLANKS (sizeof s_blanks / sizeof s_blanks[0])
static const unsigned s_hols[] = { 0, 24, 72, 168, 336 };
static const char *const s_hol_names[] = { "End holiday", "1 day", "3 days", "1 week", "2 weeks" };
#define N_HOLS (sizeof s_hols / sizeof s_hols[0])

static int tz_index(void)
{
    const char *cur = ui_platform_tz();
    for (size_t i = 0; cur && i < ui_timezone_count; i++) {
        if (!strcmp(cur, ui_timezones[i].posix)) {
            return (int)i;
        }
    }
    return -1;
}

static void show_values(void)
{
    int tz = tz_index();
    lv_label_set_text(s_val[T_TZ], tz >= 0 ? ui_timezones[tz].name : "Custom");
    lv_label_set_text(s_val[T_UNITS], DEG "C");
    zone_bus_info_t bi;
    zone_store_bus_info(&bi);
    if (bi.poll_ms % 60000 == 0) {
        lv_label_set_text_fmt(s_val[T_POLL], "%u min", (unsigned)(bi.poll_ms / 60000));
    } else {
        lv_label_set_text_fmt(s_val[T_POLL], "%u s", (unsigned)(bi.poll_ms / 1000));
    }
    lv_label_set_text_fmt(s_val[T_MASTER], "0x%02X", bi.master);
    uint32_t b = ui_blank_timeout();
    if (!b) {
        lv_label_set_text(s_val[T_BLANK], "Never");
    } else if (b % 60 == 0) {
        lv_label_set_text_fmt(s_val[T_BLANK], "%u min", (unsigned)(b / 60));
    } else {
        lv_label_set_text_fmt(s_val[T_BLANK], "%u s", (unsigned)b);
    }
    lv_label_set_text(s_val[T_PIN], ui_platform_pin_enabled() ? "On" : "Off");
}

static void show_wifi(void)
{
    ui_sys_info_t si;
    ui_platform_sys_info(&si);
    if (!si.wifi_configured) {
        lv_label_set_text(s_wifi, HM_ICON_WIFI_OFF "  Wi-Fi not set up");
    } else if (si.wifi_up) {
        lv_label_set_text_fmt(s_wifi, HM_ICON_WIFI "  Wi-Fi connected  %s", si.ip);
    } else {
        lv_label_set_text_fmt(s_wifi, HM_ICON_WIFI_OFF "  Connecting to %s" ELLIPSIS, si.ssid);
    }
    lv_obj_set_style_text_color(s_wifi, si.wifi_up ? C_MUTED : C_FAULT, 0);
}

/* ---- the six tiles ------------------------------------------------------------------- */

static void pick_tz(int i)
{
    ui_platform_set_tz(ui_timezones[i].posix);
    show_values();
}

static void pick_poll(int i)
{
    ui_report(zone_store_set_poll_interval(s_polls[i]), "Poll interval");
    show_values();
}

static void pick_blank(int i)
{
    ui_set_blank_timeout(s_blanks[i]);
    ui_platform_save_blank_timeout(s_blanks[i]);
    show_values();
}

/* Eight of the 32 master addresses (0x81-0xA0): enough to step aside from
 * another master's address. The console's 'master' command reaches the rest
 * for a test, without saving. */
static const char *const s_master_names[] = { "0x81", "0x82", "0x83", "0x84", "0x85", "0x86", "0x87", "0x88" };
#define N_MASTERS (sizeof s_master_names / sizeof s_master_names[0])

static void pick_master(int i)
{
    ui_report(zone_store_set_master((uint8_t)(ZONE_MASTER_MIN + i)), "Master address");
    show_values();
}

static void tile_event(lv_event_t *e)
{
    static const char *tz_names[16];
    switch ((int)(intptr_t)lv_event_get_user_data(e)) {
    case T_TZ:
        for (size_t i = 0; i < ui_timezone_count && i < 16; i++) {
            tz_names[i] = ui_timezones[i].name;
        }
        ui_choose("Timezone", tz_names, (int)(ui_timezone_count < 16 ? ui_timezone_count : 16), tz_index(),
                  pick_tz);
        break;
    case T_UNITS:
        ui_toast("Heatmiser stats work in whole degrees Celsius");
        break;
    case T_POLL: {
        zone_bus_info_t bi;
        zone_store_bus_info(&bi);
        int sel = -1;
        for (size_t i = 0; i < N_POLLS; i++) {
            sel = s_polls[i] == bi.poll_ms ? (int)i : sel;
        }
        ui_choose("Poll each stat every", s_poll_names, N_POLLS, sel, pick_poll);
        break;
    }
    case T_MASTER: {
        zone_bus_info_t bi;
        zone_store_bus_info(&bi);
        ui_choose("Bridge address on the bus", s_master_names, N_MASTERS, (int)bi.master - ZONE_MASTER_MIN,
                  pick_master);
        break;
    }
    case T_BLANK: {
        int sel = -1;
        for (size_t i = 0; i < N_BLANKS; i++) {
            sel = s_blanks[i] == ui_blank_timeout() ? (int)i : sel;
        }
        ui_choose("Turn the screen off after", s_blank_names, N_BLANKS, sel, pick_blank);
        break;
    }
    case T_PIN:
        ui_pin_setup(show_values);
        break;
    }
}

static void tile(lv_obj_t *grid, int id, const char *icon, const char *name)
{
    lv_obj_t *t = ui_panel(grid);
    lv_obj_set_size(t, (SCR_W - 2 * PAD - 2 * GAP) / 3, 76);
    lv_obj_set_style_pad_hor(t, 14, 0);
    lv_obj_set_style_pad_ver(t, 0, 0);
    lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(t, C_PRESSED, LV_STATE_PRESSED);
    lv_obj_add_event_cb(t, tile_event, LV_EVENT_CLICKED, (void *)(intptr_t)id);
    lv_obj_t *cap = ui_label(t, F_CAP, C_MUTED, "");
    lv_label_set_text_fmt(cap, "%s  %s", icon, name);
    lv_obj_align(cap, LV_ALIGN_TOP_LEFT, 0, 12);
    s_val[id] = ui_label(t, &hm_bc600_30, C_TEXT, "");
    lv_obj_align(s_val[id], LV_ALIGN_BOTTOM_LEFT, 0, -8);
    lv_obj_t *chev = ui_label(t, &hm_icon_22, C_MUTED, HM_ICON_NEXT);
    lv_obj_align(chev, LV_ALIGN_RIGHT_MID, 0, 0);
}

/* ---- screens and actions ---------------------------------------------------------------- */

static void screen_event(lv_event_t *e)
{
    switch ((int)(intptr_t)lv_event_get_user_data(e)) {
    case 0:
        ui_show_zones();
        break;
    case 1:
        ui_show_matter();
        break;
    case 2:
        ui_show_diag();
        break;
    default:
        ui_show_system();
        break;
    }
}

static void sync_event(lv_event_t *e)
{
    (void)e;
    int n = zone_store_sync_clocks();
    char msg[80];
    if (n < 0) {
        snprintf(msg, sizeof msg, "The bridge's clock isn't set yet. Check the Wi-Fi.");
    } else if (n == 0) {
        snprintf(msg, sizeof msg, "No stats are online to set");
    } else {
        snprintf(msg, sizeof msg, "Setting the clock on %d stat%s", n, n == 1 ? "" : "s");
    }
    ui_toast(msg);
}

static void pick_holiday(int i)
{
    int n = zone_store_holiday_all(s_hols[i]);
    char msg[80];
    if (n <= 0) {
        snprintf(msg, sizeof msg, "No stats are online");
    } else if (!s_hols[i]) {
        snprintf(msg, sizeof msg, "Ending holiday on %d stat%s", n, n == 1 ? "" : "s");
    } else {
        snprintf(msg, sizeof msg, "Holiday for %s on %d stat%s", s_hol_names[i], n, n == 1 ? "" : "s");
    }
    ui_toast(msg);
}

static void holiday_event(lv_event_t *e)
{
    (void)e;
    ui_choose("Holiday for every zone", s_hol_names, N_HOLS, -1, pick_holiday);
}

static void do_reset(void *arg)
{
    (void)arg;
    ui_platform_factory_reset();
}

static void reset_event(lv_event_t *e)
{
    (void)e;
    ui_confirm("Factory reset?",
               "The bridge forgets its zones, names, Wi-Fi, Matter pairings and settings, then restarts. "
               "The thermostats keep their own settings and schedules.",
               "Reset", do_reset, NULL);
}

static void back_event(lv_event_t *e)
{
    (void)e;
    ui_show_dash();
}

static lv_obj_t *section(lv_obj_t *body, const char *cap)
{
    ui_caption(body, cap);
    lv_obj_t *row = ui_row(body, GAP);
    return row;
}

static lv_obj_t *wide_btn(lv_obj_t *row, const char *text, lv_event_cb_t cb, void *user)
{
    lv_obj_t *b = ui_btn(row, text, cb, user);
    lv_obj_set_flex_grow(b, 1);
    return b;
}

static void settings_refresh(uint32_t dirty)
{
    (void)dirty;
}

static void settings_tick(void)
{
    show_values();
    show_wifi();
}

static lv_obj_t *settings_create(void)
{
    s_scr = ui_screen_new();
    ui_hdr_t h = ui_header(s_scr, "Settings", back_event);
    s_wifi = ui_label(h.right, F_LABEL, C_MUTED, "");

    lv_obj_t *body = ui_body(s_scr);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(body, GAP, 0);

    lv_obj_t *grid = ui_box(body);
    lv_obj_set_size(grid, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(grid, GAP, 0);
    lv_obj_set_style_pad_column(grid, GAP, 0);
    tile(grid, T_TZ, HM_ICON_CLOCK, "Timezone");
    tile(grid, T_UNITS, HM_ICON_THERMO, "Units");
    tile(grid, T_POLL, HM_ICON_REFRESH, "Poll interval");
    tile(grid, T_MASTER, HM_ICON_RADIO, "Master address");
    tile(grid, T_BLANK, HM_ICON_MONITOR, "Display timeout");
    tile(grid, T_PIN, HM_ICON_LOCK, "PIN");

    lv_obj_t *r1 = section(body, "SCREENS");
    wide_btn(r1, HM_ICON_LIST "  Zones and scan", screen_event, (void *)0);
    wide_btn(r1, HM_ICON_SHARE "  Matter", screen_event, (void *)1);
    wide_btn(r1, HM_ICON_ACTIVITY "  Bus diagnostics", screen_event, (void *)2);
    wide_btn(r1, HM_ICON_INFO "  System", screen_event, (void *)3);

    lv_obj_t *r2 = section(body, "ACTIONS");
    wide_btn(r2, HM_ICON_CLOCK "  Sync clocks now", sync_event, NULL);
    wide_btn(r2, HM_ICON_PALM "  Holiday, all zones", holiday_event, NULL);
    ui_btn_kind(wide_btn(r2, HM_ICON_RESET "  Factory reset", reset_event, NULL), UI_BTN_DANGER);
    return s_scr;
}

const ui_screen_ops_t ui_settings_ops = { settings_create, settings_refresh, settings_tick };
