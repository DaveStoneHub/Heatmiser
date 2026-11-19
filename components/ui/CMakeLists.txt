file(GLOB FONTS "fonts/*.c")
idf_component_register(SRCS "ui.c" "ui_dash.c" "ui_zone.c" "ui_sched.c" "ui_hw.c" "ui_rename.c"
                            "ui_settings.c" "ui_zones.c" "ui_diag.c" "ui_system.c" "ui_matter.c" ${FONTS}
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "."
                       REQUIRES zone_store)
