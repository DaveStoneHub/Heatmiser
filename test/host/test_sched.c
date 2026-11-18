/* Host tests for the bus scheduler, against simulated stats on a simulated
 * bus with a simulated clock. */
#include <stdlib.h>
#include <string.h>

#include "hm_crc.h"
#include "hm_frame.h"
#include "hm_sched.h"
#include "hm_write.h"
#include "test_util.h"

/* ---- Simulated bus ------------------------------------------------------- */

typedef struct {
    bool     present;
    uint8_t  dcb[HM_DCB_MAX_LEN];
    uint16_t len;
    int      drop_writes;    /* ignore this many write frames (no reply) */
    int      drop_reads;     /* ignore this many read frames */
    int      crc_reads;      /* answer this many reads with a corrupted CRC */
    bool     ack_no_apply;   /* ack writes but keep the old value */
} sim_stat_t;

typedef struct {
    sim_stat_t st[33];
    uint32_t   now;
    uint8_t    rx[HM_MAX_FRAME];
    unsigned   frames, reads, full_reads, writes, probes;
    int        lock_depth, max_lock_depth, locks;
    hm_event_t ev[256];
    size_t     nev;
    uint32_t   scan_first_full_read;  /* time of first full read after scan start */
} sim_t;

static sim_t S;

static uint32_t wire_ms(size_t bytes)
{
    return (uint32_t)((bytes * 10 * 1000 + 4799) / 4800);
}

static void sim_prt(uint8_t addr, uint8_t set_temp)
{
    sim_stat_t *s = &S.st[addr];
    static const uint8_t base[64] = {
        0x00, 0x40, 0x00, 0x13, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01,
        0x00, 0x00, 0x00, 0x14, 0x00, 0x10, 0x10, 0x1C, 0x01, 0x01, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0xE6, 0x00, 0x00,
        0x01, 0x03, 0x32, 0x0F, 0x08, 0x00, 0x15, 0x09, 0x1E, 0x10, 0x10, 0x1E,
        0x16, 0x17, 0x00, 0x11, 0x09, 0x00, 0x15, 0x16, 0x00, 0x10, 0x18, 0x00,
        0x10, 0x18, 0x00, 0x10,
    };
    memset(s, 0, sizeof(*s));
    s->present = true;
    memcpy(s->dcb, base, sizeof base);
    s->dcb[11] = addr;
    s->dcb[18] = set_temp;
    s->len = 64;
}

static void sim_prthw(uint8_t addr)
{
    sim_stat_t *s = &S.st[addr];
    memset(s, 0, sizeof(*s));
    s->present = true;
    s->len = 97;
    s->dcb[1] = 97;
    s->dcb[4] = HM_MODEL_PRTHW;
    s->dcb[11] = addr;
    s->dcb[17] = 12;
    s->dcb[18] = 20;
    s->dcb[21] = 1;
    s->dcb[32] = 0x00;
    s->dcb[33] = 0xC8;
}

static bool writable(uint8_t model, uint8_t prog, uint16_t ua)
{
    switch (ua) {
    case 17: case 18: case 19: case 21: case 22: case 23: case 24: case 32: case 43:
        return true;
    case 42:
        return model == HM_MODEL_PRTHW;
    }
    return ua >= HM_UA_COMFORT_52 && hm_dcb_index(model, prog, ua) >= 0;
}

static size_t make_frame(uint8_t *f, uint8_t master, uint8_t stat, uint8_t func,
                         uint16_t start, const uint8_t *data, uint16_t n)
{
    size_t len;
    f[0] = master;
    if (func == HM_FUNC_WRITE) {
        len = 7;
        f[1] = 7;
        f[2] = 0;
        f[3] = stat;
        f[4] = 1;
    } else {
        len = 11u + n;
        f[1] = (uint8_t)len;
        f[2] = (uint8_t)(len >> 8);
        f[3] = stat;
        f[4] = 0;
        f[5] = (uint8_t)start;
        f[6] = (uint8_t)(start >> 8);
        f[7] = (uint8_t)n;
        f[8] = (uint8_t)(n >> 8);
        memcpy(&f[9], data, n);
    }
    uint16_t crc = hm_crc16(f, len - 2);
    f[len - 2] = (uint8_t)crc;
    f[len - 1] = (uint8_t)(crc >> 8);
    return len;
}

static hm_xfer_t sim_xfer(void *ctx, const uint8_t *req, size_t req_len, uint8_t stat,
                          uint32_t timeout_ms, hm_reply_t *reply)
{
    (void)ctx;
    CHECK_EQ(S.lock_depth, 0); /* never hold the lock across the bus */
    S.frames++;
    S.now += wire_ms(req_len);
    CHECK(hm_crc16(req, req_len - 2) == (uint16_t)(req[req_len - 2] | (req[req_len - 1] << 8)));
    CHECK_EQ(req[0], stat);
    const uint8_t master = req[2], func = req[3];
    const uint16_t start = (uint16_t)(req[4] | (req[5] << 8));
    const uint16_t n = (uint16_t)(req[6] | (req[7] << 8));
    sim_stat_t *s = (stat >= 1 && stat <= 32) ? &S.st[stat] : NULL;

    bool answer = s && s->present;
    size_t len = 0;
    if (func == HM_FUNC_READ) {
        S.reads++;
        if (start == 0 && n == HM_READ_ALL) {
            S.full_reads++;
        } else if (start == 0 && n == HM_SCAN_PROBE_LEN) {
            S.probes++;
        }
        if (answer && s->drop_reads > 0) {
            s->drop_reads--;
            answer = false;
        }
        if (answer) {
            if (start == 0 && n == HM_READ_ALL) {
                len = make_frame(S.rx, master, stat, 0, 0, s->dcb, s->len);
            } else {
                len = make_frame(S.rx, master, stat, 0, start, s->dcb, n);
            }
            if (s->crc_reads > 0) {
                s->crc_reads--;
                S.rx[len - 1] ^= 0x55;
            }
        }
    } else {
        S.writes++;
        uint8_t model = s ? s->dcb[4] : 0, prog = s ? s->dcb[16] : 0;
        if (answer && s->drop_writes > 0) {
            s->drop_writes--;
            answer = false;
        }
        if (answer && !writable(model, prog, start)) {
            answer = false; /* the V3 stats say nothing to a bad write */
        }
        if (answer) {
            int i = hm_dcb_index(model, prog, start);
            const uint8_t *d = &req[8];
            if (!s->ack_no_apply) {
                if (start == HM_UA_HOLIDAY_HOURS || start == HM_UA_HOLD_MINUTES) {
                    s->dcb[i] = d[1];
                    s->dcb[i + 1] = d[0];
                } else if (start == HM_UA_HW_STATE) {
                    if (d[0]) {
                        s->dcb[i] = d[0] == 1;
                    }
                } else {
                    memcpy(&s->dcb[i], d, n);
                }
            }
            len = make_frame(S.rx, master, stat, 1, 0, NULL, 0);
        }
    }
    if (!answer) {
        S.now += timeout_ms + 100;
        return HM_XFER_TIMEOUT;
    }
    S.now += 7 + wire_ms(len) + 100; /* turnaround, reply, gap */
    hm_status_t st = hm_parse_reply(S.rx, len, master, stat, reply);
    if (st == HM_ERR_CRC) {
        return HM_XFER_CRC;
    }
    return st == HM_OK ? HM_XFER_OK : HM_XFER_BAD;
}

static uint32_t sim_now(void *ctx)
{
    (void)ctx;
    return S.now;
}

static void sim_event(void *ctx, const hm_event_t *ev)
{
    (void)ctx;
    CHECK_EQ(S.lock_depth, 0);
    if (S.nev < sizeof S.ev / sizeof S.ev[0]) {
        S.ev[S.nev++] = *ev;
    }
}

static void sim_lock(void *ctx)
{
    (void)ctx;
    S.locks++;
    S.lock_depth++;
    if (S.lock_depth > S.max_lock_depth) {
        S.max_lock_depth = S.lock_depth;
    }
}

static void sim_unlock(void *ctx)
{
    (void)ctx;
    S.lock_depth--;
}

static hm_sched_t *new_sched(hm_sched_config_t *cfg_in)
{
    memset(&S, 0, sizeof S);
    S.now = 0xFFFF0000u; /* start near the wrap to test wrap-safe timing */
    static hm_sched_t sched;
    hm_sched_config_t cfg = HM_SCHED_CONFIG_DEFAULT();
    if (cfg_in) {
        cfg = *cfg_in;
    }
    hm_sched_io_t io = {
        .xfer = sim_xfer, .now_ms = sim_now, .event = sim_event,
        .lock = sim_lock, .unlock = sim_unlock, .ctx = NULL,
    };
    hm_sched_init(&sched, &cfg, &io);
    return &sched;
}

/* Run until the simulated clock passes `ms` from now, idling as told. */
static void run_for(hm_sched_t *s, uint32_t ms)
{
    uint32_t end = S.now + ms;
    while ((int32_t)(S.now - end) < 0) {
        hm_step_t did;
        uint32_t wait = hm_sched_step(s, &did);
        if (did == HM_STEP_IDLE) {
            uint32_t left = end - S.now;
            S.now += wait < left ? wait : left;
        }
    }
}

static size_t count_ev(hm_event_type_t t, uint8_t stat)
{
    size_t n = 0;
    for (size_t i = 0; i < S.nev; i++) {
        if (S.ev[i].type == t && (stat == 0 || S.ev[i].stat == stat)) {
            n++;
        }
    }
    return n;
}

static const hm_event_t *last_ev(hm_event_type_t t)
{
    for (size_t i = S.nev; i-- > 0;) {
        if (S.ev[i].type == t) {
            return &S.ev[i];
        }
    }
    return NULL;
}

static const hm_event_t *write_done(uint32_t id)
{
    for (size_t i = 0; i < S.nev; i++) {
        if (S.ev[i].type == HM_EV_WRITE_DONE && S.ev[i].write_id == id) {
            return &S.ev[i];
        }
    }
    return NULL;
}

/* ---- Tests ---------------------------------------------------------------- */

static void test_round_robin(void)
{
    hm_sched_t *s = new_sched(NULL);
    for (uint8_t a = 1; a <= 12; a++) {
        sim_prt(a, (uint8_t)(15 + a));
        CHECK_EQ(hm_sched_add(s, a), 0);
    }
    CHECK_EQ(hm_sched_add(s, 0), -1);
    CHECK_EQ(hm_sched_add(s, 33), -1);
    CHECK_EQ(hm_sched_list(s), 0xFFF);

    run_for(s, 60000);
    /* 12 stats, one read each per 10 s, for 60 s: 6 or 7 reads each. */
    for (uint8_t a = 1; a <= 12; a++) {
        hm_stat_t st;
        CHECK(hm_sched_get(s, a, &st));
        CHECK_EQ(st.link, HM_LINK_ONLINE);
        CHECK(st.cnt.polls >= 6 && st.cnt.polls <= 7);
        CHECK_EQ(st.cnt.poll_ok, st.cnt.polls);
        CHECK_EQ(st.cnt.misses, 0);
        CHECK(st.have_dcb);
        CHECK_EQ(st.dcb[18], 15 + a);
    }
    CHECK_EQ(count_ev(HM_EV_ONLINE, 0), 12);
    CHECK_EQ(count_ev(HM_EV_OFFLINE, 0), 0);
    /* Only the first read of each stat counts as a change. */
    size_t changed = 0;
    for (size_t i = 0; i < S.nev; i++) {
        if (S.ev[i].type == HM_EV_UPDATED && S.ev[i].changed) changed++;
    }
    CHECK_EQ(changed, 12);
    hm_stat_t x;
    CHECK(!hm_sched_get(s, 13, &x));
    CHECK_EQ(S.max_lock_depth, 1);
    CHECK_EQ(S.lock_depth, 0);

    /* A temperature change shows as changed; a clock tick does not. */
    S.nev = 0;
    S.st[3].dcb[39] = 7;     /* seconds */
    run_for(s, 10000);
    CHECK(last_ev(HM_EV_UPDATED) != NULL);
    for (size_t i = 0; i < S.nev; i++) {
        if (S.ev[i].type == HM_EV_UPDATED) CHECK(!S.ev[i].changed);
    }
    S.nev = 0;
    S.st[3].dcb[33] = 0xE7;  /* air 23.1 C */
    run_for(s, 10000);
    for (size_t i = 0; i < S.nev; i++) {
        if (S.ev[i].type == HM_EV_UPDATED) CHECK_EQ(S.ev[i].changed, S.ev[i].stat == 3);
    }
}

static void test_offline_backoff(void)
{
    hm_sched_t *s = new_sched(NULL);
    sim_prt(1, 20);
    S.st[2].present = false;
    hm_sched_add(s, 1);
    hm_sched_add(s, 2);

    run_for(s, 35000);
    hm_stat_t st;
    hm_sched_get(s, 2, &st);
    CHECK_EQ(st.link, HM_LINK_OFFLINE);
    CHECK_EQ(st.misses, 3);
    CHECK_EQ(st.cnt.timeouts, 6); /* each miss is the read plus one retry */
    CHECK_EQ(count_ev(HM_EV_OFFLINE, 2), 1);
    CHECK_EQ(count_ev(HM_EV_ONLINE, 2), 0);

    /* Offline: one attempt a minute, not one per sweep. */
    uint32_t polls = st.cnt.polls;
    run_for(s, 120000);
    hm_sched_get(s, 2, &st);
    CHECK(st.cnt.polls - polls >= 1 && st.cnt.polls - polls <= 2);
    CHECK_EQ(count_ev(HM_EV_OFFLINE, 2), 1);
    /* The good stat kept its 10 s cadence meanwhile. */
    hm_stat_t one;
    hm_sched_get(s, 1, &one);
    CHECK(one.cnt.polls >= 15);
    CHECK_EQ(one.link, HM_LINK_ONLINE);

    /* It comes back within a minute of answering. */
    sim_prt(2, 21);
    run_for(s, 61000);
    hm_sched_get(s, 2, &st);
    CHECK_EQ(st.link, HM_LINK_ONLINE);
    CHECK_EQ(st.misses, 0);
    CHECK_EQ(count_ev(HM_EV_ONLINE, 2), 1);
}

static void test_read_retry_in_slot(void)
{
    hm_sched_t *s = new_sched(NULL);
    sim_prt(4, 20);
    S.st[4].crc_reads = 1;
    hm_sched_add(s, 4);
    hm_step_t did;
    hm_sched_step(s, &did);
    CHECK_EQ(did, HM_STEP_POLL);
    hm_stat_t st;
    hm_sched_get(s, 4, &st);
    CHECK_EQ(st.cnt.crc, 1);
    CHECK_EQ(st.cnt.poll_ok, 1);
    CHECK_EQ(st.misses, 0);
    CHECK_EQ(st.link, HM_LINK_ONLINE);

    /* Two bad reads in a row in one slot: one miss, not yet offline. */
    S.st[4].drop_reads = 2;
    run_for(s, 10500);
    hm_sched_get(s, 4, &st);
    CHECK_EQ(st.misses, 1);
    CHECK_EQ(st.link, HM_LINK_ONLINE);
}

static void test_write_confirmed(void)
{
    hm_sched_t *s = new_sched(NULL);
    for (uint8_t a = 1; a <= 3; a++) {
        sim_prt(a, 18);
        hm_sched_add(s, a);
    }
    run_for(s, 2000); /* first sweep done; nothing due for a while */

    hm_write_t w;
    hm_wr_set_temp(&w, 22);
    uint32_t id = 0;
    CHECK_EQ(hm_sched_write(s, 2, &w, &id), 0);
    CHECK(id != 0);
    CHECK_EQ(hm_sched_queue_len(s), 1);
    unsigned writes = S.writes, full = S.full_reads;
    hm_step_t did;
    uint32_t t0 = S.now;
    hm_sched_step(s, &did);
    CHECK_EQ(did, HM_STEP_WRITE);
    CHECK_EQ(S.writes - writes, 1);
    CHECK_EQ(S.full_reads - full, 1); /* read back at once */
    CHECK(S.now - t0 < 600);
    const hm_event_t *e = write_done(id);
    CHECK(e != NULL);
    if (e) {
        CHECK_EQ(e->wdone, HM_WDONE_OK);
        CHECK_EQ(e->attempts, 1);
        CHECK_EQ(e->stat, 2);
        CHECK_EQ(e->write.addr, 18);
    }
    hm_stat_t st;
    hm_sched_get(s, 2, &st);
    CHECK_EQ(st.dcb[18], 22);
    CHECK_EQ(st.cnt.writes_ok, 1);
    CHECK_EQ(S.st[2].dcb[18], 22);
    CHECK_EQ(hm_sched_queue_len(s), 0);
}

static void test_write_goes_first(void)
{
    hm_sched_t *s = new_sched(NULL);
    for (uint8_t a = 1; a <= 6; a++) {
        sim_prt(a, 18);
        hm_sched_add(s, a);
    }
    /* All six are due now; a write queued now still goes before them. */
    hm_write_t w;
    hm_wr_set_temp(&w, 25);
    uint32_t id;
    hm_sched_write(s, 6, &w, &id);
    hm_step_t did;
    hm_sched_step(s, &did);
    CHECK_EQ(did, HM_STEP_WRITE);
    CHECK(write_done(id) && write_done(id)->wdone == HM_WDONE_OK);
}

static void test_write_retries(void)
{
    hm_sched_t *s = new_sched(NULL);
    sim_prt(1, 18);
    hm_sched_add(s, 1);
    run_for(s, 1000);

    /* Two lost writes, then it takes: confirmed on attempt 3. */
    S.st[1].drop_writes = 2;
    hm_write_t w;
    hm_wr_set_temp(&w, 19);
    uint32_t id;
    hm_sched_write(s, 1, &w, &id);
    run_for(s, 5000);
    const hm_event_t *e = write_done(id);
    CHECK(e && e->wdone == HM_WDONE_OK && e->attempts == 3);
    hm_stat_t st;
    hm_sched_get(s, 1, &st);
    CHECK_EQ(st.cnt.write_retries, 2);
    CHECK_EQ(st.cnt.timeouts, 2);

    /* Never acked: the first try plus 5 retries, then NO_ACK. */
    S.st[1].drop_writes = 100;
    unsigned writes = S.writes;
    hm_wr_set_temp(&w, 20);
    hm_sched_write(s, 1, &w, &id);
    run_for(s, 20000);
    e = write_done(id);
    CHECK(e && e->wdone == HM_WDONE_NO_ACK && e->attempts == 6);
    CHECK_EQ(S.writes - writes, 6);
    CHECK_EQ(S.st[1].dcb[18], 19);
    hm_sched_get(s, 1, &st);
    CHECK_EQ(st.cnt.writes_failed, 1);
    CHECK_EQ(st.dcb[18], 19); /* the store keeps the last value read */

    /* Acked but never applied: MISMATCH. */
    S.st[1].drop_writes = 0;
    S.st[1].ack_no_apply = true;
    hm_wr_set_temp(&w, 21);
    hm_sched_write(s, 1, &w, &id);
    run_for(s, 20000);
    e = write_done(id);
    CHECK(e && e->wdone == HM_WDONE_MISMATCH && e->attempts == 6);

    /* Acked, but every read-back is lost: NO_READBACK. */
    S.st[1].ack_no_apply = false;
    S.st[1].drop_reads = 100;
    hm_wr_set_temp(&w, 22);
    hm_sched_write(s, 1, &w, &id);
    run_for(s, 30000);
    e = write_done(id);
    CHECK(e && e->wdone == HM_WDONE_NO_READBACK);
    S.st[1].drop_reads = 0;

    /* A write to a read-only address gets silence (V3 rule): NO_ACK. */
    hm_write_t ro = { .addr = HM_UA_TEMP_FORMAT, .len = 1, .data = { 1 } };
    hm_sched_write(s, 1, &ro, &id);
    run_for(s, 20000);
    e = write_done(id);
    CHECK(e && e->wdone == HM_WDONE_NO_ACK);
}

static void test_write_coalesce_and_limits(void)
{
    hm_sched_t *s = new_sched(NULL);
    sim_prt(1, 18);
    sim_prt(2, 18);
    hm_sched_add(s, 1);
    hm_sched_add(s, 2);
    run_for(s, 1000);

    /* Spinning a setpoint queues one write, with the latest value. */
    hm_write_t w;
    uint32_t id1, id2, id3, idlock;
    hm_wr_set_temp(&w, 20);
    hm_sched_write(s, 1, &w, &id1);
    hm_write_t lk;
    hm_wr_key_lock(&lk, true);
    hm_sched_write(s, 1, &lk, &idlock);
    hm_wr_set_temp(&w, 21);
    hm_sched_write(s, 1, &w, &id2);
    hm_wr_set_temp(&w, 22);
    hm_sched_write(s, 1, &w, &id3);
    CHECK_EQ(hm_sched_queue_len(s), 2);
    CHECK(write_done(id1) && write_done(id1)->wdone == HM_WDONE_SUPERSEDED);
    CHECK(write_done(id2) && write_done(id2)->wdone == HM_WDONE_SUPERSEDED);
    unsigned writes = S.writes;
    run_for(s, 3000);
    CHECK_EQ(S.writes - writes, 2);
    CHECK(write_done(id3) && write_done(id3)->wdone == HM_WDONE_OK);
    CHECK(write_done(idlock) && write_done(idlock)->wdone == HM_WDONE_OK);
    CHECK_EQ(S.st[1].dcb[18], 22);
    CHECK_EQ(S.st[1].dcb[22], 1);

    /* The same field on another stat is a separate write. */
    hm_sched_write(s, 1, &w, &id1);
    hm_sched_write(s, 2, &w, &id2);
    CHECK_EQ(hm_sched_queue_len(s), 2);
    run_for(s, 3000);

    /* Full queue. */
    for (int i = 0; i < HM_SCHED_QUEUE_LEN; i++) {
        hm_write_t x = { .addr = (uint16_t)(100 + i), .len = 1 };
        CHECK_EQ(hm_sched_write(s, 1, &x, NULL), 0);
    }
    CHECK_EQ(hm_sched_write(s, 2, &w, NULL), -1);
    CHECK_EQ(hm_sched_write(s, 0, &w, NULL), -2);
    hm_write_t empty = { .addr = 18, .len = 0 };
    CHECK_EQ(hm_sched_write(s, 1, &empty, NULL), -2);

    /* Removing the stat cancels everything queued for it. */
    S.nev = 0;
    CHECK_EQ(hm_sched_remove(s, 1), 0);
    CHECK_EQ(hm_sched_queue_len(s), 0);
    CHECK_EQ(count_ev(HM_EV_WRITE_DONE, 1), HM_SCHED_QUEUE_LEN);
    CHECK(last_ev(HM_EV_WRITE_DONE)->wdone == HM_WDONE_CANCELLED);
    hm_stat_t st;
    CHECK(!hm_sched_get(s, 1, &st));
    CHECK_EQ(hm_sched_list(s), 0x2);
}

static void test_mode_writes(void)
{
    hm_sched_t *s = new_sched(NULL);
    sim_prt(3, 18);
    S.st[3].dcb[21] = 0; /* off */
    hm_sched_add(s, 3);
    run_for(s, 1000);

    hm_write_t w[2];
    uint32_t ids[2];
    size_t n = hm_wr_mode(w, HM_MODE_FROST);
    for (size_t i = 0; i < n; i++) {
        hm_sched_write(s, 3, &w[i], &ids[i]);
    }
    run_for(s, 3000);
    CHECK(write_done(ids[0]) && write_done(ids[0])->wdone == HM_WDONE_OK);
    CHECK(write_done(ids[1]) && write_done(ids[1])->wdone == HM_WDONE_OK);
    hm_stat_t st;
    hm_sched_get(s, 3, &st);
    hm_dcb_t d;
    CHECK_EQ(hm_dcb_decode(st.dcb, st.dcb_len, &d), 0);
    CHECK_EQ(hm_dcb_mode(&d), HM_MODE_FROST);

    n = hm_wr_mode(w, HM_MODE_HEAT);
    for (size_t i = 0; i < n; i++) {
        hm_sched_write(s, 3, &w[i], &ids[i]);
    }
    run_for(s, 3000);
    hm_sched_get(s, 3, &st);
    hm_dcb_decode(st.dcb, st.dcb_len, &d);
    CHECK_EQ(hm_dcb_mode(&d), HM_MODE_HEAT);

    n = hm_wr_mode(w, HM_MODE_OFF);
    hm_sched_write(s, 3, &w[0], &ids[0]);
    run_for(s, 3000);
    hm_sched_get(s, 3, &st);
    hm_dcb_decode(st.dcb, st.dcb_len, &d);
    CHECK_EQ(hm_dcb_mode(&d), HM_MODE_OFF);
    CHECK_EQ(n, 1);
}

static void test_prthw_writes(void)
{
    hm_sched_t *s = new_sched(NULL);
    sim_prthw(12);
    hm_sched_add(s, 12);
    run_for(s, 1000);
    hm_stat_t st;
    hm_sched_get(s, 12, &st);
    CHECK_EQ(st.dcb_len, 97);

    hm_write_t w;
    uint32_t id;
    hm_wr_hw_state(&w, HM_MODEL_PRTHW, HM_HW_FORCE_ON);
    hm_sched_write(s, 12, &w, &id);
    run_for(s, 2000);
    CHECK(write_done(id) && write_done(id)->wdone == HM_WDONE_OK);
    CHECK_EQ(S.st[12].dcb[36], 1);

    hm_wr_hw_state(&w, HM_MODEL_PRTHW, HM_HW_FOLLOW_TIMER);
    hm_sched_write(s, 12, &w, &id);
    run_for(s, 2000);
    CHECK(write_done(id) && write_done(id)->wdone == HM_WDONE_OK);

    hm_wr_set_temp(&w, 23);
    hm_sched_write(s, 12, &w, &id);
    run_for(s, 2000);
    CHECK(write_done(id) && write_done(id)->wdone == HM_WDONE_OK);
    CHECK_EQ(S.st[12].dcb[18], 23);

    hm_wr_hold(&w, 120);
    hm_sched_write(s, 12, &w, &id);
    run_for(s, 2000);
    CHECK(write_done(id) && write_done(id)->wdone == HM_WDONE_OK);
    CHECK_EQ(S.st[12].dcb[26], 0);
    CHECK_EQ(S.st[12].dcb[27], 120);
}

static void test_scan(void)
{
    hm_sched_config_t cfg = HM_SCHED_CONFIG_DEFAULT();
    hm_sched_t *s = new_sched(&cfg);
    sim_prt(1, 18);
    sim_prt(5, 18);
    sim_prt(7, 18);
    sim_prthw(12);
    S.st[7].drop_reads = 1;  /* misses the first pass, answers the re-check */
    sim_prt(9, 18);          /* already added before the scan */
    hm_sched_add(s, 9);
    run_for(s, 1000);

    unsigned full_before = S.full_reads;
    S.nev = 0;
    hm_sched_scan(s);
    CHECK(hm_sched_scanning(s));
    uint32_t t0 = S.now;
    hm_step_t did;
    unsigned probes = 0;
    while (hm_sched_scanning(s)) {
        hm_sched_step(s, &did);
        CHECK_EQ(did, HM_STEP_SCAN);
        probes++;
        CHECK(probes < 100);
    }
    uint32_t took = S.now - t0;
    /* Polling paused: no full reads during the scan. */
    CHECK_EQ(S.full_reads, full_before);
    /* Pass 1: 32 probes, 4 found (7 drops its first reply); pass 2: the 28 misses. */
    CHECK_EQ(probes, 32 + 28);
    /* Pass 1 costs ~200 ms per empty address with the 100 ms gap; pass 2
     * ~400 ms. About 17 s on a nearly empty bus. */
    CHECK(took < 20000);

    const hm_event_t *done = last_ev(HM_EV_SCAN_DONE);
    CHECK(done != NULL);
    if (done) {
        CHECK_EQ(done->found_mask, (1u << 0) | (1u << 4) | (1u << 6) | (1u << 8) | (1u << 11));
    }
    CHECK_EQ(count_ev(HM_EV_SCAN_FOUND, 0), 5);
    for (size_t i = 0; i < S.nev; i++) {
        if (S.ev[i].type == HM_EV_SCAN_FOUND && S.ev[i].stat == 12) {
            CHECK_EQ(S.ev[i].model, HM_MODEL_PRTHW);
        }
    }
    /* Auto-add: all found stats are polled from now on. */
    CHECK_EQ(hm_sched_list(s), done ? done->found_mask : 0);
    run_for(s, 3000);
    for (uint8_t a = 1; a <= 32; a++) {
        hm_stat_t st;
        if (hm_sched_get(s, a, &st)) {
            CHECK_EQ(st.link, HM_LINK_ONLINE);
        }
    }

    /* A write queued mid-scan goes before the next probe. */
    hm_sched_scan(s);
    hm_sched_step(s, &did);
    CHECK_EQ(did, HM_STEP_SCAN);
    hm_write_t w;
    uint32_t id;
    hm_wr_set_temp(&w, 24);
    hm_sched_write(s, 5, &w, &id);
    hm_sched_step(s, &did);
    CHECK_EQ(did, HM_STEP_WRITE);
    CHECK(write_done(id) && write_done(id)->wdone == HM_WDONE_OK);
    hm_sched_step(s, &did);
    CHECK_EQ(did, HM_STEP_SCAN);

    /* No second pass when it is turned off. */
    cfg.scan_recheck_ms = 0;
    cfg.scan_auto_add = false;
    s = new_sched(&cfg);
    sim_prt(3, 18);
    hm_sched_scan(s);
    probes = 0;
    while (hm_sched_scanning(s)) {
        hm_sched_step(s, &did);
        probes++;
    }
    CHECK_EQ(probes, 32);
    CHECK_EQ(hm_sched_list(s), 0);
    CHECK(last_ev(HM_EV_SCAN_DONE) && last_ev(HM_EV_SCAN_DONE)->found_mask == (1u << 2));
}

static void test_idle_wait(void)
{
    hm_sched_t *s = new_sched(NULL);
    hm_step_t did;
    /* Nothing added: idle, and asks to be woken in a while. */
    uint32_t wait = hm_sched_step(s, &did);
    CHECK_EQ(did, HM_STEP_IDLE);
    CHECK(wait > 0);
    sim_prt(1, 18);
    hm_sched_add(s, 1);
    hm_sched_step(s, &did);
    CHECK_EQ(did, HM_STEP_POLL);
    wait = hm_sched_step(s, &did);
    CHECK_EQ(did, HM_STEP_IDLE);
    CHECK(wait > 9000 && wait <= 10000);
    hm_sched_set_interval(s, 5000);
    S.now += wait;
    hm_sched_step(s, &did);
    CHECK_EQ(did, HM_STEP_POLL);
    wait = hm_sched_step(s, &did);
    CHECK(wait > 4000 && wait <= 5000);
    CHECK(strcmp(hm_link_str(HM_LINK_OFFLINE), "OFFLINE") == 0);
    CHECK(strcmp(hm_wdone_str(HM_WDONE_OK), "confirmed") == 0);
}

static void test_counters_and_poll_now(void)
{
    hm_sched_t *s = new_sched(NULL);
    sim_prt(1, 18);
    sim_prt(2, 19);
    hm_sched_add(s, 1);
    hm_sched_add(s, 2);
    run_for(s, 25000);
    hm_stat_t st;
    CHECK(hm_sched_get(s, 1, &st));
    CHECK(st.cnt.polls >= 2);
    hm_sched_clear_counters(s, 1);
    CHECK(hm_sched_get(s, 1, &st));
    CHECK_EQ(st.cnt.polls, 0);
    CHECK(hm_sched_get(s, 2, &st));
    CHECK(st.cnt.polls >= 2);
    hm_sched_clear_counters(s, 0);
    CHECK(hm_sched_get(s, 2, &st));
    CHECK_EQ(st.cnt.polls, 0);

    /* Poll now: the next slot reads that stat, well before its interval. */
    hm_step_t did;
    uint32_t wait;
    do {
        wait = hm_sched_step(s, &did);
    } while (did != HM_STEP_IDLE);
    CHECK(wait > 1000);
    CHECK_EQ(hm_sched_poll_now(s, 2), 0);
    CHECK_EQ(hm_sched_poll_now(s, 9), -1);
    hm_sched_step(s, &did);
    CHECK_EQ(did, HM_STEP_POLL);
    CHECK(hm_sched_get(s, 2, &st));
    CHECK_EQ(st.cnt.polls, 1);
}

void test_sched_all(void)
{
    test_counters_and_poll_now();
    test_round_robin();
    test_offline_backoff();
    test_read_retry_in_slot();
    test_write_confirmed();
    test_write_goes_first();
    test_write_retries();
    test_write_coalesce_and_limits();
    test_mode_writes();
    test_prthw_writes();
    test_scan();
    test_idle_wait();
}
