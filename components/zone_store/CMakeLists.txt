idf_component_register(SRCS "zone_view.c" "zone_store.c"
                       INCLUDE_DIRS "include"
                       REQUIRES hm_proto hm_sched hm_poller nvs_flash esp_timer)
