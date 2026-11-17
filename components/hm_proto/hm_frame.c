#include "hm_frame.h"
#include "hm_crc.h"
#include <string.h>

static size_t put_crc(uint8_t *out, size_t n)
{
    uint16_t crc = hm_crc16(out, n);
    out[n] = (uint8_t)(crc & 0xFF);
    out[n + 1] = (uint8_t)(crc >> 8);
    return n + 2;
}

size_t hm_build_read(uint8_t *out, size_t cap, uint8_t dst, uint8_t src,
                     uint16_t start, uint16_t count)
{
    if (!out || cap < HM_READ_REQ_LEN || dst == HM_BROADCAST) {
        return 0; /* broadcast reads are not allowed */
    }
    out[0] = dst;
    out[1] = HM_READ_REQ_LEN;
    out[2] = src;
    out[3] = HM_FUNC_READ;
    out[4] = (uint8_t)(start & 0xFF);
    out[5] = (uint8_t)(start >> 8);
    out[6] = (uint8_t)(count & 0xFF);
    out[7] = (uint8_t)(count >> 8);
    return put_crc(out, 8);
}

size_t hm_build_write(uint8_t *out, size_t cap, uint8_t dst, uint8_t src,
                      uint16_t start, const uint8_t *data, uint16_t n)
{
    size_t total = 10u + n;
    /* The request length field is one byte, so n is at most 245. */
    if (!out || (n && !data) || n == 0 || total > 0xFF || cap < total) {
        return 0;
    }
    out[0] = dst;
    out[1] = (uint8_t)total;
    out[2] = src;
    out[3] = HM_FUNC_WRITE;
    out[4] = (uint8_t)(start & 0xFF);
    out[5] = (uint8_t)(start >> 8);
    out[6] = (uint8_t)(n & 0xFF);
    out[7] = (uint8_t)(n >> 8);
    memcpy(&out[8], data, n);
    return put_crc(out, 8 + n);
}

size_t hm_reply_frame_len(const uint8_t *buf, size_t have)
{
    if (!buf || have < 3) {
        return 0;
    }
    return (size_t)buf[1] | ((size_t)buf[2] << 8);
}

hm_status_t hm_parse_reply(const uint8_t *buf, size_t len, uint8_t expect_master,
                           uint8_t expect_stat, hm_reply_t *out)
{
    if (!buf || !out) {
        return HM_ERR_ARG;
    }
    memset(out, 0, sizeof(*out));
    if (len < 3) {
        return HM_ERR_SHORT;
    }
    size_t flen = hm_reply_frame_len(buf, len);
    if (flen < HM_WRITE_REPLY_LEN || flen > HM_MAX_FRAME) {
        return HM_ERR_LEN;
    }
    if (len < flen) {
        return HM_ERR_SHORT;
    }
    if (len > flen) {
        return HM_ERR_LEN; /* trailing bytes: framing is off */
    }
    uint16_t crc = (uint16_t)(buf[flen - 2] | (buf[flen - 1] << 8));
    if (hm_crc16(buf, flen - 2) != crc) {
        return HM_ERR_CRC;
    }
    out->dst = buf[0];
    out->frame_len = (uint16_t)flen;
    out->src = buf[3];
    out->func = buf[4];
    if (out->dst != expect_master) {
        return HM_ERR_DST;
    }
    if (expect_stat && out->src != expect_stat) {
        return HM_ERR_SRC;
    }
    if (out->func == HM_FUNC_WRITE) {
        return flen == HM_WRITE_REPLY_LEN ? HM_OK : HM_ERR_LEN;
    }
    if (out->func != HM_FUNC_READ) {
        return HM_ERR_FUNC;
    }
    if (flen < HM_READ_REPLY_OVERHEAD) {
        return HM_ERR_LEN;
    }
    out->start = (uint16_t)(buf[5] | (buf[6] << 8));
    out->count = (uint16_t)(buf[7] | (buf[8] << 8));
    if ((size_t)out->count + HM_READ_REPLY_OVERHEAD != flen) {
        return HM_ERR_COUNT;
    }
    out->data = &buf[9];
    return HM_OK;
}

const char *hm_status_str(hm_status_t st)
{
    switch (st) {
    case HM_OK:        return "ok";
    case HM_ERR_SHORT: return "short frame";
    case HM_ERR_LEN:   return "bad length";
    case HM_ERR_CRC:   return "CRC error";
    case HM_ERR_DST:   return "wrong destination";
    case HM_ERR_SRC:   return "wrong source";
    case HM_ERR_FUNC:  return "bad function code";
    case HM_ERR_COUNT: return "data count mismatch";
    case HM_ERR_ARG:   return "bad argument";
    }
    return "?";
}
