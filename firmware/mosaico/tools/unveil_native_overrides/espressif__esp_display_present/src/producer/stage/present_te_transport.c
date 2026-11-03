/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * TE full-flush: wait for one TE edge, push the compose buffer.
 * Multi-buffer GRAM frees the previous baseline at submit so compose of the
 * next frame can overlap the in-flight DMA. The next push still waits for
 * that DMA ticket because the panel bus is single-consumer.
 */

#include "present_te_internal.h"
#include "present_mode_internal.h"
#include "present_transfer_wait.h"

#include <inttypes.h>
#include "esp_attr.h"
#include "esp_display_present_cache.h"
#include "esp_display_present_geometry.h"
#include "esp_display_present_panel.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "present_te_compose";
/* Read/reset by the LVGL owner under the display lock. DMA ISR does not modify these. */
static uint64_t unveil_tx[6]; /* frames, bytes, prior-DMA wait us, TE wait us, min rows, max rows */
void unveil_te_profile(uint64_t out[6], bool reset)
{
    for(int i=0;i<6;++i)out[i]=unveil_tx[i];
    if(reset)for(int i=0;i<6;++i)unveil_tx[i]=0;
}

static void te_wait_submit_window(esp_display_present_te_compose_t *te_compose,
                                  size_t frame_bytes)
{
    if (te_compose->te_degraded) {
        return;
    }
    esp_display_present_te_sync_begin_frame(te_compose->te_ctx, frame_bytes);
    if (esp_display_present_te_sync_wait_for_vsync(te_compose->te_ctx) ==
            ESP_ERR_TIMEOUT) {
        if (++te_compose->te_timeouts >= 3) {
            te_compose->te_degraded = true;
            ESP_LOGW(TAG, "TE line silent; using free-running transfers");
        }
    } else {
        te_compose->te_timeouts = 0;
    }
}

esp_err_t present_te_compose_push_frame(
    esp_display_present_te_compose_t *te_compose, uint8_t *pixels,
    bool *out_submitted_any)
{
    if (te_compose == NULL || out_submitted_any == NULL ||
            pixels == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_submitted_any = false;
    size_t frame_bytes = te_compose->draw.stride_bytes *
                         te_compose->draw.height;
    te_wait_submit_window(te_compose, frame_bytes);
    esp_display_present_cache_msync_framebuffer(pixels, frame_bytes);
    esp_err_t ret = esp_display_present_tracker_submit_transfer_ticket(
                        &te_compose->tracker);
    if (ret == ESP_OK) {
        if (!te_compose->te_degraded) {
            esp_display_present_te_sync_record_tx_start(te_compose->te_ctx);
        }
        ret = esp_display_present_blit_area(
                  te_compose->panel, 0, 0, te_compose->draw.width,
                  te_compose->draw.height, pixels);
        if (ret == ESP_OK) {
            *out_submitted_any = true;
        } else {
            esp_display_present_tracker_cancel_transfer_ticket(
                &te_compose->tracker);
        }
    }
    esp_err_t drain_ret = ret == ESP_OK
                          ? present_transfer_wait_idle(
                              &te_compose->tracker,
                              te_compose->transfer_timeout_ms)
                          : ret;
    if (ret == ESP_OK) {
        ret = drain_ret;
    }
    return ret;
}

static esp_err_t submit_te_pipeline(
    esp_display_present_te_compose_t *te_compose, uint64_t frame_id,
    bool *out_submitted_any, uint16_t y1, uint16_t y2)
{
    uint8_t *pixels = te_compose->draw.pixels;
    void *old_display = te_compose->display_buffer;
    size_t frame_bytes = te_compose->draw.stride_bytes *
                         (y2-y1);
    uint8_t *tx_pixels=pixels+(size_t)y1*te_compose->draw.stride_bytes;
    int64_t wait_started=esp_timer_get_time();

    *out_submitted_any = false;
    if (present_transfer_wait_idle(
                &te_compose->tracker,
                te_compose->transfer_timeout_ms) != ESP_OK) {
        ESP_LOGE(TAG,
                 "TE transfer timed out; retaining in-flight buffer");
        return ESP_ERR_TIMEOUT;
    }
    unveil_tx[2]+=esp_timer_get_time()-wait_started;
    wait_started=esp_timer_get_time();
    te_wait_submit_window(te_compose, frame_bytes);
    unveil_tx[3]+=esp_timer_get_time()-wait_started;
    esp_display_present_cache_msync_framebuffer(tx_pixels, frame_bytes);

    esp_err_t ret = esp_display_present_tracker_submit_transfer_ticket(
                        &te_compose->tracker);
    if (ret != ESP_OK) {
        return ret;
    }
    if (!present_te_pool_commit(&te_compose->pool, old_display, pixels,
                                frame_id)) {
        esp_display_present_tracker_cancel_transfer_ticket(
            &te_compose->tracker);
        return ESP_ERR_INVALID_STATE;
    }
    if (!te_compose->te_degraded) {
        esp_display_present_te_sync_record_tx_start(te_compose->te_ctx);
    }
    ret = esp_display_present_blit_area(
              te_compose->panel, 0, y1, te_compose->draw.width,
              y2, tx_pixels);
    if (ret != ESP_OK) {
        (void)present_te_pool_cancel_commit(
            &te_compose->pool, old_display, pixels);
        esp_display_present_tracker_cancel_transfer_ticket(
            &te_compose->tracker);
        return ret;
    }
    *out_submitted_any = true;
    ++unveil_tx[0];unveil_tx[1]+=frame_bytes;
    uint64_t rows=y2-y1;
    if(!unveil_tx[4]||rows<unveil_tx[4])unveil_tx[4]=rows;
    if(rows>unveil_tx[5])unveil_tx[5]=rows;
    te_compose->display_buffer = pixels;

    ret = esp_display_present_tracker_end_frame(
              &te_compose->tracker, frame_id);
    if (ret != ESP_OK) {
        return ret;
    }

    void *next = NULL;
    ret = present_te_pool_acquire_next(
              &te_compose->pool, &te_compose->stopped, &next);
    if (ret != ESP_OK) {
        te_compose->draw.pixels = NULL;
        te_compose->has_active_buffer = false;
        ESP_LOGE(TAG, "TE pool acquire failed: ticket=%" PRIu64 " error=%s",
                 frame_id, esp_err_to_name(ret));
        return ret;
    }
    te_compose->draw.pixels = next;
    te_compose->has_active_buffer = false;

    return ESP_OK;
}

bool IRAM_ATTR esp_display_present_te_compose_notify_transfer_done_from_isr(
    esp_display_present_te_compose_t *te_compose)
{
    if (te_compose == NULL ||
            !esp_display_present_tracker_isr_enter(&te_compose->tracker)) {
        return false;
    }
    BaseType_t need_yield = pdFALSE;
    if (!te_compose->te_degraded) {
        esp_display_present_te_sync_record_tx_done(te_compose->te_ctx);
    }
    (void)esp_display_present_tracker_complete_transfer_ticket_isr(
        &te_compose->tracker);
    if (present_te_pool_enabled(&te_compose->pool)) {
        void *display_buffer = NULL;
        uint64_t ticket = 0;
        if (!present_te_pool_retire_isr(
                    &te_compose->pool, &display_buffer, &ticket)) {
            esp_display_present_tracker_mark_faulted_isr(
                &te_compose->tracker,
                ESP_DISPLAY_PRESENT_FAULT_PRODUCER_PROTOCOL);
        } else {
            if (display_buffer != NULL) {
                te_compose->display_buffer = display_buffer;
            }
            (void)esp_display_present_tracker_complete_transfer_frame_isr(
                &te_compose->tracker, ticket);
            (void)esp_display_present_tracker_complete_present_frame_isr(
                &te_compose->tracker, ticket);
        }
    }
    esp_display_present_tracker_signal_completion_isr(
        &te_compose->tracker, &need_yield);
    esp_display_present_tracker_isr_leave(&te_compose->tracker);
    return need_yield == pdTRUE;
}

uint64_t esp_display_present_te_compose_get_completed_transfer_frame(
    const esp_display_present_te_compose_t *te_compose)
{
    return te_compose != NULL
           ? esp_display_present_tracker_get_completed_transfer_frame(
               &te_compose->tracker) : 0;
}

uint64_t esp_display_present_te_compose_get_last_submitted_frame(
    const esp_display_present_te_compose_t *te_compose)
{
    return te_compose != NULL
           ? esp_display_present_tracker_get_last_submitted_frame(
               &te_compose->tracker) : 0;
}

uint64_t esp_display_present_te_compose_get_completed_present_frame(
    const esp_display_present_te_compose_t *te_compose)
{
    return te_compose != NULL
           ? esp_display_present_tracker_get_completed_present_frame(
               &te_compose->tracker) : 0;
}

static bool te_submit_is_valid(
    const esp_display_present_te_compose_t *te_compose,
    const esp_display_presenter_submit_t *submit)
{
    if (submit == NULL ||
            submit->coverage != ESP_DISPLAY_PRESENT_COVERAGE_AREAS) {
        return true;
    }
    if ((submit->areas == NULL) != (submit->area_count == 0) ||
            submit->area_count > te_compose->max_damage_areas) {
        return false;
    }
    for (size_t index = 0; index < submit->area_count; ++index) {
        if (!esp_display_present_geometry_area_is_valid(
                    &submit->areas[index], te_compose->logical_width,
                    te_compose->logical_height)) {
            return false;
        }
    }
    return true;
}

esp_err_t esp_display_present_te_compose_commit_frame(
    present_frame_ctx_t *ctx,
    const esp_display_presenter_submit_t *submit)
{
    esp_display_present_te_compose_t *te_compose =
        ctx != NULL ? ctx->mode_ctx : NULL;
    const uint64_t frame_id = ctx != NULL ? ctx->frame_id : 0;
    if (te_compose == NULL || ctx->target == NULL || frame_id == 0 ||
            !te_submit_is_valid(te_compose, submit)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (esp_display_present_tracker_get_building_frame(
                &te_compose->tracker) != frame_id) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Full-width horizontal bands remain contiguous for DMA. Keep TE and the
     * existing compose/repair protocol, but do not transmit unchanged rows. */
    uint16_t y1=0,y2=te_compose->draw.height;
    if(te_compose->rotation==ESP_DISPLAY_PRESENT_ROTATE_0 && submit &&
       submit->coverage==ESP_DISPLAY_PRESENT_COVERAGE_AREAS && submit->area_count){
        y1=te_compose->draw.height;y2=0;
        for(size_t i=0;i<submit->area_count;++i){
            if(submit->areas[i].y1<y1)y1=submit->areas[i].y1;
            if(submit->areas[i].y2+1>y2)y2=submit->areas[i].y2+1;
        }
    }
    bool submitted_any = false;
    /* Every queued tile must be in the compose buffer before the push
     * writes it back and hands it to the panel DMA. */
    esp_err_t ret = present_async_copy_drain(&te_compose->async_copy);
    if (ret != ESP_OK) {
        esp_display_present_tracker_rollback_frame(
            &te_compose->tracker, frame_id,
            te_compose->building_previous_frame, false);
        te_compose->building_previous_frame = 0;
        te_compose->has_active_buffer = false;
        return ret;
    }
    if (present_te_pool_enabled(&te_compose->pool)) {
        ret = esp_display_present_tracker_publish_frame(
                  &te_compose->tracker, frame_id,
                  &te_compose->building_previous_frame);
        if (ret == ESP_OK) {
            ret = submit_te_pipeline(te_compose, frame_id, &submitted_any, y1, y2);
        }
        if (ret == ESP_OK) {
            te_compose->building_previous_frame = 0;
            return ESP_OK;
        }
        esp_display_present_tracker_rollback_frame(
            &te_compose->tracker, frame_id,
            te_compose->building_previous_frame, submitted_any);
        te_compose->building_previous_frame = 0;
        if (te_compose->has_active_buffer && !submitted_any) {
            te_compose->has_active_buffer = false;
        }
        ESP_LOGE(TAG, "TE pipeline frame %" PRIu64 " failed: %s",
                 frame_id, esp_err_to_name(ret));
        return ret;
    }

    ret = esp_display_present_tracker_publish_frame(
              &te_compose->tracker, frame_id,
              &te_compose->building_previous_frame);
    if (ret == ESP_OK) {
        ret = present_te_compose_push_frame(
                  te_compose, te_compose->draw.pixels, &submitted_any);
    }
    if (ret == ESP_OK) {
        ret = esp_display_present_tracker_end_frame(
                  &te_compose->tracker, frame_id);
    }
    if (ret == ESP_OK) {
        ret = esp_display_present_tracker_complete_frame(
                  &te_compose->tracker, frame_id, true, true);
    }
    if (ret == ESP_OK) {
        te_compose->building_previous_frame = 0;
        return ESP_OK;
    }

    esp_display_present_tracker_rollback_frame(
        &te_compose->tracker, frame_id, te_compose->building_previous_frame,
        submitted_any);
    te_compose->building_previous_frame = 0;
    ESP_LOGE(TAG, "TE ticket %" PRIu64 " submit failed: %s",
             frame_id, esp_err_to_name(ret));
    return ret;
}
