#include "zone_view.h"
#include "zone_store.h"

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
    return p->set_temp || p->mode || p->lock || p->hold || p->hw;
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
    snprintf(buf, cap, model == HM_MODEL_PRTHW ? "HW stat %u" : "Stat %u", addr);
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
