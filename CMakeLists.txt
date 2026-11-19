# Heatmiser Matter bridge firmware (ESP-IDF 5.x).
# RS485 poller, zone store and touchscreen.
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(heatmiser_bridge)

# GCC 15 (ESP-IDF 6.x) flags string truncation inside connectedhomeip that
# older compilers did not; keep those warnings but not as errors there.
idf_build_get_property(build_components BUILD_COMPONENTS)
if("espressif__esp_matter" IN_LIST build_components)
  idf_component_get_property(matter_lib espressif__esp_matter COMPONENT_LIB)
  target_compile_options(${matter_lib} PRIVATE -Wno-error=stringop-truncation -Wno-error=stringop-overflow)
endif()
