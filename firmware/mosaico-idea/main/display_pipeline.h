// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "present_te_internal.h"

typedef void (*unveil_te_wait_fn)(esp_display_present_te_compose_t *, size_t);

esp_err_t unveil_te_enqueue(esp_display_present_te_compose_t *te, uint64_t frame_id,
                            bool *queued, uint16_t first_row, uint16_t end_row,
                            unveil_te_wait_fn wait_window);
void unveil_te_queue_profile(uint64_t out[6], bool reset);
void unveil_te_scanout_done(void);
