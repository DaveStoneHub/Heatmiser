/*
 * Decode a Heatmiser V3 RS485 DCB (protocol V3.9, sections 3.3 and 3.4).
 *
 * The decoder works on the data bytes of a full-DCB read reply, so offsets
 * here are the "index in DCB" column, not the unique address. Two-byte values
 * arrive high byte first on read.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HM_MODEL_DT     0x00
#define HM_MODEL_DT_E   0x01
#define HM_MODEL_PRT    0x02
#define HM_MODEL_PRT_E  0x03
#define HM_MODEL_PRTHW  0x04

#define HM_TEMP_ABSENT  0xFFFFu   /* sensor not fitted */

/* Indices shared by every model. */
#define HM_IDX_DCB_LEN_HI     0
#define HM_IDX_DCB_LEN_LO     1
#define HM_IDX_VENDOR         2
#define HM_IDX_VERSION        3
#define HM_IDX_MODEL          4
#define HM_IDX_TEMP_FORMAT    5
#define HM_IDX_SWITCH_DIFF    6
#define HM_IDX_FROST_ENABLE   7
#define HM_IDX_CALIBRATION    8   /* 2 bytes */
#define HM_IDX_OUTPUT_DELAY   10
#define HM_IDX_ADDRESS        11
#define HM_IDX_UPDOWN_LIMIT   12
#define HM_IDX_SENSOR_SEL     13
#define HM_IDX_OPTIMUM_START  14
#define HM_IDX_RATE_OF_CHANGE 15
#define HM_IDX_PROG_MODE      16
#define HM_IDX_FROST_TEMP     17
#define HM_IDX_SET_TEMP       18
#define HM_IDX_FLOOR_MAX      19
#define HM_IDX_FLOOR_MAX_EN   20
#define HM_IDX_ON_OFF         21
#define HM_IDX_KEY_LOCK       22
#define HM_IDX_RUN_MODE       23
#define HM_IDX_HOLIDAY_HOURS  24  /* 2 bytes; unique address 24 */
#define HM_IDX_HOLD_MINUTES   26  /* 2 bytes; unique address 32 */
#define HM_IDX_REMOTE_AIR     28  /* 2 bytes; unique address 34 */
#define HM_IDX_FLOOR_TEMP     30  /* 2 bytes; unique address 36 */
#define HM_IDX_AIR_TEMP       32  /* 2 bytes; unique address 38 */
#define HM_IDX_ERROR_CODE     34
#define HM_IDX_HEATING_ON     35
#define HM_IDX_HW_STATE       36  /* PRTHW only; unique address 42 */

#define HM_DCB_MIN_LEN        36  /* DT ends after heating state */
#define HM_PRT_DCB_LEN_52     64
#define HM_PRT_DCB_LEN_7DAY   148

typedef struct {
    uint8_t hour;     /* 24 = period cancelled */
    uint8_t minute;
    uint8_t temp;     /* comfort levels only */
} hm_period_t;

typedef struct {
    uint16_t dcb_len;          /* as reported in bytes 0-1 */
    uint8_t  vendor;
    uint8_t  version;          /* bits 0-6 on PRT; whole byte on PRTHW */
    bool     floor_limit_active;
    uint8_t  model;
    uint8_t  temp_format;      /* 0 = C, 1 = F */
    uint8_t  switch_diff;
    uint8_t  frost_enable;
    uint16_t calibration_raw;  /* units not yet confirmed on a real stat */
    uint8_t  output_delay;
    uint8_t  address;
    uint8_t  updown_limit;
    uint8_t  sensor_sel;
    uint8_t  optimum_start;
    uint8_t  rate_of_change;
    uint8_t  prog_mode;        /* 0 = 5/2, 1 = 7 day */
    uint8_t  frost_temp;
    uint8_t  set_temp;
    uint8_t  floor_max;
    uint8_t  floor_max_enable;
    uint8_t  on_off;
    uint8_t  key_lock;
    uint8_t  run_mode;         /* 0 = heating, 1 = frost */
    uint16_t holiday_hours;
    uint16_t hold_minutes;
    uint16_t remote_air_t10;   /* tenths of a degree, HM_TEMP_ABSENT if absent */
    uint16_t floor_t10;
    uint16_t air_t10;
    uint8_t  error_code;
    uint8_t  heating_on;

    bool     has_hw;           /* PRTHW */
    uint8_t  hw_on;

    bool     has_clock;        /* not on DT */
    uint8_t  clk_wday, clk_hour, clk_min, clk_sec;

    /* Comfort levels: 2 day groups (weekday, weekend) in 5/2 mode, 7 (Mon-Sun)
     * in 7-day mode, 4 periods each. comfort_days = 0 if the DCB is too short. */
    uint8_t     comfort_days;
    hm_period_t comfort[7][4];

    /* Hot water timers (PRTHW): 4 on/off pairs per day group. */
    uint8_t     hw_timer_days;
    hm_period_t hw_on_at[7][4];
    hm_period_t hw_off_at[7][4];
} hm_dcb_t;

/* Returns 0 on success, -1 if the buffer is shorter than the common fields
 * or shorter than its own length field. Unknown models decode the common
 * part only. */
int hm_dcb_decode(const uint8_t *dcb, size_t n, hm_dcb_t *out);

const char *hm_model_str(uint8_t model);

/* Human-readable dump for the bring-up console. */
void hm_dcb_print(FILE *f, const hm_dcb_t *d);

/* Hex dump, 16 bytes per line, with offsets. */
void hm_hexdump(FILE *f, const uint8_t *buf, size_t n);

#ifdef __cplusplus
}
#endif
