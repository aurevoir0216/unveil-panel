// SPDX-License-Identifier: Apache-2.0
#include "uv_preferences.h"
#include "cJSON.h"
#include "nvs.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CONFIG_MAX_BYTES 2048
static const char *config_path = "/data/apps/unveil/config.json";
static const char *restore_path = "/data/apps/unveil/config.restore.tmp";
static const char *settings_namespace = "uv_settings";
static const char *settings_key = "config";

static bool valid_config(const char *data, size_t size)
{
    if (!size || size > CONFIG_MAX_BYTES || strlen(data) != size) return false;
    cJSON *value = cJSON_ParseWithLengthOpts(data, size + 1, NULL, true);
    bool valid = cJSON_IsObject(value);
    cJSON_Delete(value);
    return valid;
}

static esp_err_t read_config(char **out, size_t *size)
{
    *out = NULL;
    FILE *file = fopen(config_path, "rb");
    if (!file) return errno == ENOENT ? ESP_ERR_NOT_FOUND : ESP_FAIL;
    char *data = malloc(CONFIG_MAX_BYTES + 2);
    if (!data) { fclose(file); return ESP_ERR_NO_MEM; }
    size_t length = fread(data, 1, CONFIG_MAX_BYTES + 1, file);
    bool failed = ferror(file);
    if (fclose(file) != 0) failed = true;
    data[length] = 0;
    if (failed || !valid_config(data, length)) {
        free(data); return ESP_ERR_INVALID_RESPONSE;
    }
    *out = data; *size = length;
    return ESP_OK;
}

static esp_err_t read_snapshot(nvs_handle_t nvs, char **out, size_t *size)
{
    *out = NULL; *size = 0;
    esp_err_t error = nvs_get_blob(nvs, settings_key, NULL, size);
    if (error != ESP_OK) return error;
    if (!*size || *size > CONFIG_MAX_BYTES) return ESP_ERR_INVALID_SIZE;
    size_t capacity = *size;
    char *data = malloc(capacity + 1);
    if (!data) return ESP_ERR_NO_MEM;
    error = nvs_get_blob(nvs, settings_key, data, size);
    if (error != ESP_OK || *size > capacity) {
        free(data); return error == ESP_OK ? ESP_ERR_INVALID_SIZE : error;
    }
    data[*size] = 0;
    if (error == ESP_OK && !valid_config(data, *size)) error = ESP_ERR_INVALID_RESPONSE;
    if (error != ESP_OK) { free(data); return error; }
    *out = data;
    return ESP_OK;
}

esp_err_t uv_preferences_save_if_config(const char *path)
{
    if (!path || strcmp(path, config_path)) return ESP_OK;
    char *data = NULL; size_t size = 0;
    esp_err_t error = read_config(&data, &size);
    if (error != ESP_OK) return error;
    nvs_handle_t nvs;
    error = nvs_open(settings_namespace, NVS_READWRITE, &nvs);
    if (error == ESP_OK) {
        char *previous = NULL; size_t previous_size = 0;
        esp_err_t read_error = read_snapshot(nvs, &previous, &previous_size);
        bool unchanged = read_error == ESP_OK && size == previous_size && !memcmp(data, previous, size);
        free(previous);
        /* Byte equality avoids redundant flash writes when a worker retries. */
        if (!unchanged) {
            error = nvs_set_blob(nvs, settings_key, data, size);
            if (error == ESP_OK) error = nvs_commit(nvs);
        }
        nvs_close(nvs);
    }
    free(data);
    return error;
}

esp_err_t uv_preferences_restore(void)
{
    nvs_handle_t nvs;
    esp_err_t error = nvs_open(settings_namespace, NVS_READONLY, &nvs);
    if (error == ESP_ERR_NVS_NOT_FOUND) return uv_preferences_save_if_config(config_path);
    if (error != ESP_OK) return error;
    char *data = NULL; size_t size = 0;
    error = read_snapshot(nvs, &data, &size);
    nvs_close(nvs);
    if (error == ESP_ERR_NVS_NOT_FOUND) return uv_preferences_save_if_config(config_path);
    if (error != ESP_OK) return error;
    char *current = NULL; size_t current_size = 0;
    bool unchanged = read_config(&current, &current_size) == ESP_OK &&
                     size == current_size && !memcmp(data, current, size);
    free(current);
    if (!unchanged) {
        FILE *file = fopen(restore_path, "wb");
        if (!file) { free(data); return ESP_FAIL; }
        error = fwrite(data, 1, size, file) == size ? ESP_OK : ESP_FAIL;
        if (fclose(file) != 0) error = ESP_FAIL;
        if (error == ESP_OK && rename(restore_path, config_path) != 0) error = ESP_FAIL;
        if (error != ESP_OK) remove(restore_path);
    }
    free(data);
    return error;
}
