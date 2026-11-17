/*
 * Bus scheduler: the single RS485 master's brain (design doc, "Polling and
 * bus scheduling").
 *
 *  1. Write queue first. A write is acked, then the stat's full DCB is read
 *     back at once and checked; a write that is not acked, or does not read
 *     back, is retried (5 retries by default) and then reported as failed.
 *  2. Round-robin poll. Each stat's full DCB is read once per poll interval.
 *     A failed read is retried once in the same slot before it counts as a
 *     miss.
 *  3. Offline back-off. After 3 misses in a row a stat is offline and is only
 *     tried once a minute until it answers again.
 *  4. Scan. Probes addresses 1-32 with a 5-byte read and a short timeout, then
 *     re-checks the misses with a longer one. Polling pauses during a scan;
 *     writes still go first.
 *
 * Pure C and single threaded: hm_sched_step() runs one slot (a write with its
 * read-back, a poll, or one scan probe) and returns. The transport, clock,
 * events and an optional lock come in through hm_sched_io_t, so the same code
 * runs on the board and in the host tests. The lock is held only while the
 * scheduler's tables change, never across a bus transaction, so other tasks
 * can queue writes and read state while a slot is on the wire.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hm_dcb.h"
#include "hm_frame.h"
#include "hm_write.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HM_SCHED_MAX_STATS  32
#define HM_SCHED_QUEUE_LEN  16
#define HM_SCAN_PROBE_LEN   5    /* addresses 0-4: length, vendor, version, model */

typedef enum {
    HM_XFER_OK = 0,
    HM_XFER_TIMEOUT,   /* nothing came back */
    HM_XFER_CRC,       /* a frame came back with a bad CRC */
    HM_XFER_BAD,       /* partial, wrong source, bad length, driver error */
} hm_xfer_t;

typedef enum {
    HM_LINK_UNKNOWN = 0,  /* added, not heard from yet */
    HM_LINK_ONLINE,
    HM_LINK_OFFLINE,
} hm_link_t;

typedef enum {
    HM_WDONE_OK = 0,      /* acked and confirmed by read-back (or acked, if not checkable) */
    HM_WDONE_NO_ACK,      /* never acked after all retries */
    HM_WDONE_MISMATCH,    /* acked, but the stat kept reporting another value */
    HM_WDONE_NO_READBACK, /* acked, but the read-back failed every time */
    HM_WDONE_SUPERSEDED,  /* a newer write to the same field replaced it in the queue */
    HM_WDONE_CANCELLED,   /* the stat was removed */
} hm_wdone_t;

typedef enum {
    HM_EV_UPDATED = 0,    /* a poll or read-back stored a new DCB; `changed` ignores the clock */
    HM_EV_ONLINE,
    HM_EV_OFFLINE,
    HM_EV_WRITE_DONE,
    HM_EV_SCAN_FOUND,
    HM_EV_SCAN_DONE,
} hm_event_type_t;

typedef struct {
    hm_event_type_t type;
    uint8_t    stat;
    uint8_t    model;       /* SCAN_FOUND, UPDATED */
    bool       changed;     /* UPDATED */
    uint32_t   write_id;    /* WRITE_DONE */
    hm_wdone_t wdone;       /* WRITE_DONE */
    uint8_t    attempts;    /* WRITE_DONE: frames sent */
    hm_write_t write;       /* WRITE_DONE */
    uint32_t   found_mask;  /* SCAN_DONE: bit (addr-1) set for each stat found */
} hm_event_t;

typedef struct {
    /* One request, one reply. On HM_XFER_OK, *reply must describe a valid
     * frame from `stat`, and reply->data must stay valid until the next call. */
    hm_xfer_t (*xfer)(void *ctx, const uint8_t *req, size_t req_len, uint8_t stat,
                      uint32_t timeout_ms, hm_reply_t *reply);
    uint32_t (*now_ms)(void *ctx);
    void (*event)(void *ctx, const hm_event_t *ev);  /* optional; called with the lock released */
    void (*lock)(void *ctx);                         /* optional */
    void (*unlock)(void *ctx);
    void *ctx;
} hm_sched_io_t;

typedef struct {
    uint8_t  master;
    uint32_t poll_interval_ms;     /* each stat is read once per interval (default 10 s) */
    uint32_t read_timeout_ms;      /* 1000 */
    uint32_t write_timeout_ms;     /* 1000 */
    uint8_t  read_retries;         /* 1: retry once in the same slot */
    uint8_t  write_retries;        /* 5, as the V3 document allows */
    uint8_t  offline_after;        /* 3 misses in a row */
    uint32_t offline_retry_ms;     /* 60 s */
    uint32_t scan_timeout_ms;      /* first pass; replies start about 7 ms after the request */
    uint32_t scan_recheck_ms;      /* second pass over the misses; 0 = no second pass */
    bool     scan_auto_add;        /* add every stat a scan finds */
} hm_sched_config_t;

#define HM_SCHED_CONFIG_DEFAULT() {   \
    .master = 0x81,                   \
    .poll_interval_ms = 10000,        \
    .read_timeout_ms = 1000,          \
    .write_timeout_ms = 1000,         \
    .read_retries = 1,                \
    .write_retries = 5,               \
    .offline_after = 3,               \
    .offline_retry_ms = 60000,        \
    .scan_timeout_ms = 100,           \
    .scan_recheck_ms = 300,           \
    .scan_auto_add = true,            \
}

typedef struct {
    uint32_t polls, poll_ok, misses;
    uint32_t timeouts, crc, bad;          /* per frame, retries included */
    uint32_t writes_ok, writes_failed, write_retries;
} hm_stat_counters_t;

typedef struct {
    bool      used;
    uint8_t   addr;
    hm_link_t link;
    uint8_t   misses;          /* consecutive */
    uint32_t  last_ok_ms;
    uint32_t  next_poll_ms;
    bool      have_dcb;
    uint16_t  dcb_len;
    uint8_t   dcb[HM_DCB_MAX_LEN];
    hm_stat_counters_t cnt;
} hm_stat_t;

typedef struct {
    bool       used;
    bool       in_flight;
    uint32_t   id;
    uint8_t    stat;
    uint8_t    attempts;
    hm_wdone_t last_fail;
    hm_write_t w;
} hm_wq_entry_t;

typedef struct {
    hm_sched_config_t cfg;
    hm_sched_io_t     io;
    hm_stat_t         stats[HM_SCHED_MAX_STATS];   /* slot = addr - 1 */
    hm_wq_entry_t     q[HM_SCHED_QUEUE_LEN];       /* FIFO, q[0] is the head */
    uint8_t           q_len;
    uint32_t          next_id;
    uint8_t           rr;                          /* round-robin cursor */
    /* scan */
    bool              scanning;
    uint8_t           scan_pass;                   /* 1 or 2 */
    uint8_t           scan_next;                   /* next address to probe */
    uint32_t          scan_found;
} hm_sched_t;

typedef enum {
    HM_STEP_IDLE = 0,
    HM_STEP_WRITE,
    HM_STEP_POLL,
    HM_STEP_SCAN,
} hm_step_t;

void hm_sched_init(hm_sched_t *s, const hm_sched_config_t *cfg, const hm_sched_io_t *io);

/* Add or remove a stat (1-32). Removing cancels its queued writes. Return 0,
 * or -1 on a bad address. */
int hm_sched_add(hm_sched_t *s, uint8_t addr);
int hm_sched_remove(hm_sched_t *s, uint8_t addr);

/* Queue a write. A queued (not yet sent) write to the same stat and address is
 * replaced, and reported SUPERSEDED. Returns 0 and the write's id, -1 if the
 * queue is full, -2 on a bad stat or write. The stat need not be added. */
int hm_sched_write(hm_sched_t *s, uint8_t stat, const hm_write_t *w, uint32_t *id);

/* Start a scan of addresses 1-32 (restarts one in progress). */
void hm_sched_scan(hm_sched_t *s);
bool hm_sched_scanning(hm_sched_t *s);

void hm_sched_set_interval(hm_sched_t *s, uint32_t poll_interval_ms);

/* Run one slot. *did says what ran. Returns the milliseconds until something
 * is due (0 = call again now). */
uint32_t hm_sched_step(hm_sched_t *s, hm_step_t *did);

/* Copy of one stat's state. Returns false if the address is not added. */
bool hm_sched_get(hm_sched_t *s, uint8_t addr, hm_stat_t *out);

/* Bit (addr-1) set for every added stat. */
uint32_t hm_sched_list(hm_sched_t *s);

size_t hm_sched_queue_len(hm_sched_t *s);

const char *hm_link_str(hm_link_t l);
const char *hm_wdone_str(hm_wdone_t r);

#ifdef __cplusplus
}
#endif
