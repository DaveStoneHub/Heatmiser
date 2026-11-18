/* Schedule: a zone's comfort levels (four times and temperatures a day), or a
 * PRT/HW-N's hot water timer (four on and off times), for each programme day
 * (Weekdays and Weekend in 5/2 mode, Mon to Sun in 7-day mode), with a
 * 24-hour profile and a copy to the other days. */
#include <stdio.h>
#include <string.h>

#include "ui_internal.h"

#define PROFILE_W 748
#define BARS_H    48

static lv_obj_t *s_scr, *s_title, *s_mode_lbl, *s_tabs_row, *s_tab[7];
static lv_obj_t *s_lvl_cap[4], *s_f1[4], *s_f2[4];
static lv_obj_t *s_prof_cap, *s_prof_note, *s_bars, *s_bar[8], *s_copy;
static bool s_hw;
static int s_day;
static uint8_t s_days_built; /* tabs built for 2 or 7 days */

/* ---- the editor popup ------------------------------------------------------------------ */

static struct {
    int level;
    int field;            /* 1 = time (or on), 2 = temperature (or off) */
    int hour, minute;     /* hour 24 = not used */
    int temp;
    lv_obj_t *value;
} s_ed;

static void day_periods(const zone_view_t *v, hm_period_t p[4], hm_period_t off[4])
{
    if (s_hw) {
        memcpy(p, v->hw_on_at[s_day], sizeof(hm_period_t) * 4);
        memcpy(off, v->hw_off_at[s_day], sizeof(hm_period_t) * 4);
    } else {
        memcpy(p, v->comfort[s_day], sizeof(hm_period_t) * 4);
    }
}

static zone_err_t write_day(uint8_t addr, int day, hm_period_t p[4], hm_period_t off[4])
{
    zone_periods_sort(p, s_hw ? off : NULL);
    if (s_hw) {
        for (int i = 0; i < 4; i++) {
            if (p[i].hour >= 24) {
                off[i].hour = 24;
                off[i].minute = 0;
            }
        }
        return zone_store_set_hw_timer(addr, day, p, off);
    }
    return zone_store_set_comfort(addr, day, p);
}

static void ed_show(void)
{
    if (s_ed.field == 2 && !s_hw) {
        lv_label_set_text_fmt(s_ed.value, "%d" DEG, s_ed.temp);
    } else if (s_ed.hour >= 24) {
        lv_label_set_text(s_ed.value, "--:--");
    } else {
        lv_label_set_text_fmt(s_ed.value, "%02d:%02d", s_ed.hour, s_ed.minute);
    }
}

static void ed_step(lv_event_t *e)
{
    int d = (int)(intptr_t)lv_event_get_user_data(e);
    if (s_ed.field == 2 && !s_hw) {
        s_ed.temp += d;
        if (s_ed.temp < HM_SET_TEMP_MIN) {
            s_ed.temp = HM_SET_TEMP_MIN;
        }
        if (s_ed.temp > HM_SET_TEMP_MAX) {
            s_ed.temp = HM_SET_TEMP_MAX;
        }
    } else {
        if (s_ed.hour >= 24) {
            s_ed.hour = 7;
            s_ed.minute = 0;
        } else if (d == 1 || d == -1) {
            s_ed.hour = (s_ed.hour + 24 + d) % 24;
        } else {
            int m = s_ed.hour * 60 + s_ed.minute + (d > 0 ? 10 : -10);
            m = (m + 1440) % 1440;
            s_ed.hour = m / 60;
            s_ed.minute = m % 60 / 10 * 10;
        }
    }
    ed_show();
}

static void ed_unused(lv_event_t *e)
{
    (void)e;
    s_ed.hour = 24;
    s_ed.minute = 0;
    ed_show();
}

static void ed_cancel(lv_event_t *e)
{
    (void)e;
    ui_popup_close();
}

static void ed_set(lv_event_t *e)
{
    (void)e;
    zone_view_t v;
    if (!ui_zone(ui_cur_addr, &v)) {
        ui_popup_close();
        return;
    }
    hm_period_t p[4], off[4];
    day_periods(&v, p, off);
    hm_period_t *t = &p[s_ed.level];
    if (s_hw && s_ed.field == 2) {
        t = &off[s_ed.level];
    }
    if (s_ed.field == 2 && !s_hw) {
        t->temp = (uint8_t)s_ed.temp;
    } else {
        t->hour = (uint8_t)s_ed.hour;
        t->minute = (uint8_t)s_ed.minute;
    }
    if (s_hw && p[s_ed.level].hour < 24) {
        const hm_period_t *on = &p[s_ed.level], *of = &off[s_ed.level];
        if (of->hour >= 24) {
            off[s_ed.level].hour = (uint8_t)(on->hour + 1 < 24 ? on->hour + 1 : 23);
            off[s_ed.level].minute = on->hour + 1 < 24 ? on->minute : 59;
        } else if (of->hour * 60 + of->minute <= on->hour * 60 + on->minute) {
            ui_toast("The off time must come after the on time");
            return;
        }
    }
    if (!s_hw && p[s_ed.level].hour < 24 && p[s_ed.level].temp < HM_SET_TEMP_MIN) {
        p[s_ed.level].temp = 18; /* a level coming back into use */
    }
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            if (i != j && p[i].hour < 24 && p[i].hour == p[j].hour && p[i].minute == p[j].minute) {
                ui_toast("Two periods can't start at the same time");
                return;
            }
        }
    }
    ui_popup_close();
    ui_report(write_day(ui_cur_addr, s_day, p, off), s_hw ? "Timer" : "Schedule");
    ui_refresh(1u << (ui_cur_addr - 1));
}

static lv_obj_t *ed_btn(lv_obj_t *row, const char *t, int d)
{
    lv_obj_t *b = ui_btn(row, t, ed_step, (void *)(intptr_t)d);
    lv_obj_set_width(b, 96);
    return b;
}

static void field_event(lv_event_t *e)
{
    int code = (int)(intptr_t)lv_event_get_user_data(e);
    zone_view_t v;
    if (!ui_zone(ui_cur_addr, &v) || !v.have) {
        return;
    }
    if (v.link == HM_LINK_OFFLINE) {
        ui_toast("That stat is offline");
        return;
    }
    s_ed.level = code / 10;
    s_ed.field = code % 10;
    hm_period_t p[4], off[4];
    day_periods(&v, p, off);
    const hm_period_t *t = (s_hw && s_ed.field == 2) ? &off[s_ed.level] : &p[s_ed.level];
    s_ed.hour = t->hour;
    s_ed.minute = t->minute;
    s_ed.temp = p[s_ed.level].temp >= HM_SET_TEMP_MIN ? p[s_ed.level].temp : 18;
    if (!s_hw && s_ed.field == 2 && p[s_ed.level].hour >= 24) {
        ui_toast("Set a time for this level first");
        return;
    }
    char title[48];
    const bool temp = s_ed.field == 2 && !s_hw;
    snprintf(title, sizeof title, "%s %d %s, %s", s_hw ? "Timer" : "Level", s_ed.level + 1,
             temp ? "temperature" : s_hw ? (s_ed.field == 1 ? "on" : "off") : "time",
             zone_prog_day_name(s_hw ? v.hw_timer_days : v.comfort_days, s_day));
    lv_obj_t *box = ui_popup(title, 560, 330, true);
    lv_obj_t *mid = ui_box(box);
    lv_obj_set_size(mid, LV_PCT(100), 150);
    s_ed.value = ui_label(mid, &hm_bc600_112, C_COPPER, "");
    lv_obj_center(s_ed.value);
    if (temp) {
        lv_obj_align(ed_btn(mid, HM_ICON_MINUS, -1), LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_align(ed_btn(mid, HM_ICON_PLUS, 1), LV_ALIGN_RIGHT_MID, 0, 0);
    } else {
        lv_obj_t *l = ui_col(mid, 8);
        lv_obj_set_width(l, 96);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
        ed_btn(l, "h +", 1);
        ed_btn(l, "h " HM_ICON_MINUS, -1);
        lv_obj_t *r = ui_col(mid, 8);
        lv_obj_set_width(r, 96);
        lv_obj_align(r, LV_ALIGN_RIGHT_MID, 0, 0);
        ed_btn(r, "min +", 2);
        ed_btn(r, "min " HM_ICON_MINUS, -2);
    }
    lv_obj_t *row = ui_row(box, GAP);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    if (!temp && (s_ed.field == 1 || !s_hw)) {
        lv_obj_t *u = ui_btn(row, "Not used", ed_unused, NULL);
        lv_obj_set_flex_grow(u, 1);
    }
    lv_obj_set_width(ui_btn(row, "Cancel", ed_cancel, NULL), 120);
    lv_obj_t *ok = ui_btn(row, "Set", ed_set, NULL);
    lv_obj_set_width(ok, 120);
    ui_btn_kind(ok, UI_BTN_SOLID);
    ed_show();
}

/* ---- the screen ------------------------------------------------------------------------- */

static void back_event(lv_event_t *e)
{
    (void)e;
    if (s_hw) {
        ui_show_hotwater(ui_cur_addr);
    } else {
        ui_show_zone(ui_cur_addr);
    }
}

static void tab_event(lv_event_t *e)
{
    s_day = (int)(intptr_t)lv_event_get_user_data(e);
    ui_refresh(1u << (ui_cur_addr - 1));
}

static void copy_event(lv_event_t *e)
{
    (void)e;
    zone_view_t v;
    if (!ui_zone(ui_cur_addr, &v) || !v.have) {
        return;
    }
    const int days = s_hw ? v.hw_timer_days : v.comfort_days;
    hm_period_t p[4], off[4];
    day_periods(&v, p, off);
    int n = 0;
    zone_err_t err = ZONE_OK;
    for (int d = 0; d < days && err == ZONE_OK; d++) {
        if (d == s_day) {
            continue;
        }
        hm_period_t cp[4], co[4];
        memcpy(cp, p, sizeof cp);
        memcpy(co, off, sizeof co);
        err = write_day(ui_cur_addr, d, cp, co);
        n += err == ZONE_OK;
    }
    if (err != ZONE_OK) {
        ui_report(err, "Copy");
    } else {
        char msg[64];
        snprintf(msg, sizeof msg, "Copied %s to %s", zone_prog_day_name(days, s_day),
                 days == 2 ? zone_prog_day_name(2, 1 - s_day) : "every day");
        ui_toast(msg);
    }
    ui_refresh(1u << (ui_cur_addr - 1));
}

static void build_tabs(uint8_t days)
{
    if (days == s_days_built) {
        return;
    }
    lv_obj_clean(s_tabs_row);
    memset(s_tab, 0, sizeof s_tab);
    for (int d = 0; d < days; d++) {
        const char *txt = days == 2 ? (d == 0 ? "Weekdays  Mon to Fri" : "Weekend  Sat and Sun")
                                    : zone_prog_day_name(7, d);
        s_tab[d] = ui_btn(s_tabs_row, txt, tab_event, (void *)(intptr_t)d);
        lv_obj_set_flex_grow(s_tab[d], 1);
        lv_obj_set_style_pad_hor(s_tab[d], 2, 0);
    }
    s_days_built = days;
}

static void field_text(lv_obj_t *b, const hm_period_t *t, bool used, bool temp)
{
    lv_obj_t *l = ui_btn_label(b);
    if (!used) {
        lv_label_set_text(l, "--");
    } else if (temp) {
        lv_label_set_text_fmt(l, "%u" DEG, t->temp);
    } else {
        lv_label_set_text_fmt(l, "%02u:%02u", t->hour, t->minute);
    }
}

static void profile_show(const zone_view_t *v)
{
    for (int i = 0; i < 8; i++) {
        lv_obj_add_flag(s_bar[i], LV_OBJ_FLAG_HIDDEN);
    }
    const int days = s_hw ? v->hw_timer_days : v->comfort_days;
    if (!days) {
        return;
    }
    if (s_hw) {
        for (int i = 0; i < 4; i++) {
            const hm_period_t *on = &v->hw_on_at[s_day][i], *off = &v->hw_off_at[s_day][i];
            if (on->hour >= 24) {
                continue;
            }
            int a = on->hour * 60 + on->minute, b = off->hour < 24 ? off->hour * 60 + off->minute : 1440;
            if (b <= a) {
                continue;
            }
            lv_obj_remove_flag(s_bar[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_pos(s_bar[i], a * PROFILE_W / 1440, 12);
            lv_obj_set_size(s_bar[i], (b - a) * PROFILE_W / 1440 - 2, BARS_H - 24);
            lv_obj_set_style_bg_color(s_bar[i], C_COPPER, 0);
        }
        return;
    }
    /* Comfort: a bar from each level's start to the next, the first from
     * midnight with last night's level. */
    const hm_period_t *p = v->comfort[s_day];
    int edges[6], n = 0;
    edges[n++] = 0;
    for (int i = 0; i < 4; i++) {
        if (p[i].hour < 24) {
            edges[n++] = p[i].hour * 60 + p[i].minute;
        }
    }
    edges[n++] = 1440;
    for (int k = 0; k + 1 < n && k < 8; k++) {
        int a = edges[k], b = edges[k + 1];
        if (b <= a) {
            continue;
        }
        uint8_t t = zone_comfort_at(p, a);
        int h = (t - 10) * 4;
        h = h < 8 ? 8 : h > BARS_H ? BARS_H : h;
        lv_obj_remove_flag(s_bar[k], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(s_bar[k], a * PROFILE_W / 1440, BARS_H - h);
        lv_obj_set_size(s_bar[k], (b - a) * PROFILE_W / 1440 - 2, h);
        lv_obj_set_style_bg_color(s_bar[k], t >= 19 ? C_COPPER : C_HEAT_BORDER, 0);
    }
}

static void sched_refresh(uint32_t dirty)
{
    zone_view_t v;
    if (!ui_cur_addr || !(dirty & (1u << (ui_cur_addr - 1))) || !ui_zone(ui_cur_addr, &v)) {
        return;
    }
    const uint8_t days = s_hw ? v.hw_timer_days : v.comfort_days;
    lv_label_set_text_fmt(s_title, "%s %s", v.cfg.name, s_hw ? "timer" : "schedule");
    lv_label_set_text(s_mode_lbl, !v.have ? "" : v.prog_mode ? "7-day programme" : "5/2 programme");
    build_tabs(days ? days : 2);
    if (s_day >= (days ? days : 2)) {
        s_day = 0;
    }
    const uint8_t busy = s_hw ? v.pend.hw_timer : v.pend.comfort;
    for (int d = 0; d < 7 && s_tab[d]; d++) {
        ui_btn_kind(s_tab[d], d == s_day ? UI_BTN_SOLID : UI_BTN_PLAIN);
    }
    const bool live = v.have && days && v.link != HM_LINK_OFFLINE;
    hm_period_t p[4], off[4];
    day_periods(&v, p, off);
    for (int i = 0; i < 4; i++) {
        const bool used = live && p[i].hour < 24;
        lv_label_set_text_fmt(s_lvl_cap[i], "%s %d%s", s_hw ? "TIMER" : "LEVEL", i + 1,
                              (busy & (1u << s_day)) ? "  " ELLIPSIS : "");
        if (s_hw) {
            lv_label_set_text_fmt(ui_btn_label(s_f1[i]), used ? "On %02u:%02u" : "On --", p[i].hour, p[i].minute);
            lv_label_set_text_fmt(ui_btn_label(s_f2[i]), used && off[i].hour < 24 ? "Off %02u:%02u" : "Off --",
                                  off[i].hour, off[i].minute);
        } else {
            field_text(s_f1[i], &p[i], used, false);
            field_text(s_f2[i], &p[i], used, true);
        }
        ui_enable(s_f1[i], live);
        ui_enable(s_f2[i], live && (used || s_hw));
    }
    if (days == 2) {
        lv_label_set_text_fmt(s_prof_cap, "%s %s", s_day == 0 ? "WEEKDAY" : "WEEKEND", s_hw ? "TIMER" : "PROFILE");
        lv_label_set_text_fmt(ui_btn_label(s_copy), HM_ICON_COPY "  Copy to %s", s_day == 0 ? "Weekend" : "Weekdays");
    } else {
        lv_label_set_text_fmt(s_prof_cap, "%s %s", days ? zone_prog_day_name(7, s_day) : "", s_hw ? "TIMER" : "PROFILE");
        lv_label_set_text(ui_btn_label(s_copy), HM_ICON_COPY "  Copy to every day");
    }
    lv_label_set_text(s_prof_note, !v.have ? "Waiting for the stat" : !days ? "Programme not read yet"
                                   : s_hw ? "Copper is on" : "Taller bars are warmer");
    lv_obj_update_layout(s_prof_cap);
    lv_obj_align_to(s_prof_note, s_prof_cap, LV_ALIGN_OUT_RIGHT_MID, 16, 0);
    ui_enable(s_copy, live);
    profile_show(&v);
}

void sched_open(uint8_t addr, bool hot_water)
{
    if (addr != ui_cur_addr || hot_water != s_hw) {
        zone_view_t v;
        int wday, now;
        s_day = 0;
        if (ui_zone(addr, &v) && ui_now(&v, &wday, &now)) {
            s_day = zone_prog_day(hot_water ? v.hw_timer_days : v.comfort_days, wday); /* today */
        }
    }
    ui_cur_addr = addr;
    s_hw = hot_water;
}

static lv_obj_t *sched_create(void)
{
    s_scr = ui_screen_new();
    ui_hdr_t h = ui_header(s_scr, "", back_event);
    s_title = h.title;
    s_mode_lbl = ui_label(h.right, F_LABEL, C_MUTED, "");

    lv_obj_t *body = ui_body(s_scr);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(body, GAP, 0);
    s_tabs_row = ui_row(body, GAP);

    lv_obj_t *levels = ui_row(body, GAP);
    for (int i = 0; i < 4; i++) {
        lv_obj_t *card = ui_panel(levels);
        lv_obj_set_flex_grow(card, 1);
        lv_obj_set_height(card, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(card, GAP, 0);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(card, 8, 0);
        s_lvl_cap[i] = ui_caption(card, "");
        s_f1[i] = ui_btn(card, "", field_event, (void *)(intptr_t)(i * 10 + 1));
        lv_obj_set_width(s_f1[i], LV_PCT(100));
        lv_obj_set_style_bg_color(s_f1[i], C_FIELD, 0);
        s_f2[i] = ui_btn(card, "", field_event, (void *)(intptr_t)(i * 10 + 2));
        lv_obj_set_width(s_f2[i], LV_PCT(100));
        lv_obj_set_style_text_font(ui_btn_label(s_f1[i]), &hm_bc600_44, 0);
        lv_obj_set_style_text_font(ui_btn_label(s_f2[i]), &hm_bc600_44, 0);
    }

    lv_obj_t *prof = ui_panel(body);
    lv_obj_set_width(prof, LV_PCT(100));
    lv_obj_set_flex_grow(prof, 1);
    lv_obj_set_style_pad_hor(prof, 14, 0);
    lv_obj_set_style_pad_ver(prof, GAP, 0);
    s_prof_cap = ui_caption(prof, "");
    lv_obj_align(s_prof_cap, LV_ALIGN_TOP_LEFT, 0, 0);
    s_prof_note = ui_label(prof, F_BODY, C_MUTED, "");
    lv_obj_align_to(s_prof_note, s_prof_cap, LV_ALIGN_OUT_RIGHT_MID, 16, 0);
    s_bars = ui_box(prof);
    lv_obj_set_size(s_bars, PROFILE_W, BARS_H + 20);
    lv_obj_align(s_bars, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_t *base = ui_box(s_bars);
    lv_obj_set_size(base, PROFILE_W, 1);
    lv_obj_set_pos(base, 0, BARS_H);
    lv_obj_set_style_bg_color(base, C_BORDER, 0);
    lv_obj_set_style_bg_opa(base, LV_OPA_COVER, 0);
    for (int i = 0; i < 8; i++) {
        s_bar[i] = ui_box(s_bars);
        lv_obj_set_style_bg_opa(s_bar[i], LV_OPA_COVER, 0);
        lv_obj_set_style_radius(s_bar[i], 3, 0);
    }
    static const char *const hours[5] = { "00", "06", "12", "18", "24" };
    for (int i = 0; i < 5; i++) {
        lv_obj_t *l = ui_label(s_bars, F_BODY, C_MUTED, hours[i]);
        lv_obj_set_pos(l, i * PROFILE_W / 4 - (i == 4 ? 18 : 0), BARS_H + 3);
    }
    s_copy = ui_btn(prof, "", copy_event, NULL);
    lv_obj_set_height(s_copy, 44);
    lv_obj_align(s_copy, LV_ALIGN_TOP_RIGHT, 0, -4);
    return s_scr;
}

const ui_screen_ops_t ui_sched_ops = { sched_create, sched_refresh, NULL };
