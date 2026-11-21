/*
 * What the screen (and later Matter) shows for one zone, built from the
 * stat's cached DCB, plus the saved zone list's on-flash format.
 *
 * Pure C, no ESP-IDF dependencies, so the host tests and the desktop UI
 * preview use the same code as the board.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hm_dcb.h"
#include "hm_sched.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZONE_NAME_MAX   16          /* bytes, without the terminator */
#define ZONE_T_ABSENT   INT16_MIN   /* no such sensor */

/* One saved zone: what the bridge must remember about a stat. Live values
 * stay on the stat and are re-read at boot. */
typedef struct {
    uint8_t  addr;                  /* 1-32 */
    uint8_t  model;                 /* last seen, 0xFF = never read */
    uint16_t endpoint;              /* Matter endpoint ID, 0 until milestone 4 */
    char     name[ZONE_NAME_MAX + 1];
    /* PRTHW: the hot water endpoint's Matter ID, 0 = none yet. Saved on its
     * own (NVS key "hwep<addr>"), not in the zone list blob, so the list
     * keeps version 1 and older firmware can still read it. */
    uint16_t hw_endpoint;
} zone_cfg_t;

/* A write the user asked for that the stat has not confirmed yet. */
typedef struct {
    bool     set_temp, mode, lock, hold, hw;
    uint8_t  comfort;               /* bit per programme day being written */
    uint8_t  hw_timer;              /* the same for hot water timers */
    uint8_t  set_temp_c;
    hm_mode_t mode_v;
    bool     lock_v;
    uint16_t hold_min;
    int      hw_cmd;                /* HM_HW_FOLLOW_TIMER / FORCE_ON / FORCE_OFF */
} zone_pending_t;

typedef struct {
    zone_cfg_t cfg;
    hm_link_t  link;
    bool       have;                /* a DCB has been read since boot */
    uint32_t   age_s;               /* since the last good read; only if have */

    uint8_t    model;
    bool       is_prthw;
    int16_t    room_t10;            /* by sensor selection, ZONE_T_ABSENT if none */
    int16_t    air_t10, remote_t10, floor_t10;
    uint8_t    sensor_sel;
    uint8_t    set_temp;            /* what the stat reports */
    uint8_t    frost_temp;
    bool       frost_enabled;
    hm_mode_t  mode;
    bool       heating;
    bool       locked;
    uint16_t   hold_min;
    uint16_t   holiday_h;
    uint8_t    error_code;          /* 0 = none; 0xE0 air, 0xE1 floor, 0xE2 remote sensor */
    bool       fahrenheit;
    bool       hw_on;               /* PRTHW: hot water is on now */
    uint8_t    prog_mode;           /* 0 = 5/2, 1 = 7 day */

    /* The stat's programme: comfort levels and, on a PRTHW, hot water
     * timers. One entry per programme day: 2 (weekday, weekend) in 5/2
     * mode, 7 (Mon-Sun) in 7-day mode; 0 days if the DCB is too short.
     * While a day is being written (pend.comfort / pend.hw_timer) it holds
     * the values asked for. */
    uint8_t     comfort_days;
    hm_period_t comfort[7][4];
    uint8_t     hw_timer_days;
    hm_period_t hw_on_at[7][4];
    hm_period_t hw_off_at[7][4];

    /* The stat's own clock, as last read. wday 1-7 = Mon-Sun, 0 = none. */
    uint8_t    clk_wday, clk_hour, clk_min;

    /* Hot water as the bridge last set it (ZONE_HW_*), and the minutes left
     * of a timed override: a boost, or a forced off that ends when the timer
     * period ends. 0 = no end time (or not known yet after a restart). */
    uint8_t    hw_mode;
    uint16_t   boost_left_min;

    zone_pending_t pend;            /* filled by the zone store */
} zone_view_t;

/* Hot water modes the bridge can put a PRTHW in. */
#define ZONE_HW_TIMER   0           /* following its timer */
#define ZONE_HW_ON      1           /* forced on until changed */
#define ZONE_HW_OFF     2           /* forced off until changed, or until boost_left_min runs out */
#define ZONE_HW_BOOST   3           /* forced on, back to the timer when the boost ends */

/* Fill the live part of *v from a stat's scheduler state. cfg and pend are
 * left alone. now_ms is the scheduler clock. */
void zone_view_from_stat(const hm_stat_t *st, uint32_t now_ms, zone_view_t *v);

/* Setpoint and mode the screen should show: the pending request if there is
 * one, else what the stat reports. */
uint8_t   zone_view_shown_set(const zone_view_t *v);
hm_mode_t zone_view_shown_mode(const zone_view_t *v);
bool      zone_view_shown_lock(const zone_view_t *v);
bool      zone_view_busy(const zone_view_t *v);

/* "20.5" (and "--.-" when absent). */
void zone_fmt_t10(int16_t t10, char *buf, size_t cap);

/* Short state line for a tile, e.g. "Heating", "Frost", "Off", "Offline". */
const char *zone_state_str(const zone_view_t *v);

/* "Air sensor fault" etc. for a non-zero error code, or NULL. */
const char *zone_error_str(uint8_t error_code);

/* Which sensor the room temperature comes from: "Air", "Remote", "Floor". */
const char *zone_sensor_str(uint8_t sensor_sel);

/* Default name for a new zone: "Zone 3". */
void zone_default_name(uint8_t addr, uint8_t model, char *buf, size_t cap);

/* Copy a name, keeping printable ASCII only and at most ZONE_NAME_MAX bytes.
 * Leading and trailing spaces go. Returns false if nothing is left. */
bool zone_clean_name(const char *in, char out[ZONE_NAME_MAX + 1]);

/* ---- programme -------------------------------------------------------------
 * Days count 0..days-1 as the stat's programme does: 5/2 has weekday (0) and
 * weekend (1); 7-day has Mon (0) to Sun (6). A period with hour 24 is not
 * used. */

/* Programme day for a weekday (1-7 = Mon-Sun) given the number of days. */
int zone_prog_day(uint8_t days, int wday);

/* "Weekdays", "Weekend", "Mon"... */
const char *zone_prog_day_name(uint8_t days, int day);

/* Comfort level in force at minute m (0-1439) of a day: the last level that
 * started at or before m; before the first, the day's last level (the
 * evening level carries over midnight). 0 if the day has no levels. */
uint8_t zone_comfort_at(const hm_period_t p[4], int m);

/* Whether hot water is on at minute m given a day's on/off pairs. */
bool zone_hw_timer_on(const hm_period_t on[4], const hm_period_t off[4], int m);

/* The next on or off time after minute m on the same day, in minutes, or -1.
 * *on_after says which. */
int zone_hw_timer_next(const hm_period_t on[4], const hm_period_t off[4], int m, bool *on_after);

/* What a plain hot water on/off switch (Matter's On/Off) should do, so the
 * water is never left forced on or off with no end:
 * - On: nothing if the water is on already (timer, boost or forced on);
 *   back to the timer if it is held off while the timer is on; otherwise a
 *   boost of boost_min minutes.
 * - Off: nothing if already forced off; if the timer has the water on now,
 *   forced off until that timer period ends; otherwise back to the timer.
 * wday (1-7 = Mon-Sun) and minute (0-1439) are the time now, wday 0 if not
 * known; then Off forces off for boost_min minutes. Returns false when there
 * is nothing to do. */
typedef struct {
    int      cmd;                   /* HM_HW_FOLLOW_TIMER / FORCE_ON / FORCE_OFF */
    uint8_t  mode;                  /* ZONE_HW_* */
    unsigned minutes;               /* end of the override, 0 = none */
} zone_hw_plan_t;
bool zone_hw_switch_plan(const zone_view_t *v, bool on, int wday, int minute, unsigned boost_min,
                         zone_hw_plan_t *out);

/* True if the periods are in time order with the unused ones last, as the
 * stat needs them. */
bool zone_periods_ok(const hm_period_t p[4]);

/* Sort periods into time order, unused ones last (the editors use this
 * before a write). on/off pairs move together when off is not NULL. */
void zone_periods_sort(hm_period_t p[4], hm_period_t *off);

/* ---- saved zone list (NVS blob "zones/list") -----------------------------
 * Version 1: [version=1][count] then count entries of
 * [addr][model][endpoint lo][endpoint hi][name, 17 bytes NUL padded].
 * Entries are sorted by address and an address appears once. */
#define ZONE_LIST_VERSION     1
#define ZONE_LIST_ENTRY_SIZE  (4 + ZONE_NAME_MAX + 1)
#define ZONE_LIST_MAX_BYTES   (2 + HM_SCHED_MAX_STATS * ZONE_LIST_ENTRY_SIZE)

size_t zone_list_encode(const zone_cfg_t *z, size_t n, uint8_t *out, size_t cap);

/* Returns the number of zones decoded, or -1 if the blob is not a version
 * this firmware understands or is malformed. */
int zone_list_decode(const uint8_t *in, size_t len, zone_cfg_t *z, size_t cap);

/* ---- saved hot water overrides (NVS blob "zones/hw") ---------------------
 * Version 1: [version=1][count] then count entries of
 * [addr][mode][end, UTC seconds, 4 bytes little endian]. Only stats whose
 * hot water is not simply following its timer are saved. end 0 = no end
 * time, or one that started before the clock was set. A ZONE_HW_TIMER entry
 * with an end is a "back to the timer" write still to be retried. */
#define ZONE_HW_BLOB_VERSION  1
#define ZONE_HW_ENTRY_SIZE    6
#define ZONE_HW_BLOB_MAX      (2 + HM_SCHED_MAX_STATS * ZONE_HW_ENTRY_SIZE)

typedef struct {
    uint8_t  addr;
    uint8_t  mode;                  /* ZONE_HW_* */
    uint32_t end_utc;
} zone_hw_saved_t;

size_t zone_hw_encode(const zone_hw_saved_t *e, size_t n, uint8_t *out, size_t cap);
/* Number of entries, or -1 if the blob is not understood. */
int zone_hw_decode(const uint8_t *in, size_t len, zone_hw_saved_t *e, size_t cap);

#ifdef __cplusplus
}
#endif
