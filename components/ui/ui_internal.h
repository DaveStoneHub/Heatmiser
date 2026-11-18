/* Shared between the UI's source files: the Copper look, shared widgets and
 * each screen's entry points. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"
#include "ui.h"
#include "zone_store.h"

/* ---- fonts (components/ui/fonts, made by tools/gen_fonts.sh) ------------------- */

LV_FONT_DECLARE(hm_icon_18);
LV_FONT_DECLARE(hm_icon_22);
LV_FONT_DECLARE(hm_icon_28);
LV_FONT_DECLARE(hm_mr500_15);   /* Manrope: body text */
LV_FONT_DECLARE(hm_mr700_15);   /* captions */
LV_FONT_DECLARE(hm_mr700_17);   /* names, buttons */
LV_FONT_DECLARE(hm_mr700_22);   /* titles, keyboard */
LV_FONT_DECLARE(hm_bc600_20);   /* Barlow Condensed: clock */
LV_FONT_DECLARE(hm_bc600_30);
LV_FONT_DECLARE(hm_bc600_44);
LV_FONT_DECLARE(hm_bc500_58);   /* tile temperatures */
LV_FONT_DECLARE(hm_bc600_112);  /* the setpoint dial */
LV_FONT_DECLARE(hm_bc600_150);  /* ON / OFF on the hot water screen */

#define F_BODY   (&hm_mr500_15)
#define F_CAP    (&hm_mr700_15)
#define F_LABEL  (&hm_mr700_17)
#define F_TITLE  (&hm_mr700_22)

/* Lucide line icons in hm_icon_*; the Manrope fonts fall back to them, so a
 * label can mix an icon and text. */
#define HM_ICON_FLAME     "\xEE\x83\x97"
#define HM_ICON_DROP      "\xEE\x82\xB8"
#define HM_ICON_LOCK      "\xEE\x84\x8F"
#define HM_ICON_UNLOCK    "\xEE\x84\x90"
#define HM_ICON_WIFI      "\xEE\x86\xAE"
#define HM_ICON_WIFI_OFF  "\xEE\x86\xAF"
#define HM_ICON_WARN      "\xEE\x86\x93"
#define HM_ICON_BACK      "\xEE\x81\xB2"
#define HM_ICON_NEXT      "\xEE\x81\xB3"
#define HM_ICON_PENCIL    "\xEE\x87\xB9"
#define HM_ICON_SLIDERS   "\xEE\x8A\x9A"
#define HM_ICON_CALENDAR  "\xEE\x81\xA7"
#define HM_ICON_COPY      "\xEE\x82\xA2"
#define HM_ICON_PLUS      "\xEE\x85\x81"
#define HM_ICON_MINUS     "\xEE\x84\xA0"
#define HM_ICON_BKSP      "\xEE\x82\xB2"
#define HM_ICON_SHIFT     "\xEE\x87\xA4"
#define HM_ICON_REFRESH   "\xEE\x85\x89"
#define HM_ICON_EYE       "\xEE\x82\xBE"
#define HM_ICON_TRASH     "\xEE\x86\x8E"
#define HM_ICON_LIST      "\xEE\x84\x8C"
#define HM_ICON_RADIO     "\xEE\x85\x86"
#define HM_ICON_ACTIVITY  "\xEE\x80\xB8"
#define HM_ICON_CLOCK     "\xEE\x82\x8B"
#define HM_ICON_PALM      "\xEE\x8A\x81"
#define HM_ICON_RESET     "\xEE\x85\x8C"
#define HM_ICON_SNOW      "\xEE\x85\xA9"
#define HM_ICON_POWER     "\xEE\x85\x84"
#define HM_ICON_THERMO    "\xEE\x86\x86"
#define HM_ICON_MONITOR   "\xEE\x84\xA1"
#define HM_ICON_CHECK     "\xEE\x81\xB0"
#define HM_ICON_X         "\xEE\x86\xB2"
#define HM_ICON_SHARE     "\xEE\x85\x9A"
#define HM_ICON_INFO      "\xEE\x83\xBE"
#define DEG               "\xC2\xB0"
#define ELLIPSIS          "\xE2\x80\xA6"

/* ---- Copper palette ------------------------------------------------------------- */

#define C_GROUND       lv_color_hex(0x0E0E0F)
#define C_TILE         lv_color_hex(0x1A1917)
#define C_BORDER       lv_color_hex(0x2A2825)
#define C_FIELD        lv_color_hex(0x232120)  /* editable values on a tile */
#define C_TRACK        lv_color_hex(0x262421)  /* empty part of a bar or strip */
#define C_CELL         lv_color_hex(0x1F1D1B)
#define C_LOG          lv_color_hex(0x141312)
#define C_HEAT_BG      lv_color_hex(0x2B1E14)
#define C_HEAT_BORDER  lv_color_hex(0x6B4524)
#define C_DIAL_TRACK   lv_color_hex(0x3A2D24)
#define C_COPPER       lv_color_hex(0xE0975A)
#define C_ON_COPPER    lv_color_hex(0x1A1208)  /* text on a copper button */
#define C_TEXT         lv_color_hex(0xF2EDE8)
#define C_MUTED        lv_color_hex(0xADA49B)
#define C_FAULT        lv_color_hex(0xFF8C7A)
#define C_FAULT_BG     lv_color_hex(0x2D1613)
#define C_OFFLINE_BG   lv_color_hex(0x121211)
#define C_PRESSED      lv_color_hex(0x2E2B28)

#define SCR_W       800
#define SCR_H       480
#define HEADER_H    52
#define PAD         12
#define GAP         10
#define RADIUS      6
#define TOUCH_MIN   64
#define BODY_H      (SCR_H - HEADER_H - PAD)      /* the area under the header */

/* Setpoint changes wait this long after the last tap before going on the
 * bus, so five taps make one write. */
#define DIAL_SETTLE_MS 700

/* ---- shared widgets (ui.c) -------------------------------------------------------- */

typedef struct {
    lv_obj_t *bar, *title, *right;
} ui_hdr_t;

lv_obj_t *ui_screen_new(void);
/* Header: a back button when back is set, the title, and a row on the right. */
ui_hdr_t ui_header(lv_obj_t *scr, const char *title, lv_event_cb_t back);
/* The area under the header, PAD from the edges. */
lv_obj_t *ui_body(lv_obj_t *scr);
lv_obj_t *ui_box(lv_obj_t *parent);      /* transparent, no padding, not scrollable */
lv_obj_t *ui_row(lv_obj_t *parent, int gap);
lv_obj_t *ui_col(lv_obj_t *parent, int gap);
lv_obj_t *ui_panel(lv_obj_t *parent);    /* tile background, border, radius */
lv_obj_t *ui_label(lv_obj_t *parent, const lv_font_t *f, lv_color_t c, const char *text);
lv_obj_t *ui_caption(lv_obj_t *parent, const char *text);
/* One line that does not fit: slide it slowly back and forth (names), or
 * end it with dots (everything else). Give the label a width first. */
void ui_label_marquee(lv_obj_t *l);
void ui_label_dots(lv_obj_t *l);
/* Set a marquee label's text, leaving it moving if the text is the same. */
void ui_marquee_text(lv_obj_t *l, const char *text);

typedef enum {
    UI_BTN_PLAIN = 0,
    UI_BTN_SOLID,      /* copper, for the chosen option or the main action */
    UI_BTN_TINT,       /* copper outline on the heat tint, for a chosen chip */
    UI_BTN_DANGER,
} ui_btn_kind_t;
lv_obj_t *ui_btn(lv_obj_t *parent, const char *text, lv_event_cb_t cb, void *user);
void ui_btn_kind(lv_obj_t *b, ui_btn_kind_t k);
lv_obj_t *ui_btn_label(lv_obj_t *b);
/* 64 x 44 header button with one icon. */
lv_obj_t *ui_icon_btn(lv_obj_t *parent, const char *icon, lv_event_cb_t cb, void *user);
void ui_enable(lv_obj_t *o, bool on);
/* Dashed outline (LVGL borders are solid). */
void ui_dashed(lv_obj_t *o, bool on, lv_color_t c);

/* A popup over the screen: returns the box to fill. A tap outside closes it
 * when closable. */
lv_obj_t *ui_popup(const char *title, int w, int h, bool closable);
void ui_popup_close(void);
bool ui_popup_open(void);

void ui_confirm(const char *title, const char *text, const char *ok, void (*on_ok)(void *), void *arg);
/* A list of choices; on_pick gets the index. */
void ui_choose(const char *title, const char *const *opts, int n, int selected, void (*on_pick)(int));

void ui_report(zone_err_t e, const char *what);
bool ui_zone(uint8_t addr, zone_view_t *v); /* zone_store_get */
void ui_fmt_temp(char *buf, size_t cap, int16_t t10);
void ui_fmt_age(char *buf, size_t cap, uint32_t s);
void ui_clock_text(char *buf, size_t cap, bool with_day);
/* Today's weekday (1-7 = Mon-Sun) and minute of the day, from the bridge's
 * clock if set, else the stat's; false if neither is known. */
bool ui_now(const zone_view_t *v, int *wday, int *minute);

/* Setpoint dial: one pending change at a time, sent after DIAL_SETTLE_MS. */
void ui_dial_nudge(uint8_t addr, int delta);   /* +-1 from the shown value */
void ui_dial_set(uint8_t addr, int celsius);
bool ui_dial_value(uint8_t addr, int *celsius); /* a change not yet sent */
void ui_dial_flush(void);

/* Refresh every built screen for these dirty zones. */
void ui_refresh(uint32_t dirty);

/* ---- screens ------------------------------------------------------------------------ */

typedef struct {
    lv_obj_t *(*create)(void);
    void (*refresh)(uint32_t dirty);    /* zones changed */
    void (*tick)(void);                 /* once a second while showing */
} ui_screen_ops_t;

extern const ui_screen_ops_t ui_dash_ops, ui_zone_ops, ui_sched_ops, ui_hw_ops, ui_rename_ops,
    ui_settings_ops, ui_zones_ops, ui_diag_ops, ui_system_ops;

/* The zone the zone, schedule, hot water or rename screen is showing. */
extern uint8_t ui_cur_addr;

void zone_scr_open(uint8_t addr);
void sched_open(uint8_t addr, bool hot_water);
void hw_open(uint8_t addr);
void rename_open(uint8_t addr, ui_screen_t back);
/* Identify: toggle a stat's key lock so its padlock blinks. */
void ui_identify(uint8_t addr);
