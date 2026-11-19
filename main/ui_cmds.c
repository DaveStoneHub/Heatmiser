/* Milestone 3 console: zone, wifi, tz, display, screen. */
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "bench.h"
#include "board.h"
#include "display.h"
#include "esp_console.h"
#include "esp_heap_caps.h"
#include "esp_lvgl_port.h"
#include "net.h"
#include "ui.h"
#include "zone_store.h"

static int parse_addr(const char *s, int *a)
{
    return bench_parse_int(s, 0, 1, 32, a);
}

static void print_zones(void)
{
    uint32_t list = zone_store_list();
    if (!list) {
        printf("No zones. 'poll scan' or Zones and scan in Settings finds them.\n");
        return;
    }
    printf("addr name              model     link     room   set  mode  heat lock pending\n");
    for (uint8_t a = 1; a <= 32; a++) {
        zone_view_t v;
        if (!(list & (1u << (a - 1))) || !zone_store_get(a, &v)) {
            continue;
        }
        char room[12];
        zone_fmt_t10(v.have ? v.room_t10 : ZONE_T_ABSENT, room, sizeof room);
        const char *model = v.have ? hm_model_str(v.model)
                            : v.cfg.model == 0xFF ? "?" : hm_model_str(v.cfg.model);
        printf("%4u %-17s %-9s %-8s %5s  %3u  %-5s %-4s %-4s %s%s%s%s%s\n", a, v.cfg.name, model,
               hm_link_str(v.link), room, v.have ? v.set_temp : 0, v.have ? hm_mode_str(v.mode) : "-",
               v.have && v.heating ? "yes" : "no", v.have && v.locked ? "on" : "off",
               v.pend.set_temp ? "set " : "", v.pend.mode ? "mode " : "", v.pend.lock ? "lock " : "",
               v.pend.hold ? "hold " : "", v.pend.hw ? "hw" : "");
    }
}

static int cmd_zone(int argc, char **argv)
{
    const char *sub = argc > 1 ? argv[1] : "list";
    int a;
    if (strcmp(sub, "list") == 0) {
        print_zones();
        return 0;
    }
    if (argc < 3 || parse_addr(argv[2], &a)) {
        printf("usage: zone [list] | name <addr> <name...> | add <addr> | remove <addr>\n");
        return 1;
    }
    zone_err_t e;
    if (strcmp(sub, "name") == 0 && argc > 3) {
        char name[64] = "";
        for (int i = 3; i < argc; i++) {
            if (i > 3) {
                strlcat(name, " ", sizeof name);
            }
            strlcat(name, argv[i], sizeof name);
        }
        e = zone_store_rename((uint8_t)a, name);
    } else if (strcmp(sub, "add") == 0) {
        e = zone_store_add((uint8_t)a);
    } else if (strcmp(sub, "remove") == 0) {
        e = zone_store_remove((uint8_t)a);
    } else {
        printf("unknown: zone %s\n", sub);
        return 1;
    }
    printf("%s\n", e == ZONE_OK ? "OK, saved." : zone_err_str(e));
    return e == ZONE_OK ? 0 : 1;
}

static int cmd_wifi(int argc, char **argv)
{
    const char *sub = argc > 1 ? argv[1] : "status";
    if (strcmp(sub, "join") == 0 && argc >= 3) {
        esp_err_t err = net_join(argv[2], argc > 3 ? argv[3] : "");
        printf(err == ESP_OK ? "Joining '%s' (saved for next boot)...\n" : "Failed: %s\n",
               err == ESP_OK ? argv[2] : esp_err_to_name(err));
        return err != ESP_OK;
    }
    if (strcmp(sub, "forget") == 0) {
        net_forget();
        printf("Wi-Fi forgotten.\n");
        return 0;
    }
    net_status_t ns;
    net_status(&ns);
    if (!ns.configured) {
        printf("Wi-Fi not set up. wifi join <ssid> <password>  (quote a name with spaces)\n");
    } else {
        printf("Wi-Fi '%s': %s", ns.ssid, ns.up ? "up" : "connecting");
        if (ns.up) {
            printf(", IP %s, gateway %s, %d dBm", ns.ip, ns.gw, ns.rssi);
        }
        printf(", %u disconnect(s) since boot\n", ns.disconnects);
    }
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    char buf[48];
    strftime(buf, sizeof buf, "%a %d %b %Y %H:%M:%S %Z", &tm);
    printf("Clock: %s%s (TZ %s)\n", buf, net_time_set() ? "" : ", NOT synced yet", net_tz());
    return 0;
}

static int cmd_tz(int argc, char **argv)
{
    if (argc < 2) {
        printf("TZ %s. Set a POSIX TZ string, e.g. tz GMT0BST,M3.5.0/1,M10.5.0\n", net_tz());
        return 0;
    }
    esp_err_t err = net_set_tz(argv[1]);
    printf(err == ESP_OK ? "Saved.\n" : "Failed: %s\n", esp_err_to_name(err));
    return err != ESP_OK;
}

static int cmd_display(int argc, char **argv)
{
    const char *sub = argc > 1 ? argv[1] : "status";
    if (strcmp(sub, "soak") == 0 && argc > 2) {
        bool on = strcmp(argv[2], "on") == 0;
        if (!on && strcmp(argv[2], "off") != 0) {
            display_soak_log("now");
            return 0;
        }
        if (lvgl_port_lock(1000)) {
            ui_set_soak(on);
            lvgl_port_unlock();
        }
        printf("Display soak %s.\n", on ? "running: drift pattern on, Wi-Fi and NVS load on" : "stopped");
        return 0;
    }
    if (strcmp(sub, "timeout") == 0 && argc > 2) {
        int s;
        if (bench_parse_int(argv[2], 0, 0, 86400, &s)) {
            return 1;
        }
        if (lvgl_port_lock(1000)) {
            ui_set_blank_timeout((uint32_t)s);
            lvgl_port_unlock();
        }
        ui_platform_save_blank_timeout((uint32_t)s);
        printf("Screen blanks after %d s without a touch%s.\n", s, s ? "" : " (never)");
        return 0;
    }
    if (strcmp(sub, "backlight") == 0 && argc > 2) {
        return board_backlight(strcmp(argv[2], "on") == 0) != ESP_OK;
    }
    if (strcmp(sub, "restart") == 0) {
        printf("RGB DMA restart requested: %s\n", esp_err_to_name(board_lcd_restart()));
        return 0;
    }
    printf("Backlight %s, %" PRIu32 " frames drawn, blank after %" PRIu32 " s, soak %s\n",
           board_backlight_is_on() ? "on" : "off", board_flush_count(), ui_blank_timeout(),
           display_soak_running() ? "running" : "off");
    printf("Internal RAM %u free (lowest %u), PSRAM %u free\n",
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    return 0;
}

static int cmd_screen(int argc, char **argv)
{
    const char *sub = argc > 1 ? argv[1] : "dash";
    int a = 0;
    const bool per_zone = !strcmp(sub, "zone") || !strcmp(sub, "schedule") || !strcmp(sub, "timer") ||
                          !strcmp(sub, "hotwater") || !strcmp(sub, "rename");
    if (per_zone && (argc < 3 || parse_addr(argv[2], &a))) {
        return 1;
    }
    if (!lvgl_port_lock(1000)) {
        return 1;
    }
    if (strcmp(sub, "zone") == 0) {
        ui_show_zone((uint8_t)a);
    } else if (strcmp(sub, "schedule") == 0 || strcmp(sub, "timer") == 0) {
        ui_show_schedule((uint8_t)a, sub[0] == 't');
    } else if (strcmp(sub, "hotwater") == 0) {
        ui_show_hotwater((uint8_t)a);
    } else if (strcmp(sub, "rename") == 0) {
        ui_show_rename((uint8_t)a);
    } else if (strcmp(sub, "settings") == 0) {
        ui_show_settings();
    } else if (strcmp(sub, "zones") == 0) {
        ui_show_zones();
    } else if (strcmp(sub, "diag") == 0) {
        ui_show_diag();
    } else if (strcmp(sub, "system") == 0) {
        ui_show_system();
    } else if (strcmp(sub, "matter") == 0) {
        ui_show_matter();
    } else {
        ui_show_dash();
    }
    lvgl_port_unlock();
    return 0;
}

void ui_register_commands(void)
{
    const esp_console_cmd_t cmds[] = {
        { .command = "zone", .help = "Zones on the screen: list (default), name <addr> <name>, add <addr>, "
                                     "remove <addr>",
          .hint = "[list|name|add|remove]", .func = cmd_zone },
        { .command = "wifi", .help = "Wi-Fi and clock: status (default), join <ssid> [password], forget",
          .hint = "[status|join|forget]", .func = cmd_wifi },
        { .command = "tz", .help = "Show or set the timezone (POSIX TZ string)", .hint = "[tz]",
          .func = cmd_tz },
        { .command = "display", .help = "Display: status (default), soak on|off|now, timeout <s>, "
                                        "backlight on|off, restart",
          .hint = "[status|soak|timeout|backlight|restart]", .func = cmd_display },
        { .command = "screen", .help = "Show a screen: dash, zone|schedule|timer|hotwater|rename <addr>, settings, "
                                       "zones, diag, system, matter",
          .hint = "[dash|zone|schedule|timer|hotwater|rename|settings|zones|diag|system|matter]",
          .func = cmd_screen },
    };
    for (size_t i = 0; i < sizeof cmds / sizeof cmds[0]; i++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
    }
}
