/*
 * Wi-Fi station and the clock: joins the saved network at boot, reconnects
 * with back-off, syncs time over SNTP and applies the saved timezone.
 * With Matter, the Matter stack owns the Wi-Fi driver and joins the network
 * the controller gives it at pairing (or one set with 'wifi join'); net then
 * only watches it, and runs the clock.
 */
#pragma once
#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* UK by default: GMT, BST from the last Sunday in March to the last in October. */
#define NET_TZ_DEFAULT "GMT0BST,M3.5.0/1,M10.5.0"

typedef struct {
    bool configured;     /* an SSID is saved */
    bool up;             /* associated and has an IP */
    char ssid[33];
    char ip[16];
    char gw[16];
    int  rssi;
    unsigned disconnects;
} net_status_t;

esp_err_t net_init(void);  /* needs nvs_flash_init() first */
/* Instead of net_init when Matter has started Wi-Fi. */
esp_err_t net_attach(void);
esp_err_t net_join(const char *ssid, const char *password);
esp_err_t net_forget(void);
void net_status(net_status_t *out);

bool net_time_set(void);
esp_err_t net_set_tz(const char *posix_tz);
const char *net_tz(void);

#ifdef __cplusplus
}
#endif
