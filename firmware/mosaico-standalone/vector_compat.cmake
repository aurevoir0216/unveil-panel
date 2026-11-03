# Project-owned LVGL/ThorVG and TE transport overrides, pinned to the bundled dependencies.
if(CONFIG_LV_USE_VECTOR_GRAPHIC AND CONFIG_LV_USE_THORVG_INTERNAL)
    idf_component_get_property(unveil_lvgl_dir lvgl__lvgl COMPONENT_DIR)
    file(READ "${unveil_lvgl_dir}/lv_version.h" unveil_lvgl_version)
    if(NOT unveil_lvgl_version MATCHES "LVGL_VERSION_MAJOR 9" OR NOT unveil_lvgl_version MATCHES "LVGL_VERSION_MINOR 5")
        message(FATAL_ERROR "Review Unveil native overrides before changing LVGL version")
    endif()
    foreach(unveil_native_path IN ITEMS
        "lvgl__lvgl/src/draw/sw/lv_draw_sw_vector.c"
        "lvgl__lvgl/src/draw/sw/lv_draw_sw.c"
        "lvgl__lvgl/src/libs/thorvg/tvgSwRle.cpp"
)
        configure_file("${CMAKE_CURRENT_LIST_DIR}/render_overrides/${unveil_native_path}"
                       "${CMAKE_SOURCE_DIR}/managed_components/${unveil_native_path}" COPYONLY)
    endforeach()
endif()
