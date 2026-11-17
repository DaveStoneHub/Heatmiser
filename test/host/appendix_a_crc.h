/* The CRC routine from Appendix A of the Heatmiser V3.9 protocol document,
 * transcribed as published (only the watchdog call removed and the globals
 * made static), used as the reference the production table code must match. */
#pragma once

static const unsigned char CRC16_LookupHigh[16] = {
    0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70,
    0x81, 0x91, 0xA1, 0xB1, 0xC1, 0xD1, 0xE1, 0xF1
};
static const unsigned char CRC16_LookupLow[16] = {
    0x00, 0x21, 0x42, 0x63, 0x84, 0xA5, 0xC6, 0xE7,
    0x08, 0x29, 0x4A, 0x6B, 0x8C, 0xAD, 0xCE, 0xEF
};

static unsigned char CRC16_High, CRC16_Low;

static void CRC16_Update4Bits(unsigned char val)
{
    unsigned char t;
    t = CRC16_High >> 4;
    t = t ^ val;
    CRC16_High = (CRC16_High << 4) | (CRC16_Low >> 4);
    CRC16_Low = CRC16_Low << 4;
    CRC16_High = CRC16_High ^ CRC16_LookupHigh[t];
    CRC16_Low = CRC16_Low ^ CRC16_LookupLow[t];
}

static void CRC16_Update(unsigned char val)
{
    CRC16_Update4Bits(val >> 4);
    CRC16_Update4Bits(val & 0x0f);
}

static void CRC16(const unsigned char *buf, unsigned int len, unsigned char *low, unsigned char *hi)
{
    CRC16_High = 0xff;
    CRC16_Low = 0xff;
    while (len--) {
        CRC16_Update(*buf++);
    }
    *low = CRC16_Low;
    *hi = CRC16_High;
}
