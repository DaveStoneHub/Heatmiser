/* Shared between the UI's source files. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"
#include "ui.h"
#include "zone_store.h"

LV_FONT_DECLARE(hm_icons_20);
LV_FONT_DECLARE(hm_icons_32);
LV_FONT_DECLARE(hm_num_64);
LV_FONT_DECLARE(hm_num_96);

/* Font Awesome 6 Free (solid) glyphs in hm_icons_*. */
#define HM_ICON_FIRE    "\xEF\x81\xAD"
#define HM_ICON_SNOW    "\xEF\x8B\x9C"
#define HM_ICON_LOCK    "\xEF\x80\xA3"
#define HM_ICON_DROP    "\xEF\x81\x83"
#define HM_ICON_POWER   "\xEF\x80\x91"
#define HM_ICON_WIFI    "\xEF\x87\xAB"
#define HM_ICON_WARN    "\xEF\x81\xB1"
#define HM_ICON_CLOCK   "\xEF\x80\x97"
#define HM_ICON_THERMO  "\xEF\x8B\x89"
#define HM_ICON_GEAR    "\xEF\x80\x93"
#define HM_ICON_BOLT    "\xEF\x83\xA7"
#define DEG             "\xC2\xB0"

/* Palette */
#define C_BG        lv_color_hex(0x0F1317)
#define C_PANEL     lv_color_hex(0x1B2229)
#define C_PANEL_HI  lv_color_hex(0x252E37)
#define C_PANEL_DIM lv_color_hex(0x151A1F)
#define C_TEXT      lv_color_hex(0xE8EDF2)
#define C_MUTED     lv_color_hex(0x8D99A6)
#define C_FAINT     lv_color_hex(0x56616C)
#define C_HEAT      lv_color_hex(0xFF8A3D)
#define C_FROST     lv_color_hex(0x5FB4FF)
#define C_OK        lv_color_hex(0x4CC38A)
#define C_BAD       lv_color_hex(0xF0605D)
#define C_WATER     lv_color_hex(0x3FA7E0)

#define HEADER_H    56
#define TOUCH_MIN   64

/* Setpoint changes wait this long after the last tap before going on the
 * bus, so five taps make one write. */
#define DIAL_SETTLE_MS 700

/* ui.c */
extern lv_font_t ui_font20i;  /* Montserrat 20 with the icons as a fallback */
lv_obj_t *ui_header(lv_obj_t *scr, const char *title, bool back, lv_event_cb_t back_cb);
lv_obj_t *ui_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb, void *user);
void ui_style_screen(lv_obj_t *scr);
void ui_rename_dialog(uint8_t addr);
void ui_confirm(const char *title, const char *text, const char *ok, void (*on_ok)(void *), void *arg);
void ui_report(zone_err_t e, const char *what);
bool ui_zone(uint8_t addr, zone_view_t *v); /* zone_store_get */
void ui_fmt_temp(char *buf, size_t cap, int16_t t10);
void ui_fmt_age(char *buf, size_t cap, uint32_t s);
void ui_clock_text(char *buf, size_t cap);

/* Setpoint dial: one pending change at a time, sent after DIAL_SETTLE_MS. */
void ui_dial_nudge(uint8_t addr, int delta);   /* +-1 from the shown value */
void ui_dial_set(uint8_t addr, int celsius);
bool ui_dial_value(uint8_t addr, int *celsius); /* a change not yet sent */
void ui_dial_flush(void);

/* ui_dash.c */
lv_obj_t *dash_create(void);
void dash_refresh(uint32_t dirty);
void dash_tick(void);

/* ui_zone.c */
lv_obj_t *zone_scr_create(void);
void zone_scr_open(uint8_t addr);
void zone_scr_refresh(uint32_t dirty);
void zone_scr_tick(void);
uint8_t zone_scr_addr(void);

/* ui_setup.c */
lv_obj_t *setup_create(void);
void setup_refresh(uint32_t dirty);
void setup_tick(void);
