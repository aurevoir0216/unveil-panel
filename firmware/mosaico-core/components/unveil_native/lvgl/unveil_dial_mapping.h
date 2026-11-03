#pragma once
#include <math.h>
/* Shared by hit testing and the generated dial fill boundaries. */
enum { UV_DIAL_CENTER_X=240, UV_DIAL_CENTER_Y=240, UV_DIAL_INNER_RADIUS=60, UV_DIAL_OUTER_RADIUS=173 };
static inline int uv_dial_index_from_radius(float radius,int count)
{
    if(count<=1 || radius<=UV_DIAL_INNER_RADIUS)return 1;
    if(radius>=UV_DIAL_OUTER_RADIUS)return count;
    int index=1+(int)floorf((radius-UV_DIAL_INNER_RADIUS)*count/(UV_DIAL_OUTER_RADIUS-UV_DIAL_INNER_RADIUS));
    return index>count?count:index;
}
