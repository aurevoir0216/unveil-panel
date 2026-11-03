// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "lua.h"
#define UV_JOB_ID_LEN 32
esp_err_t uv_register_module(const char*,lua_CFunction);
esp_err_t uv_register_exit_cleanup(void (*fn)(lua_State*));
bool uv_runtime_stop_requested(lua_State*);
const char *uv_runtime_job_id(lua_State*);
esp_err_t uv_runtime_init(void);
esp_err_t uv_runtime_start(void);
esp_err_t uv_status_read(const char *name, char *out, size_t capacity);
lua_State *uv_new_state(void);
char *uv_encode(lua_State*,int);
void uv_decode(lua_State*,const char*);
void uv_attach_job(lua_State*,void*);
void uv_cleanup(lua_State*);
int uv_open_thread(lua_State*);
void uv_platform_init(void);
void uv_motor_pulse(unsigned);
