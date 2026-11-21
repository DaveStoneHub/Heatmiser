/* Matter: the pairing QR code and manual code, whether a controller can pair
 * now, and the controllers (fabrics) the bridge has joined, with a button to
 * let another one pair and one to forget each. */
#include <stdio.h>
#include <string.h>

#include "ui_internal.h"

#define QR_PX 196

static lv_obj_t *s_scr, *s_qr, *s_qr_box, *s_qr_note, *s_code, *s_state, *s_count;
static lv_obj_t *s_list, *s_rows[UI_MATTER_FABRICS], *s_row_name[UI_MATTER_FABRICS], *s_row_btn[UI_MATTER_FABRICS];
static lv_obj_t *s_none, *s_open_btn;
static char s_shown_qr[48];
static uint8_t s_remove_index;

static const char *vendor_name(uint16_t v)
{
    switch (v) {
    case 0x115F: return "Aqara";
    case 0x1349: return "Apple Home";
    case 0x1384: return "Apple (iPhone keychain)";
    case 0x6006: return "Google Home";
    case 0x1217: return "Amazon Alexa";
    case 0x110A: return "SmartThings";
    case 0x130D: return "Home Assistant";
    case 0x134B: return "Home Assistant";
    case 0xFFF1: case 0xFFF2: case 0xFFF3: case 0xFFF4: return "Test controller";
    default: return NULL;
    }
}

/* "34970112332" -> "3497-011-2332" */
static void fmt_manual(const char *in, char *out, size_t cap)
{
    if (strlen(in) == 11) {
        snprintf(out, cap, "%.4s-%.3s-%.4s", in, in + 4, in + 7);
    } else {
        snprintf(out, cap, "%s", in);
    }
}

static void back_event(lv_event_t *e)
{
    (void)e;
    ui_show_settings();
}

static void open_event(lv_event_t *e)
{
    (void)e;
    ui_toast(ui_platform_matter_open_window() ? "Pairing open for 15 minutes. Scan the code in the controller's app."
                                              : "Could not open pairing. Is the bridge on Wi-Fi?");
}

static void do_remove(void *arg)
{
    (void)arg;
    ui_toast(ui_platform_matter_remove_fabric(s_remove_index) ? "Controller removed" : "Could not remove it");
}

static void remove_event(lv_event_t *e)
{
    s_remove_index = (uint8_t)(intptr_t)lv_event_get_user_data(e);
    ui_confirm("Remove this controller?",
               "Its rooms and automations for these thermostats go. "
               "Pair it again to bring the thermostats back.",
               "Remove", do_remove, NULL);
}

static void matter_tick(void)
{
    ui_matter_info_t mi;
    ui_platform_matter_info(&mi);

    bool show_qr = mi.running && mi.window_open && mi.qr[0];
    if (show_qr && strcmp(s_shown_qr, mi.qr)) {
        lv_qrcode_update(s_qr, mi.qr, (uint32_t)strlen(mi.qr));
        snprintf(s_shown_qr, sizeof s_shown_qr, "%s", mi.qr);
    }
    if (show_qr) {
        lv_obj_remove_flag(s_qr, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_qr_note, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_qr, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_qr_note, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_qr_note, !mi.running ? "Matter is starting" ELLIPSIS
                                                 : "Pairing is closed.\nOpen it to add a controller.");
    }

    char code[24];
    fmt_manual(mi.manual, code, sizeof code);
    lv_label_set_text(s_code, show_qr ? code : "");

    if (!mi.running) {
        lv_label_set_text(s_state, "Matter is not running");
    } else if (mi.window_open) {
        lv_label_set_text_fmt(s_state, "Ready to pair, %u:%02u left", (unsigned)(mi.window_left_s / 60),
                              (unsigned)(mi.window_left_s % 60));
    } else {
        lv_label_set_text(s_state, mi.fabric_count ? "Paired" : "Not paired");
    }
    lv_obj_set_style_text_color(s_state, mi.window_open ? C_COPPER : C_MUTED, 0);
    lv_label_set_text_fmt(s_count, "%u thermostat%s shared", (unsigned)mi.endpoints, mi.endpoints == 1 ? "" : "s");

    for (int i = 0; i < UI_MATTER_FABRICS; i++) {
        if (i < mi.fabric_count) {
            const ui_matter_fabric_t *f = &mi.fabrics[i];
            const char *vn = vendor_name(f->vendor);
            char name[64];
            if (f->label[0]) {
                snprintf(name, sizeof name, "%s", f->label);
            } else if (vn) {
                snprintf(name, sizeof name, "%s", vn);
            } else {
                snprintf(name, sizeof name, "Controller (vendor 0x%04X)", f->vendor);
            }
            lv_label_set_text(s_row_name[i], name);
            lv_obj_remove_event_cb(s_row_btn[i], remove_event);
            lv_obj_add_event_cb(s_row_btn[i], remove_event, LV_EVENT_CLICKED, (void *)(intptr_t)f->index);
            lv_obj_remove_flag(s_rows[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_rows[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (mi.fabric_count) {
        lv_obj_add_flag(s_none, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(s_none, LV_OBJ_FLAG_HIDDEN);
    }
    ui_enable(s_open_btn, mi.running && !mi.window_open);
}

static void matter_refresh(uint32_t dirty)
{
    (void)dirty;
}

static lv_obj_t *matter_create(void)
{
    s_scr = ui_screen_new();
    ui_hdr_t h = ui_header(s_scr, "Matter", back_event);
    s_count = ui_label(h.right, F_LABEL, C_MUTED, "");

    lv_obj_t *body = ui_body(s_scr);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(body, GAP, 0);

    /* Left: the code. */
    lv_obj_t *left = ui_panel(body);
    lv_obj_set_size(left, 300, LV_PCT(100));
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(left, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(left, 8, 0);

    s_qr_box = lv_obj_create(left);
    lv_obj_remove_style_all(s_qr_box);
    lv_obj_set_size(s_qr_box, QR_PX + 24, QR_PX + 24);
    lv_obj_set_style_bg_color(s_qr_box, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(s_qr_box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_qr_box, RADIUS, 0);
    lv_obj_remove_flag(s_qr_box, LV_OBJ_FLAG_SCROLLABLE);
    s_qr = lv_qrcode_create(s_qr_box);
    lv_qrcode_set_size(s_qr, QR_PX);
    lv_qrcode_set_dark_color(s_qr, lv_color_hex(0x000000));
    lv_qrcode_set_light_color(s_qr, lv_color_hex(0xFFFFFF));
    lv_obj_center(s_qr);
    s_qr_note = ui_label(s_qr_box, F_LABEL, lv_color_hex(0x1A1917), "");
    lv_obj_set_width(s_qr_note, QR_PX);
    lv_obj_set_style_text_align(s_qr_note, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_qr_note, LV_LABEL_LONG_WRAP);
    lv_obj_center(s_qr_note);

    s_code = ui_label(left, &hm_bc600_30, C_TEXT, "");
    s_state = ui_label(left, F_LABEL, C_MUTED, "");

    /* Right: controllers. */
    lv_obj_t *right = ui_col(body, GAP);
    lv_obj_set_height(right, LV_PCT(100));
    lv_obj_set_flex_grow(right, 1);

    ui_caption(right, "CONTROLLERS");
    s_list = ui_panel(right);
    lv_obj_set_width(s_list, LV_PCT(100));
    lv_obj_set_flex_grow(s_list, 1);
    lv_obj_set_style_pad_hor(s_list, 16, 0);
    lv_obj_set_style_pad_ver(s_list, 4, 0);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    s_none = ui_label(s_list, F_BODY, C_MUTED,
                      "No controller yet. In the Aqara Home app (or Apple Home), add a Matter "
                      "device and scan the code on the left.");
    lv_obj_set_width(s_none, LV_PCT(100));
    lv_label_set_long_mode(s_none, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_pad_top(s_none, 12, 0);
    for (int i = 0; i < UI_MATTER_FABRICS; i++) {
        lv_obj_t *r = ui_row(s_list, GAP);
        lv_obj_set_size(r, LV_PCT(100), 72);
        lv_obj_set_style_border_side(r, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_color(r, C_BORDER, 0);
        lv_obj_set_style_border_width(r, 1, 0);
        lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_t *icon = ui_label(r, &hm_icon_22, C_COPPER, HM_ICON_SHARE);
        (void)icon;
        s_row_name[i] = ui_label(r, F_LABEL, C_TEXT, "");
        lv_obj_set_flex_grow(s_row_name[i], 1);
        ui_label_dots(s_row_name[i]);
        s_row_btn[i] = ui_btn(r, HM_ICON_TRASH "  Remove", NULL, NULL);
        lv_obj_set_width(s_row_btn[i], 150);
        s_rows[i] = r;
        lv_obj_add_flag(r, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_t *row = ui_row(right, GAP);
    lv_obj_set_width(row, LV_PCT(100));
    s_open_btn = ui_btn(row, HM_ICON_PLUS "  Pair another controller", open_event, NULL);
    lv_obj_set_flex_grow(s_open_btn, 1);
    ui_btn_kind(s_open_btn, UI_BTN_SOLID);
    lv_obj_t *note = ui_label(right, F_BODY, C_MUTED,
                              "Each zone shows up as a thermostat. Setpoint and Heat or Off work from the "
                              "controller; Off puts the stat in frost protection.");
    lv_obj_set_width(note, LV_PCT(100));
    lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
    return s_scr;
}

const ui_screen_ops_t ui_matter_ops = { matter_create, matter_refresh, matter_tick };
