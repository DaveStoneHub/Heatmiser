/*
 * Waveshare ESP32-S3-Touch-LCD-7: the 800 x 480 RGB panel, GT911 touch and
 * the CH422G IO expander that drives the backlight and the reset lines,
 * brought up under LVGL 9 with esp_lvgl_port.
 *
 * Pins and panel timings follow Waveshare's own ESP-IDF LVGL 9 demo for this
 * board (examples/ESP-IDF/09_lvgl_v9_demo in waveshareteam/ESP32-S3-Touch-LCD-7).
 */
#pragma once
#include <stdbool.h>

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BOARD_LCD_H_RES 800
#define BOARD_LCD_V_RES 480

/* Panel, touch and LVGL. The backlight stays off until board_backlight(true),
 * so the first frame is drawn before anything shows. LVGL runs in its own
 * task on core 1; take lvgl_port_lock() around any LVGL call. */
esp_err_t board_display_init(lv_display_t **disp, lv_indev_t **touch);

/* The backlight is on/off only (EXIO2 on the CH422G); there is no PWM. */
esp_err_t board_backlight(bool on);
bool board_backlight_is_on(void);

/* Restart the RGB DMA at the next vsync: puts back a picture that has
 * drifted sideways. With CONFIG_LCD_RGB_RESTART_IN_VSYNC the driver does this
 * every frame anyway. */
esp_err_t board_lcd_restart(void);

/* LVGL refreshes so far, for the soak log. */
uint32_t board_flush_count(void);

#ifdef __cplusplus
}
#endif
