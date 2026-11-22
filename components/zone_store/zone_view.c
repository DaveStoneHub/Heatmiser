#include "zone_view.h"
#include "zone_store.h"
#include "hm_write.h"

#include <stdio.h>
#include <string.h>

static int16_t t10(uint16_t raw)
{
    /* Tenths of a degree, 0xFFFF when the sensor is not fitted. Anything
     * above 100 C is not a room temperature either. */
    if (raw == HM_TEMP_ABSENT || raw > 1000) {
        return ZONE_T_ABSENT;
    }
    return (int16_t)raw;
}

void zone_view_from_stat(const hm_stat_t *st, uint32_t now_ms, zone_view_t *v)
{
    v->link = st->link;
    hm_dcb_t d;
    if (!st->have_dcb || hm_dcb_decode(st->dcb, st->dcb_len, &d) != 0) {
        v->have = false;
        v->age_s = 0;
        return;
    }
    v->have = true;
    v->age_s = (now_ms - st->last_ok_ms) / 1000;
    v->model = d.model;
    v->is_prthw = d.model == HM_MODEL_PRTHW;
    v->room_t10 = t10(hm_dcb_room_t10(&d));
    v->air_t10 = t10(d.air_t10);
    v->remote_t10 = t10(d.remote_air_t10);
    v->floor_t10 = t10(d.floor_t10);
    v->sensor_sel = d.sensor_sel;
    v->set_temp = d.set_temp;
    v->frost_temp = d.frost_temp;
    v->frost_enabled = d.frost_enabled;
    v->mode = hm_dcb_mode(&d);
    v->heating = d.heating_on != 0;
    v->locked = d.key_lock != 0;
    v->hold_min = d.hold_minutes;
    v->holiday_h = d.holiday_hours;
    v->error_code = d.error_code;
    v->fahrenheit = d.temp_format == 1;
    v->hw_on = d.has_hw && d.hw_on;
    v->prog_mode = d.prog_mode;
    v->comfort_days = d.comfort_days;
    memcpy(v->comfort, d.comfort, sizeof v->comfort);
    v->hw_timer_days = d.hw_timer_days;
    memcpy(v->hw_on_at, d.hw_on_at, sizeof v->hw_on_at);
    memcpy(v->hw_off_at, d.hw_off_at, sizeof v->hw_off_at);
    v->clk_wday = d.has_clock ? d.clk_wday : 0;
    v->clk_hour = d.clk_hour;
    v->clk_min = d.clk_min;
}

uint8_t zone_view_shown_set(const zone_view_t *v)
{
    return v->pend.set_temp ? v->pend.set_temp_c : v->set_temp;
}

hm_mode_t zone_view_shown_mode(const zone_view_t *v)
{
    return v->pend.mode ? v->pend.mode_v : v->mode;
}

bool zone_view_shown_lock(const zone_view_t *v)
{
    return v->pend.lock ? v->pend.lock_v : v->locked;
}

bool zone_view_busy(const zone_view_t *v)
{
    const zone_pending_t *p = &v->pend;
    return p->set_temp || p->mode || p->lock || p->hold || p->hw || p->comfort || p->hw_timer;
}

void zone_fmt_t10(int16_t t, char *buf, size_t cap)
{
    if (t == ZONE_T_ABSENT) {
        snprintf(buf, cap, "--.-");
        return;
    }
    snprintf(buf, cap, "%d.%d", t / 10, t % 10);
}

const char *zone_state_str(const zone_view_t *v)
{
    if (v->link == HM_LINK_OFFLINE) {
        return "Offline";
    }
    if (!v->have) {
        return "Connecting";
    }
    if (v->holiday_h) {
        return "Holiday";
    }
    switch (zone_view_shown_mode(v)) {
    case HM_MODE_OFF:
        return "Off";
    case HM_MODE_FROST:
        return "Frost";
    default:
        break;
    }
    if (v->hold_min) {
        return v->heating ? "Heating, hold" : "Hold";
    }
    return v->heating ? "Heating" : "Idle";
}

const char *zone_error_str(uint8_t e)
{
    switch (e) {
    case 0:
        return NULL;
    case 0xE0:
        return "Air sensor fault";
    case 0xE1:
        return "Floor sensor fault";
    case 0xE2:
        return "Remote sensor fault";
    default:
        return "Stat fault";
    }
}

const char *zone_sensor_str(uint8_t s)
{
    switch (s) {
    case 1:
        return "Remote air";
    case 2:
        return "Floor";
    case 3: /* air, with the floor sensor as a limit */
        return "Air";
    case 4: /* remote air, with the floor sensor as a limit */
        return "Remote air";
    default:
        return "Air";
    }
}

void zone_default_name(uint8_t addr, uint8_t model, char *buf, size_t cap)
{
    (void)model;
    snprintf(buf, cap, "Zone %u", addr);
}

bool zone_clean_name(const char *in, char out[ZONE_NAME_MAX + 1])
{
    size_t n = 0;
    while (*in == ' ') {
        in++;
    }
    for (; *in && n < ZONE_NAME_MAX; in++) {
        unsigned char c = (unsigned char)*in;
        if (c >= 0x20 && c < 0x7F) {
            out[n++] = (char)c;
        }
    }
    while (n && out[n - 1] == ' ') {
        n--;
    }
    out[n] = 0;
    return n > 0;
}

/* ---- programme ---------------------------------------------------------- */

static int minute_of(const hm_period_t *p)
{
    return p->hour < 24 ? p->hour * 60 + p->minute : -1;
}

int zone_prog_day(uint8_t days, int wday)
{
    if (wday < 1 || wday > 7) {
        return 0;
    }
    if (days == 7) {
        return wday - 1;
    }
    return wday >= 6 ? 1 : 0;
}

const char *zone_prog_day_name(uint8_t days, int day)
{
    static const char *const week[] = { "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun" };
    if (days == 7) {
        return day >= 0 && day < 7 ? week[day] : "?";
    }
    return day == 0 ? "Weekdays" : "Weekend";
}

uint8_t zone_comfort_at(const hm_period_t p[4], int m)
{
    int best = -1, last = -1;
    for (int i = 0; i < 4; i++) {
        int t = minute_of(&p[i]);
        if (t < 0) {
            continue;
        }
        if (last < 0 || t >= minute_of(&p[last])) {
            last = i;
        }
        if (t <= m && (best < 0 || t >= minute_of(&p[best]))) {
            best = i;
        }
    }
    if (best >= 0) {
        return p[best].temp;
    }
    return last >= 0 ? p[last].temp : 0;
}

/* Which comfort period is in force at minute m: its index, or 4 + the index
 * of the day's last period before the first one starts (last night's level
 * carrying over), or -1 if the day has none. */
static int comfort_period_at(const hm_period_t p[4], int m)
{
    int best = -1, last = -1;
    for (int i = 0; i < 4; i++) {
        int t = minute_of(&p[i]);
        if (t < 0) {
            continue;
        }
        if (last < 0 || t >= minute_of(&p[last])) {
            last = i;
        }
        if (t <= m && (best < 0 || t >= minute_of(&p[best]))) {
            best = i;
        }
    }
    return best >= 0 ? best : last >= 0 ? 4 + last : -1;
}

int zone_clock_drift_min(const zone_view_t *v, int wday, int minute)
{
    if (!v->have || v->clk_wday < 1 || v->clk_wday > 7 || v->clk_hour > 23 || v->clk_min > 59 || wday < 1 ||
        wday > 7 || minute < 0 || minute > 1439) {
        return -1;
    }
    const int week = 7 * 1440;
    const int stat = (v->clk_wday - 1) * 1440 + v->clk_hour * 60 + v->clk_min;
    const int ours = (wday - 1) * 1440 + minute;
    int d = ((stat - ours) % week + week) % week;
    return d > week / 2 ? week - d : d;
}

zone_clock_why_t zone_clock_due(const zone_clock_in_t *in)
{
    if (in->drift_min < 0) {
        return ZONE_CLOCK_NO; /* no clock, or not read yet */
    }
    if (in->since_min >= 0 && in->since_min < ZONE_CLOCK_RETRY_MIN) {
        return ZONE_CLOCK_NO;
    }
    if (in->drift_min > ZONE_CLOCK_DRIFT_MIN) {
        return ZONE_CLOCK_DRIFT;
    }
    if (in->minute >= ZONE_CLOCK_DAILY_MIN && in->last_day != in->day) {
        return ZONE_CLOCK_DAILY;
    }
    return ZONE_CLOCK_NO;
}

bool zone_setpoint_to_restore(const zone_view_t *v, int new_wday, int new_minute, uint8_t *out)
{
    if (!v->have || v->comfort_days == 0 || v->holiday_h || v->pend.mode || v->pend.comfort ||
        zone_view_shown_mode(v) != HM_MODE_HEAT || v->clk_wday < 1 || v->clk_wday > 7 ||
        v->clk_hour > 23 || v->clk_min > 59) {
        return false;
    }
    const int day = zone_prog_day(v->comfort_days, v->clk_wday);
    const int m = v->clk_hour * 60 + v->clk_min;
    const int period = comfort_period_at(v->comfort[day], m);
    if (period < 0) {
        return false;
    }
    const uint8_t want = zone_view_shown_set(v);
    if (want == zone_comfort_at(v->comfort[day], m)) {
        return false; /* following the programme: nothing to lose */
    }
    /* Without a hold, an override ends at the next comfort level anyway; if
     * the corrected clock is past it, let it end. */
    const bool hold = v->pend.hold ? v->pend.hold_min > 0 : v->hold_min > 0;
    if (!hold) {
        if (new_wday < 1 || new_wday > 7 || new_minute < 0 || new_minute > 1439) {
            return false;
        }
        const int nday = zone_prog_day(v->comfort_days, new_wday);
        if (nday != day || comfort_period_at(v->comfort[nday], new_minute) != period) {
            return false;
        }
    }
    *out = want;
    return true;
}

bool zone_hw_timer_on(const hm_period_t on[4], const hm_period_t off[4], int m)
{
    for (int i = 0; i < 4; i++) {
        int a = minute_of(&on[i]), b = minute_of(&off[i]);
        if (a < 0) {
            continue;
        }
        if (b < 0) {
            b = 24 * 60; /* on to the end of the day */
        }
        if (m >= a && m < b) {
            return true;
        }
    }
    return false;
}

int zone_hw_timer_next(const hm_period_t on[4], const hm_period_t off[4], int m, bool *on_after)
{
    int best = -1;
    bool best_on = false;
    for (int i = 0; i < 4; i++) {
        int a = minute_of(&on[i]), b = minute_of(&off[i]);
        if (a < 0) {
            continue;
        }
        if (a > m && (best < 0 || a < best)) {
            best = a;
            best_on = true;
        }
        if (b > m && (best < 0 || b < best)) {
            best = b;
            best_on = false;
        }
    }
    if (on_after) {
        *on_after = best_on;
    }
    return best;
}

bool zone_periods_ok(const hm_period_t p[4])
{
    int prev = -1;
    bool ended = false;
    for (int i = 0; i < 4; i++) {
        int t = minute_of(&p[i]);
        if (t < 0) {
            ended = true;
            continue;
        }
        if (ended || t <= prev || p[i].minute > 59) {
            return false;
        }
        prev = t;
    }
    return true;
}

void zone_periods_sort(hm_period_t p[4], hm_period_t *off)
{
    for (int i = 1; i < 4; i++) {
        for (int j = i; j > 0; j--) {
            int a = minute_of(&p[j - 1]), b = minute_of(&p[j]);
            if (a < 0) {
                a = 24 * 60 + j; /* unused ones last, in their order */
            }
            if (b < 0) {
                b = 24 * 60 + j + 1;
            }
            if (a <= b) {
                break;
            }
            hm_period_t t = p[j];
            p[j] = p[j - 1];
            p[j - 1] = t;
            if (off) {
                t = off[j];
                off[j] = off[j - 1];
                off[j - 1] = t;
            }
        }
    }
}

size_t zone_list_encode(const zone_cfg_t *z, size_t n, uint8_t *out, size_t cap)
{
    if (n > HM_SCHED_MAX_STATS || cap < 2 + n * ZONE_LIST_ENTRY_SIZE) {
        return 0;
    }
    out[0] = ZONE_LIST_VERSION;
    out[1] = (uint8_t)n;
    uint8_t *p = out + 2;
    for (size_t i = 0; i < n; i++) {
        p[0] = z[i].addr;
        p[1] = z[i].model;
        p[2] = (uint8_t)(z[i].endpoint & 0xFF);
        p[3] = (uint8_t)(z[i].endpoint >> 8);
        memset(p + 4, 0, ZONE_NAME_MAX + 1);
        strncpy((char *)p + 4, z[i].name, ZONE_NAME_MAX);
        p += ZONE_LIST_ENTRY_SIZE;
    }
    return (size_t)(p - out);
}

int zone_list_decode(const uint8_t *in, size_t len, zone_cfg_t *z, size_t cap)
{
    if (len < 2 || in[0] != ZONE_LIST_VERSION) {
        return -1;
    }
    size_t n = in[1];
    if (n > HM_SCHED_MAX_STATS || n > cap || len != 2 + n * ZONE_LIST_ENTRY_SIZE) {
        return -1;
    }
    const uint8_t *p = in + 2;
    for (size_t i = 0; i < n; i++, p += ZONE_LIST_ENTRY_SIZE) {
        if (p[0] < HM_STAT_ADDR_MIN || p[0] > HM_STAT_ADDR_MAX) {
            return -1;
        }
        if (i && p[0] <= z[i - 1].addr) {
            return -1; /* must be sorted, no repeats */
        }
        z[i].addr = p[0];
        z[i].model = p[1];
        z[i].endpoint = (uint16_t)(p[2] | (p[3] << 8));
        memcpy(z[i].name, p + 4, ZONE_NAME_MAX);
        z[i].name[ZONE_NAME_MAX] = 0;
        if (!z[i].name[0]) {
            zone_default_name(z[i].addr, z[i].model, z[i].name, sizeof z[i].name);
        }
    }
    return (int)n;
}

bool zone_hw_switch_plan(const zone_view_t *v, bool on, int wday, int minute, unsigned boost_min,
                         zone_hw_plan_t *out)
{
    const bool known = wday >= 1 && wday <= 7 && minute >= 0 && minute < 24 * 60 && v->hw_timer_days > 0;
    bool timer_on = false;
    int day = 0;
    if (known) {
        day = zone_prog_day(v->hw_timer_days, wday);
        timer_on = zone_hw_timer_on(v->hw_on_at[day], v->hw_off_at[day], minute);
    }
    out->cmd = HM_HW_FOLLOW_TIMER;
    out->mode = ZONE_HW_TIMER;
    out->minutes = 0;
    if (on) {
        if (v->hw_mode == ZONE_HW_BOOST || v->hw_mode == ZONE_HW_ON) {
            return false;
        }
        if (v->hw_mode == ZONE_HW_TIMER && v->hw_on) {
            return false;
        }
        if (v->hw_mode == ZONE_HW_OFF && known && timer_on) {
            return true; /* held off while the timer is on: let the timer have it */
        }
        out->cmd = HM_HW_FORCE_ON;
        out->mode = ZONE_HW_BOOST;
        out->minutes = boost_min;
        return true;
    }
    if (v->hw_mode == ZONE_HW_OFF) {
        return false;
    }
    if (!known || timer_on) {
        out->cmd = HM_HW_FORCE_OFF;
        out->mode = ZONE_HW_OFF;
        out->minutes = boost_min;
        if (known) {
            bool on_after = false;
            int next = zone_hw_timer_next(v->hw_on_at[day], v->hw_off_at[day], minute, &on_after);
            int end = next >= 0 && !on_after ? next : 24 * 60; /* on to the end of the day */
            out->minutes = (unsigned)(end - minute);
        }
        return true;
    }
    /* The timer has it off: going back to the timer turns it off. */
    return !(v->hw_mode == ZONE_HW_TIMER && !v->hw_on);
}

size_t zone_hw_encode(const zone_hw_saved_t *e, size_t n, uint8_t *out, size_t cap)
{
    if (n > HM_SCHED_MAX_STATS || cap < 2 + n * ZONE_HW_ENTRY_SIZE) {
        return 0;
    }
    out[0] = ZONE_HW_BLOB_VERSION;
    out[1] = (uint8_t)n;
    uint8_t *p = out + 2;
    for (size_t i = 0; i < n; i++, p += ZONE_HW_ENTRY_SIZE) {
        p[0] = e[i].addr;
        p[1] = e[i].mode;
        for (int b = 0; b < 4; b++) {
            p[2 + b] = (uint8_t)(e[i].end_utc >> (8 * b));
        }
        p[6] = (uint8_t)e[i].len_min;
        p[7] = (uint8_t)(e[i].len_min >> 8);
    }
    return (size_t)(p - out);
}

int zone_hw_decode(const uint8_t *in, size_t len, zone_hw_saved_t *e, size_t cap)
{
    if (len < 2 || (in[0] != ZONE_HW_BLOB_VERSION && in[0] != 1)) {
        return -1;
    }
    const size_t size = in[0] == 1 ? ZONE_HW_ENTRY_SIZE_V1 : ZONE_HW_ENTRY_SIZE;
    size_t n = in[1];
    if (n > HM_SCHED_MAX_STATS || n > cap || len != 2 + n * size) {
        return -1;
    }
    const uint8_t *p = in + 2;
    for (size_t i = 0; i < n; i++, p += size) {
        if (p[0] < HM_STAT_ADDR_MIN || p[0] > HM_STAT_ADDR_MAX || p[1] > ZONE_HW_BOOST) {
            return -1;
        }
        e[i].addr = p[0];
        e[i].mode = p[1];
        e[i].end_utc = (uint32_t)p[2] | ((uint32_t)p[3] << 8) | ((uint32_t)p[4] << 16) | ((uint32_t)p[5] << 24);
        e[i].len_min = size == ZONE_HW_ENTRY_SIZE ? (uint16_t)(p[6] | (p[7] << 8)) : 0;
    }
    return (int)n;
}

const char *zone_err_str(zone_err_t e)
{
    switch (e) {
    case ZONE_OK:          return "ok";
    case ZONE_ERR_ADDR:    return "no such zone";
    case ZONE_ERR_RANGE:   return "out of range";
    case ZONE_ERR_OFFLINE: return "stat is offline";
    case ZONE_ERR_BUSY:    return "bus queue full, try again";
    case ZONE_ERR_MODEL:   return "not on this model";
    case ZONE_ERR_STORAGE: return "could not save";
    }
    return "?";
}
