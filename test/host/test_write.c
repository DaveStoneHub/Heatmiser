/* Host tests: the DCB address map, every field decode, write encoders and
 * read-back checks. */
#include <string.h>

#include "hm_crc.h"
#include "hm_dcb.h"
#include "hm_frame.h"
#include "hm_write.h"
#include "test_util.h"

/* ---- Address map ---------------------------------------------------------- */

static void test_index_prt(void)
{
    const uint8_t m = HM_MODEL_PRT;
    CHECK_EQ(hm_dcb_index(m, 0, 0), 0);
    CHECK_EQ(hm_dcb_index(m, 0, 18), 18);
    CHECK_EQ(hm_dcb_index(m, 0, 25), 25);
    for (int ua = 26; ua <= 31; ua++) {
        CHECK_EQ(hm_dcb_index(m, 0, (uint16_t)ua), -1);
    }
    CHECK_EQ(hm_dcb_index(m, 0, 32), 26);
    CHECK_EQ(hm_dcb_index(m, 0, 38), 32);
    CHECK_EQ(hm_dcb_index(m, 0, 41), 35);
    CHECK_EQ(hm_dcb_index(m, 0, 42), -1);  /* hot water is PRTHW only */
    CHECK_EQ(hm_dcb_index(m, 0, 43), 36);
    CHECK_EQ(hm_dcb_index(m, 0, 47), 40);
    CHECK_EQ(hm_dcb_index(m, 0, 59), 52);
    CHECK_EQ(hm_dcb_index(m, 0, 70), 63);
    CHECK_EQ(hm_dcb_index(m, 0, 71), -1);
    CHECK_EQ(hm_dcb_index(m, 0, 103), -1); /* 7-day blocks absent in 5/2 */
    CHECK_EQ(hm_dcb_index(m, 1, 103), 64);
    CHECK_EQ(hm_dcb_index(m, 1, 151), 112); /* Friday, from the spec table */
    CHECK_EQ(hm_dcb_index(m, 1, 175), 136);
    CHECK_EQ(hm_dcb_index(m, 1, 186), 147);
    CHECK_EQ(hm_dcb_index(m, 1, 187), -1);
    CHECK_EQ(hm_dcb_index(HM_MODEL_PRT_E, 0, 43), 36);
    CHECK_EQ(hm_dcb_expected_len(m, 0), 64);
    CHECK_EQ(hm_dcb_expected_len(m, 1), 148);
}

static void test_index_prthw(void)
{
    const uint8_t m = HM_MODEL_PRTHW;
    CHECK_EQ(hm_dcb_index(m, 0, 24), 24);
    CHECK_EQ(hm_dcb_index(m, 0, 28), -1);
    CHECK_EQ(hm_dcb_index(m, 0, 32), 26);
    CHECK_EQ(hm_dcb_index(m, 0, 42), 36);
    CHECK_EQ(hm_dcb_index(m, 0, 43), 37);
    CHECK_EQ(hm_dcb_index(m, 0, 47), 41);
    CHECK_EQ(hm_dcb_index(m, 0, 59), 53);
    CHECK_EQ(hm_dcb_index(m, 0, 71), 65);
    CHECK_EQ(hm_dcb_index(m, 0, 87), 81);
    CHECK_EQ(hm_dcb_index(m, 0, 102), 96);
    CHECK_EQ(hm_dcb_index(m, 0, 103), -1);
    CHECK_EQ(hm_dcb_index(m, 1, 103), 97);
    CHECK_EQ(hm_dcb_index(m, 1, 127), 121); /* Wednesday, from the spec table */
    CHECK_EQ(hm_dcb_index(m, 1, 187), 181);
    CHECK_EQ(hm_dcb_index(m, 1, 283), 277);
    CHECK_EQ(hm_dcb_index(m, 1, 298), 292);
    CHECK_EQ(hm_dcb_index(m, 1, 299), -1);
    CHECK_EQ(hm_dcb_expected_len(m, 0), 97);
    CHECK_EQ(hm_dcb_expected_len(m, 1), 293);
}

static void test_index_dt(void)
{
    CHECK_EQ(hm_dcb_index(HM_MODEL_DT, 0, 41), 35);
    CHECK_EQ(hm_dcb_index(HM_MODEL_DT, 0, 43), -1);
    CHECK_EQ(hm_dcb_index(HM_MODEL_DT_E, 0, 47), -1);
    CHECK_EQ(hm_dcb_expected_len(HM_MODEL_DT, 0), 36);
    CHECK_EQ(hm_dcb_expected_len(0x77, 0), 0);
}

/* ---- Every field decodes from its own index ----------------------------- */

static void test_decode_every_field(void)
{
    /* Give every byte a distinct value so a field read from the wrong index
     * shows up. Then fix the bytes the decoder interprets. */
    uint8_t d[64];
    for (int i = 0; i < 64; i++) {
        d[i] = (uint8_t)(0x40 + i);
    }
    d[0] = 0;
    d[1] = 64;
    d[3] = 0x80 | 21;
    d[4] = HM_MODEL_PRT_E;
    d[16] = 0; /* 5/2 */
    hm_dcb_t x;
    CHECK_EQ(hm_dcb_decode(d, sizeof d, &x), 0);
    CHECK_EQ(x.dcb_len, 64);
    CHECK_EQ(x.vendor, 0x42);
    CHECK_EQ(x.version, 21);
    CHECK(x.floor_limit_active);
    CHECK_EQ(x.model, HM_MODEL_PRT_E);
    CHECK_EQ(x.temp_format, 0x45);
    CHECK_EQ(x.switch_diff, 0x46);
    CHECK_EQ(x.frost_raw, 0x47);
    CHECK(!x.frost_enabled);
    CHECK_EQ(x.calibration_raw, 0x4849);
    CHECK_EQ(x.output_delay, 0x4A);
    CHECK_EQ(x.address, 0x4B);
    CHECK_EQ(x.updown_limit, 0x4C);
    CHECK_EQ(x.sensor_sel, 0x4D);
    CHECK_EQ(x.optimum_start, 0x4E);
    CHECK_EQ(x.rate_of_change, 0x4F);
    CHECK_EQ(x.prog_mode, 0);
    CHECK_EQ(x.frost_temp, 0x51);
    CHECK_EQ(x.set_temp, 0x52);
    CHECK_EQ(x.floor_max, 0x53);
    CHECK_EQ(x.floor_max_enable, 0x54);
    CHECK_EQ(x.on_off, 0x55);
    CHECK_EQ(x.key_lock, 0x56);
    CHECK_EQ(x.run_mode, 0x57);
    CHECK_EQ(x.holiday_hours, 0x5859);
    CHECK_EQ(x.hold_minutes, 0x5A5B);
    CHECK_EQ(x.remote_air_t10, 0x5C5D);
    CHECK_EQ(x.floor_t10, 0x5E5F);
    CHECK_EQ(x.air_t10, 0x6061);
    CHECK_EQ(x.error_code, 0x62);
    CHECK_EQ(x.heating_on, 0x63);
    CHECK(!x.has_hw);
    CHECK(x.has_clock);
    CHECK_EQ(x.clk_wday, 0x64);
    CHECK_EQ(x.clk_hour, 0x65);
    CHECK_EQ(x.clk_min, 0x66);
    CHECK_EQ(x.clk_sec, 0x67);
    CHECK_EQ(x.comfort_days, 2);
    CHECK_EQ(x.comfort[0][0].hour, 0x68);
    CHECK_EQ(x.comfort[0][0].minute, 0x69);
    CHECK_EQ(x.comfort[0][0].temp, 0x6A);
    CHECK_EQ(x.comfort[0][3].temp, 0x73);
    CHECK_EQ(x.comfort[1][0].hour, 0x74);
    CHECK_EQ(x.comfort[1][3].temp, 0x7F);

    /* The same check for a PRTHW: everything from 36 on shifts by one. */
    uint8_t h[97];
    for (int i = 0; i < 97; i++) {
        h[i] = (uint8_t)(0x80 + i);
    }
    h[0] = 0;
    h[1] = 97;
    h[4] = HM_MODEL_PRTHW;
    h[16] = 0;
    CHECK_EQ(hm_dcb_decode(h, sizeof h, &x), 0);
    CHECK_EQ(x.version, 0x83);
    CHECK(!x.floor_limit_active);
    CHECK_EQ(x.heating_on, 0xA3);
    CHECK(x.has_hw);
    CHECK_EQ(x.hw_on, 0xA4);
    CHECK_EQ(x.clk_wday, 0xA5);
    CHECK_EQ(x.clk_sec, 0xA8);
    CHECK_EQ(x.comfort[0][0].hour, 0xA9);
    CHECK_EQ(x.comfort[1][3].temp, 0xC0);
    CHECK_EQ(x.hw_timer_days, 2);
    CHECK_EQ(x.hw_on_at[0][0].hour, 0xC1);
    CHECK_EQ(x.hw_on_at[0][0].minute, 0xC2);
    CHECK_EQ(x.hw_off_at[0][0].hour, 0xC3);
    CHECK_EQ(x.hw_off_at[0][0].minute, 0xC4);
    CHECK_EQ(x.hw_off_at[1][3].minute, 0xE0);
}

static void test_decode_prthw_7day(void)
{
    uint8_t h[293];
    memset(h, 0, sizeof h);
    h[1] = 37;   /* 293 = 0x0125 */
    h[0] = 1;
    h[4] = HM_MODEL_PRTHW;
    h[16] = 1;
    h[97] = 6;   /* Monday comfort, unique 103 */
    h[97 + 6 * 12] = 8;  /* Sunday comfort, unique 175 */
    h[181] = 5;  /* Monday HW on, unique 187 */
    h[181 + 6 * 16 + 15] = 45; /* Sunday last off minute = last DCB byte */
    hm_dcb_t x;
    CHECK_EQ(hm_dcb_decode(h, sizeof h, &x), 0);
    CHECK_EQ(x.dcb_len, 293);
    CHECK_EQ(x.comfort_days, 7);
    CHECK_EQ(x.comfort[0][0].hour, 6);
    CHECK_EQ(x.comfort[6][0].hour, 8);
    CHECK_EQ(x.hw_timer_days, 7);
    CHECK_EQ(x.hw_on_at[0][0].hour, 5);
    CHECK_EQ(x.hw_off_at[6][3].minute, 45);
}

static void test_mode_and_room_temp(void)
{
    hm_dcb_t x;
    memset(&x, 0, sizeof x);
    x.on_off = 0;
    CHECK_EQ(hm_dcb_mode(&x), HM_MODE_OFF);
    x.on_off = 1;
    CHECK_EQ(hm_dcb_mode(&x), HM_MODE_HEAT);
    x.run_mode = 1;
    CHECK_EQ(hm_dcb_mode(&x), HM_MODE_FROST);
    x.on_off = 0;
    CHECK_EQ(hm_dcb_mode(&x), HM_MODE_OFF);

    x.air_t10 = 201;
    x.remote_air_t10 = 189;
    x.floor_t10 = 250;
    const uint16_t want[] = { 201, 189, 250, 201, 189 };
    for (uint8_t sel = 0; sel < 5; sel++) {
        x.sensor_sel = sel;
        CHECK_EQ(hm_dcb_room_t10(&x), want[sel]);
    }
}

/* ---- Encoders ------------------------------------------------------------ */

static void check_w(const hm_write_t *w, uint16_t addr, const uint8_t *data, uint8_t len)
{
    CHECK_EQ(w->addr, addr);
    CHECK_EQ(w->len, len);
    CHECK(memcmp(w->data, data, len) == 0);
}

static void test_encode_single_bytes(void)
{
    hm_write_t w;
    uint8_t b;

    CHECK_EQ(hm_wr_set_temp(&w, 21), HM_WR_OK);
    b = 21; check_w(&w, 18, &b, 1);
    CHECK_EQ(hm_wr_set_temp(&w, 5), HM_WR_OK);
    CHECK_EQ(hm_wr_set_temp(&w, 35), HM_WR_OK);
    CHECK_EQ(hm_wr_set_temp(&w, 4), HM_WR_RANGE);
    CHECK_EQ(hm_wr_set_temp(&w, 36), HM_WR_RANGE);

    CHECK_EQ(hm_wr_frost_temp(&w, 12), HM_WR_OK);
    b = 12; check_w(&w, 17, &b, 1);
    CHECK_EQ(hm_wr_frost_temp(&w, 6), HM_WR_RANGE);
    CHECK_EQ(hm_wr_frost_temp(&w, 18), HM_WR_RANGE);

    CHECK_EQ(hm_wr_floor_max(&w, 28), HM_WR_OK);
    b = 28; check_w(&w, 19, &b, 1);
    CHECK_EQ(hm_wr_floor_max(&w, 19), HM_WR_RANGE);
    CHECK_EQ(hm_wr_floor_max(&w, 46), HM_WR_RANGE);

    CHECK_EQ(hm_wr_on_off(&w, true), HM_WR_OK);
    b = 1; check_w(&w, 21, &b, 1);
    CHECK_EQ(hm_wr_on_off(&w, false), HM_WR_OK);
    b = 0; check_w(&w, 21, &b, 1);

    CHECK_EQ(hm_wr_key_lock(&w, true), HM_WR_OK);
    b = 1; check_w(&w, 22, &b, 1);

    CHECK_EQ(hm_wr_run_mode(&w, true), HM_WR_OK);
    b = 1; check_w(&w, 23, &b, 1);
    CHECK_EQ(hm_wr_run_mode(&w, false), HM_WR_OK);
    b = 0; check_w(&w, 23, &b, 1);

    /* Frost enable uses the stat's encoding: 0 = enabled. */
    CHECK_EQ(hm_wr_frost_enable(&w, true), HM_WR_OK);
    b = 0; check_w(&w, 7, &b, 1);
    CHECK_EQ(hm_wr_frost_enable(&w, false), HM_WR_OK);
    b = 1; check_w(&w, 7, &b, 1);

    CHECK_EQ(hm_wr_hw_state(&w, HM_MODEL_PRTHW, HM_HW_FORCE_OFF), HM_WR_OK);
    b = 2; check_w(&w, 42, &b, 1);
    CHECK_EQ(hm_wr_hw_state(&w, HM_MODEL_PRTHW, 3), HM_WR_RANGE);
    CHECK_EQ(hm_wr_hw_state(&w, HM_MODEL_PRT, 1), HM_WR_MODEL);

    CHECK_EQ(hm_wr_set_temp(NULL, 20), HM_WR_ARG);
}

static void test_encode_two_bytes_and_clock(void)
{
    hm_write_t w;
    CHECK_EQ(hm_wr_holiday(&w, 168), HM_WR_OK);
    const uint8_t hol[] = { 0xA8, 0x00 }; /* spec example: low byte first */
    check_w(&w, 24, hol, 2);
    CHECK_EQ(hm_wr_hold(&w, 0x0123), HM_WR_OK);
    const uint8_t hold[] = { 0x23, 0x01 };
    check_w(&w, 32, hold, 2);
    CHECK_EQ(hm_wr_hold(&w, 0x10000), HM_WR_RANGE);

    CHECK_EQ(hm_wr_clock(&w, 3, 14, 5, 59), HM_WR_OK);
    const uint8_t clk[] = { 3, 14, 5, 59 };
    check_w(&w, 43, clk, 4);
    CHECK_EQ(hm_wr_clock(&w, 0, 14, 5, 0), HM_WR_RANGE);
    CHECK_EQ(hm_wr_clock(&w, 8, 14, 5, 0), HM_WR_RANGE);
    CHECK_EQ(hm_wr_clock(&w, 1, 24, 5, 0), HM_WR_RANGE);
    CHECK_EQ(hm_wr_clock(&w, 1, 0, 60, 0), HM_WR_RANGE);
}

static void test_encode_comfort(void)
{
    hm_write_t w;
    /* Spec example: Friday 07:00 21, 09:00 16, 16:00 21, 22:00 16, 7-day mode,
     * "xx 16 ** 01 97 00 0c 00 07 00 15 09 00 10 10 00 15 16 00 10". */
    const hm_period_t fri[4] = { { 7, 0, 21 }, { 9, 0, 16 }, { 16, 0, 21 }, { 22, 0, 16 } };
    CHECK_EQ(hm_wr_comfort(&w, HM_MODEL_PRT, 1, 4, fri), HM_WR_OK);
    const uint8_t want[] = { 0x07, 0x00, 0x15, 0x09, 0x00, 0x10, 0x10, 0x00, 0x15, 0x16, 0x00, 0x10 };
    check_w(&w, 0x97, want, 12);
    uint8_t f[32];
    CHECK_EQ(hm_write_frame(&w, f, sizeof f, 5, 0x81), 22);
    const uint8_t hdr[] = { 0x05, 0x16, 0x81, 0x01, 0x97, 0x00, 0x0C, 0x00 };
    CHECK(memcmp(f, hdr, sizeof hdr) == 0);
    CHECK_EQ(f[20] | (f[21] << 8), hm_crc16(f, 20));

    /* 5/2: weekday 47, weekend 59, and the same on a PRTHW. */
    CHECK_EQ(hm_wr_comfort(&w, HM_MODEL_PRT, 0, 0, fri), HM_WR_OK);
    CHECK_EQ(w.addr, 47);
    CHECK_EQ(hm_wr_comfort(&w, HM_MODEL_PRTHW, 0, 1, fri), HM_WR_OK);
    CHECK_EQ(w.addr, 59);
    CHECK_EQ(hm_wr_comfort(&w, HM_MODEL_PRTHW, 1, 6, fri), HM_WR_OK);
    CHECK_EQ(w.addr, 175);
    CHECK_EQ(hm_wr_comfort(&w, HM_MODEL_PRT, 0, 2, fri), HM_WR_RANGE); /* only 2 groups in 5/2 */
    CHECK_EQ(hm_wr_comfort(&w, HM_MODEL_PRT, 1, 7, fri), HM_WR_RANGE);
    CHECK_EQ(hm_wr_comfort(&w, HM_MODEL_DT, 0, 0, fri), HM_WR_MODEL);

    /* Cancelled periods go last; their minutes are zeroed. */
    const hm_period_t wkend[4] = { { 9, 0, 21 }, { 22, 0, 16 }, { 24, 30, 16 }, { 24, 0, 16 } };
    CHECK_EQ(hm_wr_comfort(&w, HM_MODEL_PRT, 0, 1, wkend), HM_WR_OK);
    CHECK_EQ(w.data[6], 24);
    CHECK_EQ(w.data[7], 0);
    const hm_period_t gap[4] = { { 9, 0, 21 }, { 24, 0, 16 }, { 22, 0, 16 }, { 24, 0, 16 } };
    CHECK_EQ(hm_wr_comfort(&w, HM_MODEL_PRT, 0, 1, gap), HM_WR_RANGE);
    const hm_period_t order[4] = { { 9, 0, 21 }, { 8, 0, 16 }, { 24, 0, 16 }, { 24, 0, 16 } };
    CHECK_EQ(hm_wr_comfort(&w, HM_MODEL_PRT, 0, 1, order), HM_WR_RANGE);
    const hm_period_t hot[4] = { { 9, 0, 36 }, { 24, 0, 16 }, { 24, 0, 16 }, { 24, 0, 16 } };
    CHECK_EQ(hm_wr_comfort(&w, HM_MODEL_PRT, 0, 1, hot), HM_WR_RANGE);
    const hm_period_t badmin[4] = { { 9, 60, 20 }, { 24, 0, 16 }, { 24, 0, 16 }, { 24, 0, 16 } };
    CHECK_EQ(hm_wr_comfort(&w, HM_MODEL_PRT, 0, 1, badmin), HM_WR_RANGE);
}

static void test_encode_hw_timer(void)
{
    hm_write_t w;
    const hm_period_t on[4] = { { 6, 30, 0 }, { 17, 0, 0 }, { 24, 0, 0 }, { 24, 0, 0 } };
    const hm_period_t off[4] = { { 8, 0, 0 }, { 19, 15, 0 }, { 24, 0, 0 }, { 24, 0, 0 } };
    CHECK_EQ(hm_wr_hw_timer(&w, HM_MODEL_PRTHW, 0, 0, on, off), HM_WR_OK);
    const uint8_t want[] = { 6, 30, 8, 0, 17, 0, 19, 15, 24, 0, 24, 0, 24, 0, 24, 0 };
    check_w(&w, 71, want, 16);
    CHECK_EQ(hm_wr_hw_timer(&w, HM_MODEL_PRTHW, 0, 1, on, off), HM_WR_OK);
    CHECK_EQ(w.addr, 87);
    CHECK_EQ(hm_wr_hw_timer(&w, HM_MODEL_PRTHW, 1, 0, on, off), HM_WR_OK);
    CHECK_EQ(w.addr, 187);
    CHECK_EQ(hm_wr_hw_timer(&w, HM_MODEL_PRTHW, 1, 6, on, off), HM_WR_OK);
    CHECK_EQ(w.addr, 283);
    CHECK_EQ(hm_wr_hw_timer(&w, HM_MODEL_PRT, 0, 0, on, off), HM_WR_MODEL);
    const hm_period_t off_early[4] = { { 6, 0, 0 }, { 19, 15, 0 }, { 24, 0, 0 }, { 24, 0, 0 } };
    CHECK_EQ(hm_wr_hw_timer(&w, HM_MODEL_PRTHW, 0, 0, on, off_early), HM_WR_RANGE);
    const hm_period_t half[4] = { { 8, 0, 0 }, { 19, 15, 0 }, { 20, 0, 0 }, { 24, 0, 0 } };
    CHECK_EQ(hm_wr_hw_timer(&w, HM_MODEL_PRTHW, 0, 0, on, half), HM_WR_RANGE);
}

static void test_encode_mode(void)
{
    hm_write_t w[2];
    CHECK_EQ(hm_wr_mode(w, HM_MODE_OFF), 1);
    CHECK_EQ(w[0].addr, 21);
    CHECK_EQ(w[0].data[0], 0);
    CHECK_EQ(hm_wr_mode(w, HM_MODE_HEAT), 2);
    CHECK_EQ(w[0].addr, 21);
    CHECK_EQ(w[0].data[0], 1);
    CHECK_EQ(w[1].addr, 23);
    CHECK_EQ(w[1].data[0], 0);
    CHECK_EQ(hm_wr_mode(w, HM_MODE_FROST), 2);
    CHECK_EQ(w[1].data[0], 1);
    CHECK_EQ(hm_wr_mode(w, (hm_mode_t)9), 0);
}

static void test_write_frames(void)
{
    hm_write_t w;
    uint8_t f[32];
    hm_wr_set_temp(&w, 22);
    CHECK_EQ(hm_write_frame(&w, f, sizeof f, 7, 0x81), 11);
    const uint8_t want[] = { 0x07, 0x0B, 0x81, 0x01, 0x12, 0x00, 0x01, 0x00, 22 };
    CHECK(memcmp(f, want, sizeof want) == 0);
    CHECK_EQ(f[9] | (f[10] << 8), hm_crc16(f, 9));
    CHECK_EQ(hm_write_frame(&w, f, sizeof f, 0, 0x81), 0);
    CHECK_EQ(hm_write_frame(&w, f, sizeof f, 33, 0x81), 0);
    CHECK_EQ(hm_write_frame(&w, f, sizeof f, HM_BROADCAST, 0x81), 0); /* never broadcast */
    CHECK_EQ(hm_write_frame(&w, f, 10, 7, 0x81), 0);

    char d[40];
    hm_write_describe(&w, d, sizeof d);
    CHECK(strcmp(d, "set temp 22") == 0);
    hm_wr_hold(&w, 90);
    hm_write_describe(&w, d, sizeof d);
    CHECK(strcmp(d, "hold 90 min") == 0);
}

/* ---- Read-back checks ---------------------------------------------------- */

/* The bench-captured PRT-N DCB (64 bytes): set 16, frost 16, on, heating mode. */
static const uint8_t s_cap_dcb[64] = {
    0x00, 0x40, 0x01, 0x13, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x14, 0x00, 0x10, 0x10, 0x1C, 0x01, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0xE6, 0x00, 0x00,
    0x01, 0x03, 0x32, 0x0F, 0x08, 0x00, 0x15, 0x09, 0x1E, 0x10, 0x10, 0x1E,
    0x16, 0x17, 0x00, 0x11, 0x09, 0x00, 0x15, 0x16, 0x00, 0x10, 0x18, 0x00,
    0x10, 0x18, 0x00, 0x10,
};

/* What a stat does with a write, for building expected DCBs. */
static void apply(uint8_t *dcb, const hm_write_t *w)
{
    int i = hm_dcb_index(dcb[4], dcb[16], w->addr);
    if (i < 0) {
        return;
    }
    if (w->addr == HM_UA_HOLIDAY_HOURS || w->addr == HM_UA_HOLD_MINUTES) {
        dcb[i] = w->data[1];
        dcb[i + 1] = w->data[0];
    } else if (w->addr == HM_UA_HW_STATE) {
        if (w->data[0]) {
            dcb[i] = w->data[0] == 1;
        }
    } else {
        memcpy(&dcb[i], w->data, w->len);
    }
}

static void test_verify_captured(void)
{
    uint8_t d[64];
    memcpy(d, s_cap_dcb, sizeof d);
    hm_dcb_t x;
    CHECK_EQ(hm_dcb_decode(d, sizeof d, &x), 0);
    CHECK_EQ(x.set_temp, 16);
    CHECK_EQ(x.air_t10, 230);

    hm_write_t w;
    hm_wr_set_temp(&w, 16);
    CHECK_EQ(hm_write_verify(&w, d, sizeof d), HM_VERIFY_OK);
    hm_wr_set_temp(&w, 17);
    CHECK_EQ(hm_write_verify(&w, d, sizeof d), HM_VERIFY_MISMATCH);
    apply(d, &w);
    CHECK_EQ(hm_write_verify(&w, d, sizeof d), HM_VERIFY_OK);
    CHECK_EQ(d[18], 17);

    hm_wr_run_mode(&w, false);
    CHECK_EQ(hm_write_verify(&w, d, sizeof d), HM_VERIFY_OK);
    hm_wr_run_mode(&w, true);
    CHECK_EQ(hm_write_verify(&w, d, sizeof d), HM_VERIFY_MISMATCH);
    hm_wr_on_off(&w, true);
    CHECK_EQ(hm_write_verify(&w, d, sizeof d), HM_VERIFY_OK);
    hm_wr_frost_temp(&w, 16);
    CHECK_EQ(hm_write_verify(&w, d, sizeof d), HM_VERIFY_OK);
    hm_wr_frost_enable(&w, true); /* byte 7 reads 00 with frost enabled */
    CHECK_EQ(hm_write_verify(&w, d, sizeof d), HM_VERIFY_OK);

    /* The captured weekday programme reads back as written. */
    const hm_period_t wk[4] = { { 8, 0, 21 }, { 9, 30, 16 }, { 16, 30, 22 }, { 23, 0, 17 } };
    CHECK_EQ(hm_wr_comfort(&w, HM_MODEL_PRT, 0, 0, wk), HM_WR_OK);
    CHECK_EQ(hm_write_verify(&w, d, sizeof d), HM_VERIFY_OK);
    const hm_period_t we[4] = { { 9, 0, 21 }, { 22, 0, 16 }, { 24, 0, 16 }, { 24, 0, 16 } };
    CHECK_EQ(hm_wr_comfort(&w, HM_MODEL_PRT, 0, 1, we), HM_WR_OK);
    CHECK_EQ(hm_write_verify(&w, d, sizeof d), HM_VERIFY_OK);
    const hm_period_t we2[4] = { { 9, 0, 21 }, { 22, 0, 17 }, { 24, 0, 16 }, { 24, 0, 16 } };
    hm_wr_comfort(&w, HM_MODEL_PRT, 0, 1, we2);
    CHECK_EQ(hm_write_verify(&w, d, sizeof d), HM_VERIFY_MISMATCH);

    /* Fields a PRT does not have. */
    hm_wr_hw_state(&w, HM_MODEL_PRTHW, 1);
    CHECK_EQ(hm_write_verify(&w, d, sizeof d), HM_VERIFY_NO_FIELD);
    CHECK_EQ(hm_write_verify(&w, d, 20), HM_VERIFY_NO_FIELD);
}

static void test_verify_countdown_and_clock(void)
{
    uint8_t d[64];
    memcpy(d, s_cap_dcb, sizeof d);
    hm_write_t w;
    hm_wr_hold(&w, 300);
    CHECK_EQ(hm_write_verify(&w, d, sizeof d), HM_VERIFY_MISMATCH);
    apply(d, &w);
    CHECK_EQ(d[26], 0x01);   /* read back high byte first */
    CHECK_EQ(d[27], 0x2C);
    CHECK_EQ(hm_write_verify(&w, d, sizeof d), HM_VERIFY_OK);
    d[27] = 0x2B;            /* 299: a minute has ticked */
    CHECK_EQ(hm_write_verify(&w, d, sizeof d), HM_VERIFY_OK);
    d[27] = 0x29;            /* 297 */
    CHECK_EQ(hm_write_verify(&w, d, sizeof d), HM_VERIFY_MISMATCH);
    d[27] = 0x2D;            /* 301: higher than written */
    CHECK_EQ(hm_write_verify(&w, d, sizeof d), HM_VERIFY_MISMATCH);

    hm_wr_holiday(&w, 168);
    apply(d, &w);
    CHECK_EQ(d[24], 0x00);
    CHECK_EQ(d[25], 0xA8);
    CHECK_EQ(hm_write_verify(&w, d, sizeof d), HM_VERIFY_OK);

    hm_wr_clock(&w, 1, 3, 50, 0);
    CHECK_EQ(hm_write_verify(&w, d, sizeof d), HM_VERIFY_UNCHECKED);
}

static void test_verify_prthw(void)
{
    uint8_t h[97];
    memset(h, 0, sizeof h);
    h[1] = 97;
    h[4] = HM_MODEL_PRTHW;
    hm_write_t w;
    hm_wr_hw_state(&w, HM_MODEL_PRTHW, HM_HW_FORCE_ON);
    CHECK_EQ(hm_write_verify(&w, h, sizeof h), HM_VERIFY_MISMATCH);
    apply(h, &w);
    CHECK_EQ(h[36], 1);
    CHECK_EQ(hm_write_verify(&w, h, sizeof h), HM_VERIFY_OK);
    hm_wr_hw_state(&w, HM_MODEL_PRTHW, HM_HW_FORCE_OFF);
    CHECK_EQ(hm_write_verify(&w, h, sizeof h), HM_VERIFY_MISMATCH);
    apply(h, &w);
    CHECK_EQ(h[36], 0);
    CHECK_EQ(hm_write_verify(&w, h, sizeof h), HM_VERIFY_OK);
    hm_wr_hw_state(&w, HM_MODEL_PRTHW, HM_HW_FOLLOW_TIMER);
    CHECK_EQ(hm_write_verify(&w, h, sizeof h), HM_VERIFY_UNCHECKED);

    const hm_period_t on[4] = { { 6, 30, 0 }, { 24, 0, 0 }, { 24, 0, 0 }, { 24, 0, 0 } };
    const hm_period_t off[4] = { { 8, 0, 0 }, { 24, 0, 0 }, { 24, 0, 0 }, { 24, 0, 0 } };
    hm_wr_hw_timer(&w, HM_MODEL_PRTHW, 0, 1, on, off);
    CHECK_EQ(hm_write_verify(&w, h, sizeof h), HM_VERIFY_MISMATCH);
    apply(h, &w);
    CHECK_EQ(h[81], 6);
    CHECK_EQ(hm_write_verify(&w, h, sizeof h), HM_VERIFY_OK);

    /* The PRTHW set temperature is at the same index as on a PRT. */
    hm_wr_set_temp(&w, 19);
    apply(h, &w);
    CHECK_EQ(h[18], 19);
    CHECK_EQ(hm_write_verify(&w, h, sizeof h), HM_VERIFY_OK);
}

void test_write_all(void)
{
    test_index_prt();
    test_index_prthw();
    test_index_dt();
    test_decode_every_field();
    test_decode_prthw_7day();
    test_mode_and_room_temp();
    test_encode_single_bytes();
    test_encode_two_bytes_and_clock();
    test_encode_comfort();
    test_encode_hw_timer();
    test_encode_mode();
    test_write_frames();
    test_verify_captured();
    test_verify_countdown_and_clock();
    test_verify_prthw();
}
