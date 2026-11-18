/* Zones and scan: the bus scan with a grid of the 32 addresses on the left,
 * and the zone list on the right, where a zone can be renamed, identified
 * (its padlock blinks) or removed. */
#include <stdio.h>
#include <string.h>

#include "ui_internal.h"

#define LEFT_W  300
#define CELL    30
#define CELL_GAP 4

typedef struct {
    lv_obj_t *row, *name, *sub;
} row_t;

static lv_obj_t *s_scr, *s_hdr_note, *s_state_cap, *s_state, *s_bar, *s_cell[32], *s_found, *s_scan_btn;
static lv_obj_t *s_list, *s_none;
static row_t s_rows[32];
static bool s_was_scanning;
static uint8_t s_last_found, s_last_added;

static void back_event(lv_event_t *e)
{
    (void)e;
    ui_show_settings();
}

/* ---- the list ------------------------------------------------------------------------- */

static void rename_event(lv_event_t *e)
{
    ui_show_rename((uint8_t)(uintptr_t)lv_event_get_user_data(e));
}

static void ident_event(lv_event_t *e)
{
    ui_identify((uint8_t)(uintptr_t)lv_event_get_user_data(e));
}

static void do_remove(void *arg)
{
    ui_report(zone_store_remove((uint8_t)(uintptr_t)arg), "Remove");
}

static void remove_event(lv_event_t *e)
{
    uint8_t addr = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    zone_view_t v;
    if (!ui_zone(addr, &v)) {
        return;
    }
    char t[48], m[200];
    snprintf(t, sizeof t, "Remove %s?", v.cfg.name);
    snprintf(m, sizeof m,
             "The bridge stops polling stat %u and forgets its name. The thermostat itself "
             "is not changed, and a scan will find it again.", addr);
    ui_confirm(t, m, "Remove", do_remove, (void *)(uintptr_t)addr);
}

static void row_build(row_t *r, uint8_t addr)
{
    lv_obj_t *o = ui_panel(s_list);
    r->row = o;
    lv_obj_set_size(o, LV_PCT(100), 68);
    lv_obj_set_style_pad_hor(o, GAP, 0);
    lv_obj_set_style_pad_ver(o, 0, 0);
    lv_obj_set_flex_flow(o, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(o, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(o, GAP, 0);

    lv_obj_t *badge = ui_box(o);
    lv_obj_set_size(badge, 40, 40);
    lv_obj_set_style_radius(badge, RADIUS, 0);
    lv_obj_set_style_bg_color(badge, C_HEAT_BG, 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(badge, C_HEAT_BORDER, 0);
    lv_obj_set_style_border_width(badge, 1, 0);
    lv_obj_t *n = ui_label(badge, &hm_bc600_30, C_COPPER, "");
    lv_label_set_text_fmt(n, "%u", addr);
    lv_obj_center(n);

    /* Name and model: tap to rename. */
    lv_obj_t *col = ui_col(o, 2);
    lv_obj_set_height(col, LV_PCT(100));
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_add_flag(col, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(col, rename_event, LV_EVENT_CLICKED, (void *)(uintptr_t)addr);
    r->name = ui_label(col, F_LABEL, C_TEXT, "");
    lv_label_set_long_mode(r->name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(r->name, LV_PCT(100));
    r->sub = ui_label(col, F_BODY, C_MUTED, "");

    lv_obj_t *id = ui_btn(o, HM_ICON_EYE "  Identify", ident_event, (void *)(uintptr_t)addr);
    lv_obj_set_height(id, 48);
    lv_obj_t *rm = ui_btn(o, HM_ICON_TRASH, remove_event, (void *)(uintptr_t)addr);
    lv_obj_set_size(rm, 56, 48);
    lv_obj_set_style_text_color(rm, C_FAULT, 0);
}

static const char *model_name(const zone_view_t *v)
{
    uint8_t m = v->have ? v->model : v->cfg.model;
    return v->have ? (v->is_prthw ? "PRT/HW-N" : "PRT-N")
           : m == HM_MODEL_PRTHW ? "PRT/HW-N"
           : m == HM_MODEL_PRT ? "PRT-N" : "Model not read yet";
}

static void row_show(row_t *r, const zone_view_t *v)
{
    char t[16], sub[80];
    lv_label_set_text(r->name, v->cfg.name);
    if (v->link == HM_LINK_OFFLINE) {
        snprintf(sub, sizeof sub, "%s, offline", model_name(v));
    } else if (!v->have) {
        snprintf(sub, sizeof sub, "%s, connecting" ELLIPSIS, model_name(v));
    } else {
        ui_fmt_temp(t, sizeof t, v->room_t10);
        snprintf(sub, sizeof sub, "%s, %s%s", model_name(v), t, v->locked ? ", keys locked" : "");
    }
    lv_label_set_text(r->sub, sub);
    lv_obj_set_style_text_color(r->sub, v->link == HM_LINK_OFFLINE ? C_FAULT : C_MUTED, 0);
}

static void rebuild(void)
{
    lv_obj_clean(s_list);
    memset(s_rows, 0, sizeof s_rows);
    uint32_t list = zone_store_list();
    for (int i = 0; i < 32; i++) {
        if (list & (1u << i)) {
            row_build(&s_rows[i], (uint8_t)(i + 1));
        }
    }
    if (list) {
        lv_obj_add_flag(s_none, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(s_none, LV_OBJ_FLAG_HIDDEN);
    }
}

/* ---- the scan --------------------------------------------------------------------------- */

static void scan_event(lv_event_t *e)
{
    (void)e;
    zone_store_scan();
    ui_refresh(0);
}

static void show_scan(void)
{
    zone_scan_t sc;
    zone_store_scan_state(&sc);
    uint32_t list = zone_store_list();
    int done = !sc.running ? 0 : sc.pass == 1 ? sc.next - 1 : 32;
    if (sc.running) {
        lv_label_set_text(s_state_cap, sc.pass == 1 ? "SCANNING" : "CHECKING AGAIN");
        lv_label_set_text_fmt(s_state, "Address %u of 32", sc.next);
        lv_bar_set_value(s_bar, sc.pass == 1 ? sc.next - 1 : 32 + sc.next - 1, LV_ANIM_OFF);
        lv_label_set_text_fmt(s_found, "%u stat%s found so far", sc.found, sc.found == 1 ? "" : "s");
        lv_label_set_text(s_hdr_note, "Polling paused while scanning");
        s_last_found = sc.found;
        s_last_added = sc.added;
    } else {
        if (s_was_scanning) {
            s_last_found = sc.found;
            s_last_added = sc.added;
        }
        lv_label_set_text(s_state_cap, "BUS SCAN");
        lv_label_set_text(s_state, s_was_scanning || s_last_found ? "Scan finished" : "Ready to scan");
        lv_bar_set_value(s_bar, s_was_scanning || s_last_found ? 64 : 0, LV_ANIM_OFF);
        if (s_was_scanning || s_last_found) {
            lv_label_set_text_fmt(s_found, "%u stat%s found, %u new", s_last_found, s_last_found == 1 ? "" : "s",
                                  s_last_added);
        } else {
            lv_label_set_text(s_found, "A scan pauses polling for about 15 seconds");
        }
        lv_label_set_text_fmt(s_hdr_note, "%u zone%s", (unsigned)zone_store_count(),
                              zone_store_count() == 1 ? "" : "s");
    }
    ui_enable(s_scan_btn, !sc.running);
    lv_label_set_text(ui_btn_label(s_scan_btn), s_was_scanning || s_last_found || zone_store_count()
                                                    ? HM_ICON_REFRESH "  Re-scan" : HM_ICON_REFRESH "  Scan the bus");
    for (int i = 0; i < 32; i++) {
        const uint8_t a = (uint8_t)(i + 1);
        const bool in_list = (list >> i) & 1;
        const bool current = sc.running && sc.pass == 1 && a == sc.next;
        lv_color_t bg = C_TRACK, fg = C_MUTED, border = C_BORDER;
        if (in_list) {
            zone_view_t v;
            bool off = ui_zone(a, &v) && v.link == HM_LINK_OFFLINE;
            bg = off ? C_FAULT_BG : C_COPPER;
            fg = off ? C_FAULT : C_ON_COPPER;
            border = off ? C_FAULT : C_COPPER;
        } else if (current) {
            bg = C_HEAT_BG;
            fg = C_COPPER;
            border = C_COPPER;
        } else if (sc.running && a < done + 1) {
            bg = C_CELL;
        }
        lv_obj_set_style_bg_color(s_cell[i], bg, 0);
        lv_obj_set_style_border_color(s_cell[i], border, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(s_cell[i], 0), fg, 0);
    }
    s_was_scanning = sc.running;
}

static void zones_refresh(uint32_t dirty)
{
    uint32_t list = zone_store_list();
    bool need = (dirty & ZONE_DIRTY_LIST) != 0;
    for (int i = 0; i < 32 && !need; i++) {
        need = (s_rows[i].row != NULL) != ((list >> i) & 1);
    }
    if (need) {
        rebuild();
        dirty = 0xFFFFFFFFu;
    }
    for (int i = 0; i < 32; i++) {
        zone_view_t v;
        if ((dirty & (1u << i)) && s_rows[i].row && ui_zone((uint8_t)(i + 1), &v)) {
            row_show(&s_rows[i], &v);
        }
    }
    show_scan();
}

static void zones_tick(void)
{
    show_scan();
}

static lv_obj_t *zones_create(void)
{
    s_scr = ui_screen_new();
    ui_hdr_t h = ui_header(s_scr, "Zones and scan", back_event);
    s_hdr_note = ui_label(h.right, F_LABEL, C_MUTED, "");

    lv_obj_t *body = ui_body(s_scr);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(body, GAP, 0);

    lv_obj_t *left = ui_panel(body);
    lv_obj_set_size(left, LEFT_W, LV_PCT(100));
    lv_obj_set_style_pad_all(left, 14, 0);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(left, GAP, 0);
    s_state_cap = ui_caption(left, "BUS SCAN");
    s_state = ui_label(left, &hm_bc600_30, C_TEXT, "");
    s_bar = lv_bar_create(left);
    lv_obj_set_size(s_bar, LV_PCT(100), 6);
    lv_bar_set_range(s_bar, 0, 64);
    lv_obj_set_style_bg_color(s_bar, C_TRACK, 0);
    lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_bar, C_COPPER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_bar, 3, 0);
    lv_obj_set_style_radius(s_bar, 3, LV_PART_INDICATOR);

    lv_obj_t *grid = ui_box(left);
    lv_obj_set_size(grid, LV_PCT(100), 4 * CELL + 3 * CELL_GAP);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(grid, CELL_GAP, 0);
    lv_obj_set_style_pad_column(grid, CELL_GAP, 0);
    for (int i = 0; i < 32; i++) {
        lv_obj_t *c = ui_box(grid);
        lv_obj_set_size(c, CELL, CELL);
        lv_obj_set_style_radius(c, 4, 0);
        lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(c, 1, 0);
        lv_obj_t *n = ui_label(c, F_CAP, C_MUTED, "");
        lv_label_set_text_fmt(n, "%d", i + 1);
        lv_obj_center(n);
        s_cell[i] = c;
    }
    s_found = ui_label(left, F_BODY, C_MUTED, "");
    lv_obj_set_width(s_found, LV_PCT(100));
    lv_label_set_long_mode(s_found, LV_LABEL_LONG_WRAP);
    lv_obj_t *fill = ui_box(left);
    lv_obj_set_flex_grow(fill, 1);
    s_scan_btn = ui_btn(left, "", scan_event, NULL);
    lv_obj_set_width(s_scan_btn, LV_PCT(100));
    ui_btn_kind(s_scan_btn, UI_BTN_SOLID);

    lv_obj_t *right = ui_col(body, GAP);
    lv_obj_set_height(right, LV_PCT(100));
    lv_obj_set_flex_grow(right, 1);
    ui_caption(right, "ZONES  " HM_ICON_PENCIL " tap a name to rename it");
    s_list = ui_col(right, 8);
    lv_obj_set_width(s_list, LV_PCT(100));
    lv_obj_set_flex_grow(s_list, 1);
    lv_obj_add_flag(s_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    s_none = ui_label(right, F_BODY, C_MUTED, "No zones yet. Scan the bus to find the thermostats.");
    rebuild();
    return s_scr;
}

const ui_screen_ops_t ui_zones_ops = { zones_create, zones_refresh, zones_tick };
