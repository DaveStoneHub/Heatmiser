/* Milestone 4 console: matter status, pair, remove. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bench.h"
#include "esp_console.h"
#include "matter_bridge.h"

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

void matter_register_command(void)
{
    const esp_console_cmd_t c = {
        .command = "matter",
        .help = "Matter: status (default) with pairing codes, controllers and endpoints; "
                "pair [minutes] opens pairing for another controller; remove <fabric> forgets one",
        .hint = "[status|pair|remove]",
        .func = cmd_matter,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&c));
}
