# Host build of the pure-C protocol library, scheduler, zone view, Matter mapping and update versions, with their tests.
#   cmake -S test/host -B build-host && cmake --build build-host && ctest --test-dir build-host --output-on-failure
cmake_minimum_required(VERSION 3.16)
project(hm_proto_host_tests C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(PROTO ${CMAKE_CURRENT_SOURCE_DIR}/../../components/hm_proto)
set(SCHED ${CMAKE_CURRENT_SOURCE_DIR}/../../components/hm_sched)
set(ZONE ${CMAKE_CURRENT_SOURCE_DIR}/../../components/zone_store)
set(MATTER ${CMAKE_CURRENT_SOURCE_DIR}/../../components/matter_bridge)
set(OTA ${CMAKE_CURRENT_SOURCE_DIR}/../../components/ota)

add_executable(hm_tests test_main.c test_write.c test_sched.c test_zone.c test_matter.c test_ota.c
               ${PROTO}/hm_crc.c ${PROTO}/hm_frame.c ${PROTO}/hm_dcb.c ${PROTO}/hm_write.c
               ${SCHED}/hm_sched.c ${ZONE}/zone_view.c ${MATTER}/matter_map.c ${OTA}/ota_ver.c)
target_include_directories(hm_tests PRIVATE ${PROTO}/include ${SCHED}/include ${ZONE}/include ${MATTER} ${OTA}/include)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(hm_tests PRIVATE -Wall -Wextra -Werror -fsanitize=address,undefined)
  target_link_options(hm_tests PRIVATE -fsanitize=address,undefined)
endif()

enable_testing()
add_test(NAME hm_proto COMMAND hm_tests)
