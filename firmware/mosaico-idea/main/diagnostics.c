// SPDX-License-Identifier: Apache-2.0
#include "esp_iris.h"
#include "esp_iris_service_profiles.h"
#include "uv_display.h"
#include <string.h>
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
}
