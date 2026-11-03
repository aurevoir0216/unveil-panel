#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "unveil_raster.h"

static uint16_t geometry[UV_PIXELS];
static uint16_t actual[UV_PIXELS];
static uint16_t expected[UV_PIXELS];
static uint16_t palette[UV_PALETTE_SIZE];

static void verify_transition(uint16_t outside, int old_angle, bool was_running,
                              int angle, bool running)
{
    uv_frame(actual, geometry, palette, outside, old_angle, was_running);
    uv_frame(expected, geometry, palette, outside, angle, running);
    uv_damage_t damage = uv_sweep_update(actual, geometry, palette, outside,
                                         old_angle, was_running, angle, running);
    assert(memcmp(actual, expected, sizeof(actual)) == 0);

    /* A correct backing image is insufficient if the presenter misses a pixel. */
    uv_frame(expected, geometry, palette, outside, old_angle, was_running);
    for (int y = 0; y < UV_SIDE; ++y) {
        for (int x = 0; x < UV_SIDE; ++x) {
            int pixel = y * UV_SIDE + x;
            if (actual[pixel] != expected[pixel]) {
                assert(x >= damage.x0 && x <= damage.x1);
                assert(y >= damage.y0 && y <= damage.y1);
            }
        }
    }
}

int main(void)
{
    uv_geometry(geometry);
    const uint32_t backgrounds[] = {0x000000, 0xe1e1e6};
    for (size_t theme = 0; theme < sizeof(backgrounds) / sizeof(backgrounds[0]); ++theme) {
        uint32_t bg = backgrounds[theme];
        uint16_t outside = uv_565(bg);
        uv_palette_ex(palette, bg, 0x687280, 0xf54b2c, bg, 0xa5adb8);
        for (int angle = 0; angle < UV_TURN; ++angle) {
            verify_transition(outside, angle, true, (angle + 6) % UV_TURN, true);
            if (angle % 45 == 0) {
                verify_transition(outside, angle, false, angle, true);
                verify_transition(outside, angle, true, angle, false);
                verify_transition(outside, angle, true, (angle + 72) % UV_TURN, true);
                verify_transition(outside, angle, true, (angle + 864) % UV_TURN, true);
            }
        }
    }
    puts("Radar sweep matches full redraw in both themes, including damage bounds.");
    return 0;
}
