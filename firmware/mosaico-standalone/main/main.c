// SPDX-License-Identifier: Apache-2.0
#include "uv_runtime.h"
extern esp_err_t uv_display_init(void);
#include "bsp/esp_mosaico.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "nvs_flash.h"
void app_main(void)
{
    /* A failed mount/config is reported, never silently erased or reformatted. */
    ESP_ERROR_CHECK(nvs_flash_init());
    const esp_vfs_littlefs_conf_t fs = {.base_path="/data",.partition_label="unveil",.format_if_mount_failed=false,.dont_mount=false};
    ESP_ERROR_CHECK(esp_vfs_littlefs_register(&fs));
    ESP_ERROR_CHECK(uv_display_init());
    uv_platform_init();
    ESP_ERROR_CHECK(uv_runtime_init());
    ESP_ERROR_CHECK(uv_runtime_start());
}
