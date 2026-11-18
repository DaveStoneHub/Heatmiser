/*
 * The zone store on the board: NVS persistence and the poller listener.
 *
 * Locking. The poller calls listeners with its event lock held, from the bus
 * task or, for writes replaced in the queue, from the caller of
 * hm_poller_write(). So this file never calls a poller function that can emit
 * events (write, add, remove, scan) while holding s_mu; hm_poller_get() emits
 * nothing and is safe under it. Lock order: poller events -> s_mu -> poller
 * tables.
 */
#include "zone_store.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "hm_poller.h"
#include "hm_write.h"
#include "nvs.h"

static const char *TAG = "zones";

#define NOTICES     8
#define NOTICE_LEN  96
#define NVS_NS      "zones"
#define NVS_KEY     "list"

/* Pending write ids per zone. 0 = queued but its id is not known yet; the
 * listener then matches on the field's address instead. */
typedef struct {
    uint32_t set, lock, hold, hw;
    uint32_t mode_last;      /* the last write of a mode change */
    uint32_t mode_first;     /* the first, when a mode change takes two */
} zone_ids_t;

static SemaphoreHandle_t s_mu;
static bool        s_used[HM_SCHED_MAX_STATS];
static zone_view_t s_view[HM_SCHED_MAX_STATS];
static uint32_t    s_last_ok[HM_SCHED_MAX_STATS];
static zone_ids_t  s_ids[HM_SCHED_MAX_STATS];
static uint32_t    s_dirty;
static bool        s_save_needed;
static hm_stat_t   s_st;  /* under s_mu */

static char   s_notice[NOTICES][NOTICE_LEN];
static size_t s_notice_head, s_notice_n;

static struct {
    uint8_t found, added;
} s_scan;

static void lock(void)   { xSemaphoreTake(s_mu, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_mu); }

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static bool addr_ok(uint8_t a)
{
    return a >= HM_STAT_ADDR_MIN && a <= HM_STAT_ADDR_MAX;
}

/* Under s_mu. */
static void notice_locked(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void notice_locked(const char *fmt, ...)
{
    size_t slot = (s_notice_head + s_notice_n) % NOTICES;
    if (s_notice_n == NOTICES) {
        s_notice_head = (s_notice_head + 1) % NOTICES; /* drop the oldest */
    } else {
        s_notice_n++;
    }
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_notice[slot], NOTICE_LEN, fmt, ap);
    va_end(ap);
    ESP_LOGI(TAG, "notice: %s", s_notice[slot]);
}

/* ---- persistence ---------------------------------------------------------- */

static int save(void)
{
    static uint8_t blob[ZONE_LIST_MAX_BYTES];
    zone_cfg_t list[HM_SCHED_MAX_STATS];
    size_t n = 0;
    lock();
    for (int i = 0; i < HM_SCHED_MAX_STATS; i++) {
        if (s_used[i]) {
            list[n++] = s_view[i].cfg;
        }
    }
    s_save_needed = false;
    unlock();
    size_t len = zone_list_encode(list, n, blob, sizeof blob);
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, NVS_KEY, blob, len);
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "saving the zone list failed: %s", esp_err_to_name(err));
        return -1;
    }
    ESP_LOGI(TAG, "saved %u zone(s)", (unsigned)n);
    return 0;
}

static int load(zone_cfg_t *list)
{
    static uint8_t blob[ZONE_LIST_MAX_BYTES];
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return 0; /* first boot */
    }
    size_t len = sizeof blob;
    esp_err_t err = nvs_get_blob(h, NVS_KEY, blob, &len);
    nvs_close(h);
    if (err != ESP_OK) {
        return 0;
    }
    int n = zone_list_decode(blob, len, list, HM_SCHED_MAX_STATS);
    if (n < 0) {
        ESP_LOGE(TAG, "saved zone list not understood (version %u); starting empty", blob[0]);
        return 0;
    }
    return n;
}

/* ---- zone table ----------------------------------------------------------- */

/* Under s_mu. */
static void add_locked(uint8_t addr, uint8_t model, const char *name)
{
    int i = addr - 1;
    memset(&s_view[i], 0, sizeof s_view[i]);
    memset(&s_ids[i], 0, sizeof s_ids[i]);
    s_view[i].cfg.addr = addr;
    s_view[i].cfg.model = model;
    if (name && name[0]) {
        strncpy(s_view[i].cfg.name, name, ZONE_NAME_MAX);
    } else {
        zone_default_name(addr, model, s_view[i].cfg.name, sizeof s_view[i].cfg.name);
    }
    s_view[i].link = HM_LINK_UNKNOWN;
    s_used[i] = true;
    s_dirty |= ZONE_DIRTY_LIST | (1u << i);
}

/* Under s_mu: refresh one zone's live view from the poller. */
static void refresh_locked(uint8_t addr)
{
    int i = addr - 1;
    if (!s_used[i] || !hm_poller_get(addr, &s_st)) {
        return;
    }
    zone_view_from_stat(&s_st, now_ms(), &s_view[i]);
    s_last_ok[i] = s_st.last_ok_ms;
    if (s_view[i].have && s_view[i].cfg.model != s_view[i].model) {
        s_view[i].cfg.model = s_view[i].model;
        s_save_needed = true;
    }
    s_dirty |= 1u << i;
}

static const char *wdone_reason(hm_wdone_t r)
{
    switch (r) {
    case HM_WDONE_NO_ACK:      return "no reply";
    case HM_WDONE_MISMATCH:    return "the stat put it back";
    case HM_WDONE_NO_READBACK: return "could not read it back";
    case HM_WDONE_CANCELLED:   return "cancelled";
    default:                   return "failed";
    }
}

/* Under s_mu. Matches a finished write to the pending field it belongs to. */
static void write_done_locked(const hm_event_t *ev)
{
    if (!addr_ok(ev->stat) || !s_used[ev->stat - 1] || ev->wdone == HM_WDONE_SUPERSEDED) {
        return; /* a superseded write's replacement carries the pending state */
    }
    int i = ev->stat - 1;
    zone_view_t *v = &s_view[i];
    zone_ids_t *id = &s_ids[i];
    const bool ok = ev->wdone == HM_WDONE_OK;
    const char *why = wdone_reason(ev->wdone);
#define MATCH(field) (id->field == ev->write_id || id->field == 0)
    switch (ev->write.addr) {
    case HM_UA_SET_TEMP:
        if (v->pend.set_temp && MATCH(set)) {
            if (!ok) {
                notice_locked("%s: setpoint %u\xC2\xB0 not taken (%s)", v->cfg.name, v->pend.set_temp_c, why);
            }
            v->pend.set_temp = false;
        }
        break;
    case HM_UA_ON_OFF:
    case HM_UA_RUN_MODE:
        if (v->pend.mode) {
            bool last = MATCH(mode_last) &&
                        (ev->write.addr == HM_UA_RUN_MODE || v->pend.mode_v == HM_MODE_OFF);
            bool first = id->mode_first && id->mode_first == ev->write_id;
            if (!ok && (last || first)) {
                notice_locked("%s: mode %s not taken (%s)", v->cfg.name, hm_mode_str(v->pend.mode_v), why);
                v->pend.mode = false;
            } else if (ok && last) {
                v->pend.mode = false;
            }
        }
        break;
    case HM_UA_KEY_LOCK:
        if (v->pend.lock && MATCH(lock)) {
            if (!ok) {
                notice_locked("%s: key lock not changed (%s)", v->cfg.name, why);
            }
            v->pend.lock = false;
        }
        break;
    case HM_UA_HOLD_MINUTES:
        if (v->pend.hold && MATCH(hold)) {
            if (!ok) {
                notice_locked("%s: hold not set (%s)", v->cfg.name, why);
            }
            v->pend.hold = false;
        }
        break;
    case HM_UA_HW_STATE:
        if (v->pend.hw && MATCH(hw)) {
            if (!ok) {
                notice_locked("%s: hot water not changed (%s)", v->cfg.name, why);
            }
            v->pend.hw = false;
        }
        break;
    default:
        break;
    }
#undef MATCH
    refresh_locked(ev->stat);
}

static void on_event(const hm_event_t *ev, void *arg)
{
    (void)arg;
    bool save_now = false;
    lock();
    switch (ev->type) {
    case HM_EV_UPDATED:
    case HM_EV_ONLINE:
    case HM_EV_OFFLINE:
        if (addr_ok(ev->stat)) {
            refresh_locked(ev->stat);
        }
        break;
    case HM_EV_WRITE_DONE:
        write_done_locked(ev);
        break;
    case HM_EV_SCAN_FOUND:
        s_scan.found++;
        if (addr_ok(ev->stat) && !s_used[ev->stat - 1]) {
            add_locked(ev->stat, ev->model, NULL);
            s_scan.added++;
            s_save_needed = true;
        }
        break;
    case HM_EV_SCAN_DONE:
        notice_locked("Scan found %u stat%s, %u new", s_scan.found, s_scan.found == 1 ? "" : "s",
                      s_scan.added);
        s_dirty |= ZONE_DIRTY_LIST;
        save_now = s_save_needed;
        break;
    }
    /* A newly learnt model is saved too, but not mid-scan. */
    if (s_save_needed && ev->type == HM_EV_UPDATED && !hm_poller_scanning()) {
        save_now = true;
    }
    unlock();
    if (save_now) {
        save();
    }
}

/* ---- public ----------------------------------------------------------------- */

int zone_store_init(void)
{
    s_mu = xSemaphoreCreateMutex();
    if (!s_mu) {
        return -1;
    }
    zone_cfg_t list[HM_SCHED_MAX_STATS];
    int n = load(list);
    lock();
    for (int k = 0; k < n; k++) {
        add_locked(list[k].addr, list[k].model, list[k].name);
        s_view[list[k].addr - 1].cfg.endpoint = list[k].endpoint;
    }
    unlock();
    if (hm_poller_listen(on_event, NULL) != ESP_OK) {
        return -1;
    }
    for (int k = 0; k < n; k++) {
        hm_poller_add(list[k].addr);
    }
    if (n > 0) {
        hm_poller_run(true);
    }
    ESP_LOGI(TAG, "%d zone(s) restored%s", n, n ? ", polling" : "");
    return n;
}

uint32_t zone_store_list(void)
{
    uint32_t m = 0;
    lock();
    for (int i = 0; i < HM_SCHED_MAX_STATS; i++) {
        if (s_used[i]) {
            m |= 1u << i;
        }
    }
    unlock();
    return m;
}

size_t zone_store_count(void)
{
    return (size_t)__builtin_popcount(zone_store_list());
}

bool zone_store_get(uint8_t addr, zone_view_t *out)
{
    if (!addr_ok(addr)) {
        return false;
    }
    lock();
    bool ok = s_used[addr - 1];
    if (ok) {
        *out = s_view[addr - 1];
        if (out->have) {
            out->age_s = (now_ms() - s_last_ok[addr - 1]) / 1000;
        }
    }
    unlock();
    return ok;
}

uint32_t zone_store_take_dirty(void)
{
    lock();
    uint32_t d = s_dirty;
    s_dirty = 0;
    unlock();
    return d;
}

zone_err_t zone_store_add(uint8_t addr)
{
    if (!addr_ok(addr)) {
        return ZONE_ERR_ADDR;
    }
    lock();
    bool exists = s_used[addr - 1];
    if (!exists) {
        add_locked(addr, 0xFF, NULL);
    }
    unlock();
    if (exists) {
        return ZONE_OK;
    }
    hm_poller_add(addr);
    hm_poller_run(true);
    return save() == 0 ? ZONE_OK : ZONE_ERR_STORAGE;
}

zone_err_t zone_store_remove(uint8_t addr)
{
    if (!addr_ok(addr)) {
        return ZONE_ERR_ADDR;
    }
    lock();
    bool exists = s_used[addr - 1];
    s_used[addr - 1] = false;
    s_dirty |= ZONE_DIRTY_LIST;
    unlock();
    if (!exists) {
        return ZONE_ERR_ADDR;
    }
    hm_poller_remove(addr); /* cancels its queued writes; their events find no zone */
    return save() == 0 ? ZONE_OK : ZONE_ERR_STORAGE;
}

zone_err_t zone_store_rename(uint8_t addr, const char *name)
{
    char clean[ZONE_NAME_MAX + 1];
    if (!addr_ok(addr)) {
        return ZONE_ERR_ADDR;
    }
    if (!zone_clean_name(name, clean)) {
        return ZONE_ERR_RANGE;
    }
    lock();
    bool exists = s_used[addr - 1];
    if (exists) {
        memcpy(s_view[addr - 1].cfg.name, clean, sizeof clean);
        s_dirty |= 1u << (addr - 1);
    }
    unlock();
    if (!exists) {
        return ZONE_ERR_ADDR;
    }
    return save() == 0 ? ZONE_OK : ZONE_ERR_STORAGE;
}

typedef enum { F_SET, F_MODE, F_LOCK, F_HOLD, F_HW } field_t;

static zone_err_t wr_err(hm_wr_status_t s)
{
    switch (s) {
    case HM_WR_OK:    return ZONE_OK;
    case HM_WR_MODEL: return ZONE_ERR_MODEL;
    default:          return ZONE_ERR_RANGE;
    }
}

/* Mark the field pending (with ids unknown), queue the writes, then record
 * their ids. The listener can match a write finished in between by its
 * address, so the order is safe. */
static zone_err_t queue(uint8_t addr, field_t f, const hm_write_t *w, size_t n,
                        void (*set_pending)(zone_view_t *v, const void *arg), const void *arg)
{
    if (!addr_ok(addr)) {
        return ZONE_ERR_ADDR;
    }
    int i = addr - 1;
    lock();
    if (!s_used[i]) {
        unlock();
        return ZONE_ERR_ADDR;
    }
    if (s_view[i].link == HM_LINK_OFFLINE) {
        unlock();
        return ZONE_ERR_OFFLINE;
    }
    set_pending(&s_view[i], arg);
    zone_ids_t *id = &s_ids[i];
    switch (f) {
    case F_SET:  id->set = 0; break;
    case F_LOCK: id->lock = 0; break;
    case F_HOLD: id->hold = 0; break;
    case F_HW:   id->hw = 0; break;
    case F_MODE: id->mode_first = 0; id->mode_last = 0; break;
    }
    s_dirty |= 1u << i;
    unlock();

    uint32_t ids[2] = { 0, 0 };
    for (size_t k = 0; k < n; k++) {
        if (hm_poller_write(addr, &w[k], &ids[k]) != 0) {
            lock();
            memset(&s_view[i].pend, 0, sizeof s_view[i].pend); /* nothing reliable is queued */
            s_dirty |= 1u << i;
            unlock();
            return ZONE_ERR_BUSY;
        }
    }

    lock();
    switch (f) {
    case F_SET:  if (s_view[i].pend.set_temp) id->set = ids[0]; break;
    case F_LOCK: if (s_view[i].pend.lock) id->lock = ids[0]; break;
    case F_HOLD: if (s_view[i].pend.hold) id->hold = ids[0]; break;
    case F_HW:   if (s_view[i].pend.hw) id->hw = ids[0]; break;
    case F_MODE:
        if (s_view[i].pend.mode) {
            id->mode_first = n == 2 ? ids[0] : 0;
            id->mode_last = ids[n - 1];
        }
        break;
    }
    unlock();
    return ZONE_OK;
}

static void pend_set(zone_view_t *v, const void *a)
{
    v->pend.set_temp = true;
    v->pend.set_temp_c = (uint8_t)*(const int *)a;
}

static void pend_mode(zone_view_t *v, const void *a)
{
    v->pend.mode = true;
    v->pend.mode_v = *(const hm_mode_t *)a;
}

static void pend_lock(zone_view_t *v, const void *a)
{
    v->pend.lock = true;
    v->pend.lock_v = *(const bool *)a;
}

static void pend_hold(zone_view_t *v, const void *a)
{
    v->pend.hold = true;
    v->pend.hold_min = (uint16_t)*(const unsigned *)a;
}

static void pend_hw(zone_view_t *v, const void *a)
{
    v->pend.hw = true;
    v->pend.hw_cmd = *(const int *)a;
}

zone_err_t zone_store_set_temp(uint8_t addr, int c)
{
    hm_write_t w;
    zone_err_t e = wr_err(hm_wr_set_temp(&w, c));
    return e ? e : queue(addr, F_SET, &w, 1, pend_set, &c);
}

zone_err_t zone_store_set_mode(uint8_t addr, hm_mode_t m)
{
    hm_write_t w[2];
    size_t n = hm_wr_mode(w, m);
    return n ? queue(addr, F_MODE, w, n, pend_mode, &m) : ZONE_ERR_RANGE;
}

zone_err_t zone_store_set_lock(uint8_t addr, bool locked)
{
    hm_write_t w;
    zone_err_t e = wr_err(hm_wr_key_lock(&w, locked));
    return e ? e : queue(addr, F_LOCK, &w, 1, pend_lock, &locked);
}

zone_err_t zone_store_set_hold(uint8_t addr, unsigned minutes)
{
    hm_write_t w;
    zone_err_t e = wr_err(hm_wr_hold(&w, minutes));
    return e ? e : queue(addr, F_HOLD, &w, 1, pend_hold, &minutes);
}

zone_err_t zone_store_set_hw(uint8_t addr, int cmd)
{
    zone_view_t v;
    if (!zone_store_get(addr, &v)) {
        return ZONE_ERR_ADDR;
    }
    hm_write_t w;
    zone_err_t e = wr_err(hm_wr_hw_state(&w, v.have ? v.model : v.cfg.model, cmd));
    return e ? e : queue(addr, F_HW, &w, 1, pend_hw, &cmd);
}

void zone_store_scan(void)
{
    lock();
    s_scan.found = 0;
    s_scan.added = 0;
    unlock();
    hm_poller_scan();
    hm_poller_run(true);
}

void zone_store_scan_state(zone_scan_t *out)
{
    uint8_t pass = 0, next = 0;
    out->running = hm_poller_scan_state(&pass, &next);
    out->pass = pass;
    out->next = next;
    lock();
    out->found = s_scan.found;
    out->added = s_scan.added;
    unlock();
}

bool zone_store_pop_notice(char *buf, size_t cap)
{
    lock();
    bool any = s_notice_n > 0;
    if (any) {
        snprintf(buf, cap, "%s", s_notice[s_notice_head]);
        s_notice_head = (s_notice_head + 1) % NOTICES;
        s_notice_n--;
    }
    unlock();
    return any;
}
