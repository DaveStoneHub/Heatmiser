/*
 * The bus task: runs the hm_sched scheduler on the RS485 bus. It owns the
 * polling; other tasks queue writes, start scans and read state through the
 * functions here, which are thread safe.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "hm_sched.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Create the task, paused. Settings come from Kconfig. */
esp_err_t hm_poller_init(void);

/* Pause or resume polling. Queued writes and scans also wait while paused. */
void hm_poller_run(bool on);
bool hm_poller_running(void);

int  hm_poller_add(uint8_t addr);
int  hm_poller_remove(uint8_t addr);
void hm_poller_scan(void);
bool hm_poller_scanning(void);
/* Scan progress: pass (1 or 2) and the address being probed. Returns
 * hm_poller_scanning(). */
bool hm_poller_scan_state(uint8_t *pass, uint8_t *next);
void hm_poller_set_interval(uint32_t ms);
uint32_t hm_poller_interval(void);

/* Queue a write; see hm_sched_write. Wakes the task. */
int hm_poller_write(uint8_t stat, const hm_write_t *w, uint32_t *id);

/* Wait for a queued write to finish. Returns true with its WRITE_DONE event,
 * or false on timeout. */
bool hm_poller_wait_write(uint32_t id, uint32_t timeout_ms, hm_event_t *out);

bool     hm_poller_get(uint8_t addr, hm_stat_t *out);
uint32_t hm_poller_list(void);
size_t   hm_poller_queue_len(void);

/* Zero one stat's bus counters (0 = all), or poll one at the next slot. */
void hm_poller_clear_counters(uint8_t addr);
int  hm_poller_poll_now(uint8_t addr);

/* The last bus frames, for the diagnostics screen: each request (tx) and
 * what came back. Bytes beyond HM_TRACE_BYTES are left out. */
#define HM_TRACE_BYTES 10
#define HM_TRACE_LEN   12
typedef struct {
    uint32_t  ms;
    bool      tx;
    hm_xfer_t result;        /* rx only */
    uint16_t  len;           /* whole frame */
    uint8_t   n;             /* bytes in b */
    uint8_t   b[HM_TRACE_BYTES];
    uint16_t  timeout_ms;    /* rx timeouts */
} hm_trace_t;
/* Copies up to max entries, oldest first. */
size_t hm_poller_trace(hm_trace_t *out, size_t max);

/* Listeners run on the bus task: keep them short and never block. */
typedef void (*hm_poller_listener_t)(const hm_event_t *ev, void *arg);
esp_err_t hm_poller_listen(hm_poller_listener_t fn, void *arg);

/* Print scheduler events to the log (on by default). */
void hm_poller_log_events(bool on);

#ifdef __cplusplus
}
#endif
