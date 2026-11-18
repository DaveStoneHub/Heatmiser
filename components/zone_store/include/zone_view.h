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
} zone_cfg_t;

/* A write the user asked for that the stat has not confirmed yet. */
typedef struct {
    bool     set_temp, mode, lock, hold, hw;
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
    uint8_t    prog_mode;

    zone_pending_t pend;            /* filled by the zone store */
} zone_view_t;

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

/* Default name for a new zone: "Stat 3" or "Hot water stat 3". */
void zone_default_name(uint8_t addr, uint8_t model, char *buf, size_t cap);

/* Copy a name, keeping printable ASCII only and at most ZONE_NAME_MAX bytes.
 * Leading and trailing spaces go. Returns false if nothing is left. */
bool zone_clean_name(const char *in, char out[ZONE_NAME_MAX + 1]);

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

#ifdef __cplusplus
}
#endif
