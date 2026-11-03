/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "lua_lvgl_private.h"

static const char *TAG = "lua_lvgl";
lua_lvgl_state_t s_lvgl;

static char s_lvgl_job_id[UV_JOB_ID_LEN];

static unsigned s_batch_depth;
static uint32_t s_frame_sequence;
static struct {
    int64_t window_start, frame_start, total_us, max_us;
    uint32_t frames;
} s_render_stats;

static void lua_lvgl_profile_cb(lv_event_t *event)
{
    const int64_t now = esp_timer_get_time();
    if (lv_event_get_code(event) == LV_EVENT_RENDER_START) {
        s_render_stats.frame_start = now;
    } else if (s_render_stats.frame_start != 0) {
        const int64_t duration = now - s_render_stats.frame_start;
        s_render_stats.total_us += duration;
        if (duration > s_render_stats.max_us) s_render_stats.max_us = duration;
        ++s_render_stats.frames;
        ++s_frame_sequence;
        s_render_stats.frame_start = 0;
    }
}

static int lua_lvgl_render_stats(lua_State *L)
{
    const bool reset = lua_toboolean(L, 1);
    if (!s_lvgl.runtime_initialized || s_lvgl.runtime_owner != L)
        return luaL_error(L, "render_stats requires the active LVGL owner");
    esp_err_t err = lua_lvgl_lock();
    if (err != ESP_OK) return lua_lvgl_error_esp(L, "render_stats", err);
    const int64_t now = esp_timer_get_time();
    const uint32_t frames = s_render_stats.frames;
    const int64_t total = s_render_stats.total_us, maximum = s_render_stats.max_us;
    const int64_t elapsed = now - s_render_stats.window_start;
    if (reset) { memset(&s_render_stats, 0, sizeof(s_render_stats)); s_render_stats.window_start = now; }
    extern void unveil_present_profile(uint64_t out[6],bool reset);
    extern void unveil_te_profile(uint64_t out[6],bool reset);
    extern void unveil_te_scanout_profile(uint64_t out[4], bool reset) __attribute__((weak));
    uint64_t present[6],tx[6];unveil_present_profile(present,reset);unveil_te_profile(tx,reset);
    uint64_t scanout[4] = {0};
    if (unveil_te_scanout_profile) unveil_te_scanout_profile(scanout, reset);
    lua_lvgl_unlock();
    lua_createtable(L, 0, 21);
    lua_pushinteger(L, s_frame_sequence); lua_setfield(L, -2, "frame_sequence");
    lua_pushinteger(L, frames); lua_setfield(L, -2, "frames");
    lua_pushinteger(L, frames ? total / frames : 0); lua_setfield(L, -2, "mean_us");
    lua_pushinteger(L, maximum); lua_setfield(L, -2, "max_us");
    lua_pushinteger(L, elapsed / 1000); lua_setfield(L, -2, "elapsed_ms");
    const char *present_names[]={"acquire_us","submit_us","commit_us","flush_pixels","flush_regions","present_frames"};
    const char *tx_names[]={"tx_frames","tx_bytes","dma_wait_us","te_wait_us","tx_min_rows","tx_max_rows"};
    for(int i=0;i<6;++i){lua_pushinteger(L,present[i]);lua_setfield(L,-2,present_names[i]);lua_pushinteger(L,tx[i]);lua_setfield(L,-2,tx_names[i]);}
    const char *scanout_names[] = {"dma_frames", "dma_max_gap_us", "dma_late_frames", "dma_span_us"};
    for (int i = 0; i < 4; ++i) {
        lua_pushinteger(L, scanout[i]);
        lua_setfield(L, -2, scanout_names[i]);
    }
    return 1;
}

static int lua_lvgl_present_status(lua_State *L)
{
    if(!s_lvgl.runtime_initialized||s_lvgl.runtime_owner!=L)return luaL_error(L,"present_status requires UI owner");
    esp_err_t err=lua_lvgl_lock();if(err!=ESP_OK)return lua_lvgl_error_esp(L,"present status",err);
    extern bool unveil_present_state(lv_display_t*,uint64_t out[4]);
    uint64_t value[4]={0};bool ok=unveil_present_state(s_lvgl.display,value);uint32_t sequence=s_frame_sequence;
    lua_lvgl_unlock();lua_createtable(L,0,6);
    lua_pushboolean(L,ok);lua_setfield(L,-2,"ok");
    const char *keys[]={"generation","submitted","transferred","presented"};
    for(int i=0;i<4;++i){lua_pushinteger(L,value[i]);lua_setfield(L,-2,keys[i]);}
    lua_pushinteger(L,sequence);lua_setfield(L,-2,"sequence");return 1;
}

static int lua_lvgl_set_gesture_options(lua_State *L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    int limit = lua_lvgl_get_opt_int_field(L, 1, "scroll_limit", 32);
    int inertia = lua_lvgl_get_opt_int_field(L, 1, "scroll_throw", 25);
    luaL_argcheck(L, limit >= 8 && limit <= 100, 1, "invalid scroll limit");
    luaL_argcheck(L, inertia >= 1 && inertia <= 100, 1, "invalid scroll throw");
    esp_err_t err = lua_lvgl_lock();
    if (err != ESP_OK) return lua_lvgl_error_esp(L, "gesture options", err);
    lv_indev_t *indev = NULL;
    while ((indev = lv_indev_get_next(indev)) != NULL) {
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER && lv_indev_get_display(indev) == s_lvgl.display) {
            lv_indev_set_scroll_limit(indev, limit);
            lv_indev_set_scroll_throw(indev, inertia);
        }
    }
    lua_lvgl_unlock(); return 0;
}

static int lua_lvgl_batch(lua_State *L)
{
    luaL_checktype(L, 1, LUA_TFUNCTION);
    if (!s_lvgl.runtime_initialized || s_lvgl.runtime_owner != L)
        return luaL_error(L, "batch requires the active LVGL owner");
    /* Hold only the recursive display lock; widget calls take their own record lock. */
    esp_err_t err = uv_display_lock();
    if (err != ESP_OK) return lua_lvgl_error_esp(L, "batch", err);
    ++s_batch_depth;
    int result = lua_pcall(L, lua_gettop(L) - 1, 0, 0);
    --s_batch_depth;
    uv_display_unlock();
    if (result != LUA_OK) return lua_error(L);
    return 0;
}

esp_err_t lua_lvgl_lock(void)
{
    esp_err_t err;

    /* A presenter can be active before Lua owns an LVGL session. */
    if (uv_display_has_exclusive_session()) {
        err = uv_display_lock();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "display service lock failed: %s", esp_err_to_name(err));
            return err;
        }
    }
    if (!s_lvgl.mutex) {
        s_lvgl.mutex = xSemaphoreCreateMutex();
    }
    if (!s_lvgl.mutex) {
        if (uv_display_has_exclusive_session()) {
            uv_display_unlock();
        }
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(s_lvgl.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        if (uv_display_has_exclusive_session()) {
            uv_display_unlock();
        }
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

void lua_lvgl_unlock(void)
{
    lua_lvgl_sweep_records_locked();
    if (s_lvgl.mutex) {
        xSemaphoreGive(s_lvgl.mutex);
    }
    if (uv_display_has_exclusive_session()) {
        uv_display_unlock();
    }
}

int lua_lvgl_error_esp(lua_State *L, const char *what, esp_err_t err)
{
    return luaL_error(L, "lvgl %s failed: %s", what, esp_err_to_name(err));
}

static esp_err_t lua_lvgl_quiesce_runtime(void)
{
    esp_err_t err = lua_lvgl_lock();

    if (err != ESP_OK) {
        return err;
    }
    if (s_lvgl.runtime_initialized) {
        s_lvgl.runtime_initialized = false;
        /* The Lua LVGL session shares the global LVGL runtime with system UI. Do not call lv_anim_delete_all() here; object-bound animations are cleaned up when their objects are deleted. */
    }
    lua_lvgl_unlock();
    return ESP_OK;
}

static void lua_lvgl_drain_event_queue_locked(void)
{
    /* After lv_display_delete + invalidate_records, every record's events
     * list has already been emptied via record_release_resources. Anything
     * still sitting in the event queue is a `dead` sub waiting for the
     * script task to free it. We do that here, while still on the script
     * task and still holding the lock. */
    while (s_lvgl.event_queue_head) {
        lua_lvgl_event_sub_t *sub = s_lvgl.event_queue_head;

        s_lvgl.event_queue_head = sub->queue_next;
        sub->queue_next = NULL;
        sub->queued = false;
        lua_lvgl_queue_pending_unref_locked(sub->callback_ref);
        sub->callback_ref = LUA_NOREF;
        free(sub);
    }
    s_lvgl.event_queue_tail = NULL;
}

static void lua_lvgl_delete_owned_objects_locked(void)
{
    if (s_lvgl.root_screen != NULL && lv_obj_is_valid(s_lvgl.root_screen)) {
        lv_obj_delete(s_lvgl.root_screen);
        s_lvgl.root_screen = NULL;
    }

    for (lua_lvgl_obj_record_t *record = s_lvgl.records; record != NULL; record = record->next) {
        if (record->generation == s_lvgl.generation &&
                record->owned &&
                record->valid &&
                record->obj != NULL &&
                lv_obj_is_valid(record->obj)) {
            lv_obj_delete(record->obj);
        }
    }
}

static void lua_lvgl_release_runtime_locked(void)
{
    /* Snapshot the owner before deleting LVGL objects: LV_EVENT_DELETE fills
     * pending_unrefs, and those refs must be drained against the live Lua
     * state while we are still on the script task. */
    lua_State *owner = s_lvgl.runtime_owner;

    lua_lvgl_delete_owned_objects_locked();
    lua_lvgl_invalidate_records_locked();
    lua_lvgl_sweep_records_locked();
    lua_lvgl_release_fonts_locked();
    lua_lvgl_drain_event_queue_locked();
    lua_lvgl_drain_pending_unrefs_locked(owner);
    heap_caps_free(s_lvgl.draw_buf);
    s_lvgl.draw_buf = NULL;
    s_lvgl.draw_buf_size = 0;
    s_lvgl.panel = NULL;
    s_lvgl.io = NULL;
    s_lvgl.width = 0;
    s_lvgl.height = 0;
    s_lvgl.panel_if = LUA_MODULE_LVGL_PANEL_IF_IO;
    s_lvgl.display = NULL;
    s_lvgl.root_screen = NULL;
    s_lvgl.runtime_initialized = false;
    s_lvgl.runtime_owner = NULL;
    s_lvgl.flush_callbacks_registered = false;
    s_lvgl.flush_pending = false;
    s_lvgl.generation++;
}

static void lua_lvgl_session_cleanup_cb(uv_display_session_handle_t session, void *user_ctx)
{
    (void)session;
    (void)user_ctx;

    if (s_lvgl.mutex && xSemaphoreTake(s_lvgl.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "lvgl session cleanup lock timeout");
        return;
    }
    lua_lvgl_release_runtime_locked();
    s_lvgl_job_id[0] = '\0';
    if (s_lvgl.mutex) {
        xSemaphoreGive(s_lvgl.mutex);
    }
}

esp_err_t lua_lvgl_deinit_runtime(void)
{
    esp_err_t err;
    uv_display_session_handle_t session = s_lvgl.display_session;

    err = lua_lvgl_quiesce_runtime();
    if (err != ESP_OK) {
        return err;
    }

    if (session != NULL) {
        err = uv_display_close(session);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "display session close failed: %s", esp_err_to_name(err));
            return err;
        }
        s_lvgl.display_session = NULL;
    } else {
        err = lua_lvgl_lock();
        if (err != ESP_OK) {
            return err;
        }
        lua_lvgl_release_runtime_locked();
        lua_lvgl_unlock();
    }
    return ESP_OK;
}
static int lua_lvgl_init(lua_State *L)
{
    int buffer_lines = LUA_MODULE_LVGL_DEFAULT_BUFFER_LINES;
    int tick_ms = LUA_MODULE_LVGL_DEFAULT_TICK_MS;
    int task_period_ms = LUA_MODULE_LVGL_DEFAULT_TASK_PERIOD_MS;
    const char *job_id = uv_runtime_job_id(L);
    lv_display_t *display = NULL;
    lv_obj_t *root_screen = NULL;
    uv_display_session_handle_t session = NULL;
    esp_err_t err;

    if (lua_lvgl_opt_table(L, 6)) {
        buffer_lines = lua_lvgl_get_opt_int_field(L, 6, "buffer_lines", buffer_lines);
        tick_ms = lua_lvgl_get_opt_int_field(L, 6, "tick_ms", tick_ms);
        task_period_ms = lua_lvgl_get_opt_int_field(L, 6, "task_period_ms", task_period_ms);
    } else if (lua_lvgl_opt_table(L, 1)) {
        buffer_lines = lua_lvgl_get_opt_int_field(L, 1, "buffer_lines", buffer_lines);
        tick_ms = lua_lvgl_get_opt_int_field(L, 1, "tick_ms", tick_ms);
        task_period_ms = lua_lvgl_get_opt_int_field(L, 1, "task_period_ms", task_period_ms);
    }
    luaL_argcheck(L, buffer_lines > 0, 1, "buffer_lines must be positive");
    luaL_argcheck(L, tick_ms > 0, 6, "tick_ms must be positive");
    luaL_argcheck(L, task_period_ms > 0, 6, "task_period_ms must be positive");
    if (job_id == NULL) {
        return luaL_error(L, "lvgl init requires an asynchronous Lua job context");
    }

    if (!s_lvgl.mutex) {
        s_lvgl.mutex = xSemaphoreCreateMutex();
        if (!s_lvgl.mutex) {
            return lua_lvgl_error_esp(L, "create mutex", ESP_ERR_NO_MEM);
        }
    }

    err = lua_lvgl_lock();
    if (err != ESP_OK) {
        return lua_lvgl_error_esp(L, "lock", err);
    }
    if (s_lvgl.runtime_initialized) {
        lua_lvgl_unlock();
        return luaL_error(L, "lvgl runtime is already initialized");
    }
    lua_lvgl_unlock();

    strlcpy(s_lvgl_job_id, job_id, sizeof(s_lvgl_job_id));
    err = uv_display_open(&(uv_display_session_config_t) {
        .owner_name = "lua_lvgl",
        .mode = UV_DISPLAY_MODE_EXCLUSIVE_LVGL,
        .flags = UV_DISPLAY_SESSION_FLAG_RESTORE_DEFAULT_ON_RELEASE |
                 UV_DISPLAY_SESSION_FLAG_ALLOW_SYSTEM_OVERLAY,
        .cleanup_cb = lua_lvgl_session_cleanup_cb,
        .exit_request_cb = NULL,
        .display_config = {
            .buffer_lines = (uint32_t)buffer_lines,
            .tick_ms = (uint32_t)tick_ms,
            .task_period_ms = (uint32_t)task_period_ms,
        },
    }, &session);
    if (err != ESP_OK) {
        s_lvgl_job_id[0] = '\0';
        return lua_lvgl_error_esp(L, "open display session", err);
    }
    display = uv_display_session_display(session);
    if (display == NULL) {
        (void)uv_display_close(session);
        return luaL_error(L, "display service LVGL display is NULL");
    }

    err = lua_lvgl_lock();
    if (err != ESP_OK) {
        (void)uv_display_close(session);
        return lua_lvgl_error_esp(L, "lock", err);
    }
    err = lua_lvgl_register_fs_locked();
    if (err != ESP_OK) {
        lua_lvgl_unlock();
        (void)uv_display_close(session);
        return lua_lvgl_error_esp(L, "register fs", err);
    }
    root_screen = lv_obj_create(NULL);
    if (root_screen == NULL) {
        lua_lvgl_unlock();
        (void)uv_display_close(session);
        return luaL_error(L, "lvgl root screen create failed");
    }
    err = uv_display_session_load_screen_locked(session, root_screen);
    if (err != ESP_OK) {
        lv_obj_delete(root_screen);
        lua_lvgl_unlock();
        (void)uv_display_close(session);
        return lua_lvgl_error_esp(L, "load root screen", err);
    }

    s_lvgl.display_session = session;
    s_lvgl.width = (int)lv_display_get_horizontal_resolution(display);
    s_lvgl.height = (int)lv_display_get_vertical_resolution(display);
    s_lvgl.tick_ms = (uint32_t)tick_ms;
    s_lvgl.task_period_ms = (uint32_t)task_period_ms;
    s_lvgl.display = display;
    s_lvgl.root_screen = root_screen;
    memset(&s_render_stats, 0, sizeof(s_render_stats));
    s_render_stats.window_start = esp_timer_get_time();
    lv_display_add_event_cb(display, lua_lvgl_profile_cb, LV_EVENT_RENDER_START, NULL);
    lv_display_add_event_cb(display, lua_lvgl_profile_cb, LV_EVENT_RENDER_READY, NULL);
    s_lvgl.runtime_initialized = true;
    s_lvgl.runtime_owner = L;
    s_lvgl.task_stop = false;
    lua_lvgl_unlock();

    lua_pushboolean(L, 1);
    return 1;
}

static int lua_lvgl_deinit(lua_State *L)
{
    esp_err_t err;

    if (s_lvgl.runtime_initialized && s_lvgl.runtime_owner != L) {
        return luaL_error(L, "lvgl runtime is owned by another Lua runtime");
    }

    if (s_batch_depth) return luaL_error(L, "deinit is not allowed inside batch");
    err = lua_lvgl_deinit_runtime();

    if (err != ESP_OK) {
        return lua_lvgl_error_esp(L, "deinit", err);
    }
    lua_pushboolean(L, 1);
    return 1;
}
void lua_lvgl_exit_cleanup(lua_State *L)
{
    /* Single-script subsystem: only the runtime owner triggers a deinit on
     * exit. Non-owner Lua states are expected to never create LVGL objects
     * per the single-script rule (see RFC-single-script-ui.md), so no
     * per-state object cleanup is performed here. */
    if (!L) {
        return;
    }
    if (s_lvgl.runtime_initialized && s_lvgl.runtime_owner == L) {
        ESP_LOGI(TAG, "Lua exit cleanup: deinitializing lvgl owned by exiting state");
        (void)lua_lvgl_deinit_runtime();
    }
}

const luaL_Reg lua_lvgl_runtime_funcs[] = {
    {"scan_key",lua_lvgl_scan_key},
    {"ui_tree",lua_lvgl_ui_tree},
    {"present_ready",lua_lvgl_present_ready},
    {"wifi",lua_lvgl_wifi},
    {"sound",lua_lvgl_sound},
    {"chart_publish",lua_lvgl_chart_publish},
    {"chart_clear",lua_lvgl_chart_clear},
    {"screen_rgb565",lua_lvgl_screen_rgb565},
    {"chart_rgb565", lua_lvgl_chart_rgb565},
    {"guard_tap", lua_lvgl_guard_tap},
    {"signal_rgb565", lua_lvgl_signal_rgb565},
    {"set_gesture_options", lua_lvgl_set_gesture_options},
    {"batch", lua_lvgl_batch},
    {"render_stats", lua_lvgl_render_stats},
    {"present_status",lua_lvgl_present_status},
    {"init", lua_lvgl_init},
    {"deinit", lua_lvgl_deinit},
    {NULL, NULL},
};
