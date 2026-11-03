// SPDX-License-Identifier: Apache-2.0
#include "esp_wifi.h"
#include "uv_wifi_helpers.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_netif_sntp.h"
#include "nvs.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
static SemaphoreHandle_t lock;
static wifi_config_t current_config;
static wifi_ap_record_t aps[32];
static uint16_t count;
static unsigned revision;
static atomic_bool enabled=true,connected=false,scanning=false,applying=false;
static esp_netif_t *netif;
static void event(void *arg,esp_event_base_t base,int32_t id,void *data){(void)arg;(void)data;if(base==WIFI_EVENT&&id==WIFI_EVENT_STA_DISCONNECTED){atomic_store(&connected,false);if(atomic_load(&enabled)&&!atomic_load(&applying)&&current_config.sta.ssid[0])esp_wifi_connect();}else if(base==IP_EVENT&&id==IP_EVENT_STA_GOT_IP)atomic_store(&connected,true);}
void uv_wifi_init(void){lock=xSemaphoreCreateMutex();ESP_ERROR_CHECK(lock?ESP_OK:ESP_ERR_NO_MEM);ESP_ERROR_CHECK(esp_netif_init());ESP_ERROR_CHECK(esp_event_loop_create_default());netif=esp_netif_create_default_wifi_sta();ESP_ERROR_CHECK(netif?ESP_OK:ESP_ERR_NO_MEM);wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT();ESP_ERROR_CHECK(esp_wifi_init(&init));ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
 bool saved=false;nvs_handle_t nvs;if(nvs_open("uv_wifi",NVS_READONLY,&nvs)==ESP_OK){size_t size=sizeof(current_config);saved=nvs_get_blob(nvs,"credentials",&current_config,&size)==ESP_OK&&size==sizeof(current_config);if(!saved)memset(&current_config,0,sizeof(current_config));uint8_t on=1;nvs_get_u8(nvs,"enabled",&on);atomic_store(&enabled,on!=0);nvs_close(nvs);}
 /* Import this device's existing desktop connection once; no credentials are
  * compiled into the application, and the desktop namespace stays untouched. */
 if(!saved&&nvs_open("app",NVS_READONLY,&nvs)==ESP_OK){
  char ssid[33]={0},password[65]={0};size_t ssid_size=sizeof(ssid),password_size=sizeof(password);
  if(nvs_get_str(nvs,"wifi_ssid",ssid,&ssid_size)==ESP_OK&&ssid[0]&&ssid_size<=sizeof(ssid)){
   esp_err_t password_error=nvs_get_str(nvs,"wifi_password",password,&password_size);
   if(password_error==ESP_OK||password_error==ESP_ERR_NVS_NOT_FOUND){memcpy(current_config.sta.ssid,ssid,strlen(ssid));memcpy(current_config.sta.password,password,strlen(password));saved=true;}
  }
  nvs_close(nvs);
  if(saved&&nvs_open("uv_wifi",NVS_READWRITE,&nvs)==ESP_OK){if(nvs_set_blob(nvs,"credentials",&current_config,sizeof(current_config))==ESP_OK)nvs_commit(nvs);nvs_close(nvs);}
 }
 ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,event,NULL));ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,event,NULL));
 ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA,&current_config));if(atomic_load(&enabled)){ESP_ERROR_CHECK(esp_wifi_start());if(current_config.sta.ssid[0])ESP_ERROR_CHECK(esp_wifi_connect());}
 esp_sntp_config_t time_config=ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");ESP_ERROR_CHECK(esp_netif_sntp_init(&time_config));
}
static void scan(void *arg){(void)arg;wifi_scan_config_t config={0};uint16_t n=32;wifi_ap_record_t *found=calloc(32,sizeof(*found));esp_err_t e=found?esp_wifi_scan_start(&config,true):ESP_ERR_NO_MEM;if(e==ESP_OK)e=esp_wifi_scan_get_ap_records(&n,found);if(e==ESP_OK){xSemaphoreTake(lock,portMAX_DELAY);memcpy(aps,found,n*sizeof(*found));count=n;++revision;xSemaphoreGive(lock);}free(found);atomic_store(&scanning,false);vTaskDelete(NULL);}
esp_err_t unveil_wifi_command(const char *op,const char *ssid,const char *password,char *out,size_t capacity){
 esp_err_t error=ESP_OK;
 if(!strcmp(op,"scan")){bool expected=false;if(atomic_compare_exchange_strong(&scanning,&expected,true)){if(!atomic_load(&enabled))error=ESP_ERR_INVALID_STATE;else if(xTaskCreate(scan,"uv_wifi_scan",4096,NULL,3,NULL)!=pdPASS)error=ESP_ERR_NO_MEM;if(error!=ESP_OK)atomic_store(&scanning,false);}}
 else if(!strcmp(op,"enable")){bool on=ssid&&!strcmp(ssid,"1");atomic_store(&enabled,on);error=on?esp_wifi_start():esp_wifi_stop();if(!on)atomic_store(&connected,false);if(error==ESP_OK&&on&&current_config.sta.ssid[0])error=esp_wifi_connect();nvs_handle_t n;if(nvs_open("uv_wifi",NVS_READWRITE,&n)==ESP_OK){nvs_set_u8(n,"enabled",on);nvs_commit(n);nvs_close(n);}}
 else if(!strcmp(op,"connect")){
  if(!ssid||!ssid[0]||strlen(ssid)>32||(password&&strlen(password)>63))error=ESP_ERR_INVALID_ARG;
  else{wifi_config_t *cfg=calloc(1,sizeof(*cfg));if(!cfg)error=ESP_ERR_NO_MEM;else{memcpy(cfg->sta.ssid,ssid,strlen(ssid));if(password)strlcpy((char*)cfg->sta.password,password,sizeof(cfg->sta.password));else if(uv_ssid_matches(ssid,current_config.sta.ssid))memcpy(cfg->sta.password,current_config.sta.password,sizeof(cfg->sta.password));atomic_store(&applying,true);esp_wifi_disconnect();error=esp_wifi_set_config(WIFI_IF_STA,cfg);if(error==ESP_OK){current_config=*cfg;atomic_store(&enabled,true);error=esp_wifi_start();if(error==ESP_OK)error=esp_wifi_connect();}if(error==ESP_OK){nvs_handle_t n;error=nvs_open("uv_wifi",NVS_READWRITE,&n);if(error==ESP_OK){error=nvs_set_blob(n,"credentials",cfg,sizeof(*cfg));if(error==ESP_OK)error=nvs_set_u8(n,"enabled",1);if(error==ESP_OK)error=nvs_commit(n);nvs_close(n);}}atomic_store(&applying,false);free(cfg);}}
 }else if(strcmp(op,"status"))error=ESP_ERR_INVALID_ARG;
 cJSON *root=cJSON_CreateObject(),*list=cJSON_CreateArray();if(!root||!list){cJSON_Delete(root);cJSON_Delete(list);return ESP_ERR_NO_MEM;}cJSON_AddBoolToObject(root,"ok",error==ESP_OK);cJSON_AddStringToObject(root,"error",error==ESP_OK?"":esp_err_to_name(error));cJSON_AddBoolToObject(root,"enabled",atomic_load(&enabled));cJSON_AddBoolToObject(root,"connected",atomic_load(&connected));cJSON_AddBoolToObject(root,"scanning",atomic_load(&scanning));char ssid_text[33];uv_ssid_to_text(ssid_text,current_config.sta.ssid);cJSON_AddStringToObject(root,"ssid",ssid_text);cJSON_AddStringToObject(root,"state",atomic_load(&connected)?"connected":"disconnected");esp_netif_ip_info_t ip={0};esp_netif_get_ip_info(netif,&ip);char address[24];snprintf(address,sizeof(address),IPSTR,IP2STR(&ip.ip));cJSON_AddStringToObject(root,"ip",address);
 xSemaphoreTake(lock,portMAX_DELAY);cJSON_AddNumberToObject(root,"revision",revision);for(int i=0;i<count;i++){cJSON *a=cJSON_CreateObject();cJSON_AddStringToObject(a,"ssid",(char*)aps[i].ssid);cJSON_AddNumberToObject(a,"rssi",aps[i].rssi);cJSON_AddBoolToObject(a,"secured",aps[i].authmode!=WIFI_AUTH_OPEN);cJSON_AddItemToArray(list,a);}xSemaphoreGive(lock);cJSON_AddItemToObject(root,"aps",list);bool ok=cJSON_PrintPreallocated(root,out,capacity,false);cJSON_Delete(root);return ok?ESP_OK:ESP_ERR_NO_MEM;
}
