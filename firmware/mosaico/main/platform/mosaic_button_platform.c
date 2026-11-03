/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "mosaic_button_platform.h"

#include <stdbool.h>

#include "esp_board_manager_includes.h"
#include "esp_check.h"
#include "esp_log.h"
#include "mosaic_ui.h"

#define MOSAIC_ACTION_BUTTON_DEVICE     "button_power"
#define MOSAIC_AI_BUTTON_LONG_PRESS_MS  500U

static const char *TAG = "mosaic_button";

#if CONFIG_ESP_BOARD_DEV_BUTTON_SUPPORT

extern bool unveil_scan_key_is_active(void);
extern void unveil_scan_key_press(void);
static bool s_unveil_press_handled;
static bool s_back_button_ready;
static bool s_action_button_registered;

static esp_err_t get_button_handle(
    const char *device_name, button_handle_t *out_handle)
{
    dev_button_handles_t *buttons = NULL;

    ESP_RETURN_ON_FALSE(device_name && out_handle, ESP_ERR_INVALID_ARG, TAG,
                        "invalid button lookup");
    ESP_RETURN_ON_ERROR(esp_board_manager_init_device_by_name(device_name), TAG, "init %s", device_name);
    ESP_RETURN_ON_ERROR(esp_board_manager_get_device_handle(device_name, (void **)&buttons), TAG, "get %s", device_name);
    ESP_RETURN_ON_FALSE(buttons && buttons->num_buttons > 0 &&
                            buttons->button_handles[0],
                        ESP_ERR_NOT_FOUND, TAG, "%s has no button handle",
                        device_name);

    *out_handle = buttons->button_handles[0];
    return ESP_OK;
}

static void back_button_down_cb(void *button_handle, void *user_data)
{
    (void)button_handle;
    (void)user_data;
    /* A fresh press after callback registration arms Back. The press used to
     * power on the board started before registration and must not turn its
     * eventual release into a Home -> Lock transition. */
    s_back_button_ready = true;
    s_unveil_press_handled = unveil_scan_key_is_active();

}

static void back_button_cb(void *button_handle, void *user_data)
{
    (void)button_handle;
    (void)user_data;
    if (!s_back_button_ready) {
        s_back_button_ready = true;
        ESP_LOGI(TAG, "ignore power-on key release");
        return;
    }

    if (s_unveil_press_handled) {s_unveil_press_handled=false;if(unveil_scan_key_is_active())unveil_scan_key_press();return;}
    esp_err_t err = mosaic_ui_back();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "queue top-key Back failed: %s", esp_err_to_name(err));
    }
}

static void ai_button_long_press_cb(void *button_handle, void *user_data)
{
    (void)button_handle;
    (void)user_data;

    esp_err_t err = mosaic_ui_open_ai_create();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "queue AI Create failed: %s", esp_err_to_name(err));
    }
}

static esp_err_t register_action_button(void)
{
    if (s_action_button_registered) {
        return ESP_OK;
    }

    button_handle_t button = NULL;
    ESP_RETURN_ON_ERROR(get_button_handle(MOSAIC_ACTION_BUTTON_DEVICE, &button),
                        TAG, "action button unavailable");

    s_back_button_ready = iot_button_get_key_level(button) == 0;
    ESP_RETURN_ON_ERROR(
        iot_button_register_cb(button, BUTTON_PRESS_DOWN, NULL,
                               back_button_down_cb, NULL),
        TAG, "register top-key press");
    ESP_RETURN_ON_ERROR(
        iot_button_register_cb(button, BUTTON_SINGLE_CLICK, NULL,
                               back_button_cb, NULL),
        TAG, "register top-key Back");

    button_event_args_t long_press = {
        .long_press.press_time = MOSAIC_AI_BUTTON_LONG_PRESS_MS,
    };
    ESP_RETURN_ON_ERROR(
        iot_button_register_cb(button, BUTTON_LONG_PRESS_START, &long_press,
                               ai_button_long_press_cb, NULL),
        TAG, "register AI button long press");

    s_action_button_registered = true;
    return ESP_OK;
}

#endif

esp_err_t mosaic_button_platform_init(void)
{
#if CONFIG_ESP_BOARD_DEV_BUTTON_SUPPORT
    ESP_RETURN_ON_ERROR(register_action_button(), TAG,
                        "register action button");
    ESP_LOGI(TAG, "button ready: Back=single-click AI Create=hold %u ms",
             MOSAIC_AI_BUTTON_LONG_PRESS_MS);
    return ESP_OK;
#else
    ESP_LOGW(TAG, "board button support is disabled");
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

#if CONFIG_ESP_BOARD_DEV_BUTTON_SUPPORT
extern void unveil_scan_key_press(void);
static bool unveil_ignore_power_on;
static void unveil_key_down(void *handle,void *ctx)
{
    (void)handle;(void)ctx;
    if(!unveil_ignore_power_on){unveil_scan_key_press();ESP_LOGI(TAG,"Unveil top-key scan");}
}
static void unveil_key_up(void *handle,void *ctx)
{
    (void)handle;(void)ctx;unveil_ignore_power_on=false;
}
#endif
esp_err_t unveil_button_platform_init(void)
{
#if CONFIG_ESP_BOARD_DEV_BUTTON_SUPPORT
    button_handle_t button=NULL;
    ESP_RETURN_ON_ERROR(get_button_handle(MOSAIC_ACTION_BUTTON_DEVICE,&button),TAG,"Unveil top-key unavailable");
    unveil_ignore_power_on=iot_button_get_key_level(button)!=0;
    ESP_RETURN_ON_ERROR(iot_button_register_cb(button,BUTTON_PRESS_UP,NULL,unveil_key_up,NULL),TAG,"register Unveil release");
    ESP_RETURN_ON_ERROR(iot_button_register_cb(button,BUTTON_PRESS_DOWN,NULL,unveil_key_down,NULL),TAG,"register Unveil press");
    ESP_LOGI(TAG,"Unveil top-key ready: press=home/scan");return ESP_OK;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}
