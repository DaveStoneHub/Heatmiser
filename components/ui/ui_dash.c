/* Dashboard: a tile per zone, 4 across and 3 down, scrolling past 12, with a
 * hot water tile after each PRT/HW-N. Tap a tile for the zone; long-press
 * for a quick +/-. */
#include <stdio.h>
#include <string.h>

#include "ui_internal.h"

#define COLS     4
#define GAP      10
#define TILE_W   ((800 - (COLS + 1) * GAP) / COLS)          /* 187 */
#define TILE_H   ((480 - HEADER_H - 4 * GAP) / 3)           /* 128 */

typedef struct {
    lv_obj_t *obj, *name, *icon, *lock, *temp, *set, *state;
    bool hw;          /* the hot water tile of a PRT/HW-N */
} tile_t;

static lv_obj_t *s_scr, *s_grid, *s_empty, *s_clock, *s_wifi, *s_summary;
static tile_t s_tiles[HM_SCHED_MAX_STATS];
static tile_t s_hw[HM_SCHED_MAX_STATS];
static uint32_t s_has_hw; /* tiles built with a hot water tile */

/* ---- quick adjust and hot water popups ---------------------------------------------- */

static struct {
    lv_obj_t *bd, *value;
    uint8_t addr;
    lv_timer_t *timer;
} s_quick;

static void quick_close(void)
{
    if (s_quick.bd) {
        ui_dial_flush();
        lv_obj_delete_async(s_quick.bd);
        s_quick.bd = NULL;
        lv_timer_pause(s_quick.timer);
    }
}

static void quick_timeout(lv_timer_t *t)
{
    (void)t;
    quick_close();
}

static void quick_show_value(void)
{
    int c;
    zone_view_t v;
    if (!ui_dial_value(s_quick.addr, &c)) {
        if (!ui_zone(s_quick.addr, &v)) {
            return;
        }
        c = zone_view_shown_set(&v);
    }
    lv_label_set_text_fmt(s_quick.value, "%d" DEG, c);
}

static void quick_step(lv_event_t *e)
{
    int d = (int)(intptr_t)lv_event_get_user_data(e);
    ui_dial_nudge(s_quick.addr, d);
    quick_show_value();
    lv_timer_reset(s_quick.timer);
}

static void quick_bd_event(lv_event_t *e)
{
    if (lv_event_get_target(e) == lv_event_get_current_target(e)) {
        quick_close(); /* a tap outside the panel */
    }
}

static void quick_open_zone(lv_event_t *e)
{
    (void)e;
    uint8_t a = s_quick.addr;
    quick_close();
    ui_show_zone(a);
}

static lv_obj_t *round_button(lv_obj_t *p, const char *sym, lv_event_cb_t cb, void *user)
{
    lv_obj_t *b = lv_button_create(p);
    lv_obj_set_size(b, 96, 96);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(b, C_PANEL_HI, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, sym);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_40, 0);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    return b;
}

static lv_obj_t *popup(const char *title, lv_event_cb_t bd_cb, lv_obj_t **bd_out)
{
    lv_obj_t *bd = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(bd);
    lv_obj_set_size(bd, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(bd, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(bd, LV_OPA_60, 0);
    lv_obj_add_flag(bd, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(bd, bd_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *box = lv_obj_create(bd);
    lv_obj_set_size(box, 520, 300);
    lv_obj_center(box);
    lv_obj_set_style_bg_color(box, C_PANEL, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_radius(box, 18, 0);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *t = lv_label_create(box);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_28, 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 0);
    *bd_out = bd;
    return box;
}

static void quick_open(uint8_t addr)
{
    zone_view_t v;
    if (!ui_zone(addr, &v)) {
        return;
    }
    if (!v.have || v.link == HM_LINK_OFFLINE) {
        ui_toast(v.link == HM_LINK_OFFLINE ? "That stat is offline" : "Waiting for the stat to answer");
        return;
    }
    s_quick.addr = addr;
    lv_obj_t *box = popup(v.cfg.name, quick_bd_event, &s_quick.bd);
    lv_obj_t *minus = round_button(box, LV_SYMBOL_MINUS, quick_step, (void *)(intptr_t)-1);
    lv_obj_align(minus, LV_ALIGN_LEFT_MID, 10, -6);
    lv_obj_t *plus = round_button(box, LV_SYMBOL_PLUS, quick_step, (void *)(intptr_t)1);
    lv_obj_align(plus, LV_ALIGN_RIGHT_MID, -10, -6);
    s_quick.value = lv_label_create(box);
    lv_obj_set_style_text_font(s_quick.value, &hm_num_96, 0);
    lv_obj_align(s_quick.value, LV_ALIGN_CENTER, 0, -6);
    lv_obj_t *open = ui_button(box, "Open zone " LV_SYMBOL_RIGHT, quick_open_zone, NULL);
    lv_obj_align(open, LV_ALIGN_BOTTOM_MID, 0, 0);
    quick_show_value();
    if (!s_quick.timer) {
        s_quick.timer = lv_timer_create(quick_timeout, 5000, NULL);
    }
    lv_timer_reset(s_quick.timer);
    lv_timer_resume(s_quick.timer);
}

static struct {
    lv_obj_t *bd;
    uint8_t addr;
} s_hwpop;

static void hw_close(void)
{
    if (s_hwpop.bd) {
        lv_obj_delete_async(s_hwpop.bd);
        s_hwpop.bd = NULL;
    }
}

static void hw_bd_event(lv_event_t *e)
{
    if (lv_event_get_target(e) == lv_event_get_current_target(e)) {
        hw_close();
    }
}

static void hw_cmd_event(lv_event_t *e)
{
    int cmd = (int)(intptr_t)lv_event_get_user_data(e);
    ui_report(zone_store_set_hw(s_hwpop.addr, cmd), "Hot water");
    hw_close();
    dash_refresh(1u << (s_hwpop.addr - 1));
}

static void hw_open(uint8_t addr)
{
    zone_view_t v;
    if (!ui_zone(addr, &v)) {
        return;
    }
    if (v.link == HM_LINK_OFFLINE) {
        ui_toast("That stat is offline");
        return;
    }
    s_hwpop.addr = addr;
    lv_obj_t *box = popup("Hot water", hw_bd_event, &s_hwpop.bd);
    lv_obj_t *st = lv_label_create(box);
    lv_label_set_text_fmt(st, "%s, on %s", v.have && v.hw_on ? "On now" : "Off now", v.cfg.name);
    lv_obj_set_style_text_color(st, C_MUTED, 0);
    lv_obj_align(st, LV_ALIGN_TOP_MID, 0, 48);
    lv_obj_t *row = lv_obj_create(box);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_align(row, LV_ALIGN_CENTER, 0, 24);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    ui_button(row, "Timer", hw_cmd_event, (void *)(intptr_t)HM_HW_FOLLOW_TIMER);
    lv_obj_t *on = ui_button(row, "On", hw_cmd_event, (void *)(intptr_t)HM_HW_FORCE_ON);
    lv_obj_set_style_bg_color(on, lv_color_hex(0x1F4F6E), 0);
    ui_button(row, "Off", hw_cmd_event, (void *)(intptr_t)HM_HW_FORCE_OFF);
    lv_obj_t *note = lv_label_create(box);
    lv_label_set_text(note, "On and Off hold until you choose Timer.\nBoost comes in a later update.");
    lv_obj_set_style_text_font(note, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(note, C_FAINT, 0);
    lv_obj_set_style_text_align(note, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(note, LV_ALIGN_BOTTOM_MID, 0, 0);
}

/* ---- tiles --------------------------------------------------------------------------- */

static void tile_event(lv_event_t *e)
{
    tile_t *t = lv_event_get_user_data(e);
    uint8_t addr = (uint8_t)(uintptr_t)lv_obj_get_user_data(t->obj);
    if (t->hw) {
        hw_open(addr);
    } else if (lv_event_get_code(e) == LV_EVENT_LONG_PRESSED) {
        quick_open(addr);
    } else {
        ui_show_zone(addr);
    }
}

static lv_obj_t *label(lv_obj_t *p, const lv_font_t *f, lv_color_t c)
{
    lv_obj_t *l = lv_label_create(p);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_label_set_text(l, "");
    return l;
}

static void tile_build(tile_t *t, uint8_t addr, bool hw)
{
    memset(t, 0, sizeof *t);
    t->hw = hw;
    lv_obj_t *o = lv_obj_create(s_grid);
    t->obj = o;
    lv_obj_set_size(o, TILE_W, TILE_H);
    lv_obj_set_style_radius(o, 14, 0);
    lv_obj_set_style_bg_color(o, C_PANEL, 0);
    lv_obj_set_style_bg_color(o, C_PANEL_HI, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_border_color(o, C_HEAT, 0);
    lv_obj_set_style_pad_all(o, 12, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(o, (void *)(uintptr_t)addr);
    lv_obj_add_event_cb(o, tile_event, LV_EVENT_SHORT_CLICKED, t);
    lv_obj_add_event_cb(o, tile_event, LV_EVENT_LONG_PRESSED, t);

    t->name = label(o, &lv_font_montserrat_20, C_TEXT);
    lv_obj_set_size(t->name, TILE_W - 24 - 40, 26);
    lv_label_set_long_mode(t->name, LV_LABEL_LONG_DOT);
    lv_obj_align(t->name, LV_ALIGN_TOP_LEFT, 0, 0);
    t->icon = label(o, &hm_icons_20, C_FAINT);
    lv_obj_align(t->icon, LV_ALIGN_TOP_RIGHT, 0, 0);
    t->lock = label(o, &hm_icons_20, C_MUTED);
    lv_obj_align(t->lock, LV_ALIGN_TOP_RIGHT, -28, 0);
    t->temp = label(o, &lv_font_montserrat_40, C_TEXT);
    lv_obj_align(t->temp, LV_ALIGN_LEFT_MID, 0, 4);
    t->set = label(o, &lv_font_montserrat_16, C_MUTED);
    lv_obj_align(t->set, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    t->state = label(o, &lv_font_montserrat_16, C_MUTED);
    lv_obj_align(t->state, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
}

static void tile_show(tile_t *t, const zone_view_t *v)
{
    char buf[48];
    const bool offline = v->link == HM_LINK_OFFLINE;
    const char *err = v->have ? zone_error_str(v->error_code) : NULL;
    lv_obj_set_style_bg_color(t->obj, offline ? C_PANEL_DIM : C_PANEL, 0);

    if (t->hw) {
        lv_label_set_text(t->name, "Hot water");
        bool on = v->have && v->hw_on;
        lv_label_set_text(t->icon, HM_ICON_DROP);
        lv_obj_set_style_text_color(t->icon, on && !offline ? C_WATER : C_FAINT, 0);
        lv_label_set_text(t->lock, "");
        lv_label_set_text(t->temp, !v->have ? "--" : on ? "On" : "Off");
        lv_obj_set_style_text_color(t->temp, offline ? C_FAINT : on ? C_WATER : C_TEXT, 0);
        lv_label_set_text(t->set, v->cfg.name);
        lv_label_set_text(t->state, offline ? "Offline" : v->pend.hw ? "Changing..." : "");
        lv_obj_set_style_text_color(t->state, offline ? C_BAD : C_MUTED, 0);
        lv_obj_set_style_border_width(t->obj, 0, 0);
        return;
    }

    lv_label_set_text(t->name, v->cfg.name);
    lv_obj_set_style_text_color(t->name, offline ? C_MUTED : C_TEXT, 0);

    ui_fmt_temp(buf, sizeof buf, v->have ? v->room_t10 : ZONE_T_ABSENT);
    lv_label_set_text(t->temp, buf);
    lv_obj_set_style_text_color(t->temp, offline ? C_FAINT : C_TEXT, 0);

    const hm_mode_t mode = zone_view_shown_mode(v);
    const char *icon = "";
    lv_color_t ic = C_FAINT;
    if (offline || err) {
        icon = HM_ICON_WARN;
        ic = C_BAD;
    } else if (v->have && mode == HM_MODE_HEAT && v->heating) {
        icon = HM_ICON_FIRE;
        ic = C_HEAT;
    } else if (v->have && mode == HM_MODE_FROST) {
        icon = HM_ICON_SNOW;
        ic = C_FROST;
    } else if (v->have && mode == HM_MODE_OFF) {
        icon = HM_ICON_POWER;
    }
    lv_label_set_text(t->icon, icon);
    lv_obj_set_style_text_color(t->icon, ic, 0);
    lv_label_set_text(t->lock, v->have && zone_view_shown_lock(v) ? HM_ICON_LOCK : "");
    lv_obj_align(t->lock, LV_ALIGN_TOP_RIGHT, icon[0] ? -28 : 0, 0);

    const bool heating = !offline && v->have && v->heating && mode == HM_MODE_HEAT;
    lv_obj_set_style_border_width(t->obj, heating ? 2 : 0, 0);

    int dial;
    if (!v->have) {
        buf[0] = 0;
    } else if (mode == HM_MODE_FROST) {
        snprintf(buf, sizeof buf, "Frost %u" DEG, v->frost_temp);
    } else if (mode == HM_MODE_OFF) {
        buf[0] = 0;
    } else if (ui_dial_value(v->cfg.addr, &dial)) {
        snprintf(buf, sizeof buf, "Set %d" DEG "...", dial);
    } else {
        snprintf(buf, sizeof buf, "Set %u" DEG "%s", zone_view_shown_set(v), v->pend.set_temp ? "..." : "");
    }
    lv_label_set_text(t->set, buf);

    if (offline) {
        lv_label_set_text(t->state, "Offline");
        lv_obj_set_style_text_color(t->state, C_BAD, 0);
    } else if (err) {
        lv_label_set_text(t->state, "Fault");
        lv_obj_set_style_text_color(t->state, C_BAD, 0);
    } else {
        lv_label_set_text(t->state, zone_view_busy(v) ? "..." : zone_state_str(v));
        lv_obj_set_style_text_color(t->state, heating ? C_HEAT : mode == HM_MODE_FROST ? C_FROST : C_MUTED, 0);
    }
}

static bool want_hw(const zone_view_t *v)
{
    return v->have ? v->is_prthw : v->cfg.model == HM_MODEL_PRTHW;
}

/* Rebuild the grid in address order. */
static void rebuild(void)
{
    lv_obj_clean(s_grid);
    memset(s_tiles, 0, sizeof s_tiles);
    memset(s_hw, 0, sizeof s_hw);
    s_has_hw = 0;
    uint32_t list = zone_store_list();
    for (int i = 0; i < HM_SCHED_MAX_STATS; i++) {
        if (!(list & (1u << i))) {
            continue;
        }
        zone_view_t v;
        if (!ui_zone((uint8_t)(i + 1), &v)) {
            continue;
        }
        tile_build(&s_tiles[i], (uint8_t)(i + 1), false);
        if (want_hw(&v)) {
            tile_build(&s_hw[i], (uint8_t)(i + 1), true);
            s_has_hw |= 1u << i;
        }
    }
    if (list) {
        lv_obj_add_flag(s_empty, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_grid, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(s_empty, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_grid, LV_OBJ_FLAG_HIDDEN);
    }
}

static void update_summary(void)
{
    uint32_t list = zone_store_list();
    unsigned n = 0, online = 0, heating = 0;
    for (int i = 0; i < HM_SCHED_MAX_STATS; i++) {
        zone_view_t v;
        if ((list & (1u << i)) && ui_zone((uint8_t)(i + 1), &v)) {
            n++;
            online += v.link == HM_LINK_ONLINE;
            heating += v.link == HM_LINK_ONLINE && v.have && v.heating && v.mode == HM_MODE_HEAT;
        }
    }
    if (!n) {
        lv_label_set_text(s_summary, "");
    } else if (online == n) {
        lv_label_set_text_fmt(s_summary, "%u zones, %u heating", n, heating);
    } else {
        lv_label_set_text_fmt(s_summary, "%u of %u online, %u heating", online, n, heating);
    }
}

void dash_refresh(uint32_t dirty)
{
    if (!s_grid) {
        return;
    }
    bool need_rebuild = (dirty & ZONE_DIRTY_LIST) != 0;
    uint32_t list = zone_store_list();
    for (int i = 0; i < HM_SCHED_MAX_STATS && !need_rebuild; i++) {
        bool there = s_tiles[i].obj != NULL;
        if (there != ((list >> i) & 1)) {
            need_rebuild = true;
        }
    }
    if (need_rebuild) {
        rebuild();
        dirty = 0xFFFFFFFFu;
    }
    for (int i = 0; i < HM_SCHED_MAX_STATS; i++) {
        zone_view_t v;
        if (!(dirty & (1u << i)) || !s_tiles[i].obj || !ui_zone((uint8_t)(i + 1), &v)) {
            continue;
        }
        if (want_hw(&v) != ((s_has_hw >> i) & 1)) {
            rebuild(); /* learnt the model */
            dash_refresh(0xFFFFFFFFu & ~ZONE_DIRTY_LIST);
            return;
        }
        tile_show(&s_tiles[i], &v);
        if (s_hw[i].obj) {
            tile_show(&s_hw[i], &v);
        }
    }
    update_summary();
}

void dash_tick(void)
{
    char buf[24];
    ui_clock_text(buf, sizeof buf);
    lv_label_set_text(s_clock, buf);
    ui_sys_info_t si;
    ui_platform_sys_info(&si);
    lv_label_set_text(s_wifi, si.wifi_configured ? HM_ICON_WIFI : "");
    lv_obj_set_style_text_color(s_wifi, si.wifi_up ? C_MUTED : C_BAD, 0);
}

static void setup_event(lv_event_t *e)
{
    (void)e;
    ui_show_setup();
}

static void scan_now_event(lv_event_t *e)
{
    (void)e;
    zone_store_scan();
    ui_show_setup();
}

lv_obj_t *dash_create(void)
{
    s_scr = lv_obj_create(NULL);
    ui_style_screen(s_scr);
    lv_obj_t *h = ui_header(s_scr, "Heating", false, NULL);
    s_summary = label(h, &lv_font_montserrat_16, C_MUTED);
    lv_obj_align(s_summary, LV_ALIGN_LEFT_MID, 110, 1);
    lv_obj_t *gear = lv_button_create(h);
    lv_obj_remove_style_all(gear);
    lv_obj_set_size(gear, 72, HEADER_H);
    lv_obj_align(gear, LV_ALIGN_RIGHT_MID, 12, 0);
    lv_obj_t *gl = lv_label_create(gear);
    lv_label_set_text(gl, HM_ICON_GEAR);
    lv_obj_set_style_text_font(gl, &hm_icons_32, 0);
    lv_obj_set_style_text_color(gl, C_MUTED, 0);
    lv_obj_center(gl);
    lv_obj_add_event_cb(gear, setup_event, LV_EVENT_CLICKED, NULL);
    s_clock = label(h, &lv_font_montserrat_20, C_TEXT);
    lv_obj_align(s_clock, LV_ALIGN_RIGHT_MID, -70, 0);
    s_wifi = label(h, &hm_icons_20, C_MUTED);
    lv_obj_align(s_wifi, LV_ALIGN_RIGHT_MID, -190, 0);

    s_grid = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_grid);
    lv_obj_set_size(s_grid, 800, 480 - HEADER_H);
    lv_obj_set_pos(s_grid, 0, HEADER_H);
    lv_obj_set_style_pad_all(s_grid, GAP, 0);
    lv_obj_set_style_pad_row(s_grid, GAP, 0);
    lv_obj_set_style_pad_column(s_grid, GAP, 0);
    lv_obj_set_flex_flow(s_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_scroll_dir(s_grid, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_grid, LV_SCROLLBAR_MODE_ACTIVE);

    s_empty = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_empty);
    lv_obj_set_size(s_empty, 800, 480 - HEADER_H);
    lv_obj_set_pos(s_empty, 0, HEADER_H);
    lv_obj_set_flex_flow(s_empty, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_empty, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_empty, 20, 0);
    lv_obj_t *m = label(s_empty, &lv_font_montserrat_28, C_TEXT);
    lv_label_set_text(m, "No thermostats yet");
    lv_obj_t *m2 = label(s_empty, &lv_font_montserrat_20, C_MUTED);
    lv_label_set_text(m2, "Scan the RS485 bus to find them. It takes about 15 seconds.");
    lv_obj_t *b = ui_button(s_empty, "Scan for thermostats", scan_now_event, NULL);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x8A4A1F), 0);
    rebuild();
    dash_tick();
    return s_scr;
}
