// SPDX-License-Identifier: Apache-2.0
#include "uv_runtime.h"
#include "uv_preferences.h"
#include "esp_littlefs.h"
#include "esp_iris.h"
#include "iris_ota_support.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdatomic.h>
extern esp_err_t uv_display_init(void);
extern void unveil_diagnostics_register(void);
extern void unveil_apply_pending_preferences(void);
static atomic_bool first_frame_visible;
/* The desktop's renderer uses a PSRAM stack. Flash/MMU validation must run
 * on the internal startup stack, outside the LVGL render callback. */
void uv_first_frame_visible(void) { atomic_store(&first_frame_visible, true); }
esp_err_t uv_return_to_ideas(void) { return iris_ota_support_enter_recovery(); }
void app_main(void)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_iris_boot_probe());
    ESP_ERROR_CHECK(nvs_flash_init());
    iris_ota_support_start();
    const esp_vfs_littlefs_conf_t fs = {.base_path="/data",.partition_label="unveil",.format_if_mount_failed=false,.dont_mount=false};
    ESP_ERROR_CHECK(esp_vfs_littlefs_register(&fs));
    ESP_ERROR_CHECK_WITHOUT_ABORT(uv_preferences_restore());
    unveil_apply_pending_preferences();
    ESP_ERROR_CHECK(uv_display_init());
    unveil_diagnostics_register();
    uv_platform_init();
    ESP_ERROR_CHECK(uv_runtime_init());
    ESP_ERROR_CHECK(uv_runtime_start());
    while (!atomic_load(&first_frame_visible)) vTaskDelay(pdMS_TO_TICKS(10));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_iris_mark_healthy());
}
