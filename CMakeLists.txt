# Heatmiser Matter bridge firmware (ESP-IDF 5.x).
# RS485 poller, zone store and touchscreen.
cmake_minimum_required(VERSION 3.16)

# On Windows a process command line is limited to 32,767 characters. Each
# Matter source file is compiled with several hundred include directories,
# which passes that limit when the repo sits in a long path, and ninja stops
# with "CreateProcess: The parameter is incorrect". Response files keep the
# flags out of the command line.
if(CMAKE_HOST_WIN32)
  set(CMAKE_NINJA_FORCE_RESPONSE_FILE ON CACHE BOOL "" FORCE)
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(heatmiser_bridge)

# GCC 15 (ESP-IDF 6.x) flags string truncation inside connectedhomeip that
# older compilers did not; keep those warnings but not as errors there.
idf_build_get_property(build_components BUILD_COMPONENTS)
if("espressif__esp_matter" IN_LIST build_components)
  idf_component_get_property(matter_lib espressif__esp_matter COMPONENT_LIB)
  target_compile_options(${matter_lib} PRIVATE -Wno-error=stringop-truncation -Wno-error=stringop-overflow)
  # The bridge's own name in a controller (Basic Information VendorName and
  # ProductName); the Matter SDK's defaults are TEST_VENDOR and TEST_PRODUCT.
  target_compile_definitions(${matter_lib} PRIVATE
    "CHIP_DEVICE_CONFIG_DEVICE_VENDOR_NAME=\"Heatmiser\""
    "CHIP_DEVICE_CONFIG_DEVICE_PRODUCT_NAME=\"Heatmiser Bridge\"")
endif()
