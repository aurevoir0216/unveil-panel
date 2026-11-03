/* Procedural radar rasterizer. No assets, image rotation or per-frame allocation. */
#pragma once
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#define UV_SIDE 432
#define UV_CENTER 215.5f
#define UV_STATIC_BASE ((UV_TRAIL + 1) * 16)
#define UV_PALETTE_SIZE ((UV_TRAIL + 2) * 16)
#define UV_PIXELS (UV_SIDE * UV_SIDE)
#define UV_TURN 1440
#define UV_TRAIL 288
#define UV_SWEEP_RADIUS 164.f
typedef struct { int x0, y0, x1, y1; } uv_damage_t;
static inline float uv_clamp(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
static inline uint32_t uv_mix(uint32_t a, uint32_t b, float t) {
    uint32_t out = 0;
    for (int s = 0; s <= 16; s += 8) {
        int x = (a >> s) & 255, y = (b >> s) & 255;
        out |= (uint32_t)(x + (y - x) * t + .5f) << s;
    }
    return out;
}
static inline uint16_t uv_565(uint32_t c) {
    return ((c >> 8) & 0xf800) | ((c >> 5) & 0x07e0) | ((c >> 3) & 31);
}
static void uv_geometry(uint16_t *polar) {
    for (int y = 0; y < UV_SIDE; ++y) for (int x = 0; x < UV_SIDE; ++x) {
        float dx = x - UV_CENTER, dy = y - UV_CENTER;
        float r = sqrtf(dx * dx + dy * dy);
        int i = y * UV_SIDE + x;
        if (r > UV_SWEEP_RADIUS) {
            float coverage=uv_clamp(1.5f-fabsf(r-173.f));
            if ((r>189.5f&&r<192.f)||(r>197.f&&r<209.f)) {
                float theta=atan2f(dy,dx)*57.295779513f;
                float along=fmodf(theta-155.f+720.f,360.f);
                if(along<=230.f)coverage=fmaxf(coverage,uv_clamp(1.f-fabsf(r-190.5f)));
                int tick=(int)floorf(along/11.5f+.5f);
                if(tick<=20){
                    float a=(along-tick*11.5f)*.0174532925199f;
                    float tangent=fabsf(r*sinf(a)),radial=r*cosf(a),end=198+(tick%5==0?10:6);
                    float radial_gap=radial<198?198-radial:radial>end?radial-end:0;
                    coverage=fmaxf(coverage,uv_clamp(1.f-sqrtf(tangent*tangent+radial_gap*radial_gap)));
                }
            }
            int c=(int)(coverage*15+.5f);polar[i]=c?0x8000|c:UINT16_MAX;continue;
        }
        float distance = fminf(fabsf(dx), fabsf(dy));
        distance = fminf(distance, fabsf(dx - dy) * .70710678f);
        distance = fminf(distance, fabsf(dx + dy) * .70710678f);
        const float rings[] = {41,82,124,163};
        for (int j = 0; j < 4; ++j) distance = fminf(distance, fabsf(r - rings[j]));
        int coverage = (int)(15 * uv_clamp(1.5f - distance) + .5f);
        int angle = (int)((atan2f(dy,dx) * 57.295779513f + 450) * 4 + .5f) % UV_TURN;
        polar[i] = angle | (coverage << 11);
    }
}
static void uv_palette_ex(uint16_t *palette, uint32_t bg, uint32_t grid, uint32_t orange,uint32_t outside,uint32_t axis) {
    for (int lag = 0; lag <= UV_TRAIL; ++lag) {
        float t = lag < UV_TRAIL ? .23f * powf(1.f - (float)lag / UV_TRAIL, 1.6f) : 0;
        /* Soft leading ray, then a continuous faint falloff into the background. */
        if (lag < 4) t = .90f - .16f * lag;
        uint32_t base = uv_mix(bg, orange, t);
        for (int g = 0; g < 16; ++g) palette[lag * 16 + g] = uv_565(uv_mix(base,grid,g / 15.f));
    }
    for(int g=0;g<16;++g)palette[UV_STATIC_BASE+g]=uv_565(uv_mix(outside,axis,g/15.f));
}
static void uv_palette(uint16_t *palette,uint32_t bg,uint32_t grid,uint32_t orange){uv_palette_ex(palette,bg,grid,orange,bg,grid);}
static void uv_frame_area(uint16_t *pixels, const uint16_t *polar, const uint16_t *palette,
                     uint16_t bg, int angle, int running, int x0,int y0,int x1,int y1) {
    for (int y=y0;y<=y1;++y) for (int x=x0;x<=x1;++x) {
        int i=y*UV_SIDE+x;
        unsigned p = polar[i];
        if (p == UINT16_MAX) { pixels[i] = bg; continue; }
        if (p & 0x8000) {pixels[i]=palette[UV_STATIC_BASE+(p&15)];continue;}
        int lag = angle - (p & 2047);
        if (lag < 0) lag += UV_TURN;
        if (!running || lag >= UV_TRAIL) lag = UV_TRAIL;
        pixels[i] = palette[lag * 16 + (p >> 11)];
    }
}

static inline void uv_frame(uint16_t *pixels,const uint16_t *polar,const uint16_t *palette,uint16_t bg,int angle,int running) {uv_frame_area(pixels,polar,palette,bg,angle,running,0,0,UV_SIDE-1,UV_SIDE-1);}

/* Clip one row against the start and end rays of a clockwise wedge. The
 * intersection can only be empty or a single span because the wedge is <180°. */
static inline int uv_clip_ray(float dy, float sine, float cosine, float slope, float *left, float *right)
{
    if (fabsf(cosine) < .0001f) return sine * dy >= 0;
    float x = UV_CENTER - slope * dy;
    if (cosine > 0) *left = fmaxf(*left, x);
    else *right = fminf(*right, x);
    return *left <= *right;
}

static inline void uv_extend_damage(uv_damage_t *box, float x, float y)
{
    box->x0 = (int)fminf(box->x0, floorf(x));
    box->y0 = (int)fminf(box->y0, floorf(y));
    box->x1 = (int)fmaxf(box->x1, ceilf(x));
    box->y1 = (int)fmaxf(box->y1, ceilf(y));
}

/* Restore the old trail and draw the new one in a single pass. One quarter
 * degree of padding covers polar quantization in the geometry table. */
static inline uv_damage_t uv_sweep_update(uint16_t *pixels, const uint16_t *polar,
                                         const uint16_t *palette, uint16_t bg,
                                         int old_angle, int was_running,
                                         int angle, int running)
{
    int delta = angle - old_angle;
    if (delta > UV_TURN / 2) delta -= UV_TURN;
    if (delta < -UV_TURN / 2) delta += UV_TURN;
    if (was_running && running && abs(delta) > UV_TRAIL) {
        uv_frame(pixels, polar, palette, bg, angle, running);
        return (uv_damage_t){0, 0, UV_SIDE - 1, UV_SIDE - 1};
    }

    int first = was_running ? old_angle : angle;
    int last = running ? angle : old_angle;
    if (was_running && running) {
        first = old_angle + (delta < 0 ? delta : 0);
        last = old_angle + (delta > 0 ? delta : 0);
    }
    first -= UV_TRAIL + 1;
    last += 1;
    const float radians_per_tick = .00436332313f;
    const float sa = sinf(first * radians_per_tick), ca = cosf(first * radians_per_tick);
    const float sb = sinf(last * radians_per_tick), cb = cosf(last * radians_per_tick);
    const float slope_a = fabsf(ca) < .0001f ? 0 : sa / ca;
    const float slope_b = fabsf(cb) < .0001f ? 0 : sb / cb;
    uv_damage_t box = {215, 215, 216, 216};
    uv_extend_damage(&box, UV_CENTER + UV_SWEEP_RADIUS * sa, UV_CENTER - UV_SWEEP_RADIUS * ca);
    uv_extend_damage(&box, UV_CENTER + UV_SWEEP_RADIUS * sb, UV_CENTER - UV_SWEEP_RADIUS * cb);
    /* Endpoints and cardinal directions bound every point in the arc. */
    const int quarter = UV_TURN / 4;
    const int cardinal_x[] = {0, 1, 0, -1};
    const int cardinal_y[] = {-1, 0, 1, 0};
    for (int axis = 0; axis < 4; ++axis) {
        int offset = (axis * quarter - first + 2 * UV_TURN) % UV_TURN;
        if (offset <= last - first)
            uv_extend_damage(&box, UV_CENTER + UV_SWEEP_RADIUS * cardinal_x[axis],
                             UV_CENTER + UV_SWEEP_RADIUS * cardinal_y[axis]);
    }

    for (int y = box.y0; y <= box.y1; ++y) {
        float left = box.x0, right = box.x1;
        float dy = y - UV_CENTER;
        if (uv_clip_ray(dy, sa, ca, slope_a, &left, &right) &&
            uv_clip_ray(dy, -sb, -cb, slope_b, &left, &right))
            uv_frame_area(pixels, polar, palette, bg, angle, running,
                          (int)floorf(left), y, (int)ceilf(right), y);
    }
    return box;
}
