// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "esp_err.h"
/* Boot restores preferences from retained NVS before Lua starts. Thereafter
 * the coalescing persistence worker is the single snapshot writer. */
esp_err_t uv_preferences_restore(void);
esp_err_t uv_preferences_save_if_config(const char *path);
