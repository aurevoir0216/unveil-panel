// SPDX-License-Identifier: Apache-2.0
#include "app_recovery.h"

#include "esp_log.h"
#include "esp_ota_ops.h"
#ifdef UNVEIL_DESKTOP_BUILD
#include "mosaico_recovery_contract.h"
#include "nvs.h"
#include "nvs_flash.h"
#endif

static esp_err_t accept_retained_boot(uint32_t address)
{
#ifdef UNVEIL_DESKTOP_BUILD
    esp_err_t err=nvs_flash_init_partition(MOSAICO_SYSMETA_PARTITION);
    if(err!=ESP_OK)return err;
    nvs_handle_t handle;
    err=nvs_open_from_partition(MOSAICO_SYSMETA_PARTITION,MOSAICO_OTA_NAMESPACE,NVS_READWRITE,&handle);
    if(err!=ESP_OK)return err;
    err=nvs_set_u32(handle,"last_good",address);
    if(err==ESP_OK)err=nvs_set_u32(handle,"target",0);
    if(err==ESP_OK)err=nvs_commit(handle);
    nvs_close(handle);return err;
#else
    (void)address;return ESP_OK;
#endif
}

esp_err_t app_recovery_mark_healthy(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running == NULL || running->subtype != ESP_PARTITION_SUBTYPE_APP_OTA_0) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_ota_img_states_t state;
    esp_err_t err = esp_ota_get_state_partition(running, &state);
    if (err == ESP_ERR_NOT_FOUND) {
        // A native first install has blank otadata and no pending OTA receipt.
        return accept_retained_boot(running->address);
    }
    if (err != ESP_OK) {
        return err;
    }
    if (state == ESP_OTA_IMG_VALID || state == ESP_OTA_IMG_UNDEFINED) {
        return accept_retained_boot(running->address);
    }
    if (state != ESP_OTA_IMG_PENDING_VERIFY) {
        return ESP_ERR_INVALID_STATE;
    }
    err = esp_ota_mark_app_valid_cancel_rollback();
    if (err == ESP_OK) {
        ESP_LOGI("app_recovery", "Claw startup confirmed on main_app");
        err=accept_retained_boot(running->address);
    }
    return err;
}
