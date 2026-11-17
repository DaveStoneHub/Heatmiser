/*
 * Heatmiser V3 RS485 writes: one function group per frame (protocol V3.9,
 * section 3.2), and the read-back check that confirms a write took.
 *
 * Writable over RS485 (yellow in the V3.9 DCB tables): frost temperature 17,
 * set temperature 18, floor limit 19, on/off 21, key lock 22, run mode 23,
 * holiday hours 24-25, hold minutes 32-33, hot water 42 (PRTHW), clock 43-46,
 * comfort levels and hot water timers. Everything else is read only, and a
 * write to a read-only address gets no reply at all.
 *
 * Pure C, no ESP-IDF dependencies.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hm_dcb.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HM_WRITE_MAX_DATA 16   /* a hot water timer day is the largest group */

/* One write: the function group's start address and its data in wire order
 * (two-byte values low byte first). */
typedef struct {
    uint16_t addr;
    uint8_t  len;
    uint8_t  data[HM_WRITE_MAX_DATA];
} hm_write_t;

typedef enum {
    HM_WR_OK = 0,
    HM_WR_RANGE,      /* value out of the field's range */
    HM_WR_MODEL,      /* field does not exist on this model or programme mode */
    HM_WR_ARG,
} hm_wr_status_t;

/* Range limits from the V3.9 DCB tables. */
#define HM_SET_TEMP_MIN     5
#define HM_SET_TEMP_MAX     35
#define HM_FROST_TEMP_MIN   7
#define HM_FROST_TEMP_MAX   17
#define HM_FLOOR_MAX_MIN    20
#define HM_FLOOR_MAX_MAX    45

/* Hot water command (address 42). Reads back 1 = on now, 0 = off now. */
#define HM_HW_FOLLOW_TIMER  0
#define HM_HW_FORCE_ON      1
#define HM_HW_FORCE_OFF     2

hm_wr_status_t hm_wr_set_temp(hm_write_t *w, int celsius);
hm_wr_status_t hm_wr_frost_temp(hm_write_t *w, int celsius);
hm_wr_status_t hm_wr_floor_max(hm_write_t *w, int celsius);
hm_wr_status_t hm_wr_on_off(hm_write_t *w, bool on);
hm_wr_status_t hm_wr_key_lock(hm_write_t *w, bool locked);
hm_wr_status_t hm_wr_run_mode(hm_write_t *w, bool frost);
hm_wr_status_t hm_wr_holiday(hm_write_t *w, unsigned hours);
hm_wr_status_t hm_wr_hold(hm_write_t *w, unsigned minutes);
hm_wr_status_t hm_wr_hw_state(hm_write_t *w, uint8_t model, int cmd);
/* wday 1-7 = Mon-Sun. */
hm_wr_status_t hm_wr_clock(hm_write_t *w, int wday, int hour, int min, int sec);

/* One day of comfort levels. day is 0-1 (weekday, weekend) in 5/2 mode and
 * 0-6 (Mon-Sun) in 7-day mode. Hour 24 cancels a period; cancelled periods
 * must come last, and the others must be in time order. */
hm_wr_status_t hm_wr_comfort(hm_write_t *w, uint8_t model, uint8_t prog_mode, int day,
                             const hm_period_t p[4]);

/* One day of PRTHW hot water timers: four on/off pairs, same day numbering. */
hm_wr_status_t hm_wr_hw_timer(hm_write_t *w, uint8_t model, uint8_t prog_mode, int day,
                              const hm_period_t on[4], const hm_period_t off[4]);

/* Experimental: the V3.9 table marks frost protection enable (7) read only,
 * yet its own example frame writes it. If a stat does accept it, it must get
 * the stat's encoding: 0 = enabled (DCB byte 7 mirrors Feature 03). Not used
 * by the bridge. */
hm_wr_status_t hm_wr_frost_enable(hm_write_t *w, bool enabled);

/* Writes that put a stat in a mode, in the order to send them. Heat and frost
 * switch the stat on first. Returns how many (1 or 2), 0 on a bad mode. */
size_t hm_wr_mode(hm_write_t out[2], hm_mode_t mode);

/* Build the request frame for a write. Returns frame bytes or 0. */
size_t hm_write_frame(const hm_write_t *w, uint8_t *out, size_t cap, uint8_t stat,
                      uint8_t master);

typedef enum {
    HM_VERIFY_OK = 0,      /* the DCB holds what was written */
    HM_VERIFY_UNCHECKED,   /* nothing stable to compare (clock, follow-timer) */
    HM_VERIFY_MISMATCH,
    HM_VERIFY_NO_FIELD,    /* the DCB does not reach that address */
} hm_verify_t;

/* Check a full DCB read after a write. Hold minutes and holiday hours count
 * down, so a value up to 2 lower still confirms. */
hm_verify_t hm_write_verify(const hm_write_t *w, const uint8_t *dcb, size_t n);

const char *hm_wr_status_str(hm_wr_status_t s);
const char *hm_verify_str(hm_verify_t v);

/* Short description, e.g. "set temp 21" or "comfort @47", for logs. */
void hm_write_describe(const hm_write_t *w, char *buf, size_t cap);

#ifdef __cplusplus
}
#endif
