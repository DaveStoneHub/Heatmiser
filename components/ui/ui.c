/* Screens, shared widgets, the refresh timer, blanking and the soak pattern. */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "ui_internal.h"

lv_font_t ui_font20i;

static lv_obj_t *s_dash, *s_zone, *s_setup;
static ui_screen_t s_screen = UI_SCREEN_DASH;

/* ---- formatting ------------------------------------------------------------- */

void ui_fmt_temp(char *buf, size_t cap, int16_t t10)
{
    if (t10 == ZONE_T_ABSENT) {
        snprintf(buf, cap, "--");
        return;
    }
    char t[12];
    zone_fmt_t10(t10, t, sizeof t);
    snprintf(buf, cap, "%s" DEG, t);
}

void ui_fmt_age(char *buf, size_t cap, uint32_t s)
{
    if (s < 60) {
        snprintf(buf, cap, "%u s ago", (unsigned)s);
    } else if (s < 3600) {
        snprintf(buf, cap, "%u min ago", (unsigned)(s / 60));
    } else if (s < 172800) {
        snprintf(buf, cap, "%u h ago", (unsigned)(s / 3600));
    } else {
        snprintf(buf, cap, "%u days ago", (unsigned)(s / 86400));
    }
}

void ui_clock_text(char *buf, size_t cap)
{
    ui_sys_info_t si;
    ui_platform_sys_info(&si);
    if (!si.time_set) {
        snprintf(buf, cap, "--:--");
        return;
    }
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    strftime(buf, cap, "%a %H:%M", &tm);
}

bool ui_zone(uint8_t addr, zone_view_t *v)
{
    return zone_store_get(addr, v);
}

void ui_report(zone_err_t e, const char *what)
{
    if (e == ZONE_OK) {
        return;
    }
    char msg[96];
    snprintf(msg, sizeof msg, "%s: %s", what, zone_err_str(e));
    ui_toast(msg);
}

/* ---- shared widgets ------------------------------------------------------------ */

void ui_style_screen(lv_obj_t *scr)
{
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(scr, C_TEXT, 0);
    lv_obj_set_style_text_font(scr, &lv_font_montserrat_20, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t *ui_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb, void *user)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_height(b, TOUCH_MIN);
    lv_obj_set_style_min_width(b, 120, 0);
    lv_obj_set_style_radius(b, 12, 0);
    lv_obj_set_style_bg_color(b, C_PANEL_HI, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x34404C), LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_pad_hor(b, 18, 0);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_center(l);
    if (cb) {
        lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    }
    return b;
}

lv_obj_t *ui_header(lv_obj_t *scr, const char *title, bool back, lv_event_cb_t back_cb)
{
    lv_obj_t *h = lv_obj_create(scr);
    lv_obj_remove_style_all(h);
    lv_obj_set_size(h, LV_PCT(100), HEADER_H);
    lv_obj_set_style_bg_color(h, C_PANEL_DIM, 0);
    lv_obj_set_style_bg_opa(h, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(h, 12, 0);
    lv_obj_remove_flag(h, LV_OBJ_FLAG_SCROLLABLE);
    if (back) {
        lv_obj_t *b = lv_button_create(h);
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, 150, HEADER_H);
        lv_obj_align(b, LV_ALIGN_LEFT_MID, -12, 0);
        lv_obj_set_style_pad_left(b, 16, 0);
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, LV_SYMBOL_LEFT "  Back");
        lv_obj_set_style_text_color(l, C_MUTED, 0);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_add_event_cb(b, back_cb, LV_EVENT_CLICKED, NULL);
    }
    lv_obj_t *t = lv_label_create(h);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_20, 0);
    lv_obj_align(t, back ? LV_ALIGN_CENTER : LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_user_data(h, t);
    return h;
}

/* ---- toast ---------------------------------------------------------------------- */

static lv_obj_t *s_toast;
static lv_timer_t *s_toast_timer;

static void toast_hide(lv_timer_t *t)
{
    (void)t;
    if (s_toast) {
        lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
    }
    lv_timer_pause(s_toast_timer);
}

void ui_toast(const char *msg)
{
    if (!s_toast) {
        s_toast = lv_label_create(lv_layer_top());
        lv_obj_set_style_bg_color(s_toast, lv_color_hex(0x2E3946), 0);
        lv_obj_set_style_bg_opa(s_toast, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(s_toast, 12, 0);
        lv_obj_set_style_pad_hor(s_toast, 20, 0);
        lv_obj_set_style_pad_ver(s_toast, 12, 0);
        lv_obj_set_style_text_color(s_toast, C_TEXT, 0);
        lv_obj_set_style_text_font(s_toast, &lv_font_montserrat_20, 0);
        lv_obj_set_style_max_width(s_toast, 720, 0);
        lv_label_set_long_mode(s_toast, LV_LABEL_LONG_WRAP);
        s_toast_timer = lv_timer_create(toast_hide, 4500, NULL);
    }
    lv_label_set_text(s_toast, msg);
    lv_obj_remove_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(s_toast, LV_ALIGN_BOTTOM_MID, 0, -16);
    lv_obj_move_foreground(s_toast);
    lv_timer_reset(s_toast_timer);
    lv_timer_resume(s_toast_timer);
}

/* ---- modal helpers ------------------------------------------------------------------ */

static lv_obj_t *backdrop(void)
{
    lv_obj_t *bd = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(bd);
    lv_obj_set_size(bd, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(bd, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(bd, LV_OPA_70, 0);
    lv_obj_add_flag(bd, LV_OBJ_FLAG_CLICKABLE);
    return bd;
}

/* Rename: a text field and the keyboard. */
static uint8_t s_rename_addr;

static void rename_event(lv_event_t *e)
{
    lv_obj_t *kb = lv_event_get_target(e);
    lv_obj_t *bd = lv_event_get_user_data(e);
    if (lv_event_get_code(e) == LV_EVENT_READY) {
        lv_obj_t *ta = lv_keyboard_get_textarea(kb);
        ui_report(zone_store_rename(s_rename_addr, lv_textarea_get_text(ta)), "Rename");
    }
    lv_obj_delete_async(bd);
}

void ui_rename_dialog(uint8_t addr)
{
    zone_view_t v;
    if (!ui_zone(addr, &v)) {
        return;
    }
    s_rename_addr = addr;
    lv_obj_t *bd = backdrop();
    lv_obj_t *box = lv_obj_create(bd);
    lv_obj_set_size(box, 560, 128);
    lv_obj_align(box, LV_ALIGN_TOP_MID, 0, 16);
    lv_obj_set_style_bg_color(box, C_PANEL, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *cap = lv_label_create(box);
    lv_label_set_text_fmt(cap, "Name for stat %u", addr);
    lv_obj_set_style_text_color(cap, C_MUTED, 0);
    lv_obj_set_style_text_font(cap, &lv_font_montserrat_16, 0);
    lv_obj_align(cap, LV_ALIGN_TOP_LEFT, 0, -4);
    lv_obj_t *ta = lv_textarea_create(box);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta, ZONE_NAME_MAX);
    lv_textarea_set_text(ta, v.cfg.name);
    lv_obj_set_width(ta, LV_PCT(100));
    lv_obj_align(ta, LV_ALIGN_BOTTOM_MID, 0, 4);
    lv_obj_set_style_text_font(ta, &lv_font_montserrat_28, 0);
    lv_obj_t *kb = lv_keyboard_create(bd);
    lv_obj_set_size(kb, LV_PCT(100), 320);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_font(kb, &lv_font_montserrat_20, 0);
    lv_keyboard_set_textarea(kb, ta);
    lv_obj_add_event_cb(kb, rename_event, LV_EVENT_READY, bd);
    lv_obj_add_event_cb(kb, rename_event, LV_EVENT_CANCEL, bd);
}

/* Confirm: a message box with Cancel and one action. */
typedef struct {
    void (*on_ok)(void *);
    void *arg;
    lv_obj_t *bd;
} confirm_t;
static confirm_t s_confirm;

static void confirm_event(lv_event_t *e)
{
    bool ok = (uintptr_t)lv_event_get_user_data(e) == 1;
    lv_obj_delete_async(s_confirm.bd);
    if (ok && s_confirm.on_ok) {
        s_confirm.on_ok(s_confirm.arg);
    }
}

void ui_confirm(const char *title, const char *text, const char *ok, void (*on_ok)(void *), void *arg)
{
    s_confirm.on_ok = on_ok;
    s_confirm.arg = arg;
    s_confirm.bd = backdrop();
    lv_obj_t *box = lv_obj_create(s_confirm.bd);
    lv_obj_set_size(box, 520, LV_SIZE_CONTENT);
    lv_obj_center(box);
    lv_obj_set_style_bg_color(box, C_PANEL, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_pad_all(box, 24, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(box, 16, 0);
    lv_obj_t *t = lv_label_create(box);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_28, 0);
    lv_obj_t *m = lv_label_create(box);
    lv_label_set_text(m, text);
    lv_label_set_long_mode(m, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(m, LV_PCT(100));
    lv_obj_set_style_text_color(m, C_MUTED, 0);
    lv_obj_t *row = lv_obj_create(box);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 12, 0);
    ui_button(row, "Cancel", confirm_event, (void *)0);
    lv_obj_t *b = ui_button(row, ok, confirm_event, (void *)1);
    lv_obj_set_style_bg_color(b, C_BAD, 0);
}

/* ---- setpoint dial ------------------------------------------------------------------- */

static struct {
    bool active;
    uint8_t addr;
    int value;
    lv_timer_t *timer;
} s_dial;

static void dial_send(lv_timer_t *t)
{
    (void)t;
    ui_dial_flush();
}

void ui_dial_flush(void)
{
    if (s_dial.timer) {
        lv_timer_pause(s_dial.timer);
    }
    if (!s_dial.active) {
        return;
    }
    s_dial.active = false;
    zone_view_t v;
    if (ui_zone(s_dial.addr, &v) && zone_view_shown_set(&v) == s_dial.value) {
        return; /* back where it was */
    }
    ui_report(zone_store_set_temp(s_dial.addr, s_dial.value), "Setpoint");
    dash_refresh(1u << (s_dial.addr - 1));
    zone_scr_refresh(1u << (s_dial.addr - 1));
}

void ui_dial_set(uint8_t addr, int c)
{
    if (s_dial.active && s_dial.addr != addr) {
        ui_dial_flush();
    }
    if (c < HM_SET_TEMP_MIN) {
        c = HM_SET_TEMP_MIN;
    }
    if (c > HM_SET_TEMP_MAX) {
        c = HM_SET_TEMP_MAX;
    }
    s_dial.active = true;
    s_dial.addr = addr;
    s_dial.value = c;
    if (!s_dial.timer) {
        s_dial.timer = lv_timer_create(dial_send, DIAL_SETTLE_MS, NULL);
    }
    lv_timer_reset(s_dial.timer);
    lv_timer_resume(s_dial.timer);
}

void ui_dial_nudge(uint8_t addr, int delta)
{
    int cur;
    if (!ui_dial_value(addr, &cur)) {
        zone_view_t v;
        if (!ui_zone(addr, &v) || !v.have) {
            return;
        }
        cur = zone_view_shown_set(&v);
    }
    ui_dial_set(addr, cur + delta);
}

bool ui_dial_value(uint8_t addr, int *c)
{
    if (s_dial.active && s_dial.addr == addr) {
        *c = s_dial.value;
        return true;
    }
    return false;
}

/* ---- blanking ------------------------------------------------------------------------ */

static uint32_t s_blank_s = 600;
static bool s_blanked;
static lv_obj_t *s_wake;

static void wake_event(lv_event_t *e)
{
    (void)e;
    ui_platform_backlight(true);
    s_blanked = false;
    lv_obj_delete_async(s_wake);
    s_wake = NULL;
}

static void blank_check(void)
{
    if (s_blanked || s_blank_s == 0 || ui_soak_on()) {
        return;
    }
    if (lv_display_get_inactive_time(NULL) < s_blank_s * 1000u) {
        return;
    }
    ui_dial_flush();
    /* Something to catch the waking touch, so it does not press a button. */
    s_wake = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_wake);
    lv_obj_set_size(s_wake, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(s_wake, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_wake, wake_event, LV_EVENT_PRESSED, NULL);
    s_blanked = true;
    ui_platform_backlight(false);
    if (s_screen != UI_SCREEN_DASH) {
        ui_show_dash(); /* wake up to the overview */
    }
}

void ui_set_blank_timeout(uint32_t s)
{
    s_blank_s = s;
}

uint32_t ui_blank_timeout(void)
{
    return s_blank_s;
}

/* ---- soak pattern ---------------------------------------------------------------------- */

static struct {
    lv_obj_t *root, *status, *bar;
    lv_anim_t anim;
    uint32_t started_s;
} s_soak;

static lv_obj_t *soak_rect(lv_obj_t *p, int x, int y, int w, int h, lv_color_t c)
{
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}

static void soak_bar_anim(void *obj, int32_t v)
{
    lv_obj_set_x(obj, v);
}

static void soak_drift_event(lv_event_t *e)
{
    (void)e;
    ui_platform_drift_seen();
    ui_toast("Drift noted in the log; the panel DMA was restarted");
}

static void soak_build(void)
{
    const int W = 800, H = 480;
    lv_obj_t *r = lv_obj_create(lv_layer_sys());
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, W, H);
    lv_obj_remove_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(r, LV_OBJ_FLAG_FLOATING);
    s_soak.root = r;
    /* White 1 px frame on the outermost pixels, red 1 px frame inside it. A
     * sideways shift moves the white line off one edge and wraps it in at
     * the other. */
    soak_rect(r, 0, 0, W, 1, lv_color_white());
    soak_rect(r, 0, H - 1, W, 1, lv_color_white());
    soak_rect(r, 0, 0, 1, H, lv_color_white());
    soak_rect(r, W - 1, 0, 1, H, lv_color_white());
    soak_rect(r, 2, 2, W - 4, 1, C_BAD);
    soak_rect(r, 2, H - 3, W - 4, 1, C_BAD);
    soak_rect(r, 2, 2, 1, H - 4, C_BAD);
    soak_rect(r, W - 3, 2, 1, H - 4, C_BAD);
    /* Corner marks: green top left, blue top right, yellow bottom left,
     * magenta bottom right. Colour order also shows a byte-order fault. */
    soak_rect(r, 4, 4, 12, 12, lv_color_hex(0x00FF00));
    soak_rect(r, W - 16, 4, 12, 12, lv_color_hex(0x0000FF));
    soak_rect(r, 4, H - 16, 12, 12, lv_color_hex(0xFFFF00));
    soak_rect(r, W - 16, H - 16, 12, 12, lv_color_hex(0xFF00FF));
    /* Moving bar keeps LVGL drawing and the frame buffers busy. */
    s_soak.bar = soak_rect(r, 0, H - 8, 60, 3, C_HEAT);
    lv_anim_init(&s_soak.anim);
    lv_anim_set_var(&s_soak.anim, s_soak.bar);
    lv_anim_set_exec_cb(&s_soak.anim, soak_bar_anim);
    lv_anim_set_values(&s_soak.anim, 20, W - 80);
    lv_anim_set_duration(&s_soak.anim, 2000);
    lv_anim_set_playback_duration(&s_soak.anim, 2000);
    lv_anim_set_repeat_count(&s_soak.anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&s_soak.anim);

    s_soak.status = lv_label_create(r);
    lv_obj_set_style_text_font(s_soak.status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_soak.status, lv_color_white(), 0);
    lv_obj_set_style_bg_color(s_soak.status, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_soak.status, LV_OPA_60, 0);
    lv_obj_set_style_pad_hor(s_soak.status, 6, 0);
    lv_obj_align(s_soak.status, LV_ALIGN_BOTTOM_LEFT, 22, -12);

    lv_obj_t *b = lv_button_create(r);
    lv_obj_set_size(b, 200, 48);
    lv_obj_align(b, LV_ALIGN_BOTTOM_RIGHT, -22, -14);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x5A2626), 0);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, "Picture shifted");
    lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, soak_drift_event, LV_EVENT_CLICKED, NULL);
}

static void soak_tick(void)
{
    if (!s_soak.root || lv_obj_has_flag(s_soak.root, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    ui_sys_info_t si;
    ui_platform_sys_info(&si);
    uint32_t up = si.uptime_s - s_soak.started_s;
    lv_label_set_text_fmt(s_soak.status, "Drift test %uh %02um  |  Wi-Fi %s %d dBm  |  frames %u  |  RAM %uk (min %uk)",
                          (unsigned)(up / 3600), (unsigned)(up / 60 % 60),
                          si.wifi_up ? "up" : "DOWN", si.rssi, (unsigned)si.frames,
                          (unsigned)(si.heap_int_free / 1024), (unsigned)(si.heap_int_min / 1024));
}

void ui_set_soak(bool on)
{
    if (on && !s_soak.root) {
        soak_build();
    }
    if (s_soak.root) {
        if (on) {
            ui_sys_info_t si;
            ui_platform_sys_info(&si);
            s_soak.started_s = si.uptime_s;
            lv_obj_remove_flag(s_soak.root, LV_OBJ_FLAG_HIDDEN);
            soak_tick();
        } else {
            lv_obj_add_flag(s_soak.root, LV_OBJ_FLAG_HIDDEN);
        }
    }
    ui_platform_soak(on);
}

bool ui_soak_on(void)
{
    return s_soak.root && !lv_obj_has_flag(s_soak.root, LV_OBJ_FLAG_HIDDEN);
}

/* ---- screens and timers ------------------------------------------------------------------ */

ui_screen_t ui_screen(void)
{
    return s_screen;
}

void ui_show_dash(void)
{
    ui_dial_flush();
    s_screen = UI_SCREEN_DASH;
    dash_refresh(0xFFFFFFFFu);
    lv_screen_load(s_dash);
}

void ui_show_zone(uint8_t addr)
{
    zone_view_t v;
    if (!ui_zone(addr, &v)) {
        return;
    }
    ui_dial_flush();
    s_screen = UI_SCREEN_ZONE;
    zone_scr_open(addr);
    lv_screen_load(s_zone);
}

void ui_show_setup(void)
{
    ui_dial_flush();
    s_screen = UI_SCREEN_SETUP;
    setup_refresh(0xFFFFFFFFu);
    lv_screen_load(s_setup);
}

static void refresh_timer(lv_timer_t *t)
{
    (void)t;
    uint32_t dirty = zone_store_take_dirty();
    if (dirty) {
        dash_refresh(dirty);
        if (s_screen == UI_SCREEN_ZONE) {
            zone_scr_refresh(dirty);
        }
        if (s_screen == UI_SCREEN_SETUP) {
            setup_refresh(dirty);
        }
        if (s_screen == UI_SCREEN_ZONE && (dirty & ZONE_DIRTY_LIST)) {
            zone_view_t v;
            if (!ui_zone(zone_scr_addr(), &v)) {
                ui_show_dash(); /* removed under us */
            }
        }
    }
    char msg[96];
    if (zone_store_pop_notice(msg, sizeof msg)) {
        ui_toast(msg);
    }
}

static void second_timer(lv_timer_t *t)
{
    (void)t;
    dash_tick();
    if (s_screen == UI_SCREEN_ZONE) {
        zone_scr_tick();
    }
    if (s_screen == UI_SCREEN_SETUP) {
        setup_tick();
    }
    soak_tick();
    blank_check();
}

void ui_init(void)
{
    lv_display_t *d = lv_display_get_default();
    lv_theme_t *th = lv_theme_default_init(d, lv_color_hex(0xFF8A3D), lv_color_hex(0x5FB4FF), true,
                                           &lv_font_montserrat_20);
    lv_display_set_theme(d, th);
    ui_font20i = lv_font_montserrat_20;
    ui_font20i.fallback = &hm_icons_20;
    s_dash = dash_create();
    s_zone = zone_scr_create();
    s_setup = setup_create();
    lv_timer_create(refresh_timer, 200, NULL);
    lv_timer_create(second_timer, 1000, NULL);
    ui_show_dash();
}
