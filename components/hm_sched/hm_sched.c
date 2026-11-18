#include "hm_sched.h"

#include <string.h>

#define MAX_EVENTS (HM_SCHED_QUEUE_LEN + 4)

typedef struct {
    hm_event_t ev[MAX_EVENTS];
    size_t n;
} evbuf_t;

static void lock(hm_sched_t *s)
{
    if (s->io.lock) {
        s->io.lock(s->io.ctx);
    }
}

static void unlock(hm_sched_t *s)
{
    if (s->io.unlock) {
        s->io.unlock(s->io.ctx);
    }
}

static uint32_t now(hm_sched_t *s)
{
    return s->io.now_ms(s->io.ctx);
}

/* Wrap-safe "a is at or after b". */
static bool reached(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) >= 0;
}

static hm_event_t *ev_add(evbuf_t *b, hm_event_type_t type, uint8_t stat)
{
    if (b->n >= MAX_EVENTS) {
        return NULL;
    }
    hm_event_t *e = &b->ev[b->n++];
    memset(e, 0, sizeof(*e));
    e->type = type;
    e->stat = stat;
    return e;
}

static void ev_flush(hm_sched_t *s, evbuf_t *b)
{
    if (s->io.event) {
        for (size_t i = 0; i < b->n; i++) {
            s->io.event(s->io.ctx, &b->ev[i]);
        }
    }
    b->n = 0;
}

static bool addr_ok(uint8_t a)
{
    return a >= HM_STAT_ADDR_MIN && a <= HM_STAT_ADDR_MAX;
}

static hm_stat_t *stat_of(hm_sched_t *s, uint8_t addr)
{
    return addr_ok(addr) && s->stats[addr - 1].used ? &s->stats[addr - 1] : NULL;
}

void hm_sched_init(hm_sched_t *s, const hm_sched_config_t *cfg, const hm_sched_io_t *io)
{
    memset(s, 0, sizeof(*s));
    s->cfg = *cfg;
    s->io = *io;
    s->next_id = 1;
}

static void add_locked(hm_sched_t *s, uint8_t addr, uint32_t t)
{
    hm_stat_t *st = &s->stats[addr - 1];
    if (st->used) {
        return;
    }
    memset(st, 0, sizeof(*st));
    st->used = true;
    st->addr = addr;
    st->link = HM_LINK_UNKNOWN;
    st->next_poll_ms = t; /* poll it straight away */
}

int hm_sched_add(hm_sched_t *s, uint8_t addr)
{
    if (!addr_ok(addr)) {
        return -1;
    }
    uint32_t t = now(s);
    lock(s);
    add_locked(s, addr, t);
    unlock(s);
    return 0;
}

static void q_pop(hm_sched_t *s, size_t i)
{
    memmove(&s->q[i], &s->q[i + 1], (s->q_len - i - 1) * sizeof(s->q[0]));
    s->q_len--;
    memset(&s->q[s->q_len], 0, sizeof(s->q[0]));
}

int hm_sched_remove(hm_sched_t *s, uint8_t addr)
{
    if (!addr_ok(addr)) {
        return -1;
    }
    evbuf_t b = { .n = 0 };
    lock(s);
    memset(&s->stats[addr - 1], 0, sizeof(s->stats[0]));
    for (size_t i = 0; i < s->q_len;) {
        if (s->q[i].stat == addr) {
            hm_event_t *e = ev_add(&b, HM_EV_WRITE_DONE, addr);
            if (e) {
                e->write_id = s->q[i].id;
                e->wdone = HM_WDONE_CANCELLED;
                e->attempts = s->q[i].attempts;
                e->write = s->q[i].w;
            }
            q_pop(s, i);
        } else {
            i++;
        }
    }
    unlock(s);
    ev_flush(s, &b);
    return 0;
}

int hm_sched_write(hm_sched_t *s, uint8_t stat, const hm_write_t *w, uint32_t *id)
{
    if (!addr_ok(stat) || !w || w->len == 0 || w->len > HM_WRITE_MAX_DATA) {
        return -2;
    }
    evbuf_t b = { .n = 0 };
    int rc = 0;
    lock(s);
    hm_wq_entry_t *slot = NULL;
    for (size_t i = 0; i < s->q_len; i++) {
        hm_wq_entry_t *e = &s->q[i];
        if (!e->in_flight && e->stat == stat && e->w.addr == w->addr) {
            hm_event_t *ev = ev_add(&b, HM_EV_WRITE_DONE, stat);
            if (ev) {
                ev->write_id = e->id;
                ev->wdone = HM_WDONE_SUPERSEDED;
                ev->attempts = e->attempts;
                ev->write = e->w;
            }
            slot = e; /* keep its place in the queue */
            break;
        }
    }
    if (!slot) {
        if (s->q_len >= HM_SCHED_QUEUE_LEN) {
            rc = -1;
        } else {
            slot = &s->q[s->q_len++];
        }
    }
    if (slot) {
        memset(slot, 0, sizeof(*slot));
        slot->used = true;
        slot->id = s->next_id++;
        if (s->next_id == 0) {
            s->next_id = 1;
        }
        slot->stat = stat;
        slot->w = *w;
        if (id) {
            *id = slot->id;
        }
    }
    unlock(s);
    ev_flush(s, &b);
    return rc;
}

void hm_sched_scan(hm_sched_t *s)
{
    lock(s);
    s->scanning = true;
    s->scan_pass = 1;
    s->scan_next = HM_STAT_ADDR_MIN;
    s->scan_found = 0;
    unlock(s);
}

bool hm_sched_scanning(hm_sched_t *s)
{
    lock(s);
    bool v = s->scanning;
    unlock(s);
    return v;
}

void hm_sched_set_interval(hm_sched_t *s, uint32_t ms)
{
    lock(s);
    s->cfg.poll_interval_ms = ms;
    unlock(s);
}

void hm_sched_clear_counters(hm_sched_t *s, uint8_t addr)
{
    lock(s);
    for (int i = 0; i < HM_SCHED_MAX_STATS; i++) {
        if (addr == 0 || i == addr - 1) {
            memset(&s->stats[i].cnt, 0, sizeof s->stats[i].cnt);
        }
    }
    unlock(s);
}

int hm_sched_poll_now(hm_sched_t *s, uint8_t addr)
{
    int rc = -1;
    uint32_t t = now(s);
    lock(s);
    hm_stat_t *st = stat_of(s, addr);
    if (st) {
        st->next_poll_ms = t;
        rc = 0;
    }
    unlock(s);
    return rc;
}

bool hm_sched_get(hm_sched_t *s, uint8_t addr, hm_stat_t *out)
{
    bool ok = false;
    lock(s);
    hm_stat_t *st = stat_of(s, addr);
    if (st) {
        *out = *st;
        ok = true;
    }
    unlock(s);
    return ok;
}

uint32_t hm_sched_list(hm_sched_t *s)
{
    uint32_t m = 0;
    lock(s);
    for (int i = 0; i < HM_SCHED_MAX_STATS; i++) {
        if (s->stats[i].used) {
            m |= 1u << i;
        }
    }
    unlock(s);
    return m;
}

size_t hm_sched_queue_len(hm_sched_t *s)
{
    lock(s);
    size_t n = s->q_len;
    unlock(s);
    return n;
}

/* ---- bus slots ---------------------------------------------------------- */

static void count_fail(hm_sched_t *s, uint8_t stat, hm_xfer_t r)
{
    lock(s);
    hm_stat_t *st = stat_of(s, stat);
    if (st) {
        if (r == HM_XFER_TIMEOUT) st->cnt.timeouts++;
        else if (r == HM_XFER_CRC) st->cnt.crc++;
        else st->cnt.bad++;
    }
    unlock(s);
}

static bool dcb_reply_ok(const hm_reply_t *r)
{
    if (r->func != HM_FUNC_READ || r->start != 0 || !r->data || r->count < HM_DCB_MIN_LEN ||
        r->count > HM_DCB_MAX_LEN) {
        return false;
    }
    uint16_t len = (uint16_t)((r->data[0] << 8) | r->data[1]);
    return len == r->count;
}

/* Full DCB read with retries. On success, the DCB is in buf. */
static hm_xfer_t read_dcb(hm_sched_t *s, uint8_t stat, uint8_t *buf, uint16_t *len)
{
    uint8_t req[HM_READ_REQ_LEN];
    hm_xfer_t r = HM_XFER_BAD;
    if (hm_build_read(req, sizeof req, stat, s->cfg.master, 0, HM_READ_ALL) != sizeof req) {
        return HM_XFER_BAD;
    }
    for (unsigned attempt = 0; attempt <= s->cfg.read_retries; attempt++) {
        hm_reply_t rep;
        memset(&rep, 0, sizeof rep);
        r = s->io.xfer(s->io.ctx, req, sizeof req, stat, s->cfg.read_timeout_ms, &rep);
        if (r == HM_XFER_OK && !dcb_reply_ok(&rep)) {
            r = HM_XFER_BAD;
        }
        if (r == HM_XFER_OK) {
            memcpy(buf, rep.data, rep.count);
            *len = rep.count;
            return r;
        }
        count_fail(s, stat, r);
    }
    return r;
}

/* Clock bytes tick every poll; leave them out of "changed". */
static bool dcb_changed(const hm_stat_t *st, const uint8_t *dcb, uint16_t len)
{
    if (!st->have_dcb || st->dcb_len != len) {
        return true;
    }
    int clk = hm_dcb_index(dcb[HM_IDX_MODEL], dcb[HM_IDX_PROG_MODE], HM_UA_CLOCK);
    for (uint16_t i = 0; i < len; i++) {
        if (clk >= 0 && i >= clk && i < clk + 4) {
            continue;
        }
        if (st->dcb[i] != dcb[i]) {
            return true;
        }
    }
    return false;
}

/* Under the lock: a good DCB arrived from a stat. */
static void got_dcb(hm_sched_t *s, evbuf_t *b, uint8_t stat, const uint8_t *dcb, uint16_t len,
                    uint32_t t)
{
    hm_stat_t *st = stat_of(s, stat);
    if (!st) {
        return; /* a write to a stat that is not added */
    }
    bool changed = dcb_changed(st, dcb, len);
    memcpy(st->dcb, dcb, len);
    st->dcb_len = len;
    st->have_dcb = true;
    st->misses = 0;
    st->last_ok_ms = t;
    if (st->link != HM_LINK_ONLINE) {
        st->link = HM_LINK_ONLINE;
        ev_add(b, HM_EV_ONLINE, stat);
    }
    hm_event_t *e = ev_add(b, HM_EV_UPDATED, stat);
    if (e) {
        e->model = dcb[HM_IDX_MODEL];
        e->changed = changed;
    }
}

static void do_write(hm_sched_t *s, hm_wq_entry_t e, evbuf_t *b)
{
    static uint8_t dcb[HM_DCB_MAX_LEN];
    uint16_t len = 0;
    uint8_t req[10 + HM_WRITE_MAX_DATA];
    hm_wdone_t outcome = HM_WDONE_NO_ACK;
    bool have_dcb = false;

    size_t n = hm_write_frame(&e.w, req, sizeof req, e.stat, s->cfg.master);
    hm_reply_t rep;
    memset(&rep, 0, sizeof rep);
    hm_xfer_t r = n ? s->io.xfer(s->io.ctx, req, n, e.stat, s->cfg.write_timeout_ms, &rep)
                    : HM_XFER_BAD;
    if (r == HM_XFER_OK && rep.func != HM_FUNC_WRITE) {
        r = HM_XFER_BAD;
    }
    if (r != HM_XFER_OK) {
        count_fail(s, e.stat, r);
    } else if (read_dcb(s, e.stat, dcb, &len) != HM_XFER_OK) {
        outcome = HM_WDONE_NO_READBACK;
    } else {
        have_dcb = true;
        hm_verify_t v = hm_write_verify(&e.w, dcb, len);
        outcome = (v == HM_VERIFY_OK || v == HM_VERIFY_UNCHECKED) ? HM_WDONE_OK : HM_WDONE_MISMATCH;
    }

    uint32_t t = now(s);
    lock(s);
    if (have_dcb) {
        got_dcb(s, b, e.stat, dcb, len, t);
    }
    hm_stat_t *st = stat_of(s, e.stat);
    /* The head is still this write unless the stat was removed meanwhile. */
    if (s->q_len && s->q[0].id == e.id) {
        hm_wq_entry_t *h = &s->q[0];
        h->attempts++;
        h->in_flight = false;
        bool done = outcome == HM_WDONE_OK || h->attempts > s->cfg.write_retries;
        if (outcome != HM_WDONE_OK) {
            h->last_fail = outcome;
        }
        if (done) {
            hm_event_t *ev = ev_add(b, HM_EV_WRITE_DONE, e.stat);
            if (ev) {
                ev->write_id = h->id;
                ev->wdone = outcome == HM_WDONE_OK ? HM_WDONE_OK : h->last_fail;
                ev->attempts = h->attempts;
                ev->write = h->w;
            }
            if (st) {
                if (outcome == HM_WDONE_OK) st->cnt.writes_ok++;
                else st->cnt.writes_failed++;
            }
            q_pop(s, 0);
        } else if (st) {
            st->cnt.write_retries++;
        }
    }
    unlock(s);
}

static void do_poll(hm_sched_t *s, uint8_t stat, uint32_t t0, evbuf_t *b)
{
    static uint8_t dcb[HM_DCB_MAX_LEN];
    uint16_t len = 0;
    hm_xfer_t r = read_dcb(s, stat, dcb, &len);
    uint32_t t = now(s);
    lock(s);
    hm_stat_t *st = stat_of(s, stat);
    if (st) {
        st->cnt.polls++;
        if (r == HM_XFER_OK) {
            st->cnt.poll_ok++;
            got_dcb(s, b, stat, dcb, len, t);
            st->next_poll_ms = t0 + s->cfg.poll_interval_ms;
        } else {
            st->cnt.misses++;
            if (st->misses < 255) {
                st->misses++;
            }
            if (st->misses >= s->cfg.offline_after && st->link != HM_LINK_OFFLINE) {
                st->link = HM_LINK_OFFLINE;
                ev_add(b, HM_EV_OFFLINE, stat);
            }
            st->next_poll_ms = t0 + (st->link == HM_LINK_OFFLINE ? s->cfg.offline_retry_ms
                                                                 : s->cfg.poll_interval_ms);
        }
    }
    unlock(s);
}

static void do_scan_probe(hm_sched_t *s, uint8_t addr, uint32_t timeout, evbuf_t *b)
{
    uint8_t req[HM_READ_REQ_LEN];
    hm_reply_t rep;
    memset(&rep, 0, sizeof rep);
    hm_build_read(req, sizeof req, addr, s->cfg.master, 0, HM_SCAN_PROBE_LEN);
    hm_xfer_t r = s->io.xfer(s->io.ctx, req, sizeof req, addr, timeout, &rep);
    bool found = r == HM_XFER_OK && rep.func == HM_FUNC_READ && rep.count == HM_SCAN_PROBE_LEN &&
                 rep.data;
    uint8_t model = found ? rep.data[HM_IDX_MODEL] : 0;
    uint32_t t = now(s);

    lock(s);
    if (found) {
        s->scan_found |= 1u << (addr - 1);
        hm_event_t *e = ev_add(b, HM_EV_SCAN_FOUND, addr);
        if (e) {
            e->model = model;
        }
        if (s->cfg.scan_auto_add) {
            add_locked(s, addr, t);
        }
    }
    if (!s->scanning || s->scan_next != addr) {
        unlock(s); /* the scan was restarted while this probe was on the wire */
        return;
    }
    /* Next address: pass 2 only re-checks the misses. */
    uint8_t next = (uint8_t)(addr + 1);
    for (;;) {
        if (next > HM_STAT_ADDR_MAX) {
            bool misses = s->scan_found != 0xFFFFFFFFu;
            if (s->scan_pass == 1 && s->cfg.scan_recheck_ms && misses) {
                s->scan_pass = 2;
                next = HM_STAT_ADDR_MIN;
            } else {
                s->scanning = false;
                hm_event_t *e = ev_add(b, HM_EV_SCAN_DONE, 0);
                if (e) {
                    e->found_mask = s->scan_found;
                }
                break;
            }
        }
        if (s->scan_pass == 2 && (s->scan_found & (1u << (next - 1)))) {
            next++;
            continue;
        }
        break;
    }
    s->scan_next = next;
    unlock(s);
}

uint32_t hm_sched_step(hm_sched_t *s, hm_step_t *did)
{
    evbuf_t b = { .n = 0 };
    hm_step_t what = HM_STEP_IDLE;
    uint32_t wait = 0;
    uint32_t t = now(s);

    lock(s);
    if (s->q_len) {
        s->q[0].in_flight = true;
        hm_wq_entry_t e = s->q[0];
        unlock(s);
        what = HM_STEP_WRITE;
        do_write(s, e, &b);
    } else if (s->scanning) {
        uint8_t addr = s->scan_next;
        uint32_t timeout = s->scan_pass == 1 ? s->cfg.scan_timeout_ms : s->cfg.scan_recheck_ms;
        unlock(s);
        what = HM_STEP_SCAN;
        do_scan_probe(s, addr, timeout, &b);
    } else {
        int pick = -1;
        uint32_t soonest = UINT32_MAX;
        for (int k = 0; k < HM_SCHED_MAX_STATS; k++) {
            int i = (s->rr + k) % HM_SCHED_MAX_STATS;
            hm_stat_t *st = &s->stats[i];
            if (!st->used) {
                continue;
            }
            if (reached(t, st->next_poll_ms)) {
                pick = i;
                break;
            }
            uint32_t left = st->next_poll_ms - t;
            if (left < soonest) {
                soonest = left;
            }
        }
        if (pick >= 0) {
            s->rr = (uint8_t)((pick + 1) % HM_SCHED_MAX_STATS);
            unlock(s);
            what = HM_STEP_POLL;
            do_poll(s, (uint8_t)(pick + 1), t, &b);
        } else {
            wait = soonest == UINT32_MAX ? 1000 : soonest;
            unlock(s);
        }
    }
    ev_flush(s, &b);
    if (did) {
        *did = what;
    }
    return wait;
}

const char *hm_link_str(hm_link_t l)
{
    switch (l) {
    case HM_LINK_UNKNOWN: return "new";
    case HM_LINK_ONLINE:  return "online";
    case HM_LINK_OFFLINE: return "OFFLINE";
    }
    return "?";
}

const char *hm_wdone_str(hm_wdone_t r)
{
    switch (r) {
    case HM_WDONE_OK:          return "confirmed";
    case HM_WDONE_NO_ACK:      return "no ack";
    case HM_WDONE_MISMATCH:    return "read-back differs";
    case HM_WDONE_NO_READBACK: return "acked, read-back failed";
    case HM_WDONE_SUPERSEDED:  return "superseded";
    case HM_WDONE_CANCELLED:   return "cancelled";
    }
    return "?";
}
