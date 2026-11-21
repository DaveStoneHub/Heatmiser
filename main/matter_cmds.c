/* Milestone 4 and 5 console: matter status, pair, remove; hw (hot water). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bench.h"
#include "esp_console.h"
#include "matter_bridge.h"
#include "hm_write.h"
#include "zone_store.h"

static int cmd_matter(int argc, char **argv)
{
    const char *sub = argc > 1 ? argv[1] : "status";
    if (!strcmp(sub, "status")) {
        matter_bridge_print();
        return 0;
    }
    if (!strcmp(sub, "pair")) {
        int min = 15;
        if (argc > 2 && bench_parse_int(argv[2], 15, 3, 15, &min) != 0) {
            printf("pair [minutes]: 3 to 15\n");
            return 1;
        }
        esp_err_t e = matter_bridge_open_window((uint16_t)(min * 60));
        printf(e == ESP_OK ? "Pairing open for %d min; use the code from 'matter'\n" : "Could not open pairing\n", min);
        return e == ESP_OK ? 0 : 1;
    }
    if (!strcmp(sub, "remove") && argc > 2) {
        int idx;
        if (bench_parse_int(argv[2], 0, 1, 254, &idx) != 0) {
            printf("remove <fabric index>, as 'matter' lists them\n");
            return 1;
        }
        esp_err_t e = matter_bridge_remove_fabric((uint8_t)idx);
        printf(e == ESP_OK ? "Fabric %d removed\n" : "Fabric %d not removed\n", idx);
        return e == ESP_OK ? 0 : 1;
    }
    printf("matter [status | pair [minutes] | remove <fabric>]\n");
    return 1;
}

/* The first PRTHW in the zone list. */
static uint8_t find_prthw(void)
{
    uint32_t list = zone_store_list();
    for (uint8_t a = 1; a <= 32; a++) {
        zone_view_t v;
        if ((list & (1u << (a - 1))) && zone_store_get(a, &v) &&
            (v.have ? v.is_prthw : v.cfg.model == HM_MODEL_PRTHW)) {
            return a;
        }
    }
    return 0;
}

static int cmd_hw(int argc, char **argv)
{
    uint8_t a = find_prthw();
    if (!a) {
        printf("No PRT/HW-N in the zone list\n");
        return 1;
    }
    const char *sub = argc > 1 ? argv[1] : "status";
    zone_err_t e = ZONE_OK;
    int n;
    if (!strcmp(sub, "boost") && argc > 2 && bench_parse_int(argv[2], 0, 0, 1440, &n) == 0) {
        e = zone_store_hw_boost(a, (unsigned)n);
    } else if (!strcmp(sub, "on") || !strcmp(sub, "off")) {
        /* What the Matter switch does */
        e = zone_store_hw_switch(a, sub[1] == 'n', 60);
    } else if (!strcmp(sub, "timer")) {
        e = zone_store_set_hw(a, HM_HW_FOLLOW_TIMER);
    } else if (strcmp(sub, "status")) {
        printf("hw [status | on | off | boost <minutes> | timer]\n");
        return 1;
    }
    if (e != ZONE_OK) {
        printf("%s\n", zone_err_str(e));
        return 1;
    }
    zone_view_t v;
    zone_store_get(a, &v);
    const char *mode = v.hw_mode == ZONE_HW_BOOST ? "boost" : v.hw_mode == ZONE_HW_ON ? "forced on"
                       : v.hw_mode == ZONE_HW_OFF ? "forced off" : "following timer";
    printf("Stat %u '%s': hot water %s, %s", a, v.cfg.name, !v.have ? "not read yet" : v.hw_on ? "on" : "off", mode);
    if (v.boost_left_min) {
        printf(", %u min left", v.boost_left_min);
    } else if (v.hw_mode == ZONE_HW_BOOST) {
        printf(", waiting for the clock");
    }
    printf("%s; Matter endpoint %u\n", v.pend.hw ? " (change on its way)" : "", matter_bridge_hw_endpoint(a));
    return 0;
}

void matter_register_command(void)
{
    const esp_console_cmd_t hw = {
        .command = "hw",
        .help = "Hot water on the PRT/HW-N: status (default); on | off as the Matter switch does them; "
                "boost <minutes> (0 ends it); timer",
        .hint = "[status|on|off|boost|timer]",
        .func = cmd_hw,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&hw));

    const esp_console_cmd_t c = {
        .command = "matter",
        .help = "Matter: status (default) with pairing codes, controllers and endpoints; "
                "pair [minutes] opens pairing for another controller; remove <fabric> forgets one",
        .hint = "[status|pair|remove]",
        .func = cmd_matter,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&c));
}
