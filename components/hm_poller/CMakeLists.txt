idf_component_register(SRCS "hm_poller.c"
                       INCLUDE_DIRS "include"
                       REQUIRES hm_proto hm_sched hm_bus esp_timer)
