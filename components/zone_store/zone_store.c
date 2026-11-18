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
#include <time.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "hm_bus.h"
#include "hm_poller.h"
#include "hm_write.h"
#include "nvs.h"

static const char *TAG = "zones";

#define NOTICES     8
#define NOTICE_LEN  96
#define NVS_NS      "zones"
#define NVS_KEY     "list"
#define NVS_POLL    "poll"
#define NVS_BOOST   "boost"   /* bit (addr-1) for each stat in a boost */
#define BOOST_CHECK_MS 5000

/* Pending write ids per zone. 0 = queued but its id is not known yet; the
 * listener then matches on the field's address instead. */
typedef struct {
    uint32_t set, lock, hold, hw;
    uint32_t mode_last;      /* the last write of a mode change */
    uint32_t mode_first;     /* the first, when a mode change takes two */
    uint32_t comfort[7], hwt[7];
} zone_ids_t;

static SemaphoreHandle_t s_mu;
static bool        s_used[HM_SCHED_MAX_STATS];
static zone_view_t s_view[HM_SCHED_MAX_STATS];
static uint32_t    s_last_ok[HM_SCHED_MAX_STATS];
static zone_ids_t  s_ids[HM_SCHED_MAX_STATS];
static uint32_t    s_dirty;
static bool        s_save_needed;
static hm_stat_t   s_st;  /* under s_mu */
static uint32_t    s_boost_end[HM_SCHED_MAX_STATS]; /* ms, 0 = no boost */
static uint32_t    s_boost_saved;                   /* the mask in NVS */

static char   s_notice[NOTICES][NOTICE_LEN];
static size_t s_notice_head, s_notice_n;

static struct {
    uint8_t found, added;
} s_scan;

static zone_err_t hw_cmd(uint8_t addr, int cmd, uint8_t mode, unsigned boost_min);
static void boost_check(void *arg);
static void save_boosts(void);

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

/* Under s_mu: refresh one zone's live view from the poller. Programme days
 * being written keep the values asked for. */
static void refresh_locked(uint8_t addr)
{
    static hm_period_t keep_c[7][4], keep_on[7][4], keep_off[7][4];
    int i = addr - 1;
    if (!s_used[i] || !hm_poller_get(addr, &s_st)) {
        return;
    }
    zone_view_t *v = &s_view[i];
    const uint8_t pc = v->pend.comfort, ph = v->pend.hw_timer;
    if (pc || ph) {
        memcpy(keep_c, v->comfort, sizeof keep_c);
        memcpy(keep_on, v->hw_on_at, sizeof keep_on);
        memcpy(keep_off, v->hw_off_at, sizeof keep_off);
    }
    zone_view_from_stat(&s_st, now_ms(), v);
    for (int d = 0; d < 7; d++) {
        if (pc & (1u << d)) {
            memcpy(v->comfort[d], keep_c[d], sizeof keep_c[d]);
        }
        if (ph & (1u << d)) {
            memcpy(v->hw_on_at[d], keep_on[d], sizeof keep_on[d]);
            memcpy(v->hw_off_at[d], keep_off[d], sizeof keep_off[d]);
        }
    }
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
    case HM_UA_HOLIDAY_HOURS:
        if (!ok) {
            notice_locked("%s: holiday not set (%s)", v->cfg.name, why);
        }
        break;
    case HM_UA_CLOCK:
        if (!ok) {
            notice_locked("%s: clock not set (%s)", v->cfg.name, why);
        }
        break;
    default:
        for (int d = 0; d < 7; d++) {
            const uint16_t c = (uint16_t)(v->prog_mode ? HM_UA_COMFORT_7DAY + 12 * d : HM_UA_COMFORT_52 + 12 * d);
            const uint16_t h = (uint16_t)(v->prog_mode ? HM_UA_HW_TIMER_7DAY + 16 * d : HM_UA_HW_TIMER_52 + 16 * d);
            const uint8_t bit = (uint8_t)(1u << d);
            if (ev->write.addr == c && (v->pend.comfort & bit) && MATCH(comfort[d])) {
                if (!ok) {
                    notice_locked("%s: %s programme not changed (%s)", v->cfg.name,
                                  zone_prog_day_name(v->comfort_days, d), why);
                }
                v->pend.comfort &= (uint8_t)~bit;
            } else if (ev->write.addr == h && (v->pend.hw_timer & bit) && MATCH(hwt[d])) {
                if (!ok) {
                    notice_locked("%s: %s hot water timer not changed (%s)", v->cfg.name,
                                  zone_prog_day_name(v->hw_timer_days, d), why);
                }
                v->pend.hw_timer &= (uint8_t)~bit;
            }
        }
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

    /* Saved poll interval; boosts cut short by a restart go back to the timer. */
    uint32_t poll = 0, boosts = 0;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u32(h, NVS_POLL, &poll);
        nvs_get_u32(h, NVS_BOOST, &boosts);
        nvs_close(h);
    }
    if (poll >= 2000 && poll <= 120000) {
        hm_poller_set_interval(poll);
    }
    if (n > 0) {
        hm_poller_run(true);
    }
    s_boost_saved = boosts;
    for (uint8_t a = HM_STAT_ADDR_MIN; a <= HM_STAT_ADDR_MAX; a++) {
        if (boosts & (1u << (a - 1))) {
            ESP_LOGI(TAG, "stat %u was in a boost at restart: back to its timer", a);
            hw_cmd(a, HM_HW_FOLLOW_TIMER, ZONE_HW_TIMER, 0);
        }
    }
    save_boosts();
    const esp_timer_create_args_t ta = { .callback = boost_check, .name = "zone_boost" };
    esp_timer_handle_t timer;
    if (esp_timer_create(&ta, &timer) == ESP_OK) {
        esp_timer_start_periodic(timer, BOOST_CHECK_MS * 1000ull);
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
        uint32_t end = s_boost_end[addr - 1];
        int32_t left = end ? (int32_t)(end - now_ms()) : 0;
        out->boost_left_min = left > 0 ? (uint16_t)((left + 59999) / 60000) : 0;
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
    s_boost_end[addr - 1] = 0;
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

typedef enum { F_SET, F_MODE, F_LOCK, F_HOLD, F_HW, F_COMFORT, F_HWT } field_t;

/* Argument for the programme fields. */
typedef struct {
    int day;
    const hm_period_t *p, *on, *off;
} prog_arg_t;

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
    case F_COMFORT: id->comfort[((const prog_arg_t *)arg)->day] = 0; break;
    case F_HWT:     id->hwt[((const prog_arg_t *)arg)->day] = 0; break;
    }
    s_dirty |= 1u << i;
    unlock();

    uint32_t ids[2] = { 0, 0 };
    for (size_t k = 0; k < n; k++) {
        if (hm_poller_write(addr, &w[k], &ids[k]) != 0) {
            lock();
            memset(&s_view[i].pend, 0, sizeof s_view[i].pend); /* nothing reliable is queued */
            s_dirty |= 1u << i;
            refresh_locked(addr); /* and the programme back to what the stat has */
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
    case F_COMFORT: {
        int d = ((const prog_arg_t *)arg)->day;
        if (s_view[i].pend.comfort & (1u << d)) {
            id->comfort[d] = ids[0];
        }
        break;
    }
    case F_HWT: {
        int d = ((const prog_arg_t *)arg)->day;
        if (s_view[i].pend.hw_timer & (1u << d)) {
            id->hwt[d] = ids[0];
        }
        break;
    }
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

static void pend_comfort(zone_view_t *v, const void *a)
{
    const prog_arg_t *pa = a;
    v->pend.comfort |= (uint8_t)(1u << pa->day);
    memcpy(v->comfort[pa->day], pa->p, sizeof v->comfort[pa->day]);
}

static void pend_hwt(zone_view_t *v, const void *a)
{
    const prog_arg_t *pa = a;
    v->pend.hw_timer |= (uint8_t)(1u << pa->day);
    memcpy(v->hw_on_at[pa->day], pa->on, sizeof v->hw_on_at[pa->day]);
    memcpy(v->hw_off_at[pa->day], pa->off, sizeof v->hw_off_at[pa->day]);
}

/* Save which stats are in a boost, so a restart can end them. */
static void save_boosts(void)
{
    uint32_t mask = 0;
    lock();
    for (int i = 0; i < HM_SCHED_MAX_STATS; i++) {
        if (s_boost_end[i]) {
            mask |= 1u << i;
        }
    }
    bool changed = mask != s_boost_saved;
    s_boost_saved = mask;
    unlock();
    nvs_handle_t h;
    if (changed && nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        if (nvs_set_u32(h, NVS_BOOST, mask) == ESP_OK) {
            nvs_commit(h);
        }
        nvs_close(h);
    }
}

static zone_err_t hw_cmd(uint8_t addr, int cmd, uint8_t mode, unsigned boost_min)
{
    zone_view_t v;
    if (!zone_store_get(addr, &v)) {
        return ZONE_ERR_ADDR;
    }
    hm_write_t w;
    zone_err_t e = wr_err(hm_wr_hw_state(&w, v.have ? v.model : v.cfg.model, cmd));
    if (!e) {
        e = queue(addr, F_HW, &w, 1, pend_hw, &cmd);
    }
    if (!e) {
        lock();
        s_view[addr - 1].hw_mode = mode;
        s_boost_end[addr - 1] = boost_min ? (now_ms() + boost_min * 60000u) | 1u : 0;
        s_dirty |= 1u << (addr - 1);
        unlock();
        save_boosts();
    }
    return e;
}

zone_err_t zone_store_set_hw(uint8_t addr, int cmd)
{
    uint8_t mode = cmd == HM_HW_FORCE_ON ? ZONE_HW_ON : cmd == HM_HW_FORCE_OFF ? ZONE_HW_OFF : ZONE_HW_TIMER;
    return hw_cmd(addr, cmd, mode, 0);
}

zone_err_t zone_store_hw_boost(uint8_t addr, unsigned minutes)
{
    if (minutes > 24 * 60) {
        return ZONE_ERR_RANGE;
    }
    if (minutes == 0) {
        return hw_cmd(addr, HM_HW_FOLLOW_TIMER, ZONE_HW_TIMER, 0);
    }
    return hw_cmd(addr, HM_HW_FORCE_ON, ZONE_HW_BOOST, minutes);
}

/* esp_timer task: end boosts that are due. */
static void boost_check(void *arg)
{
    (void)arg;
    const uint32_t t = now_ms();
    for (uint8_t a = HM_STAT_ADDR_MIN; a <= HM_STAT_ADDR_MAX; a++) {
        lock();
        uint32_t end = s_boost_end[a - 1];
        bool due = end && (int32_t)(t - end) >= 0;
        if (s_used[a - 1] && end) {
            s_dirty |= 1u << (a - 1); /* the minutes left change */
        }
        unlock();
        if (due && hw_cmd(a, HM_HW_FOLLOW_TIMER, ZONE_HW_TIMER, 0) != ZONE_OK) {
            lock();
            /* Offline or a full queue: try again on the next check, unless
             * the zone has gone. */
            if (!s_used[a - 1]) {
                s_boost_end[a - 1] = 0;
            }
            unlock();
        }
    }
}

static int prog_check(uint8_t addr, int day, bool hw, uint8_t *model, uint8_t *prog)
{
    zone_view_t v;
    if (!zone_store_get(addr, &v)) {
        return ZONE_ERR_ADDR;
    }
    if (!v.have) {
        return ZONE_ERR_OFFLINE;
    }
    uint8_t days = hw ? v.hw_timer_days : v.comfort_days;
    if (hw && !v.is_prthw) {
        return ZONE_ERR_MODEL;
    }
    if (day < 0 || day >= days) {
        return ZONE_ERR_RANGE;
    }
    *model = v.model;
    *prog = v.prog_mode;
    return ZONE_OK;
}

zone_err_t zone_store_set_comfort(uint8_t addr, int day, const hm_period_t p[4])
{
    uint8_t model, prog;
    zone_err_t e = (zone_err_t)prog_check(addr, day, false, &model, &prog);
    if (e) {
        return e;
    }
    if (!zone_periods_ok(p)) {
        return ZONE_ERR_RANGE;
    }
    hm_write_t w;
    e = wr_err(hm_wr_comfort(&w, model, prog, day, p));
    prog_arg_t a = { .day = day, .p = p };
    return e ? e : queue(addr, F_COMFORT, &w, 1, pend_comfort, &a);
}

zone_err_t zone_store_set_hw_timer(uint8_t addr, int day, const hm_period_t on[4], const hm_period_t off[4])
{
    uint8_t model, prog;
    zone_err_t e = (zone_err_t)prog_check(addr, day, true, &model, &prog);
    if (e) {
        return e;
    }
    if (!zone_periods_ok(on)) {
        return ZONE_ERR_RANGE;
    }
    hm_write_t w;
    e = wr_err(hm_wr_hw_timer(&w, model, prog, day, on, off));
    prog_arg_t a = { .day = day, .on = on, .off = off };
    return e ? e : queue(addr, F_HWT, &w, 1, pend_hwt, &a);
}

/* Queue one write on every online stat; returns how many took it. */
static int write_all(const hm_write_t *w)
{
    int n = 0;
    for (uint8_t a = HM_STAT_ADDR_MIN; a <= HM_STAT_ADDR_MAX; a++) {
        lock();
        bool go = s_used[a - 1] && s_view[a - 1].have && s_view[a - 1].link == HM_LINK_ONLINE;
        unlock();
        uint32_t id;
        if (go && hm_poller_write(a, w, &id) == 0) {
            n++;
        }
    }
    return n;
}

int zone_store_sync_clocks(void)
{
    time_t now = time(NULL);
    if (now < 1700000000) { /* not synced: still 1970 */
        return -1;
    }
    struct tm tm;
    localtime_r(&now, &tm);
    hm_write_t w;
    if (hm_wr_clock(&w, tm.tm_wday == 0 ? 7 : tm.tm_wday, tm.tm_hour, tm.tm_min, tm.tm_sec) != HM_WR_OK) {
        return 0;
    }
    return write_all(&w);
}

int zone_store_holiday_all(unsigned hours)
{
    hm_write_t w;
    if (hm_wr_holiday(&w, hours) != HM_WR_OK) {
        return 0;
    }
    return write_all(&w);
}

/* ---- bus diagnostics ------------------------------------------------------ */

bool zone_store_diag(uint8_t addr, zone_diag_t *out)
{
    memset(out, 0, sizeof *out);
    if (!addr_ok(addr)) {
        return false;
    }
    lock();
    bool ok = hm_poller_get(addr, &s_st);
    if (ok) {
        const hm_stat_counters_t *c = &s_st.cnt;
        out->polls = c->polls;
        out->poll_ok = c->poll_ok;
        out->timeouts = c->timeouts;
        out->crc = c->crc;
        out->bad = c->bad;
        out->writes_ok = c->writes_ok;
        out->writes_failed = c->writes_failed;
        out->seen = s_st.have_dcb;
        out->seen_age_s = s_st.have_dcb ? (now_ms() - s_st.last_ok_ms) / 1000 : 0;
    }
    unlock();
    return ok;
}

size_t zone_store_frames(zone_frame_t *out, size_t max)
{
    hm_trace_t t[HM_TRACE_LEN];
    size_t n = hm_poller_trace(t, max < HM_TRACE_LEN ? max : HM_TRACE_LEN);
    const uint32_t now = now_ms();
    for (size_t i = 0; i < n; i++) {
        zone_frame_t *f = &out[i];
        memset(f, 0, sizeof *f);
        f->age_ms = now - t[i].ms;
        f->tx = t[i].tx;
        f->rx = t[i].result == HM_XFER_OK ? ZONE_RX_OK : t[i].result == HM_XFER_TIMEOUT ? ZONE_RX_TIMEOUT
              : t[i].result == HM_XFER_CRC ? ZONE_RX_CRC : ZONE_RX_BAD;
        f->len = t[i].len;
        f->n = t[i].n < sizeof f->b ? t[i].n : sizeof f->b;
        memcpy(f->b, t[i].b, f->n);
        f->timeout_ms = t[i].timeout_ms;
    }
    return n;
}

void zone_store_clear_counters(void)
{
    hm_poller_clear_counters(0);
}

zone_err_t zone_store_read_now(uint8_t addr)
{
    if (!addr_ok(addr)) {
        return ZONE_ERR_ADDR;
    }
    return hm_poller_poll_now(addr) == 0 ? ZONE_OK : ZONE_ERR_ADDR;
}

void zone_store_bus_info(zone_bus_info_t *out)
{
    out->poll_ms = hm_poller_interval();
    out->master = hm_bus_config()->master_addr;
    out->baud = (uint32_t)hm_bus_config()->baud;
}

zone_err_t zone_store_set_poll_interval(uint32_t ms)
{
    if (ms < 2000 || ms > 120000) {
        return ZONE_ERR_RANGE;
    }
    hm_poller_set_interval(ms);
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_u32(h, NVS_POLL, ms);
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    return err == ESP_OK ? ZONE_OK : ZONE_ERR_STORAGE;
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
