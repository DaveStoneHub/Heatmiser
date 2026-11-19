/*
 * The Matter side of the bridge: one Matter node (root endpoint 0, an
 * Aggregator on endpoint 1) with a bridged Thermostat endpoint for each zone,
 * kept in step with the zone store.
 *
 * - Each zone's endpoint ID is saved with the zone and restored at boot, so
 *   controllers keep their rooms and automations. A removed zone's ID is
 *   never used again (esp-matter keeps the counter).
 * - Matter writes (setpoint, Heat/Off, key lock) are queued on the bus
 *   through the zone store; the attributes then follow what the stat
 *   reports, so a write the stat does not take is undone in the controller.
 *
 * Commissioning is over BLE with the test DAC (vendor 0xFFF1), and the
 * Matter stack owns Wi-Fi: call net_attach() after matter_bridge_start().
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Needs nvs_flash_init() and zone_store_init() first. */
esp_err_t matter_bridge_start(void);

#define MATTER_FABRICS_MAX 5

typedef struct {
    uint8_t  index;
    uint16_t vendor;
    char     label[33];
} matter_fabric_t;

typedef struct {
    bool     running;
    bool     window_open;
    uint32_t window_left_s;
    char     qr[48];
    char     manual[24];
    uint16_t endpoints;             /* bridged thermostats */
    uint8_t  fabric_count;
    matter_fabric_t fabrics[MATTER_FABRICS_MAX];
} matter_status_t;

void matter_bridge_status(matter_status_t *out);

/* Open the basic commissioning window so another controller can pair. */
esp_err_t matter_bridge_open_window(uint16_t seconds);
esp_err_t matter_bridge_remove_fabric(uint8_t index);

/* The thermostat endpoint of a zone, 0 if none. */
uint16_t matter_bridge_endpoint(uint8_t addr);

/* Print endpoints, fabrics and the pairing codes on the console. */
void matter_bridge_print(void);

#ifdef __cplusplus
}
#endif
