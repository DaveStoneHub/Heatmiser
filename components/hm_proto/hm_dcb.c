#include "hm_dcb.h"
#include <string.h>

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

const char *hm_model_str(uint8_t model)
{
    switch (model) {
    case HM_MODEL_DT:    return "DT";
    case HM_MODEL_DT_E:  return "DT-E";
    case HM_MODEL_PRT:   return "PRT";
    case HM_MODEL_PRT_E: return "PRT-E";
    case HM_MODEL_PRTHW: return "PRT/HW";
    }
    return "unknown";
}

static void decode_comfort(const uint8_t *p, hm_period_t day[4])
{
    for (int i = 0; i < 4; i++) {
        day[i].hour = p[i * 3];
        day[i].minute = p[i * 3 + 1];
        day[i].temp = p[i * 3 + 2];
    }
}

static void decode_hw(const uint8_t *p, hm_period_t on[4], hm_period_t off[4])
{
    for (int i = 0; i < 4; i++) {
        on[i].hour = p[i * 4];
        on[i].minute = p[i * 4 + 1];
        on[i].temp = 0;
        off[i].hour = p[i * 4 + 2];
        off[i].minute = p[i * 4 + 3];
        off[i].temp = 0;
    }
}

int hm_dcb_decode(const uint8_t *dcb, size_t n, hm_dcb_t *out)
{
    if (!dcb || !out || n < HM_DCB_MIN_LEN) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    out->dcb_len = be16(&dcb[HM_IDX_DCB_LEN_HI]);
    if (out->dcb_len > n) {
        return -1;
    }
    /* Trust the stat's own length from here on. */
    if (out->dcb_len >= HM_DCB_MIN_LEN) {
        n = out->dcb_len;
    }

    out->model = dcb[HM_IDX_MODEL];
    out->vendor = dcb[HM_IDX_VENDOR];
    if (out->model == HM_MODEL_PRTHW) {
        out->version = dcb[HM_IDX_VERSION];
    } else {
        out->version = dcb[HM_IDX_VERSION] & 0x7F;
        out->floor_limit_active = (dcb[HM_IDX_VERSION] & 0x80) != 0;
    }
    out->temp_format = dcb[HM_IDX_TEMP_FORMAT];
    out->switch_diff = dcb[HM_IDX_SWITCH_DIFF];
    out->frost_raw = dcb[HM_IDX_FROST_ENABLE];
    out->frost_enabled = out->frost_raw == 0;
    out->calibration_raw = be16(&dcb[HM_IDX_CALIBRATION]);
    out->output_delay = dcb[HM_IDX_OUTPUT_DELAY];
    out->address = dcb[HM_IDX_ADDRESS];
    out->updown_limit = dcb[HM_IDX_UPDOWN_LIMIT];
    out->sensor_sel = dcb[HM_IDX_SENSOR_SEL];
    out->optimum_start = dcb[HM_IDX_OPTIMUM_START];
    out->rate_of_change = dcb[HM_IDX_RATE_OF_CHANGE];
    out->prog_mode = dcb[HM_IDX_PROG_MODE];
    out->frost_temp = dcb[HM_IDX_FROST_TEMP];
    out->set_temp = dcb[HM_IDX_SET_TEMP];
    out->floor_max = dcb[HM_IDX_FLOOR_MAX];
    out->floor_max_enable = dcb[HM_IDX_FLOOR_MAX_EN];
    out->on_off = dcb[HM_IDX_ON_OFF];
    out->key_lock = dcb[HM_IDX_KEY_LOCK];
    out->run_mode = dcb[HM_IDX_RUN_MODE];
    out->holiday_hours = be16(&dcb[HM_IDX_HOLIDAY_HOURS]);
    out->hold_minutes = be16(&dcb[HM_IDX_HOLD_MINUTES]);
    out->remote_air_t10 = be16(&dcb[HM_IDX_REMOTE_AIR]);
    out->floor_t10 = be16(&dcb[HM_IDX_FLOOR_TEMP]);
    out->air_t10 = be16(&dcb[HM_IDX_AIR_TEMP]);
    out->error_code = dcb[HM_IDX_ERROR_CODE];
    out->heating_on = dcb[HM_IDX_HEATING_ON];

    size_t clk;
    if (out->model == HM_MODEL_PRTHW) {
        if (n < HM_IDX_HW_STATE + 1) {
            return 0;
        }
        out->has_hw = true;
        out->hw_on = dcb[HM_IDX_HW_STATE];
        clk = HM_IDX_HW_STATE + 1;   /* 37 */
    } else if (out->model == HM_MODEL_PRT || out->model == HM_MODEL_PRT_E) {
        clk = HM_IDX_HEATING_ON + 1; /* 36 */
    } else {
        return 0; /* DT/DT-E end here; unknown models: common part only */
    }

    if (n < clk + 4) {
        return 0;
    }
    out->has_clock = true;
    out->clk_wday = dcb[clk];
    out->clk_hour = dcb[clk + 1];
    out->clk_min = dcb[clk + 2];
    out->clk_sec = dcb[clk + 3];

    size_t comfort = clk + 4;
    uint8_t days = out->prog_mode ? 7 : 2;
    if (n < comfort + (size_t)days * 12) {
        return 0;
    }
    out->comfort_days = days;
    for (uint8_t d = 0; d < days; d++) {
        decode_comfort(&dcb[comfort + d * 12u], out->comfort[d]);
    }

    if (out->has_hw) {
        size_t hw = comfort + (size_t)days * 12;
        if (n >= hw + (size_t)days * 16) {
            out->hw_timer_days = days;
            for (uint8_t d = 0; d < days; d++) {
                decode_hw(&dcb[hw + d * 16u], out->hw_on_at[d], out->hw_off_at[d]);
            }
        }
    }
    return 0;
}

static void print_temp(FILE *f, const char *label, uint16_t t10)
{
    if (t10 == HM_TEMP_ABSENT) {
        fprintf(f, "  %-22s not fitted\n", label);
    } else {
        fprintf(f, "  %-22s %u.%u C\n", label, t10 / 10, t10 % 10);
    }
}

static const char *day_label(uint8_t days, uint8_t d)
{
    static const char *w52[] = { "Mon-Fri", "Sat-Sun" };
    static const char *w7[] = { "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun" };
    return days == 2 ? w52[d] : w7[d];
}

void hm_dcb_print(FILE *f, const hm_dcb_t *d)
{
    static const char *sensors[] = {
        "built-in air", "remote air", "floor", "built-in air + floor", "remote air + floor"
    };
    static const char *wdays[] = { "?", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun" };

    fprintf(f, "  %-22s %u bytes\n", "DCB length", d->dcb_len);
    fprintf(f, "  %-22s %s (0x%02X), vendor %s, version %u\n", "Model",
            hm_model_str(d->model), d->model, d->vendor ? "OEM" : "Heatmiser", d->version);
    fprintf(f, "  %-22s %u\n", "Address", d->address);
    fprintf(f, "  %-22s %s\n", "Programme mode", d->prog_mode ? "7 day" : "5/2 day");
    fprintf(f, "  %-22s %s, %s\n", "Power / run mode", d->on_off ? "on" : "off",
            d->run_mode ? "frost protection" : "heating");
    fprintf(f, "  %-22s %u C\n", "Set temperature", d->set_temp);
    fprintf(f, "  %-22s %s, %u C (byte 7 = %02X)\n", "Frost protection",
            d->frost_enabled ? "enabled" : "disabled", d->frost_temp, d->frost_raw);
    print_temp(f, "Built-in air temp", d->air_t10);
    print_temp(f, "Remote air temp", d->remote_air_t10);
    print_temp(f, "Floor temp", d->floor_t10);
    fprintf(f, "  %-22s %s\n", "Sensor selection",
            d->sensor_sel < 5 ? sensors[d->sensor_sel] : "unknown");
    fprintf(f, "  %-22s %s\n", "Heating now", d->heating_on ? "yes" : "no");
    if (d->has_hw) {
        fprintf(f, "  %-22s %s\n", "Hot water now", d->hw_on ? "on" : "off");
    }
    fprintf(f, "  %-22s %s\n", "Key lock", d->key_lock ? "locked" : "unlocked");
    fprintf(f, "  %-22s %u h\n", "Holiday", d->holiday_hours);
    fprintf(f, "  %-22s %u min\n", "Temperature hold", d->hold_minutes);
    fprintf(f, "  %-22s 0x%02X%s\n", "Error code", d->error_code, d->error_code ? " (fault)" : "");
    fprintf(f, "  %-22s %s\n", "Temperature format", d->temp_format ? "F" : "C");
    fprintf(f, "  %-22s %u C, output delay %u min, up/down limit %u\n", "Switch differential",
            d->switch_diff, d->output_delay, d->updown_limit);
    fprintf(f, "  %-22s %u, rate of change %u\n", "Optimum start", d->optimum_start,
            d->rate_of_change);
    fprintf(f, "  %-22s %u C (%s)%s\n", "Floor max limit", d->floor_max,
            d->floor_max_enable ? "enabled" : "disabled",
            d->floor_limit_active ? ", limiting now" : "");
    fprintf(f, "  %-22s 0x%04X (raw; units unconfirmed)\n", "Calibration offset",
            d->calibration_raw);
    if (d->has_clock) {
        fprintf(f, "  %-22s %s %02u:%02u:%02u\n", "Stat clock",
                d->clk_wday <= 7 ? wdays[d->clk_wday] : "?", d->clk_hour, d->clk_min, d->clk_sec);
    }
    for (uint8_t day = 0; day < d->comfort_days; day++) {
        fprintf(f, "  %-22s", day == 0 ? "Comfort levels" : "");
        fprintf(f, "%-8s", day_label(d->comfort_days, day));
        for (int p = 0; p < 4; p++) {
            const hm_period_t *x = &d->comfort[day][p];
            if (x->hour == 24) {
                fprintf(f, "  --:--     ");
            } else {
                fprintf(f, "  %02u:%02u %2uC", x->hour, x->minute, x->temp);
            }
        }
        fputc('\n', f);
    }
    for (uint8_t day = 0; day < d->hw_timer_days; day++) {
        fprintf(f, "  %-22s", day == 0 ? "Hot water timers" : "");
        fprintf(f, "%-8s", day_label(d->hw_timer_days, day));
        for (int p = 0; p < 4; p++) {
            const hm_period_t *on = &d->hw_on_at[day][p];
            const hm_period_t *off = &d->hw_off_at[day][p];
            if (on->hour == 24) {
                fprintf(f, "  -----------");
            } else {
                fprintf(f, "  %02u:%02u-%02u:%02u", on->hour, on->minute, off->hour, off->minute);
            }
        }
        fputc('\n', f);
    }
}

void hm_hexdump(FILE *f, const uint8_t *buf, size_t n)
{
    for (size_t i = 0; i < n; i += 16) {
        fprintf(f, "  %04X:", (unsigned)i);
        for (size_t j = i; j < i + 16 && j < n; j++) {
            fprintf(f, " %02X", buf[j]);
        }
        fputc('\n', f);
    }
}
