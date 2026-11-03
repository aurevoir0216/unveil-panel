// SPDX-License-Identifier: Apache-2.0
#include "esp_iris.h"
#include "esp_iris_service_profiles.h"
#include "uv_display.h"
#include "uv_preferences.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UNVEIL_DIAGNOSTIC_SERVICE 0x554eU
#define UNVEIL_CONFIG_MAX 2048U
static const char *config_path="/data/apps/unveil/config.json";
static const char *next_config_path="/data/apps/unveil/config.next.json";
static esp_err_t validate_preferences(cJSON *value)
{
 if(!cJSON_IsObject(value))return ESP_ERR_INVALID_ARG;
 const cJSON *v;
 cJSON_ArrayForEach(v,value){
  const char *key=v->string;if(!key)return ESP_ERR_INVALID_ARG;
  if(!strcmp(key,"dark")||!strcmp(key,"history_newest")||!strcmp(key,"sound_muted")){if(!cJSON_IsBool(v))return ESP_ERR_INVALID_ARG;}
  else if(!strcmp(key,"language")){if(!cJSON_IsString(v)||(strcmp(v->valuestring,"en")&&strcmp(v->valuestring,"zh")))return ESP_ERR_INVALID_ARG;}
  else if(!strcmp(key,"pi_url")){
   if(!cJSON_IsString(v)||strlen(v->valuestring)>384)return ESP_ERR_INVALID_ARG;
   if(v->valuestring[0]&&strncmp(v->valuestring,"http://",7)&&strncmp(v->valuestring,"https://",8))return ESP_ERR_INVALID_ARG;
   for(const unsigned char *p=(const unsigned char*)v->valuestring;*p;++p)if(*p<32||*p==127)return ESP_ERR_INVALID_ARG;
  }else if(!strcmp(key,"mode")){if(!cJSON_IsString(v)||(strcmp(v->valuestring,"scan")&&strcmp(v->valuestring,"collect")))return ESP_ERR_INVALID_ARG;}
  else if(!strcmp(key,"replay_job")){if(!cJSON_IsString(v)||v->valuestring[0])return ESP_ERR_INVALID_ARG;}
  else if(!strcmp(key,"capture_seconds")||!strcmp(key,"analysis_seconds")||!strcmp(key,"discovery_min_rounds")||!strcmp(key,"sound_volume")){
   if(!cJSON_IsNumber(v)||!isfinite(v->valuedouble))return ESP_ERR_INVALID_ARG;
   double min=!strcmp(key,"capture_seconds")?.5:!strcmp(key,"discovery_min_rounds")?1:0;
   double max=!strcmp(key,"sound_volume")?100:!strcmp(key,"discovery_min_rounds")?5:10;
   if(v->valuedouble<min||v->valuedouble>max)return ESP_ERR_INVALID_ARG;
  }else return ESP_ERR_INVALID_ARG;
 }
 return ESP_OK;
}
/* Stage preferences for next boot, so the live UI's asynchronous persistence
 * cannot overwrite an incoming USB configuration before the restart. */
static esp_err_t stage_preferences(cJSON *value)
{
 esp_err_t error=validate_preferences(value);if(error!=ESP_OK)return error;
 char *data=cJSON_PrintUnformatted(value);if(!data)return ESP_ERR_NO_MEM;
 size_t length=strlen(data);if(length>UNVEIL_CONFIG_MAX){free(data);return ESP_ERR_INVALID_SIZE;}
 FILE *file=fopen("/data/apps/unveil/config.next.tmp","wb");if(!file){free(data);return ESP_FAIL;}
 error=fwrite(data,1,length,file)==length?ESP_OK:ESP_FAIL;
 if(fclose(file)!=0)error=ESP_FAIL;
 free(data);
 if(error==ESP_OK&&rename("/data/apps/unveil/config.next.tmp",next_config_path)!=0)error=ESP_FAIL;
 if(error!=ESP_OK)remove("/data/apps/unveil/config.next.tmp");
 return error;
}
void unveil_apply_pending_preferences(void)
{
 FILE *file=fopen(next_config_path,"rb");if(!file)return;
 char data[UNVEIL_CONFIG_MAX+1];size_t size=fread(data,1,UNVEIL_CONFIG_MAX,file);bool oversized=fgetc(file)!=EOF;fclose(file);data[size]=0;
 cJSON *value=oversized?NULL:cJSON_Parse(data);
 esp_err_t error=validate_preferences(value);cJSON_Delete(value);
 if(error==ESP_OK&&rename(next_config_path,config_path)==0){ESP_ERROR_CHECK_WITHOUT_ABORT(uv_preferences_save_if_config(config_path));ESP_LOGI("unveil_config","Pending preferences applied");}
 else{ESP_LOGW("unveil_config","Invalid or unwritable pending preferences");remove(next_config_path);}
}
static esp_err_t configuration(const esp_iris_rpc_request_t *request,uint8_t *out,size_t capacity,size_t *size,void *context)
{
 (void)context;
 if(request->payload_size>UNVEIL_CONFIG_MAX)return ESP_ERR_INVALID_SIZE;
 cJSON *body=cJSON_ParseWithLength((const char*)request->payload,request->payload_size),*reply=cJSON_CreateObject();
 if(!reply){cJSON_Delete(body);return ESP_ERR_NO_MEM;}
 const cJSON *operation=body?cJSON_GetObjectItemCaseSensitive(body,"operation"):NULL;
 const char *op=cJSON_IsString(operation)?operation->valuestring:"";esp_err_t error=ESP_ERR_INVALID_ARG;
 if(!strcmp(op,"settings")){
  error=stage_preferences(cJSON_GetObjectItemCaseSensitive(body,"settings"));
  if(error==ESP_OK)cJSON_AddBoolToObject(reply,"restart_required",true);
 }else if(!strcmp(op,"read")){
  const cJSON *name=cJSON_GetObjectItemCaseSensitive(body,"name");
  if(cJSON_IsString(name)&&(!strcmp(name->valuestring,"config.json")||!strcmp(name->valuestring,"ui-status.json")||!strcmp(name->valuestring,"launch-status.json"))){
   char path[96];snprintf(path,sizeof(path),"/data/apps/unveil/%s",name->valuestring);
   char *data=heap_caps_malloc(UNVEIL_CONFIG_MAX+1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
   error=data?ESP_ERR_NOT_FOUND:ESP_ERR_NO_MEM;
   FILE *file=data?fopen(path,"rb"):NULL;
   if(file){size_t n=fread(data,1,UNVEIL_CONFIG_MAX,file);bool oversized=fgetc(file)!=EOF;fclose(file);data[n]=0;cJSON *value=oversized?NULL:cJSON_Parse(data);error=value?ESP_OK:ESP_ERR_INVALID_RESPONSE;if(value)cJSON_AddItemToObject(reply,"data",value);}
   heap_caps_free(data);
  }
 }else if(!strcmp(op,"scan_key")){extern void unveil_scan_key_press(void);unveil_scan_key_press();error=ESP_OK;}
 cJSON_AddBoolToObject(reply,"ok",error==ESP_OK);cJSON_AddStringToObject(reply,"error",error==ESP_OK?"":esp_err_to_name(error));
 bool fits=cJSON_PrintPreallocated(reply,(char*)out,capacity,false);cJSON_Delete(reply);cJSON_Delete(body);
 if(!fits)return ESP_ERR_NO_MEM;
 *size=strlen((char*)out);return ESP_OK;
}
static esp_err_t begin(const esp_iris_media_desc_t *requested,esp_iris_media_desc_t *actual,uint32_t *size,void *context)
{
 (void)requested;(void)context;
 esp_err_t error=uv_display_capture_begin();if(error!=ESP_OK)return error;
 *actual=(esp_iris_media_desc_t){.width=480,.height=480,.stride=960,.format=ESP_IRIS_PIXEL_FORMAT_RGB565};
 *size=480*480*2;return ESP_OK;
}
static esp_err_t read_pixels(uint32_t offset,uint8_t *out,size_t capacity,size_t *size,void *context)
{(void)context;return uv_display_capture_read(offset,out,capacity,size);}
static void end(void *context){(void)context;uv_display_capture_end();}
static esp_err_t pointer(const esp_iris_rpc_request_t *request,uint8_t *response,size_t capacity,size_t *size,void *context)
{
 (void)context;
 if(request->payload_size!=ESP_IRIS_POINTER_MESSAGE_SIZE||capacity<ESP_IRIS_POINTER_MESSAGE_SIZE||request->payload[0]>2)return ESP_ERR_INVALID_SIZE;
 uint16_t x=(uint16_t)request->payload[2]|((uint16_t)request->payload[3]<<8);
 uint16_t y=(uint16_t)request->payload[4]|((uint16_t)request->payload[5]<<8);
 esp_err_t error=uv_display_pointer(x,y,request->payload[0]!=2);if(error!=ESP_OK)return error;
 memcpy(response,request->payload,ESP_IRIS_POINTER_MESSAGE_SIZE);*size=ESP_IRIS_POINTER_MESSAGE_SIZE;return ESP_OK;
}
void unveil_diagnostics_register(void)
{
 const esp_iris_screen_backend_t screen={.begin=begin,.read=read_pixels,.end=end};
 ESP_ERROR_CHECK(esp_iris_screen_register(&screen));
 ESP_ERROR_CHECK(esp_iris_rpc_register(ESP_IRIS_POINTER_SERVICE_ID,ESP_IRIS_POINTER_METHOD_ID,pointer,NULL));
 ESP_ERROR_CHECK(esp_iris_rpc_register(UNVEIL_DIAGNOSTIC_SERVICE,1,configuration,NULL));
}
