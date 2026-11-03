#pragma once
#include <math.h>
/* Shared by hit testing and the generated dial fill boundaries. */
enum { UV_DIAL_CENTER_X=240, UV_DIAL_CENTER_Y=240, UV_DIAL_INNER_RADIUS=60, UV_DIAL_OUTER_RADIUS=173 };

/* During a drag the press point defines the outward ray. A position across
 * the center has a negative projection and stays at the minimum detent. */
static inline float uv_dial_drag_radius(float x, float y, float press_x, float press_y,
                                        float press_radius)
{
    return (x * press_x + y * press_y) / press_radius;
}

static inline int uv_dial_index_from_radius(float radius,int count)
{
    if(count<=1 || radius<=UV_DIAL_INNER_RADIUS)return 1;
    if(radius>=UV_DIAL_OUTER_RADIUS)return count;
    int index=1+(int)floorf((radius-UV_DIAL_INNER_RADIUS)*count/(UV_DIAL_OUTER_RADIUS-UV_DIAL_INNER_RADIUS));
    return index>count?count:index;
}
