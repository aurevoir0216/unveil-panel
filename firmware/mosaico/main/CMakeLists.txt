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
endif()
