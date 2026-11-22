/*
 * Milestone 6: the week-long gate's health line, and the 'health', 'ota'
 * and 'pin' console commands.
 *
 * Once an hour (and on 'health') the bridge prints one line for
 * tools/week_logger.py:
 *   HMBRIDGE-HEALTH up_s=.. fw=.. zones=.. online=.. offline=3,7 polls=..
 *     timeouts=.. crc=.. bad=.. wr_ok=.. wr_fail=.. clk_ok=.. clk_fail=..
 *     clk_auto=.. clk_age_s=.. time=1 wifi=1 wifi_drops=.. matter=1
 *     fabrics=2 ota=idle trial=0
 */
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "bench.h"
#include "esp_console.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "health.h"
#include "matter_bridge.h"
#include "net.h"
#include "ota.h"
#include "ui.h"
#include "zone_store.h"

#define HEALTH_PERIOD_S 3600

void health_print(void)
{
    uint32_t list = zone_store_list();
    unsigned zones = 0, online = 0;
    char offline[100] = "";
    size_t off_len = 0;
    uint32_t polls = 0, timeouts = 0, crc = 0, bad = 0, wr_ok = 0, wr_fail = 0;
    for (uint8_t a = 1; a <= 32; a++) {
        zone_view_t v;
        if (!(list & (1u << (a - 1))) || !zone_store_get(a, &v)) {
            continue;
        }
        zones++;
        if (v.link == HM_LINK_ONLINE) {
            online++;
        } else if (off_len + 4 < sizeof offline) {
            off_len += (size_t)snprintf(offline + off_len, sizeof offline - off_len, "%s%u", off_len ? "," : "", a);
        }
        zone_diag_t d;
        if (zone_store_diag(a, &d)) {
            polls += d.polls;
            timeouts += d.timeouts;
            crc += d.crc;
            bad += d.bad;
            wr_ok += d.writes_ok;
            wr_fail += d.writes_failed;
        }
    }
    zone_clock_info_t ck;
    zone_store_clock_info(&ck);
    const time_t now = time(NULL);
    const long clk_age = ck.last_utc && now > (time_t)ck.last_utc ? (long)(now - (time_t)ck.last_utc) : -1;
    net_status_t ns;
    net_status(&ns);
    matter_status_t ms;
    matter_bridge_status(&ms);
    ota_status_t os;
    ota_status(&os);
    printf("HMBRIDGE-HEALTH up_s=%" PRIu32 " fw=%s zones=%u online=%u offline=%s polls=%" PRIu32 " timeouts=%" PRIu32
           " crc=%" PRIu32 " bad=%" PRIu32 " wr_ok=%" PRIu32 " wr_fail=%" PRIu32 " clk_ok=%" PRIu32
           " clk_fail=%" PRIu32 " clk_auto=%" PRIu32 " clk_age_s=%ld time=%d wifi=%d wifi_drops=%u matter=%d"
           " fabrics=%u ota=\"%s\" trial=%d\n",
           (uint32_t)(esp_timer_get_time() / 1000000), os.running, zones, online, offline[0] ? offline : "-", polls,
           timeouts, crc, bad, wr_ok, wr_fail, ck.ok, ck.failed, ck.auto_runs, clk_age, net_time_set(), ns.up,
           ns.disconnects, ms.running, ms.fabric_count, ota_state_str(os.state), os.on_trial);
}

static void health_tick(void *arg)
{
    (void)arg;
    health_print();
}

bool health_ok(void)
{
    /* A new firmware is kept once the bus works: no zones yet, or one answers. */
    uint32_t list = zone_store_list();
    if (!list) {
        return true;
    }
    for (uint8_t a = 1; a <= 32; a++) {
        zone_view_t v;
        if ((list & (1u << (a - 1))) && zone_store_get(a, &v) && v.link == HM_LINK_ONLINE) {
            return true;
        }
    }
    return false;
}

static int cmd_health(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    health_print();
    return 0;
}

static void print_ota(void)
{
    ota_status_t s;
    ota_status(&s);
    printf("Firmware %s%s. Updates: %s", s.running, s.on_trial ? " (new, on trial)" : "", ota_state_str(s.state));
    if (s.latest[0]) {
        printf(", latest release %s", s.latest);
    }
    if (s.state == OTA_DOWNLOADING && s.percent >= 0) {
        printf(", %d%%", s.percent);
    }
    if (s.error[0]) {
        printf(". %s", s.error);
    }
    printf(".\n");
}

static int cmd_ota(int argc, char **argv)
{
    const char *sub = argc > 1 ? argv[1] : "status";
    esp_err_t e = ESP_OK;
    if (strcmp(sub, "status") == 0) {
        print_ota();
        return 0;
    } else if (strcmp(sub, "check") == 0) {
        e = ota_check();
        if (e == ESP_OK) {
            printf("Checking GitHub for a newer release; 'ota' shows the result.\n");
        }
    } else if (strcmp(sub, "install") == 0) {
        e = ota_install(argc > 2 ? argv[2] : NULL);
        if (e == ESP_OK) {
            printf("Installing; the bridge restarts when it is done.\n");
        } else if (e == ESP_ERR_INVALID_ARG) {
            printf("The URL must start http:// or https://\n");
            return 1;
        } else if (argc <= 2 && e == ESP_ERR_INVALID_STATE) {
            printf("No release found yet: 'ota check' first, or give a URL.\n");
            return 1;
        }
    } else {
        printf("usage: ota [status] | check | install [url]\n");
        return 1;
    }
    if (e == ESP_ERR_INVALID_STATE) {
        printf("An update is already running.\n");
        return 1;
    }
    return e == ESP_OK ? 0 : 1;
}

static int cmd_pin(int argc, char **argv)
{
    const char *sub = argc > 1 ? argv[1] : "";
    if (strcmp(sub, "off") == 0) {
        if (ui_platform_pin_save(NULL)) {
            printf("Settings PIN turned off.\n");
            return 0;
        }
        printf("Could not save.\n");
        return 1;
    }
    printf("Settings PIN is %s. 'pin off' turns it off (for a forgotten PIN); set it on the screen.\n",
           ui_platform_pin_enabled() ? "on" : "off");
    return 0;
}

void health_register_commands(void)
{
    const esp_console_cmd_t cmds[] = {
        { .command = "health", .help = "Print the health line the week logger records (also hourly)",
          .func = cmd_health },
        { .command = "ota", .help = "Firmware updates: status, check GitHub for a release, or install it "
                                    "(or a build from a URL, e.g. http://<pc>:8070/heatmiser_bridge.bin)",
          .hint = "[status] | check | install [url]", .func = cmd_ota },
        { .command = "pin", .help = "Show the settings PIN state, or turn it off", .hint = "[off]",
          .func = cmd_pin },
    };
    for (size_t i = 0; i < sizeof cmds / sizeof cmds[0]; i++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
    }
    const esp_timer_create_args_t ta = { .callback = health_tick, .name = "health" };
    esp_timer_handle_t t;
    if (esp_timer_create(&ta, &t) == ESP_OK) {
        esp_timer_start_periodic(t, HEALTH_PERIOD_S * 1000000ull);
    }
}
