/* Firmware updates: see ota.h. */
#include "ota.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "ota_ver.h"

static const char *TAG = "ota";

#define GITHUB          "https://github.com/"
#define TASK_STACK      8192
#define TIMEOUT_MS      15000
/* The download redirects to a signed URL of several hundred bytes, which
 * must fit in the client's header and request buffers. */
#define HTTP_RX_BUF     4096
#define HTTP_TX_BUF     2048
#define VERIFY_TICK_MS  10000

static SemaphoreHandle_t s_mu;
static ota_status_t s_st;
static TaskHandle_t s_task;
static char s_url[512];
static bool s_check_only;
static ota_health_fn s_healthy;
static esp_timer_handle_t s_verify;

static void lock(void)   { xSemaphoreTake(s_mu, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_mu); }

const char *ota_state_str(ota_state_t s)
{
    switch (s) {
    case OTA_IDLE:        return "idle";
    case OTA_CHECKING:    return "checking";
    case OTA_UP_TO_DATE:  return "up to date";
    case OTA_AVAILABLE:   return "update available";
    case OTA_DOWNLOADING: return "downloading";
    case OTA_RESTARTING:  return "restarting";
    case OTA_FAILED:      return "failed";
    }
    return "?";
}

static void set_state(ota_state_t s, const char *err)
{
    lock();
    s_st.state = s;
    snprintf(s_st.error, sizeof s_st.error, "%s", err ? err : "");
    unlock();
}

static void fail(const char *why)
{
    ESP_LOGW(TAG, "%s", why);
    printf("HMBRIDGE-OTA result=FAIL why=\"%s\"\n", why);
    set_state(OTA_FAILED, why);
}

/* ---- the check: where does releases/latest redirect to? ------------------- */

static char s_location[512];

static esp_err_t check_event(esp_http_client_event_t *e)
{
    if (e->event_id == HTTP_EVENT_ON_HEADER && e->header_key && e->header_value &&
        strcasecmp(e->header_key, "Location") == 0) {
        snprintf(s_location, sizeof s_location, "%s", e->header_value);
    }
    return ESP_OK;
}

static void do_check(void)
{
    char url[160];
    snprintf(url, sizeof url, GITHUB "%s/releases/latest", CONFIG_HM_OTA_REPO);
    s_location[0] = '\0';
    esp_http_client_config_t hc = {
        .url = url,
        .method = HTTP_METHOD_HEAD,
        .timeout_ms = TIMEOUT_MS,
        .disable_auto_redirect = true,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = check_event,
        .buffer_size = HTTP_RX_BUF,
        .buffer_size_tx = HTTP_TX_BUF,
    };
    esp_http_client_handle_t c = esp_http_client_init(&hc);
    if (!c) {
        fail("Could not start the HTTP client");
        return;
    }
    esp_err_t err = esp_http_client_perform(c);
    int status = esp_http_client_get_status_code(c);
    esp_http_client_cleanup(c);
    if (err != ESP_OK) {
        char why[80];
        snprintf(why, sizeof why, "Could not reach GitHub (%s)", esp_err_to_name(err));
        fail(why);
        return;
    }
    char tag[32];
    if (status / 100 != 3 || !ota_tag_from_location(s_location, tag, sizeof tag)) {
        char why[80];
        if (status == 404) {
            snprintf(why, sizeof why, "Repository not found (is it public?)");
        } else {
            snprintf(why, sizeof why, "No release published yet (HTTP %d)", status);
        }
        fail(why);
        return;
    }
    lock();
    snprintf(s_st.latest, sizeof s_st.latest, "%s", tag);
    const bool newer = ota_ver_newer(tag, s_st.running);
    s_st.state = newer ? OTA_AVAILABLE : OTA_UP_TO_DATE;
    s_st.error[0] = '\0';
    unlock();
    ESP_LOGI(TAG, "latest release %s, running %s: %s", tag, s_st.running, newer ? "update available" : "up to date");
    printf("HMBRIDGE-OTA check latest=%s running=%s newer=%d\n", tag, s_st.running, newer);
}

/* ---- the download ----------------------------------------------------------- */

static void do_install(void)
{
    ESP_LOGI(TAG, "downloading %s", s_url);
    printf("HMBRIDGE-OTA start url=%s\n", s_url);
    lock();
    s_st.state = OTA_DOWNLOADING;
    s_st.percent = 0;
    s_st.error[0] = '\0';
    unlock();
    esp_http_client_config_t hc = {
        .url = s_url,
        .timeout_ms = TIMEOUT_MS,
        .keep_alive_enable = true,
        .buffer_size = HTTP_RX_BUF,
        .buffer_size_tx = HTTP_TX_BUF,
    };
    if (strncmp(s_url, "https:", 6) == 0) {
        hc.crt_bundle_attach = esp_crt_bundle_attach;
    }
    esp_https_ota_config_t oc = { .http_config = &hc };
    esp_https_ota_handle_t h = NULL;
    esp_err_t err = esp_https_ota_begin(&oc, &h);
    if (err != ESP_OK) {
        char why[80];
        snprintf(why, sizeof why, "Download did not start (%s)", esp_err_to_name(err));
        fail(why);
        return;
    }
    esp_app_desc_t nd;
    err = esp_https_ota_get_img_desc(h, &nd);
    const esp_app_desc_t *me = esp_app_get_description();
    if (err != ESP_OK) {
        esp_https_ota_abort(h);
        fail("The file is not a firmware image");
        return;
    }
    if (strncmp(nd.project_name, me->project_name, sizeof nd.project_name) != 0) {
        esp_https_ota_abort(h);
        char why[80];
        snprintf(why, sizeof why, "Not bridge firmware (it is '%.32s')", nd.project_name);
        fail(why);
        return;
    }
    ESP_LOGI(TAG, "new firmware %s (built %s %s)", nd.version, nd.date, nd.time);
    const int size = esp_https_ota_get_image_size(h);
    int last = -10;
    while ((err = esp_https_ota_perform(h)) == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
        const int got = esp_https_ota_get_image_len_read(h);
        const int pct = size > 0 ? (int)((int64_t)got * 100 / size) : -1;
        lock();
        s_st.percent = pct;
        unlock();
        if (pct >= last + 10) {
            printf("HMBRIDGE-OTA progress=%d bytes=%d\n", pct, got);
            last = pct;
        }
    }
    if (err != ESP_OK || !esp_https_ota_is_complete_data_received(h)) {
        esp_https_ota_abort(h);
        char why[80];
        snprintf(why, sizeof why, "Download failed (%s)", esp_err_to_name(err));
        fail(why);
        return;
    }
    err = esp_https_ota_finish(h); /* checks the image and sets it to boot */
    if (err != ESP_OK) {
        char why[80];
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
            snprintf(why, sizeof why, "The image is corrupt");
        } else {
            snprintf(why, sizeof why, "Install failed (%s)", esp_err_to_name(err));
        }
        fail(why);
        return;
    }
    printf("HMBRIDGE-OTA result=OK version=%s restarting\n", nd.version);
    lock();
    s_st.state = OTA_RESTARTING;
    s_st.percent = 100;
    snprintf(s_st.latest, sizeof s_st.latest, "%.31s", nd.version);
    unlock();
    vTaskDelay(pdMS_TO_TICKS(2000)); /* time for the screen to say so */
    esp_restart();
}

static void ota_task(void *arg)
{
    (void)arg;
    if (s_check_only) {
        do_check();
    } else {
        do_install();
    }
    lock();
    s_task = NULL;
    unlock();
    vTaskDelete(NULL);
}

static esp_err_t start(bool check_only)
{
    lock();
    const bool busy = s_task != NULL || s_st.state == OTA_RESTARTING;
    if (!busy) {
        s_check_only = check_only;
        s_st.state = check_only ? OTA_CHECKING : OTA_DOWNLOADING;
        s_st.percent = 0;
        s_st.error[0] = '\0';
    }
    unlock();
    if (busy) {
        return ESP_ERR_INVALID_STATE;
    }
    TaskHandle_t t = NULL;
    if (xTaskCreate(ota_task, "ota", TASK_STACK, NULL, 3, &t) != pdPASS) {
        set_state(OTA_FAILED, "Out of memory");
        return ESP_ERR_NO_MEM;
    }
    lock();
    if (s_st.state == OTA_CHECKING || s_st.state == OTA_DOWNLOADING) {
        s_task = t; /* unless it already finished */
    }
    unlock();
    return ESP_OK;
}

esp_err_t ota_check(void)
{
    return start(true);
}

esp_err_t ota_install(const char *url)
{
    if (url && url[0]) {
        if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) {
            return ESP_ERR_INVALID_ARG;
        }
        lock();
        const bool busy = s_task != NULL;
        if (!busy) {
            snprintf(s_url, sizeof s_url, "%s", url);
        }
        unlock();
        if (busy) {
            return ESP_ERR_INVALID_STATE;
        }
    } else {
        lock();
        const bool have = s_st.latest[0] && s_task == NULL;
        if (have) {
            snprintf(s_url, sizeof s_url, GITHUB "%s/releases/download/%s/%s", CONFIG_HM_OTA_REPO, s_st.latest,
                     CONFIG_HM_OTA_ASSET);
        }
        unlock();
        if (!have) {
            return ESP_ERR_INVALID_STATE; /* check first */
        }
    }
    return start(false);
}

void ota_status(ota_status_t *out)
{
    lock();
    *out = s_st;
    unlock();
}

/* ---- the trial of a new firmware ------------------------------------------- */

static void verify_tick(void *arg)
{
    (void)arg;
    const int64_t up_s = esp_timer_get_time() / 1000000;
    if (up_s >= CONFIG_HM_OTA_VERIFY_S && (!s_healthy || s_healthy())) {
        esp_timer_stop(s_verify);
        if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
            ESP_LOGI(TAG, "new firmware %s is healthy: kept", s_st.running);
            printf("HMBRIDGE-OTA kept version=%s\n", s_st.running);
            lock();
            s_st.on_trial = false;
            unlock();
        }
        return;
    }
    if (up_s >= CONFIG_HM_OTA_ROLLBACK_S) {
        esp_timer_stop(s_verify);
        ESP_LOGE(TAG, "new firmware %s never got healthy: going back to the previous one", s_st.running);
        printf("HMBRIDGE-OTA rollback version=%s\n", s_st.running);
        esp_ota_mark_app_invalid_rollback_and_reboot();
    }
}

esp_err_t ota_init(ota_health_fn healthy)
{
    s_mu = xSemaphoreCreateMutex();
    if (!s_mu) {
        return ESP_ERR_NO_MEM;
    }
    s_healthy = healthy;
    snprintf(s_st.running, sizeof s_st.running, "%.31s", esp_app_get_description()->version);
    s_st.percent = -1;
    const esp_partition_t *run = esp_ota_get_running_partition();
    esp_ota_img_states_t st;
    if (run && esp_ota_get_state_partition(run, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY) {
        s_st.on_trial = true;
        ESP_LOGW(TAG, "new firmware %s on trial: kept after %d s if the bus answers, else rolled back by %d s",
                 s_st.running, CONFIG_HM_OTA_VERIFY_S, CONFIG_HM_OTA_ROLLBACK_S);
        printf("HMBRIDGE-OTA trial version=%s\n", s_st.running);
        const esp_timer_create_args_t ta = { .callback = verify_tick, .name = "ota_verify" };
        if (esp_timer_create(&ta, &s_verify) == ESP_OK) {
            esp_timer_start_periodic(s_verify, VERIFY_TICK_MS * 1000ull);
        }
    }
    const esp_partition_t *bad = esp_ota_get_last_invalid_partition();
    esp_app_desc_t bd;
    if (bad && esp_ota_get_partition_description(bad, &bd) == ESP_OK) {
        s_st.rolled_back = true;
        snprintf(s_st.error, sizeof s_st.error, "Firmware %.32s did not start properly and was undone", bd.version);
        ESP_LOGW(TAG, "%s", s_st.error);
    }
    ESP_LOGI(TAG, "running %s from %s", s_st.running, run ? run->label : "?");
    return ESP_OK;
}
