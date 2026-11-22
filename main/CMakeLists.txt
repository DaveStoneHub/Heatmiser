idf_component_register(SRCS "main.c" "bench_cmds.c" "soak.c" "poll_cmds.c"
                            "ui_platform.c" "ui_cmds.c" "matter_cmds.c"
                       INCLUDE_DIRS "."
                       REQUIRES hm_proto hm_sched hm_bus hm_poller zone_store board ui net matter_bridge
                                console esp_timer nvs_flash esp_app_format lwip)
