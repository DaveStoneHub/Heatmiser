/* Setup: find, name, identify and remove thermostats; system status, screen
 * timeout and the drift test pattern. */
#include <stdio.h>
#include <string.h>

#include "ui_internal.h"

typedef struct {
    lv_obj_t *row, *name, *sub;
} row_t;

static lv_obj_t *s_scr, *s_list, *s_scan_btn, *s_scan_lbl, *s_scan_bar, *s_none;
static lv_obj_t *s_sys, *s_timeout, *s_soak_sw;
static row_t s_rows[HM_SCHED_MAX_STATS];
static bool s_was_scanning;

static const uint32_t s_timeouts[] = { 0, 60, 300, 600, 1800 };

/* ---- identify: toggle the key lock so the padlock blinks on the stat ------------------- */

static struct {
    uint8_t addr;
    int step;          /* 4 changes, ending where it started */
    bool start;
    lv_timer_t *timer;
} s_ident;

static void ident_tick(lv_timer_t *t)
{
    (void)t;
    zone_view_t v;
    if (!s_ident.addr || !ui_zone(s_ident.addr, &v)) {
        lv_timer_pause(s_ident.timer);
        return;
    }
    if (v.pend.lock) {
        return; /* wait for the last change to be confirmed */
    }
    if (s_ident.step >= 4) {
        lv_timer_pause(s_ident.timer);
        s_ident.addr = 0;
        return;
    }
    bool want = (s_ident.step % 2 == 0) ? !s_ident.start : s_ident.start;
    zone_err_t e = zone_store_set_lock(s_ident.addr, want);
    if (e != ZONE_OK) {
        ui_report(e, "Identify");
        lv_timer_pause(s_ident.timer);
        s_ident.addr = 0;
        return;
    }
    s_ident.step++;
}

static void ident_event(lv_event_t *e)
{
    uint8_t addr = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    zone_view_t v;
    if (!ui_zone(addr, &v) || !v.have || v.link == HM_LINK_OFFLINE) {
        ui_toast("That stat is not answering");
        return;
    }
    if (s_ident.addr) {
        ui_toast("Already identifying a stat");
        return;
    }
    s_ident.addr = addr;
    s_ident.step = 0;
    s_ident.start = v.locked;
    if (!s_ident.timer) {
        s_ident.timer = lv_timer_create(ident_tick, 2500, NULL);
    }
    lv_timer_resume(s_ident.timer);
    ident_tick(s_ident.timer);
    char msg[64];
    snprintf(msg, sizeof msg, "Watch for the padlock blinking on %s", v.cfg.name);
    ui_toast(msg);
}

/* ---- rows ------------------------------------------------------------------------------------- */

static void rename_event(lv_event_t *e)
{
    ui_rename_dialog((uint8_t)(uintptr_t)lv_event_get_user_data(e));
}

static void do_remove(void *arg)
{
    ui_report(zone_store_remove((uint8_t)(uintptr_t)arg), "Remove");
}

static void remove_event(lv_event_t *e)
{
    uint8_t addr = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    zone_view_t v;
    if (!ui_zone(addr, &v)) {
        return;
    }
    char t[48], m[200];
    snprintf(t, sizeof t, "Remove %s?", v.cfg.name);
    snprintf(m, sizeof m,
             "The bridge stops polling stat %u and forgets its name. The thermostat itself "
             "is not changed, and a scan will find it again.", addr);
    ui_confirm(t, m, "Remove", do_remove, (void *)(uintptr_t)addr);
}

static lv_obj_t *small_button(lv_obj_t *p, const char *txt, lv_event_cb_t cb, uint8_t addr)
{
    lv_obj_t *b = ui_button(p, txt, cb, (void *)(uintptr_t)addr);
    lv_obj_set_style_min_width(b, 0, 0);
    lv_obj_set_style_pad_hor(b, 14, 0);
    lv_obj_set_height(b, 56);
    lv_obj_set_style_text_font(b, &lv_font_montserrat_16, 0);
    return b;
}

static void row_build(row_t *r, uint8_t addr)
{
    lv_obj_t *o = lv_obj_create(s_list);
    r->row = o;
    lv_obj_set_size(o, LV_PCT(100), 72);
    lv_obj_set_style_bg_color(o, C_PANEL, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, 12, 0);
    lv_obj_set_style_pad_hor(o, 12, 0);
    lv_obj_set_style_pad_ver(o, 0, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(o, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(o, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(o, 10, 0);

    lv_obj_t *badge = lv_label_create(o);
    lv_label_set_text_fmt(badge, "%u", addr);
    lv_obj_set_size(badge, 44, 44);
    lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(badge, C_PANEL_HI, 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    lv_obj_set_style_text_align(badge, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(badge, 11, 0);

    lv_obj_t *col = lv_obj_create(o);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, 1, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    r->name = lv_label_create(col);
    lv_label_set_long_mode(r->name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(r->name, LV_PCT(100));
    r->sub = lv_label_create(col);
    lv_obj_set_style_text_font(r->sub, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(r->sub, C_MUTED, 0);

    small_button(o, LV_SYMBOL_EDIT " Rename", rename_event, addr);
    small_button(o, LV_SYMBOL_EYE_OPEN " Identify", ident_event, addr);
    lv_obj_t *rm = small_button(o, LV_SYMBOL_TRASH, remove_event, addr);
    lv_obj_set_style_text_color(rm, C_BAD, 0);
}

static void row_show(row_t *r, const zone_view_t *v)
{
    char t[16], sub[96];
    lv_label_set_text(r->name, v->cfg.name);
    const char *model = v->have ? (v->is_prthw ? "PRT/HW-N" : "PRT-N")
                        : v->cfg.model == HM_MODEL_PRTHW ? "PRT/HW-N"
                        : v->cfg.model == HM_MODEL_PRT ? "PRT-N" : "model not read yet";
    if (v->link == HM_LINK_OFFLINE) {
        snprintf(sub, sizeof sub, "%s, offline", model);
        lv_obj_set_style_text_color(r->sub, C_BAD, 0);
    } else if (!v->have) {
        snprintf(sub, sizeof sub, "%s, connecting", model);
        lv_obj_set_style_text_color(r->sub, C_MUTED, 0);
    } else {
        ui_fmt_temp(t, sizeof t, v->room_t10);
        snprintf(sub, sizeof sub, "%s, online, %s%s", model, t, v->locked ? ", keys locked" : "");
        lv_obj_set_style_text_color(r->sub, C_MUTED, 0);
    }
    lv_label_set_text(r->sub, sub);
}

static void rebuild(void)
{
    lv_obj_clean(s_list);
    memset(s_rows, 0, sizeof s_rows);
    uint32_t list = zone_store_list();
    for (int i = 0; i < HM_SCHED_MAX_STATS; i++) {
        if (list & (1u << i)) {
            row_build(&s_rows[i], (uint8_t)(i + 1));
        }
    }
    if (list) {
        lv_obj_add_flag(s_none, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(s_none, LV_OBJ_FLAG_HIDDEN);
    }
}

void setup_refresh(uint32_t dirty)
{
    if (!s_list) {
        return;
    }
    uint32_t list = zone_store_list();
    bool need = (dirty & ZONE_DIRTY_LIST) != 0;
    for (int i = 0; i < HM_SCHED_MAX_STATS && !need; i++) {
        need = (s_rows[i].row != NULL) != ((list >> i) & 1);
    }
    if (need) {
        rebuild();
        dirty = 0xFFFFFFFFu;
    }
    for (int i = 0; i < HM_SCHED_MAX_STATS; i++) {
        zone_view_t v;
        if ((dirty & (1u << i)) && s_rows[i].row && ui_zone((uint8_t)(i + 1), &v)) {
            row_show(&s_rows[i], &v);
        }
    }
    setup_tick();
}

/* ---- scan ----------------------------------------------------------------------------------------- */

static void scan_event(lv_event_t *e)
{
    (void)e;
    zone_store_scan();
    setup_tick();
}

static void show_scan(void)
{
    zone_scan_t sc;
    zone_store_scan_state(&sc);
    if (sc.running) {
        int done = sc.pass == 1 ? sc.next - 1 : 32 + sc.next - 1;
        lv_bar_set_value(s_scan_bar, done, LV_ANIM_OFF);
        lv_obj_remove_flag(s_scan_bar, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_fmt(s_scan_lbl, "%s address %u, found %u so far",
                              sc.pass == 1 ? "Scanning" : "Checking again", sc.next, sc.found);
        lv_obj_add_state(s_scan_btn, LV_STATE_DISABLED);
    } else {
        lv_obj_add_flag(s_scan_bar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_state(s_scan_btn, LV_STATE_DISABLED);
        if (s_was_scanning) {
            lv_label_set_text_fmt(s_scan_lbl, "Found %u stat%s, %u new", sc.found, sc.found == 1 ? "" : "s",
                                  sc.added);
        } else if (!lv_label_get_text(s_scan_lbl)[0]) {
            lv_label_set_text(s_scan_lbl, "Scanning pauses polling for about 15 seconds.");
        }
    }
    s_was_scanning = sc.running;
}

/* ---- system ------------------------------------------------------------------------------------------ */

static void timeout_event(lv_event_t *e)
{
    uint32_t i = lv_dropdown_get_selected(lv_event_get_target(e));
    if (i < sizeof s_timeouts / sizeof s_timeouts[0]) {
        ui_set_blank_timeout(s_timeouts[i]);
        ui_platform_save_blank_timeout(s_timeouts[i]);
    }
}

static void soak_event(lv_event_t *e)
{
    ui_set_soak(lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED));
}

static void show_sys(void)
{
    ui_sys_info_t si;
    ui_platform_sys_info(&si);
    char wifi[96], clock[24];
    if (!si.wifi_configured) {
        snprintf(wifi, sizeof wifi, "not set up (use 'wifi join' on the console)");
    } else if (si.wifi_up) {
        snprintf(wifi, sizeof wifi, "%s, %s, %d dBm", si.ssid, si.ip, si.rssi);
    } else {
        snprintf(wifi, sizeof wifi, "%s, connecting...", si.ssid);
    }
    ui_clock_text(clock, sizeof clock);
    lv_label_set_text_fmt(s_sys,
                          "Wi-Fi:  %s\n"
                          "Clock:  %s%s\n"
                          "Running:  %u d %u h %02u min\n"
                          "Memory:  %u KB internal free (lowest %u KB), %u KB PSRAM\n"
                          "Display:  %u frames drawn\n"
                          "Firmware:  %s",
                          wifi, clock, si.time_set ? "" : " (not synced yet)",
                          (unsigned)(si.uptime_s / 86400), (unsigned)(si.uptime_s / 3600 % 24),
                          (unsigned)(si.uptime_s / 60 % 60), (unsigned)(si.heap_int_free / 1024),
                          (unsigned)(si.heap_int_min / 1024), (unsigned)(si.heap_psram_free / 1024),
                          (unsigned)si.frames, si.version ? si.version : "?");
    if (ui_soak_on() != lv_obj_has_state(s_soak_sw, LV_STATE_CHECKED)) {
        if (ui_soak_on()) {
            lv_obj_add_state(s_soak_sw, LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(s_soak_sw, LV_STATE_CHECKED);
        }
    }
}

void setup_tick(void)
{
    if (!s_list) {
        return;
    }
    show_scan();
    show_sys();
}

static void back_event(lv_event_t *e)
{
    (void)e;
    ui_show_dash();
}

static lv_obj_t *row_flex(lv_obj_t *p)
{
    lv_obj_t *r = lv_obj_create(p);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r, 16, 0);
    return r;
}

lv_obj_t *setup_create(void)
{
    s_scr = lv_obj_create(NULL);
    ui_style_screen(s_scr);
    ui_header(s_scr, "Setup", true, back_event);

    lv_obj_t *tv = lv_tabview_create(s_scr);
    lv_obj_set_size(tv, 800, 480 - HEADER_H);
    lv_obj_set_pos(tv, 0, HEADER_H);
    lv_tabview_set_tab_bar_size(tv, 56);
    lv_obj_set_style_bg_color(tv, C_BG, 0);
    lv_obj_t *bar = lv_tabview_get_tab_bar(tv);
    lv_obj_set_style_bg_color(bar, C_PANEL_DIM, 0);
    lv_obj_set_style_text_font(bar, &lv_font_montserrat_20, 0);
    lv_obj_t *t1 = lv_tabview_add_tab(tv, "Thermostats");
    lv_obj_t *t2 = lv_tabview_add_tab(tv, "System");
    /* Swiping between tabs fights the list's own scrolling. */
    lv_obj_remove_flag(lv_tabview_get_content(tv), LV_OBJ_FLAG_SCROLLABLE);

    /* Thermostats */
    lv_obj_set_style_pad_all(t1, 12, 0);
    lv_obj_set_flex_flow(t1, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(t1, 10, 0);
    lv_obj_remove_flag(t1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *top = row_flex(t1);
    s_scan_btn = ui_button(top, LV_SYMBOL_REFRESH "  Scan bus", scan_event, NULL);
    lv_obj_set_style_bg_color(s_scan_btn, lv_color_hex(0x8A4A1F), 0);
    lv_obj_t *col = lv_obj_create(top);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, 1, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, 8, 0);
    s_scan_lbl = lv_label_create(col);
    lv_label_set_text(s_scan_lbl, "");
    lv_obj_set_style_text_color(s_scan_lbl, C_MUTED, 0);
    s_scan_bar = lv_bar_create(col);
    lv_obj_set_size(s_scan_bar, LV_PCT(100), 10);
    lv_bar_set_range(s_scan_bar, 0, 64);
    lv_obj_add_flag(s_scan_bar, LV_OBJ_FLAG_HIDDEN);

    s_list = lv_obj_create(t1);
    lv_obj_remove_style_all(s_list);
    lv_obj_set_width(s_list, LV_PCT(100));
    lv_obj_set_flex_grow(s_list, 1);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_list, 8, 0);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    s_none = lv_label_create(t1);
    lv_label_set_text(s_none, "No thermostats yet. Scan the bus to find them.");
    lv_obj_set_style_text_color(s_none, C_MUTED, 0);

    /* System */
    lv_obj_set_style_pad_all(t2, 20, 0);
    lv_obj_set_flex_flow(t2, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(t2, 18, 0);
    s_sys = lv_label_create(t2);
    lv_obj_set_width(s_sys, LV_PCT(100));
    lv_obj_set_style_text_line_space(s_sys, 8, 0);
    lv_obj_set_style_text_font(s_sys, &lv_font_montserrat_16, 0);
    lv_label_set_text(s_sys, "");

    lv_obj_t *r1 = row_flex(t2);
    lv_obj_t *l1 = lv_label_create(r1);
    lv_label_set_text(l1, "Screen off after");
    lv_obj_set_width(l1, 260);
    s_timeout = lv_dropdown_create(r1);
    lv_dropdown_set_options_static(s_timeout, "Never\n1 minute\n5 minutes\n10 minutes\n30 minutes");
    lv_obj_set_size(s_timeout, 220, TOUCH_MIN);
    lv_obj_set_style_pad_ver(s_timeout, 18, 0);
    lv_obj_set_style_bg_color(s_timeout, C_PANEL_HI, 0);
    lv_obj_set_style_border_width(s_timeout, 0, 0);
    lv_dropdown_set_dir(s_timeout, LV_DIR_TOP);
    lv_obj_add_event_cb(s_timeout, timeout_event, LV_EVENT_VALUE_CHANGED, NULL);
    for (uint32_t i = 0; i < sizeof s_timeouts / sizeof s_timeouts[0]; i++) {
        if (s_timeouts[i] == ui_blank_timeout()) {
            lv_dropdown_set_selected(s_timeout, i);
        }
    }

    lv_obj_t *r2 = row_flex(t2);
    lv_obj_t *l2 = lv_label_create(r2);
    lv_label_set_text(l2, "Drift test pattern");
    lv_obj_set_width(l2, 260);
    s_soak_sw = lv_switch_create(r2);
    lv_obj_set_size(s_soak_sw, 96, 48);
    lv_obj_add_event_cb(s_soak_sw, soak_event, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_t *l3 = lv_label_create(r2);
    lv_label_set_text(l3, "for the 24 h display soak");
    lv_obj_set_style_text_color(l3, C_FAINT, 0);
    lv_obj_set_style_text_font(l3, &lv_font_montserrat_16, 0);

    rebuild();
    return s_scr;
}
