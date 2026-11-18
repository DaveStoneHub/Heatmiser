/* A zone store with made-up data for the preview: 12 stats like the house
 * bus (11 PRT-N, a PRT/HW-N at address 3, stat 2 not answering). Changes
 * apply at once. */
#include <stdio.h>
#include <string.h>

#include "fake.h"
#include "zone_store.h"

static zone_view_t s_z[HM_SCHED_MAX_STATS];
static bool s_used[HM_SCHED_MAX_STATS];
static uint32_t s_dirty;
static zone_scan_t s_scan;
static char s_notice[96];

static void put(uint8_t addr, const char *name, int room, int set, hm_mode_t mode, bool heating)
{
    zone_view_t *v = &s_z[addr - 1];
    memset(v, 0, sizeof *v);
    s_used[addr - 1] = true;
    v->cfg.addr = addr;
    v->cfg.model = HM_MODEL_PRT;
    snprintf(v->cfg.name, sizeof v->cfg.name, "%s", name);
    v->link = HM_LINK_ONLINE;
    v->have = true;
    v->age_s = addr % 9;
    v->model = HM_MODEL_PRT;
    v->room_t10 = (int16_t)room;
    v->air_t10 = (int16_t)room;
    v->remote_t10 = ZONE_T_ABSENT;
    v->floor_t10 = ZONE_T_ABSENT;
    v->set_temp = (uint8_t)set;
    v->frost_temp = 7;
    v->frost_enabled = true;
    v->mode = mode;
    v->heating = heating;
    /* 5/2 programme: weekdays 06:30 21, 08:30 16, 17:00 21, 22:30 16;
     * weekends 08:00 21, 23:00 16. */
    static const hm_period_t wk[4] = { { 6, 30, 21 }, { 8, 30, 16 }, { 17, 0, 21 }, { 22, 30, 16 } };
    static const hm_period_t we[4] = { { 8, 0, 21 }, { 23, 0, 16 }, { 24, 0, 16 }, { 24, 0, 16 } };
    v->comfort_days = 2;
    memcpy(v->comfort[0], wk, sizeof wk);
    memcpy(v->comfort[1], we, sizeof we);
    v->clk_wday = 3;
    v->clk_hour = 18;
    v->clk_min = 40;
}

static void put_hw(zone_view_t *v)
{
    static const hm_period_t on_wk[4] = { { 6, 0, 0 }, { 17, 0, 0 }, { 24, 0, 0 }, { 24, 0, 0 } };
    static const hm_period_t off_wk[4] = { { 7, 30, 0 }, { 19, 0, 0 }, { 24, 0, 0 }, { 24, 0, 0 } };
    static const hm_period_t on_we[4] = { { 7, 30, 0 }, { 18, 0, 0 }, { 24, 0, 0 }, { 24, 0, 0 } };
    static const hm_period_t off_we[4] = { { 9, 30, 0 }, { 20, 0, 0 }, { 24, 0, 0 }, { 24, 0, 0 } };
    v->model = v->cfg.model = HM_MODEL_PRTHW;
    v->is_prthw = true;
    v->hw_timer_days = 2;
    memcpy(v->hw_on_at[0], on_wk, sizeof on_wk);
    memcpy(v->hw_off_at[0], off_wk, sizeof off_wk);
    memcpy(v->hw_on_at[1], on_we, sizeof on_we);
    memcpy(v->hw_off_at[1], off_we, sizeof off_we);
}

void fake_load_house(void)
{
    memset(s_used, 0, sizeof s_used);
    put(1, "Living room", 203, 21, HM_MODE_HEAT, true);
    put(2, "Kitchen", 0, 0, HM_MODE_HEAT, false);
    s_z[1].have = false;
    s_z[1].link = HM_LINK_OFFLINE;
    s_z[1].comfort_days = 0;
    put(3, "Hall", 188, 19, HM_MODE_HEAT, false);
    put_hw(&s_z[2]);
    s_z[2].hw_on = true;
    put(4, "Dining room", 195, 20, HM_MODE_HEAT, true);
    put(5, "Study", 211, 20, HM_MODE_HEAT, false);
    s_z[4].locked = true;
    put(6, "Bedroom 1", 176, 18, HM_MODE_HEAT, false);
    s_z[5].hold_min = 85;
    put(7, "Bedroom 2", 169, 17, HM_MODE_FROST, false);
    put(8, "Bedroom 3", 172, 18, HM_MODE_HEAT, true);
    put(9, "Bedroom 4", 158, 16, HM_MODE_OFF, false);
    put(10, "Bathroom", 224, 22, HM_MODE_HEAT, false);
    s_z[9].floor_t10 = 262;
    s_z[9].sensor_sel = 3;
    put(11, "Landing", 181, 18, HM_MODE_HEAT, false);
    put(12, "Conservatory", 141, 12, HM_MODE_HEAT, false);
    s_z[11].error_code = 0xE0;
    s_dirty = 0xFFFFFFFFu;
}

void fake_clear(void)
{
    memset(s_used, 0, sizeof s_used);
    s_dirty = 0xFFFFFFFFu;
}

void fake_set_scan(bool running, uint8_t pass, uint8_t next, uint8_t found)
{
    s_scan.running = running;
    s_scan.pass = pass;
    s_scan.next = next;
    s_scan.found = found;
    s_scan.added = 0;
}

void fake_set_boost(uint8_t addr, unsigned left)
{
    zone_view_t *v = &s_z[addr - 1];
    v->hw_mode = left ? ZONE_HW_BOOST : ZONE_HW_TIMER;
    v->boost_left_min = (uint16_t)left;
    v->hw_on = left ? true : v->hw_on;
    s_dirty |= 1u << (addr - 1);
}

void fake_pending_set(uint8_t addr, int c)
{
    s_z[addr - 1].pend.set_temp = true;
    s_z[addr - 1].pend.set_temp_c = (uint8_t)c;
    s_dirty |= 1u << (addr - 1);
}

int zone_store_init(void) { return 0; }

uint32_t zone_store_list(void)
{
    uint32_t m = 0;
    for (int i = 0; i < HM_SCHED_MAX_STATS; i++) {
        if (s_used[i]) {
            m |= 1u << i;
        }
    }
    return m;
}

size_t zone_store_count(void) { return (size_t)__builtin_popcount(zone_store_list()); }

bool zone_store_get(uint8_t addr, zone_view_t *out)
{
    if (addr < 1 || addr > 32 || !s_used[addr - 1]) {
        return false;
    }
    *out = s_z[addr - 1];
    return true;
}

uint32_t zone_store_take_dirty(void)
{
    uint32_t d = s_dirty;
    s_dirty = 0;
    return d;
}

zone_err_t zone_store_add(uint8_t addr) { (void)addr; return ZONE_OK; }

zone_err_t zone_store_remove(uint8_t addr)
{
    s_used[addr - 1] = false;
    s_dirty |= ZONE_DIRTY_LIST;
    return ZONE_OK;
}

zone_err_t zone_store_rename(uint8_t addr, const char *name)
{
    if (!zone_clean_name(name, s_z[addr - 1].cfg.name)) {
        return ZONE_ERR_RANGE;
    }
    s_dirty |= 1u << (addr - 1);
    return ZONE_OK;
}

static zone_err_t check(uint8_t addr)
{
    if (addr < 1 || addr > 32 || !s_used[addr - 1]) {
        return ZONE_ERR_ADDR;
    }
    if (s_z[addr - 1].link == HM_LINK_OFFLINE) {
        return ZONE_ERR_OFFLINE;
    }
    s_dirty |= 1u << (addr - 1);
    return ZONE_OK;
}

zone_err_t zone_store_set_temp(uint8_t addr, int c)
{
    zone_err_t e = check(addr);
    if (!e) {
        s_z[addr - 1].set_temp = (uint8_t)c;
    }
    return e;
}

zone_err_t zone_store_set_mode(uint8_t addr, hm_mode_t m)
{
    zone_err_t e = check(addr);
    if (!e) {
        s_z[addr - 1].mode = m;
    }
    return e;
}

zone_err_t zone_store_set_lock(uint8_t addr, bool l)
{
    zone_err_t e = check(addr);
    if (!e) {
        s_z[addr - 1].locked = l;
    }
    return e;
}

zone_err_t zone_store_set_hold(uint8_t addr, unsigned m)
{
    zone_err_t e = check(addr);
    if (!e) {
        s_z[addr - 1].hold_min = (uint16_t)m;
    }
    return e;
}

zone_err_t zone_store_set_hw(uint8_t addr, int cmd)
{
    zone_err_t e = check(addr);
    if (!e) {
        zone_view_t *v = &s_z[addr - 1];
        v->hw_mode = cmd == HM_HW_FORCE_ON ? ZONE_HW_ON : cmd == HM_HW_FORCE_OFF ? ZONE_HW_OFF : ZONE_HW_TIMER;
        v->boost_left_min = 0;
        if (cmd != HM_HW_FOLLOW_TIMER) {
            v->hw_on = cmd == HM_HW_FORCE_ON;
        }
    }
    return e;
}

zone_err_t zone_store_hw_boost(uint8_t addr, unsigned minutes)
{
    zone_err_t e = check(addr);
    if (!e) {
        fake_set_boost(addr, minutes);
    }
    return e;
}

zone_err_t zone_store_set_comfort(uint8_t addr, int day, const hm_period_t p[4])
{
    zone_err_t e = check(addr);
    if (!e && (day < 0 || day >= s_z[addr - 1].comfort_days || !zone_periods_ok(p))) {
        e = ZONE_ERR_RANGE;
    }
    if (!e) {
        memcpy(s_z[addr - 1].comfort[day], p, sizeof(hm_period_t) * 4);
    }
    return e;
}

zone_err_t zone_store_set_hw_timer(uint8_t addr, int day, const hm_period_t on[4], const hm_period_t off[4])
{
    zone_err_t e = check(addr);
    if (!e && (day < 0 || day >= s_z[addr - 1].hw_timer_days)) {
        e = ZONE_ERR_RANGE;
    }
    if (!e) {
        memcpy(s_z[addr - 1].hw_on_at[day], on, sizeof(hm_period_t) * 4);
        memcpy(s_z[addr - 1].hw_off_at[day], off, sizeof(hm_period_t) * 4);
    }
    return e;
}

static int online_count(void)
{
    int n = 0;
    for (int i = 0; i < HM_SCHED_MAX_STATS; i++) {
        n += s_used[i] && s_z[i].link != HM_LINK_OFFLINE;
    }
    return n;
}

int zone_store_sync_clocks(void) { return online_count(); }

int zone_store_holiday_all(unsigned hours)
{
    for (int i = 0; i < HM_SCHED_MAX_STATS; i++) {
        if (s_used[i] && s_z[i].link != HM_LINK_OFFLINE) {
            s_z[i].holiday_h = (uint16_t)hours;
            s_dirty |= 1u << i;
        }
    }
    return online_count();
}

/* ---- bus diagnostics ---- */

static uint32_t s_poll_ms = 10000;

bool zone_store_diag(uint8_t addr, zone_diag_t *out)
{
    if (addr < 1 || addr > 32 || !s_used[addr - 1]) {
        return false;
    }
    memset(out, 0, sizeof *out);
    const bool off = s_z[addr - 1].link == HM_LINK_OFFLINE;
    out->polls = 1204 - (off ? 14 : 0) - (addr == 3);
    out->timeouts = off ? 214 : addr == 2 ? 1 : 0;
    out->crc = addr == 4 ? 2 : addr == 8 ? 1 : 0;
    out->poll_ok = out->polls - out->timeouts - out->crc;
    out->seen = true;
    out->seen_age_s = off ? 12 * 60 : 2 + addr % 7;
    return true;
}

size_t zone_store_frames(zone_frame_t *out, size_t max)
{
    size_t n = 0;
    for (uint8_t a = 3; a <= 7 && n + 1 < max; a++) {
        zone_frame_t *t = &out[n++];
        memset(t, 0, sizeof *t);
        t->tx = true;
        t->len = 10;
        t->n = 10;
        const uint8_t tx[10] = { a, 0x0A, 0x81, 0, 0, 0, 0xFF, 0xFF, (uint8_t)(0x80 + a * 7), (uint8_t)(a * 31) };
        memcpy(t->b, tx, 10);
        zone_frame_t *r = &out[n++];
        memset(r, 0, sizeof *r);
        r->timeout_ms = 1000;
        if (s_used[a - 1] && s_z[a - 1].link == HM_LINK_OFFLINE) {
            r->rx = ZONE_RX_TIMEOUT;
            continue;
        }
        r->len = 75;
        r->n = 10;
        const uint8_t rx[10] = { 0x81, 0x4B, 0, a, 0, 0, 0, 0x40, 0, 0x41 };
        memcpy(r->b, rx, 10);
    }
    return n;
}

void zone_store_clear_counters(void) {}

zone_err_t zone_store_read_now(uint8_t addr) { return s_used[addr - 1] ? ZONE_OK : ZONE_ERR_ADDR; }

void zone_store_bus_info(zone_bus_info_t *out)
{
    out->poll_ms = s_poll_ms;
    out->master = 0x81;
    out->baud = 4800;
}

zone_err_t zone_store_set_poll_interval(uint32_t ms)
{
    if (ms < 2000 || ms > 120000) {
        return ZONE_ERR_RANGE;
    }
    s_poll_ms = ms;
    return ZONE_OK;
}

void zone_store_scan(void) { fake_set_scan(true, 1, 1, 0); }

void zone_store_scan_state(zone_scan_t *out) { *out = s_scan; }

void fake_notice(const char *msg) { snprintf(s_notice, sizeof s_notice, "%s", msg); }

bool zone_store_pop_notice(char *buf, size_t cap)
{
    if (!s_notice[0]) {
        return false;
    }
    snprintf(buf, cap, "%s", s_notice);
    s_notice[0] = 0;
    return true;
}
