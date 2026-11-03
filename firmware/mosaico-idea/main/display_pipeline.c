// SPDX-License-Identifier: Apache-2.0
#include "display_pipeline.h"

#include <inttypes.h>
#include <string.h>
#include "esp_attr.h"
#include "esp_display_present_panel.h"
#include "esp_log.h"
#include "esp_timer.h"

typedef struct {
    esp_display_present_te_compose_t *te;
    uint8_t *pixels;
    uint64_t frame_id;
    uint64_t dma_wait_us;
    uint16_t first_row, end_row;
    unveil_te_wait_fn wait_window;
} te_frame_t;

static QueueHandle_t send_queue;
static portMUX_TYPE stats_lock = portMUX_INITIALIZER_UNLOCKED;
static uint64_t tx[6]; /* frames, bytes, DMA wait us, TE wait us, min rows, max rows */
static struct {
    uint64_t completed, longest_gap, late, span_us;
    int64_t last_us;
} scanout;

void unveil_te_queue_profile(uint64_t out[6], bool reset)
{
    portENTER_CRITICAL(&stats_lock);
    for (unsigned i = 0; i < 4; ++i) out[i] += tx[i];
    if (tx[4] && (!out[4] || tx[4] < out[4])) out[4] = tx[4];
    if (tx[5] > out[5]) out[5] = tx[5];
    if (reset) memset(tx, 0, sizeof(tx));
    portEXIT_CRITICAL(&stats_lock);
}

void unveil_te_scanout_profile(uint64_t out[4], bool reset)
{
    portENTER_CRITICAL(&stats_lock);
    out[0] = scanout.completed;
    out[1] = scanout.longest_gap;
    out[2] = scanout.late;
    out[3] = scanout.span_us;
    if (reset) memset(&scanout, 0, sizeof(scanout));
    portEXIT_CRITICAL(&stats_lock);
}

void IRAM_ATTR unveil_te_scanout_done(void)
{
    const int64_t now = esp_timer_get_time();
    portENTER_CRITICAL_ISR(&stats_lock);
    if (scanout.last_us) {
        const uint64_t gap = now - scanout.last_us;
        scanout.span_us += gap;
        if (gap > scanout.longest_gap) scanout.longest_gap = gap;
        if (gap > 33333) ++scanout.late;
    }
    scanout.last_us = now;
    ++scanout.completed;
    portEXIT_CRITICAL_ISR(&stats_lock);
}

static esp_err_t wait_for_previous(esp_display_present_te_compose_t *te)
{
    esp_display_present_frame_tracker_t *tracker = &te->tracker;
    const TickType_t timeout = pdMS_TO_TICKS(te->transfer_timeout_ms);
    const TickType_t started = xTaskGetTickCount();
    while (esp_display_present_tracker_get_completed_transfer_frame(tracker) <
           te->building_previous_frame) {
        if (esp_display_present_tracker_is_faulted(tracker)) return ESP_ERR_INVALID_STATE;
        TickType_t elapsed = xTaskGetTickCount() - started;
        if (elapsed >= timeout ||
            !esp_display_present_tracker_wait_completion(tracker, timeout - elapsed)) {
            esp_display_present_tracker_mark_faulted(tracker, ESP_DISPLAY_PRESENT_FAULT_TRANSFER_TIMEOUT);
            return ESP_ERR_TIMEOUT;
        }
    }
    return esp_display_present_tracker_is_faulted(tracker) ? ESP_ERR_INVALID_STATE : ESP_OK;
}

static void send_frames(void *arg)
{
    QueueHandle_t queue = arg;
    te_frame_t frame;
    while (xQueueReceive(queue, &frame, portMAX_DELAY) == pdTRUE) {
        esp_display_present_te_compose_t *te = frame.te;
        const uint64_t rows = frame.end_row - frame.first_row;
        const size_t bytes = te->draw.stride_bytes * rows;
        uint8_t *pixels = frame.pixels + te->draw.stride_bytes * frame.first_row;
        /* The SPI LCD driver synchronizes this TX range before queuing DMA. */
        const int64_t wait_started = esp_timer_get_time();
        frame.wait_window(te, bytes);
        const int64_t te_wait = esp_timer_get_time() - wait_started;
        if (!te->te_degraded) esp_display_present_te_sync_record_tx_start(te->te_ctx);
        esp_err_t error = esp_display_present_blit_area(te->panel, 0, frame.first_row,
                                                        te->draw.width, frame.end_row, pixels);
        if (error != ESP_OK) {
            /* Preserve ownership when a driver error cannot rule out DMA.
             * The existing fault fence prevents further submissions. */
            esp_display_present_tracker_mark_faulted(&te->tracker, ESP_DISPLAY_PRESENT_FAULT_PRODUCER_PROTOCOL);
            esp_display_present_tracker_signal_completion(&te->tracker);
            ESP_LOGE("unveil_te", "frame %" PRIu64 " failed: %s", frame.frame_id, esp_err_to_name(error));
            continue;
        }
        portENTER_CRITICAL(&stats_lock);
        ++tx[0];
        tx[1] += bytes;
        tx[2] += frame.dma_wait_us;
        tx[3] += te_wait;
        if (!tx[4] || rows < tx[4]) tx[4] = rows;
        if (rows > tx[5]) tx[5] = rows;
        portEXIT_CRITICAL(&stats_lock);
    }
    vTaskDelete(NULL);
}

esp_err_t unveil_te_enqueue(esp_display_present_te_compose_t *te, uint64_t frame_id,
                            bool *queued, uint16_t first_row, uint16_t end_row,
                            unveil_te_wait_fn wait_window)
{
    if (te->compose_buffer_count < 2) return ESP_ERR_NOT_SUPPORTED;
    if (!send_queue) {
        QueueHandle_t queue = xQueueCreate(1, sizeof(te_frame_t));
        if (!queue) return ESP_ERR_NOT_SUPPORTED;
        /* LVGL renders on core 1. The SPI/TE worker runs on core 0. */
        if (xTaskCreatePinnedToCore(send_frames, "unveil_te", 4096, queue,
                                    uxTaskPriorityGet(NULL) + 1, NULL, 0) != pdPASS) {
            vQueueDelete(queue);
            return ESP_ERR_NOT_SUPPORTED;
        }
        send_queue = queue;
        ESP_LOGI("unveil_te", "async scanout active, %u compose buffers", te->compose_buffer_count);
    }

    /* The next frame has already been composed while SPI used the other
     * buffer. Wait for its real retirement before reclaiming that buffer. */
    const int64_t started = esp_timer_get_time();
    esp_err_t error = wait_for_previous(te);
    if (error != ESP_OK) return error;
    const te_frame_t frame = {
        .te = te, .pixels = te->draw.pixels, .frame_id = frame_id,
        .dma_wait_us = esp_timer_get_time() - started,
        .first_row = first_row, .end_row = end_row, .wait_window = wait_window,
    };
    error = esp_display_present_tracker_submit_transfer_ticket(&te->tracker);
    if (error != ESP_OK) return error;
    if (!present_te_pool_commit(&te->pool, te->display_buffer, frame.pixels, frame_id)) {
        esp_display_present_tracker_cancel_transfer_ticket(&te->tracker);
        return ESP_ERR_INVALID_STATE;
    }
    *queued = true; /* The ticket now owns the buffer, even on a handoff fault. */
    void *next = NULL;
    error = present_te_pool_acquire_next(&te->pool, &te->stopped, &next);
    if (error != ESP_OK) return error;
    error = esp_display_present_tracker_end_frame(&te->tracker, frame_id);
    if (error != ESP_OK) return error;
    /* Repair takes its baseline from the last composed frame even if that
     * frame is still queued. The DMA ISR only retires its physical ticket. */
    te->display_buffer = frame.pixels;
    te->draw.pixels = next;
    te->has_active_buffer = false;
    return xQueueSend(send_queue, &frame, 0) == pdTRUE ? ESP_OK : ESP_ERR_INVALID_STATE;
}
