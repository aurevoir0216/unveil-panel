#pragma once
#include "app_settings_service.h"
#include "esp_err.h"
#include <stddef.h>
void unveil_wifi_configure(app_settings_service_handle_t settings);
esp_err_t unveil_wifi_command(const char *operation,const char *ssid,const char *password,char *out,size_t capacity);
