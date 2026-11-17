#include "hm_bus.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "hm_bus";

#define RX_RING_SIZE   1024
#define TX_RING_SIZE   0      /* write blocks until bytes are in the FIFO; fine for <=255 */
#define EVT_QUEUE_LEN  32
#define RX_FULL_THRESH 16     /* bytes in FIFO before the ISR empties it */
#define RX_TOUT_SYMBOLS 2     /* idle symbol times before the ISR empties it */
#define TX_DONE_WAIT_MS 1500  /* a 255-byte frame is about 530 ms at 4800 */
#define FRAME_HARD_CAP_MS 2500

static hm_bus_config_t s_cfg;
static SemaphoreHandle_t s_lock;
static QueueHandle_t s_evt;
static hm_bus_counters_t s_cnt;
static bool s_ready;
static uint32_t s_byte_us;

static volatile int64_t s_edge_us;
static volatile bool s_edge_armed;

static void IRAM_ATTR rx_edge_isr(void *arg)
{
    (void)arg;
    if (s_edge_armed) {
        s_edge_us = esp_timer_get_time();
        s_edge_armed = false;
    }
}

static void edge_arm(void)
{
    if (!s_cfg.edge_timing) {
        return;
    }
    s_edge_us = 0;
    s_edge_armed = true;
    gpio_intr_enable(s_cfg.rx_gpio);
}

static int64_t edge_disarm(void)
{
    if (!s_cfg.edge_timing) {
        return 0;
    }
    gpio_intr_disable(s_cfg.rx_gpio);
    s_edge_armed = false;
    return s_edge_us;
}

/* Drain driver events and count line errors. Data events are ignored. */
static uint32_t drain_events(void)
{
    uint32_t errors = 0;
    uart_event_t ev;
    while (xQueueReceive(s_evt, &ev, 0) == pdTRUE) {
        switch (ev.type) {
        case UART_FRAME_ERR:
        case UART_PARITY_ERR:
        case UART_FIFO_OVF:
        case UART_BUFFER_FULL:
        case UART_BREAK:
            errors++;
            break;
        default:
            break;
        }
    }
    return errors;
}

esp_err_t hm_bus_init(const hm_bus_config_t *cfg)
{
    if (s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    s_cfg = *cfg;
    s_byte_us = (uint32_t)(10u * 1000000u / (uint32_t)s_cfg.baud);
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        return ESP_ERR_NO_MEM;
    }

    const uart_config_t uc = {
        .baud_rate = s_cfg.baud,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t err = uart_driver_install(s_cfg.uart_num, RX_RING_SIZE, TX_RING_SIZE,
                                        EVT_QUEUE_LEN, &s_evt, 0);
    if (err == ESP_OK) err = uart_param_config(s_cfg.uart_num, &uc);
    if (err == ESP_OK) err = uart_set_pin(s_cfg.uart_num, s_cfg.tx_gpio, s_cfg.rx_gpio,
                                          UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    /* The Waveshare transceiver switches direction itself, so plain UART mode:
     * there is no DE/RTS pin to drive. */
    if (err == ESP_OK) err = uart_set_mode(s_cfg.uart_num, UART_MODE_UART);
    if (err == ESP_OK) err = uart_set_rx_full_threshold(s_cfg.uart_num, RX_FULL_THRESH);
    if (err == ESP_OK) err = uart_set_rx_timeout(s_cfg.uart_num, RX_TOUT_SYMBOLS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART setup failed: %s", esp_err_to_name(err));
        return err;
    }
    /* Keep RX high if the transceiver output ever floats. */
    gpio_set_pull_mode(s_cfg.rx_gpio, GPIO_PULLUP_ONLY);

    if (s_cfg.edge_timing) {
        err = gpio_install_isr_service(0);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "GPIO ISR service: %s; edge timing off", esp_err_to_name(err));
            s_cfg.edge_timing = false;
        } else {
            gpio_set_intr_type(s_cfg.rx_gpio, GPIO_INTR_NEGEDGE);
            gpio_intr_disable(s_cfg.rx_gpio);
            if (gpio_isr_handler_add(s_cfg.rx_gpio, rx_edge_isr, NULL) != ESP_OK) {
                ESP_LOGW(TAG, "edge ISR not added; edge timing off");
                s_cfg.edge_timing = false;
            }
        }
    }

    uart_flush_input(s_cfg.uart_num);
    drain_events();
    s_ready = true;
    ESP_LOGI(TAG, "UART%d TX=GPIO%d RX=GPIO%d %d 8N1, master 0x%02X, timeout %u ms, gap %u ms, edge timing %s",
             s_cfg.uart_num, s_cfg.tx_gpio, s_cfg.rx_gpio, s_cfg.baud, s_cfg.master_addr,
             (unsigned)s_cfg.reply_timeout_ms, (unsigned)s_cfg.gap_ms,
             s_cfg.edge_timing ? "on" : "off");
    return ESP_OK;
}

const hm_bus_config_t *hm_bus_config(void)
{
    return &s_cfg;
}

void hm_bus_set_master(uint8_t addr)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_cfg.master_addr = addr;
    xSemaphoreGive(s_lock);
}

void hm_bus_set_timing(uint32_t reply_timeout_ms, uint32_t gap_ms)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (reply_timeout_ms) s_cfg.reply_timeout_ms = reply_timeout_ms;
    if (gap_ms) s_cfg.gap_ms = gap_ms;
    xSemaphoreGive(s_lock);
}

static TickType_t ticks_until(int64_t deadline_us)
{
    int64_t left = deadline_us - esp_timer_get_time();
    if (left <= 0) {
        return 0;
    }
    TickType_t t = pdMS_TO_TICKS((left + 999) / 1000);
    return t ? t : 1;
}

/* Read up to `want` more bytes into t->rx, waiting at most until deadline.
 * Returns bytes read. Updates first/last arrival times. */
static size_t rx_some(hm_txn_t *t, size_t want, TickType_t wait)
{
    if (t->rx_len + want > sizeof(t->rx)) {
        want = sizeof(t->rx) - t->rx_len;
    }
    if (want == 0) {
        return 0;
    }
    int n = uart_read_bytes(s_cfg.uart_num, &t->rx[t->rx_len], want, wait);
    if (n <= 0) {
        return 0;
    }
    int64_t now = esp_timer_get_time();
    if (t->rx_len == 0 && t->t_rx_first_us == 0) {
        /* The driver hands bytes over in chunks; back-date to the first. */
        t->t_rx_first_us = now - (int64_t)(n - 1) * s_byte_us;
    }
    t->t_rx_last_us = now;
    t->rx_len += (size_t)n;
    return (size_t)n;
}

/* Keep reading until `silence` passes with no bytes, the buffer fills, or the
 * hard cap. Used to drain garbage so the next frame starts clean. */
static void rx_until_quiet(hm_txn_t *t, uint32_t silence_ms, int64_t cap_us)
{
    while (esp_timer_get_time() < cap_us && t->rx_len < sizeof(t->rx)) {
        if (rx_some(t, sizeof(t->rx) - t->rx_len, pdMS_TO_TICKS(silence_ms)) == 0) {
            break;
        }
    }
}

/* Remove a looped-back copy of our own request from the start of rx. */
static void strip_echo(hm_txn_t *t)
{
    if (t->rx_len >= t->req_len && memcmp(t->rx, t->req, t->req_len) == 0) {
        memmove(t->rx, &t->rx[t->req_len], t->rx_len - t->req_len);
        t->rx_len -= t->req_len;
        t->echo_seen = true;
        t->t_rx_first_us = 0;
    }
}

static esp_err_t transact_locked(uint8_t expect_stat, hm_txn_t *t)
{
    const int port = s_cfg.uart_num;

    uart_flush_input(port);
    drain_events();

    t->t_tx_start_us = esp_timer_get_time();
    int w = uart_write_bytes(port, t->req, t->req_len);
    if (w != (int)t->req_len || uart_wait_tx_done(port, pdMS_TO_TICKS(TX_DONE_WAIT_MS)) != ESP_OK) {
        t->result = HM_TXN_ERR;
        return ESP_FAIL;
    }
    t->t_tx_done_us = esp_timer_get_time();
    edge_arm();

    const int64_t first_deadline = t->t_tx_done_us + (int64_t)s_cfg.reply_timeout_ms * 1000;
    const int64_t hard_cap = t->t_tx_done_us + (int64_t)FRAME_HARD_CAP_MS * 1000;
    const TickType_t gap_ticks = pdMS_TO_TICKS(s_cfg.interbyte_timeout_ms);

    /* Phase 1: the first byte, within the reply timeout. */
    rx_some(t, 1, ticks_until(first_deadline));

    /* An auto-direction transceiver may hand our own request back. Reply
     * byte 0 is the master address (>= 0x81), request byte 0 the stat
     * address (<= 32), so a leading stat address means echo. */
    if (t->rx_len && t->rx[0] == t->req[0] && t->req[0] != s_cfg.master_addr) {
        while (t->rx_len < t->req_len && rx_some(t, t->req_len - t->rx_len, gap_ticks)) {
        }
        strip_echo(t);
        /* Echo bytes arrive while we transmit, before the edge ISR is armed,
         * so the edge it catches is still the reply's start bit. */
        if (t->echo_seen && t->rx_len == 0) {
            rx_some(t, 1, ticks_until(first_deadline));
        }
    }

    if (t->rx_len == 0) {
        edge_disarm();
        t->result = t->echo_seen ? HM_TXN_ECHO_ONLY : HM_TXN_TIMEOUT;
        t->uart_errors = drain_events();
        return ESP_OK;
    }

    /* Phase 2: the 3-byte header gives the frame length. */
    while (t->rx_len < 3 && rx_some(t, 3 - t->rx_len, gap_ticks)) {
    }
    size_t flen = hm_reply_frame_len(t->rx, t->rx_len);
    if (t->rx_len >= 3 && flen >= HM_WRITE_REPLY_LEN && flen <= HM_MAX_FRAME &&
        t->rx[0] == s_cfg.master_addr) {
        /* Phase 3: the rest, ending on inter-byte silence. */
        while (t->rx_len < flen && esp_timer_get_time() < hard_cap) {
            if (rx_some(t, flen - t->rx_len, gap_ticks) == 0) {
                break;
            }
        }
        /* Anything trailing right behind the frame is a framing problem; catch it. */
        rx_some(t, sizeof(t->rx) - t->rx_len, pdMS_TO_TICKS(5));
    } else {
        rx_until_quiet(t, s_cfg.interbyte_timeout_ms, hard_cap);
    }

    int64_t edge = edge_disarm();
    if (edge) {
        t->t_rx_edge_us = edge;
        t->turnaround_us = (int32_t)(edge - t->t_tx_done_us);
        t->turnaround_from_edge = true;
    } else if (t->t_rx_first_us) {
        t->turnaround_us = (int32_t)(t->t_rx_first_us - t->t_tx_done_us - s_byte_us);
        t->turnaround_from_edge = false;
    }

    t->proto = hm_parse_reply(t->rx, t->rx_len, s_cfg.master_addr, expect_stat, &t->reply);
    if (t->proto == HM_OK) {
        t->result = HM_TXN_OK;
    } else if (t->proto == HM_ERR_SHORT) {
        t->result = HM_TXN_PARTIAL;
    } else {
        t->result = HM_TXN_BAD_FRAME;
    }
    t->uart_errors = drain_events();
    return ESP_OK;
}

esp_err_t hm_bus_transact(const uint8_t *req, size_t req_len, uint8_t expect_stat, hm_txn_t *t)
{
    if (!s_ready || !req || !t || req_len == 0 || req_len > HM_MAX_FRAME) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(t, 0, sizeof(*t));
    t->turnaround_us = -1;
    memcpy(t->req, req, req_len);
    t->req_len = req_len;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = transact_locked(expect_stat, t);

    s_cnt.txns++;
    s_cnt.uart_errors += t->uart_errors;
    switch (t->result) {
    case HM_TXN_OK:        s_cnt.ok++; break;
    case HM_TXN_TIMEOUT:   s_cnt.timeout++; break;
    case HM_TXN_PARTIAL:   s_cnt.partial++; break;
    case HM_TXN_ECHO_ONLY: s_cnt.echo_only++; break;
    case HM_TXN_BAD_FRAME:
        if (t->proto == HM_ERR_CRC) s_cnt.crc++; else s_cnt.bad_other++;
        break;
    default:               s_cnt.bad_other++; break;
    }

    /* Bus recovery gap. After a bad frame the stats need >= 20 ms idle to
     * reset their receivers; the 100 ms gap covers that. */
    vTaskDelay(pdMS_TO_TICKS(s_cfg.gap_ms));
    if (t->result != HM_TXN_OK) {
        uart_flush_input(s_cfg.uart_num);
    }
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t hm_bus_read(uint8_t stat, uint16_t start, uint16_t count, hm_txn_t *t)
{
    uint8_t req[HM_READ_REQ_LEN];
    if (stat < HM_STAT_ADDR_MIN || stat > HM_STAT_ADDR_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    if (hm_build_read(req, sizeof req, stat, s_cfg.master_addr, start, count) != sizeof req) {
        return ESP_ERR_INVALID_ARG;
    }
    return hm_bus_transact(req, sizeof req, stat, t);
}

size_t hm_bus_sniff(uint8_t *buf, size_t cap, uint32_t timeout_ms, uint32_t silence_ms,
                    int64_t *t_first_us, uint32_t *uart_errors)
{
    if (!s_ready || !buf || cap == 0) {
        return 0;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    size_t n = 0;
    if (t_first_us) *t_first_us = 0;
    int got = uart_read_bytes(s_cfg.uart_num, buf, 1, ticks_until(deadline));
    if (got > 0) {
        if (t_first_us) *t_first_us = esp_timer_get_time();
        n = 1;
        while (n < cap) {
            got = uart_read_bytes(s_cfg.uart_num, &buf[n], cap - n, pdMS_TO_TICKS(silence_ms));
            if (got <= 0) {
                break;
            }
            n += (size_t)got;
        }
    }
    uint32_t e = drain_events();
    if (uart_errors) *uart_errors = e;
    xSemaphoreGive(s_lock);
    return n;
}

void hm_bus_counters(hm_bus_counters_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_cnt;
    xSemaphoreGive(s_lock);
}

void hm_bus_counters_reset(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    memset(&s_cnt, 0, sizeof(s_cnt));
    xSemaphoreGive(s_lock);
}

const char *hm_txn_result_str(hm_txn_result_t r)
{
    switch (r) {
    case HM_TXN_OK:        return "ok";
    case HM_TXN_TIMEOUT:   return "timeout (no reply)";
    case HM_TXN_PARTIAL:   return "partial reply";
    case HM_TXN_BAD_FRAME: return "bad frame";
    case HM_TXN_ECHO_ONLY: return "echo only, no reply";
    case HM_TXN_ERR:       return "driver error";
    }
    return "?";
}
