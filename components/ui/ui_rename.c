/* Rename zone: the name with a 16-character counter and an on-screen
 * keyboard with letters, an apostrophe, a digits layer, a key that puts back
 * the default name, Cancel and Save. */
#include <stdio.h>
#include <string.h>

#include "ui_internal.h"

static lv_obj_t *s_scr, *s_sub, *s_text, *s_cursor, *s_count, *s_hint, *s_keys, *s_layer_btn, *s_reset;
static char s_name[ZONE_NAME_MAX + 1];
static char s_default[ZONE_NAME_MAX + 1];
static ui_screen_t s_back;
static bool s_shift, s_digits;
static lv_timer_t *s_blink;

/* Three rows of 20 width units; shift and backspace are one and a half keys. */
static const char *s_map_lower[] = {
    "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "\n",
    "a", "s", "d", "f", "g", "h", "j", "k", "l", "'", "\n",
    HM_ICON_SHIFT, "z", "x", "c", "v", "b", "n", "m", HM_ICON_BKSP, "",
};
static const char *s_map_upper[] = {
    "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "\n",
    "A", "S", "D", "F", "G", "H", "J", "K", "L", "'", "\n",
    HM_ICON_SHIFT, "Z", "X", "C", "V", "B", "N", "M", HM_ICON_BKSP, "",
};
static const char *s_map_digits[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "\n",
    "-", "/", "&", "(", ")", ".", ",", "!", "?", "'", "\n",
    HM_ICON_SHIFT, "+", "#", "@", ":", ";", "=", "*", HM_ICON_BKSP, "",
};
#define KEY_SHIFT 20
#define KEY_BKSP  28

static void show_text(void)
{
    lv_label_set_text(s_text, s_name);
    lv_label_set_text_fmt(s_count, "%u / %u", (unsigned)strlen(s_name), ZONE_NAME_MAX);
    lv_obj_set_style_text_color(s_count, strlen(s_name) >= ZONE_NAME_MAX ? C_FAULT : C_COPPER, 0);
    lv_obj_update_layout(s_text);
    lv_obj_align_to(s_cursor, s_text, LV_ALIGN_OUT_RIGHT_MID, 3, 0);
    if (!s_name[0]) {
        lv_obj_align(s_cursor, LV_ALIGN_LEFT_MID, 0, 0);
    }
}

static void set_map(void)
{
    const char **map = s_digits ? s_map_digits : s_shift ? s_map_upper : s_map_lower;
    lv_buttonmatrix_set_map(s_keys, map);
    lv_buttonmatrix_set_button_width(s_keys, KEY_SHIFT, 3);
    lv_buttonmatrix_set_button_width(s_keys, KEY_BKSP, 3);
    for (uint32_t i = 0; i <= KEY_BKSP; i++) {
        if (i != KEY_SHIFT && i != KEY_BKSP) {
            lv_buttonmatrix_set_button_width(s_keys, i, 2);
        }
    }
    if (s_shift && !s_digits) {
        lv_buttonmatrix_set_button_ctrl(s_keys, KEY_SHIFT, LV_BUTTONMATRIX_CTRL_CHECKED);
    } else {
        lv_buttonmatrix_clear_button_ctrl(s_keys, KEY_SHIFT, LV_BUTTONMATRIX_CTRL_CHECKED);
    }
    lv_label_set_text(ui_btn_label(s_layer_btn), s_digits ? "ABC" : "123");
}

static void type(const char *s)
{
    size_t n = strlen(s_name);
    if (n + strlen(s) > ZONE_NAME_MAX) {
        ui_toast("Names are 16 characters at most");
        return;
    }
    strcat(s_name, s);
    if (s_shift && !s_digits) {
        s_shift = false; /* one capital, then back to lower case */
        set_map();
    }
    show_text();
}

static void key_event(lv_event_t *e)
{
    lv_obj_t *m = lv_event_get_target(e);
    uint32_t id = lv_buttonmatrix_get_selected_button(m);
    if (id == LV_BUTTONMATRIX_BUTTON_NONE) {
        return;
    }
    if (id == KEY_SHIFT) {
        s_shift = !s_shift;
        if (s_digits) {
            s_digits = false;
        }
        set_map();
    } else if (id == KEY_BKSP) {
        size_t n = strlen(s_name);
        if (n) {
            s_name[n - 1] = 0;
        }
        show_text();
    } else {
        type(lv_buttonmatrix_get_button_text(m, id));
    }
}

static void space_event(lv_event_t *e)
{
    (void)e;
    if (s_name[0] && s_name[strlen(s_name) - 1] != ' ') {
        type(" ");
        if (!s_digits) {
            s_shift = true; /* a capital to start the next word */
            set_map();
        }
    }
}

static void layer_event(lv_event_t *e)
{
    (void)e;
    s_digits = !s_digits;
    set_map();
}

static void reset_event(lv_event_t *e)
{
    (void)e;
    snprintf(s_name, sizeof s_name, "%s", s_default);
    show_text();
}

static void go_back(void)
{
    if (s_back == UI_SCREEN_ZONES) {
        ui_show_zones();
    } else {
        ui_show_zone(ui_cur_addr);
    }
}

static void cancel_event(lv_event_t *e)
{
    (void)e;
    go_back();
}

static void save_event(lv_event_t *e)
{
    (void)e;
    char clean[ZONE_NAME_MAX + 1];
    if (!zone_clean_name(s_name, clean)) {
        ui_toast("Type a name first, or use the default");
        return;
    }
    zone_err_t err = zone_store_rename(ui_cur_addr, clean);
    if (err != ZONE_OK) {
        ui_report(err, "Rename");
        return;
    }
    go_back();
}

static void blink(lv_timer_t *t)
{
    (void)t;
    if (lv_obj_has_flag(s_cursor, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_remove_flag(s_cursor, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_cursor, LV_OBJ_FLAG_HIDDEN);
    }
}

void rename_open(uint8_t addr, ui_screen_t back)
{
    zone_view_t v;
    if (!ui_zone(addr, &v)) {
        return;
    }
    ui_cur_addr = addr;
    s_back = back;
    snprintf(s_name, sizeof s_name, "%s", v.cfg.name);
    zone_default_name(addr, v.cfg.model, s_default, sizeof s_default);
    lv_label_set_text_fmt(s_sub, "%s on address %u", v.cfg.name, addr);
    lv_label_set_text(ui_btn_label(s_reset), s_default);
    lv_label_set_text_fmt(s_hint, "Up to %u characters. %s puts back the default name.", ZONE_NAME_MAX, s_default);
    s_digits = false;
    s_shift = s_name[0] == 0;
    set_map();
    show_text();
}

static void rename_refresh(uint32_t dirty)
{
    (void)dirty;
}

static lv_obj_t *rename_create(void)
{
    s_scr = ui_screen_new();
    ui_hdr_t h = ui_header(s_scr, "Rename zone", cancel_event);
    s_sub = ui_label(h.right, F_LABEL, C_MUTED, "");

    lv_obj_t *body = ui_body(s_scr);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(body, 8, 0);

    lv_obj_t *field = ui_panel(body);
    lv_obj_set_size(field, LV_PCT(100), 72);
    lv_obj_set_style_bg_color(field, C_HEAT_BG, 0);
    lv_obj_set_style_border_color(field, C_HEAT_BORDER, 0);
    lv_obj_set_style_pad_ver(field, 0, 0);
    lv_obj_set_style_pad_hor(field, 16, 0);
    s_text = ui_label(field, &hm_bc600_44, C_TEXT, "");
    lv_obj_align(s_text, LV_ALIGN_LEFT_MID, 0, 0);
    s_cursor = ui_box(field);
    lv_obj_set_size(s_cursor, 3, 40);
    lv_obj_set_style_bg_color(s_cursor, C_COPPER, 0);
    lv_obj_set_style_bg_opa(s_cursor, LV_OPA_COVER, 0);
    s_count = ui_label(field, &hm_bc600_30, C_COPPER, "");
    lv_obj_align(s_count, LV_ALIGN_RIGHT_MID, 0, 0);

    s_hint = ui_label(body, F_BODY, C_MUTED, "");

    s_keys = lv_buttonmatrix_create(body);
    lv_obj_set_size(s_keys, LV_PCT(100), 3 * TOUCH_MIN + 2 * 8);
    lv_obj_set_style_bg_opa(s_keys, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_keys, 0, 0);
    lv_obj_set_style_pad_all(s_keys, 0, 0);
    lv_obj_set_style_pad_gap(s_keys, 8, 0);
    lv_obj_set_style_radius(s_keys, 0, 0);
    lv_obj_set_style_bg_color(s_keys, C_TILE, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(s_keys, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_border_color(s_keys, C_BORDER, LV_PART_ITEMS);
    lv_obj_set_style_border_width(s_keys, 1, LV_PART_ITEMS);
    lv_obj_set_style_radius(s_keys, RADIUS, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(s_keys, 0, LV_PART_ITEMS);
    lv_obj_set_style_text_font(s_keys, F_TITLE, LV_PART_ITEMS);
    lv_obj_set_style_text_color(s_keys, C_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(s_keys, C_PRESSED, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(s_keys, C_HEAT_BG, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_color(s_keys, C_COPPER, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(s_keys, C_COPPER, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_add_event_cb(s_keys, key_event, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *row = ui_row(body, 8);
    s_layer_btn = ui_btn(row, "123", layer_event, NULL);
    lv_obj_set_width(s_layer_btn, 88);
    s_reset = ui_btn(row, "", reset_event, NULL);
    lv_obj_set_width(s_reset, 120);
    lv_obj_t *sp = ui_btn(row, "space", space_event, NULL);
    lv_obj_set_flex_grow(sp, 1);
    lv_obj_set_style_text_color(sp, C_MUTED, 0);
    lv_obj_t *c = ui_btn(row, "Cancel", cancel_event, NULL);
    lv_obj_set_width(c, 110);
    lv_obj_t *s = ui_btn(row, "Save", save_event, NULL);
    lv_obj_set_width(s, 110);
    ui_btn_kind(s, UI_BTN_SOLID);

    s_blink = lv_timer_create(blink, 530, NULL);
    set_map();
    return s_scr;
}

const ui_screen_ops_t ui_rename_ops = { rename_create, rename_refresh, NULL };
