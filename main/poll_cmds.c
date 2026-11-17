/* Milestone 2 console: poll, set, gate. */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bench.h"
#include "esp_console.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hm_dcb.h"
#include "hm_poller.h"
#include "hm_write.h"

#define WRITE_WAIT_MS 30000  /* 6 attempts at 1 s plus read-backs, with margin */

static hm_stat_t s_st; /* big; keep it off the console task stack */

static bool get_decoded(uint8_t addr, hm_dcb_t *d)
{
    return hm_poller_get(addr, &s_st) && s_st.have_dcb &&
           hm_dcb_decode(s_st.dcb, s_st.dcb_len, d) == 0;
}

static int parse_addr(const char *s, int *a)
{
    return bench_parse_int(s, 0, HM_STAT_ADDR_MIN, HM_STAT_ADDR_MAX, a);
}

static void print_t10(uint16_t t)
{
    if (t == HM_TEMP_ABSENT) {
        printf("  -- ");
    } else {
        printf("%3u.%u", t / 10, t % 10);
    }
}

/* ---- poll ----------------------------------------------------------------- */

static void print_status(void)
{
    uint32_t list = hm_poller_list();
    printf("Poller %s, interval %" PRIu32 " s, %u write(s) queued%s\n",
           hm_poller_running() ? "running" : "PAUSED (poll start)", hm_poller_interval() / 1000,
           (unsigned)hm_poller_queue_len(), hm_poller_scanning() ? ", scanning" : "");
    if (!list) {
        printf("No stats added. Try 'poll scan' or 'poll add <addr>'.\n");
        return;
    }
    printf("addr model   link     set  air   mode  heat lock hold  polls  miss   to  crc  bad  wr ok/fail/retry  seen\n");
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    for (uint8_t a = 1; a <= 32; a++) {
        if (!(list & (1u << (a - 1))) || !hm_poller_get(a, &s_st)) {
            continue;
        }
        printf("%4u ", a);
        hm_dcb_t d;
        if (s_st.have_dcb && hm_dcb_decode(s_st.dcb, s_st.dcb_len, &d) == 0) {
            printf("%-7s %-8s %3u ", hm_model_str(d.model), hm_link_str(s_st.link), d.set_temp);
            print_t10(hm_dcb_room_t10(&d));
            printf("  %-5s %-4s %-4s %4u ", hm_mode_str(hm_dcb_mode(&d)), d.heating_on ? "yes" : "no",
                   d.key_lock ? "on" : "off", d.hold_minutes);
        } else {
            printf("%-7s %-8s %3s %5s  %-5s %-4s %-4s %4s ", "?", hm_link_str(s_st.link), "-", "-",
                   "-", "-", "-", "-");
        }
        const hm_stat_counters_t *c = &s_st.cnt;
        printf("%6" PRIu32 " %5" PRIu32 " %4" PRIu32 " %4" PRIu32 " %4" PRIu32 "  %5" PRIu32 "/%" PRIu32
               "/%" PRIu32,
               c->polls, c->misses, c->timeouts, c->crc, c->bad, c->writes_ok, c->writes_failed,
               c->write_retries);
        if (s_st.have_dcb) {
            printf("  %" PRIu32 " s ago", (now - s_st.last_ok_ms) / 1000);
        }
        printf("\n");
    }
}

static int cmd_poll(int argc, char **argv)
{
    const char *sub = argc > 1 ? argv[1] : "status";
    if (strcmp(sub, "start") == 0) {
        if (argc > 2) {
            int s;
            if (bench_parse_int(argv[2], 10, 5, 60, &s)) return 1;
            hm_poller_set_interval((uint32_t)s * 1000);
        }
        hm_poller_run(true);
        printf("Polling every %" PRIu32 " s.\n", hm_poller_interval() / 1000);
    } else if (strcmp(sub, "stop") == 0) {
        hm_poller_run(false);
        printf("Poller paused (a slot already on the wire finishes first).\n");
    } else if (strcmp(sub, "status") == 0) {
        print_status();
    } else if (strcmp(sub, "add") == 0 && argc > 2) {
        for (int i = 2; i < argc; i++) {
            int a;
            if (parse_addr(argv[i], &a)) return 1;
            hm_poller_add((uint8_t)a);
        }
        printf("Added. %s\n", hm_poller_running() ? "" : "Start with 'poll start'.");
    } else if (strcmp(sub, "remove") == 0 && argc > 2) {
        int a;
        if (parse_addr(argv[2], &a)) return 1;
        hm_poller_remove((uint8_t)a);
        printf("Removed stat %d.\n", a);
    } else if (strcmp(sub, "scan") == 0) {
        hm_poller_scan();
        hm_poller_run(true);
        printf("Scanning 1-32 (found stats are added and polled). Polling pauses until it ends...\n");
        int64_t t0 = esp_timer_get_time();
        while (hm_poller_scanning() && esp_timer_get_time() - t0 < 120 * 1000000LL) {
            vTaskDelay(pdMS_TO_TICKS(200));
        }
        printf("Scan took %.1f s.\n", (esp_timer_get_time() - t0) / 1e6);
        vTaskDelay(pdMS_TO_TICKS(100));
        print_status();
    } else if (strcmp(sub, "log") == 0 && argc > 2) {
        hm_poller_log_events(strcmp(argv[2], "on") == 0);
    } else {
        printf("usage: poll [status] | start [interval_s] | stop | scan | add <addr>... |\n"
               "       remove <addr> | log on|off\n");
        return 1;
    }
    return 0;
}

/* ---- set ------------------------------------------------------------------ */

/* Queue one write, wait for it, print the outcome. Returns true if confirmed. */
static bool do_write(uint8_t a, const hm_write_t *w, bool quiet, uint8_t *attempts, uint32_t *ms)
{
    char what[40];
    hm_write_describe(w, what, sizeof what);
    uint32_t id;
    int64_t t0 = esp_timer_get_time();
    int rc = hm_poller_write(a, w, &id);
    if (rc) {
        printf("  stat %u %s: %s\n", a, what, rc == -1 ? "write queue full" : "bad write");
        return false;
    }
    hm_event_t ev;
    if (!hm_poller_wait_write(id, WRITE_WAIT_MS, &ev)) {
        printf("  stat %u %s: no result after %d s\n", a, what, WRITE_WAIT_MS / 1000);
        return false;
    }
    uint32_t took = (uint32_t)((esp_timer_get_time() - t0) / 1000);
    if (attempts) *attempts = ev.attempts;
    if (ms) *ms = took;
    if (!quiet) {
        printf("  stat %u %s: %s, %u attempt(s), %" PRIu32 " ms\n", a, what, hm_wdone_str(ev.wdone),
               ev.attempts, took);
    }
    return ev.wdone == HM_WDONE_OK;
}

static bool parse_onoff(const char *s, bool *v)
{
    if (!strcmp(s, "on") || !strcmp(s, "1")) { *v = true; return true; }
    if (!strcmp(s, "off") || !strcmp(s, "0")) { *v = false; return true; }
    return false;
}

static int cmd_set(int argc, char **argv)
{
    if (argc != 4) {
        printf("usage: set <addr> temp <5-35> | frost <7-17> | mode heat|frost|off |\n"
               "       lock on|off | hold <min> | holiday <hours> | hw timer|on|off | floor <20-45>\n");
        return 1;
    }
    int a;
    if (parse_addr(argv[1], &a)) return 1;
    if (!hm_poller_running()) {
        printf("The poller is paused; 'poll start' first.\n");
        return 1;
    }
    const char *what = argv[2], *val = argv[3];
    hm_write_t w[2];
    size_t n = 1;
    hm_wr_status_t st = HM_WR_ARG;
    bool b;
    int v = atoi(val);

    if (!strcmp(what, "temp")) {
        st = hm_wr_set_temp(&w[0], v);
    } else if (!strcmp(what, "frost")) {
        st = hm_wr_frost_temp(&w[0], v);
    } else if (!strcmp(what, "floor")) {
        st = hm_wr_floor_max(&w[0], v);
    } else if (!strcmp(what, "hold")) {
        st = v >= 0 ? hm_wr_hold(&w[0], (unsigned)v) : HM_WR_RANGE;
    } else if (!strcmp(what, "holiday")) {
        st = v >= 0 ? hm_wr_holiday(&w[0], (unsigned)v) : HM_WR_RANGE;
    } else if (!strcmp(what, "lock") && parse_onoff(val, &b)) {
        st = hm_wr_key_lock(&w[0], b);
    } else if (!strcmp(what, "mode")) {
        hm_mode_t m = !strcmp(val, "heat") ? HM_MODE_HEAT : !strcmp(val, "frost") ? HM_MODE_FROST
                    : !strcmp(val, "off") ? HM_MODE_OFF : (hm_mode_t)-1;
        n = hm_wr_mode(w, m);
        st = n ? HM_WR_OK : HM_WR_RANGE;
    } else if (!strcmp(what, "hw")) {
        int cmd = !strcmp(val, "timer") ? HM_HW_FOLLOW_TIMER : !strcmp(val, "on") ? HM_HW_FORCE_ON
                : !strcmp(val, "off") ? HM_HW_FORCE_OFF : -1;
        hm_dcb_t d;
        uint8_t model = get_decoded((uint8_t)a, &d) ? d.model : HM_MODEL_PRTHW;
        st = hm_wr_hw_state(&w[0], model, cmd);
    }
    if (st != HM_WR_OK) {
        printf("Can't set %s to %s: %s\n", what, val, hm_wr_status_str(st));
        return 1;
    }
    bool ok = true;
    for (size_t i = 0; i < n && ok; i++) {
        ok = do_write((uint8_t)a, &w[i], false, NULL, NULL);
    }
    hm_dcb_t d;
    if (get_decoded((uint8_t)a, &d)) {
        printf("Stat %d now: set %u C, mode %s, frost %u C, lock %s, hold %u min, holiday %u h%s%s\n", a,
               d.set_temp, hm_mode_str(hm_dcb_mode(&d)), d.frost_temp, d.key_lock ? "on" : "off",
               d.hold_minutes, d.holiday_hours, d.has_hw ? ", hot water " : "",
               d.has_hw ? (d.hw_on ? "on" : "off") : "");
    }
    return ok ? 0 : 1;
}

/* ---- gate ------------------------------------------------------------------ */

typedef struct {
    int stats, steps, failed_steps, retries;
    uint32_t worst_ms;
} gate_tally_t;

static bool gate_step(gate_tally_t *g, uint8_t a, const char *step, const hm_write_t *w, size_t n,
                      bool (*check)(const hm_dcb_t *, int), int want, int slow_s)
{
    bool ok = true;
    uint8_t att = 0, att_total = 0;
    uint32_t ms = 0, ms_total = 0;
    for (size_t i = 0; i < n && ok; i++) {
        ok = do_write(a, &w[i], true, &att, &ms);
        att_total += att;
        ms_total += ms;
        g->retries += att > 0 ? att - 1 : 0;
    }
    hm_dcb_t d;
    bool seen = ok && get_decoded(a, &d) && check(&d, want);
    g->steps++;
    if (!seen) {
        g->failed_steps++;
    }
    if (ms_total > g->worst_ms) {
        g->worst_ms = ms_total;
    }
    printf("GATE2 stat=%u step=%-14s %s frames=%u time=%" PRIu32 "ms\n", a, step,
           seen ? "CONFIRMED" : ok ? "FAIL(read-back)" : "FAIL", att_total, ms_total);
    if (slow_s && seen) {
        printf("      (check the stat's display now)\n");
        vTaskDelay(pdMS_TO_TICKS(slow_s * 1000));
    }
    return seen;
}

static bool chk_set(const hm_dcb_t *d, int v) { return d->set_temp == v; }
static bool chk_mode(const hm_dcb_t *d, int v) { return (int)hm_dcb_mode(d) == v; }

static void gate_stat(gate_tally_t *g, uint8_t a, int slow_s)
{
    hm_dcb_t orig;
    if (!get_decoded(a, &orig) || s_st.link != HM_LINK_ONLINE) {
        printf("GATE2 stat=%u not online; skipped (counts as a failure)\n", a);
        g->steps++;
        g->failed_steps++;
        return;
    }
    g->stats++;
    const hm_mode_t mode0 = hm_dcb_mode(&orig);
    const int set0 = orig.set_temp;
    printf("GATE2 stat=%u model=%s start: set %d C, mode %s\n", a, hm_model_str(orig.model), set0,
           hm_mode_str(mode0));

    hm_write_t w[2];
    char name[24];
    int set1 = set0 < HM_SET_TEMP_MAX ? set0 + 1 : set0 - 1;
    hm_wr_set_temp(&w[0], set1);
    snprintf(name, sizeof name, "setpoint->%d", set1);
    gate_step(g, a, name, w, 1, chk_set, set1, slow_s);
    hm_wr_set_temp(&w[0], set0);
    snprintf(name, sizeof name, "setpoint->%d", set0);
    bool set_restored = gate_step(g, a, name, w, 1, chk_set, set0, 0);

    static const hm_mode_t seq[] = { HM_MODE_FROST, HM_MODE_HEAT, HM_MODE_OFF };
    for (size_t i = 0; i < sizeof seq / sizeof seq[0]; i++) {
        size_t n = hm_wr_mode(w, seq[i]);
        snprintf(name, sizeof name, "mode->%s", hm_mode_str(seq[i]));
        gate_step(g, a, name, w, n, chk_mode, seq[i], slow_s);
    }
    size_t n = hm_wr_mode(w, mode0);
    snprintf(name, sizeof name, "mode->%s", hm_mode_str(mode0));
    bool mode_restored = gate_step(g, a, name, w, n, chk_mode, mode0, 0);

    if (!set_restored || !mode_restored) {
        printf("!!! stat %u may not be back as it was: set it to %d C, mode %s by hand\n", a, set0,
               hm_mode_str(mode0));
    }
}

static int cmd_gate(int argc, char **argv)
{
    uint32_t list = 0;
    int slow_s = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "slow")) {
            slow_s = 5;
            continue;
        }
        int a;
        if (parse_addr(argv[i], &a)) return 1;
        list |= 1u << (a - 1);
    }
    if (!list) {
        list = hm_poller_list();
    }
    if (!list) {
        printf("No stats. Run 'poll scan' first.\n");
        return 1;
    }
    if (!hm_poller_running()) {
        hm_poller_run(true);
        printf("Started the poller.\n");
    }
    /* Give newly added stats a first read. */
    for (int wait = 0; wait < 50; wait++) {
        bool all = true;
        for (uint8_t a = 1; a <= 32; a++) {
            if ((list & (1u << (a - 1))) && !(hm_poller_get(a, &s_st) && s_st.have_dcb)) {
                hm_poller_add(a);
                all = false;
            }
        }
        if (all) break;
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    printf("Milestone 2 gate: on each stat, setpoint +1 and back, then mode frost, heat, off,\n"
           "and back to how it was. Every write must be acked and read back.\n");
    gate_tally_t g = { 0 };
    int64_t t0 = esp_timer_get_time();
    for (uint8_t a = 1; a <= 32; a++) {
        if (list & (1u << (a - 1))) {
            gate_stat(&g, a, slow_s);
        }
    }
    bool pass = g.failed_steps == 0 && g.stats > 0;
    printf("GATE2-RESULT %s stats=%d steps=%d failed=%d retries=%d worst=%" PRIu32 "ms total=%.1fs\n",
           pass ? "PASS" : "FAIL", g.stats, g.steps, g.failed_steps, g.retries, g.worst_ms,
           (esp_timer_get_time() - t0) / 1e6);
    return pass ? 0 : 1;
}

void poll_register_commands(void)
{
    const esp_console_cmd_t cmds[] = {
        { .command = "poll", .help = "Poller: status (default), start [interval_s], stop, scan, "
                                     "add <addr>..., remove <addr>, log on|off",
          .hint = "[status|start|stop|scan|add|remove|log]", .func = cmd_poll },
        { .command = "set", .help = "Write through the poller's queue and confirm by read-back: "
                                    "temp, frost, mode heat|frost|off, lock, hold, holiday, hw, floor",
          .hint = "<addr> <what> <value>", .func = cmd_set },
        { .command = "gate", .help = "Milestone 2 gate: setpoint and mode writes confirmed on every "
                                     "stat, then restored. 'slow' pauses to watch the stats",
          .hint = "[addr...] [slow]", .func = cmd_gate },
    };
    for (size_t i = 0; i < sizeof cmds / sizeof cmds[0]; i++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
    }
}
