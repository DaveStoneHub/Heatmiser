/* Dashboard: a tile per zone, 4 across and 3 down, scrolling past 12, with a
 * hot water tile after each PRT/HW-N. Tap a tile for the zone (or the hot
 * water screen); long-press a zone for a quick +/-. */
#include <stdio.h>
#include <string.h>

#include "ui_internal.h"

#define COLS     4
#define BODY_W   (SCR_W - 2 * PAD)
#define TILE_W   ((BODY_W - (COLS - 1) * GAP) / COLS)       /* 186 */
#define TILE_H   ((BODY_H - 2 * GAP) / 3)                   /* 132 */

typedef struct {
    lv_obj_t *obj, *name, *icons, *temp, *deg, *sub;
    bool hw;          /* the hot water tile of a PRT/HW-N */
} tile_t;

static lv_obj_t *s_scr, *s_grid, *s_empty, *s_clock, *s_wifi;
static tile_t s_tiles[HM_SCHED_MAX_STATS];
static tile_t s_hw[HM_SCHED_MAX_STATS];
static uint32_t s_has_hw; /* tiles built with a hot water tile */

/* ---- quick adjust popup ------------------------------------------------------------- */

static struct {
    lv_obj_t *value;
    uint8_t addr;
    lv_timer_t *timer;
} s_quick;

static void quick_timeout(lv_timer_t *t)
{
    (void)t;
    lv_timer_pause(s_quick.timer);
    ui_dial_flush();
    ui_popup_close();
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

static void quick_open_zone(lv_event_t *e)
{
    (void)e;
    lv_timer_pause(s_quick.timer);
    ui_show_zone(s_quick.addr);
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
    if (zone_view_shown_mode(&v) != HM_MODE_HEAT) {
        ui_show_zone(addr); /* the setpoint only applies in Heat */
        return;
    }
    s_quick.addr = addr;
    lv_obj_t *box = ui_popup(v.cfg.name, 520, 300, true);
    lv_obj_t *row = ui_box(box);
    lv_obj_set_size(row, LV_PCT(100), 140);
    lv_obj_t *minus = ui_btn(row, HM_ICON_MINUS, quick_step, (void *)(intptr_t)-1);
    lv_obj_set_size(minus, 88, TOUCH_MIN);
    lv_obj_set_style_text_font(ui_btn_label(minus), &hm_icon_28, 0);
    lv_obj_align(minus, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *plus = ui_btn(row, HM_ICON_PLUS, quick_step, (void *)(intptr_t)1);
    lv_obj_set_size(plus, 88, TOUCH_MIN);
    lv_obj_set_style_text_font(ui_btn_label(plus), &hm_icon_28, 0);
    lv_obj_align(plus, LV_ALIGN_RIGHT_MID, 0, 0);
    s_quick.value = ui_label(row, &hm_bc600_112, C_COPPER, "");
    lv_obj_align(s_quick.value, LV_ALIGN_CENTER, 0, 4);
    lv_obj_t *open = ui_btn(box, "Open zone  " HM_ICON_NEXT, quick_open_zone, NULL);
    lv_obj_set_width(open, LV_PCT(100));
    quick_show_value();
    if (!s_quick.timer) {
        s_quick.timer = lv_timer_create(quick_timeout, 5000, NULL);
    }
    lv_timer_reset(s_quick.timer);
    lv_timer_resume(s_quick.timer);
}

/* ---- tiles --------------------------------------------------------------------------- */

static void tile_event(lv_event_t *e)
{
    tile_t *t = lv_event_get_user_data(e);
    uint8_t addr = (uint8_t)(uintptr_t)lv_obj_get_user_data(t->obj);
    if (t->hw) {
        ui_show_hotwater(addr);
    } else if (lv_event_get_code(e) == LV_EVENT_LONG_PRESSED) {
        quick_open(addr);
    } else {
        ui_show_zone(addr);
    }
}

static void tile_build(tile_t *t, uint8_t addr, bool hw)
{
    memset(t, 0, sizeof *t);
    t->hw = hw;
    lv_obj_t *o = ui_panel(s_grid);
    t->obj = o;
    lv_obj_set_size(o, TILE_W, TILE_H);
    lv_obj_set_style_pad_hor(o, 14, 0);
    lv_obj_set_style_bg_color(o, C_PRESSED, LV_STATE_PRESSED);
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(o, (void *)(uintptr_t)addr);
    lv_obj_add_event_cb(o, tile_event, LV_EVENT_SHORT_CLICKED, t);
    if (!hw) {
        lv_obj_add_event_cb(o, tile_event, LV_EVENT_LONG_PRESSED, t);
    }

    t->name = ui_label(o, F_LABEL, C_TEXT, "");
    ui_label_marquee(t->name);
    lv_obj_set_width(t->name, TILE_W - 28 - 52);
    lv_obj_align(t->name, LV_ALIGN_TOP_LEFT, 0, 0);
    t->icons = ui_label(o, &hm_icon_22, C_MUTED, "");
    lv_obj_set_style_text_letter_space(t->icons, 6, 0);
    lv_obj_align(t->icons, LV_ALIGN_TOP_RIGHT, 0, -2);
    t->temp = ui_label(o, hw ? &hm_bc600_44 : &hm_bc500_58, C_TEXT, "");
    lv_obj_align(t->temp, LV_ALIGN_LEFT_MID, -2, hw ? 2 : 4);
    t->deg = ui_label(o, &hm_bc600_30, C_TEXT, "");
    lv_obj_align_to(t->deg, t->temp, LV_ALIGN_OUT_RIGHT_TOP, 2, 4);
    t->sub = ui_label(o, F_BODY, C_MUTED, "");
    lv_obj_set_width(t->sub, TILE_W - 28);
    ui_label_dots(t->sub);
    lv_obj_align(t->sub, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

typedef enum { LOOK_PLAIN, LOOK_HEAT, LOOK_OFFLINE, LOOK_FAULT } look_t;

static void tile_look(tile_t *t, look_t k)
{
    lv_color_t bg = C_TILE, border = C_BORDER;
    switch (k) {
    case LOOK_HEAT:    bg = C_HEAT_BG; border = C_HEAT_BORDER; break;
    case LOOK_OFFLINE: bg = C_OFFLINE_BG; break;
    case LOOK_FAULT:   bg = C_FAULT_BG; border = C_FAULT; break;
    default: break;
    }
    lv_obj_set_style_bg_color(t->obj, bg, 0);
    lv_obj_set_style_border_color(t->obj, border, 0);
    ui_dashed(t->obj, k == LOOK_OFFLINE, C_MUTED);
    lv_obj_set_style_text_color(t->name, k == LOOK_OFFLINE ? C_MUTED : C_TEXT, 0);
}

static void set_temp_text(tile_t *t, const char *big, bool deg, lv_color_t c)
{
    lv_label_set_text(t->temp, big);
    lv_label_set_text(t->deg, deg ? DEG : "");
    lv_obj_set_style_text_color(t->temp, c, 0);
    lv_obj_set_style_text_color(t->deg, c, 0);
    lv_obj_align_to(t->deg, t->temp, LV_ALIGN_OUT_RIGHT_TOP, 1, 4);
}

static const char *hw_mode_str(const zone_view_t *v, char *buf, size_t cap)
{
    if (v->pend.hw) {
        return "Changing" ELLIPSIS;
    }
    switch (v->hw_mode) {
    case ZONE_HW_ON:  return "Forced on";
    case ZONE_HW_OFF:
        if (!v->boost_left_min) {
            return "Forced off";
        }
        snprintf(buf, cap, "Off for %u min", v->boost_left_min);
        return buf;
    case ZONE_HW_BOOST:
        if (!v->boost_left_min) {
            return "Boost"; /* just restarted, waiting for the clock */
        }
        snprintf(buf, cap, "Boost, %u min left", v->boost_left_min);
        return buf;
    default:          return "Following timer";
    }
}

static void tile_show_hw(tile_t *t, const zone_view_t *v)
{
    char buf[40];
    const bool offline = v->link == HM_LINK_OFFLINE;
    const bool on = v->have && v->hw_on && !offline;
    ui_marquee_text(t->name, "Hot water");
    tile_look(t, offline ? LOOK_OFFLINE : on ? LOOK_HEAT : LOOK_PLAIN);
    lv_label_set_text(t->icons, offline ? HM_ICON_WIFI_OFF : HM_ICON_DROP);
    lv_obj_set_style_text_color(t->icons, on ? C_COPPER : C_MUTED, 0);
    set_temp_text(t, !v->have || offline ? "--" : on ? "On" : "Off", false,
                  on ? C_COPPER : offline ? C_MUTED : C_TEXT);
    lv_label_set_text(t->sub, offline ? "Offline" : !v->have ? "Connecting" ELLIPSIS : hw_mode_str(v, buf, sizeof buf));
}

static void tile_show(tile_t *t, const zone_view_t *v)
{
    if (t->hw) {
        tile_show_hw(t, v);
        return;
    }
    char buf[48], icons[16] = "";
    const bool offline = v->link == HM_LINK_OFFLINE;
    const char *err = v->have && !offline ? zone_error_str(v->error_code) : NULL;
    const hm_mode_t mode = zone_view_shown_mode(v);
    const bool heating = !offline && v->have && v->heating && mode != HM_MODE_OFF;

    ui_marquee_text(t->name, v->cfg.name);
    if (v->have && !offline && zone_view_shown_lock(v)) {
        strcat(icons, HM_ICON_LOCK);
    }

    if (offline) {
        tile_look(t, LOOK_OFFLINE);
        strcpy(icons, HM_ICON_WIFI_OFF);
        lv_obj_set_style_text_color(t->icons, C_MUTED, 0);
        set_temp_text(t, "--", false, C_MUTED);
        lv_label_set_text(t->sub, "Offline");
        lv_obj_set_style_text_color(t->sub, C_MUTED, 0);
        lv_label_set_text(t->icons, icons);
        return;
    }
    if (err) {
        tile_look(t, LOOK_FAULT);
        strcpy(icons, HM_ICON_WARN);
        lv_obj_set_style_text_color(t->icons, C_FAULT, 0);
        snprintf(buf, sizeof buf, "E%X", v->error_code & 0x0F);
        set_temp_text(t, buf, false, C_FAULT);
        lv_label_set_text(t->sub, err);
        lv_obj_set_style_text_color(t->sub, C_FAULT, 0);
        lv_label_set_text(t->icons, icons);
        return;
    }

    tile_look(t, heating ? LOOK_HEAT : LOOK_PLAIN);
    if (heating) {
        strcat(icons, HM_ICON_FLAME);
    } else if (v->have && mode == HM_MODE_FROST) {
        strcat(icons, HM_ICON_SNOW);
    } else if (v->have && mode == HM_MODE_OFF) {
        strcat(icons, HM_ICON_POWER);
    }
    lv_label_set_text(t->icons, icons);
    lv_obj_set_style_text_color(t->icons, heating ? C_COPPER : C_MUTED, 0);

    if (v->have && v->room_t10 != ZONE_T_ABSENT) {
        zone_fmt_t10(v->room_t10, buf, sizeof buf);
        set_temp_text(t, buf, true, heating ? C_COPPER : C_TEXT);
    } else {
        set_temp_text(t, "--", false, C_MUTED);
    }

    int dial;
    if (!v->have) {
        snprintf(buf, sizeof buf, "Connecting" ELLIPSIS);
    } else if (v->holiday_h) {
        snprintf(buf, sizeof buf, "Holiday, %u h", v->holiday_h);
    } else if (mode == HM_MODE_FROST) {
        snprintf(buf, sizeof buf, "Frost %u" DEG "%s", v->frost_temp, v->pend.mode ? ELLIPSIS : "");
    } else if (mode == HM_MODE_OFF) {
        snprintf(buf, sizeof buf, "Off%s", v->pend.mode ? ELLIPSIS : "");
    } else if (ui_dial_value(v->cfg.addr, &dial)) {
        snprintf(buf, sizeof buf, "Set %d" DEG ELLIPSIS, dial);
    } else {
        int n = snprintf(buf, sizeof buf, "Set %u" DEG "%s", zone_view_shown_set(v),
                         v->pend.set_temp || v->pend.mode ? ELLIPSIS : "");
        if (v->hold_min && n > 0 && (size_t)n < sizeof buf) {
            snprintf(buf + n, sizeof buf - (size_t)n, " \xC2\xB7 hold");
        }
    }
    lv_label_set_text(t->sub, buf);
    lv_obj_set_style_text_color(t->sub, C_MUTED, 0);
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
        zone_view_t v;
        if (!(list & (1u << i)) || !ui_zone((uint8_t)(i + 1), &v)) {
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

static void dash_refresh(uint32_t dirty)
{
    bool need_rebuild = (dirty & ZONE_DIRTY_LIST) != 0;
    uint32_t list = zone_store_list();
    for (int i = 0; i < HM_SCHED_MAX_STATS && !need_rebuild; i++) {
        need_rebuild = (s_tiles[i].obj != NULL) != ((list >> i) & 1);
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
}

static void dash_tick(void)
{
    char buf[24];
    ui_clock_text(buf, sizeof buf, false);
    lv_label_set_text(s_clock, buf);
    ui_sys_info_t si;
    ui_platform_sys_info(&si);
    lv_label_set_text(s_wifi, !si.wifi_configured ? "" : si.wifi_up ? HM_ICON_WIFI : HM_ICON_WIFI_OFF);
    lv_obj_set_style_text_color(s_wifi, si.wifi_up ? C_MUTED : C_FAULT, 0);
    /* Boost minutes count down on the hot water tiles. */
    for (int i = 0; i < HM_SCHED_MAX_STATS; i++) {
        zone_view_t v;
        if (s_hw[i].obj && ui_zone((uint8_t)(i + 1), &v) && v.hw_mode == ZONE_HW_BOOST) {
            tile_show(&s_hw[i], &v);
        }
    }
}

static void settings_event(lv_event_t *e)
{
    (void)e;
    ui_show_settings();
}

static void scan_now_event(lv_event_t *e)
{
    (void)e;
    zone_store_scan();
    ui_show_zones();
}

static lv_obj_t *dash_create(void)
{
    s_scr = ui_screen_new();
    ui_hdr_t h = ui_header(s_scr, "Heating", NULL);
    s_wifi = ui_label(h.right, &hm_icon_22, C_MUTED, "");
    s_clock = ui_label(h.right, &hm_bc600_20, C_TEXT, "");
    lv_obj_set_style_text_font(s_clock, &hm_bc600_30, 0);
    ui_icon_btn(h.right, HM_ICON_SLIDERS, settings_event, NULL);

    s_grid = ui_body(s_scr);
    lv_obj_set_style_pad_row(s_grid, GAP, 0);
    lv_obj_set_style_pad_column(s_grid, GAP, 0);
    lv_obj_set_flex_flow(s_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_add_flag(s_grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_grid, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_grid, LV_SCROLLBAR_MODE_ACTIVE);

    s_empty = ui_body(s_scr);
    lv_obj_set_flex_flow(s_empty, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_empty, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_empty, 18, 0);
    ui_label(s_empty, &hm_bc600_44, C_TEXT, "No thermostats yet");
    ui_label(s_empty, F_BODY, C_MUTED, "Scan the RS485 bus to find them. It takes about 15 seconds.");
    lv_obj_t *b = ui_btn(s_empty, HM_ICON_REFRESH "  Scan for thermostats", scan_now_event, NULL);
    ui_btn_kind(b, UI_BTN_SOLID);
    rebuild();
    return s_scr;
}

const ui_screen_ops_t ui_dash_ops = { dash_create, dash_refresh, dash_tick };
