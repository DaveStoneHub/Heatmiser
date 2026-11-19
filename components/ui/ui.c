/* Screens, shared widgets, popups, the refresh timers, blanking and the soak
 * pattern. */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "ui_internal.h"

static const ui_screen_ops_t *const s_ops[UI_SCREEN_COUNT] = {
    [UI_SCREEN_DASH] = &ui_dash_ops,         [UI_SCREEN_ZONE] = &ui_zone_ops,
    [UI_SCREEN_SCHEDULE] = &ui_sched_ops,    [UI_SCREEN_HOTWATER] = &ui_hw_ops,
    [UI_SCREEN_RENAME] = &ui_rename_ops,     [UI_SCREEN_SETTINGS] = &ui_settings_ops,
    [UI_SCREEN_ZONES] = &ui_zones_ops,       [UI_SCREEN_DIAG] = &ui_diag_ops,
    [UI_SCREEN_SYSTEM] = &ui_system_ops,     [UI_SCREEN_MATTER] = &ui_matter_ops,
};
static lv_obj_t *s_scr[UI_SCREEN_COUNT];
static ui_screen_t s_screen = UI_SCREEN_DASH;
uint8_t ui_cur_addr;

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

void ui_clock_text(char *buf, size_t cap, bool with_day)
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
    strftime(buf, cap, with_day ? "%a %H:%M" : "%H:%M", &tm);
}

bool ui_now(const zone_view_t *v, int *wday, int *minute)
{
    ui_sys_info_t si;
    ui_platform_sys_info(&si);
    if (si.time_set) {
        time_t now = time(NULL);
        struct tm tm;
        localtime_r(&now, &tm);
        *wday = tm.tm_wday == 0 ? 7 : tm.tm_wday;
        *minute = tm.tm_hour * 60 + tm.tm_min;
        return true;
    }
    if (v && v->have && v->clk_wday >= 1 && v->clk_wday <= 7) {
        *wday = v->clk_wday;
        *minute = v->clk_hour * 60 + v->clk_min;
        return true;
    }
    return false;
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

lv_obj_t *ui_box(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static lv_obj_t *flex(lv_obj_t *parent, lv_flex_flow_t flow, int gap)
{
    lv_obj_t *o = ui_box(parent);
    lv_obj_set_flex_flow(o, flow);
    lv_obj_set_style_pad_gap(o, gap, 0);
    return o;
}

lv_obj_t *ui_row(lv_obj_t *parent, int gap)
{
    lv_obj_t *o = flex(parent, LV_FLEX_FLOW_ROW, gap);
    lv_obj_set_size(o, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_align(o, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return o;
}

lv_obj_t *ui_col(lv_obj_t *parent, int gap)
{
    lv_obj_t *o = flex(parent, LV_FLEX_FLOW_COLUMN, gap);
    lv_obj_set_size(o, LV_PCT(100), LV_SIZE_CONTENT);
    return o;
}

lv_obj_t *ui_panel(lv_obj_t *parent)
{
    lv_obj_t *o = ui_box(parent);
    lv_obj_set_style_bg_color(o, C_TILE, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(o, C_BORDER, 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_radius(o, RADIUS, 0);
    lv_obj_set_style_pad_all(o, PAD, 0);
    return o;
}

lv_obj_t *ui_label(lv_obj_t *parent, const lv_font_t *f, lv_color_t c, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_label_set_text(l, text ? text : "");
    return l;
}

lv_obj_t *ui_caption(lv_obj_t *parent, const char *text)
{
    return ui_label(parent, F_CAP, C_MUTED, text);
}

void ui_label_marquee(lv_obj_t *l)
{
    /* Two seconds still at each end, then a slow slide; a name that fits
     * does not move. */
    static lv_anim_t tmpl;
    static bool init;
    if (!init) {
        lv_anim_init(&tmpl);
        lv_anim_set_repeat_count(&tmpl, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_reverse_delay(&tmpl, 2000);
        lv_anim_set_repeat_delay(&tmpl, 2000);
        tmpl.act_time = -2000;
        init = true;
    }
    lv_label_set_long_mode(l, LV_LABEL_LONG_SCROLL);
    lv_obj_set_style_anim(l, &tmpl, 0);
    lv_obj_set_style_anim_duration(l, lv_anim_speed_clamped(25, 600, 10000), 0);
}

void ui_marquee_text(lv_obj_t *l, const char *text)
{
    /* Setting the same text again would restart the slide from its pause. */
    if (strcmp(lv_label_get_text(l), text) != 0) {
        lv_label_set_text(l, text);
    }
}

void ui_label_dots(lv_obj_t *l)
{
    /* Dots only stop wrapping when the height is one line. */
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_height(l, lv_font_get_line_height(lv_obj_get_style_text_font(l, 0)));
}

lv_obj_t *ui_screen_new(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, C_GROUND, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(scr, C_TEXT, 0);
    lv_obj_set_style_text_font(scr, F_LABEL, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    return scr;
}

lv_obj_t *ui_btn_label(lv_obj_t *b)
{
    return lv_obj_get_child(b, 0);
}

void ui_btn_kind(lv_obj_t *b, ui_btn_kind_t k)
{
    lv_color_t bg = C_TILE, border = C_BORDER, text = C_TEXT;
    switch (k) {
    case UI_BTN_SOLID:  bg = C_COPPER; border = C_COPPER; text = C_ON_COPPER; break;
    case UI_BTN_TINT:   bg = C_HEAT_BG; border = C_COPPER; text = C_COPPER; break;
    case UI_BTN_DANGER: bg = C_FAULT_BG; border = C_FAULT; text = C_FAULT; break;
    default: break;
    }
    lv_obj_set_style_bg_color(b, bg, 0);
    lv_obj_set_style_border_color(b, border, 0);
    lv_obj_set_style_text_color(b, text, 0);
}

lv_obj_t *ui_btn(lv_obj_t *parent, const char *text, lv_event_cb_t cb, void *user)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_height(b, TOUCH_MIN);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_radius(b, RADIUS, 0);
    lv_obj_set_style_pad_hor(b, 16, 0);
    lv_obj_set_style_text_font(b, F_LABEL, 0);
    lv_obj_set_style_bg_color(b, C_PRESSED, LV_STATE_PRESSED);
    lv_obj_set_style_opa(b, LV_OPA_40, LV_STATE_DISABLED);
    ui_btn_kind(b, UI_BTN_PLAIN);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_center(l);
    if (cb) {
        lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    }
    return b;
}

lv_obj_t *ui_icon_btn(lv_obj_t *parent, const char *icon, lv_event_cb_t cb, void *user)
{
    lv_obj_t *b = ui_btn(parent, icon, cb, user);
    lv_obj_set_size(b, 64, 44);
    lv_obj_set_style_pad_hor(b, 0, 0);
    lv_obj_set_style_text_font(ui_btn_label(b), &hm_icon_22, 0);
    lv_obj_set_ext_click_area(b, 4);
    return b;
}

void ui_enable(lv_obj_t *o, bool on)
{
    if (on) {
        lv_obj_remove_state(o, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(o, LV_STATE_DISABLED);
    }
}

static void dashed_draw(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_current_target(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = lv_color_hex((uint32_t)(uintptr_t)lv_event_get_user_data(e));
    d.width = 1;
    d.dash_width = 5;
    d.dash_gap = 4;
    const lv_value_precise_t x1 = a.x1, y1 = a.y1, x2 = a.x2, y2 = a.y2;
    const lv_value_precise_t pts[4][4] = {
        { x1 + RADIUS, y1, x2 - RADIUS, y1 }, { x1 + RADIUS, y2, x2 - RADIUS, y2 },
        { x1, y1 + RADIUS, x1, y2 - RADIUS }, { x2, y1 + RADIUS, x2, y2 - RADIUS },
    };
    for (int i = 0; i < 4; i++) {
        d.p1.x = pts[i][0];
        d.p1.y = pts[i][1];
        d.p2.x = pts[i][2];
        d.p2.y = pts[i][3];
        lv_draw_line(layer, &d);
    }
}

void ui_dashed(lv_obj_t *o, bool on, lv_color_t c)
{
    lv_obj_remove_event_cb(o, dashed_draw);
    if (on) {
        lv_obj_set_style_border_width(o, 0, 0);
        lv_obj_add_event_cb(o, dashed_draw, LV_EVENT_DRAW_POST, (void *)(uintptr_t)lv_color_to_u32(c));
    } else {
        lv_obj_set_style_border_width(o, 1, 0);
    }
    lv_obj_invalidate(o);
}

ui_hdr_t ui_header(lv_obj_t *scr, const char *title, lv_event_cb_t back)
{
    ui_hdr_t h;
    h.bar = ui_box(scr);
    lv_obj_set_size(h.bar, SCR_W, HEADER_H);
    lv_obj_set_style_pad_hor(h.bar, PAD, 0);
    lv_obj_t *left = ui_box(h.bar);
    lv_obj_set_size(left, LV_SIZE_CONTENT, HEADER_H);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(left, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(left, PAD, 0);
    lv_obj_align(left, LV_ALIGN_LEFT_MID, 0, 0);
    if (back) {
        ui_icon_btn(left, HM_ICON_BACK, back, NULL);
    }
    h.title = ui_label(left, F_TITLE, C_TEXT, title);
    lv_obj_set_user_data(h.bar, left);
    h.right = ui_box(h.bar);
    lv_obj_set_size(h.right, LV_SIZE_CONTENT, HEADER_H);
    lv_obj_set_flex_flow(h.right, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(h.right, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(h.right, 14, 0);
    lv_obj_align(h.right, LV_ALIGN_RIGHT_MID, 0, 0);
    return h;
}

lv_obj_t *ui_body(lv_obj_t *scr)
{
    lv_obj_t *b = ui_box(scr);
    lv_obj_set_pos(b, PAD, HEADER_H);
    lv_obj_set_size(b, SCR_W - 2 * PAD, BODY_H);
    return b;
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
        lv_obj_set_style_bg_color(s_toast, C_TILE, 0);
        lv_obj_set_style_bg_opa(s_toast, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(s_toast, C_COPPER, 0);
        lv_obj_set_style_border_width(s_toast, 1, 0);
        lv_obj_set_style_radius(s_toast, RADIUS, 0);
        lv_obj_set_style_pad_hor(s_toast, 20, 0);
        lv_obj_set_style_pad_ver(s_toast, 14, 0);
        lv_obj_set_style_text_color(s_toast, C_TEXT, 0);
        lv_obj_set_style_text_font(s_toast, F_LABEL, 0);
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

/* ---- popups ---------------------------------------------------------------------- */

static lv_obj_t *s_popup;

static void popup_bd_event(lv_event_t *e)
{
    if (lv_event_get_target(e) == lv_event_get_current_target(e)) {
        ui_popup_close(); /* a tap outside the box */
    }
}

lv_obj_t *ui_popup(const char *title, int w, int h, bool closable)
{
    ui_popup_close();
    lv_obj_t *bd = ui_box(lv_layer_top());
    lv_obj_set_size(bd, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(bd, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(bd, LV_OPA_70, 0);
    lv_obj_add_flag(bd, LV_OBJ_FLAG_CLICKABLE);
    if (closable) {
        lv_obj_add_event_cb(bd, popup_bd_event, LV_EVENT_CLICKED, NULL);
    }
    s_popup = bd;
    lv_obj_t *box = ui_panel(bd);
    lv_obj_add_flag(box, LV_OBJ_FLAG_CLICKABLE); /* taps inside stay inside */
    lv_obj_set_size(box, w, h);
    lv_obj_center(box);
    lv_obj_set_style_pad_all(box, 16, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(box, GAP, 0);
    if (title) {
        ui_label(box, F_TITLE, C_TEXT, title);
    }
    return box;
}

void ui_popup_close(void)
{
    if (s_popup) {
        lv_obj_delete_async(s_popup);
        s_popup = NULL;
    }
}

bool ui_popup_open(void)
{
    return s_popup != NULL;
}

/* Confirm: a message with Cancel and one action. */
static struct {
    void (*on_ok)(void *);
    void *arg;
} s_confirm;

static void confirm_event(lv_event_t *e)
{
    bool ok = (uintptr_t)lv_event_get_user_data(e) == 1;
    ui_popup_close();
    if (ok && s_confirm.on_ok) {
        s_confirm.on_ok(s_confirm.arg);
    }
}

void ui_confirm(const char *title, const char *text, const char *ok, void (*on_ok)(void *), void *arg)
{
    s_confirm.on_ok = on_ok;
    s_confirm.arg = arg;
    lv_obj_t *box = ui_popup(title, 540, LV_SIZE_CONTENT, true);
    lv_obj_t *m = ui_label(box, F_BODY, C_MUTED, text);
    lv_label_set_long_mode(m, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(m, LV_PCT(100));
    lv_obj_t *row = ui_row(box, GAP);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(row, 6, 0);
    lv_obj_t *c = ui_btn(row, "Cancel", confirm_event, (void *)0);
    lv_obj_set_width(c, 140);
    lv_obj_t *b = ui_btn(row, ok, confirm_event, (void *)1);
    lv_obj_set_width(b, 160);
    ui_btn_kind(b, UI_BTN_DANGER);
}

/* Choose: a column of options. */
static void (*s_pick)(int);

static void choose_event(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    void (*pick)(int) = s_pick;
    ui_popup_close();
    if (pick) {
        pick(i);
    }
}

void ui_choose(const char *title, const char *const *opts, int n, int selected, void (*on_pick)(int))
{
    s_pick = on_pick;
    const int cols = n > 5 ? 2 : 1;
    const int rows = (n + cols - 1) / cols;
    lv_obj_t *box = ui_popup(title, cols == 2 ? 600 : 400, 16 * 2 + 30 + rows * (TOUCH_MIN + GAP), true);
    lv_obj_t *grid = ui_box(box);
    lv_obj_set_size(grid, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_gap(grid, GAP, 0);
    for (int i = 0; i < n; i++) {
        lv_obj_t *b = ui_btn(grid, opts[i], choose_event, (void *)(intptr_t)i);
        lv_obj_set_width(b, cols == 2 ? 279 : LV_PCT(100));
        if (i == selected) {
            ui_btn_kind(b, UI_BTN_TINT);
        }
    }
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
    ui_refresh(1u << (s_dial.addr - 1));
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

/* ---- identify: toggle the key lock so the padlock blinks on the stat -------------------- */

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
        s_ident.addr = 0;
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

void ui_identify(uint8_t addr)
{
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
    ui_popup_close();
    /* Something to catch the waking touch, so it does not press a button. */
    s_wake = ui_box(lv_layer_top());
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
    lv_obj_t *o = ui_box(p);
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
    const int W = SCR_W, H = SCR_H;
    lv_obj_t *r = ui_box(lv_layer_sys());
    lv_obj_set_size(r, W, H);
    lv_obj_add_flag(r, LV_OBJ_FLAG_FLOATING);
    s_soak.root = r;
    /* White 1 px frame on the outermost pixels, red 1 px frame inside it. A
     * sideways shift moves the white line off one edge and wraps it in at
     * the other. */
    soak_rect(r, 0, 0, W, 1, lv_color_white());
    soak_rect(r, 0, H - 1, W, 1, lv_color_white());
    soak_rect(r, 0, 0, 1, H, lv_color_white());
    soak_rect(r, W - 1, 0, 1, H, lv_color_white());
    soak_rect(r, 2, 2, W - 4, 1, lv_color_hex(0xFF0000));
    soak_rect(r, 2, H - 3, W - 4, 1, lv_color_hex(0xFF0000));
    soak_rect(r, 2, 2, 1, H - 4, lv_color_hex(0xFF0000));
    soak_rect(r, W - 3, 2, 1, H - 4, lv_color_hex(0xFF0000));
    /* Corner marks: green top left, blue top right, yellow bottom left,
     * magenta bottom right. Colour order also shows a byte-order fault. */
    soak_rect(r, 4, 4, 12, 12, lv_color_hex(0x00FF00));
    soak_rect(r, W - 16, 4, 12, 12, lv_color_hex(0x0000FF));
    soak_rect(r, 4, H - 16, 12, 12, lv_color_hex(0xFFFF00));
    soak_rect(r, W - 16, H - 16, 12, 12, lv_color_hex(0xFF00FF));
    /* Moving bar keeps LVGL drawing and the frame buffers busy. */
    s_soak.bar = soak_rect(r, 0, H - 8, 60, 3, C_COPPER);
    lv_anim_init(&s_soak.anim);
    lv_anim_set_var(&s_soak.anim, s_soak.bar);
    lv_anim_set_exec_cb(&s_soak.anim, soak_bar_anim);
    lv_anim_set_values(&s_soak.anim, 20, W - 80);
    lv_anim_set_duration(&s_soak.anim, 2000);
    lv_anim_set_playback_duration(&s_soak.anim, 2000);
    lv_anim_set_repeat_count(&s_soak.anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&s_soak.anim);

    s_soak.status = ui_label(r, F_BODY, lv_color_white(), "");
    lv_obj_set_style_bg_color(s_soak.status, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_soak.status, LV_OPA_60, 0);
    lv_obj_set_style_pad_hor(s_soak.status, 6, 0);
    lv_obj_align(s_soak.status, LV_ALIGN_BOTTOM_LEFT, 22, -14);

    lv_obj_t *b = ui_btn(r, "Picture shifted", soak_drift_event, NULL);
    lv_obj_set_size(b, 200, 48);
    ui_btn_kind(b, UI_BTN_DANGER);
    lv_obj_align(b, LV_ALIGN_BOTTOM_RIGHT, -22, -14);
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

void ui_refresh(uint32_t dirty)
{
    const ui_screen_ops_t *ops = s_ops[s_screen];
    if (ops->refresh) {
        ops->refresh(dirty);
    }
}

static void show(ui_screen_t s)
{
    ui_dial_flush();
    ui_popup_close();
    s_screen = s;
    ui_refresh(0xFFFFFFFFu);
    if (s_ops[s]->tick) {
        s_ops[s]->tick();
    }
    lv_screen_load(s_scr[s]);
}

void ui_show_dash(void)
{
    show(UI_SCREEN_DASH);
}

static bool zone_exists(uint8_t addr)
{
    zone_view_t v;
    return ui_zone(addr, &v);
}

void ui_show_zone(uint8_t addr)
{
    if (zone_exists(addr)) {
        zone_scr_open(addr);
        show(UI_SCREEN_ZONE);
    }
}

void ui_show_schedule(uint8_t addr, bool hot_water)
{
    if (zone_exists(addr)) {
        sched_open(addr, hot_water);
        show(UI_SCREEN_SCHEDULE);
    }
}

void ui_show_hotwater(uint8_t addr)
{
    if (zone_exists(addr)) {
        hw_open(addr);
        show(UI_SCREEN_HOTWATER);
    }
}

void ui_show_rename(uint8_t addr)
{
    if (zone_exists(addr)) {
        rename_open(addr, s_screen == UI_SCREEN_ZONES ? UI_SCREEN_ZONES : UI_SCREEN_ZONE);
        show(UI_SCREEN_RENAME);
    }
}

void ui_show_settings(void)
{
    show(UI_SCREEN_SETTINGS);
}

void ui_show_zones(void)
{
    show(UI_SCREEN_ZONES);
}

void ui_show_diag(void)
{
    show(UI_SCREEN_DIAG);
}

void ui_show_system(void)
{
    show(UI_SCREEN_SYSTEM);
}

void ui_show_matter(void)
{
    show(UI_SCREEN_MATTER);
}

static void refresh_timer(lv_timer_t *t)
{
    (void)t;
    uint32_t dirty = zone_store_take_dirty();
    if (dirty) {
        ui_refresh(dirty);
        /* A zone screen whose zone was removed goes back to the dashboard. */
        if ((dirty & ZONE_DIRTY_LIST) &&
            (s_screen == UI_SCREEN_ZONE || s_screen == UI_SCREEN_SCHEDULE ||
             s_screen == UI_SCREEN_HOTWATER || s_screen == UI_SCREEN_RENAME) &&
            !zone_exists(ui_cur_addr)) {
            ui_show_dash();
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
    if (s_ops[s_screen]->tick) {
        s_ops[s_screen]->tick();
    }
    soak_tick();
    blank_check();
}

void ui_init(void)
{
    lv_display_t *d = lv_display_get_default();
    lv_theme_t *th = lv_theme_default_init(d, C_COPPER, C_MUTED, true, F_LABEL);
    lv_display_set_theme(d, th);
    for (int i = 0; i < UI_SCREEN_COUNT; i++) {
        s_scr[i] = s_ops[i]->create();
    }
    lv_timer_create(refresh_timer, 200, NULL);
    lv_timer_create(second_timer, 1000, NULL);
    ui_show_dash();
}
