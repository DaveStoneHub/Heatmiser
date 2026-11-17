# ESP-IDF component. Pure C: also built on the host by test/host/CMakeLists.txt.
idf_component_register(SRCS "hm_crc.c" "hm_frame.c" "hm_dcb.c" "hm_write.c"
                       INCLUDE_DIRS "include")
