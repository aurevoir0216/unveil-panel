idf_component_register(SRCS "main.c" "diagnostics.c" "display_band.c" "display_pipeline.c" INCLUDE_DIRS "." REQUIRES unveil_native littlefs nvs_flash esp_iris esp_mosaico_app_recovery cjson PRIV_REQUIRES esp_driver_ppa)

idf_component_get_property(present_dir espressif__esp_display_present COMPONENT_DIR)
idf_component_get_property(lvgl_dir lvgl__lvgl COMPONENT_DIR)
target_include_directories(${COMPONENT_LIB} PRIVATE "${present_dir}/private_include" "${lvgl_dir}/src/draw/sw/blend")
target_link_libraries(${COMPONENT_LIB} INTERFACE
    "-Wl,--wrap=present_async_copy_submit" "-Wl,-u,__wrap_present_async_copy_submit"
    "-Wl,--wrap=lv_draw_sw_blend_color_to_rgb565" "-Wl,-u,__wrap_lv_draw_sw_blend_color_to_rgb565"
    "-Wl,-u,unveil_te_enqueue")
