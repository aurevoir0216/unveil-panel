// SPDX-License-Identifier: Apache-2.0
/* One UI owner, bounded asynchronous worker slots and a bounded message queue. */
#include "uv_runtime.h"
#include "lauxlib.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define UV_WORKERS 8
static const char registry_key;
typedef struct {atomic_int status;atomic_bool stop;char id[32],name[64],path[256];char *args;int64_t deadline;unsigned serial;} job_t;
static job_t jobs[UV_WORKERS];
static portMUX_TYPE guard=portMUX_INITIALIZER_UNLOCKED;
static unsigned sequence;
static QueueHandle_t messages;
static char queue_name[64];
static size_t message_limit=2048;
void uv_attach_job(lua_State *L,void *job){lua_pushlightuserdata(L,(void*)&registry_key);lua_pushlightuserdata(L,job);lua_rawset(L,LUA_REGISTRYINDEX);}
static job_t *current(lua_State *L){lua_pushlightuserdata(L,(void*)&registry_key);lua_rawget(L,LUA_REGISTRYINDEX);job_t *j=lua_touserdata(L,-1);lua_pop(L,1);return j;}
bool uv_runtime_stop_requested(lua_State *L){job_t *j=current(L);return j&&(atomic_load(&j->stop)||(j->deadline&&esp_timer_get_time()>j->deadline));}
const char *uv_runtime_job_id(lua_State *L){job_t *j=current(L);return j?j->id:"unveil_ui";}
static void hook(lua_State *L,lua_Debug *ar){(void)ar;if(uv_runtime_stop_requested(L))luaL_error(L,"worker cancelled or timed out");}
static int run_job(lua_State *L){job_t *j=current(L);uv_decode(L,j->args);lua_setglobal(L,"args");if(luaL_loadfile(L,j->path)!=LUA_OK)return lua_error(L);lua_call(L,0,0);return 0;}
static void execute(void *argument){job_t *j=argument;lua_State *L=uv_new_state();int result=L?LUA_OK:LUA_ERRMEM;if(L){uv_attach_job(L,j);lua_sethook(L,hook,LUA_MASKCOUNT,10000);atomic_store(&j->status,2);lua_pushcfunction(L,run_job);result=lua_pcall(L,0,0,0);if(result!=LUA_OK)ESP_LOGW("unveil_worker","%s: %s",j->name,lua_tostring(L,-1));uv_cleanup(L);lua_close(L);}free(j->args);j->args=NULL;atomic_store(&j->status,result==LUA_OK?3:4);vTaskDelete(NULL);}
static job_t *find(const char *name){job_t *found=NULL;for(int i=0;i<UV_WORKERS;i++)if((!strcmp(jobs[i].name,name)||!strcmp(jobs[i].id,name))&&(!found||jobs[i].serial>found->serial))found=&jobs[i];return found;}
static int start(lua_State *L){const char *path=luaL_checkstring(L,1);luaL_checktype(L,2,LUA_TTABLE);luaL_checktype(L,3,LUA_TTABLE);lua_getfield(L,3,"name");const char *name=luaL_optstring(L,-1,"worker");if(strlen(path)>=256||strlen(name)>=64)return luaL_error(L,"worker path or name too long");char *args=uv_encode(L,2);if(!args)return luaL_error(L,"worker arguments allocation failed");lua_getfield(L,3,"timeout_ms");int ms=luaL_optinteger(L,-1,40000);lua_pop(L,1);job_t *j=NULL;
 portENTER_CRITICAL(&guard);job_t *existing=find(name);if(!existing||atomic_load(&existing->status)>=3){for(int i=0;i<UV_WORKERS;i++)if(atomic_load(&jobs[i].status)==0||atomic_load(&jobs[i].status)>=3){j=&jobs[i];break;}}
 if(j){j->serial=++sequence;snprintf(j->id,sizeof(j->id),"uv%08u",j->serial);strlcpy(j->name,name,sizeof(j->name));strlcpy(j->path,path,sizeof(j->path));j->args=args;j->deadline=esp_timer_get_time()+(int64_t)(ms>0?ms:40000)*1000;atomic_store(&j->stop,false);atomic_store(&j->status,1);}portEXIT_CRITICAL(&guard);
 if(!j){free(args);lua_pushboolean(L,0);lua_pushliteral(L,"worker busy");return 2;}
 if(xTaskCreate(execute,j->name,16384,j,3,NULL)!=pdPASS){free(j->args);j->args=NULL;atomic_store(&j->status,4);lua_pushboolean(L,0);lua_pushliteral(L,"worker allocation failed");return 2;}
 lua_pushboolean(L,1);lua_pushfstring(L,"job_id=%s status=queued",j->id);return 2;
}
static int get(lua_State *L){const char *name=luaL_checkstring(L,1);portENTER_CRITICAL(&guard);job_t *j=find(name);int state=j?atomic_load(&j->status):0;portEXIT_CRITICAL(&guard);static const char *states[]={"unknown","queued","running","done","failed"};lua_pushboolean(L,j!=NULL);lua_pushfstring(L,"status=%s",states[state]);return 2;}
static int stop(lua_State *L){const char *name=luaL_checkstring(L,1);portENTER_CRITICAL(&guard);job_t *j=find(name);if(j)atomic_store(&j->stop,true);portEXIT_CRITICAL(&guard);lua_pushboolean(L,j!=NULL);return 1;}
static int create_queue(lua_State *L){const char *name=luaL_checkstring(L,1);luaL_checktype(L,2,LUA_TTABLE);if(messages){lua_pushboolean(L,0);lua_pushliteral(L,"exists");return 2;}lua_getfield(L,2,"depth");unsigned depth=luaL_optinteger(L,-1,4);lua_pop(L,1);lua_getfield(L,2,"item_size");size_t limit=luaL_optinteger(L,-1,2048);lua_pop(L,1);luaL_argcheck(L,depth>0&&depth<=16&&limit>0&&limit<=65536,2,"queue bounds");messages=xQueueCreate(depth,sizeof(char*));if(messages){strlcpy(queue_name,name,sizeof(queue_name));message_limit=limit;}lua_pushboolean(L,messages!=NULL);return 1;}
static int delete_queue(lua_State *L){const char *name=luaL_checkstring(L,1);if(messages&&!strcmp(name,queue_name)){char *value;while(xQueueReceive(messages,&value,0)==pdTRUE)free(value);vQueueDelete(messages);messages=NULL;queue_name[0]=0;}lua_pushboolean(L,1);return 1;}
static int send(lua_State *L){const char *name=luaL_checkstring(L,1);size_t len;const char *data=luaL_checklstring(L,2,&len);int wait=luaL_optinteger(L,3,0);if(!messages||strcmp(name,queue_name)||len>=message_limit){lua_pushboolean(L,0);return 1;}char *copy=malloc(len+1);if(!copy){lua_pushboolean(L,0);return 1;}memcpy(copy,data,len);copy[len]=0;bool sent=xQueueSend(messages,&copy,pdMS_TO_TICKS(wait>0?wait:0))==pdTRUE;if(!sent)free(copy);lua_pushboolean(L,sent);return 1;}
static int recv(lua_State *L){const char *name=luaL_checkstring(L,1);int wait=luaL_optinteger(L,2,0);char *data=NULL;if(messages&&!strcmp(name,queue_name)&&xQueueReceive(messages,&data,pdMS_TO_TICKS(wait>0?wait:0))==pdTRUE){lua_pushstring(L,data);free(data);}else lua_pushnil(L);return 1;}
int uv_open_thread(lua_State *L){luaL_Reg r[]={{"start",start},{"get",get},{"stop",stop},{NULL,NULL}};luaL_newlib(L,r);lua_newtable(L);lua_pushcfunction(L,create_queue);lua_setfield(L,-2,"queue_create");lua_pushcfunction(L,delete_queue);lua_setfield(L,-2,"queue_delete");lua_pushcfunction(L,send);lua_setfield(L,-2,"queue_send");lua_pushcfunction(L,recv);lua_setfield(L,-2,"queue_recv");lua_setfield(L,-2,"sync");return 1;}
