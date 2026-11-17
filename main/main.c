/*
 * Heatmiser bridge, milestone 1: RS485 bus bring-up console.
 *
 * Connect a terminal to the board's console (115200 baud) and type "help".
 */
#include <stdio.h>

#include "bench.h"
#include "esp_console.h"
#include "esp_log.h"
#include "esp_system.h"
#include "hm_bus.h"

static const char *TAG = "main";

void app_main(void)
{
    /* The soak logger on the PC looks for this line to spot an unexpected reset. */
    printf("\nHMBRIDGE-BOOT reset_reason=%d\n", (int)esp_reset_reason());

    const hm_bus_config_t cfg = HM_BUS_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(hm_bus_init(&cfg));

    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.prompt = "hm> ";
    repl_cfg.max_cmdline_length = 128;

    esp_console_register_help_command();
    bench_register_commands();
    soak_register_command();

#if defined(CONFIG_ESP_CONSOLE_UART_DEFAULT) || defined(CONFIG_ESP_CONSOLE_UART_CUSTOM)
    esp_console_dev_uart_config_t hw = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&hw, &repl_cfg, &repl));
#elif defined(CONFIG_ESP_CONSOLE_USB_CDC)
    esp_console_dev_usb_cdc_config_t hw = ESP_CONSOLE_DEV_CDC_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_cdc(&hw, &repl_cfg, &repl));
#elif defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
    esp_console_dev_usb_serial_jtag_config_t hw = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&hw, &repl_cfg, &repl));
#else
#error "Select a console (UART, USB CDC or USB Serial/JTAG) in menuconfig"
#endif

    printf("\nHeatmiser RS485 bring-up. Default stat %d, master 0x%02X.\n"
           "Try: read, scan, turnaround, sniff, soak start. Type 'help' for all.\n\n",
           CONFIG_HM_BUS_DEFAULT_STAT, cfg.master_addr);
    ESP_LOGI(TAG, "console ready");
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
