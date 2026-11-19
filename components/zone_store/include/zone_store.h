/*
 * The zone store: the bridge's list of zones (stats) with their names, and a
 * live view of each one, between the bus task, the screen and Matter.
 *
 * - The saved list (address, model, name, endpoint ID) lives in NVS and is
 *   restored at boot; every saved zone is added to the poller.
 * - The poller's events keep each zone's view current. Readers take a copy
 *   with zone_store_get(); zone_store_take_dirty() says which zones changed.
 * - Change requests (setpoint, mode, key lock, hold, hot water, programme)
 *   are queued
 *   on the bus. Until the stat confirms one, the view carries it as pending,
 *   so the screen can show what was asked for. A write that fails is dropped,
 *   the view falls back to what the stat reports, and a notice is posted.
 *
 * All functions are thread safe and never block on the bus. The UI preview
 * on the desktop links a fake with the same interface.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "zone_view.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ZONE_OK = 0,
    ZONE_ERR_ADDR,      /* not a configured zone, or out of 1-32 */
    ZONE_ERR_RANGE,     /* value out of the stat's range */
    ZONE_ERR_OFFLINE,   /* the stat is offline; the change was not queued */
    ZONE_ERR_BUSY,      /* the bus queue is full */
    ZONE_ERR_MODEL,     /* the stat has no such function */
    ZONE_ERR_STORAGE,   /* saving the zone list failed */
} zone_err_t;

/* Restore the saved list, add every zone to the poller and start polling.
 * Needs nvs_flash_init() and hm_poller_init() first. */
int zone_store_init(void);

/* Bit (addr-1) for every configured zone. */
uint32_t zone_store_list(void);
size_t   zone_store_count(void);

/* Copy of one zone's view. False if addr is not configured. */
bool zone_store_get(uint8_t addr, zone_view_t *out);

/* Bits of the zones whose view changed since the last call, and clear them.
 * Bit 31 (ZONE_DIRTY_LIST) is set when zones were added or removed. */
#define ZONE_DIRTY_LIST (1u << 31)
uint32_t zone_store_take_dirty(void);   /* the screen's copy */

/* Each reader has its own copy of the dirty bits. */
typedef enum { ZONE_READER_UI = 0, ZONE_READER_MATTER, ZONE_READERS } zone_reader_t;
uint32_t zone_store_take_dirty_for(zone_reader_t reader);

/* Save the Matter endpoint ID of a zone's thermostat (0 = none yet). */
zone_err_t zone_store_set_endpoint(uint8_t addr, uint16_t endpoint);

/* Zone list. add does nothing if the zone exists. */
zone_err_t zone_store_add(uint8_t addr);
zone_err_t zone_store_remove(uint8_t addr);
zone_err_t zone_store_rename(uint8_t addr, const char *name);

/* Changes, queued on the bus. */
zone_err_t zone_store_set_temp(uint8_t addr, int celsius);
zone_err_t zone_store_set_mode(uint8_t addr, hm_mode_t mode);
zone_err_t zone_store_set_lock(uint8_t addr, bool locked);
zone_err_t zone_store_set_hold(uint8_t addr, unsigned minutes);
zone_err_t zone_store_set_hw(uint8_t addr, int cmd);  /* HM_HW_FOLLOW_TIMER etc. */

/* Hot water on for this many minutes, then back to the timer (0 ends a
 * boost now). The bridge times the boost; if it restarts mid-boost, the stat
 * goes back to its timer at boot. */
zone_err_t zone_store_hw_boost(uint8_t addr, unsigned minutes);

/* One programme day (see zone_view.h): four comfort levels, or four hot
 * water on/off pairs. Periods must be in time order, unused ones (hour 24)
 * last. */
zone_err_t zone_store_set_comfort(uint8_t addr, int day, const hm_period_t p[4]);
zone_err_t zone_store_set_hw_timer(uint8_t addr, int day, const hm_period_t on[4],
                                   const hm_period_t off[4]);

/* Every online stat: set its clock to the bridge's local time, or start a
 * holiday of this many hours (0 ends it). Return how many stats the change
 * was queued for, or -1 for clocks when the bridge's own clock is not set. */
int zone_store_sync_clocks(void);
int zone_store_holiday_all(unsigned hours);

/* Scan addresses 1-32. Stats that answer join the list; zones that do not
 * answer stay (remove them by hand). */
void zone_store_scan(void);
typedef struct {
    bool    running;
    uint8_t pass;        /* 1 or 2 */
    uint8_t next;        /* address being probed */
    uint8_t found;       /* stats answering so far */
    uint8_t added;       /* of which new to the list */
} zone_scan_t;
void zone_store_scan_state(zone_scan_t *out);

/* ---- the bus, for the diagnostics and settings screens -------------------- */

typedef struct {
    uint32_t polls, poll_ok, timeouts, crc, bad;
    uint32_t writes_ok, writes_failed;
    bool     seen;               /* answered since boot */
    uint32_t seen_age_s;
} zone_diag_t;
bool zone_store_diag(uint8_t addr, zone_diag_t *out);

typedef enum { ZONE_RX_OK = 0, ZONE_RX_TIMEOUT, ZONE_RX_CRC, ZONE_RX_BAD } zone_rx_t;
typedef struct {
    uint32_t  age_ms;
    bool      tx;
    zone_rx_t rx;                /* rx only */
    uint16_t  len;               /* whole frame */
    uint8_t   n;                 /* bytes in b */
    uint8_t   b[10];
    uint16_t  timeout_ms;
} zone_frame_t;
/* The last bus frames, oldest first. */
size_t zone_store_frames(zone_frame_t *out, size_t max);

void zone_store_clear_counters(void);
/* Read this stat at the next free slot. */
zone_err_t zone_store_read_now(uint8_t addr);

typedef struct {
    uint32_t poll_ms;
    uint8_t  master;
    uint32_t baud;
} zone_bus_info_t;
void zone_store_bus_info(zone_bus_info_t *out);
/* Poll interval, 2-120 s, saved. */
zone_err_t zone_store_set_poll_interval(uint32_t ms);

/* Messages for the user: a failed write, a scan result. Returns false when
 * there are none. */
bool zone_store_pop_notice(char *buf, size_t cap);

const char *zone_err_str(zone_err_t e);

#ifdef __cplusplus
}
#endif
