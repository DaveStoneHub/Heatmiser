/* Host tests for the zone view to Matter mapping (components/matter_bridge/matter_map.c). */
#include <string.h>

#include "matter_map.h"
#include "test_util.h"

static void base_view(zone_view_t *v)
{
    memset(v, 0, sizeof *v);
    v->cfg.addr = 2;
    v->link = HM_LINK_ONLINE;
    v->have = true;
    v->room_t10 = 205;
    v->set_temp = 21;
    v->mode = HM_MODE_HEAT;
    v->heating = true;
}

static void test_map_heating(void)
{
    zone_view_t v;
    mm_thermo_t m;
    base_view(&v);
    mm_from_view(&v, &m);
    CHECK(m.reachable);
    CHECK(!m.local_null);
    CHECK_EQ(m.local_t, 2050);
    CHECK_EQ(m.heat_setpoint, 2100);
    CHECK_EQ(m.system_mode, MM_SYSTEM_MODE_HEAT);
    CHECK_EQ(m.running_state, MM_RUNNING_HEAT);
    CHECK_EQ(m.pi_heating, 100);
    CHECK_EQ(m.hold, 0);
    CHECK(m.hold_null);
    CHECK_EQ(m.keypad_lockout, 0);
    CHECK_EQ(m.display_mode, 0);
}

static void test_map_frost_off_offline(void)
{
    zone_view_t v;
    mm_thermo_t m;
    base_view(&v);
    v.mode = HM_MODE_FROST;
    v.heating = false;
    v.locked = true;
    v.hold_min = 90;
    mm_from_view(&v, &m);
    CHECK_EQ(m.system_mode, MM_SYSTEM_MODE_OFF);
    CHECK_EQ(m.running_state, 0);
    CHECK_EQ(m.pi_heating, 0);
    CHECK_EQ(m.keypad_lockout, 1);
    CHECK_EQ(m.hold, 1);
    CHECK(!m.hold_null);
    CHECK_EQ(m.hold_min, 90);

    v.mode = HM_MODE_OFF; /* switched off at the stat */
    mm_from_view(&v, &m);
    CHECK_EQ(m.system_mode, MM_SYSTEM_MODE_OFF);

    v.link = HM_LINK_OFFLINE;
    v.room_t10 = ZONE_T_ABSENT;
    mm_from_view(&v, &m);
    CHECK(!m.reachable);
    CHECK(m.local_null);

    /* Added but never read: reachable until it misses its polls, no reading. */
    memset(&v, 0, sizeof v);
    v.link = HM_LINK_UNKNOWN;
    mm_from_view(&v, &m);
    CHECK(m.reachable);
    CHECK(m.local_null);
    CHECK_EQ(m.heat_setpoint, 2000);
    CHECK_EQ(m.system_mode, MM_SYSTEM_MODE_HEAT);
}

static void test_map_pending(void)
{
    zone_view_t v;
    mm_thermo_t m;
    base_view(&v);
    v.pend.set_temp = true;
    v.pend.set_temp_c = 23;
    v.pend.mode = true;
    v.pend.mode_v = HM_MODE_FROST;
    v.pend.lock = true;
    v.pend.lock_v = true;
    mm_from_view(&v, &m);
    CHECK_EQ(m.heat_setpoint, 2300);
    CHECK_EQ(m.system_mode, MM_SYSTEM_MODE_OFF);
    CHECK_EQ(m.keypad_lockout, 1);
}

static void test_map_writes(void)
{
    CHECK_EQ(mm_setpoint_to_c(2100), 21);
    CHECK_EQ(mm_setpoint_to_c(2149), 21);
    CHECK_EQ(mm_setpoint_to_c(2150), 22);
    CHECK_EQ(mm_setpoint_to_c(2050), 21);
    CHECK_EQ(mm_setpoint_to_c(400), 5);
    CHECK_EQ(mm_setpoint_to_c(-300), 5);
    CHECK_EQ(mm_setpoint_to_c(3600), 35);
    hm_mode_t mode = HM_MODE_OFF;
    CHECK(mm_mode_from_system(MM_SYSTEM_MODE_HEAT, &mode));
    CHECK_EQ(mode, HM_MODE_HEAT);
    CHECK(mm_mode_from_system(MM_SYSTEM_MODE_OFF, &mode));
    CHECK_EQ(mode, HM_MODE_FROST);
    CHECK(!mm_mode_from_system(3, &mode)); /* Cool */
    CHECK(!mm_mode_from_system(1, &mode)); /* Auto */
}

void test_matter_all(void)
{
    test_map_heating();
    test_map_frost_off_offline();
    test_map_pending();
    test_map_writes();
}
