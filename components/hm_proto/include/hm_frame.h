/*
 * Heatmiser V3 RS485 frames (protocol V3.9, chapter 3).
 *
 * Master request:  dst, len, src, func, addr_lo, addr_hi, n_lo, n_hi, [data], crc_lo, crc_hi
 * Read reply:      dst, len_lo, len_hi, src, 0x00, addr_lo, addr_hi, n_lo, n_hi, data[n], crc_lo, crc_hi
 * Write reply:     dst, 0x07, 0x00, src, 0x01, crc_lo, crc_hi
 *
 * Pure C, no ESP-IDF dependencies, so it builds and tests on a host.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HM_FUNC_READ          0x00
#define HM_FUNC_WRITE         0x01
#define HM_READ_ALL           0xFFFFu   /* length value meaning "whole DCB" */
#define HM_READ_REQ_LEN       10
#define HM_WRITE_REPLY_LEN    7
#define HM_READ_REPLY_OVERHEAD 11       /* reply length = 11 + n */
#define HM_MAX_FRAME          512       /* PRT/HW-N 7-day is about 304 */
#define HM_STAT_ADDR_MIN      1
#define HM_STAT_ADDR_MAX      32
#define HM_MASTER_ADDR_MIN    0x81
#define HM_MASTER_ADDR_MAX    0xA0
#define HM_BROADCAST          0xFF

typedef enum {
    HM_OK = 0,
    HM_ERR_SHORT,     /* fewer bytes than the frame's own length field says */
    HM_ERR_LEN,       /* length field impossible, or disagrees with byte count */
    HM_ERR_CRC,
    HM_ERR_DST,       /* not addressed to us */
    HM_ERR_SRC,       /* came from a different stat */
    HM_ERR_FUNC,
    HM_ERR_COUNT,     /* data count does not match frame length */
    HM_ERR_ARG,
} hm_status_t;

typedef struct {
    uint8_t  dst;        /* master address */
    uint8_t  src;        /* stat address */
    uint8_t  func;       /* HM_FUNC_READ or HM_FUNC_WRITE */
    uint16_t frame_len;  /* total bytes, CRC included */
    uint16_t start;      /* read replies only */
    uint16_t count;      /* read replies only */
    const uint8_t *data; /* points into the caller's buffer; NULL for write replies */
} hm_reply_t;

/* Build a read request. Returns bytes written (10) or 0 on bad arguments. */
size_t hm_build_read(uint8_t *out, size_t cap, uint8_t dst, uint8_t src,
                     uint16_t start, uint16_t count);

/* Build a write request for n data bytes. Returns 10+n or 0 on bad arguments.
 * Two-byte values must already be in wire order (low byte first on write). */
size_t hm_build_write(uint8_t *out, size_t cap, uint8_t dst, uint8_t src,
                      uint16_t start, const uint8_t *data, uint16_t n);

/* Given the first bytes of a reply, return its total length from the length
 * field, or 0 if fewer than 3 bytes are available yet. */
size_t hm_reply_frame_len(const uint8_t *buf, size_t have);

/* Validate and decode a complete reply. expect_stat = 0 skips the source check. */
hm_status_t hm_parse_reply(const uint8_t *buf, size_t len, uint8_t expect_master,
                           uint8_t expect_stat, hm_reply_t *out);

const char *hm_status_str(hm_status_t st);

#ifdef __cplusplus
}
#endif
