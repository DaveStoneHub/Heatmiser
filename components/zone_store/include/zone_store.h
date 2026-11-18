/*
 * The zone store: the bridge's list of zones (stats) with their names, and a
 * live view of each one, between the bus task and the screen (and, from
 * milestone 4, Matter).
 *
 * - The saved list (address, model, name, endpoint ID) lives in NVS and is
 *   restored at boot; every saved zone is added to the poller.
 * - The poller's events keep each zone's view current. Readers take a copy
 *   with zone_store_get(); zone_store_take_dirty() says which zones changed.
 * - Change requests (setpoint, mode, key lock, hold, hot water) are queued
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
uint32_t zone_store_take_dirty(void);

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

/* Messages for the user: a failed write, a scan result. Returns false when
 * there are none. */
bool zone_store_pop_notice(char *buf, size_t cap);

const char *zone_err_str(zone_err_t e);

#ifdef __cplusplus
}
#endif
