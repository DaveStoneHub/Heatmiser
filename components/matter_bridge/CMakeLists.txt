idf_component_register(SRCS "matter_bridge.cpp" "matter_map.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "."
                       REQUIRES zone_store esp_matter
                       PRIV_REQUIRES nvs_flash esp_timer)
