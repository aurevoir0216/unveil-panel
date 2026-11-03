file(GLOB UV_LVGL_SOURCES "lvgl/*.c")
idf_component_register(SRCS ${UV_LVGL_SOURCES} "lua_module_json.c" "lua_module_storage.c" "lua_module_delay.c" "uv_runtime.c" "uv_jobs.c" "uv_platform.c" "uv_display.c" "uv_wifi.c" "uv_sound.c"
 INCLUDE_DIRS "include" "lvgl" "." REQUIRES lua lvgl esp-mosaico-bsp esp_wifi esp_netif esp_event esp_http_client mbedtls nvs_flash littlefs cjson esp_timer esp_lcd esp_lcd_touch)
