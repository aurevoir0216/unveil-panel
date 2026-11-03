idf_component_register(
    SRC_DIRS "." "platform"
    INCLUDE_DIRS "." "platform"
    PRIV_INCLUDE_DIRS "../third-party/esp-mosaico-utils/esp-mosaico-vibe-mode/include"
    PRIV_REQUIRES
        cap_lua
        cap_http_request
        cap_system
        app_update
        app_settings_service
        app_system_config
        audio_hub
        esp_codec_dev
        claw_hw_registry
        claw_event_router
        cmd_wifi
        esp_app_format
        esp_hw_support
        esp_lcd_touch_mux
        esp_wifi
        espressif__cjson
        gen_bmgr_codes
        mosaico_camera_service
        mosaico_bsp_bmgr_bridge
        mosaico_boot_splash
        nvs_flash
        weather_service
        gmf_core
)

if(UNVEIL_DISTRIBUTION STREQUAL "desktop")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE UNVEIL_DESKTOP_BUILD=1)
    file(GLOB_RECURSE UNVEIL_SEED_INPUTS CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/app/scripts/*.lua" "${CMAKE_SOURCE_DIR}/app/assets/*" "${CMAKE_SOURCE_DIR}/app/launcher.json")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${UNVEIL_SEED_INPUTS})
    set(UNVEIL_APP_SEED "${CMAKE_CURRENT_BINARY_DIR}/unveil_app_seed.bin")
    execute_process(COMMAND ${PYTHON} "${CMAKE_SOURCE_DIR}/tools/stage_unveil_app.py"
        --app "${CMAKE_SOURCE_DIR}/app" --archive "${UNVEIL_APP_SEED}"
        RESULT_VARIABLE UNVEIL_SEED_RESULT)
    if(NOT UNVEIL_SEED_RESULT EQUAL 0)
        message(FATAL_ERROR "Unveil desktop seed generation failed")
    endif()
    target_add_binary_data(${COMPONENT_LIB} "${UNVEIL_APP_SEED}" BINARY)
endif()
