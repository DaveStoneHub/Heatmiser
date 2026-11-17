/*
 * Heatmiser V3 CRC-16.
 *
 * Appendix A of the V3.9 protocol document computes this a nibble at a time;
 * it is CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, no final
 * XOR. It covers every byte of a frame from the destination address to the
 * last data byte, and is sent low byte first.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HM_CRC_INIT 0xFFFFu

uint16_t hm_crc16_update(uint16_t crc, uint8_t byte);
uint16_t hm_crc16(const uint8_t *buf, size_t len);

#ifdef __cplusplus
}
#endif
