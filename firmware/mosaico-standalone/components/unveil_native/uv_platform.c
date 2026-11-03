// SPDX-License-Identifier: Apache-2.0
#include "uv_runtime.h"
#include "bsp/esp_mosaico.h"
#include "esp_timer.h"
#include "iot_button.h"
#include "esp_log.h"
extern void uv_wifi_init(void);
extern esp_err_t unveil_sound_init(void);
extern void unveil_scan_key_press(void);
static esp_timer_handle_t motor_timer;
static bool ignore_power_on;
static void motor_off(void *arg){(void)arg;bsp_motor_set(false);}
void uv_motor_pulse(unsigned ms){if(!motor_timer)return;esp_timer_stop(motor_timer);if(bsp_motor_set(true)==ESP_OK){esp_err_t e=esp_timer_start_once(motor_timer,(uint64_t)(ms?ms:1)*1000);if(e!=ESP_OK)bsp_motor_set(false);}}
static void down(void *button,void *ctx){(void)button;(void)ctx;if(!ignore_power_on)unveil_scan_key_press();}
static void up(void *button,void *ctx){(void)button;(void)ctx;ignore_power_on=false;}
void uv_platform_init(void){uv_wifi_init();ESP_ERROR_CHECK(bsp_motor_init());esp_timer_create_args_t cfg={.callback=motor_off,.name="uv_motor_off"};ESP_ERROR_CHECK(esp_timer_create(&cfg,&motor_timer));ESP_ERROR_CHECK(unveil_sound_init());button_handle_t buttons[BSP_BUTTON_NUM];int count=0;ESP_ERROR_CHECK(bsp_iot_button_create(buttons,&count,BSP_BUTTON_NUM));if(count>0){ignore_power_on=iot_button_get_key_level(buttons[0])!=0;ESP_ERROR_CHECK(iot_button_register_cb(buttons[0],BUTTON_PRESS_DOWN,NULL,down,NULL));ESP_ERROR_CHECK(iot_button_register_cb(buttons[0],BUTTON_PRESS_UP,NULL,up,NULL));}}
