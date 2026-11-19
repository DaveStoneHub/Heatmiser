#include "net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs.h"

static const char *TAG = "net";

#define RETRY_MIN_MS  2000
#define RETRY_MAX_MS  60000

static esp_netif_t *s_netif;
static esp_timer_handle_t s_retry;
static uint32_t s_retry_ms = RETRY_MIN_MS;
static volatile bool s_up, s_time_set, s_started;
static bool s_shared; /* Matter owns the Wi-Fi driver; we only watch it */
static volatile unsigned s_disconnects;
static char s_tz[64] = NET_TZ_DEFAULT;

static bool configured(char *ssid, size_t cap)
{
    wifi_config_t c;
    if (esp_wifi_get_config(WIFI_IF_STA, &c) != ESP_OK || !c.sta.ssid[0]) {
        return false;
    }
    if (ssid) {
        snprintf(ssid, cap, "%.32s", (const char *)c.sta.ssid);
    }
    return true;
}

static void retry_cb(void *arg)
{
    (void)arg;
    if (!s_shared && !s_up && configured(NULL, 0)) {
        esp_wifi_connect();
    }
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (!s_shared && configured(NULL, 0)) {
            esp_wifi_connect();
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *d = data;
        if (s_up) {
            s_disconnects++;
        }
        s_up = false;
        if (s_shared) {
            ESP_LOGW(TAG, "Wi-Fi disconnected (reason %d)", d ? d->reason : -1);
            return; /* Matter reconnects */
        }
        ESP_LOGW(TAG, "Wi-Fi disconnected (reason %d); retrying in %u s", d ? d->reason : -1,
                 (unsigned)(s_retry_ms / 1000));
        esp_timer_stop(s_retry);
        esp_timer_start_once(s_retry, (uint64_t)s_retry_ms * 1000);
        s_retry_ms = s_retry_ms * 2 > RETRY_MAX_MS ? RETRY_MAX_MS : s_retry_ms * 2;
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *e = data;
        s_up = true;
        s_retry_ms = RETRY_MIN_MS;
        ESP_LOGI(TAG, "Wi-Fi up, IP " IPSTR, IP2STR(&e->ip_info.ip));
    }
}

static void time_synced(struct timeval *tv)
{
    (void)tv;
    if (!s_time_set) {
        time_t now = time(NULL);
        struct tm tm;
        localtime_r(&now, &tm);
        char buf[40];
        strftime(buf, sizeof buf, "%a %d %b %Y %H:%M:%S %Z", &tm);
        ESP_LOGI(TAG, "clock set by SNTP: %s", buf);
    }
    s_time_set = true;
}

static void load_tz(void)
{
    nvs_handle_t h;
    if (nvs_open("ui", NVS_READONLY, &h) == ESP_OK) {
        size_t len = sizeof s_tz;
        if (nvs_get_str(h, "tz", s_tz, &len) != ESP_OK) {
            snprintf(s_tz, sizeof s_tz, "%s", NET_TZ_DEFAULT);
        }
        nvs_close(h);
    }
    setenv("TZ", s_tz, 1);
    tzset();
}

static void start_sntp(void)
{
    esp_sntp_config_t sc = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    sc.sync_cb = time_synced;
    ESP_ERROR_CHECK(esp_netif_sntp_init(&sc));
}

esp_err_t net_attach(void)
{
    load_tz();
    s_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    wifi_mode_t mode;
    if (!s_netif || esp_wifi_get_mode(&mode) != ESP_OK) {
        return ESP_ERR_INVALID_STATE; /* Matter has not set up Wi-Fi, or it failed */
    }
    s_shared = true;
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, on_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL));
    const esp_timer_create_args_t ta = { .callback = retry_cb, .name = "wifi_retry" };
    ESP_ERROR_CHECK(esp_timer_create(&ta, &s_retry));
    esp_netif_ip_info_t ip;
    if (esp_netif_get_ip_info(s_netif, &ip) == ESP_OK && ip.ip.addr) {
        s_up = true; /* joined before we started watching */
    }
    s_started = true;
    start_sntp();
    char ssid[33];
    if (configured(ssid, sizeof ssid)) {
        ESP_LOGI(TAG, "Matter is joining '%s'", ssid);
    } else {
        ESP_LOGI(TAG, "no Wi-Fi saved; pair with a Matter controller, or 'wifi join <ssid> <password>'");
    }
    return ESP_OK;
}

esp_err_t net_init(void)
{
    load_tz();
    ESP_ERROR_CHECK(esp_netif_init());
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    /* Matter may have made the station interface before its Wi-Fi start failed. */
    s_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!s_netif) {
        s_netif = esp_netif_create_default_wifi_sta();
    }
    esp_netif_set_hostname(s_netif, "heatmiser-bridge");
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL));
    const esp_timer_create_args_t ta = { .callback = retry_cb, .name = "wifi_retry" };
    ESP_ERROR_CHECK(esp_timer_create(&ta, &s_retry));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    s_started = true;
    start_sntp();

    char ssid[33];
    if (configured(ssid, sizeof ssid)) {
        ESP_LOGI(TAG, "joining '%s'", ssid);
    } else {
        ESP_LOGI(TAG, "no Wi-Fi saved; use 'wifi join <ssid> <password>'");
    }
    return ESP_OK;
}

esp_err_t net_join(const char *ssid, const char *password)
{
    if (!s_started || !ssid || !ssid[0] || strlen(ssid) > 32 || (password && strlen(password) > 63)) {
        return ESP_ERR_INVALID_ARG;
    }
    wifi_config_t c = { 0 };
    memcpy(c.sta.ssid, ssid, strlen(ssid));
    if (password) {
        memcpy(c.sta.password, password, strlen(password));
    }
    c.sta.threshold.authmode = password && password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    c.sta.pmf_cfg.capable = true;
    esp_timer_stop(s_retry);
    s_retry_ms = RETRY_MIN_MS;
    esp_wifi_disconnect();
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &c);
    if (err == ESP_OK) {
        err = esp_wifi_connect();
    }
    return err;
}

esp_err_t net_forget(void)
{
    esp_timer_stop(s_retry);
    esp_wifi_disconnect();
    wifi_config_t c = { 0 };
    return esp_wifi_set_config(WIFI_IF_STA, &c);
}

void net_status(net_status_t *o)
{
    memset(o, 0, sizeof *o);
    if (!s_started) {
        return;
    }
    o->configured = configured(o->ssid, sizeof o->ssid);
    o->up = s_up;
    o->disconnects = s_disconnects;
    if (s_up) {
        esp_netif_ip_info_t ip;
        if (esp_netif_get_ip_info(s_netif, &ip) == ESP_OK) {
            snprintf(o->ip, sizeof o->ip, IPSTR, IP2STR(&ip.ip));
            snprintf(o->gw, sizeof o->gw, IPSTR, IP2STR(&ip.gw));
        }
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            o->rssi = ap.rssi;
        }
    }
}

bool net_time_set(void)
{
    return s_time_set;
}

esp_err_t net_set_tz(const char *tz)
{
    if (!tz || !tz[0] || strlen(tz) >= sizeof s_tz) {
        return ESP_ERR_INVALID_ARG;
    }
    snprintf(s_tz, sizeof s_tz, "%s", tz);
    setenv("TZ", s_tz, 1);
    tzset();
    nvs_handle_t h;
    esp_err_t err = nvs_open("ui", NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_str(h, "tz", s_tz);
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    return err;
}

const char *net_tz(void)
{
    return s_tz;
}
