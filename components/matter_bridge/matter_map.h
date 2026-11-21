/*
 * Zone view <-> Matter Thermostat values (design doc, "Thermostat cluster
 * mapping"). Pure C, host tested.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "zone_view.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MM_SYSTEM_MODE_OFF   0
#define MM_SYSTEM_MODE_HEAT  4
#define MM_CONTROL_HEATING_ONLY 2
#define MM_RUNNING_HEAT      0x0001
#define MM_SETPOINT_MIN      500     /* 0.01 degC */
#define MM_SETPOINT_MAX      3500

typedef struct {
    bool     reachable;
    bool     local_null;             /* no sensor reading */
    int16_t  local_t;                /* 0.01 degC */
    int16_t  heat_setpoint;          /* 0.01 degC */
    uint8_t  system_mode;            /* MM_SYSTEM_MODE_* */
    uint16_t running_state;          /* MM_RUNNING_HEAT or 0 */
    uint8_t  pi_heating;             /* 0 or 100 */
    uint8_t  hold;                   /* TemperatureSetpointHold, 0 or 1 */
    bool     hold_null;
    uint16_t hold_min;               /* TemperatureSetpointHoldDuration */
    uint8_t  keypad_lockout;         /* 0 or 1 */
    uint8_t  display_mode;           /* 0 Celsius, 1 Fahrenheit */
} mm_thermo_t;

/* Hot water (PRTHW) as an On/Off Plug-in Unit. */
#define MM_HW_BOOST_MIN 60           /* On from a controller boosts for this long */

typedef struct {
    bool reachable;
    bool on;                         /* hot water on now, or as asked for */
} mm_hw_t;

void mm_hw_from_view(const zone_view_t *v, mm_hw_t *out);

/* What Matter should show for a zone. Pending writes show as asked for, so a
 * controller's change does not bounce back while it is on the bus. */
void mm_from_view(const zone_view_t *v, mm_thermo_t *out);

/* A Matter heating setpoint (0.01 degC) as the stat's whole degrees, 5-35. */
int mm_setpoint_to_c(int32_t centi);

/* Matter SystemMode to a stat mode: Heat -> heating, Off -> frost (frost
 * protection stays on). False for modes a heat-only stat has not got. */
bool mm_mode_from_system(uint8_t system_mode, hm_mode_t *out);

#ifdef __cplusplus
}
#endif
