/* Zone detail: the setpoint dial, mode, hold, key lock and the stat's details
 * for one zone, with ways to its schedule and to rename it. */
#include <stdio.h>
#include <string.h>

#include "ui_internal.h"

#define DIAL      300
#define ARC_W     16
#define LEFT_W    400

static lv_obj_t *s_scr, *s_title, *s_state;
static lv_obj_t *s_left, *s_arc, *s_set, *s_set_deg, *s_set_cap, *s_now, *s_minus, *s_plus;
static lv_obj_t *s_mode[3], *s_hold[4];
static lv_obj_t *s_info_val[4], *s_info_key[4];
static lv_obj_t *s_lock, *s_sched;
static bool s_updating; /* setting widgets from the store, not from a touch */

static const hm_mode_t s_modes[3] = { HM_MODE_HEAT, HM_MODE_FROST, HM_MODE_OFF };
static const unsigned s_hold_min[4] = { 0, 60, 240, 1440 };

static void back_event(lv_event_t *e)
{
    (void)e;
    ui_show_dash();
}

static void rename_event(lv_event_t *e)
{
    (void)e;
    ui_show_rename(ui_cur_addr);
}

static void sched_event(lv_event_t *e)
{
    (void)e;
    ui_show_schedule(ui_cur_addr, false);
}

static void refresh_me(void)
{
    ui_refresh(1u << (ui_cur_addr - 1));
}

static void arc_event(lv_event_t *e)
{
    if (s_updating) {
        return;
    }
    int v = lv_arc_get_value(lv_event_get_target(e));
    ui_dial_set(ui_cur_addr, v);
    refresh_me();
}

static void step_event(lv_event_t *e)
{
    ui_dial_nudge(ui_cur_addr, (int)(intptr_t)lv_event_get_user_data(e));
    refresh_me();
}

static void mode_event(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    ui_dial_flush();
    ui_report(zone_store_set_mode(ui_cur_addr, s_modes[i]), "Mode");
    refresh_me();
}

static void hold_event(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    ui_dial_flush();
    ui_report(zone_store_set_hold(ui_cur_addr, s_hold_min[i]), "Hold");
    refresh_me();
}

static void lock_event(lv_event_t *e)
{
    (void)e;
    zone_view_t v;
    if (ui_zone(ui_cur_addr, &v)) {
        ui_report(zone_store_set_lock(ui_cur_addr, !zone_view_shown_lock(&v)), "Key lock");
        refresh_me();
    }
}

static void info_set(int row, const char *key, const char *val, lv_color_t c)
{
    lv_label_set_text(s_info_key[row], key ? key : "");
    lv_label_set_text(s_info_val[row], val ? val : "");
    lv_obj_set_style_text_color(s_info_val[row], c, 0);
}

static void zone_refresh(uint32_t dirty)
{
    zone_view_t v;
    if (!ui_cur_addr || !(dirty & (1u << (ui_cur_addr - 1))) || !ui_zone(ui_cur_addr, &v)) {
        return;
    }
    char buf[64];
    s_updating = true;
    const bool offline = v.link == HM_LINK_OFFLINE;
    const bool live = v.have && !offline;
    const hm_mode_t mode = zone_view_shown_mode(&v);
    const bool heating = live && v.heating && mode != HM_MODE_OFF;
    const char *err = live ? zone_error_str(v.error_code) : NULL;

    ui_marquee_text(s_title, v.cfg.name);

    /* Header state */
    lv_color_t sc = C_MUTED;
    if (offline) {
        lv_label_set_text(s_state, HM_ICON_WIFI_OFF "  Offline");
        sc = C_FAULT;
    } else if (!v.have) {
        lv_label_set_text(s_state, "Connecting" ELLIPSIS);
    } else if (err) {
        lv_label_set_text_fmt(s_state, HM_ICON_WARN "  %s", err);
        sc = C_FAULT;
    } else if (mode == HM_MODE_OFF) {
        lv_label_set_text(s_state, HM_ICON_POWER "  Switched off");
    } else if (heating) {
        lv_label_set_text(s_state, HM_ICON_FLAME "  Heating now");
        sc = C_COPPER;
    } else if (mode == HM_MODE_FROST) {
        lv_label_set_text(s_state, HM_ICON_SNOW "  Frost protection");
    } else {
        lv_label_set_text(s_state, "Not heating");
    }
    lv_obj_set_style_text_color(s_state, sc, 0);

    /* Dial */
    lv_obj_set_style_bg_color(s_left, heating ? C_HEAT_BG : C_TILE, 0);
    lv_obj_set_style_border_color(s_left, heating ? C_HEAT_BORDER : C_BORDER, 0);
    int dial;
    const bool frost = mode == HM_MODE_FROST;
    const bool pending = ui_dial_value(ui_cur_addr, &dial);
    int shown = pending ? dial : frost ? v.frost_temp : zone_view_shown_set(&v);
    const bool dial_on = live && mode == HM_MODE_HEAT;
    ui_enable(s_arc, dial_on);
    ui_enable(s_minus, dial_on);
    ui_enable(s_plus, dial_on);
    if (v.have && shown >= HM_SET_TEMP_MIN && shown <= HM_SET_TEMP_MAX) {
        lv_arc_set_value(s_arc, shown);
    } else if (v.have && frost) {
        lv_arc_set_value(s_arc, HM_SET_TEMP_MIN);
    }
    if (v.have && mode != HM_MODE_OFF) {
        lv_label_set_text_fmt(s_set, "%d", shown);
        lv_label_set_text(s_set_deg, DEG);
    } else {
        lv_label_set_text(s_set, "--");
        lv_label_set_text(s_set_deg, "");
    }
    lv_obj_update_layout(s_set);
    lv_obj_align_to(s_set_deg, s_set, LV_ALIGN_OUT_RIGHT_TOP, 0, 8);
    lv_color_t vc = dial_on ? C_COPPER : C_MUTED;
    lv_obj_set_style_text_color(s_set, vc, 0);
    lv_obj_set_style_text_color(s_set_deg, vc, 0);
    lv_label_set_text(s_set_cap, frost ? "FROST" : mode == HM_MODE_OFF ? "OFF"
                                 : (v.pend.set_temp || pending) ? "SETTING" ELLIPSIS : "SET");
    if (v.have && v.room_t10 != ZONE_T_ABSENT) {
        char t[16];
        ui_fmt_temp(t, sizeof t, v.room_t10);
        lv_label_set_text_fmt(s_now, "Now %s", t);
    } else {
        lv_label_set_text(s_now, v.have ? "No reading" : "");
    }

    /* Mode and hold */
    for (int i = 0; i < 3; i++) {
        ui_enable(s_mode[i], live);
        ui_btn_kind(s_mode[i], v.have && mode == s_modes[i] ? UI_BTN_SOLID : UI_BTN_PLAIN);
    }
    unsigned hold = v.pend.hold ? v.pend.hold_min : v.hold_min;
    int sel = 0;
    if (hold) {
        sel = 3;
        for (int i = 3; i >= 1; i--) {
            if (hold <= s_hold_min[i]) {
                sel = i;
            }
        }
    }
    for (int i = 0; i < 4; i++) {
        ui_enable(s_hold[i], live && mode == HM_MODE_HEAT);
        ui_btn_kind(s_hold[i], v.have && i == sel ? UI_BTN_TINT : UI_BTN_PLAIN);
    }

    /* Details */
    if (!v.have) {
        info_set(0, "Stat", offline ? "Not answering" : "Waiting for a reading", offline ? C_FAULT : C_TEXT);
        snprintf(buf, sizeof buf, "%u", v.cfg.addr);
        info_set(1, "Address", buf, C_TEXT);
        info_set(2, NULL, NULL, C_TEXT);
        info_set(3, NULL, NULL, C_TEXT);
    } else {
        snprintf(buf, sizeof buf, "%s, %s", zone_sensor_str(v.sensor_sel), v.is_prthw ? "PRT/HW-N" : "PRT-N");
        info_set(0, "Sensor", buf, C_TEXT);
        if (v.floor_t10 != ZONE_T_ABSENT) {
            ui_fmt_temp(buf, sizeof buf, v.floor_t10);
        } else {
            snprintf(buf, sizeof buf, "Not fitted");
        }
        info_set(1, "Floor", buf, C_TEXT);
        if (err) {
            snprintf(buf, sizeof buf, "%s (E%X)", err, v.error_code & 0x0F);
            info_set(2, "Fault", buf, C_FAULT);
        } else if (v.holiday_h) {
            snprintf(buf, sizeof buf, "%u h left", v.holiday_h);
            info_set(2, "Holiday", buf, C_COPPER);
        } else if (hold) {
            snprintf(buf, sizeof buf, "%u h %02u min left%s", hold / 60, hold % 60, v.pend.hold ? ELLIPSIS : "");
            info_set(2, "Hold", buf, C_COPPER);
        } else if (mode == HM_MODE_FROST) {
            snprintf(buf, sizeof buf, "%u" DEG "%s", v.frost_temp, v.frost_enabled ? "" : ", protection off");
            info_set(2, "Frost", buf, C_TEXT);
        } else {
            snprintf(buf, sizeof buf, "%u" DEG ", %s", v.frost_temp, v.frost_enabled ? "on" : "off");
            info_set(2, "Frost protection", buf, C_TEXT);
        }
        info_set(3, offline ? "Last heard" : "Last update", "", C_TEXT);
    }

    /* Lock and schedule */
    const bool locked = v.have && zone_view_shown_lock(&v);
    ui_enable(s_lock, live);
    lv_label_set_text(ui_btn_label(s_lock), locked ? HM_ICON_LOCK "  Keys locked" : HM_ICON_UNLOCK "  Key lock");
    ui_btn_kind(s_lock, locked ? UI_BTN_TINT : UI_BTN_PLAIN);
    ui_enable(s_sched, v.have && v.comfort_days > 0);
    s_updating = false;
}

static void zone_tick(void)
{
    zone_view_t v;
    if (!ui_cur_addr || !ui_zone(ui_cur_addr, &v) || !v.have) {
        return;
    }
    char age[24];
    ui_fmt_age(age, sizeof age, v.age_s);
    lv_label_set_text(s_info_val[3], age);
}

void zone_scr_open(uint8_t addr)
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

static lv_obj_t *zone_create(void)
{
    s_scr = ui_screen_new();
    ui_hdr_t h = ui_header(s_scr, "", back_event);
    s_title = h.title;
    lv_obj_t *left = lv_obj_get_user_data(h.bar);
    ui_icon_btn(left, HM_ICON_PENCIL, rename_event, NULL);
    ui_label_marquee(s_title);
    lv_obj_set_style_max_width(s_title, 330, 0);
    s_state = ui_label(h.right, F_LABEL, C_MUTED, "");

    lv_obj_t *body = ui_body(s_scr);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(body, GAP, 0);

    /* Left: the dial */
    s_left = ui_panel(body);
    lv_obj_set_size(s_left, LEFT_W, LV_PCT(100));
    s_arc = lv_arc_create(s_left);
    lv_obj_set_size(s_arc, DIAL, DIAL);
    lv_obj_align(s_arc, LV_ALIGN_TOP_MID, 0, 0);
    lv_arc_set_rotation(s_arc, 135);
    lv_arc_set_bg_angles(s_arc, 0, 270);
    lv_arc_set_range(s_arc, HM_SET_TEMP_MIN, HM_SET_TEMP_MAX);
    lv_obj_set_style_pad_all(s_arc, 14, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_arc, ARC_W, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_arc, ARC_W, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_arc, C_DIAL_TRACK, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_arc, C_COPPER, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_arc, C_MUTED, LV_PART_INDICATOR | LV_STATE_DISABLED);
    lv_obj_set_style_arc_rounded(s_arc, true, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(s_arc, true, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_arc, C_TEXT, LV_PART_KNOB);
    lv_obj_set_style_border_color(s_arc, C_COPPER, LV_PART_KNOB);
    lv_obj_set_style_border_width(s_arc, 4, LV_PART_KNOB);
    lv_obj_set_style_pad_all(s_arc, 7, LV_PART_KNOB);
    lv_obj_set_style_shadow_width(s_arc, 0, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(s_arc, LV_OPA_TRANSP, LV_PART_KNOB | LV_STATE_DISABLED);
    lv_obj_set_style_border_opa(s_arc, LV_OPA_TRANSP, LV_PART_KNOB | LV_STATE_DISABLED);
    lv_obj_set_style_opa(s_arc, LV_OPA_COVER, LV_STATE_DISABLED);
    lv_obj_add_event_cb(s_arc, arc_event, LV_EVENT_VALUE_CHANGED, NULL);
    s_set_cap = ui_label(s_arc, F_CAP, C_MUTED, "SET");
    lv_obj_align(s_set_cap, LV_ALIGN_CENTER, 0, -72);
    s_set = ui_label(s_arc, &hm_bc600_112, C_COPPER, "");
    lv_obj_align(s_set, LV_ALIGN_CENTER, -10, 0);
    s_set_deg = ui_label(s_arc, &hm_bc500_58, C_COPPER, "");
    lv_obj_align_to(s_set_deg, s_set, LV_ALIGN_OUT_RIGHT_TOP, 0, 8);
    lv_obj_add_flag(s_set, LV_OBJ_FLAG_EVENT_BUBBLE);
    s_now = ui_label(s_arc, F_BODY, C_MUTED, "");
    lv_obj_set_style_text_font(s_now, F_LABEL, 0);
    lv_obj_align(s_now, LV_ALIGN_CENTER, 0, 70);

    lv_obj_t *steps = ui_box(s_left);
    lv_obj_set_size(steps, LV_PCT(100), TOUCH_MIN);
    lv_obj_align(steps, LV_ALIGN_BOTTOM_MID, 0, 0);
    s_minus = ui_btn(steps, HM_ICON_MINUS, step_event, (void *)(intptr_t)-1);
    lv_obj_set_width(s_minus, 88);
    lv_obj_set_style_text_font(ui_btn_label(s_minus), &hm_icon_28, 0);
    lv_obj_align(s_minus, LV_ALIGN_LEFT_MID, 0, 0);
    s_plus = ui_btn(steps, HM_ICON_PLUS, step_event, (void *)(intptr_t)1);
    lv_obj_set_width(s_plus, 88);
    lv_obj_set_style_text_font(ui_btn_label(s_plus), &hm_icon_28, 0);
    lv_obj_align(s_plus, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_t *range = ui_label(steps, F_BODY, C_MUTED, "5 to 35" DEG ", whole degrees");
    lv_obj_center(range);

    /* Right: mode, hold, details, lock and schedule */
    lv_obj_t *right = ui_col(body, GAP);
    lv_obj_set_height(right, LV_PCT(100));
    lv_obj_set_flex_grow(right, 1);

    lv_obj_t *modes = ui_row(right, GAP);
    static const char *const mode_txt[3] = { "Heat", "Frost", "Off" };
    for (int i = 0; i < 3; i++) {
        s_mode[i] = wide_btn(modes, mode_txt[i], mode_event, (void *)(intptr_t)i);
    }

    lv_obj_t *hold = ui_col(right, 6);
    ui_caption(hold, "HOLD");
    lv_obj_t *chips = ui_row(hold, 8);
    static const char *const hold_txt[4] = { "Off", "1 h", "4 h", "24 h" };
    for (int i = 0; i < 4; i++) {
        s_hold[i] = wide_btn(chips, hold_txt[i], hold_event, (void *)(intptr_t)i);
    }

    lv_obj_t *info = ui_panel(right);
    lv_obj_set_width(info, LV_PCT(100));
    lv_obj_set_flex_grow(info, 1);
    lv_obj_set_style_pad_hor(info, 14, 0);
    lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(info, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    for (int i = 0; i < 4; i++) {
        lv_obj_t *r = ui_box(info);
        lv_obj_set_size(r, LV_PCT(100), 20);
        s_info_key[i] = ui_label(r, F_BODY, C_MUTED, "");
        lv_obj_align(s_info_key[i], LV_ALIGN_LEFT_MID, 0, 0);
        s_info_val[i] = ui_label(r, F_CAP, C_TEXT, "");
        lv_obj_align(s_info_val[i], LV_ALIGN_RIGHT_MID, 0, 0);
    }

    lv_obj_t *bottom = ui_row(right, GAP);
    s_lock = wide_btn(bottom, "", lock_event, NULL);
    s_sched = wide_btn(bottom, HM_ICON_CALENDAR "  Schedule", sched_event, NULL);
    return s_scr;
}

const ui_screen_ops_t ui_zone_ops = { zone_create, zone_refresh, zone_tick };
