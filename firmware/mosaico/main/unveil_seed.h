// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef UNVEIL_SEED_HOST_TEST
typedef int esp_err_t;
#else
#include "esp_err.h"
#endif
esp_err_t unveil_seed_install(const uint8_t *archive,size_t size,const char *directory);
esp_err_t unveil_seed_bootstrap(void);
