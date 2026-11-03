cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(unveil_standalone VERSION 0.2.0)
littlefs_create_partition_image(unveil "${CMAKE_SOURCE_DIR}/resources" FLASH_IN_PROJECT)
# Keep the same checked-in vector and TE rendering improvements as the framework build.
include("${CMAKE_SOURCE_DIR}/vector_compat.cmake")

if(CONFIG_LV_USE_VECTOR_GRAPHIC AND CONFIG_LV_USE_THORVG_INTERNAL)
    idf_component_get_property(uv_lvgl_lib lvgl__lvgl COMPONENT_LIB)
    idf_component_get_property(uv_lvgl_dir lvgl__lvgl COMPONENT_DIR)
    set_property(SOURCE "${uv_lvgl_dir}/src/libs/thorvg/tvgCompressor.cpp"
        TARGET_DIRECTORY ${uv_lvgl_lib} APPEND PROPERTY COMPILE_OPTIONS -Wno-error=type-limits)
endif()
