file(GLOB FONTS "fonts/*.c")
idf_component_register(SRCS "ui.c" "ui_dash.c" "ui_zone.c" "ui_setup.c" ${FONTS}
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "."
                       REQUIRES zone_store)
