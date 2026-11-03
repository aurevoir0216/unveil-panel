// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "lvgl.h"
typedef struct uv_display_session *uv_display_session_handle_t;
typedef struct {uint32_t buffer_lines,tick_ms,task_period_ms;} uv_display_config_t;
typedef struct {const char *owner_name;int mode;uint32_t flags;uv_display_config_t display_config;void (*cleanup_cb)(uv_display_session_handle_t,void*);void (*exit_request_cb)(uv_display_session_handle_t,void*);void *user_ctx;} uv_display_session_config_t;
#define UV_DISPLAY_MODE_EXCLUSIVE_LVGL 1
#define UV_DISPLAY_SESSION_FLAG_RESTORE_DEFAULT_ON_RELEASE 1
#define UV_DISPLAY_SESSION_FLAG_ALLOW_SYSTEM_OVERLAY 2
esp_err_t uv_display_lock(void);
void uv_display_unlock(void);
bool uv_display_has_exclusive_session(void);
esp_err_t uv_display_open(const uv_display_session_config_t*,uv_display_session_handle_t*);
esp_err_t uv_display_close(uv_display_session_handle_t);
lv_display_t *uv_display_session_display(uv_display_session_handle_t);
esp_err_t uv_display_session_load_screen_locked(uv_display_session_handle_t,lv_obj_t*);

/* Bounded diagnostics used by the Idea adapter; physical touch takes priority. */
esp_err_t uv_display_capture_begin(void);
esp_err_t uv_display_capture_read(size_t,void*,size_t,size_t*);
void uv_display_capture_end(void);
esp_err_t uv_display_pointer(uint16_t,uint16_t,bool);
