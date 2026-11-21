#include "board.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "board";

/* ---- pins (Waveshare 09_lvgl_v9_demo) --------------------------------------- */

#define PIN_I2C_SDA   GPIO_NUM_8
#define PIN_I2C_SCL   GPIO_NUM_9
#define PIN_TOUCH_INT GPIO_NUM_4   /* held low through the GT911 reset: address 0x5D */

#define PIN_VSYNC GPIO_NUM_3
#define PIN_HSYNC GPIO_NUM_46
#define PIN_DE    GPIO_NUM_5
#define PIN_PCLK  GPIO_NUM_7

/* B3-B7, G2-G7, R3-R7 */
static const int s_data_pins[16] = {
    14, 38, 18, 17, 10, 39, 0, 45, 48, 47, 21, 1, 2, 42, 41, 40,
};

/* CH422G: writing 0x24 sets its mode (0x01 = IO0-7 are outputs), writing
 * 0x38 sets those outputs. Each register is its own I2C address. */
#define CH422G_ADDR_MODE   0x24
#define CH422G_ADDR_OUT    0x38
#define EXIO_TP_RST        (1u << 1)
#define EXIO_LCD_BL        (1u << 2)
#define EXIO_LCD_RST       (1u << 3)
#define EXIO_SD_CS         (1u << 4)   /* high = SD card not selected */
#define EXIO_USB_SEL       (1u << 5)   /* low = native USB on GPIO19/20, as the demo leaves it */

static i2c_master_bus_handle_t s_i2c;
static i2c_master_dev_handle_t s_exp_mode, s_exp_out;
static uint8_t s_exio = EXIO_TP_RST | EXIO_LCD_RST | EXIO_SD_CS; /* backlight off */
static esp_lcd_panel_handle_t s_panel;
static volatile uint32_t s_flushes;

static esp_err_t exio_write(uint8_t v)
{
    esp_err_t err = i2c_master_transmit(s_exp_out, &v, 1, 100);
    if (err == ESP_OK) {
        s_exio = v;
    }
    return err;
}

static esp_err_t expander_init(void)
{
    const i2c_master_bus_config_t bus = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus, &s_i2c), TAG, "i2c bus");
    i2c_device_config_t dev = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = CH422G_ADDR_MODE,
        .scl_speed_hz = 400000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_i2c, &dev, &s_exp_mode), TAG, "ch422g");
    dev.device_address = CH422G_ADDR_OUT;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_i2c, &dev, &s_exp_out), TAG, "ch422g");
    const uint8_t mode = 0x01;
    ESP_RETURN_ON_ERROR(i2c_master_transmit(s_exp_mode, &mode, 1, 100), TAG,
                        "CH422G not answering on I2C (SDA 8, SCL 9)");
    return exio_write(s_exio);
}

/* The GT911 latches its I2C address from INT while reset rises. */
static esp_err_t touch_reset(void)
{
    const gpio_config_t io = {
        .pin_bit_mask = 1ULL << PIN_TOUCH_INT,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "touch int");
    ESP_RETURN_ON_ERROR(exio_write(s_exio & ~EXIO_TP_RST), TAG, "touch reset");
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(PIN_TOUCH_INT, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_RETURN_ON_ERROR(exio_write(s_exio | EXIO_TP_RST), TAG, "touch reset");
    vTaskDelay(pdMS_TO_TICKS(60));
    /* Address latched: hand INT back to the GT911 (floating input, as its
     * datasheet asks), so the two never drive the line against each other. */
    gpio_set_direction(PIN_TOUCH_INT, GPIO_MODE_INPUT);
    vTaskDelay(pdMS_TO_TICKS(140));
    return ESP_OK;
}

static esp_err_t panel_init(void)
{
    esp_lcd_rgb_panel_config_t cfg = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = CONFIG_BOARD_LCD_PCLK_MHZ * 1000 * 1000,
            .h_res = BOARD_LCD_H_RES,
            .v_res = BOARD_LCD_V_RES,
            .hsync_pulse_width = 4,
            .hsync_back_porch = 8,
            .hsync_front_porch = 8,
            .vsync_pulse_width = 4,
            .vsync_back_porch = 8,
            .vsync_front_porch = 8,
            .flags.pclk_active_neg = 1,
        },
        /* RGB565: in_color_format defaults from a 16-line bus. */
        .data_width = 16,
        .num_fbs = 2, /* LVGL draws into one while the panel streams the other */
        .bounce_buffer_size_px = BOARD_LCD_H_RES * CONFIG_BOARD_LCD_BOUNCE_LINES,
        .dma_burst_size = 64,
        .hsync_gpio_num = PIN_HSYNC,
        .vsync_gpio_num = PIN_VSYNC,
        .de_gpio_num = PIN_DE,
        .pclk_gpio_num = PIN_PCLK,
        .disp_gpio_num = GPIO_NUM_NC,
        .flags.fb_in_psram = 1,
    };
    for (int i = 0; i < 16; i++) {
        cfg.data_gpio_nums[i] = s_data_pins[i];
    }
    ESP_RETURN_ON_ERROR(esp_lcd_new_rgb_panel(&cfg, &s_panel), TAG, "rgb panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "panel reset");
    return esp_lcd_panel_init(s_panel);
}

/* The RGB driver's interrupt, which refills the bounce buffers, runs on the
 * core that creates the panel. Create it on core 1 so Wi-Fi, Bluetooth and
 * Matter (all on core 0) cannot hold it off; a late refill shows as torn or
 * repeated lines. */
typedef struct {
    TaskHandle_t caller;
    esp_err_t err;
} panel_job_t;

static void panel_task(void *arg)
{
    panel_job_t *job = arg;
    job->err = panel_init();
    xTaskNotifyGive(job->caller);
    vTaskDelete(NULL);
}

static esp_err_t panel_init_on_core1(void)
{
    panel_job_t job = { .caller = xTaskGetCurrentTaskHandle(), .err = ESP_FAIL };
    if (xTaskCreatePinnedToCore(panel_task, "lcd_init", 4096, &job, 5, NULL, 1) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    return job.err;
}

static esp_err_t touch_init(esp_lcd_touch_handle_t *tp)
{
    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    io_cfg.scl_speed_hz = 400000;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(s_i2c, &io_cfg, &io), TAG, "touch io");
    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = BOARD_LCD_H_RES,
        .y_max = BOARD_LCD_V_RES,
        .rst_gpio_num = GPIO_NUM_NC, /* reset is on the expander, done above */
        .int_gpio_num = GPIO_NUM_NC, /* polled */
    };
    return esp_lcd_touch_new_i2c_gt911(io, &tp_cfg, tp);
}

static void on_flush_event(lv_event_t *e)
{
    (void)e;
    s_flushes++;
}

esp_err_t board_display_init(lv_display_t **disp, lv_indev_t **touch)
{
    ESP_RETURN_ON_ERROR(expander_init(), TAG, "io expander");
    ESP_RETURN_ON_ERROR(panel_init_on_core1(), TAG, "panel");
    ESP_RETURN_ON_ERROR(touch_reset(), TAG, "touch reset");
    esp_lcd_touch_handle_t tp = NULL;
    esp_err_t terr = touch_init(&tp);
    if (terr != ESP_OK) {
        ESP_LOGE(TAG, "GT911 touch not found (%s); carrying on without touch", esp_err_to_name(terr));
        tp = NULL;
    }

    lvgl_port_cfg_t port = ESP_LVGL_PORT_INIT_CONFIG();
    port.task_priority = CONFIG_BOARD_LVGL_TASK_PRIORITY;
    port.task_stack = CONFIG_BOARD_LVGL_TASK_STACK;
    port.task_affinity = 1; /* core 1; the bus task and Wi-Fi are on core 0 */
    port.task_max_sleep_ms = 100;
    ESP_RETURN_ON_ERROR(lvgl_port_init(&port), TAG, "lvgl port");

    /* LVGL draws straight into the panel's two PSRAM frame buffers and swaps
     * them at vsync (direct mode, no tearing). The bounce buffers in internal
     * RAM then feed the panel, which is what stops drift. */
    const lvgl_port_display_cfg_t dcfg = {
        .panel_handle = s_panel,
        .buffer_size = BOARD_LCD_H_RES * BOARD_LCD_V_RES,
        .double_buffer = true,
        .hres = BOARD_LCD_H_RES,
        .vres = BOARD_LCD_V_RES,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {
            .buff_spiram = true,
            .direct_mode = true,
        },
    };
    const lvgl_port_display_rgb_cfg_t rgb = {
        .flags = {
            .bb_mode = true,
            .avoid_tearing = true,
        },
    };
    lv_display_t *d = lvgl_port_add_disp_rgb(&dcfg, &rgb);
    ESP_RETURN_ON_FALSE(d, ESP_FAIL, TAG, "lvgl display");

    lv_indev_t *in = NULL;
    if (tp) {
        const lvgl_port_touch_cfg_t tcfg = { .disp = d, .handle = tp };
        in = lvgl_port_add_touch(&tcfg);
    }
    if (lvgl_port_lock(0)) {
        lv_display_add_event_cb(d, on_flush_event, LV_EVENT_REFR_READY, NULL);
        lvgl_port_unlock();
    }
    ESP_LOGI(TAG, "display up: %ux%u, %d MHz, bounce %d lines, touch %s", BOARD_LCD_H_RES,
             BOARD_LCD_V_RES, CONFIG_BOARD_LCD_PCLK_MHZ, CONFIG_BOARD_LCD_BOUNCE_LINES,
             in ? "GT911" : "none");
    if (disp) {
        *disp = d;
    }
    if (touch) {
        *touch = in;
    }
    return ESP_OK;
}

esp_err_t board_backlight(bool on)
{
    if (!s_exp_out) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t v = on ? (s_exio | EXIO_LCD_BL) : (s_exio & ~EXIO_LCD_BL);
    return v == s_exio ? ESP_OK : exio_write(v);
}

bool board_backlight_is_on(void)
{
    return (s_exio & EXIO_LCD_BL) != 0;
}

esp_err_t board_lcd_restart(void)
{
    return s_panel ? esp_lcd_rgb_panel_restart(s_panel) : ESP_ERR_INVALID_STATE;
}

uint32_t board_flush_count(void)
{
    return s_flushes;
}
