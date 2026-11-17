/*
 * Soak test for the milestone gate: read one stat's full DCB repeatedly and
 * count every failure. Runs in its own task so the console stays usable.
 *
 * Output lines are prefixed for tools/soak_logger.py:
 *   SOAK-START ...           parameters
 *   SOAK-ERR ...             one line per failed read, with raw bytes
 *   SOAK ...                 progress, once a minute
 *   SOAK-RESULT PASS|FAIL .. final verdict
 */
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "bench.h"
#include "esp_console.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hm_dcb.h"

typedef struct {
    int addr;
    int seconds;
    int period_ms;
} soak_args_t;

typedef struct {
    uint32_t reads, ok, crc, timeout, partial, bad, echo_only, uart_err, dcb_len_changes;
    int32_t ta_min, ta_max;
    int64_t ta_sum;
    uint32_t ta_n;
    int64_t txn_max_us;
    uint16_t first_len;
    uint16_t last_air_t10;
    int64_t t0;
} soak_stats_t;

static TaskHandle_t s_task;
static volatile bool s_stop;
static soak_args_t s_args;
static soak_stats_t s_st;

static uint32_t failures(const soak_stats_t *s)
{
    return s->crc + s->timeout + s->partial + s->bad + s->echo_only + s->uart_err;
}

static void print_progress(const char *tag)
{
    const soak_stats_t *s = &s_st;
    double el = (esp_timer_get_time() - s->t0) / 1e6;
    printf("%s t=%.0fs reads=%" PRIu32 " ok=%" PRIu32 " crc=%" PRIu32 " timeout=%" PRIu32
           " partial=%" PRIu32 " bad=%" PRIu32 " uart_err=%" PRIu32 " ta_ms=%.1f/%.1f/%.1f"
           " txn_max_ms=%.0f air=",
           tag, el, s->reads, s->ok, s->crc, s->timeout, s->partial, s->bad + s->echo_only,
           s->uart_err, s->ta_n ? s->ta_min / 1000.0 : 0.0,
           s->ta_n ? (double)s->ta_sum / s->ta_n / 1000.0 : 0.0,
           s->ta_n ? s->ta_max / 1000.0 : 0.0, s->txn_max_us / 1000.0);
    if (s->last_air_t10 == HM_TEMP_ABSENT || s->ok == 0) {
        printf("n/a\n");
    } else {
        printf("%u.%uC\n", s->last_air_t10 / 10, s->last_air_t10 % 10);
    }
}

static void soak_task(void *arg)
{
    (void)arg;
    static hm_txn_t t;
    soak_stats_t *s = &s_st;
    memset(s, 0, sizeof(*s));
    s->ta_min = INT32_MAX;
    s->t0 = esp_timer_get_time();
    const int64_t end = s->t0 + (int64_t)s_args.seconds * 1000000;
    int64_t next_report = s->t0 + 60 * 1000000LL;

    printf("SOAK-START addr=%d seconds=%d period_ms=%d master=0x%02X timeout_ms=%" PRIu32
           " gap_ms=%" PRIu32 "\n",
           s_args.addr, s_args.seconds, s_args.period_ms, hm_bus_config()->master_addr,
           hm_bus_config()->reply_timeout_ms, hm_bus_config()->gap_ms);

    TickType_t wake = xTaskGetTickCount();
    while (!s_stop && esp_timer_get_time() < end) {
        hm_bus_read((uint8_t)s_args.addr, 0, HM_READ_ALL, &t);
        s->reads++;
        s->uart_err += t.uart_errors;
        bool ok = false;
        switch (t.result) {
        case HM_TXN_OK: {
            hm_dcb_t d;
            if (hm_dcb_decode(t.reply.data, t.reply.count, &d) == 0) {
                ok = true;
                s->ok++;
                s->last_air_t10 = d.air_t10;
                if (s->first_len == 0) {
                    s->first_len = d.dcb_len;
                } else if (d.dcb_len != s->first_len) {
                    s->dcb_len_changes++;
                }
            } else {
                s->bad++;
            }
            break;
        }
        case HM_TXN_TIMEOUT:   s->timeout++; break;
        case HM_TXN_PARTIAL:   s->partial++; break;
        case HM_TXN_ECHO_ONLY: s->echo_only++; break;
        case HM_TXN_BAD_FRAME:
            if (t.proto == HM_ERR_CRC) s->crc++; else s->bad++;
            break;
        default:               s->bad++; break;
        }
        if (ok && t.turnaround_us >= 0) {
            s->ta_n++;
            s->ta_sum += t.turnaround_us;
            if (t.turnaround_us < s->ta_min) s->ta_min = t.turnaround_us;
            if (t.turnaround_us > s->ta_max) s->ta_max = t.turnaround_us;
            int64_t txn = t.t_rx_last_us - t.t_tx_start_us;
            if (txn > s->txn_max_us) s->txn_max_us = txn;
        }
        if (!ok || t.uart_errors) {
            printf("SOAK-ERR t=%.1fs read=%" PRIu32 " result=\"%s\" proto=\"%s\" rx_len=%u uart_err=%" PRIu32
                   " rx=",
                   (esp_timer_get_time() - s->t0) / 1e6, s->reads, hm_txn_result_str(t.result),
                   hm_status_str(t.proto), (unsigned)t.rx_len, t.uart_errors);
            for (size_t i = 0; i < t.rx_len; i++) printf("%02X", t.rx[i]);
            printf("\n");
        }
        if (esp_timer_get_time() >= next_report) {
            print_progress("SOAK");
            next_report += 60 * 1000000LL;
        }
        if (s_args.period_ms > 0) {
            xTaskDelayUntil(&wake, pdMS_TO_TICKS(s_args.period_ms));
            /* If a read overran the period, don't burst to catch up. */
            if ((int32_t)(xTaskGetTickCount() - wake) > 0) wake = xTaskGetTickCount();
        }
    }

    print_progress("SOAK");
    bool complete = !s_stop;
    bool pass = complete && s->reads > 0 && failures(s) == 0;
    printf("SOAK-RESULT %s%s duration=%.0fs reads=%" PRIu32 " ok=%" PRIu32 " crc=%" PRIu32
           " timeout=%" PRIu32 " partial=%" PRIu32 " bad=%" PRIu32 " uart_err=%" PRIu32
           " dcb_len=%u dcb_len_changes=%" PRIu32 " ta_ms_min=%.2f ta_ms_avg=%.2f ta_ms_max=%.2f\n",
           pass ? "PASS" : "FAIL", complete ? "" : " (stopped early)",
           (esp_timer_get_time() - s->t0) / 1e6, s->reads, s->ok, s->crc, s->timeout, s->partial,
           s->bad + s->echo_only, s->uart_err, s->first_len, s->dcb_len_changes,
           s->ta_n ? s->ta_min / 1000.0 : 0.0, s->ta_n ? (double)s->ta_sum / s->ta_n / 1000.0 : 0.0,
           s->ta_n ? s->ta_max / 1000.0 : 0.0);
    s_task = NULL;
    vTaskDelete(NULL);
}

static int cmd_soak(int argc, char **argv)
{
    const char *sub = argc > 1 ? argv[1] : "status";
    if (strcmp(sub, "start") == 0) {
        if (s_task) {
            printf("A soak is already running; 'soak stop' first.\n");
            return 1;
        }
        if (bench_parse_int(argc > 2 ? argv[2] : NULL, CONFIG_HM_BUS_DEFAULT_STAT, 1, 32, &s_args.addr) ||
            bench_parse_int(argc > 3 ? argv[3] : NULL, 3600, 10, 7 * 86400, &s_args.seconds) ||
            bench_parse_int(argc > 4 ? argv[4] : NULL, 1000, 0, 60000, &s_args.period_ms)) {
            return 1;
        }
        s_stop = false;
        if (xTaskCreate(soak_task, "soak", 6144, NULL, 5, &s_task) != pdPASS) {
            printf("could not start soak task\n");
            return 1;
        }
        return 0;
    }
    if (strcmp(sub, "stop") == 0) {
        if (!s_task) {
            printf("No soak running.\n");
            return 0;
        }
        s_stop = true;
        printf("Stopping after the current read...\n");
        return 0;
    }
    if (strcmp(sub, "status") == 0) {
        if (s_task) {
            print_progress("SOAK-STATUS");
        } else {
            printf("No soak running.\n");
        }
        return 0;
    }
    printf("usage: soak start [addr] [seconds] [period_ms] | soak stop | soak status\n");
    return 1;
}

void soak_register_command(void)
{
    const esp_console_cmd_t cmd = {
        .command = "soak",
        .help = "Gate test: read the full DCB repeatedly and count errors. "
                "Defaults: default stat, 3600 s, one read every 1000 ms (0 = back to back)",
        .hint = "start [addr] [seconds] [period_ms] | stop | status",
        .func = cmd_soak,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
