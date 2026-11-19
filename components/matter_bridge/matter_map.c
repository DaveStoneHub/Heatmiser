#include "matter_map.h"

#include <string.h>

void mm_from_view(const zone_view_t *v, mm_thermo_t *o)
{
    memset(o, 0, sizeof *o);
    o->reachable = v->link != HM_LINK_OFFLINE;
    if (!v->have || v->room_t10 == ZONE_T_ABSENT) {
        o->local_null = true;
    } else {
        o->local_t = (int16_t)(v->room_t10 * 10);
    }
    int c = v->have || v->pend.set_temp ? zone_view_shown_set(v) : 20;
    if (c < 5) {
        c = 5;
    } else if (c > 35) {
        c = 35;
    }
    o->heat_setpoint = (int16_t)(c * 100);
    hm_mode_t m = zone_view_shown_mode(v);
    o->system_mode = v->have || v->pend.mode ? (m == HM_MODE_HEAT ? MM_SYSTEM_MODE_HEAT : MM_SYSTEM_MODE_OFF)
                                             : MM_SYSTEM_MODE_HEAT;
    if (v->have && v->heating) {
        o->running_state = MM_RUNNING_HEAT;
        o->pi_heating = 100;
    }
    uint16_t hold = v->pend.hold ? v->pend.hold_min : v->hold_min;
    o->hold = hold > 0;
    o->hold_null = hold == 0;
    o->hold_min = hold;
    o->keypad_lockout = zone_view_shown_lock(v) ? 1 : 0;
    o->display_mode = v->fahrenheit ? 1 : 0;
}

int mm_setpoint_to_c(int32_t centi)
{
    int32_t c = centi >= 0 ? (centi + 50) / 100 : -((-centi + 50) / 100);
    if (c < 5) {
        c = 5;
    } else if (c > 35) {
        c = 35;
    }
    return (int)c;
}

bool mm_mode_from_system(uint8_t sm, hm_mode_t *out)
{
    switch (sm) {
    case MM_SYSTEM_MODE_HEAT:
        *out = HM_MODE_HEAT;
        return true;
    case MM_SYSTEM_MODE_OFF:
        *out = HM_MODE_FROST;
        return true;
    default:
        return false;
    }
}
