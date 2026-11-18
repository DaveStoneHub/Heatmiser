# WHOLE_ARCHIVE: LVGL itself calls lv_malloc_core() and friends from
# lv_mem_psram.c, and the linker would otherwise not pull them in.
idf_component_register(SRCS "board.c" "lv_mem_psram.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_lcd esp_driver_i2c esp_driver_gpio
                       PRIV_REQUIRES esp_timer
                       WHOLE_ARCHIVE)
