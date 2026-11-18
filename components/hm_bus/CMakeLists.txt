idf_component_register(SRCS "hm_bus.c"
                       INCLUDE_DIRS "include"
                       REQUIRES hm_proto esp_driver_uart esp_driver_gpio esp_timer)
