// SPDX-License-Identifier: Apache-2.0
/* Standalone runtime. No Mosaico desktop, Works registry or Claw capability router. */
#include "uv_runtime.h"
#include "uv_paths.h"
#include "lua_module_lvgl.h"
#include "lua_module_json.h"
#include "lua_module_storage.h"
#include "lua_module_delay.h"
#include "lauxlib.h"
#include "lualib.h"
#include "cJSON.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_littlefs.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static struct {const char *name;lua_CFunction open;} modules[12];
static unsigned module_count;
static void (*cleanup_fn)(lua_State*);
esp_err_t uv_register_module(const char *name,lua_CFunction fn){if(module_count>=12)return ESP_ERR_NO_MEM;modules[module_count].name=name;modules[module_count++].open=fn;return ESP_OK;}
esp_err_t uv_register_exit_cleanup(void (*fn)(lua_State*)){cleanup_fn=fn;return ESP_OK;}
void uv_cleanup(lua_State *L){if(cleanup_fn)cleanup_fn(L);}
const char *uv_paths_get(uv_path_root_t root){(void)root;return "/data";}
esp_err_t uv_paths_join(uv_path_root_t root,const char *name,char *out,size_t n){(void)root;if(!name||strstr(name,".."))return ESP_ERR_INVALID_ARG;while(*name=='/')++name;int written=snprintf(out,n,"/data/%s",name);return written>=0&&(size_t)written<n?ESP_OK:ESP_ERR_INVALID_SIZE;}
esp_err_t uv_paths_get_space(uv_path_root_t root,uint64_t *total,uint64_t *free_bytes){(void)root;size_t capacity,used;esp_err_t e=esp_littlefs_info("unveil",&capacity,&used);if(e==ESP_OK){*total=capacity;*free_bytes=capacity-used;}return e;}
static int millis(lua_State *L){lua_pushinteger(L,esp_timer_get_time()/1000);return 1;}
static int info(lua_State *L){lua_newtable(L);lua_pushinteger(L,heap_caps_get_free_size(MALLOC_CAP_INTERNAL));lua_setfield(L,-2,"sram_free");lua_pushinteger(L,heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));lua_setfield(L,-2,"sram_largest");lua_pushinteger(L,heap_caps_get_free_size(MALLOC_CAP_SPIRAM));lua_setfield(L,-2,"psram_free");return 1;}
/* Diagnostics are live snapshots shared by the UI, worker and USB tasks.
 * Writing them to flash during a scan can stall flash-backed drawing. */
static const char *const status_names[] = {
    "ui-status.json", "launch-status.json", "connection-status.json",
};
static EXT_RAM_BSS_ATTR struct {
    size_t length;
    char data[2049];
} status_slots[sizeof(status_names) / sizeof(status_names[0])];
static portMUX_TYPE status_lock = portMUX_INITIALIZER_UNLOCKED;

static int status_slot(const char *name)
{
    for (size_t i = 0; i < sizeof(status_names) / sizeof(status_names[0]); ++i) {
        if (strcmp(name, status_names[i]) == 0) return (int)i;
    }
    return -1;
}

esp_err_t uv_status_read(const char *name, char *out, size_t capacity)
{
    const int slot = status_slot(name);
    if (slot < 0) return ESP_ERR_INVALID_ARG;

    portENTER_CRITICAL(&status_lock);
    const size_t length = status_slots[slot].length;
    esp_err_t error = ESP_ERR_NOT_FOUND;
    if (length) {
        error = ESP_ERR_INVALID_SIZE;
        if (length < capacity) {
            memcpy(out, status_slots[slot].data, length + 1);
            error = ESP_OK;
        }
    }
    portEXIT_CRITICAL(&status_lock);
    return error;
}

static int publish_status(lua_State *L)
{
    const int slot = status_slot(luaL_checkstring(L, 1));
    luaL_argcheck(L, slot >= 0, 1, "unknown diagnostic status");
    size_t length;
    const char *data = luaL_checklstring(L, 2, &length);
    luaL_argcheck(L, length > 0 && length < sizeof(status_slots[0].data) &&
                  !memchr(data, 0, length), 2, "invalid diagnostic payload");

    portENTER_CRITICAL(&status_lock);
    memcpy(status_slots[slot].data, data, length + 1);
    status_slots[slot].length = length;
    portEXIT_CRITICAL(&status_lock);
    return 0;
}

static int open_system(lua_State *L){luaL_Reg r[]={{"millis",millis},{"info",info},{"publish_status",publish_status},{NULL,NULL}};luaL_newlib(L,r);return 1;}
char *uv_encode(lua_State *L,int index){index=lua_absindex(L,index);luaL_requiref(L,"json",luaopen_json,0);lua_getfield(L,-1,"encode");lua_pushvalue(L,index);lua_call(L,1,1);const char *value=luaL_checkstring(L,-1);char *copy=strdup(value);lua_pop(L,2);return copy;}
void uv_decode(lua_State *L,const char *data){luaL_requiref(L,"json",luaopen_json,0);lua_getfield(L,-1,"decode");lua_pushstring(L,data);lua_call(L,1,1);lua_remove(L,-2);}
static const char *field(lua_State *L,int index,const char *name){lua_getfield(L,index,name);const char *v=lua_tostring(L,-1);lua_pop(L,1);return v;}
extern esp_err_t uv_return_to_ideas(void) __attribute__((weak));
static int call(lua_State *L){
 const char *name=luaL_checkstring(L,1);luaL_checktype(L,2,LUA_TTABLE);
 if(!strcmp(name,"system_control")){const char *op=field(L,2,"operation");if(op&&!strcmp(op,"return_to_ideas")&&uv_return_to_ideas){lua_pushboolean(L,uv_return_to_ideas()==ESP_OK);return 1;}if(!op||strcmp(op,"vibrate")){lua_pushboolean(L,0);lua_pushliteral(L,"Unsupported operation");return 2;}lua_getfield(L,2,"duration_ms");unsigned ms=(unsigned)luaL_optinteger(L,-1,60);lua_pop(L,1);uv_motor_pulse(ms>1000?1000:ms);lua_pushboolean(L,1);lua_pushliteral(L,"ok");return 2;}
 if(strcmp(name,"http_request")){lua_pushboolean(L,0);lua_pushliteral(L,"Unsupported capability");return 2;}
 const char *url=field(L,2,"url"),*body=field(L,2,"body"),*method=field(L,2,"method");
 if(!url||(strncmp(url,"http://",7)&&strncmp(url,"https://",8))){lua_pushboolean(L,0);lua_pushliteral(L,"Invalid HTTP URL");return 2;}
 lua_getfield(L,2,"timeout_ms");int timeout=(int)luaL_optinteger(L,-1,15000);lua_pop(L,1);if(timeout<100)timeout=100;if(timeout>40000)timeout=40000;
 lua_getfield(L,2,"max_body_bytes");size_t capacity=(size_t)luaL_optinteger(L,-1,65535);lua_pop(L,1);if(capacity>65535)capacity=65535;
 char *buffer=malloc(capacity+48);if(!buffer){lua_pushboolean(L,0);lua_pushliteral(L,"HTTP allocation failed");return 2;}
 esp_http_client_config_t cfg={.url=url,.timeout_ms=timeout,.crt_bundle_attach=esp_crt_bundle_attach,.buffer_size=4096,.buffer_size_tx=1024};esp_http_client_handle_t h=esp_http_client_init(&cfg);esp_err_t error=h?ESP_OK:ESP_ERR_NO_MEM;int code=0;size_t used=0;
 if(h){esp_http_client_set_header(h,"User-Agent","Mozilla/5.0 Unveil/0.2");if(body|| (method&&!strcmp(method,"POST")))esp_http_client_set_method(h,HTTP_METHOD_POST);if(body)esp_http_client_set_header(h,"Content-Type","application/json");size_t len=body?strlen(body):0;error=esp_http_client_open(h,len);if(error==ESP_OK&&len&&esp_http_client_write(h,body,len)!=(int)len)error=ESP_FAIL;
  if(error==ESP_OK&&esp_http_client_fetch_headers(h)<0){error=ESP_FAIL;}
  code=esp_http_client_get_status_code(h);
  while(error==ESP_OK&&used<capacity){int n=esp_http_client_read(h,buffer+32+used,capacity-used);if(n<0){error=ESP_FAIL;break;}if(!n)break;used+=n;}
  if(error==ESP_OK&&!esp_http_client_is_complete_data_received(h)){error=ESP_ERR_INVALID_SIZE;}
  esp_http_client_close(h);esp_http_client_cleanup(h);
 }
 if(error==ESP_OK){char header[32];int n=snprintf(header,sizeof(header),"HTTP %d\n",code);memmove(buffer+n,buffer+32,used);memcpy(buffer,header,n);lua_pushboolean(L,1);lua_pushlstring(L,buffer,n+used);}else{lua_pushboolean(L,0);lua_pushstring(L,esp_err_to_name(error));}free(buffer);return 2;
}
static int open_capability(lua_State *L){lua_newtable(L);lua_pushcfunction(L,call);lua_setfield(L,-2,"call");return 1;}
static int open_modules(lua_State *L){luaL_openlibs(L);for(unsigned i=0;i<module_count;i++){luaL_requiref(L,modules[i].name,modules[i].open,0);lua_pop(L,1);}return 0;}
/* Lua has many small strings/tables. Keep them in external RAM too, so
 * Wi-Fi, file locks and task control blocks retain internal memory. */
static void *lua_external_alloc(void *context, void *pointer, size_t old_size, size_t new_size)
{
    (void)context;
    (void)old_size;
    if (!new_size) {
        heap_caps_free(pointer);
        return NULL;
    }
    return heap_caps_realloc(pointer, new_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
lua_State *uv_new_state(void){lua_State *L=lua_newstate(lua_external_alloc,NULL,luaL_makeseed(NULL));if(!L)return NULL;lua_pushcfunction(L,open_modules);if(lua_pcall(L,0,0,0)!=LUA_OK){lua_close(L);return NULL;}return L;}
esp_err_t uv_runtime_init(void){ESP_ERROR_CHECK(lua_module_json_register());ESP_ERROR_CHECK(lua_module_storage_register());ESP_ERROR_CHECK(lua_module_delay_register());ESP_ERROR_CHECK(lua_module_lvgl_register_with_data_root("/data"));ESP_ERROR_CHECK(uv_register_module("system",open_system));ESP_ERROR_CHECK(uv_register_module("thread",uv_open_thread));return uv_register_module("capability",open_capability);}
static void ui_task(void *arg){(void)arg;lua_State *L=uv_new_state();if(!L){ESP_LOGE("unveil","Lua allocation failed");vTaskDelete(NULL);return;}ESP_LOGI("unveil","UI start internal=%u largest=%u psram=%u",(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),(unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));uv_attach_job(L,NULL);int result=luaL_dofile(L,"/data/apps/unveil/scripts/unveil.lua");if(result!=LUA_OK)ESP_LOGE("unveil","UI stopped: %s",lua_tostring(L,-1));uv_cleanup(L);lua_close(L);vTaskDelete(NULL);}
esp_err_t uv_runtime_start(void){return xTaskCreate(ui_task,"unveil_ui",24576,NULL,4,NULL)==pdPASS?ESP_OK:ESP_ERR_NO_MEM;}
