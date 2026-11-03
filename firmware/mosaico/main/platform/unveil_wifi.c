/* Unveil view adapter over Mosaico's existing configuration and Wi-Fi manager. */
#include "unveil_wifi.h"
#include "app_config.h"
#include "wifi_manager.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>
#define UV_WIFI_APS 32
static app_settings_service_handle_t s_settings;
static portMUX_TYPE s_lock=portMUX_INITIALIZER_UNLOCKED;
static wifi_manager_scan_record_t s_aps[UV_WIFI_APS];
static uint16_t s_count;
static uint32_t s_revision;
static bool s_scanning;
static esp_err_t s_scan_error;
void unveil_wifi_configure(app_settings_service_handle_t settings) {s_settings=settings;}
static void scan_task(void *arg)
{
    (void)arg;
    wifi_manager_scan_record_t *records=calloc(UV_WIFI_APS,sizeof(*records));uint16_t count=0;
    esp_err_t err=records ? wifi_manager_scan_aps(records,UV_WIFI_APS,&count) : ESP_ERR_NO_MEM;
    portENTER_CRITICAL(&s_lock);
    if (err==ESP_OK) {memcpy(s_aps,records,count*sizeof(*records));s_count=count;}
    s_scan_error=err;s_scanning=false;++s_revision;
    portEXIT_CRITICAL(&s_lock);free(records);vTaskDelete(NULL);
}
static esp_err_t request_scan(void)
{
    portENTER_CRITICAL(&s_lock);bool busy=s_scanning;if (!busy) s_scanning=true;portEXIT_CRITICAL(&s_lock);
    if (busy) return ESP_OK;
    if (xTaskCreate(scan_task,"unveil_wifi_scan",4096,NULL,3,NULL)!=pdPASS) {
        portENTER_CRITICAL(&s_lock);s_scanning=false;s_scan_error=ESP_ERR_NO_MEM;portEXIT_CRITICAL(&s_lock);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
static esp_err_t set_enabled(bool enabled)
{
    esp_err_t err=app_settings_service_set_wifi_enabled(s_settings,enabled);
    return err==ESP_OK ? wifi_manager_set_enabled(enabled) : err;
}
static esp_err_t connect_network(const char *ssid,const char *password)
{
    if (!ssid || !ssid[0] || strlen(ssid)>32) return ESP_ERR_INVALID_ARG;
    app_config_t *config=malloc(sizeof(*config));if (!config) return ESP_ERR_NO_MEM;
    esp_err_t err=app_config_load(config);
    if (err==ESP_OK) {
        bool same_ssid=!strcmp(ssid,config->wifi_ssid);
        if (!password && !same_ssid) password="";
        bool unchanged=same_ssid && (!password || !strcmp(password,config->wifi_password));
        strlcpy(config->wifi_ssid,ssid,sizeof(config->wifi_ssid));
        if (password) strlcpy(config->wifi_password,password,sizeof(config->wifi_password));
        err=app_config_validate_wifi(config,NULL);
        if (err==ESP_OK) err=app_config_save(config);
        wifi_manager_status_t current={0};wifi_manager_get_status(&current);
        if (err==ESP_OK && !(unchanged && current.sta_connected)) {
            err=set_enabled(true);
            if (err==ESP_OK) err=wifi_manager_apply_sta_config(&(wifi_manager_config_t){
                .sta_ssid=config->wifi_ssid,.sta_password=config->wifi_password,
                .ap_ssid=config->ap_ssid[0] ? config->ap_ssid : NULL,
                .ap_password=config->ap_password[0] ? config->ap_password : NULL,
                .ap_behavior=config->ap_behavior});
        }
    }
    free(config);return err;
}
esp_err_t unveil_wifi_command(const char *operation,const char *ssid,const char *password,char *out,size_t capacity)
{
    esp_err_t err=ESP_OK;
    if (!strcmp(operation,"scan")) err=request_scan();
    else if (!strcmp(operation,"enable")) err=set_enabled(ssid && !strcmp(ssid,"1"));
    else if (!strcmp(operation,"connect")) err=connect_network(ssid,password);
    else if (strcmp(operation,"status")) err=ESP_ERR_INVALID_ARG;
    wifi_manager_status_t status={0};wifi_manager_get_status(&status);
    wifi_manager_scan_record_t *records=calloc(UV_WIFI_APS,sizeof(*records));if (!records) return ESP_ERR_NO_MEM;
    portENTER_CRITICAL(&s_lock);
    uint16_t count=s_count;uint32_t revision=s_revision;bool scanning=s_scanning;esp_err_t scan_error=s_scan_error;
    memcpy(records,s_aps,count*sizeof(*records));portEXIT_CRITICAL(&s_lock);
    cJSON *root=cJSON_CreateObject(),*aps=cJSON_CreateArray();
    if (!root || !aps) {cJSON_Delete(root);cJSON_Delete(aps);free(records);return ESP_ERR_NO_MEM;}
    cJSON_AddBoolToObject(root,"ok",err==ESP_OK);cJSON_AddStringToObject(root,"error",err==ESP_OK ? "" : esp_err_to_name(err));
    cJSON_AddBoolToObject(root,"enabled",status.enabled);cJSON_AddBoolToObject(root,"connected",status.sta_connected);
    cJSON_AddStringToObject(root,"ssid",status.sta_ssid ? status.sta_ssid : "");
    cJSON_AddStringToObject(root,"ip",status.sta_ip ? status.sta_ip : "");
    cJSON_AddStringToObject(root,"state",wifi_manager_state_string(status.state));
    cJSON_AddBoolToObject(root,"scanning",scanning);cJSON_AddNumberToObject(root,"revision",revision);
    cJSON_AddNumberToObject(root,"scan_error",scan_error);
    for (int i=0;i<count;++i) {
        if (!records[i].ssid[0]) continue;
        cJSON *ap=cJSON_CreateObject();cJSON_AddStringToObject(ap,"ssid",records[i].ssid);
        cJSON_AddNumberToObject(ap,"rssi",records[i].rssi);cJSON_AddBoolToObject(ap,"secured",records[i].authmode!=WIFI_AUTH_OPEN);
        cJSON_AddItemToArray(aps,ap);
    }
    cJSON_AddItemToObject(root,"aps",aps);bool good=cJSON_PrintPreallocated(root,out,(int)capacity,false);
    cJSON_Delete(root);free(records);return good ? ESP_OK : ESP_ERR_NO_MEM;
}
