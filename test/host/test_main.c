// Host unit tests for hm_proto. No dependencies: build with CMake (ctest) or
//   cc -std=c99 -Wall -I../../components/hm_proto/include test_main.c
//      ../../components/hm_proto/hm_*.c -o hm_tests && ./hm_tests
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hm_crc.h"
#include "hm_dcb.h"
#include "hm_frame.h"
#include "appendix_a_crc.h"

static int s_fail, s_pass;

#define CHECK(cond) do { \
    if (cond) { s_pass++; } \
    else { s_fail++; fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

#define CHECK_EQ(a, b) do { \
    long long _a = (long long)(a), _b = (long long)(b); \
    if (_a == _b) { s_pass++; } \
    else { s_fail++; fprintf(stderr, "%s:%d: %s == %s failed: 0x%llX vs 0x%llX\n", \
                             __FILE__, __LINE__, #a, #b, _a, _b); } \
} while (0)

static uint16_t bitwise_crc(const uint8_t *p, size_t n)
{
    uint16_t c = 0xFFFF;
    while (n--) {
        c ^= (uint16_t)(*p++ << 8);
        for (int i = 0; i < 8; i++) {
            c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1);
        }
    }
    return c;
}

static uint16_t appendix_crc(const uint8_t *p, size_t n)
{
    unsigned char lo, hi;
    CRC16(p, (unsigned)n, &lo, &hi);
    return (uint16_t)(lo | (hi << 8));
}

/* ---- CRC ---------------------------------------------------------------- */

static void test_crc_check_value(void)
{
    /* Standard CRC-16/CCITT-FALSE check value. */
    const uint8_t s[] = "123456789";
    CHECK_EQ(hm_crc16(s, 9), 0x29B1);
    CHECK_EQ(appendix_crc(s, 9), 0x29B1);
    CHECK_EQ(hm_crc16(s, 0), 0xFFFF);
}

static void test_crc_matches_appendix(void)
{
    uint8_t buf[300];
    srand(12345);
    for (int round = 0; round < 2000; round++) {
        size_t n = (size_t)(rand() % (int)sizeof(buf));
        for (size_t i = 0; i < n; i++) {
            buf[i] = (uint8_t)rand();
        }
        uint16_t a = hm_crc16(buf, n);
        CHECK_EQ(a, appendix_crc(buf, n));
        CHECK_EQ(a, bitwise_crc(buf, n));
    }
}

static void test_crc_residue(void)
{
    /* Appending the CRC big-endian gives a zero residue for this CRC; the
     * wire order is little-endian, so check the parse path instead. */
    uint8_t f[12];
    size_t n = hm_build_read(f, sizeof f, 1, 0x81, 0, HM_READ_ALL);
    CHECK_EQ(n, 10);
    uint16_t crc = hm_crc16(f, 8);
    CHECK_EQ(f[8], crc & 0xFF);
    CHECK_EQ(f[9], crc >> 8);
}

/* ---- Captured frames ----------------------------------------------------
 * Frames captured on the real bus go here. Each must pass CRC as received.
 * Add one per model once the bench rig prints them ("read" prints the raw
 * request and reply in hex).
 */
typedef struct {
    const char *what;
    const uint8_t *bytes;
    size_t n;
} captured_t;

static const captured_t s_captured[] = {
    { NULL, NULL, 0 },
};

static void test_captured_frames(void)
{
    for (size_t i = 0; s_captured[i].bytes; i++) {
        const captured_t *c = &s_captured[i];
        uint16_t crc = (uint16_t)(c->bytes[c->n - 2] | (c->bytes[c->n - 1] << 8));
        if (hm_crc16(c->bytes, c->n - 2) != crc) {
            fprintf(stderr, "captured frame '%s' fails CRC\n", c->what);
        }
        CHECK_EQ(hm_crc16(c->bytes, c->n - 2), crc);
    }
}

/* ---- Request frames ----------------------------------------------------- */

static void test_build_read_full(void)
{
    /* Spec: "xx 0a ** 00 00 00 ff ff ## ##" */
    uint8_t f[16];
    size_t n = hm_build_read(f, sizeof f, 3, 0x81, 0, HM_READ_ALL);
    const uint8_t want[] = { 0x03, 0x0A, 0x81, 0x00, 0x00, 0x00, 0xFF, 0xFF };
    CHECK_EQ(n, 10);
    CHECK(memcmp(f, want, sizeof want) == 0);
    CHECK_EQ(f[8] | (f[9] << 8), bitwise_crc(f, 8));

    /* Regression vector: full-DCB read of stat 1 from 0x81. */
    const uint8_t stat1[] = { 0x01, 0x0A, 0x81, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x2C, 0x09 };
    n = hm_build_read(f, sizeof f, 1, 0x81, 0, HM_READ_ALL);
    CHECK_EQ(n, 10);
    CHECK(memcmp(f, stat1, sizeof stat1) == 0);
}

static void test_build_read_partial(void)
{
    uint8_t f[16];
    size_t n = hm_build_read(f, sizeof f, 32, 0xA0, 32, 11);
    const uint8_t want[] = { 0x20, 0x0A, 0xA0, 0x00, 0x20, 0x00, 0x0B, 0x00 };
    CHECK_EQ(n, 10);
    CHECK(memcmp(f, want, sizeof want) == 0);
}

static void test_build_read_rejects(void)
{
    uint8_t f[16];
    CHECK_EQ(hm_build_read(f, 9, 1, 0x81, 0, 1), 0);           /* too small */
    CHECK_EQ(hm_build_read(f, sizeof f, 0xFF, 0x81, 0, 1), 0); /* broadcast read */
    CHECK_EQ(hm_build_read(NULL, 10, 1, 0x81, 0, 1), 0);
}

static void test_build_write_examples(void)
{
    uint8_t f[32];
    /* Spec: frost protection disable "xx 0b ** 01 07 00 01 00 00 ## ##" */
    const uint8_t off = 0;
    size_t n = hm_build_write(f, sizeof f, 5, 0x81, 7, &off, 1);
    const uint8_t want1[] = { 0x05, 0x0B, 0x81, 0x01, 0x07, 0x00, 0x01, 0x00, 0x00 };
    CHECK_EQ(n, 11);
    CHECK(memcmp(f, want1, sizeof want1) == 0);
    CHECK_EQ(f[9] | (f[10] << 8), bitwise_crc(f, 9));

    /* Spec: holiday 168 h "xx 0c ** 01 18 00 02 00 a8 00 ## ##" (low byte first on write) */
    const uint8_t hol[] = { 0xA8, 0x00 };
    n = hm_build_write(f, sizeof f, 5, 0x81, 24, hol, 2);
    const uint8_t want2[] = { 0x05, 0x0C, 0x81, 0x01, 0x18, 0x00, 0x02, 0x00, 0xA8, 0x00 };
    CHECK_EQ(n, 12);
    CHECK(memcmp(f, want2, sizeof want2) == 0);

    /* Spec: Friday comfort levels (7-day) "xx 16 ** 01 97 00 0c 00 07 00 15 09 00 10 10 00 15 16 00 10" */
    const uint8_t fri[] = { 0x07, 0x00, 0x15, 0x09, 0x00, 0x10, 0x10, 0x00, 0x15, 0x16, 0x00, 0x10 };
    n = hm_build_write(f, sizeof f, 5, 0x81, 0x97, fri, sizeof fri);
    const uint8_t want3[] = { 0x05, 0x16, 0x81, 0x01, 0x97, 0x00, 0x0C, 0x00 };
    CHECK_EQ(n, 22);
    CHECK(memcmp(f, want3, sizeof want3) == 0);
    CHECK(memcmp(&f[8], fri, sizeof fri) == 0);
}

static void test_build_write_rejects(void)
{
    uint8_t f[300], d[250] = { 0 };
    CHECK_EQ(hm_build_write(f, sizeof f, 1, 0x81, 0, d, 0), 0);
    CHECK_EQ(hm_build_write(f, sizeof f, 1, 0x81, 0, d, 246), 0); /* len byte overflow */
    CHECK_EQ(hm_build_write(f, 11, 1, 0x81, 0, d, 2), 0);
}

/* ---- Replies ------------------------------------------------------------ */

/* Default PRT-N DCB in 5/2 mode from the section 3.3 table, with a few live
 * values filled in: address 3, set 21 C, air 20.5 C, holiday 0x0102 h,
 * hold 90 min, heating on, clock Tue 14:05:30. */
static size_t make_prt_dcb(uint8_t *d)
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
    memcpy(d, base, sizeof base);
    return sizeof base;
}

static size_t make_reply(uint8_t *f, uint8_t master, uint8_t stat, uint16_t start,
                         const uint8_t *data, uint16_t n)
{
    size_t len = 11u + n;
    f[0] = master;
    f[1] = (uint8_t)(len & 0xFF);
    f[2] = (uint8_t)(len >> 8);
    f[3] = stat;
    f[4] = 0x00;
    f[5] = (uint8_t)(start & 0xFF);
    f[6] = (uint8_t)(start >> 8);
    f[7] = (uint8_t)(n & 0xFF);
    f[8] = (uint8_t)(n >> 8);
    memcpy(&f[9], data, n);
    uint16_t crc = hm_crc16(f, len - 2);
    f[len - 2] = (uint8_t)(crc & 0xFF);
    f[len - 1] = (uint8_t)(crc >> 8);
    return len;
}

static void test_parse_read_reply(void)
{
    uint8_t dcb[64], f[128];
    make_prt_dcb(dcb);
    size_t len = make_reply(f, 0x81, 3, 0, dcb, 64);
    CHECK_EQ(len, 75);
    CHECK_EQ(hm_reply_frame_len(f, 2), 0);
    CHECK_EQ(hm_reply_frame_len(f, 3), 75);

    hm_reply_t r;
    CHECK_EQ(hm_parse_reply(f, len, 0x81, 3, &r), HM_OK);
    CHECK_EQ(r.frame_len, 75);
    CHECK_EQ(r.src, 3);
    CHECK_EQ(r.func, HM_FUNC_READ);
    CHECK_EQ(r.start, 0);
    CHECK_EQ(r.count, 64);
    CHECK(r.data == &f[9]);
    CHECK_EQ(hm_parse_reply(f, len, 0x81, 0, &r), HM_OK); /* any source */
}

static void test_parse_errors(void)
{
    uint8_t dcb[64], f[128];
    hm_reply_t r;
    make_prt_dcb(dcb);
    size_t len = make_reply(f, 0x81, 3, 0, dcb, 64);

    CHECK_EQ(hm_parse_reply(f, len - 1, 0x81, 3, &r), HM_ERR_SHORT);
    CHECK_EQ(hm_parse_reply(f, 2, 0x81, 3, &r), HM_ERR_SHORT);
    CHECK_EQ(hm_parse_reply(f, len + 1, 0x81, 3, &r), HM_ERR_LEN);
    CHECK_EQ(hm_parse_reply(f, len, 0x82, 3, &r), HM_ERR_DST);
    CHECK_EQ(hm_parse_reply(f, len, 0x81, 4, &r), HM_ERR_SRC);

    /* Every single-bit flip anywhere in the frame must be caught. */
    int caught = 0, total = 0;
    for (size_t i = 0; i < len; i++) {
        for (int b = 0; b < 8; b++) {
            uint8_t g[128];
            memcpy(g, f, len);
            g[i] ^= (uint8_t)(1u << b);
            total++;
            if (hm_parse_reply(g, len, 0x81, 3, &r) != HM_OK) {
                caught++;
            }
        }
    }
    CHECK_EQ(caught, total);

    /* Flip a data bit: specifically a CRC error. */
    f[20] ^= 0x01;
    CHECK_EQ(hm_parse_reply(f, len, 0x81, 3, &r), HM_ERR_CRC);
    f[20] ^= 0x01;

    /* Impossible length field. */
    uint8_t bad[8] = { 0x81, 0x03, 0x00, 0x03, 0x00, 0, 0, 0 };
    CHECK_EQ(hm_parse_reply(bad, sizeof bad, 0x81, 3, &r), HM_ERR_LEN);
    uint8_t huge[3] = { 0x81, 0x00, 0x10 };
    CHECK_EQ(hm_parse_reply(huge, sizeof huge, 0x81, 3, &r), HM_ERR_LEN);

    /* Count field that disagrees with length, with a valid CRC. */
    uint8_t g[128];
    memcpy(g, f, len);
    g[7] = 63;
    uint16_t crc = hm_crc16(g, len - 2);
    g[len - 2] = (uint8_t)crc;
    g[len - 1] = (uint8_t)(crc >> 8);
    CHECK_EQ(hm_parse_reply(g, len, 0x81, 3, &r), HM_ERR_COUNT);
}

static void test_parse_write_reply(void)
{
    uint8_t f[7] = { 0x81, 0x07, 0x00, 0x05, 0x01, 0, 0 };
    uint16_t crc = hm_crc16(f, 5);
    f[5] = (uint8_t)crc;
    f[6] = (uint8_t)(crc >> 8);
    hm_reply_t r;
    CHECK_EQ(hm_parse_reply(f, 7, 0x81, 5, &r), HM_OK);
    CHECK_EQ(r.func, HM_FUNC_WRITE);
    CHECK(r.data == NULL);
}

/* ---- DCB decode --------------------------------------------------------- */

static void test_decode_prt_52(void)
{
    uint8_t dcb[64];
    hm_dcb_t d;
    make_prt_dcb(dcb);
    CHECK_EQ(hm_dcb_decode(dcb, sizeof dcb, &d), 0);
    CHECK_EQ(d.dcb_len, 64);
    CHECK_EQ(d.model, HM_MODEL_PRT);
    CHECK_EQ(d.version, 5);
    CHECK(!d.floor_limit_active);
    CHECK_EQ(d.address, 3);
    CHECK_EQ(d.prog_mode, 0);
    CHECK_EQ(d.frost_temp, 12);
    CHECK_EQ(d.set_temp, 21);
    CHECK_EQ(d.on_off, 1);
    CHECK_EQ(d.holiday_hours, 0x0102); /* high byte first on read */
    CHECK_EQ(d.hold_minutes, 90);
    CHECK_EQ(d.remote_air_t10, HM_TEMP_ABSENT);
    CHECK_EQ(d.floor_t10, HM_TEMP_ABSENT);
    CHECK_EQ(d.air_t10, 205);
    CHECK_EQ(d.heating_on, 1);
    CHECK(!d.has_hw);
    CHECK(d.has_clock);
    CHECK_EQ(d.clk_wday, 2);
    CHECK_EQ(d.clk_hour, 14);
    CHECK_EQ(d.clk_min, 5);
    CHECK_EQ(d.clk_sec, 30);
    CHECK_EQ(d.comfort_days, 2);
    CHECK_EQ(d.comfort[0][0].hour, 7);
    CHECK_EQ(d.comfort[0][0].temp, 21);
    CHECK_EQ(d.comfort[0][3].hour, 22);
    CHECK_EQ(d.comfort[1][0].hour, 9);
    CHECK_EQ(d.comfort[1][2].hour, 24); /* cancelled */
    CHECK_EQ(d.hw_timer_days, 0);
}

static void test_decode_floor_limit_bit(void)
{
    uint8_t dcb[64];
    hm_dcb_t d;
    make_prt_dcb(dcb);
    dcb[3] = 0x85;
    CHECK_EQ(hm_dcb_decode(dcb, sizeof dcb, &d), 0);
    CHECK_EQ(d.version, 5);
    CHECK(d.floor_limit_active);
}

static void test_decode_prt_7day(void)
{
    uint8_t dcb[148];
    hm_dcb_t d;
    make_prt_dcb(dcb);
    dcb[0] = 0;
    dcb[1] = 148;
    dcb[16] = 1;
    for (int day = 0; day < 7; day++) {
        for (int p = 0; p < 4; p++) {
            dcb[40 + day * 12 + p * 3] = (uint8_t)(6 + p * 4);
            dcb[40 + day * 12 + p * 3 + 1] = 30;
            dcb[40 + day * 12 + p * 3 + 2] = (uint8_t)(15 + day);
        }
    }
    CHECK_EQ(hm_dcb_decode(dcb, sizeof dcb, &d), 0);
    CHECK_EQ(d.comfort_days, 7);
    CHECK_EQ(d.comfort[6][3].hour, 18);
    CHECK_EQ(d.comfort[6][3].temp, 21);
    CHECK_EQ(d.comfort[0][0].temp, 15);
}

static void test_decode_prthw_52(void)
{
    /* PRTHW 5/2: 41 fixed bytes, 24 comfort, 32 hot water timer = 97. */
    uint8_t dcb[97];
    hm_dcb_t d;
    memset(dcb, 0, sizeof dcb);
    dcb[1] = 97;
    dcb[3] = 0x90; /* PRTHW version is the whole byte */
    dcb[4] = HM_MODEL_PRTHW;
    dcb[32] = 0x00;
    dcb[33] = 0xD2; /* 21.0 C */
    dcb[36] = 1;    /* hot water on */
    dcb[37] = 7;    /* Sun */
    dcb[38] = 23;
    dcb[41] = 6;    /* first comfort period at index 41 */
    dcb[65] = 6;    /* weekday HW on 06:30 */
    dcb[66] = 30;
    dcb[67] = 8;    /* off 08:00 */
    dcb[68] = 0;
    dcb[96] = 59;   /* last byte: weekend period 4 off minute */
    CHECK_EQ(hm_dcb_decode(dcb, sizeof dcb, &d), 0);
    CHECK_EQ(d.model, HM_MODEL_PRTHW);
    CHECK_EQ(d.version, 0x90);
    CHECK(!d.floor_limit_active);
    CHECK(d.has_hw);
    CHECK_EQ(d.hw_on, 1);
    CHECK_EQ(d.air_t10, 210);
    CHECK_EQ(d.clk_wday, 7);
    CHECK_EQ(d.clk_hour, 23);
    CHECK_EQ(d.comfort_days, 2);
    CHECK_EQ(d.comfort[0][0].hour, 6);
    CHECK_EQ(d.hw_timer_days, 2);
    CHECK_EQ(d.hw_on_at[0][0].hour, 6);
    CHECK_EQ(d.hw_on_at[0][0].minute, 30);
    CHECK_EQ(d.hw_off_at[0][0].hour, 8);
    CHECK_EQ(d.hw_off_at[1][3].minute, 59);
}

static void test_decode_short(void)
{
    uint8_t dcb[64];
    hm_dcb_t d;
    make_prt_dcb(dcb);
    CHECK_EQ(hm_dcb_decode(dcb, 35, &d), -1);  /* below common fields */
    CHECK_EQ(hm_dcb_decode(dcb, 50, &d), -1);  /* shorter than its own length */
    /* A DT-length DCB decodes the common part with no clock. */
    dcb[1] = 36;
    dcb[4] = HM_MODEL_DT;
    CHECK_EQ(hm_dcb_decode(dcb, 36, &d), 0);
    CHECK(!d.has_clock);
    CHECK_EQ(d.comfort_days, 0);
}

static void test_print_smoke(void)
{
    uint8_t dcb[64];
    hm_dcb_t d;
    make_prt_dcb(dcb);
    hm_dcb_decode(dcb, sizeof dcb, &d);
    FILE *f = tmpfile();
    CHECK(f != NULL);
    if (f) {
        hm_dcb_print(f, &d);
        hm_hexdump(f, dcb, sizeof dcb);
        long size = ftell(f);
        CHECK(size > 500);
        rewind(f);
        char text[4096] = { 0 };
        fread(text, 1, sizeof text - 1, f);
        CHECK(strstr(text, "20.5 C") != NULL);
        CHECK(strstr(text, "Tue 14:05:30") != NULL);
        fclose(f);
    }
}

int main(void)
{
    test_crc_check_value();
    test_crc_matches_appendix();
    test_crc_residue();
    test_captured_frames();
    test_build_read_full();
    test_build_read_partial();
    test_build_read_rejects();
    test_build_write_examples();
    test_build_write_rejects();
    test_parse_read_reply();
    test_parse_errors();
    test_parse_write_reply();
    test_decode_prt_52();
    test_decode_floor_limit_bit();
    test_decode_prt_7day();
    test_decode_prthw_52();
    test_decode_short();
    test_print_smoke();

    printf("hm_proto host tests: %d passed, %d failed\n", s_pass, s_fail);
    return s_fail ? 1 : 0;
}
