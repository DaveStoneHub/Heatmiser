#include "hm_crc.h"

/* 256-entry table for poly 0x1021, MSB first. Generated once at first use so
 * the file stays readable; 512 bytes of RAM. */
static uint16_t s_table[256];
static int s_table_ready;

static void build_table(void)
{
    for (unsigned i = 0; i < 256; i++) {
        uint16_t c = (uint16_t)(i << 8);
        for (int b = 0; b < 8; b++) {
            c = (c & 0x8000u) ? (uint16_t)((c << 1) ^ 0x1021u) : (uint16_t)(c << 1);
        }
        s_table[i] = c;
    }
    s_table_ready = 1;
}

uint16_t hm_crc16_update(uint16_t crc, uint8_t byte)
{
    if (!s_table_ready) {
        build_table();
    }
    return (uint16_t)((crc << 8) ^ s_table[((crc >> 8) ^ byte) & 0xFFu]);
}

uint16_t hm_crc16(const uint8_t *buf, size_t len)
{
    uint16_t crc = HM_CRC_INIT;
    while (len--) {
        crc = hm_crc16_update(crc, *buf++);
    }
    return crc;
}
