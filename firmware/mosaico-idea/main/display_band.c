// SPDX-License-Identifier: Apache-2.0
#include "present_async_copy.h"

#include "driver/ppa.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "lv_draw_sw_blend_private.h"

void __real_lv_draw_sw_blend_color_to_rgb565(lv_draw_sw_blend_fill_dsc_t *fill);

/* A contiguous opaque backdrop can use the PPA without touching adjacent
 * rows or adding a GPU layer. Partial fills still use the LVGL renderer. */
void __wrap_lv_draw_sw_blend_color_to_rgb565(lv_draw_sw_blend_fill_dsc_t *fill)
{
    static ppa_client_handle_t client;
    static bool disabled;
    const size_t bytes = (size_t)fill->dest_stride * fill->dest_h;
    const size_t cache_alignment = 128;
    if (!disabled && fill->mask_buf == NULL && fill->opa >= LV_OPA_MAX &&
        fill->dest_w >= 320 && fill->dest_h >= 96 &&
        fill->dest_stride == fill->dest_w * 2 && esp_ptr_external_ram(fill->dest_buf) &&
        (((uintptr_t)fill->dest_buf | bytes) & (cache_alignment - 1)) == 0) {
        if (!client) {
            const ppa_client_config_t config = {
                .oper_type = PPA_OPERATION_FILL,
                .max_pending_trans_num = 1,
            };
            disabled = ppa_register_client(&config, &client) != ESP_OK;
        }
        if (client) {
            const ppa_fill_oper_config_t operation = {
                .out = {
                    .buffer = fill->dest_buf, .buffer_size = bytes,
                    .pic_w = fill->dest_w, .pic_h = fill->dest_h,
                    .fill_cm = PPA_FILL_COLOR_MODE_RGB565,
                },
                .fill_block_w = fill->dest_w, .fill_block_h = fill->dest_h,
                .fill_argb_color.val = lv_color_to_u32(fill->color),
                .mode = PPA_TRANS_MODE_BLOCKING,
            };
            if (ppa_do_fill(client, &operation) == ESP_OK) return;
            disabled = true;
        }
    }
    __real_lv_draw_sw_blend_color_to_rgb565(fill);
}

esp_err_t __real_present_async_copy_submit(present_async_copy_t *copy,
    esp_display_present_target_t *target, const esp_display_present_blit_fb_t *dst,
    const esp_display_presenter_region_t *region, bool swap_rgb565);

/* In the pinned presenter a wide LVGL tile is swapped in place and then
 * copied to the panel buffer. Full-width bands can do both in a single PPA
 * operation while keeping the existing TE and transfer ownership intact. */
esp_err_t __wrap_present_async_copy_submit(present_async_copy_t *copy,
    esp_display_present_target_t *target, const esp_display_present_blit_fb_t *dst,
    const esp_display_presenter_region_t *region, bool swap_rgb565)
{
    static ppa_client_handle_t client;
    static bool unavailable;
    static bool reported;
    const esp_display_presenter_surface_t *surface = &region->surface;
    if (unavailable || !swap_rgb565 || dst->color_bytes != 2 ||
        dst->rotation != ESP_DISPLAY_PRESENT_ROTATE_0 || region->origin_x != 0 ||
        surface->width != dst->width || surface->height < 64 ||
        surface->stride_bytes != dst->stride_bytes ||
        dst->stride_bytes != (size_t)dst->width * 2 ||
        region->origin_y + surface->height > dst->height) {
        return __real_present_async_copy_submit(copy, target, dst, region, swap_rgb565);
    }
    /* Complete the fast path inline only when there is no earlier tile in
     * flight. The normal worker path preserves ordering for mixed regions. */
    for (uint8_t i = 0; copy && copy->pool && i < copy->pool->count; ++i) {
        if (copy->busy[i]) {
            return __real_present_async_copy_submit(copy, target, dst, region, swap_rgb565);
        }
    }

    if (!client) {
        const ppa_client_config_t config = {
            .oper_type = PPA_OPERATION_SRM,
            .max_pending_trans_num = 1,
        };
        unavailable = ppa_register_client(&config, &client) != ESP_OK;
    }
    if (client) {
        const ppa_srm_oper_config_t operation = {
            .in = {
                .buffer = surface->pixels,
                .pic_w = surface->width,
                .pic_h = surface->height,
                .block_w = surface->width,
                .block_h = surface->height,
                .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
            },
            .out = {
                .buffer = dst->pixels,
                .buffer_size = dst->stride_bytes * dst->height,
                .pic_w = dst->width,
                .pic_h = dst->height,
                .block_offset_y = region->origin_y,
                .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
            },
            .rotation_angle = PPA_SRM_ROTATION_ANGLE_0,
            .scale_x = 1,
            .scale_y = 1,
            .byte_swap = true,
            .mode = PPA_TRANS_MODE_BLOCKING,
        };
        esp_err_t error = ppa_do_scale_rotate_mirror(client, &operation);
        if (error == ESP_OK) {
            if (!reported) {
                ESP_LOGI("unveil_display", "PPA RGB565 band copy active");
                reported = true;
            }
            return ESP_OK;
        }
        ESP_LOGW("unveil_display", "PPA band copy unavailable: %s", esp_err_to_name(error));
        unavailable = true;
    }
    return __real_present_async_copy_submit(copy, target, dst, region, swap_rgb565);
}
