/* Hot water on a PRT/HW-N: on or off now and why, the next switch, timer or
 * forced on or off, a boost, and today's timer on a 24-hour strip. */
#include <stdio.h>
#include <string.h>

#include "ui_internal.h"

#define LEFT_W   280
#define STRIP_W  458
#define STRIP_H  36

static lv_obj_t *s_scr, *s_boost_state, *s_left, *s_icon_line, *s_big, *s_mode_txt, *s_next_cap, *s_next;
static lv_obj_t *s_mode[3], *s_boost[3], *s_now_lbl, *s_strip, *s_seg[4], *s_marker, *s_none, *s_edit;

static const unsigned s_boost_min[3] = { 30, 60, 120 };
static const int s_cmds[3] = { HM_HW_FOLLOW_TIMER, HM_HW_FORCE_ON, HM_HW_FORCE_OFF };

static void back_event(lv_event_t *e)
{
    (void)e;
    ui_show_dash();
}

static void mode_event(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    ui_report(zone_store_set_hw(ui_cur_addr, s_cmds[i]), "Hot water");
    ui_refresh(1u << (ui_cur_addr - 1));
}

static void boost_event(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    zone_view_t v;
    if (!ui_zone(ui_cur_addr, &v)) {
        return;
    }
    if (v.hw_mode == ZONE_HW_BOOST && v.boost_len_min == s_boost_min[i]) {
        ui_report(zone_store_hw_boost(ui_cur_addr, 0), "Boost"); /* tap again to end it */
    } else {
        ui_report(zone_store_hw_boost(ui_cur_addr, s_boost_min[i]), "Boost");
    }
    ui_refresh(1u << (ui_cur_addr - 1));
}

static void edit_event(lv_event_t *e)
{
    (void)e;
    ui_show_schedule(ui_cur_addr, true);
}

static void strip_show(const zone_view_t *v)
{
    int wday, now;
    const bool have_now = ui_now(v, &wday, &now);
    const int day = zone_prog_day(v->hw_timer_days, have_now ? wday : 1);
    const bool have = v->have && v->hw_timer_days > 0;
    for (int i = 0; i < 4; i++) {
        const hm_period_t *on = &v->hw_on_at[day][i], *off = &v->hw_off_at[day][i];
        if (!have || on->hour >= 24) {
            lv_obj_add_flag(s_seg[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        int a = on->hour * 60 + on->minute;
        int b = off->hour < 24 ? off->hour * 60 + off->minute : 24 * 60;
        if (b <= a) {
            lv_obj_add_flag(s_seg[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(s_seg[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(s_seg[i], a * STRIP_W / 1440, 0);
        lv_obj_set_size(s_seg[i], (b - a) * STRIP_W / 1440, STRIP_H);
    }
    if (have && have_now) {
        lv_obj_remove_flag(s_marker, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_x(s_marker, now * STRIP_W / 1440 - 1);
    } else {
        lv_obj_add_flag(s_marker, LV_OBJ_FLAG_HIDDEN);
    }
    if (have) {
        lv_obj_add_flag(s_none, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(s_none, LV_OBJ_FLAG_HIDDEN);
    }
    if (have_now) {
        lv_label_set_text_fmt(s_now_lbl, "%s, now %02d:%02d",
                              have ? zone_prog_day_name(v->hw_timer_days, day) : "Today", now / 60, now % 60);
    } else {
        lv_label_set_text(s_now_lbl, "Clock not set");
    }
}

static void hw_refresh(uint32_t dirty)
{
    zone_view_t v;
    if (!ui_cur_addr || !(dirty & (1u << (ui_cur_addr - 1))) || !ui_zone(ui_cur_addr, &v)) {
        return;
    }
    const bool offline = v.link == HM_LINK_OFFLINE;
    const bool live = v.have && !offline;
    const bool on = live && v.hw_on;

    lv_obj_set_style_bg_color(s_left, on ? C_HEAT_BG : C_TILE, 0);
    lv_obj_set_style_border_color(s_left, on ? C_HEAT_BORDER : C_BORDER, 0);
    char line[ZONE_NAME_MAX + 8];
    snprintf(line, sizeof line, HM_ICON_DROP "  %s", v.cfg.name);
    ui_marquee_text(s_icon_line, line);
    lv_obj_set_style_text_color(s_icon_line, on ? C_COPPER : C_MUTED, 0);
    lv_label_set_text(s_big, !live ? "--" : on ? "ON" : "OFF");
    lv_obj_set_style_text_color(s_big, on ? C_COPPER : live ? C_TEXT : C_MUTED, 0);

    const char *mode = "Following timer";
    switch (v.hw_mode) {
    case ZONE_HW_ON:    mode = "Forced on"; break;
    case ZONE_HW_OFF:   mode = v.boost_left_min ? "Held off" : "Forced off"; break;
    case ZONE_HW_BOOST: mode = "Boost"; break;
    default: break;
    }
    lv_label_set_text(s_mode_txt, offline ? "Stat offline" : !v.have ? "Connecting" ELLIPSIS
                                  : v.pend.hw ? "Changing" ELLIPSIS : mode);

    /* Next switch */
    int wday, now;
    bool on_after = false;
    int next = -1;
    if (live && v.hw_timer_days && ui_now(&v, &wday, &now)) {
        int day = zone_prog_day(v.hw_timer_days, wday);
        next = zone_hw_timer_next(v.hw_on_at[day], v.hw_off_at[day], now, &on_after);
    }
    if (v.hw_mode == ZONE_HW_BOOST && v.boost_left_min) {
        lv_label_set_text(s_next_cap, "Boost ends in");
        lv_label_set_text_fmt(s_next, "%u min", v.boost_left_min);
    } else if (v.hw_mode == ZONE_HW_OFF && v.boost_left_min) {
        lv_label_set_text(s_next_cap, "Timer resumes in");
        lv_label_set_text_fmt(s_next, "%u min", v.boost_left_min);
    } else if (v.hw_mode == ZONE_HW_ON || v.hw_mode == ZONE_HW_OFF) {
        lv_label_set_text(s_next_cap, "Until you choose");
        lv_label_set_text(s_next, "Resume timer");
        lv_obj_set_style_text_font(s_next, F_TITLE, 0);
    } else if (next >= 0) {
        lv_label_set_text(s_next_cap, on_after ? "Timer turns on at" : "Timer turns off at");
        lv_label_set_text_fmt(s_next, "%02d:%02d", next / 60, next % 60);
    } else {
        lv_label_set_text(s_next_cap, "Timer");
        lv_label_set_text(s_next, live && v.hw_timer_days ? "No more today" : "--");
    }
    if (v.hw_mode != ZONE_HW_ON && !(v.hw_mode == ZONE_HW_OFF && !v.boost_left_min)) {
        lv_obj_set_style_text_font(s_next, &hm_bc600_44, 0);
    }

    if (v.hw_mode == ZONE_HW_BOOST && !v.boost_left_min) {
        lv_label_set_text(s_boost_state, "Boost, waiting for the clock");
        lv_obj_set_style_text_color(s_boost_state, C_COPPER, 0);
    } else if (v.hw_mode == ZONE_HW_BOOST) {
        lv_label_set_text_fmt(s_boost_state, "Boost, %u min left", v.boost_left_min);
        lv_obj_set_style_text_color(s_boost_state, C_COPPER, 0);
    } else {
        lv_label_set_text(s_boost_state, "Boost off");
        lv_obj_set_style_text_color(s_boost_state, C_MUTED, 0);
    }

    /* A held off (from Matter, ending with the timer period) is not the
     * Force off button's open-ended off, so like a boost it lights none. */
    const bool held_off = v.hw_mode == ZONE_HW_OFF && v.boost_left_min;
    const int sel = v.hw_mode == ZONE_HW_ON ? 1 : v.hw_mode == ZONE_HW_OFF && !held_off ? 2
                    : v.hw_mode == ZONE_HW_TIMER ? 0 : -1;
    for (int i = 0; i < 3; i++) {
        ui_enable(s_mode[i], live);
        ui_btn_kind(s_mode[i], i == sel ? UI_BTN_SOLID : UI_BTN_PLAIN);
        ui_enable(s_boost[i], live);
        /* The bridge keeps the boost's length, so this holds after leaving
         * the screen or a restart; a Matter boost of another length lights
         * none. */
        ui_btn_kind(s_boost[i], v.hw_mode == ZONE_HW_BOOST && v.boost_len_min == s_boost_min[i] ? UI_BTN_TINT
                                                                                                : UI_BTN_PLAIN);
    }
    ui_enable(s_edit, v.have && v.hw_timer_days > 0);
    strip_show(&v);
}

static void hw_tick(void)
{
    hw_refresh(ui_cur_addr ? 1u << (ui_cur_addr - 1) : 0);
}

void hw_open(uint8_t addr)
{
    ui_cur_addr = addr;
}

static lv_obj_t *wide_btn(lv_obj_t *parent, const char *text, lv_event_cb_t cb, void *user)
{
    lv_obj_t *b = ui_btn(parent, text, cb, user);
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_style_pad_hor(b, 4, 0);
    return b;
}

static lv_obj_t *hw_create(void)
{
    s_scr = ui_screen_new();
    ui_hdr_t h = ui_header(s_scr, "Hot water", back_event);
    s_boost_state = ui_label(h.right, F_LABEL, C_MUTED, "");

    lv_obj_t *body = ui_body(s_scr);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(body, GAP, 0);

    s_left = ui_panel(body);
    lv_obj_set_size(s_left, LEFT_W, LV_PCT(100));
    lv_obj_set_style_pad_all(s_left, 16, 0);
    s_icon_line = ui_label(s_left, F_LABEL, C_MUTED, "");
    ui_label_marquee(s_icon_line);
    lv_obj_set_width(s_icon_line, LV_PCT(100));
    s_big = ui_label(s_left, &hm_bc600_150, C_TEXT, "");
    lv_obj_align(s_big, LV_ALIGN_LEFT_MID, -4, -50);
    s_mode_txt = ui_label(s_left, F_TITLE, C_TEXT, "");
    lv_obj_set_style_text_font(s_mode_txt, F_LABEL, 0);
    lv_obj_align(s_mode_txt, LV_ALIGN_LEFT_MID, 0, 46);
    s_next = ui_label(s_left, &hm_bc600_44, C_TEXT, "");
    lv_obj_align(s_next, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    s_next_cap = ui_label(s_left, F_BODY, C_MUTED, "");
    lv_obj_align(s_next_cap, LV_ALIGN_BOTTOM_LEFT, 0, -52);

    lv_obj_t *right = ui_col(body, GAP);
    lv_obj_set_height(right, LV_PCT(100));
    lv_obj_set_flex_grow(right, 1);

    lv_obj_t *mode = ui_col(right, 6);
    ui_caption(mode, "MODE");
    lv_obj_t *mr = ui_row(mode, 8);
    static const char *const mode_txt[3] = { "Resume timer", "Force on", "Force off" };
    for (int i = 0; i < 3; i++) {
        s_mode[i] = wide_btn(mr, mode_txt[i], mode_event, (void *)(intptr_t)i);
    }
    lv_obj_t *boost = ui_col(right, 6);
    ui_caption(boost, "BOOST");
    lv_obj_t *br = ui_row(boost, 8);
    static const char *const boost_txt[3] = { "30 min", "60 min", "120 min" };
    for (int i = 0; i < 3; i++) {
        s_boost[i] = wide_btn(br, boost_txt[i], boost_event, (void *)(intptr_t)i);
    }

    lv_obj_t *today = ui_panel(right);
    lv_obj_set_width(today, LV_PCT(100));
    lv_obj_set_flex_grow(today, 1);
    lv_obj_set_style_pad_hor(today, 14, 0);
    lv_obj_t *cap = ui_caption(today, "TODAY");
    lv_obj_align(cap, LV_ALIGN_TOP_LEFT, 0, 0);
    s_now_lbl = ui_label(today, F_BODY, C_MUTED, "");
    lv_obj_align(s_now_lbl, LV_ALIGN_TOP_RIGHT, 0, 0);

    s_strip = ui_box(today);
    lv_obj_set_size(s_strip, STRIP_W, STRIP_H + 24);
    lv_obj_align(s_strip, LV_ALIGN_TOP_LEFT, 0, 30);
    lv_obj_set_style_clip_corner(s_strip, false, 0);
    lv_obj_t *track = ui_box(s_strip);
    lv_obj_set_size(track, STRIP_W, STRIP_H);
    lv_obj_set_style_bg_color(track, C_TRACK, 0);
    lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(track, 4, 0);
    for (int i = 0; i < 4; i++) {
        s_seg[i] = ui_box(s_strip);
        lv_obj_set_style_bg_color(s_seg[i], C_COPPER, 0);
        lv_obj_set_style_bg_opa(s_seg[i], LV_OPA_COVER, 0);
        lv_obj_set_style_radius(s_seg[i], 4, 0);
    }
    s_marker = ui_box(s_strip);
    lv_obj_set_size(s_marker, 3, STRIP_H + 8);
    lv_obj_set_y(s_marker, -4);
    lv_obj_set_style_bg_color(s_marker, C_TEXT, 0);
    lv_obj_set_style_bg_opa(s_marker, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_marker, 2, 0);
    lv_obj_add_flag(s_marker, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_add_flag(s_strip, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    static const char *const hours[5] = { "00", "06", "12", "18", "24" };
    for (int i = 0; i < 5; i++) {
        lv_obj_t *l = ui_label(s_strip, F_BODY, C_MUTED, hours[i]);
        lv_obj_update_layout(l);
        int x = i * STRIP_W / 4 - lv_obj_get_width(l) / 2;
        if (i == 0) {
            x = 0;
        } else if (i == 4) {
            x = STRIP_W - lv_obj_get_width(l);
        }
        lv_obj_set_pos(l, x, STRIP_H + 6);
    }
    s_none = ui_label(s_strip, F_BODY, C_MUTED, "Timer not read yet");
    lv_obj_set_pos(s_none, 12, 9);

    s_edit = ui_btn(today, HM_ICON_PENCIL "  Edit timer", edit_event, NULL);
    lv_obj_align(s_edit, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    return s_scr;
}

const ui_screen_ops_t ui_hw_ops = { hw_create, hw_refresh, hw_tick };
