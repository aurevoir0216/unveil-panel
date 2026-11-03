file(GLOB UV_LVGL_SOURCES "lvgl/*.c")
idf_component_register(SRCS ${UV_LVGL_SOURCES} "lua_module_json.c" "lua_module_storage.c" "lua_module_delay.c" "uv_runtime.c" "uv_jobs.c" "uv_platform.c" "uv_display.c" "uv_wifi.c" "uv_sound.c" "uv_preferences.c"
 INCLUDE_DIRS "include" "lvgl" "." REQUIRES lua lvgl esp-mosaico-bsp esp_lvgl_present espressif__esp_display_present esp_wifi esp_netif esp_event esp_http_client mbedtls nvs_flash littlefs cjson esp_timer esp_lcd esp_lcd_touch)
