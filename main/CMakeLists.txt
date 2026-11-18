idf_component_register(SRCS "main.c" "bench_cmds.c" "soak.c" "poll_cmds.c"
                       INCLUDE_DIRS "."
                       REQUIRES hm_proto hm_sched hm_bus hm_poller console esp_timer)
