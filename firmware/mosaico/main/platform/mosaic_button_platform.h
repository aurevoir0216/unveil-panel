/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "esp_err.h"

/** Bind the board buttons to Mosaic UI navigation actions. */
esp_err_t mosaic_button_platform_init(void);

/** Dedicated Unveil: fresh press returns home and requests scanning. */
esp_err_t unveil_button_platform_init(void);
