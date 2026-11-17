idf_component_register(SRCS "main.c" "bench_cmds.c" "soak.c"
                       INCLUDE_DIRS "."
                       REQUIRES hm_proto hm_bus console esp_timer)
