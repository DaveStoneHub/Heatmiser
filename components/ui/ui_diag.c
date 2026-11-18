/* Bus diagnostics: counters for each stat, the last frames on the bus, and
 * buttons to clear the counters or read one stat now. */
#include <stdio.h>
#include <string.h>

#include "ui_internal.h"

#define N_COLS   6
#define N_LINES  7

static const int s_col_w[N_COLS] = { 64, 230, 110, 120, 80, 120 };
static const char *const s_col_name[N_COLS] = { "ADDR", "ZONE", "POLLS", "TIMEOUTS", "CRC", "LAST SEEN" };

static lv_obj_t *s_scr, *s_hdr_note, *s_table, *s_none, *s_line[N_LINES];
static lv_obj_t *s_cells[32][N_COLS];
static uint32_t s_built;
static uint8_t s_read_addrs[32];

static void back_event(lv_event_t *e)
{
    (void)e;
    ui_show_settings();
}

static void fmt_count(char *buf, size_t cap, uint32_t n)
{
    if (n >= 1000000) {
        snprintf(buf, cap, "%u,%03u,%03u", (unsigned)(n / 1000000), (unsigned)(n / 1000 % 1000),
                 (unsigned)(n % 1000));
    } else if (n >= 1000) {
        snprintf(buf, cap, "%u,%03u", (unsigned)(n / 1000), (unsigned)(n % 1000));
    } else {
        snprintf(buf, cap, "%u", (unsigned)n);
    }
}

static lv_obj_t *table_row(void)
{
    lv_obj_t *r = ui_row(s_table, 0);
    lv_obj_set_height(r, 34);
    lv_obj_set_style_border_side(r, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(r, C_BORDER, 0);
    lv_obj_set_style_border_width(r, 1, 0);
    return r;
}

static void rebuild(void)
{
    lv_obj_clean(s_table);
    memset(s_cells, 0, sizeof s_cells);
    lv_obj_t *head = table_row();
    for (int c = 0; c < N_COLS; c++) {
        lv_obj_t *l = ui_caption(head, s_col_name[c]);
        lv_obj_set_width(l, s_col_w[c]);
    }
    s_built = zone_store_list();
    for (int i = 0; i < 32; i++) {
        if (!(s_built & (1u << i))) {
            continue;
        }
        lv_obj_t *r = table_row();
        for (int c = 0; c < N_COLS; c++) {
            lv_obj_t *l = ui_label(r, c == 1 ? F_LABEL : &hm_bc600_20, C_TEXT, "");
            lv_obj_set_width(l, s_col_w[c]);
            lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
            s_cells[i][c] = l;
        }
        lv_label_set_text_fmt(s_cells[i][0], "%d", i + 1);
    }
    if (s_built) {
        lv_obj_add_flag(s_none, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(s_none, LV_OBJ_FLAG_HIDDEN);
    }
}

static void show_table(void)
{
    if (zone_store_list() != s_built) {
        rebuild();
    }
    for (int i = 0; i < 32; i++) {
        zone_view_t v;
        zone_diag_t d;
        if (!s_cells[i][0] || !ui_zone((uint8_t)(i + 1), &v) || !zone_store_diag((uint8_t)(i + 1), &d)) {
            continue;
        }
        char buf[24];
        lv_label_set_text(s_cells[i][1], v.cfg.name);
        fmt_count(buf, sizeof buf, d.polls);
        lv_label_set_text(s_cells[i][2], buf);
        fmt_count(buf, sizeof buf, d.timeouts);
        lv_label_set_text(s_cells[i][3], buf);
        fmt_count(buf, sizeof buf, d.crc + d.bad);
        lv_label_set_text(s_cells[i][4], buf);
        if (d.seen) {
            ui_fmt_age(buf, sizeof buf, d.seen_age_s);
            lv_label_set_text(s_cells[i][5], buf);
        } else {
            lv_label_set_text(s_cells[i][5], "Never");
        }
        const bool off = v.link == HM_LINK_OFFLINE;
        for (int c = 0; c < N_COLS; c++) {
            lv_color_t col = off ? C_FAULT : c == 0 ? C_COPPER : C_TEXT;
            if (!off && ((c == 3 && d.timeouts) || (c == 4 && d.crc + d.bad))) {
                col = C_COPPER;
            }
            lv_obj_set_style_text_color(s_cells[i][c], col, 0);
        }
    }
}

static void show_frames(void)
{
    zone_frame_t f[N_LINES];
    size_t n = zone_store_frames(f, N_LINES);
    for (size_t i = 0; i < N_LINES; i++) {
        lv_obj_t *l = s_line[i];
        if (i >= n) {
            lv_label_set_text(l, "");
            continue;
        }
        const zone_frame_t *x = &f[i];
        char buf[80];
        int k = snprintf(buf, sizeof buf, "%s ", x->tx ? "TX" : "RX");
        if (!x->tx && x->rx == ZONE_RX_TIMEOUT) {
            snprintf(buf + k, sizeof buf - (size_t)k, " timeout %u ms", x->timeout_ms);
        } else {
            for (int b = 0; b < x->n && k < (int)sizeof buf - 8; b++) {
                k += snprintf(buf + k, sizeof buf - (size_t)k, " %02X", x->b[b]);
            }
            if (x->len > x->n) {
                k += snprintf(buf + k, sizeof buf - (size_t)k, " " ELLIPSIS);
            }
            if (!x->tx && x->rx == ZONE_RX_CRC) {
                snprintf(buf + k, sizeof buf - (size_t)k, "   CRC error");
            } else if (!x->tx && x->rx == ZONE_RX_BAD) {
                snprintf(buf + k, sizeof buf - (size_t)k, "   not understood");
            }
        }
        lv_label_set_text(l, buf);
        const bool bad = !x->tx && x->rx != ZONE_RX_OK;
        lv_obj_set_style_text_color(l, bad ? C_FAULT : x->tx ? C_COPPER : C_TEXT, 0);
    }
}

static void clear_event(lv_event_t *e)
{
    (void)e;
    zone_store_clear_counters();
    ui_toast("Counters cleared");
    show_table();
}

static void pick_read(int i)
{
    zone_err_t err = zone_store_read_now(s_read_addrs[i]);
    if (err == ZONE_OK) {
        char msg[48];
        snprintf(msg, sizeof msg, "Reading stat %u now", s_read_addrs[i]);
        ui_toast(msg);
    } else {
        ui_report(err, "Read");
    }
}

static void read_event(lv_event_t *e)
{
    (void)e;
    static char names[32][ZONE_NAME_MAX + 8];
    static const char *opts[32];
    int n = 0;
    uint32_t list = zone_store_list();
    for (int i = 0; i < 32; i++) {
        zone_view_t v;
        if ((list & (1u << i)) && ui_zone((uint8_t)(i + 1), &v)) {
            snprintf(names[n], sizeof names[n], "%d  %s", i + 1, v.cfg.name);
            opts[n] = names[n];
            s_read_addrs[n++] = (uint8_t)(i + 1);
        }
    }
    if (!n) {
        ui_toast("No zones yet");
        return;
    }
    ui_choose("Read which stat?", opts, n, -1, pick_read);
}

static void diag_refresh(uint32_t dirty)
{
    (void)dirty;
    show_table();
}

static void diag_tick(void)
{
    zone_bus_info_t bi;
    zone_store_bus_info(&bi);
    size_t n = zone_store_count();
    lv_label_set_text_fmt(s_hdr_note, "%u baud, %u stat%s", (unsigned)bi.baud, (unsigned)n, n == 1 ? "" : "s");
    show_table();
    show_frames();
}

static lv_obj_t *diag_create(void)
{
    s_scr = ui_screen_new();
    ui_hdr_t h = ui_header(s_scr, "Bus diagnostics", back_event);
    s_hdr_note = ui_label(h.right, F_LABEL, C_MUTED, "");

    lv_obj_t *body = ui_body(s_scr);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(body, GAP, 0);

    lv_obj_t *tp = ui_panel(body);
    lv_obj_set_width(tp, LV_PCT(100));
    lv_obj_set_flex_grow(tp, 1);
    lv_obj_set_style_pad_hor(tp, 14, 0);
    lv_obj_set_style_pad_ver(tp, 4, 0);
    s_table = ui_col(tp, 0);
    lv_obj_set_size(s_table, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(s_table, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_table, LV_DIR_VER);
    s_none = ui_label(tp, F_BODY, C_MUTED, "No zones yet. Scan the bus from Zones and scan.");
    lv_obj_center(s_none);

    lv_obj_t *bottom = ui_row(body, GAP);
    lv_obj_set_height(bottom, 6 + 16 + N_LINES * 18 + 14);
    lv_obj_t *log = ui_panel(bottom);
    lv_obj_set_height(log, LV_PCT(100));
    lv_obj_set_flex_grow(log, 1);
    lv_obj_set_style_bg_color(log, C_LOG, 0);
    lv_obj_set_style_pad_hor(log, 14, 0);
    lv_obj_set_style_pad_ver(log, 8, 0);
    lv_obj_set_flex_flow(log, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(log, 0, 0);
    ui_caption(log, "RECENT FRAMES");
    for (int i = 0; i < N_LINES; i++) {
        s_line[i] = ui_label(log, &hm_bc600_20, C_TEXT, "");
        lv_obj_set_height(s_line[i], 18);
    }
    lv_obj_t *btns = ui_col(bottom, GAP);
    lv_obj_set_size(btns, 200, LV_PCT(100));
    lv_obj_t *b1 = ui_btn(btns, HM_ICON_RESET "  Clear counters", clear_event, NULL);
    lv_obj_set_width(b1, LV_PCT(100));
    lv_obj_t *b2 = ui_btn(btns, HM_ICON_REFRESH "  Single read", read_event, NULL);
    lv_obj_set_width(b2, LV_PCT(100));
    ui_btn_kind(b2, UI_BTN_SOLID);
    rebuild();
    return s_scr;
}

const ui_screen_ops_t ui_diag_ops = { diag_create, diag_refresh, diag_tick };
