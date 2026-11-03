// SPDX-License-Identifier: Apache-2.0
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include <assert.h>
#include "uv_wifi_helpers.h"
extern int luaopen_json(lua_State*);
int uv_register_module(const char *name,lua_CFunction fn){(void)name;(void)fn;return 0;}
int main(int argc,char **argv){
 struct {uint8_t ssid[32];char adjacent[16];} record;
 memset(record.ssid,'A',32);memcpy(record.adjacent,"not-part-of-ssid",16);
 char name[33];uv_ssid_to_text(name,record.ssid);assert(strlen(name)==32);assert(uv_ssid_matches(name,record.ssid));name[31]=0;assert(!uv_ssid_matches(name,record.ssid));
 printf("WIFI_SSID_BOUNDARY_OK\n");
assert(sizeof(lua_Number)==4);assert(argc==3);lua_State *L=luaL_newstate();assert(L);luaL_openlibs(L);luaL_requiref(L,"json",luaopen_json,1);lua_pop(L,1);lua_pushstring(L,argv[2]);lua_setglobal(L,"GENERATION_MODULE");int error=luaL_dofile(L,argv[1]);if(error){fprintf(stderr,"%s\n",lua_tostring(L,-1));}lua_close(L);return error?1:0;}
