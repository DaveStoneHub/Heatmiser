/* Bench console commands: read, scan, turnaround, sniff, stats, master, timing. */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bench.h"
#include "esp_console.h"
#include "esp_timer.h"
#include "hm_crc.h"
#include "hm_dcb.h"

int bench_parse_int(const char *s, int dflt, int lo, int hi, int *out)
{
    if (!s) {
        *out = dflt;
        return 0;
    }
    char *end = NULL;
    long v = strtol(s, &end, 0); /* accepts 0x81 too */
    if (!end || *end || v < lo || v > hi) {
        printf("bad value '%s' (expected %d..%d)\n", s, lo, hi);
        return -1;
    }
    *out = (int)v;
    return 0;
}

void bench_print_hex(const char *label, const uint8_t *b, size_t n)
{
    printf("%s (%u bytes):", label, (unsigned)n);
    for (size_t i = 0; i < n; i++) {
        if (i % 24 == 0) printf("\n   ");
        printf(" %02X", b[i]);
    }
    printf("\n");
}

static void print_c_array(const uint8_t *b, size_t n)
{
    printf("  C array for test/host captured frames:\n  {");
    for (size_t i = 0; i < n; i++) {
        if (i % 12 == 0) printf("\n    ");
        printf("0x%02X,%s", b[i], (i + 1) % 12 ? " " : "");
    }
    printf("\n  }\n");
}

void bench_print_txn(const hm_txn_t *t)
{
    printf("Result: %s", hm_txn_result_str(t->result));
    if (t->result == HM_TXN_BAD_FRAME || t->result == HM_TXN_PARTIAL) {
        printf(" (%s)", hm_status_str(t->proto));
    }
    printf("%s", t->echo_seen ? ", transceiver echoes our TX" : "");
    if (t->uart_errors) {
        printf(", %" PRIu32 " UART line errors (framing/parity: check A/B polarity)", t->uart_errors);
    }
    printf("\n");
    bench_print_hex("  Request", t->req, t->req_len);
    if (t->rx_len) {
        bench_print_hex("  Reply", t->rx, t->rx_len);
    }
    if (t->turnaround_us >= 0) {
        printf("  Turnaround %" PRId32 ".%03" PRId32 " ms (request end to reply start, %s)\n",
               t->turnaround_us / 1000, t->turnaround_us % 1000,
               t->turnaround_from_edge ? "start-bit edge" : "byte arrival, about +-2 ms");
    }
    if (t->t_rx_first_us && t->t_rx_last_us) {
        printf("  Request on wire %.1f ms, reply on wire about %.1f ms, whole transaction %.1f ms\n",
               (t->t_tx_done_us - t->t_tx_start_us) / 1000.0,
               (t->t_rx_last_us - t->t_rx_first_us) / 1000.0 + 2.1,
               (t->t_rx_last_us - t->t_tx_start_us) / 1000.0);
    }
}

/* ---- read ---------------------------------------------------------------- */

static int cmd_read(int argc, char **argv)
{
    int addr, start = 0, count = HM_READ_ALL;
    if (bench_parse_int(argc > 1 ? argv[1] : NULL, CONFIG_HM_BUS_DEFAULT_STAT, 1, 32, &addr) ||
        bench_parse_int(argc > 2 ? argv[2] : NULL, 0, 0, 0xFFFF, &start) ||
        bench_parse_int(argc > 3 ? argv[3] : NULL, HM_READ_ALL, 1, 0xFFFF, &count)) {
        return 1;
    }
    static hm_txn_t t; /* big; keep it off the console task stack */
    if (hm_bus_read((uint8_t)addr, (uint16_t)start, (uint16_t)count, &t) != ESP_OK) {
        printf("bus error\n");
        return 1;
    }
    bench_print_txn(&t);
    if (t.result != HM_TXN_OK) {
        if (t.result == HM_TXN_TIMEOUT) {
            printf("No reply. Check: stat address (shown in the stat's setup menu), A/B swapped,\n"
                   "no other master on the bus, stat powered. Try 'scan'.\n");
        }
        return 1;
    }
    printf("  Reply from stat %u, start %u, %u data bytes\n", t.reply.src, t.reply.start,
           t.reply.count);
    if (start == 0 && count == HM_READ_ALL) {
        hm_dcb_t d;
        if (hm_dcb_decode(t.reply.data, t.reply.count, &d) == 0) {
            printf("DCB:\n");
            hm_dcb_print(stdout, &d);
        } else {
            printf("DCB too short to decode\n");
        }
        printf("Raw DCB:\n");
        hm_hexdump(stdout, t.reply.data, t.reply.count);
    } else {
        hm_hexdump(stdout, t.reply.data, t.reply.count);
    }
    print_c_array(t.rx, t.rx_len);
    return 0;
}

/* ---- scan ---------------------------------------------------------------- */

static int cmd_scan(int argc, char **argv)
{
    int timeout;
    if (bench_parse_int(argc > 1 ? argv[1] : NULL, 300, 50, 2000, &timeout)) {
        return 1;
    }
    const hm_bus_config_t *cfg = hm_bus_config();
    uint32_t saved = cfg->reply_timeout_ms;
    hm_bus_set_timing((uint32_t)timeout, 0);
    printf("Scanning 1..32 (short read of addresses 0-4, %d ms timeout)...\n", timeout);
    static hm_txn_t t;
    int found = 0, noisy = 0;
    for (int a = 1; a <= 32; a++) {
        hm_bus_read((uint8_t)a, 0, 5, &t);
        if (t.result == HM_TXN_OK && t.reply.count >= 5) {
            const uint8_t *d = t.reply.data;
            printf("  %2d: %s, DCB %u bytes, version %u, turnaround %.1f ms\n", a,
                   hm_model_str(d[4]), (d[0] << 8) | d[1], d[3] & 0x7F,
                   t.turnaround_us / 1000.0);
            found++;
        } else if (t.result != HM_TXN_TIMEOUT) {
            printf("  %2d: %s (%s), %u bytes, %" PRIu32 " UART errors\n", a,
                   hm_txn_result_str(t.result), hm_status_str(t.proto), (unsigned)t.rx_len,
                   t.uart_errors);
            noisy++;
        }
    }
    hm_bus_set_timing(saved, 0);
    printf("Found %d stat(s)%s.\n", found,
           noisy ? "; bytes seen at other addresses suggest noise, polarity or another master" : "");
    return 0;
}

/* ---- turnaround ---------------------------------------------------------- */

static int cmd_turnaround(int argc, char **argv)
{
    int addr, n;
    if (bench_parse_int(argc > 1 ? argv[1] : NULL, CONFIG_HM_BUS_DEFAULT_STAT, 1, 32, &addr) ||
        bench_parse_int(argc > 2 ? argv[2] : NULL, 50, 1, 10000, &n)) {
        return 1;
    }
    static hm_txn_t t;
    int32_t mn_full = INT32_MAX, mx_full = 0, mn_short = INT32_MAX, mx_short = 0;
    int64_t sum_full = 0, sum_short = 0, sum_txn = 0;
    int ok_full = 0, ok_short = 0, bad = 0;
    bool edge = false;
    printf("Measuring turnaround on stat %d: %d full-DCB reads and %d 1-byte reads...\n", addr, n, n);
    for (int i = 0; i < n; i++) {
        hm_bus_read((uint8_t)addr, 0, HM_READ_ALL, &t);
        if (t.result == HM_TXN_OK && t.turnaround_us >= 0) {
            ok_full++;
            edge = t.turnaround_from_edge;
            sum_full += t.turnaround_us;
            sum_txn += t.t_rx_last_us - t.t_tx_start_us;
            if (t.turnaround_us < mn_full) mn_full = t.turnaround_us;
            if (t.turnaround_us > mx_full) mx_full = t.turnaround_us;
        } else {
            bad++;
        }
        hm_bus_read((uint8_t)addr, 18, 1, &t); /* set temperature */
        if (t.result == HM_TXN_OK && t.turnaround_us >= 0) {
            ok_short++;
            sum_short += t.turnaround_us;
            if (t.turnaround_us < mn_short) mn_short = t.turnaround_us;
            if (t.turnaround_us > mx_short) mx_short = t.turnaround_us;
        } else {
            bad++;
        }
    }
    printf("Timing source: %s\n", edge ? "start-bit edge interrupt (us resolution)"
                                       : "byte arrival (about +-2 ms)");
    if (ok_full) {
        printf("TURNAROUND full-DCB  n=%d min=%.2f avg=%.2f max=%.2f ms; whole transaction avg %.1f ms\n",
               ok_full, mn_full / 1000.0, (double)sum_full / ok_full / 1000.0, mx_full / 1000.0,
               (double)sum_txn / ok_full / 1000.0);
    }
    if (ok_short) {
        printf("TURNAROUND 1-byte    n=%d min=%.2f avg=%.2f max=%.2f ms\n", ok_short,
               mn_short / 1000.0, (double)sum_short / ok_short / 1000.0, mx_short / 1000.0);
    }
    printf("Failed transactions: %d\n", bad);
    return 0;
}

/* ---- sniff --------------------------------------------------------------- */

static void describe_frame(const uint8_t *b, size_t n)
{
    /* Requests carry a 1-byte length at [1]; replies a 2-byte length at [1..2]. */
    bool req_ok = n >= 10 && b[1] == n && hm_crc16(b, n - 2) == (uint16_t)(b[n - 2] | (b[n - 1] << 8));
    bool rep_ok = n >= 7 && (size_t)(b[1] | (b[2] << 8)) == n &&
                  hm_crc16(b, n - 2) == (uint16_t)(b[n - 2] | (b[n - 1] << 8));
    if (req_ok) {
        printf("    request %s from 0x%02X to stat %u, addr %u, len %u: CRC ok\n",
               b[3] ? "WRITE" : "READ", b[2], b[0], b[4] | (b[5] << 8), b[6] | (b[7] << 8));
    } else if (rep_ok) {
        printf("    reply from stat %u to 0x%02X: CRC ok\n", b[3], b[0]);
    } else {
        printf("    not a valid V3 frame (CRC or length mismatch, or two frames run together)\n");
    }
}

static int cmd_sniff(int argc, char **argv)
{
    int secs;
    if (bench_parse_int(argc > 1 ? argv[1] : NULL, 30, 1, 3600, &secs)) {
        return 1;
    }
    printf("Listening for %d s without transmitting. Frames are split on 10 ms of silence.\n", secs);
    static uint8_t buf[HM_MAX_FRAME];
    const int64_t t0 = esp_timer_get_time();
    const int64_t end = t0 + (int64_t)secs * 1000000;
    int frames = 0, good = 0;
    uint32_t errs_total = 0;
    while (esp_timer_get_time() < end) {
        int64_t first;
        uint32_t errs;
        uint32_t left_ms = (uint32_t)((end - esp_timer_get_time()) / 1000);
        size_t n = hm_bus_sniff(buf, sizeof buf, left_ms ? left_ms : 1, 10, &first, &errs);
        errs_total += errs;
        if (n == 0) {
            continue;
        }
        frames++;
        printf("[%9.3f s] %u bytes%s:", (first - t0) / 1e6, (unsigned)n,
               errs ? " WITH UART ERRORS" : "");
        for (size_t i = 0; i < n; i++) {
            if (i && i % 24 == 0) printf("\n             ");
            printf(" %02X", buf[i]);
        }
        printf("\n");
        uint16_t crc = n >= 3 ? (uint16_t)(buf[n - 2] | (buf[n - 1] << 8)) : 0;
        if (n >= 7 && hm_crc16(buf, n - 2) == crc) good++;
        describe_frame(buf, n);
    }
    printf("Sniff done: %d bursts, %d with valid CRC, %" PRIu32 " UART line errors.\n", frames, good,
           errs_total);
    return 0;
}

/* ---- stats, master, timing ---------------------------------------------- */

static int cmd_stats(int argc, char **argv)
{
    hm_bus_counters_t c;
    hm_bus_counters(&c);
    printf("Bus since boot/reset: txns=%" PRIu32 " ok=%" PRIu32 " timeout=%" PRIu32
           " partial=%" PRIu32 " crc=%" PRIu32 " bad_other=%" PRIu32 " echo_only=%" PRIu32
           " uart_err=%" PRIu32 "\n",
           c.txns, c.ok, c.timeout, c.partial, c.crc, c.bad_other, c.echo_only, c.uart_errors);
    if (argc > 1 && strcmp(argv[1], "reset") == 0) {
        hm_bus_counters_reset();
        printf("Counters reset.\n");
    }
    return 0;
}

static int cmd_master(int argc, char **argv)
{
    if (argc > 1) {
        int a;
        if (bench_parse_int(argv[1], 0x81, 0x81, 0xA0, &a)) {
            return 1;
        }
        hm_bus_set_master((uint8_t)a);
    }
    printf("Master address 0x%02X\n", hm_bus_config()->master_addr);
    return 0;
}

static int cmd_timing(int argc, char **argv)
{
    int to = 0, gap = 0;
    if (argc > 1 && bench_parse_int(argv[1], 0, 50, 5000, &to)) return 1;
    if (argc > 2 && bench_parse_int(argv[2], 0, 20, 5000, &gap)) return 1;
    hm_bus_set_timing((uint32_t)to, (uint32_t)gap);
    const hm_bus_config_t *c = hm_bus_config();
    printf("Reply timeout %" PRIu32 " ms, bus gap %" PRIu32 " ms, inter-byte %" PRIu32
           " ms, %d baud, edge timing %s\n",
           c->reply_timeout_ms, c->gap_ms, c->interbyte_timeout_ms, c->baud,
           c->edge_timing ? "on" : "off");
    return 0;
}

void bench_register_commands(void)
{
    const esp_console_cmd_t cmds[] = {
        { .command = "read", .help = "Read a stat's full DCB and print it decoded, with raw frames and timing",
          .hint = "[addr] [start] [count]", .func = cmd_read },
        { .command = "scan", .help = "Probe addresses 1-32 and list the stats that answer",
          .hint = "[timeout_ms]", .func = cmd_scan },
        { .command = "turnaround", .help = "Measure reply turnaround over N full and N 1-byte reads",
          .hint = "[addr] [n]", .func = cmd_turnaround },
        { .command = "sniff", .help = "Listen without transmitting and dump bytes and frames",
          .hint = "[seconds]", .func = cmd_sniff },
        { .command = "stats", .help = "Bus counters since boot; 'stats reset' clears them",
          .hint = "[reset]", .func = cmd_stats },
        { .command = "master", .help = "Show or set the bridge's master address (0x81-0xA0)",
          .hint = "[addr]", .func = cmd_master },
        { .command = "timing", .help = "Show or set reply timeout and inter-frame gap",
          .hint = "[timeout_ms] [gap_ms]", .func = cmd_timing },
    };
    for (size_t i = 0; i < sizeof cmds / sizeof cmds[0]; i++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
    }
}
