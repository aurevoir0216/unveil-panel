// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
typedef enum {UV_PATH_DATA,UV_PATH_SYSTEM} uv_path_root_t;
const char *uv_paths_get(uv_path_root_t);
esp_err_t uv_paths_join(uv_path_root_t,const char*,char*,size_t);
esp_err_t uv_paths_get_space(uv_path_root_t,uint64_t*,uint64_t*);
