/* Zone detail: the room temperature, the setpoint dial, mode, hold and key
 * lock for one stat. */
#include <stdio.h>
#include <string.h>

#include "ui_internal.h"

static lv_obj_t *s_scr, *s_title, *s_info;
static lv_obj_t *s_room, *s_sensor, *s_state_icon, *s_state, *s_hold, *s_fault, *s_age;
static lv_obj_t *s_arc, *s_set, *s_set_cap, *s_minus, *s_plus;
static lv_obj_t *s_mode, *s_hold_dd, *s_lock, *s_lock_lbl;
static uint8_t s_addr;
static bool s_updating; /* setting widgets from the store, not from a touch */

static const char *const s_mode_map[] = { "Heat", "Frost", "Off", "" };
static const unsigned s_hold_min[] = { 0, 60, 120, 240, 480, 1440 };

uint8_t zone_scr_addr(void)
{
    return s_addr;
}

static lv_obj_t *label(lv_obj_t *p, const lv_font_t *f, lv_color_t c, int x, int y)
{
    lv_obj_t *l = lv_label_create(p);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_label_set_text(l, "");
    lv_obj_set_pos(l, x, y);
    return l;
}

static void back_event(lv_event_t *e)
{
    (void)e;
    ui_show_dash();
}

static void rename_event(lv_event_t *e)
{
    (void)e;
    ui_rename_dialog(s_addr);
}

static void arc_event(lv_event_t *e)
{
    if (s_updating) {
        return;
    }
    int v = lv_arc_get_value(lv_event_get_target(e));
    ui_dial_set(s_addr, v);
    lv_label_set_text_fmt(s_set, "%d" DEG, v);
}

static void step_event(lv_event_t *e)
{
    int d = (int)(intptr_t)lv_event_get_user_data(e);
    ui_dial_nudge(s_addr, d);
    zone_scr_refresh(1u << (s_addr - 1));
}

static void mode_event(lv_event_t *e)
{
    if (s_updating) {
        return;
    }
    uint32_t b = lv_buttonmatrix_get_selected_button(lv_event_get_target(e));
    static const hm_mode_t modes[] = { HM_MODE_HEAT, HM_MODE_FROST, HM_MODE_OFF };
    if (b < 3) {
        ui_dial_flush();
        ui_report(zone_store_set_mode(s_addr, modes[b]), "Mode");
        zone_scr_refresh(1u << (s_addr - 1));
    }
}

static void hold_event(lv_event_t *e)
{
    if (s_updating) {
        return;
    }
    uint32_t i = lv_dropdown_get_selected(lv_event_get_target(e));
    if (i < sizeof s_hold_min / sizeof s_hold_min[0]) {
        ui_dial_flush();
        ui_report(zone_store_set_hold(s_addr, s_hold_min[i]), "Hold");
    }
    zone_scr_refresh(1u << (s_addr - 1));
}

static void lock_event(lv_event_t *e)
{
    (void)e;
    zone_view_t v;
    if (ui_zone(s_addr, &v)) {
        ui_report(zone_store_set_lock(s_addr, !zone_view_shown_lock(&v)), "Key lock");
        zone_scr_refresh(1u << (s_addr - 1));
    }
}

static lv_obj_t *step_button(lv_obj_t *p, const char *sym, int d)
{
    lv_obj_t *b = lv_button_create(p);
    lv_obj_set_size(b, 84, 84);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(b, C_PANEL_HI, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, sym);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_40, 0);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, step_event, LV_EVENT_CLICKED, (void *)(intptr_t)d);
    return b;
}

static void enable(lv_obj_t *o, bool on)
{
    if (on) {
        lv_obj_remove_state(o, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(o, LV_STATE_DISABLED);
    }
}

void zone_scr_refresh(uint32_t dirty)
{
    zone_view_t v;
    if (!s_scr || !s_addr || !(dirty & (1u << (s_addr - 1))) || !ui_zone(s_addr, &v)) {
        return;
    }
    char buf[64];
    s_updating = true;
    const bool offline = v.link == HM_LINK_OFFLINE;
    const bool live = v.have && !offline;
    const hm_mode_t mode = zone_view_shown_mode(&v);

    lv_label_set_text_fmt(s_title, "%s  " LV_SYMBOL_EDIT, v.cfg.name);
    lv_label_set_text_fmt(s_info, "Stat %u  %s", v.cfg.addr,
                          v.have ? (v.is_prthw ? "PRT/HW-N" : "PRT-N") : "");

    ui_fmt_temp(buf, sizeof buf, v.have ? v.room_t10 : ZONE_T_ABSENT);
    lv_label_set_text(s_room, buf);
    lv_obj_set_style_text_color(s_room, live ? C_TEXT : C_FAINT, 0);

    if (v.have) {
        char fl[16];
        ui_fmt_temp(fl, sizeof fl, v.floor_t10);
        if (v.floor_t10 != ZONE_T_ABSENT && v.sensor_sel != 2) {
            lv_label_set_text_fmt(s_sensor, "%s sensor, floor %s", zone_sensor_str(v.sensor_sel), fl);
        } else {
            lv_label_set_text_fmt(s_sensor, "%s sensor", zone_sensor_str(v.sensor_sel));
        }
    } else {
        lv_label_set_text(s_sensor, "");
    }

    const char *icon = "";
    lv_color_t ic = C_MUTED;
    if (!v.have) {
        lv_label_set_text(s_state, offline ? "" : "Waiting for the stat...");
    } else if (mode == HM_MODE_OFF) {
        icon = HM_ICON_POWER;
        lv_label_set_text(s_state, "Switched off");
    } else if (mode == HM_MODE_FROST) {
        icon = HM_ICON_SNOW;
        ic = C_FROST;
        lv_label_set_text_fmt(s_state, "Frost protection at %u" DEG "%s", v.frost_temp,
                              v.heating ? ", heating" : "");
    } else if (v.heating) {
        icon = HM_ICON_FIRE;
        ic = C_HEAT;
        lv_label_set_text(s_state, "Heating now");
    } else {
        icon = HM_ICON_THERMO;
        lv_label_set_text(s_state, "Not heating");
    }
    lv_label_set_text(s_state_icon, icon);
    lv_obj_set_style_text_color(s_state_icon, ic, 0);

    if (v.have && v.holiday_h) {
        lv_label_set_text_fmt(s_hold, HM_ICON_CLOCK "  Holiday, %u h left", v.holiday_h);
    } else if (v.have && (v.hold_min || v.pend.hold)) {
        unsigned m = v.pend.hold ? v.pend.hold_min : v.hold_min;
        if (m) {
            lv_label_set_text_fmt(s_hold, HM_ICON_CLOCK "  Held for %u h %02u min%s", m / 60, m % 60,
                                  v.pend.hold ? "..." : "");
        } else {
            lv_label_set_text(s_hold, HM_ICON_CLOCK "  Ending hold...");
        }
    } else {
        lv_label_set_text(s_hold, "");
    }

    const char *err = v.have ? zone_error_str(v.error_code) : NULL;
    if (offline) {
        lv_label_set_text(s_fault, HM_ICON_WARN "  Offline: check its wiring and address");
    } else if (err) {
        lv_label_set_text_fmt(s_fault, HM_ICON_WARN "  %s (E%X)", err, v.error_code & 0x0F);
    } else {
        lv_label_set_text(s_fault, "");
    }

    /* Dial */
    int dial;
    bool frost = mode == HM_MODE_FROST;
    int shown = ui_dial_value(s_addr, &dial) ? dial : frost ? v.frost_temp : zone_view_shown_set(&v);
    bool dial_on = live && mode == HM_MODE_HEAT;
    enable(s_arc, dial_on);
    enable(s_minus, dial_on);
    enable(s_plus, dial_on);
    if (!v.have) {
        lv_arc_set_value(s_arc, HM_SET_TEMP_MIN);
    } else if (!frost && shown >= HM_SET_TEMP_MIN && shown <= HM_SET_TEMP_MAX) {
        lv_arc_set_value(s_arc, shown);
    }
    if (v.have) {
        lv_label_set_text_fmt(s_set, "%d" DEG, shown);
    } else {
        lv_label_set_text(s_set, "--");
    }
    lv_obj_set_style_text_color(s_set, dial_on ? C_TEXT : C_FAINT, 0);
    lv_label_set_text(s_set_cap, frost ? "Frost" : (v.pend.set_temp || ui_dial_value(s_addr, &dial)) ? "Setting..." : "Target");

    /* Mode, hold, lock */
    enable(s_mode, live);
    enable(s_hold_dd, live && mode == HM_MODE_HEAT);
    enable(s_lock, live);
    lv_buttonmatrix_clear_button_ctrl_all(s_mode, LV_BUTTONMATRIX_CTRL_CHECKED);
    if (v.have) {
        lv_buttonmatrix_set_button_ctrl(s_mode, mode == HM_MODE_HEAT ? 0 : mode == HM_MODE_FROST ? 1 : 2,
                                        LV_BUTTONMATRIX_CTRL_CHECKED);
    }
    const bool locked = v.have && zone_view_shown_lock(&v);
    lv_label_set_text(s_lock_lbl, locked ? HM_ICON_LOCK "  Locked" : HM_ICON_LOCK "  Unlocked");
    lv_obj_set_style_bg_color(s_lock, locked ? lv_color_hex(0x3A4756) : C_PANEL_HI, 0);
    s_updating = false;
    zone_scr_tick();
}

void zone_scr_tick(void)
{
    zone_view_t v;
    if (!s_addr || !ui_zone(s_addr, &v)) {
        return;
    }
    char age[24];
    if (!v.have) {
        lv_label_set_text(s_age, "");
        return;
    }
    ui_fmt_age(age, sizeof age, v.age_s);
    lv_label_set_text_fmt(s_age, "%s %s", v.link == HM_LINK_OFFLINE ? "Last heard" : "Updated", age);
}

void zone_scr_open(uint8_t addr)
{
    s_addr = addr;
    lv_dropdown_close(s_hold_dd);
    zone_scr_refresh(1u << (addr - 1));
}

lv_obj_t *zone_scr_create(void)
{
    s_scr = lv_obj_create(NULL);
    ui_style_screen(s_scr);
    lv_obj_t *h = ui_header(s_scr, "", true, back_event);
    s_title = lv_obj_get_user_data(h);
    lv_obj_add_flag(s_title, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(s_title, 16);
    lv_obj_add_event_cb(s_title, rename_event, LV_EVENT_CLICKED, NULL);
    s_info = lv_label_create(h);
    lv_obj_set_style_text_font(s_info, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_info, C_MUTED, 0);
    lv_obj_align(s_info, LV_ALIGN_RIGHT_MID, 0, 0);

    /* Left: the room */
    lv_label_set_text(label(s_scr, &lv_font_montserrat_20, C_MUTED, 28, 76), "Room");
    s_room = label(s_scr, &hm_num_96, C_TEXT, 20, 100);
    s_sensor = label(s_scr, &lv_font_montserrat_16, C_MUTED, 28, 206);
    s_state_icon = label(s_scr, &hm_icons_20, C_MUTED, 28, 242);
    s_state = label(s_scr, &lv_font_montserrat_20, C_TEXT, 62, 240);
    s_hold = label(s_scr, &ui_font20i, C_TEXT, 28, 276);
    s_fault = label(s_scr, &ui_font20i, C_BAD, 28, 312);
    s_age = label(s_scr, &lv_font_montserrat_16, C_FAINT, 28, 352);

    /* Right: the setpoint dial */
    s_arc = lv_arc_create(s_scr);
    lv_obj_set_size(s_arc, 250, 250);
    lv_obj_set_pos(s_arc, 480, 66);
    lv_arc_set_rotation(s_arc, 135);
    lv_arc_set_bg_angles(s_arc, 0, 270);
    lv_arc_set_range(s_arc, HM_SET_TEMP_MIN, HM_SET_TEMP_MAX);
    lv_obj_set_style_arc_width(s_arc, 18, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_arc, 18, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_arc, C_PANEL_HI, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_arc, C_HEAT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_arc, C_FAINT, LV_PART_INDICATOR | LV_STATE_DISABLED);
    lv_obj_set_style_bg_color(s_arc, C_TEXT, LV_PART_KNOB);
    lv_obj_set_style_pad_all(s_arc, 6, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(s_arc, LV_OPA_TRANSP, LV_PART_KNOB | LV_STATE_DISABLED);
    lv_obj_add_event_cb(s_arc, arc_event, LV_EVENT_VALUE_CHANGED, NULL);
    s_set = lv_label_create(s_arc);
    lv_obj_set_style_text_font(s_set, &hm_num_64, 0);
    lv_obj_align(s_set, LV_ALIGN_CENTER, 0, 6);
    s_set_cap = lv_label_create(s_arc);
    lv_obj_set_style_text_color(s_set_cap, C_MUTED, 0);
    lv_obj_align(s_set_cap, LV_ALIGN_CENTER, 0, -50);
    s_minus = step_button(s_scr, LV_SYMBOL_MINUS, -1);
    lv_obj_set_pos(s_minus, 440, 296);
    s_plus = step_button(s_scr, LV_SYMBOL_PLUS, 1);
    lv_obj_set_pos(s_plus, 686, 296);

    /* Bottom: mode, hold, lock */
    s_mode = lv_buttonmatrix_create(s_scr);
    lv_buttonmatrix_set_map(s_mode, s_mode_map);
    lv_buttonmatrix_set_button_ctrl_all(s_mode, LV_BUTTONMATRIX_CTRL_CHECKABLE);
    lv_buttonmatrix_set_one_checked(s_mode, true);
    lv_obj_set_size(s_mode, 380, 72);
    lv_obj_set_pos(s_mode, 16, 396);
    lv_obj_set_style_bg_color(s_mode, C_PANEL_DIM, 0);
    lv_obj_set_style_border_width(s_mode, 0, 0);
    lv_obj_set_style_pad_all(s_mode, 4, 0);
    lv_obj_set_style_pad_gap(s_mode, 4, 0);
    lv_obj_set_style_bg_color(s_mode, C_PANEL_HI, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(s_mode, lv_color_hex(0x8A4A1F), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(s_mode, C_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(s_mode, 0, LV_PART_ITEMS);
    lv_obj_add_event_cb(s_mode, mode_event, LV_EVENT_VALUE_CHANGED, NULL);

    s_hold_dd = lv_dropdown_create(s_scr);
    lv_dropdown_set_options_static(s_hold_dd, "No hold\nHold 1 h\nHold 2 h\nHold 4 h\nHold 8 h\nHold 24 h");
    lv_dropdown_set_text(s_hold_dd, "Hold...");
    lv_dropdown_set_dir(s_hold_dd, LV_DIR_TOP);
    lv_dropdown_set_symbol(s_hold_dd, LV_SYMBOL_UP);
    lv_obj_set_size(s_hold_dd, 180, 72);
    lv_obj_set_pos(s_hold_dd, 410, 396);
    lv_obj_set_style_bg_color(s_hold_dd, C_PANEL_HI, 0);
    lv_obj_set_style_border_width(s_hold_dd, 0, 0);
    lv_obj_set_style_pad_ver(s_hold_dd, 22, 0);
    lv_obj_add_event_cb(s_hold_dd, hold_event, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_t *list = lv_dropdown_get_list(s_hold_dd);
    lv_obj_set_style_text_font(list, &lv_font_montserrat_20, 0);
    lv_obj_set_style_pad_ver(list, 12, 0);
    lv_obj_set_style_bg_color(list, C_PANEL_HI, 0);
    lv_obj_set_style_max_height(list, 380, 0);

    s_lock = lv_button_create(s_scr);
    lv_obj_set_size(s_lock, 190, 72);
    lv_obj_set_pos(s_lock, 600, 396);
    lv_obj_set_style_shadow_width(s_lock, 0, 0);
    lv_obj_set_style_radius(s_lock, 12, 0);
    s_lock_lbl = lv_label_create(s_lock);
    lv_obj_set_style_text_font(s_lock_lbl, &ui_font20i, 0);
    lv_obj_center(s_lock_lbl);
    lv_obj_add_event_cb(s_lock, lock_event, LV_EVENT_CLICKED, NULL);
    return s_scr;
}
