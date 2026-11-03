idf_component_register(SRCS "main.c" "diagnostics.c" INCLUDE_DIRS "." REQUIRES unveil_native littlefs nvs_flash esp_iris esp_mosaico_app_recovery)
