/* Host tests for the zone view (components/zone_store/zone_view.c). */
#include <string.h>

#include "test_util.h"
#include "zone_store.h"
#include "zone_view.h"
#include "hm_write.h"

/* PRT-N, 5/2 mode: set 21 C, air 20.5 C, heating on, holiday 0x0102 h,
 * hold 90 min, frost 12 C, frost protection byte 0x01 (disabled). */
static void make_stat(hm_stat_t *st, uint32_t last_ok)
{
    static const uint8_t base[64] = {
        /* 0 */ 0x00, 0x40, 0x00, 0x05, 0x02, 0x00, 0x01, 0x01,
        /* 8 */ 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 20,
        /*16 */ 0x00, 12, 21, 28, 0x01, 0x01, 0x00, 0x00,
        /*24 */ 0x01, 0x02, 0x00, 90, 0xFF, 0xFF, 0xFF, 0xFF,
        /*32 */ 0x00, 205, 0x00, 0x01, 2, 14, 5, 30,
        /*40 */ 7, 0, 21, 9, 0, 16, 16, 0, 21, 22, 0, 16,
        /*52 */ 9, 0, 21, 22, 0, 16, 24, 0, 16, 24, 0, 16,
    };
    memset(st, 0, sizeof *st);
    st->used = true;
    st->addr = 3;
    st->link = HM_LINK_ONLINE;
    st->have_dcb = true;
    st->dcb_len = sizeof base;
    memcpy(st->dcb, base, sizeof base);
    st->last_ok_ms = last_ok;
}

static void test_view_from_stat(void)
{
    hm_stat_t st;
    make_stat(&st, 1000);
    zone_view_t v;
    memset(&v, 0, sizeof v);
    zone_view_from_stat(&st, 13500, &v);
    CHECK(v.have);
    CHECK_EQ(v.age_s, 12);
    CHECK_EQ(v.model, HM_MODEL_PRT);
    CHECK(!v.is_prthw);
    CHECK_EQ(v.room_t10, 205);
    CHECK_EQ(v.air_t10, 205);
    CHECK_EQ(v.floor_t10, ZONE_T_ABSENT);
    CHECK_EQ(v.remote_t10, ZONE_T_ABSENT);
    CHECK_EQ(v.set_temp, 21);
    CHECK_EQ(v.frost_temp, 12);
    CHECK(!v.frost_enabled);
    CHECK_EQ(v.mode, HM_MODE_HEAT);
    CHECK(v.heating);
    CHECK(!v.locked);
    CHECK_EQ(v.hold_min, 90);
    CHECK_EQ(v.holiday_h, 0x0102);
    CHECK_EQ(v.error_code, 0);
    CHECK(strcmp(zone_state_str(&v), "Holiday") == 0);

    /* Frost mode and key lock. */
    st.dcb[23] = 1;
    st.dcb[22] = 1;
    st.dcb[24] = st.dcb[25] = 0;
    zone_view_from_stat(&st, 13500, &v);
    CHECK_EQ(v.mode, HM_MODE_FROST);
    CHECK(v.locked);
    CHECK(strcmp(zone_state_str(&v), "Frost") == 0);

    /* Heat, holding, not heating. */
    st.dcb[23] = 0;
    st.dcb[35] = 0;
    zone_view_from_stat(&st, 13500, &v);
    CHECK(strcmp(zone_state_str(&v), "Hold") == 0);
    st.dcb[26] = st.dcb[27] = 0;
    zone_view_from_stat(&st, 13500, &v);
    CHECK(strcmp(zone_state_str(&v), "Idle") == 0);

    /* Floor sensor only: room temperature comes from the floor. */
    st.dcb[13] = 2;
    st.dcb[30] = 0x01;
    st.dcb[31] = 0x04; /* 26.0 */
    zone_view_from_stat(&st, 13500, &v);
    CHECK_EQ(v.room_t10, 260);
    CHECK_EQ(v.floor_t10, 260);

    /* Off. */
    st.dcb[21] = 0;
    zone_view_from_stat(&st, 13500, &v);
    CHECK_EQ(v.mode, HM_MODE_OFF);
    CHECK(strcmp(zone_state_str(&v), "Off") == 0);

    /* Offline wins over everything; no DCB means not have. */
    st.link = HM_LINK_OFFLINE;
    zone_view_from_stat(&st, 13500, &v);
    CHECK(strcmp(zone_state_str(&v), "Offline") == 0);
    st.link = HM_LINK_UNKNOWN;
    st.have_dcb = false;
    zone_view_from_stat(&st, 13500, &v);
    CHECK(!v.have);
    CHECK(strcmp(zone_state_str(&v), "Connecting") == 0);
}

static void test_pending(void)
{
    hm_stat_t st;
    make_stat(&st, 0);
    zone_view_t v;
    memset(&v, 0, sizeof v);
    zone_view_from_stat(&st, 0, &v);
    CHECK_EQ(zone_view_shown_set(&v), 21);
    CHECK(!zone_view_busy(&v));
    v.pend.set_temp = true;
    v.pend.set_temp_c = 23;
    CHECK_EQ(zone_view_shown_set(&v), 23);
    CHECK(zone_view_busy(&v));
    v.pend.mode = true;
    v.pend.mode_v = HM_MODE_OFF;
    CHECK_EQ(zone_view_shown_mode(&v), HM_MODE_OFF);
    v.pend.lock = true;
    v.pend.lock_v = true;
    CHECK(zone_view_shown_lock(&v));
    /* A refresh from the stat leaves the pending part alone. */
    zone_view_from_stat(&st, 0, &v);
    CHECK_EQ(zone_view_shown_set(&v), 23);
    CHECK_EQ(v.set_temp, 21);
}

static void test_format(void)
{
    char b[16];
    zone_fmt_t10(205, b, sizeof b);
    CHECK(strcmp(b, "20.5") == 0);
    zone_fmt_t10(0, b, sizeof b);
    CHECK(strcmp(b, "0.0") == 0);
    zone_fmt_t10(ZONE_T_ABSENT, b, sizeof b);
    CHECK(strcmp(b, "--.-") == 0);
    zone_default_name(3, HM_MODEL_PRTHW, b, sizeof b);
    CHECK(strcmp(b, "Zone 3") == 0);
    zone_default_name(12, HM_MODEL_PRT, b, sizeof b);
    CHECK(strcmp(b, "Zone 12") == 0);
    CHECK(zone_error_str(0) == NULL);
    CHECK(strcmp(zone_error_str(0xE0), "Air sensor fault") == 0);
    CHECK(strcmp(zone_error_str(0xE1), "Floor sensor fault") == 0);
    CHECK(strcmp(zone_error_str(0x42), "Stat fault") == 0);
    CHECK(strcmp(zone_sensor_str(4), "Remote air") == 0);
    CHECK(strcmp(zone_err_str(ZONE_ERR_OFFLINE), "stat is offline") == 0);
}

static void test_clean_name(void)
{
    char n[ZONE_NAME_MAX + 1];
    CHECK(zone_clean_name("  Living  room  ", n));
    CHECK(strcmp(n, "Living  room") == 0);
    CHECK(zone_clean_name("A very long room name indeed", n));
    CHECK_EQ(strlen(n), ZONE_NAME_MAX);
    CHECK(strcmp(n, "A very long room") == 0);
    CHECK(zone_clean_name("Caf\xC3\xA9\tBar", n));
    CHECK(strcmp(n, "CafBar") == 0);
    CHECK(!zone_clean_name("   ", n));
    CHECK(!zone_clean_name("", n));
}

static void test_list_blob(void)
{
    zone_cfg_t in[3] = {
        { .addr = 1, .model = HM_MODEL_PRT, .endpoint = 0, .name = "Living room" },
        { .addr = 3, .model = HM_MODEL_PRTHW, .endpoint = 0x1234, .name = "Hall" },
        { .addr = 32, .model = 0xFF, .endpoint = 7, .name = "Sixteen chars ok" },
    };
    uint8_t blob[ZONE_LIST_MAX_BYTES];
    size_t n = zone_list_encode(in, 3, blob, sizeof blob);
    CHECK_EQ(n, 2 + 3 * ZONE_LIST_ENTRY_SIZE);
    CHECK_EQ(blob[0], ZONE_LIST_VERSION);
    CHECK_EQ(blob[1], 3);
    zone_cfg_t out[HM_SCHED_MAX_STATS];
    CHECK_EQ(zone_list_decode(blob, n, out, HM_SCHED_MAX_STATS), 3);
    for (int i = 0; i < 3; i++) {
        CHECK_EQ(out[i].addr, in[i].addr);
        CHECK_EQ(out[i].model, in[i].model);
        CHECK_EQ(out[i].endpoint, in[i].endpoint);
        CHECK(strcmp(out[i].name, in[i].name) == 0);
    }
    /* Empty list. */
    n = zone_list_encode(in, 0, blob, sizeof blob);
    CHECK_EQ(n, 2);
    CHECK_EQ(zone_list_decode(blob, n, out, HM_SCHED_MAX_STATS), 0);

    /* Rejects: unknown version, wrong length, unsorted, bad address, too small a buffer. */
    n = zone_list_encode(in, 3, blob, sizeof blob);
    blob[0] = 2;
    CHECK_EQ(zone_list_decode(blob, n, out, HM_SCHED_MAX_STATS), -1);
    blob[0] = ZONE_LIST_VERSION;
    CHECK_EQ(zone_list_decode(blob, n - 1, out, HM_SCHED_MAX_STATS), -1);
    CHECK_EQ(zone_list_decode(blob, n, out, 2), -1);
    uint8_t swap[ZONE_LIST_MAX_BYTES];
    memcpy(swap, blob, n);
    swap[2] = 3; /* first entry now repeats the second's address */
    CHECK_EQ(zone_list_decode(swap, n, out, HM_SCHED_MAX_STATS), -1);
    memcpy(swap, blob, n);
    swap[2] = 0;
    CHECK_EQ(zone_list_decode(swap, n, out, HM_SCHED_MAX_STATS), -1);
    CHECK_EQ(zone_list_encode(in, 3, blob, 10), 0);

    /* A blank name decodes to the default one. */
    memcpy(swap, blob, n);
    n = zone_list_encode(in, 3, swap, sizeof swap);
    memset(swap + 2 + ZONE_LIST_ENTRY_SIZE + 4, 0, ZONE_NAME_MAX + 1);
    CHECK_EQ(zone_list_decode(swap, n, out, HM_SCHED_MAX_STATS), 3);
    CHECK(strcmp(out[1].name, "Zone 3") == 0);
}

static void test_programme(void)
{
    /* The 5/2 DCB carries both days of comfort levels. */
    hm_stat_t st;
    make_stat(&st, 0);
    zone_view_t v;
    memset(&v, 0, sizeof v);
    zone_view_from_stat(&st, 0, &v);
    CHECK_EQ(v.comfort_days, 2);
    CHECK_EQ(v.hw_timer_days, 0);
    CHECK(zone_periods_ok(v.comfort[0]));

    CHECK_EQ(zone_prog_day(2, 1), 0);
    CHECK_EQ(zone_prog_day(2, 5), 0);
    CHECK_EQ(zone_prog_day(2, 6), 1);
    CHECK_EQ(zone_prog_day(2, 7), 1);
    CHECK_EQ(zone_prog_day(7, 3), 2);
    CHECK_EQ(zone_prog_day(7, 0), 0);
    CHECK(strcmp(zone_prog_day_name(2, 1), "Weekend") == 0);
    CHECK(strcmp(zone_prog_day_name(7, 6), "Sun") == 0);

    const hm_period_t c[4] = { { 6, 30, 21 }, { 8, 30, 17 }, { 17, 0, 21 }, { 22, 30, 16 } };
    CHECK_EQ(zone_comfort_at(c, 0), 16);       /* last night's level */
    CHECK_EQ(zone_comfort_at(c, 6 * 60 + 30), 21);
    CHECK_EQ(zone_comfort_at(c, 12 * 60), 17);
    CHECK_EQ(zone_comfort_at(c, 23 * 60), 16);
    const hm_period_t two[4] = { { 7, 0, 20 }, { 21, 0, 15 }, { 24, 0, 0 }, { 24, 0, 0 } };
    CHECK(zone_periods_ok(two));
    CHECK_EQ(zone_comfort_at(two, 60), 15);
    CHECK_EQ(zone_comfort_at(two, 8 * 60), 20);
    const hm_period_t none[4] = { { 24, 0, 0 }, { 24, 0, 0 }, { 24, 0, 0 }, { 24, 0, 0 } };
    CHECK_EQ(zone_comfort_at(none, 100), 0);
    CHECK(zone_periods_ok(none));

    /* Out of order, or a gap before a used period, is refused. */
    hm_period_t bad[4] = { { 8, 0, 20 }, { 7, 0, 20 }, { 24, 0, 0 }, { 24, 0, 0 } };
    CHECK(!zone_periods_ok(bad));
    zone_periods_sort(bad, NULL);
    CHECK_EQ(bad[0].hour, 7);
    CHECK(zone_periods_ok(bad));
    hm_period_t gap[4] = { { 24, 0, 0 }, { 9, 0, 20 }, { 24, 0, 0 }, { 6, 15, 18 } };
    CHECK(!zone_periods_ok(gap));
    hm_period_t goff[4] = { { 24, 0, 0 }, { 10, 0, 0 }, { 24, 0, 0 }, { 7, 0, 0 } };
    zone_periods_sort(gap, goff);
    CHECK(zone_periods_ok(gap));
    CHECK_EQ(gap[0].hour, 6);
    CHECK_EQ(gap[0].minute, 15);
    CHECK_EQ(goff[0].hour, 7);   /* off times move with their on times */
    CHECK_EQ(gap[1].hour, 9);
    CHECK_EQ(goff[1].hour, 10);
    CHECK_EQ(gap[2].hour, 24);
    CHECK_EQ(gap[3].hour, 24);

    /* Hot water: on 06:00-08:00 and 17:00 to the end of the day. */
    const hm_period_t on[4] = { { 6, 0, 0 }, { 17, 0, 0 }, { 24, 0, 0 }, { 24, 0, 0 } };
    const hm_period_t off[4] = { { 8, 0, 0 }, { 24, 0, 0 }, { 24, 0, 0 }, { 24, 0, 0 } };
    CHECK(!zone_hw_timer_on(on, off, 5 * 60 + 59));
    CHECK(zone_hw_timer_on(on, off, 6 * 60));
    CHECK(!zone_hw_timer_on(on, off, 8 * 60));
    CHECK(zone_hw_timer_on(on, off, 23 * 60 + 59));
    bool on_after = false;
    CHECK_EQ(zone_hw_timer_next(on, off, 0, &on_after), 360);
    CHECK(on_after);
    CHECK_EQ(zone_hw_timer_next(on, off, 7 * 60, &on_after), 480);
    CHECK(!on_after);
    CHECK_EQ(zone_hw_timer_next(on, off, 18 * 60, &on_after), -1);
}

static void hw_view(zone_view_t *v)
{
    /* Both programme days: on 06:00-08:00 and 17:00-22:00. */
    memset(v, 0, sizeof *v);
    v->cfg.addr = 3;
    v->link = HM_LINK_ONLINE;
    v->have = true;
    v->is_prthw = true;
    v->hw_timer_days = 2;
    const hm_period_t on[4] = { { 6, 0, 0 }, { 17, 0, 0 }, { 24, 0, 0 }, { 24, 0, 0 } };
    const hm_period_t off[4] = { { 8, 0, 0 }, { 22, 0, 0 }, { 24, 0, 0 }, { 24, 0, 0 } };
    memcpy(v->hw_on_at[0], on, sizeof on);
    memcpy(v->hw_off_at[0], off, sizeof off);
    memcpy(v->hw_on_at[1], on, sizeof on);
    memcpy(v->hw_off_at[1], off, sizeof off);
}

static void test_hw_switch_plan(void)
{
    zone_view_t v;
    zone_hw_plan_t p;
    hw_view(&v);

    /* Timer off (Mon 12:00), water off: On boosts, Off does nothing. */
    CHECK(zone_hw_switch_plan(&v, true, 1, 12 * 60, 60, &p));
    CHECK_EQ(p.cmd, HM_HW_FORCE_ON);
    CHECK_EQ(p.mode, ZONE_HW_BOOST);
    CHECK_EQ(p.minutes, 60);
    CHECK(!zone_hw_switch_plan(&v, false, 1, 12 * 60, 60, &p));

    /* Timer on (Mon 07:15): On does nothing, Off holds it off until 08:00. */
    v.hw_on = true;
    CHECK(!zone_hw_switch_plan(&v, true, 1, 7 * 60 + 15, 60, &p));
    CHECK(zone_hw_switch_plan(&v, false, 1, 7 * 60 + 15, 60, &p));
    CHECK_EQ(p.cmd, HM_HW_FORCE_OFF);
    CHECK_EQ(p.mode, ZONE_HW_OFF);
    CHECK_EQ(p.minutes, 45);

    /* Held off while the timer is on: On hands it back to the timer. */
    v.hw_on = false;
    v.hw_mode = ZONE_HW_OFF;
    CHECK(zone_hw_switch_plan(&v, true, 1, 7 * 60 + 15, 60, &p));
    CHECK_EQ(p.cmd, HM_HW_FOLLOW_TIMER);
    CHECK_EQ(p.mode, ZONE_HW_TIMER);
    CHECK_EQ(p.minutes, 0);
    CHECK(!zone_hw_switch_plan(&v, false, 1, 7 * 60 + 15, 60, &p));

    /* Boosting with the timer off: On does nothing, Off goes back to the timer. */
    v.hw_mode = ZONE_HW_BOOST;
    v.hw_on = true;
    CHECK(!zone_hw_switch_plan(&v, true, 1, 12 * 60, 60, &p));
    CHECK(zone_hw_switch_plan(&v, false, 1, 12 * 60, 60, &p));
    CHECK_EQ(p.cmd, HM_HW_FOLLOW_TIMER);

    /* Forced on from the screen, then Off from Matter in a timer period
     * (Saturday 21:00): held off until 22:00. */
    v.hw_mode = ZONE_HW_ON;
    CHECK(zone_hw_switch_plan(&v, false, 6, 21 * 60, 60, &p));
    CHECK_EQ(p.cmd, HM_HW_FORCE_OFF);
    CHECK_EQ(p.minutes, 60);

    /* A period on to the end of the day: held off until midnight. */
    v.hw_off_at[0][1].hour = 24;
    v.hw_mode = ZONE_HW_TIMER;
    CHECK(zone_hw_switch_plan(&v, false, 2, 23 * 60, 60, &p));
    CHECK_EQ(p.minutes, 60);

    /* No clock: Off forces off for the boost length, never for good. */
    CHECK(zone_hw_switch_plan(&v, false, 0, 0, 60, &p));
    CHECK_EQ(p.cmd, HM_HW_FORCE_OFF);
    CHECK_EQ(p.minutes, 60);
}

static void test_hw_blob(void)
{
    const zone_hw_saved_t in[2] = { { 3, ZONE_HW_BOOST, 1795000000u }, { 9, ZONE_HW_ON, 0 } };
    zone_hw_saved_t out[HM_SCHED_MAX_STATS];
    uint8_t blob[ZONE_HW_BLOB_MAX];
    size_t n = zone_hw_encode(in, 2, blob, sizeof blob);
    CHECK_EQ(n, 2 + 2 * ZONE_HW_ENTRY_SIZE);
    CHECK_EQ(zone_hw_decode(blob, n, out, HM_SCHED_MAX_STATS), 2);
    CHECK_EQ(out[0].addr, 3);
    CHECK_EQ(out[0].mode, ZONE_HW_BOOST);
    CHECK(out[0].end_utc == 1795000000u);
    CHECK_EQ(out[1].addr, 9);
    CHECK_EQ(out[1].mode, ZONE_HW_ON);
    CHECK(out[1].end_utc == 0);
    CHECK_EQ(zone_hw_decode(blob, n - 1, out, HM_SCHED_MAX_STATS), -1);
    blob[0] = 2;
    CHECK_EQ(zone_hw_decode(blob, n, out, HM_SCHED_MAX_STATS), -1);
    blob[0] = ZONE_HW_BLOB_VERSION;
    blob[2] = 0; /* address 0 */
    CHECK_EQ(zone_hw_decode(blob, n, out, HM_SCHED_MAX_STATS), -1);
    CHECK_EQ(zone_hw_encode(in, 0, blob, sizeof blob), 2);
    CHECK_EQ(zone_hw_decode(blob, 2, out, HM_SCHED_MAX_STATS), 0);
}

void test_zone_all(void)
{
    test_hw_switch_plan();
    test_hw_blob();
    test_programme();
    test_view_from_stat();
    test_pending();
    test_format();
    test_clean_name();
    test_list_blob();
}
