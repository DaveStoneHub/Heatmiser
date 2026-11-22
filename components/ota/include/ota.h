/*
 * Firmware updates over Wi-Fi, with rollback.
 *
 * - Check: ask GitHub which release is the latest (CONFIG_HM_OTA_REPO) and
 *   compare its tag with the running version.
 * - Install: download that release's firmware file (CONFIG_HM_OTA_ASSET)
 *   over HTTPS, or any http:// or https:// URL given (a build served from a
 *   PC on the LAN, for the bench), into the other app slot, then restart.
 * - After the restart the new firmware is on trial. It is kept once it has
 *   run for CONFIG_HM_OTA_VERIFY_S seconds and the health check passes;
 *   otherwise, or if it crashes or hangs first, the bootloader goes back to
 *   the previous one.
 *
 * Check and install run in a task of their own; ota_status() says how they
 * are going. Console lines start "HMBRIDGE-OTA" for the PC logger.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OTA_IDLE = 0,
    OTA_CHECKING,
    OTA_UP_TO_DATE,
    OTA_AVAILABLE,       /* latest is newer than running */
    OTA_DOWNLOADING,
    OTA_RESTARTING,
    OTA_FAILED,          /* error says why */
} ota_state_t;

typedef struct {
    ota_state_t state;
    char running[32];    /* this firmware's version */
    char latest[32];     /* the latest release's tag, once checked */
    int  percent;        /* while downloading, -1 if the size is not known */
    char error[80];
    bool on_trial;       /* this firmware is new and not yet kept */
    bool rolled_back;    /* the last update did not start properly and was undone */
} ota_status_t;

/* Health of a new firmware on trial: true once it works (the bus answers). */
typedef bool (*ota_health_fn)(void);

/* At boot. Starts the trial of a new firmware if there is one. */
esp_err_t ota_init(ota_health_fn healthy);

/* Look for a newer release. ESP_ERR_INVALID_STATE if busy. */
esp_err_t ota_check(void);

/* Download and install, then restart. url NULL: the latest release (after
 * a check that found one). */
esp_err_t ota_install(const char *url);

void ota_status(ota_status_t *out);
const char *ota_state_str(ota_state_t s);

#ifdef __cplusplus
}
#endif
