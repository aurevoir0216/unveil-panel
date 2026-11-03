cmake_minimum_required(VERSION 3.16)
if(NOT MOSAICO_UTILS_ROOT)
    if(DEFINED ENV{MOSAICO_UTILS_ROOT})
        set(MOSAICO_UTILS_ROOT "$ENV{MOSAICO_UTILS_ROOT}")
    else()
        set(MOSAICO_UTILS_ROOT "${CMAKE_CURRENT_LIST_DIR}/../mosaico/third-party/esp-mosaico-utils")
    endif()
endif()
if(NOT UV_SHARED_ROOT)
    if(DEFINED ENV{UV_SHARED_ROOT})
        set(UV_SHARED_ROOT "$ENV{UV_SHARED_ROOT}")
    else()
        set(UV_SHARED_ROOT "${CMAKE_CURRENT_LIST_DIR}/../mosaico-core")
    endif()
endif()
if(NOT UV_DESKTOP_ROOT)
    if(DEFINED ENV{UV_DESKTOP_ROOT})
        set(UV_DESKTOP_ROOT "$ENV{UV_DESKTOP_ROOT}")
    else()
        set(UV_DESKTOP_ROOT "${CMAKE_CURRENT_LIST_DIR}/../mosaico")
    endif()
endif()
set(SDKCONFIG_DEFAULTS "${UV_SHARED_ROOT}/sdkconfig.defaults;${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")
list(APPEND EXTRA_COMPONENT_DIRS "${UV_SHARED_ROOT}/components/unveil_native"
    "${UV_SHARED_ROOT}/components/esp-mosaico-bsp"
    "${UV_DESKTOP_ROOT}/components/esp_lvgl_present"
    "${MOSAICO_UTILS_ROOT}/ESP-Iris/components/esp_iris"
    "${MOSAICO_UTILS_ROOT}/esp-mosaico-vibe-mode/components/esp_mosaico_app_recovery")
include("${MOSAICO_UTILS_ROOT}/esp-mosaico-vibe-mode/cmake/mosaico_idf_project.cmake")
project(unveil_idea VERSION 0.2.2)
file(COPY "${UV_SHARED_ROOT}/resources/" DESTINATION "${CMAKE_BINARY_DIR}/resources")
configure_file("${CMAKE_SOURCE_DIR}/resources_overrides/unveil.lua" "${CMAKE_BINARY_DIR}/resources/apps/unveil/scripts/unveil.lua" COPYONLY)
add_custom_target(unveil_idea_resources ALL
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${CMAKE_BINARY_DIR}/resources"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${UV_SHARED_ROOT}/resources" "${CMAKE_BINARY_DIR}/resources"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/resources_overrides/unveil.lua" "${CMAKE_BINARY_DIR}/resources/apps/unveil/scripts/unveil.lua")
littlefs_create_partition_image(unveil "${CMAKE_BINARY_DIR}/resources" FLASH_IN_PROJECT)
add_dependencies(littlefs_unveil_bin unveil_idea_resources)
set_property(GLOBAL APPEND PROPERTY MOSAICO_SYSTEM_UPDATE_DATA_LABELS "unveil")
set_property(GLOBAL PROPERTY MOSAICO_SYSTEM_UPDATE_DATA_unveil_IMAGE "${CMAKE_BINARY_DIR}/unveil.bin")
set_property(GLOBAL PROPERTY MOSAICO_SYSTEM_UPDATE_DATA_unveil_TARGET littlefs_unveil_bin)
# Shared native LVGL compatibility fixes, resolved against this project's dependency directory.
include("${UV_SHARED_ROOT}/vector_compat.cmake")
# Use the desktop's exact TE compose transport, without linking its desktop or
# application services. Both distributions keep one owner for this code.
configure_file("${UV_DESKTOP_ROOT}/tools/unveil_native_overrides/espressif__esp_display_present/src/producer/stage/present_te_transport.c"
    "${CMAKE_SOURCE_DIR}/managed_components/espressif__esp_display_present/src/producer/stage/present_te_transport.c" COPYONLY)
if(CONFIG_LV_USE_VECTOR_GRAPHIC AND CONFIG_LV_USE_THORVG_INTERNAL)
    idf_component_get_property(uv_lvgl_lib lvgl__lvgl COMPONENT_LIB)
    idf_component_get_property(uv_lvgl_dir lvgl__lvgl COMPONENT_DIR)
    set_property(SOURCE "${uv_lvgl_dir}/src/libs/thorvg/tvgCompressor.cpp" TARGET_DIRECTORY ${uv_lvgl_lib} APPEND PROPERTY COMPILE_OPTIONS -Wno-error=type-limits)
endif()
include("${MOSAICO_UTILS_ROOT}/mosaico-tools/cmake/system_update.cmake")
