/*
 * Heatmiser bridge: the RS485 poller (milestones 1 and 2) with the zone store,
 * the touchscreen (milestone 3), the Matter bridge (milestones 4 and 5), and
 * clock sync, settings PIN, firmware updates and the health line (milestone
 * 6), plus the bench console.
 *
 * Connect a terminal to the board's console (115200 baud) and type "help".
 */
#include <stdio.h>

#include "bench.h"
#include "board.h"
#include "display.h"
#include "esp_console.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hm_bus.h"
#include "health.h"
#include "hm_poller.h"
#include "matter_bridge.h"
#include "net.h"
#include "nvs_flash.h"
#include "ota.h"
#include "ui.h"
#include "zone_store.h"

static const char *TAG = "main";

void app_main(void)
{
    /* The soak logger on the PC looks for this line to spot an unexpected reset. */
    printf("\nHMBRIDGE-BOOT reset_reason=%d\n", (int)esp_reset_reason());

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition reformatted (%s); saved zones and Wi-Fi are gone", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    const hm_bus_config_t cfg = HM_BUS_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(hm_bus_init(&cfg));
    ESP_ERROR_CHECK(hm_poller_init()); /* paused until there are zones, 'poll start' or 'poll scan' */
    if (zone_store_init() < 0) {
        ESP_LOGE(TAG, "zone store failed to start");
    }

    /* A new firmware from an update is on trial until the bus answers. */
    ota_init(health_ok);

    /* The screen. A display fault must not stop the bus, so carry on without it. */
    lv_display_t *disp = NULL;
    err = board_display_init(&disp, NULL);
    if (err == ESP_OK && lvgl_port_lock(0)) {
        ui_set_blank_timeout(display_saved_blank_timeout());
        ui_init();
        lvgl_port_unlock();
        vTaskDelay(pdMS_TO_TICKS(150)); /* first frame drawn before the light comes on */
        board_backlight(true);
    } else {
        ESP_LOGE(TAG, "display failed to start: %s", esp_err_to_name(err));
    }

    /* Matter owns Wi-Fi. The zones are restored above, so each one's saved
     * endpoint is back before a controller can look. If Matter will not
     * start, the bridge still runs as a local controller on its own Wi-Fi. */
    if (matter_bridge_start() != ESP_OK) {
        ESP_LOGE(TAG, "Matter failed to start; running as a local controller");
    }
    if (net_attach() != ESP_OK && net_init() != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi failed to start");
    }

    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.prompt = "hm> ";
    repl_cfg.max_cmdline_length = 128;

    esp_console_register_help_command();
    bench_register_commands();
    soak_register_command();
    poll_register_commands();
    ui_register_commands();
    matter_register_command();
    health_register_commands();

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

    printf("\nHeatmiser RS485 bridge. Default stat %d, master 0x%02X.\n"
           "Milestone 6: health, ota, pin, matter, zone. Type 'help' for all.\n\n",
           CONFIG_HM_BUS_DEFAULT_STAT, cfg.master_addr);
    ESP_LOGI(TAG, "console ready");
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
