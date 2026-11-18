#include "hm_poller.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hm_bus.h"

static const char *TAG = "hm_poll";

#define MAX_LISTENERS 4
#define DONE_RING     16

static hm_sched_t *s_sched;
static SemaphoreHandle_t s_lock;     /* the scheduler's tables */
static SemaphoreHandle_t s_ev_lock;  /* listeners and the done ring */
static TaskHandle_t s_task;
static volatile bool s_running;
static volatile bool s_log = true;

static struct {
    hm_poller_listener_t fn;
    void *arg;
} s_listeners[MAX_LISTENERS];

static hm_event_t s_done[DONE_RING];
static size_t s_done_next;

static portMUX_TYPE s_trace_mux = portMUX_INITIALIZER_UNLOCKED;
static hm_trace_t s_trace[HM_TRACE_LEN];
static size_t s_trace_next, s_trace_n;

static void trace(bool tx, hm_xfer_t r, const uint8_t *b, size_t len, uint32_t timeout_ms)
{
    hm_trace_t e = {
        .ms = (uint32_t)(esp_timer_get_time() / 1000),
        .tx = tx,
        .result = r,
        .len = (uint16_t)len,
        .n = (uint8_t)(len < HM_TRACE_BYTES ? len : HM_TRACE_BYTES),
        .timeout_ms = (uint16_t)(timeout_ms > 65535 ? 65535 : timeout_ms),
    };
    if (b && e.n) {
        memcpy(e.b, b, e.n);
    } else {
        e.n = 0;
    }
    portENTER_CRITICAL(&s_trace_mux);
    s_trace[s_trace_next] = e;
    s_trace_next = (s_trace_next + 1) % HM_TRACE_LEN;
    if (s_trace_n < HM_TRACE_LEN) {
        s_trace_n++;
    }
    portEXIT_CRITICAL(&s_trace_mux);
}

/* ---- scheduler I/O ------------------------------------------------------ */

static hm_xfer_t io_xfer(void *ctx, const uint8_t *req, size_t req_len, uint8_t stat,
                         uint32_t timeout_ms, hm_reply_t *reply)
{
    (void)ctx;
    static hm_txn_t t; /* only the bus task calls this */
    trace(true, HM_XFER_OK, req, req_len, 0);
    hm_xfer_t r;
    if (hm_bus_transact_timeout(req, req_len, stat, timeout_ms, &t) != ESP_OK) {
        r = HM_XFER_BAD;
    } else {
        switch (t.result) {
        case HM_TXN_OK:
            *reply = t.reply; /* data points into t.rx, valid until the next call */
            r = HM_XFER_OK;
            break;
        case HM_TXN_TIMEOUT:
        case HM_TXN_ECHO_ONLY:
            r = HM_XFER_TIMEOUT;
            break;
        case HM_TXN_BAD_FRAME:
            r = t.proto == HM_ERR_CRC ? HM_XFER_CRC : HM_XFER_BAD;
            break;
        default:
            r = HM_XFER_BAD;
            break;
        }
    }
    trace(false, r, t.rx, r == HM_XFER_TIMEOUT ? 0 : t.rx_len, timeout_ms);
    return r;
}

static uint32_t io_now(void *ctx)
{
    (void)ctx;
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void io_lock(void *ctx)
{
    (void)ctx;
    xSemaphoreTake(s_lock, portMAX_DELAY);
}

static void io_unlock(void *ctx)
{
    (void)ctx;
    xSemaphoreGive(s_lock);
}

static void log_event(const hm_event_t *ev)
{
    char what[40];
    switch (ev->type) {
    case HM_EV_ONLINE:
        ESP_LOGI(TAG, "stat %u online", ev->stat);
        break;
    case HM_EV_OFFLINE:
        ESP_LOGW(TAG, "stat %u OFFLINE (retrying every %u s)", ev->stat,
                 (unsigned)(CONFIG_HM_POLL_OFFLINE_RETRY_MS / 1000));
        break;
    case HM_EV_SCAN_FOUND:
        ESP_LOGI(TAG, "scan: stat %u found, %s", ev->stat, hm_model_str(ev->model));
        break;
    case HM_EV_SCAN_DONE:
        ESP_LOGI(TAG, "scan done: %d stat(s)", __builtin_popcount(ev->found_mask));
        break;
    case HM_EV_WRITE_DONE:
        hm_write_describe(&ev->write, what, sizeof what);
        if (ev->wdone == HM_WDONE_OK) {
            ESP_LOGI(TAG, "stat %u %s: %s (%u attempt%s)", ev->stat, what,
                     hm_wdone_str(ev->wdone), ev->attempts, ev->attempts == 1 ? "" : "s");
        } else if (ev->wdone != HM_WDONE_SUPERSEDED) {
            ESP_LOGW(TAG, "stat %u %s: %s after %u attempt(s)", ev->stat, what,
                     hm_wdone_str(ev->wdone), ev->attempts);
        }
        break;
    default:
        break;
    }
}

static void io_event(void *ctx, const hm_event_t *ev)
{
    (void)ctx;
    if (s_log) {
        log_event(ev);
    }
    xSemaphoreTake(s_ev_lock, portMAX_DELAY);
    if (ev->type == HM_EV_WRITE_DONE) {
        s_done[s_done_next] = *ev;
        s_done_next = (s_done_next + 1) % DONE_RING;
    }
    for (int i = 0; i < MAX_LISTENERS; i++) {
        if (s_listeners[i].fn) {
            s_listeners[i].fn(ev, s_listeners[i].arg);
        }
    }
    xSemaphoreGive(s_ev_lock);
}

/* ---- task --------------------------------------------------------------- */

static void poller_task(void *arg)
{
    (void)arg;
    for (;;) {
        if (!s_running) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }
        /* Follow the bench 'master' command. */
        io_lock(NULL);
        s_sched->cfg.master = hm_bus_config()->master_addr;
        io_unlock(NULL);
        hm_step_t did;
        uint32_t wait = hm_sched_step(s_sched, &did);
        if (did == HM_STEP_IDLE) {
            /* Sleep until the next stat is due, or a write or scan wakes us. */
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wait ? wait : 1));
        }
    }
}

static void wake(void)
{
    if (s_task) {
        xTaskNotifyGive(s_task);
    }
}

esp_err_t hm_poller_init(void)
{
    if (s_sched) {
        return ESP_OK;
    }
    s_lock = xSemaphoreCreateMutex();
    s_ev_lock = xSemaphoreCreateMutex();
    s_sched = heap_caps_calloc(1, sizeof(*s_sched), MALLOC_CAP_8BIT);
    if (!s_lock || !s_ev_lock || !s_sched) {
        return ESP_ERR_NO_MEM;
    }
    hm_sched_config_t cfg = HM_SCHED_CONFIG_DEFAULT();
    cfg.master = hm_bus_config()->master_addr;
    cfg.poll_interval_ms = CONFIG_HM_POLL_INTERVAL_MS;
    cfg.read_timeout_ms = hm_bus_config()->reply_timeout_ms;
    cfg.write_timeout_ms = CONFIG_HM_POLL_WRITE_TIMEOUT_MS;
    cfg.write_retries = CONFIG_HM_POLL_WRITE_RETRIES;
    cfg.offline_after = CONFIG_HM_POLL_OFFLINE_AFTER;
    cfg.offline_retry_ms = CONFIG_HM_POLL_OFFLINE_RETRY_MS;
    cfg.scan_timeout_ms = CONFIG_HM_POLL_SCAN_TIMEOUT_MS;
    cfg.scan_recheck_ms = CONFIG_HM_POLL_SCAN_RECHECK_MS;
    const hm_sched_io_t io = {
        .xfer = io_xfer, .now_ms = io_now, .event = io_event,
        .lock = io_lock, .unlock = io_unlock, .ctx = NULL,
    };
    hm_sched_init(s_sched, &cfg, &io);

    /* Core 0, as the design's task table has it; the UI will take core 1. */
    if (xTaskCreatePinnedToCore(poller_task, "hm_bus", 6144, NULL, CONFIG_HM_POLL_TASK_PRIORITY,
                                &s_task, 0) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "poller ready (paused): interval %u ms, master 0x%02X", (unsigned)cfg.poll_interval_ms,
             cfg.master);
    return ESP_OK;
}

void hm_poller_run(bool on)
{
    s_running = on;
    wake();
}

bool hm_poller_running(void)
{
    return s_running;
}

int hm_poller_add(uint8_t addr)
{
    int rc = hm_sched_add(s_sched, addr);
    wake();
    return rc;
}

int hm_poller_remove(uint8_t addr)
{
    return hm_sched_remove(s_sched, addr);
}

void hm_poller_scan(void)
{
    hm_sched_scan(s_sched);
    wake();
}

bool hm_poller_scanning(void)
{
    return hm_sched_scanning(s_sched);
}

bool hm_poller_scan_state(uint8_t *pass, uint8_t *next)
{
    io_lock(NULL);
    bool on = s_sched->scanning;
    if (pass) {
        *pass = s_sched->scan_pass;
    }
    if (next) {
        *next = s_sched->scan_next;
    }
    io_unlock(NULL);
    return on;
}

void hm_poller_set_interval(uint32_t ms)
{
    hm_sched_set_interval(s_sched, ms);
    wake();
}

uint32_t hm_poller_interval(void)
{
    io_lock(NULL);
    uint32_t v = s_sched->cfg.poll_interval_ms;
    io_unlock(NULL);
    return v;
}

int hm_poller_write(uint8_t stat, const hm_write_t *w, uint32_t *id)
{
    int rc = hm_sched_write(s_sched, stat, w, id);
    wake();
    return rc;
}

bool hm_poller_wait_write(uint32_t id, uint32_t timeout_ms, hm_event_t *out)
{
    const int64_t end = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    for (;;) {
        bool found = false;
        xSemaphoreTake(s_ev_lock, portMAX_DELAY);
        for (int i = 0; i < DONE_RING; i++) {
            if (s_done[i].type == HM_EV_WRITE_DONE && s_done[i].write_id == id) {
                if (out) {
                    *out = s_done[i];
                }
                found = true;
                break;
            }
        }
        xSemaphoreGive(s_ev_lock);
        if (found) {
            return true;
        }
        if (esp_timer_get_time() >= end) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

bool hm_poller_get(uint8_t addr, hm_stat_t *out)
{
    return hm_sched_get(s_sched, addr, out);
}

uint32_t hm_poller_list(void)
{
    return hm_sched_list(s_sched);
}

size_t hm_poller_queue_len(void)
{
    return hm_sched_queue_len(s_sched);
}

void hm_poller_clear_counters(uint8_t addr)
{
    hm_sched_clear_counters(s_sched, addr);
}

int hm_poller_poll_now(uint8_t addr)
{
    int rc = hm_sched_poll_now(s_sched, addr);
    wake();
    return rc;
}

size_t hm_poller_trace(hm_trace_t *out, size_t max)
{
    portENTER_CRITICAL(&s_trace_mux);
    size_t n = s_trace_n < max ? s_trace_n : max;
    size_t first = (s_trace_next + HM_TRACE_LEN - n) % HM_TRACE_LEN;
    for (size_t i = 0; i < n; i++) {
        out[i] = s_trace[(first + i) % HM_TRACE_LEN];
    }
    portEXIT_CRITICAL(&s_trace_mux);
    return n;
}

esp_err_t hm_poller_listen(hm_poller_listener_t fn, void *arg)
{
    esp_err_t err = ESP_ERR_NO_MEM;
    xSemaphoreTake(s_ev_lock, portMAX_DELAY);
    for (int i = 0; i < MAX_LISTENERS; i++) {
        if (!s_listeners[i].fn) {
            s_listeners[i].fn = fn;
            s_listeners[i].arg = arg;
            err = ESP_OK;
            break;
        }
    }
    xSemaphoreGive(s_ev_lock);
    return err;
}

void hm_poller_log_events(bool on)
{
    s_log = on;
}
