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

/* Listeners run on the bus task: keep them short and never block. */
typedef void (*hm_poller_listener_t)(const hm_event_t *ev, void *arg);
esp_err_t hm_poller_listen(hm_poller_listener_t fn, void *arg);

/* Print scheduler events to the log (on by default). */
void hm_poller_log_events(bool on);

#ifdef __cplusplus
}
#endif
