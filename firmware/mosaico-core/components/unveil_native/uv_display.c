// SPDX-License-Identifier: Apache-2.0
/* Idea uses the desktop's esp_lvgl_present and measured TE composer.
 * This file only supplies board handles, direct boot and on-demand diagnostics. */
#include "uv_display.h"
#include "bsp/esp_mosaico.h"
#include "esp_display_present.h"
#include "esp_lv_adapter.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_lcd_touch.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DISPLAY_SIDE 480
#define DISPLAY_BYTES (DISPLAY_SIDE * DISPLAY_SIDE * 2U)
struct uv_display_session { uv_display_session_config_t config; bool active; };
static struct uv_display_session session;
static esp_display_presenter_t *presenter;
static esp_lcd_touch_handle_t touch;
static lv_display_t *display;
static lv_obj_t *pending_screen, *boot_screen;
static lv_font_t *boot_font;
static void *boot_font_data;
static bool boot_visible, first_frame_ready, handoff_pending;
static uint64_t handoff_frame;
static SemaphoreHandle_t capture_done;
static uint8_t *capture_pixels;
static bool capture_requested, capture_drawing;
static esp_err_t capture_result;
static bool injected_pressed, physical_pressed;
static uint16_t injected_x, injected_y, physical_x, physical_y;
static int64_t last_physical_touch, injection_time;
static atomic_uint touch_sequence;
static unsigned touch_consumed;
static const char *TAG = "unveil_display";
extern bool unveil_release_boot_frame(lv_display_t *);
extern void uv_first_frame_visible(void) __attribute__((weak));

static void touch_irq(esp_lcd_touch_handle_t tp)
{
    (void)tp;
    atomic_fetch_add(&touch_sequence, 1);
}
static void read_touch(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    unsigned sequence = atomic_load(&touch_sequence);
    /* As on desktop, idle touch never performs I2C reads under the render
     * lock. Held touches still poll until release if an IRQ edge is lost. */
    if (physical_pressed || sequence != touch_consumed) {
        touch_consumed = sequence;
        uint8_t count = 0;
        esp_lcd_touch_point_data_t point = {0};
        esp_err_t error = esp_lcd_touch_read_data(touch);
        physical_pressed = error == ESP_OK &&
            esp_lcd_touch_get_data(touch, &point, &count, 1) == ESP_OK && count;
        if (physical_pressed) { physical_x = point.x; physical_y = point.y; }
    }
    int64_t now = esp_timer_get_time();
    if (physical_pressed) { last_physical_touch = now; injected_pressed = false; }
    if (injected_pressed && now - injection_time > 2000000) injected_pressed = false;
    data->state = physical_pressed || injected_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    data->point.x = physical_pressed ? physical_x : injected_x;
    data->point.y = physical_pressed ? physical_y : injected_y;
}
static void observe_region(lv_display_t *d, const lv_area_t *area,
                            const void *pixels, size_t stride, void *ctx)
{
    (void)d; (void)ctx;
    if (!capture_drawing) return;
    size_t width = (size_t)lv_area_get_width(area) * 2;
    for (int y = area->y1; y <= area->y2; ++y) {
        memcpy(capture_pixels + ((size_t)y * DISPLAY_SIDE + area->x1) * 2,
               (const uint8_t *)pixels + (size_t)(y - area->y1) * stride, width);
    }
}
static void refresh_ready(lv_event_t *event)
{
    (void)event;
    esp_display_presenter_frame_status_t state;
    if (esp_display_presenter_get_frame_status(presenter, &state) != ESP_OK) return;
    if (handoff_pending && !handoff_frame) handoff_frame = state.last_submitted_frame;
    if (capture_drawing) {
        capture_drawing = false;
        capture_result = state.fault_reason ? ESP_FAIL : ESP_OK;
        xSemaphoreGive(capture_done);
    }
}
static void housekeeping(lv_timer_t *timer)
{
    (void)timer;
    esp_display_presenter_frame_status_t state;
    if (esp_display_presenter_get_frame_status(presenter, &state) != ESP_OK) return;
    if (!boot_visible && state.completed_present_frame) {
        if (bsp_display_brightness_set(100) == ESP_OK) boot_visible = true;
    }
    if (!first_frame_ready && handoff_pending && handoff_frame &&
            state.completed_present_frame >= handoff_frame && !state.fault_reason) {
        first_frame_ready = true; handoff_pending = false;
        if (boot_screen && boot_screen != lv_screen_active()) {
            lv_obj_delete(boot_screen); boot_screen = NULL;
            if (boot_font) { lv_tiny_ttf_destroy(boot_font); boot_font = NULL; }
            free(boot_font_data); boot_font_data = NULL;
        }
        if (uv_first_frame_visible) uv_first_frame_visible();
    }
    if (capture_requested) {
        capture_requested = false; capture_drawing = true;
        lv_obj_invalidate(lv_screen_active());
    }
}
static void boot_logo(void)
{
    boot_screen = lv_screen_active();
    lv_obj_set_style_bg_color(boot_screen, lv_color_black(), 0);
    FILE *file = fopen("/data/apps/unveil/assets/Doto-Regular.ttf", "rb");
    if (file) {
        if (fseek(file, 0, SEEK_END) == 0) {
            long size = ftell(file); rewind(file);
            if (size > 0 && size < 262144) {
                boot_font_data = malloc((size_t)size);
                if (boot_font_data && fread(boot_font_data, 1, size, file) == (size_t)size)
                    boot_font = lv_tiny_ttf_create_data(boot_font_data, size, 72);
            }
        }
        fclose(file);
    }
    lv_obj_t *word = lv_label_create(boot_screen);
    lv_label_set_text(word, "Unveil");
    lv_obj_set_style_text_color(word, lv_color_hex(0xf54b2c), 0);
    if (boot_font) lv_obj_set_style_text_font(word, boot_font, 0);
    lv_obj_center(word);
    /* Only schedule: the shared LVGL task owns every render and commit. */
    unveil_release_boot_frame(display);
}
esp_err_t uv_display_init(void)
{
    capture_done = xSemaphoreCreateBinary();
    if (!capture_done) return ESP_ERR_NO_MEM;
    esp_lcd_panel_handle_t panel;
    bsp_display_config_t board = BSP_DISPLAY_DEFAULT_CONFIG();
    ESP_ERROR_CHECK(bsp_display_new(&board, &panel));
    ESP_ERROR_CHECK(bsp_display_brightness_set(0));
    ESP_ERROR_CHECK(bsp_touch_new(board.rotation, &touch));
    const esp_display_presenter_config_t config = {
        .width = DISPLAY_SIDE, .height = DISPLAY_SIDE,
        .pixel_format = ESP_DISPLAY_PRESENT_PIXEL_FORMAT_RGB565,
        .max_damage_areas = 32,
        .target = {
            .hw = {.panel = panel, .io = bsp_display_get_panel_io(),
                   .panel_type = ESP_DISPLAY_PRESENT_PANEL_IO,
                   .input_pixel_format = ESP_DISPLAY_PRESENT_PIXEL_FORMAT_RGB565,
                   .rotation = ESP_DISPLAY_PRESENT_ROTATE_0, .swap_bytes = true,
                   .te_enabled = true,
                   .te_sync = {.gpio_num = BSP_LCD_TE, .bus_freq_hz = 40000000, .data_lines = 4}},
            .fb = {.mode = ESP_DISPLAY_PRESENT_MODE_TE_SYNC},
            .drawbuf = {.lines = DISPLAY_SIDE, .in_psram = true, .te_compose_buffers = 2},
        },
    };
    ESP_ERROR_CHECK(esp_display_presenter_create(&config, &presenter));
    esp_lv_adapter_config_t adapter = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    adapter.task_priority = 5; adapter.task_stack_size = 32768;
    adapter.stack_in_psram = true; adapter.task_core_id = 1;
    adapter.tick_period_ms = 5; adapter.task_max_delay_ms = 5;
    ESP_ERROR_CHECK(esp_lv_adapter_init(&adapter));
    const esp_lv_adapter_display_config_t profile =
        ESP_LV_ADAPTER_DISPLAY_SPI_WITH_PSRAM_DEFAULT_CONFIG(DISPLAY_SIDE, DISPLAY_SIDE, ESP_LV_ADAPTER_ROTATE_0);
    const esp_lv_adapter_presenter_config_t binding = {
        .presenter = presenter,
        .render_alignment = {.x_pixels = 4, .y_pixels = 4, .width_pixels = 4, .height_pixels = 4},
        .observe_region = observe_region,
    };
    display = esp_lv_adapter_register_display_with_presenter(&profile, &binding);
    if (!display) return ESP_ERR_NO_MEM;
    lv_display_add_event_cb(display, refresh_ready, LV_EVENT_REFR_READY, NULL);
    lv_indev_t *indev = lv_indev_create();
    if (!indev) return ESP_ERR_NO_MEM;
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, read_touch);
    ESP_ERROR_CHECK(esp_lcd_touch_register_interrupt_callback(touch, touch_irq));
    atomic_store(&touch_sequence, 1);
    if (!lv_timer_create(housekeeping, 10, NULL)) return ESP_ERR_NO_MEM;
    boot_logo();
    ESP_LOGI(TAG, "Desktop display path: coherent double compose, measured TE, IRQ touch");
    return esp_lv_adapter_start();
}
esp_err_t uv_display_lock(void) { return esp_lv_adapter_lock(1000); }
void uv_display_unlock(void) { esp_lv_adapter_unlock(); }
esp_err_t uv_display_capture_begin(void)
{
    esp_err_t error = uv_display_lock();
    if (error != ESP_OK) return error;
    if (capture_pixels || !first_frame_ready) { uv_display_unlock(); return ESP_ERR_INVALID_STATE; }
    capture_pixels = heap_caps_calloc(DISPLAY_SIDE * DISPLAY_SIDE, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!capture_pixels) { uv_display_unlock(); return ESP_ERR_NO_MEM; }
    while (xSemaphoreTake(capture_done, 0) == pdTRUE) {}
    capture_result = ESP_ERR_TIMEOUT; capture_requested = true;
    uv_display_unlock();
    if (xSemaphoreTake(capture_done, pdMS_TO_TICKS(5000)) == pdTRUE && capture_result == ESP_OK) return ESP_OK;
    error = capture_result; uv_display_capture_end(); return error;
}
esp_err_t uv_display_capture_read(size_t offset, void *out, size_t capacity, size_t *size)
{
    esp_err_t error = uv_display_lock();
    if (error != ESP_OK) return error;
    if (!capture_pixels || offset >= DISPLAY_BYTES) { uv_display_unlock(); return ESP_ERR_INVALID_STATE; }
    *size = DISPLAY_BYTES - offset; if (*size > capacity) *size = capacity;
    memcpy(out, capture_pixels + offset, *size); uv_display_unlock(); return ESP_OK;
}
void uv_display_capture_end(void)
{
    if (uv_display_lock() != ESP_OK) return;
    capture_requested = capture_drawing = false;
    heap_caps_free(capture_pixels); capture_pixels = NULL; uv_display_unlock();
}
esp_err_t uv_display_pointer(uint16_t x, uint16_t y, bool pressed)
{
    if (x >= DISPLAY_SIDE || y >= DISPLAY_SIDE) return ESP_ERR_INVALID_ARG;
    esp_err_t error = uv_display_lock(); if (error != ESP_OK) return error;
    int64_t now = esp_timer_get_time();
    if (pressed && last_physical_touch && now - last_physical_touch < 2000000) {
        uv_display_unlock(); return ESP_ERR_INVALID_STATE;
    }
    injected_x = x; injected_y = y; injected_pressed = pressed; injection_time = now;
    uv_display_unlock(); return ESP_OK;
}
bool uv_display_has_exclusive_session(void) { return session.active; }
esp_err_t uv_display_open(const uv_display_session_config_t *cfg, uv_display_session_handle_t *out)
{
    if (session.active) return ESP_ERR_INVALID_STATE;
    session.config = *cfg; session.active = true; *out = &session; return ESP_OK;
}
esp_err_t uv_display_close(uv_display_session_handle_t s)
{
    if (s != &session) return ESP_ERR_INVALID_ARG;
    esp_err_t error = uv_display_lock(); if (error != ESP_OK) return error;
    if (s->config.cleanup_cb) s->config.cleanup_cb(s, s->config.user_ctx);
    s->active = false; uv_display_unlock(); return ESP_OK;
}
lv_display_t *uv_display_session_display(uv_display_session_handle_t s) { return s == &session ? display : NULL; }
esp_err_t uv_display_session_load_screen_locked(uv_display_session_handle_t s, lv_obj_t *screen)
{
    if (s != &session) return ESP_ERR_INVALID_ARG;
    if (!first_frame_ready) pending_screen = screen; else lv_screen_load(screen);
    return ESP_OK;
}
bool uv_display_present_ready(lv_display_t *d)
{
    if (d != display) return false;
    if (pending_screen) {
        lv_obj_update_layout(pending_screen); lv_screen_load(pending_screen); pending_screen = NULL;
        handoff_pending = true; handoff_frame = 0;
    }
    lv_obj_invalidate(lv_screen_active()); return true;
}
