# Desktop preview of the touchscreen UI: runs the real UI code against a fake
# zone store and writes PNG screenshots of each screen. No SDL needed.
#
#   cmake -S tools/ui_preview -B build-preview          # fetches LVGL 9.4.0
#   cmake --build build-preview && ./build-preview/ui_preview out/
#
# Pass -DLVGL_DIR=/path/to/lvgl to use a local LVGL checkout instead.
cmake_minimum_required(VERSION 3.16)
project(ui_preview C)
set(CMAKE_C_STANDARD 99)

set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
if(NOT LVGL_DIR)
  set(LVGL_DIR ${CMAKE_BINARY_DIR}/lvgl-v9.4.0)
  if(NOT EXISTS ${LVGL_DIR}/lvgl.h)
    find_package(Git REQUIRED)
    execute_process(COMMAND ${GIT_EXECUTABLE} clone --quiet --depth 1 --branch v9.4.0
                            https://github.com/lvgl/lvgl.git ${LVGL_DIR}
                    RESULT_VARIABLE rc)
    if(rc)
      message(FATAL_ERROR "Could not fetch LVGL; pass -DLVGL_DIR=/path/to/lvgl")
    endif()
  endif()
endif()

file(GLOB_RECURSE LVGL_SRC ${LVGL_DIR}/src/*.c)
add_library(lvgl_host STATIC ${LVGL_SRC})
target_include_directories(lvgl_host PUBLIC ${LVGL_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(lvgl_host PUBLIC LV_CONF_INCLUDE_SIMPLE)

file(GLOB UI_SRC ${ROOT}/components/ui/*.c ${ROOT}/components/ui/fonts/*.c)
add_executable(ui_preview main.c fake_store.c png.c ${UI_SRC}
               ${ROOT}/components/zone_store/zone_view.c
               ${ROOT}/components/hm_proto/hm_dcb.c)
target_include_directories(ui_preview PRIVATE ${ROOT}/components/ui ${ROOT}/components/ui/include
                           ${ROOT}/components/zone_store/include ${ROOT}/components/hm_proto/include
                           ${ROOT}/components/hm_sched/include)
target_link_libraries(ui_preview PRIVATE lvgl_host)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(ui_preview PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter
                         -fsanitize=address,undefined)
  target_link_options(ui_preview PRIVATE -fsanitize=address,undefined)
endif()
