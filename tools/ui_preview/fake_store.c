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
}

void fake_load_house(void)
{
    memset(s_used, 0, sizeof s_used);
    put(1, "Living room", 203, 21, HM_MODE_HEAT, true);
    put(2, "Kitchen", 0, 0, HM_MODE_HEAT, false);
    s_z[1].have = false;
    s_z[1].link = HM_LINK_OFFLINE;
    put(3, "Hall", 188, 19, HM_MODE_HEAT, false);
    s_z[2].model = s_z[2].cfg.model = HM_MODEL_PRTHW;
    s_z[2].is_prthw = true;
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
    if (!e && cmd != HM_HW_FOLLOW_TIMER) {
        s_z[addr - 1].hw_on = cmd == HM_HW_FORCE_ON;
    }
    return e;
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
