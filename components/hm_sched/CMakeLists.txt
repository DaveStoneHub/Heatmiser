# ESP-IDF component. Pure C: also built on the host by test/host/CMakeLists.txt.
idf_component_register(SRCS "hm_sched.c"
                       INCLUDE_DIRS "include"
                       REQUIRES hm_proto)
