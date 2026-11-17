#include "hm_write.h"

#include <stdio.h>
#include <string.h>

#include "hm_frame.h"

static hm_wr_status_t one_byte(hm_write_t *w, uint16_t addr, int v, int lo, int hi)
{
    if (!w) {
        return HM_WR_ARG;
    }
    if (v < lo || v > hi) {
        return HM_WR_RANGE;
    }
    memset(w, 0, sizeof(*w));
    w->addr = addr;
    w->len = 1;
    w->data[0] = (uint8_t)v;
    return HM_WR_OK;
}

static hm_wr_status_t two_bytes(hm_write_t *w, uint16_t addr, unsigned v)
{
    if (!w) {
        return HM_WR_ARG;
    }
    if (v > 0xFFFFu) {
        return HM_WR_RANGE;
    }
    memset(w, 0, sizeof(*w));
    w->addr = addr;
    w->len = 2;
    w->data[0] = (uint8_t)(v & 0xFF); /* low byte first on write */
    w->data[1] = (uint8_t)(v >> 8);
    return HM_WR_OK;
}

hm_wr_status_t hm_wr_set_temp(hm_write_t *w, int c)
{
    return one_byte(w, HM_UA_SET_TEMP, c, HM_SET_TEMP_MIN, HM_SET_TEMP_MAX);
}

hm_wr_status_t hm_wr_frost_temp(hm_write_t *w, int c)
{
    return one_byte(w, HM_UA_FROST_TEMP, c, HM_FROST_TEMP_MIN, HM_FROST_TEMP_MAX);
}

hm_wr_status_t hm_wr_floor_max(hm_write_t *w, int c)
{
    return one_byte(w, HM_UA_FLOOR_MAX, c, HM_FLOOR_MAX_MIN, HM_FLOOR_MAX_MAX);
}

hm_wr_status_t hm_wr_on_off(hm_write_t *w, bool on)
{
    return one_byte(w, HM_UA_ON_OFF, on ? 1 : 0, 0, 1);
}

hm_wr_status_t hm_wr_key_lock(hm_write_t *w, bool locked)
{
    return one_byte(w, HM_UA_KEY_LOCK, locked ? 1 : 0, 0, 1);
}

hm_wr_status_t hm_wr_run_mode(hm_write_t *w, bool frost)
{
    return one_byte(w, HM_UA_RUN_MODE, frost ? 1 : 0, 0, 1);
}

hm_wr_status_t hm_wr_frost_enable(hm_write_t *w, bool enabled)
{
    return one_byte(w, HM_UA_FROST_ENABLE, enabled ? 0 : 1, 0, 1);
}

hm_wr_status_t hm_wr_holiday(hm_write_t *w, unsigned hours)
{
    return two_bytes(w, HM_UA_HOLIDAY_HOURS, hours);
}

hm_wr_status_t hm_wr_hold(hm_write_t *w, unsigned minutes)
{
    return two_bytes(w, HM_UA_HOLD_MINUTES, minutes);
}

hm_wr_status_t hm_wr_hw_state(hm_write_t *w, uint8_t model, int cmd)
{
    if (w && model != HM_MODEL_PRTHW) {
        return HM_WR_MODEL;
    }
    return one_byte(w, HM_UA_HW_STATE, cmd, HM_HW_FOLLOW_TIMER, HM_HW_FORCE_OFF);
}

hm_wr_status_t hm_wr_clock(hm_write_t *w, int wday, int hour, int min, int sec)
{
    if (!w) {
        return HM_WR_ARG;
    }
    if (wday < 1 || wday > 7 || hour < 0 || hour > 23 || min < 0 || min > 59 || sec < 0 ||
        sec > 59) {
        return HM_WR_RANGE;
    }
    memset(w, 0, sizeof(*w));
    w->addr = HM_UA_CLOCK;
    w->len = 4;
    w->data[0] = (uint8_t)wday;
    w->data[1] = (uint8_t)hour;
    w->data[2] = (uint8_t)min;
    w->data[3] = (uint8_t)sec;
    return HM_WR_OK;
}

/* Periods: valid times, cancelled (hour 24) ones last, the rest ascending. */
static bool periods_ok(const hm_period_t *p, size_t n, size_t stride)
{
    bool cancelled = false;
    int last = -1;
    for (size_t i = 0; i < n; i++) {
        const hm_period_t *x = &p[i * stride];
        if (x->hour == 24) {
            cancelled = true;
            continue;
        }
        if (cancelled || x->hour > 23 || x->minute > 59) {
            return false;
        }
        int t = x->hour * 60 + x->minute;
        if (t <= last) {
            return false;
        }
        last = t;
    }
    return true;
}

static int day_addr(uint8_t model, uint8_t prog_mode, int day, uint16_t base52,
                    uint16_t base7, uint16_t size)
{
    if (day < 0 || day >= (prog_mode ? 7 : 2)) {
        return -1;
    }
    uint16_t ua = (uint16_t)((prog_mode ? base7 : base52) + day * size);
    return hm_dcb_index(model, prog_mode, ua) < 0 ? -1 : ua;
}

hm_wr_status_t hm_wr_comfort(hm_write_t *w, uint8_t model, uint8_t prog_mode, int day,
                             const hm_period_t p[4])
{
    if (!w || !p) {
        return HM_WR_ARG;
    }
    if (model != HM_MODEL_PRT && model != HM_MODEL_PRT_E && model != HM_MODEL_PRTHW) {
        return HM_WR_MODEL;
    }
    int ua = day_addr(model, prog_mode, day, HM_UA_COMFORT_52, HM_UA_COMFORT_7DAY, 12);
    if (ua < 0) {
        return HM_WR_RANGE;
    }
    if (!periods_ok(p, 4, 1)) {
        return HM_WR_RANGE;
    }
    for (int i = 0; i < 4; i++) {
        if (p[i].hour != 24 && (p[i].temp < HM_SET_TEMP_MIN || p[i].temp > HM_SET_TEMP_MAX)) {
            return HM_WR_RANGE;
        }
    }
    memset(w, 0, sizeof(*w));
    w->addr = (uint16_t)ua;
    w->len = 12;
    for (int i = 0; i < 4; i++) {
        w->data[i * 3] = p[i].hour;
        w->data[i * 3 + 1] = p[i].hour == 24 ? 0 : p[i].minute;
        w->data[i * 3 + 2] = p[i].temp;
    }
    return HM_WR_OK;
}

hm_wr_status_t hm_wr_hw_timer(hm_write_t *w, uint8_t model, uint8_t prog_mode, int day,
                              const hm_period_t on[4], const hm_period_t off[4])
{
    if (!w || !on || !off) {
        return HM_WR_ARG;
    }
    if (model != HM_MODEL_PRTHW) {
        return HM_WR_MODEL;
    }
    int ua = day_addr(model, prog_mode, day, HM_UA_HW_TIMER_52, HM_UA_HW_TIMER_7DAY, 16);
    if (ua < 0) {
        return HM_WR_RANGE;
    }
    /* Interleave on/off so the time-order check covers both. */
    hm_period_t seq[8];
    for (int i = 0; i < 4; i++) {
        seq[i * 2] = on[i];
        seq[i * 2 + 1] = off[i];
        if ((on[i].hour == 24) != (off[i].hour == 24)) {
            return HM_WR_RANGE;
        }
    }
    if (!periods_ok(seq, 8, 1)) {
        return HM_WR_RANGE;
    }
    memset(w, 0, sizeof(*w));
    w->addr = (uint16_t)ua;
    w->len = 16;
    for (int i = 0; i < 4; i++) {
        w->data[i * 4] = on[i].hour;
        w->data[i * 4 + 1] = on[i].hour == 24 ? 0 : on[i].minute;
        w->data[i * 4 + 2] = off[i].hour;
        w->data[i * 4 + 3] = off[i].hour == 24 ? 0 : off[i].minute;
    }
    return HM_WR_OK;
}

size_t hm_wr_mode(hm_write_t out[2], hm_mode_t mode)
{
    if (!out) {
        return 0;
    }
    switch (mode) {
    case HM_MODE_OFF:
        hm_wr_on_off(&out[0], false);
        return 1;
    case HM_MODE_HEAT:
    case HM_MODE_FROST:
        hm_wr_on_off(&out[0], true);
        hm_wr_run_mode(&out[1], mode == HM_MODE_FROST);
        return 2;
    }
    return 0;
}

size_t hm_write_frame(const hm_write_t *w, uint8_t *out, size_t cap, uint8_t stat,
                      uint8_t master)
{
    if (!w || w->len == 0 || w->len > HM_WRITE_MAX_DATA || stat < HM_STAT_ADDR_MIN ||
        stat > HM_STAT_ADDR_MAX) {
        return 0;
    }
    return hm_build_write(out, cap, stat, master, w->addr, w->data, w->len);
}

hm_verify_t hm_write_verify(const hm_write_t *w, const uint8_t *dcb, size_t n)
{
    if (!w || !dcb || n < HM_DCB_MIN_LEN) {
        return HM_VERIFY_NO_FIELD;
    }
    const uint8_t model = dcb[HM_IDX_MODEL];
    const uint8_t prog = dcb[HM_IDX_PROG_MODE];
    const int idx = hm_dcb_index(model, prog, w->addr);
    if (idx < 0 || (size_t)idx + w->len > n) {
        return HM_VERIFY_NO_FIELD;
    }
    const uint8_t *p = &dcb[idx];

    switch (w->addr) {
    case HM_UA_CLOCK:
        return HM_VERIFY_UNCHECKED; /* it has moved on by the time we read */
    case HM_UA_HW_STATE:
        if (w->data[0] == HM_HW_FORCE_ON) {
            return p[0] == 1 ? HM_VERIFY_OK : HM_VERIFY_MISMATCH;
        }
        if (w->data[0] == HM_HW_FORCE_OFF) {
            return p[0] == 0 ? HM_VERIFY_OK : HM_VERIFY_MISMATCH;
        }
        return HM_VERIFY_UNCHECKED; /* follow timer: either state is right */
    case HM_UA_HOLIDAY_HOURS:
    case HM_UA_HOLD_MINUTES: {
        /* Written low byte first, read high byte first; and they count down. */
        unsigned want = (unsigned)w->data[0] | ((unsigned)w->data[1] << 8);
        unsigned got = ((unsigned)p[0] << 8) | p[1];
        return (got <= want && got + 2 >= want) ? HM_VERIFY_OK : HM_VERIFY_MISMATCH;
    }
    default:
        break;
    }
    /* Comfort levels: the minute and temperature of a cancelled period are
     * don't-cares, so compare hours only there. */
    if (w->len == 12 && w->addr >= HM_UA_COMFORT_52) {
        for (int i = 0; i < 4; i++) {
            if (p[i * 3] != w->data[i * 3]) {
                return HM_VERIFY_MISMATCH;
            }
            if (w->data[i * 3] != 24 && memcmp(&p[i * 3 + 1], &w->data[i * 3 + 1], 2) != 0) {
                return HM_VERIFY_MISMATCH;
            }
        }
        return HM_VERIFY_OK;
    }
    if (w->len == 16) {
        for (int i = 0; i < 8; i++) {
            if (p[i * 2] != w->data[i * 2]) {
                return HM_VERIFY_MISMATCH;
            }
            if (w->data[i * 2] != 24 && p[i * 2 + 1] != w->data[i * 2 + 1]) {
                return HM_VERIFY_MISMATCH;
            }
        }
        return HM_VERIFY_OK;
    }
    return memcmp(p, w->data, w->len) == 0 ? HM_VERIFY_OK : HM_VERIFY_MISMATCH;
}

const char *hm_wr_status_str(hm_wr_status_t s)
{
    switch (s) {
    case HM_WR_OK:    return "ok";
    case HM_WR_RANGE: return "out of range";
    case HM_WR_MODEL: return "not on this model";
    case HM_WR_ARG:   return "bad argument";
    }
    return "?";
}

const char *hm_verify_str(hm_verify_t v)
{
    switch (v) {
    case HM_VERIFY_OK:        return "confirmed";
    case HM_VERIFY_UNCHECKED: return "acked (not checkable)";
    case HM_VERIFY_MISMATCH:  return "read-back differs";
    case HM_VERIFY_NO_FIELD:  return "field missing from DCB";
    }
    return "?";
}

void hm_write_describe(const hm_write_t *w, char *buf, size_t cap)
{
    if (!buf || cap == 0) {
        return;
    }
    if (!w) {
        snprintf(buf, cap, "?");
        return;
    }
    const unsigned v16 = (unsigned)w->data[0] | ((unsigned)w->data[1] << 8);
    switch (w->addr) {
    case HM_UA_FROST_ENABLE:  snprintf(buf, cap, "frost protect %s", w->data[0] ? "off" : "on"); break;
    case HM_UA_FROST_TEMP:    snprintf(buf, cap, "frost temp %u", w->data[0]); break;
    case HM_UA_SET_TEMP:      snprintf(buf, cap, "set temp %u", w->data[0]); break;
    case HM_UA_FLOOR_MAX:     snprintf(buf, cap, "floor max %u", w->data[0]); break;
    case HM_UA_ON_OFF:        snprintf(buf, cap, "power %s", w->data[0] ? "on" : "off"); break;
    case HM_UA_KEY_LOCK:      snprintf(buf, cap, "key lock %s", w->data[0] ? "on" : "off"); break;
    case HM_UA_RUN_MODE:      snprintf(buf, cap, "run mode %s", w->data[0] ? "frost" : "heat"); break;
    case HM_UA_HOLIDAY_HOURS: snprintf(buf, cap, "holiday %u h", v16); break;
    case HM_UA_HOLD_MINUTES:  snprintf(buf, cap, "hold %u min", v16); break;
    case HM_UA_HW_STATE: {
        static const char *hw[] = { "timer", "on", "off" };
        snprintf(buf, cap, "hot water %s", w->data[0] <= 2 ? hw[w->data[0]] : "?");
        break;
    }
    case HM_UA_CLOCK:
        snprintf(buf, cap, "clock day %u %02u:%02u:%02u", w->data[0], w->data[1], w->data[2],
                 w->data[3]);
        break;
    default:
        snprintf(buf, cap, "%s @%u", w->len == 16 ? "hw timer" : w->len == 12 ? "comfort" : "write",
                 w->addr);
        break;
    }
}
