#include "lua_lvgl_private.h"
#include <stdatomic.h>
#include "src/misc/cache/instance/lv_image_cache.h"
#include "unveil_raster.h"
#include "freertos/semphr.h"
#include "unveil_mosaic.h"
#include "unveil_dial_mapping.h"
#include "unveil_dial_vector.h"
#include "unveil_pager_motion.h"

enum { UV_RADAR_TURN_MS = 6000, UV_RADAR_MAX_STEP_MS = 70 };

typedef struct {
    uint16_t *polar;
    uint16_t palette[UV_PALETTE_SIZE];
    uint32_t bg, outside, grid, orange, axis, last_tick;
    uint32_t phase_ms;
    int angle;
    bool running;
    lv_obj_t *obj;
    uint16_t *pixels;
    lv_timer_t *timer;
} unveil_radar_cache_t;

/* Both old and replacement radar instances borrow this immutable geometry. */
static uint16_t *uv_shared_polar;
static unsigned uv_polar_refs;
void lua_lvgl_radar_cache_free(void *ptr)
{
    unveil_radar_cache_t *cache=ptr;
    if (!cache) return;
    if (cache->timer) lv_timer_delete(cache->timer);
    if (cache->polar && --uv_polar_refs==0) {free(uv_shared_polar);uv_shared_polar=NULL;}
    free(cache);
}
static void unveil_radar_draw(unveil_radar_cache_t *cache,int angle,bool running,bool full)
{
    uv_damage_t damage = {0, 0, UV_SIDE - 1, UV_SIDE - 1};
    uint16_t outside = uv_565(cache->outside);
    if (full) {
        uv_frame(cache->pixels, cache->polar, cache->palette, outside, angle, running);
    } else {
        if (running == cache->running && (!running || angle == cache->angle)) return;
        damage = uv_sweep_update(cache->pixels, cache->polar, cache->palette, outside,
                                 cache->angle, cache->running, angle, running);
    }
    lv_area_t box;
    lv_obj_get_coords(cache->obj, &box);
    box.x2 = box.x1 + damage.x1;
    box.y2 = box.y1 + damage.y1;
    box.x1 += damage.x0;
    box.y1 += damage.y0;
    lv_obj_invalidate_area(cache->obj,&box);
    cache->angle=angle;cache->running=running;
}
static void unveil_radar_tick(lv_timer_t *timer)
{
    unveil_radar_cache_t *cache=lv_timer_get_user_data(timer);
    uint32_t now=lv_tick_get(),elapsed=now-cache->last_tick;cache->last_tick=now;
    if (!cache->running || !lv_obj_is_visible(cache->obj)) return;
    for (lv_obj_t *p=lv_obj_get_parent(cache->obj);p;p=lv_obj_get_parent(p)) {
        if (lv_obj_is_scrolling(p)) return;
    }
    /* Avoid a large visual jump on the first frame after a stalled render. */
    if (elapsed > UV_RADAR_MAX_STEP_MS) elapsed = UV_RADAR_MAX_STEP_MS;
    cache->phase_ms = (cache->phase_ms + elapsed) % UV_RADAR_TURN_MS;
    int angle = (cache->phase_ms * UV_TURN + UV_RADAR_TURN_MS / 2) / UV_RADAR_TURN_MS % UV_TURN;
    unveil_radar_draw(cache, angle, true, false);
}
int lua_lvgl_image_radar_frame(lua_State *L)
{
    lua_lvgl_obj_ud_t *ud=lua_lvgl_check_ud(L,1);
    int angle=(int)luaL_checkinteger(L,2)%UV_TURN;
    uint32_t bg=(uint32_t)luaL_checkinteger(L,3),grid=(uint32_t)luaL_checkinteger(L,4),orange=(uint32_t)luaL_checkinteger(L,5);
    bool running=lua_toboolean(L,6);
    uint32_t outside=(uint32_t)luaL_optinteger(L,7,bg);
    uint32_t axis=(uint32_t)luaL_optinteger(L,8,grid);
    if (angle<0) angle+=UV_TURN;
    lv_obj_t *obj=lua_lvgl_lock_widget(L,ud,LUA_LVGL_OBJ_IMAGE);
    int64_t started=esp_timer_get_time();
    lua_lvgl_obj_record_t *record=ud->record;
    bool fresh=record->radar_cache==NULL;
    if (fresh) {
        unveil_radar_cache_t *cache=heap_caps_calloc(1,sizeof(*cache),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
        if(!cache)cache=heap_caps_calloc(1,sizeof(*cache),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        uint16_t *pixels=heap_caps_malloc(UV_PIXELS*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        lv_image_dsc_t *dsc=calloc(1,sizeof(*dsc));
        if (!cache || !pixels || !dsc) {
            free(cache);free(pixels);free(dsc);lua_lvgl_unlock();return luaL_error(L,"radar cache allocation failed");
        }
        if(!uv_shared_polar){uv_shared_polar=heap_caps_malloc(UV_PIXELS*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(uv_shared_polar)uv_geometry(uv_shared_polar);}
        if(!uv_shared_polar){free(cache);free(pixels);free(dsc);lua_lvgl_unlock();return luaL_error(L,"radar geometry allocation failed");}
        cache->polar=uv_shared_polar;++uv_polar_refs;cache->obj=obj;cache->pixels=pixels;
        cache->angle = angle;
        cache->phase_ms = (angle * UV_RADAR_TURN_MS + UV_TURN / 2) / UV_TURN;
        dsc->header.magic=LV_IMAGE_HEADER_MAGIC;dsc->header.cf=LV_COLOR_FORMAT_RGB565;
        dsc->header.w=dsc->header.h=UV_SIDE;dsc->header.stride=UV_SIDE*2;
        dsc->data=(const uint8_t *)pixels;dsc->data_size=UV_PIXELS*2;
        if (record->data2) lv_image_cache_drop(record->data2);
        free(record->data);free(record->data2);
        record->radar_cache=cache;record->data=pixels;record->data2=dsc;
        record->data_size=UV_PIXELS*2;record->data2_size=sizeof(*dsc);record->data_width=record->data_height=UV_SIDE;
    }
    unveil_radar_cache_t *cache=record->radar_cache;
    bool full=fresh || axis!=cache->axis || outside!=cache->outside || bg!=cache->bg || grid!=cache->grid || orange!=cache->orange;
    if (full) {uv_palette_ex(cache->palette,bg,grid,orange,outside,axis);cache->axis=axis;cache->bg=bg;cache->outside=outside;cache->grid=grid;cache->orange=orange;}
    bool was_running = cache->running;
    unveil_radar_draw(cache,cache->angle,running,full);
    if (fresh) {lv_image_set_src(obj,record->data2);cache->timer=lv_timer_create(unveil_radar_tick,25,cache);}
    if (cache->timer && (fresh || running != was_running)) {
        if (running) {
            cache->last_tick = lv_tick_get();
            lv_timer_reset(cache->timer);
            lv_timer_resume(cache->timer);
        } else {
            lv_timer_pause(cache->timer);
        }
    }
    lua_lvgl_unlock();lua_pushinteger(L,(esp_timer_get_time()-started)/1000);return 1;
}
/* Pure numeric conversion: callable from a worker without owning the LVGL display. */
int lua_lvgl_chart_rgb565(lua_State *L)
{
    luaL_checktype(L,1,LUA_TTABLE);
    luaL_argcheck(L,lua_rawlen(L,1)==96*64,1,"chart requires 96x64 levels");
    bool dark=lua_toboolean(L,2);
    uint8_t *values=lua_newuserdatauv(L,96*64,0);
    for (int i=0;i<96*64;++i) {
        lua_rawgeti(L,1,i+1);lua_Integer v=luaL_checkinteger(L,-1);lua_pop(L,1);
        luaL_argcheck(L,v>=0 && v<=63,1,"chart level out of range");values[i]=v;
    }
    uint32_t bg=dark ? 0 : 0xe1e1e6;
    luaL_Buffer buffer;uint16_t *pixels=(uint16_t *)luaL_buffinitsize(L,&buffer,442*338*2);
    for (int i=0;i<442*338;++i) pixels[i]=uv_565(bg);
    uv_stft_mosaic(pixels,values,bg);
    luaL_pushresultsize(&buffer,442*338*2);return 1;
}

/* Website signal views, rendered locally from small numeric arrays. */
int lua_lvgl_signal_rgb565(lua_State *L)
{
    luaL_checktype(L,1,LUA_TTABLE);
    int kind=(int)luaL_checkinteger(L,2);bool dark=lua_toboolean(L,3);
    luaL_argcheck(L,kind>=1 && kind<=3,2,"unknown signal view");
    int low=(int)luaL_optinteger(L,4,-800),high=(int)luaL_optinteger(L,5,0);
    luaL_argcheck(L,high>low,5,"invalid spectrum range");
    int n=kind==1 ? 20 : 96;
    luaL_argcheck(L,lua_rawlen(L,1)==(size_t)n,1,"invalid signal values");
    int values[360];
    for (int i=0;i<n;++i) {lua_rawgeti(L,1,i+1);values[i]=(int)luaL_checkinteger(L,-1);lua_pop(L,1);}
    uint32_t bg=dark ? 0 : 0xe1e1e6,grid=dark ? 0x687280 : 0x8a919b,ink=0xf54b2c;
    luaL_Buffer buffer;uint16_t *pixels=(uint16_t *)luaL_buffinitsize(L,&buffer,442*338*2);
    uint16_t base=uv_565(bg),rule=uv_565(grid);
    for (int i=0;i<442*338;++i) pixels[i]=base;
    const int x0=20,y0=4,w=416,h=304;
    if (kind==3) for (int g=0;g<5;++g) {
        int x=x0+g*w/4,y=y0+g*h/4;
        for (int a=x0;a<=x0+w;++a) pixels[y*442+a]=rule;
        for (int a=y0;a<=y0+h;++a) pixels[a*442+x]=rule;
    }
    if (kind==1) {
        uv_activity_mosaic(pixels,values,bg,low,high);
    } else if (kind==2) {
        uv_spectrum_mosaic(pixels,values,bg,low,high);
    } else {
        int prevx=0,prevy=0;
        for (int i=0;i<96;++i) {
            float v=uv_clamp(kind==2 ? (float)(values[i]-low)/(high-low) : values[i]/1000.f);
            int x=x0+i*w/95,y=y0+(int)((1-v)*h+.5f);
            if (i) {
                int steps=abs(x-prevx)>abs(y-prevy) ? abs(x-prevx) : abs(y-prevy);
                if (steps<1) steps=1;
                for (int j=0;j<=steps;++j) {
                    int xx=prevx+(x-prevx)*j/steps,yy=prevy+(y-prevy)*j/steps;
                    pixels[yy*442+xx]=uv_565(ink);
                    if (yy<y0+h) pixels[(yy+1)*442+xx]=uv_565(ink);
                }
            }
            prevx=x;prevy=y;
        }
    }
    luaL_pushresultsize(&buffer,442*338*2);return 1;
}

typedef struct {lv_point_t start;int limit;bool moved;} unveil_tap_guard_t;
static void unveil_tap_guard_event(lv_event_t *event)
{
    unveil_tap_guard_t *guard=lv_event_get_user_data(event);
    lv_event_code_t code=lv_event_get_code(event);
    if (code==LV_EVENT_DELETE) {free(guard);return;}
    lv_indev_t *indev=lv_indev_active();
    if (!indev) return;
    lv_point_t p;lv_indev_get_point(indev,&p);
    if (code==LV_EVENT_PRESSED) {guard->start=p;guard->moved=false;}
    else if (code==LV_EVENT_PRESSING || code==LV_EVENT_RELEASED) {
        int dx=p.x-guard->start.x,dy=p.y-guard->start.y;
        if (dx*dx+dy*dy>guard->limit*guard->limit) guard->moved=true;
    } else if (code==LV_EVENT_CLICKED && guard->moved) lv_event_stop_processing(event);
}
int lua_lvgl_guard_tap(lua_State *L)
{
    lua_lvgl_obj_ud_t *ud=lua_lvgl_check_ud(L,1);
    int limit=(int)luaL_optinteger(L,2,18);
    luaL_argcheck(L,limit>=4 && limit<=100,2,"invalid tap limit");
    esp_err_t err=lua_lvgl_lock();
    if (err!=ESP_OK) return lua_lvgl_error_esp(L,"tap guard",err);
    const char *error=NULL;lv_obj_t *obj=lua_lvgl_validate_ud_locked(ud,NULL,&error);
    if (!obj) {lua_lvgl_unlock();return luaL_error(L,"%s",error);}
    unveil_tap_guard_t *guard=calloc(1,sizeof(*guard));
    if (!guard) {lua_lvgl_unlock();return luaL_error(L,"tap guard allocation failed");}
    guard->limit=limit;lv_obj_add_event_cb(obj,unveil_tap_guard_event,LV_EVENT_ALL,guard);
    lua_lvgl_unlock();lua_pushboolean(L,1);return 1;
}

/* Each glyph folds around its own horizontal hinge. No synthetic counts. */
typedef struct {char previous[2],next[2];bool switched;} uv_flap_t;
static void uv_flap_delete(lv_event_t *event) {free(lv_event_get_user_data(event));}
static void uv_flap_step(void *obj,int32_t phase)
{
    uv_flap_t *flap=lv_obj_get_user_data(obj);
    if (phase>=128 && !flap->switched) {lv_label_set_text(obj,flap->next);flap->switched=true;}
    int scale=phase<128 ? 256-phase*2 : (phase-128)*2;
    lv_obj_set_style_transform_scale_y(obj,scale,0);
    lv_obj_set_style_opa(obj,192+scale/4>255 ? 255 : 192+scale/4,0);
}
int lua_lvgl_label_set_text_motion(lua_State *L)
{
    lua_lvgl_obj_ud_t *ud=lua_lvgl_check_ud(L,1);const char *text=luaL_checkstring(L,2);
    int duration=(int)luaL_optinteger(L,3,240);
    luaL_argcheck(L,duration>=0 && duration<=400,3,"invalid text duration");
    lv_obj_t *obj=lua_lvgl_lock_widget(L,ud,LUA_LVGL_OBJ_LABEL);
    const char *old=lv_label_get_text(obj);
    if (!strcmp(text,old)) {lua_lvgl_unlock();return 0;}
    char previous[16];snprintf(previous,sizeof(previous),"%s",old);
    lv_obj_clean(obj);lv_obj_set_style_text_opa(obj,255,0);lv_label_set_text(obj,text);lv_obj_update_layout(obj);
    size_t n=strlen(text),old_n=strlen(previous);
    bool numeric=n>0 && n<8;
    for (size_t i=0;i<n && numeric;++i) numeric=text[i]>='0' && text[i]<='9';
    bool was_numeric=old_n>0 && old_n<8;
    for (size_t i=0;i<old_n && was_numeric;++i) was_numeric=previous[i]>='0' && previous[i]<='9';
    if (duration && numeric && was_numeric && n==old_n) {
        const lv_font_t *font=lv_obj_get_style_text_font(obj,LV_PART_MAIN);
        int spacing=lv_obj_get_style_text_letter_space(obj,LV_PART_MAIN);
        lv_point_t size;lv_text_get_size(&size,text,font,spacing,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
        lv_obj_update_layout(obj);
        int x=(lv_obj_get_content_width(obj)-size.x)/2;
        lv_obj_set_style_text_opa(obj,0,0);
        for (size_t i=0;i<n;++i) {
            char ch[2]={text[i],0};lv_point_t glyph;
            lv_text_get_size(&glyph,ch,font,0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
            lv_obj_t *digit=lv_label_create(obj);lv_obj_remove_flag(digit,LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_flag(digit,LV_OBJ_FLAG_IGNORE_LAYOUT);
            lv_obj_set_style_text_opa(digit,255,0);lv_obj_set_pos(digit,x,0);
            lv_font_glyph_dsc_t gd={0};int pivot=font->line_height/2;
            if(lv_font_get_glyph_dsc(font,&gd,(uint8_t)text[i],0)){pivot=font->line_height-font->base_line-gd.ofs_y-gd.box_h/2;lv_font_glyph_release_draw_data(&gd);}
            lv_obj_set_style_transform_pivot_y(digit,pivot,0);
            uv_flap_t *flap=calloc(1,sizeof(*flap));
            if (!flap) {lv_label_set_text(digit,ch);x+=glyph.x+spacing;continue;}
            int oi=(int)i+(int)old_n-(int)n;
            flap->previous[0]=oi>=0 ? previous[oi] : ' ';flap->next[0]=text[i];
            lv_obj_set_user_data(digit,flap);lv_obj_add_event_cb(digit,uv_flap_delete,LV_EVENT_DELETE,flap);
            lv_label_set_text(digit,flap->previous);
            if (flap->previous[0]!=text[i]) {
                lv_anim_t a;lv_anim_init(&a);lv_anim_set_var(&a,digit);
                lv_anim_set_duration(&a,duration);lv_anim_set_values(&a,0,256);
                lv_anim_set_exec_cb(&a,uv_flap_step);lv_anim_set_path_cb(&a,lv_anim_path_linear);lv_anim_start(&a);
            }
            x+=glyph.x+spacing;
        }
    }
    lua_lvgl_unlock();return 0;
}
static void uv_arc_sweep(void *obj,int32_t value)
{
    int start=lv_arc_get_bg_angle_start(obj);lv_arc_set_bg_angles(obj,start,(start+value)%360);
}
int lua_lvgl_arc_set_sweep_motion(lua_State *L)
{
    lua_lvgl_obj_ud_t *ud=lua_lvgl_check_ud(L,1);
    int sweep=(int)luaL_checkinteger(L,3),duration=(int)luaL_optinteger(L,6,180);
    luaL_argcheck(L,duration>=0 && duration<=400,6,"invalid arc duration");
    lv_obj_t *obj=lua_lvgl_lock_widget(L,ud,LUA_LVGL_OBJ_ARC);
    int old=((int)lroundf(lv_arc_get_bg_angle_end(obj)-lv_arc_get_bg_angle_start(obj))+360)%360;
    lv_anim_delete(obj,uv_arc_sweep);lua_lvgl_unlock();
    lua_lvgl_arc_set_sweep(L);
    obj=lua_lvgl_lock_widget(L,ud,LUA_LVGL_OBJ_ARC);
    if (duration && sweep>old) {
        lv_anim_t a;lv_anim_init(&a);lv_anim_set_var(&a,obj);lv_anim_set_duration(&a,duration);
        lv_anim_set_values(&a,old,sweep);lv_anim_set_exec_cb(&a,uv_arc_sweep);
        lv_anim_set_path_cb(&a,lv_anim_path_ease_out);lv_anim_start(&a);
    }
    lua_lvgl_unlock();return 0;
}
#include "src/draw/snapshot/lv_snapshot.h"
int lua_lvgl_screen_rgb565(lua_State *L)
{
    esp_err_t err=lua_lvgl_lock();
    if (err!=ESP_OK) return lua_lvgl_error_esp(L,"screen capture",err);
    lv_obj_t *screen=lv_screen_active();
    lv_draw_buf_t *buffer=screen ? lv_snapshot_take(screen,LV_COLOR_FORMAT_RGB565) : NULL;
    if (!buffer) {lua_lvgl_unlock();return luaL_error(L,"screen capture unavailable");}
    lua_pushlstring(L,(const char *)buffer->data,buffer->header.stride*buffer->header.h);
    lua_pushinteger(L,buffer->header.w);lua_pushinteger(L,buffer->header.h);
    lv_draw_buf_destroy(buffer);lua_lvgl_unlock();return 3;
}

extern bool uv_display_present_ready(lv_display_t *disp);
int lua_lvgl_present_ready(lua_State *L)
{
    esp_err_t err=lua_lvgl_lock();if (err!=ESP_OK) return lua_lvgl_error_esp(L,"first frame",err);
    if (!s_lvgl.runtime_initialized || s_lvgl.runtime_owner!=L) {lua_lvgl_unlock();return luaL_error(L,"first frame requires UI owner");}
    lv_obj_t *screen=lv_display_get_screen_active(s_lvgl.display);lv_obj_update_layout(screen);
    if (!uv_display_present_ready(s_lvgl.display)) {lua_lvgl_unlock();return luaL_error(L,"first-frame release failed");}
    ESP_LOGI("unveil_boot","first visible LVGL frame released: radar ready");
    lua_lvgl_unlock();return 0;
}
extern esp_err_t unveil_wifi_command(const char *,const char *,const char *,char *,size_t) __attribute__((weak));
int lua_lvgl_wifi(lua_State *L)
{
    const char *operation=luaL_optstring(L,1,"status");
    const char *ssid=!strcmp(operation,"enable") ? (lua_toboolean(L,2) ? "1" : "0") : luaL_optstring(L,2,NULL);
    const char *password=luaL_optstring(L,3,NULL);
    if (!unveil_wifi_command) return luaL_error(L,"Wi-Fi backend unavailable");
    char *out=malloc(8192);if (!out) return luaL_error(L,"Wi-Fi response allocation failed");
    esp_err_t err=unveil_wifi_command(operation,ssid,password,out,8192);
    if (err!=ESP_OK) {free(out);return luaL_error(L,"Wi-Fi: %s",esp_err_to_name(err));}
    lua_pushstring(L,out);free(out);return 1;
}
int lua_lvgl_textarea_get_text(lua_State *L)
{
    lua_lvgl_obj_ud_t *ud=lua_lvgl_check_ud(L,1);lv_obj_t *obj=lua_lvgl_lock_widget(L,ud,LUA_LVGL_OBJ_TEXTAREA);
    lua_pushstring(L,lv_textarea_get_text(obj));lua_lvgl_unlock();return 1;
}
int lua_lvgl_keyboard_set_palette(lua_State *L)
{
    lua_lvgl_obj_ud_t *ud=lua_lvgl_check_ud(L,1);
    uint32_t bg=luaL_checkinteger(L,2),card=luaL_checkinteger(L,3),fg=luaL_checkinteger(L,4),accent=luaL_checkinteger(L,5);
    lv_obj_t *obj=lua_lvgl_lock_widget(L,ud,LUA_LVGL_OBJ_KEYBOARD);
    lv_obj_set_style_bg_color(obj,lv_color_hex(bg),0);lv_obj_set_style_pad_all(obj,3,0);
    lv_obj_set_style_text_font(obj,lv_obj_get_style_text_font(obj,0),LV_PART_ITEMS);
    lv_obj_set_style_bg_color(obj,lv_color_hex(card),LV_PART_ITEMS);lv_obj_set_style_text_color(obj,lv_color_hex(fg),LV_PART_ITEMS);
    lv_obj_set_style_border_width(obj,0,LV_PART_ITEMS);lv_obj_set_style_radius(obj,6,LV_PART_ITEMS);
    lv_obj_set_style_bg_color(obj,lv_color_hex(accent),LV_PART_ITEMS|LV_STATE_PRESSED);
    lv_obj_set_style_text_color(obj,lv_color_hex(0x171619),LV_PART_ITEMS|LV_STATE_PRESSED);
    lua_lvgl_unlock();return 0;
}

extern esp_err_t unveil_sound_command(const char *,int,bool) __attribute__((weak));
int lua_lvgl_sound(lua_State *L)
{
 const char *kind=luaL_checkstring(L,1);int volume=luaL_optinteger(L,2,40);bool muted=lua_toboolean(L,3);
 if(!unveil_sound_command)return luaL_error(L,"Sound backend unavailable");
 esp_err_t err=unveil_sound_command(kind,volume,muted);if(err!=ESP_OK)return lua_lvgl_error_esp(L,"sound",err);
 lua_pushboolean(L,1);return 1;
}
int lua_lvgl_slider_set_palette(lua_State *L)
{
 lua_lvgl_obj_ud_t *ud=lua_lvgl_check_ud(L,1);uint32_t base=luaL_checkinteger(L,2),accent=luaL_checkinteger(L,3);
 lv_obj_t *obj=lua_lvgl_lock_widget(L,ud,LUA_LVGL_OBJ_SLIDER);
 lv_obj_set_style_bg_color(obj,lv_color_hex(base),LV_PART_MAIN);lv_obj_set_style_bg_color(obj,lv_color_hex(accent),LV_PART_INDICATOR);lv_obj_set_style_bg_color(obj,lv_color_hex(accent),LV_PART_KNOB);lv_obj_set_style_pad_all(obj,7,LV_PART_KNOB);
 lua_lvgl_unlock();return 0;
}

/* One numeric source only (~6.5KB). Raster pixels belong only to the visible image. */
typedef struct {uint8_t stft[6144];int activity[20],spectrum[96];int alo,ahi,slo,shi,generation;uint32_t token;} uv_chart_source_t;
static uv_chart_source_t *uv_chart_source;
static SemaphoreHandle_t uv_chart_mutex;
static portMUX_TYPE uv_chart_init_mux=portMUX_INITIALIZER_UNLOCKED;
static uint32_t uv_chart_serial;
static SemaphoreHandle_t uv_chart_lock(void)
{
 if(!uv_chart_mutex) {
  SemaphoreHandle_t candidate=xSemaphoreCreateMutex();
  if(!candidate)return NULL;
  portENTER_CRITICAL(&uv_chart_init_mux);
  if(!uv_chart_mutex){uv_chart_mutex=candidate;candidate=NULL;}
  portEXIT_CRITICAL(&uv_chart_init_mux);
  if(candidate)vSemaphoreDelete(candidate);
 }
 xSemaphoreTake(uv_chart_mutex,portMAX_DELAY);return uv_chart_mutex;
}
int lua_lvgl_chart_publish(lua_State *L)
{
 luaL_checktype(L,1,LUA_TTABLE);luaL_checktype(L,2,LUA_TTABLE);luaL_checktype(L,3,LUA_TTABLE);
 luaL_argcheck(L,lua_rawlen(L,1)==6144&&lua_rawlen(L,2)==20&&lua_rawlen(L,3)==96,1,"invalid chart arrays");
 uv_chart_source_t *data=heap_caps_calloc(1,sizeof(*data),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
 if(!data)return luaL_error(L,"chart source allocation failed");
 for(int group=0;group<3;++group)for(int i=0;i<(group==0 ? 6144 : group==1 ? 20 : 96);++i) {
  lua_rawgeti(L,group+1,i+1);int valid=0;lua_Integer v=lua_tointegerx(L,-1,&valid);lua_pop(L,1);
  if(!valid||v<(group==0 ? 0 : -32768)||v>(group==0 ? 63 : 32767)){free(data);return luaL_error(L,"invalid numeric chart value");}
  if(group==0)data->stft[i]=v;else if(group==1)data->activity[i]=v;else data->spectrum[i]=v;
 }
 data->alo=luaL_checkinteger(L,4);data->ahi=luaL_checkinteger(L,5);data->slo=luaL_checkinteger(L,6);data->shi=luaL_checkinteger(L,7);data->generation=luaL_optinteger(L,8,0);
 if(data->ahi<=data->alo||data->shi<=data->slo){free(data);return luaL_error(L,"invalid chart range");}
 SemaphoreHandle_t lock=uv_chart_lock();if(!lock){free(data);return luaL_error(L,"chart lock allocation failed");}
 if(uv_chart_source&&data->generation<uv_chart_source->generation){xSemaphoreGive(lock);free(data);lua_pushinteger(L,0);return 1;}
 data->token=++uv_chart_serial;free(uv_chart_source);uv_chart_source=data;uint32_t token=data->token;xSemaphoreGive(lock);lua_pushinteger(L,token);return 1;
}
int lua_lvgl_chart_clear(lua_State *L)
{
 SemaphoreHandle_t lock=uv_chart_lock();if(!lock)return luaL_error(L,"chart lock unavailable");
 free(uv_chart_source);uv_chart_source=NULL;++uv_chart_serial;xSemaphoreGive(lock);return 0;
}
int lua_lvgl_image_set_chart(lua_State *L)
{
 lua_lvgl_obj_ud_t *ud=lua_lvgl_check_ud(L,1);int kind=luaL_checkinteger(L,2);uint32_t token=luaL_checkinteger(L,3);bool dark=lua_toboolean(L,4);
 luaL_argcheck(L,kind>=1&&kind<=3,2,"invalid chart view");
 lv_obj_t *obj=lua_lvgl_lock_widget(L,ud,LUA_LVGL_OBJ_IMAGE);SemaphoreHandle_t lock=uv_chart_lock();
 if(!lock||!uv_chart_source||uv_chart_source->token!=token){if(lock)xSemaphoreGive(lock);lua_lvgl_unlock();lua_pushboolean(L,0);return 1;}
 const size_t bytes=442*338*2;lua_lvgl_obj_record_t *record=ud->record;
 if(!record->data||record->data_size!=bytes||!record->data2) {
  uint16_t *pixels=heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);lv_image_dsc_t *dsc=calloc(1,sizeof(*dsc));
  if(!pixels||!dsc){free(pixels);free(dsc);xSemaphoreGive(lock);lua_lvgl_unlock();return luaL_error(L,"chart pixels allocation failed");}
  if(record->data2)lv_image_cache_drop(record->data2);
  free(record->data);free(record->data2);record->data=pixels;record->data2=dsc;record->data_size=bytes;record->data2_size=sizeof(*dsc);record->data_width=442;record->data_height=338;
  dsc->header.magic=LV_IMAGE_HEADER_MAGIC;dsc->header.cf=LV_COLOR_FORMAT_RGB565;dsc->header.w=442;dsc->header.h=338;dsc->header.stride=884;dsc->data=(void *)pixels;dsc->data_size=bytes;
 }
 uint16_t *pixels=record->data;uint32_t bg=dark ? 0 : 0xe1e1e6;uint16_t base=uv_565(bg);
 for(int i=0;i<442*338;++i)pixels[i]=base;
 if(kind==1)uv_spectrum_mosaic(pixels,uv_chart_source->spectrum,bg,uv_chart_source->slo,uv_chart_source->shi);
 else if(kind==2)uv_stft_mosaic(pixels,uv_chart_source->stft,bg);
 else uv_activity_mosaic(pixels,uv_chart_source->activity,bg,uv_chart_source->alo,uv_chart_source->ahi);
 xSemaphoreGive(lock);lv_image_cache_drop(record->data2);lv_image_set_src(obj,record->data2);lv_obj_invalidate(obj);lua_lvgl_unlock();lua_pushboolean(L,1);return 1;
}

int lua_lvgl_label_center_ink(lua_State *L)
{
 lua_lvgl_obj_ud_t *ud=lua_lvgl_check_ud(L,1);lv_obj_t *obj=lua_lvgl_lock_widget(L,ud,LUA_LVGL_OBJ_LABEL);
 const lv_font_t *font=lv_obj_get_style_text_font(obj,0);const char *text=lv_label_get_text(obj);int spacing=lv_obj_get_style_text_letter_space(obj,0);
 int minx=100000,miny=100000,maxx=-100000,maxy=-100000,cursor=0;
 for(size_t i=0;text[i];++i) {
  lv_font_glyph_dsc_t glyph={0};
  if(!lv_font_get_glyph_dsc(font,&glyph,(uint8_t)text[i],(uint8_t)text[i+1]))continue;
  if(glyph.box_w&&glyph.box_h) {
   int x=cursor+glyph.ofs_x,y=font->line_height-font->base_line-glyph.box_h-glyph.ofs_y;
   if(x<minx)minx=x;
   if(y<miny)miny=y;
   if(x+glyph.box_w>maxx)maxx=x+glyph.box_w;
   if(y+glyph.box_h>maxy)maxy=y+glyph.box_h;
  }
  cursor+=glyph.adv_w+spacing;lv_font_glyph_release_draw_data(&glyph);
 }
 lv_obj_update_layout(obj);int w=lv_obj_get_width(obj),h=lv_obj_get_height(obj);
 int dx=maxx>=minx ? (int)lroundf(w*.5f-(minx+maxx)*.5f) : 0;
 int dy=maxy>=miny ? (int)lroundf(h*.5f-(miny+maxy)*.5f) : 0;
 lv_obj_align(obj,LV_ALIGN_CENTER,dx,dy);lv_obj_update_layout(obj);
 lv_obj_t *parent=lv_obj_get_parent(obj);float cx=lv_obj_get_content_width(parent)*.5f,cy=lv_obj_get_content_height(parent)*.5f;
 lua_lvgl_unlock();lua_pushnumber(L,cx+(maxx>=minx ? (maxx-minx)*.5f : w*.5f));lua_pushnumber(L,cy+(maxy>=miny ? (maxy-miny)*.5f : h*.5f));return 2;
}
/* LVGL vector widget bindings. */
int lua_lvgl_dial_init(lua_State *L)
{
    lua_lvgl_obj_ud_t *ud=lua_lvgl_check_ud(L,1);lv_obj_t *obj=lua_lvgl_lock_widget(L,ud,LUA_LVGL_OBJ_GENERIC);
    bool ok=unveil_dial_vector_attach(obj);lua_lvgl_unlock();if(!ok)return luaL_error(L,"vector dial initialization failed");return 0;
}
int lua_lvgl_dial_values(lua_State *L)
{
    int levels[3];for(int i=0;i<3;++i)levels[i]=luaL_checkinteger(L,i+2);
    lua_lvgl_obj_ud_t *ud=lua_lvgl_check_ud(L,1);lv_obj_t *obj=lua_lvgl_lock_widget(L,ud,LUA_LVGL_OBJ_GENERIC);
    int64_t started=esp_timer_get_time();bool ok=unveil_dial_vector_set(obj,levels);int64_t elapsed=esp_timer_get_time()-started;
    lua_lvgl_unlock();if(!ok)return luaL_error(L,"invalid vector dial indices");lua_pushinteger(L,elapsed);return 1;
}

int lua_lvgl_dial_motion(lua_State *L)
{
    const bool prepare=lua_isnoneornil(L,2);
    const bool active=lua_toboolean(L,2);
    lua_lvgl_obj_ud_t *ud=lua_lvgl_check_ud(L,1);
    lv_obj_t *obj=lua_lvgl_lock_widget(L,ud,LUA_LVGL_OBJ_GENERIC);
    if(prepare)unveil_dial_vector_prepare_motion(obj);
    else unveil_dial_vector_paging(obj,active);
    lua_lvgl_unlock();
    return 0;
}

/* One pointer owner, instead of 80 independently scrolling transparent sliders. */
typedef struct {lv_point_t start;int sector,counts[3],value;float radius;unsigned mode;} uv_radial_t;
static void uv_radial_emit(lv_obj_t *obj,uv_radial_t *g,const lv_point_t *p)
{
    const float x = p->x - UV_DIAL_CENTER_X;
    const float y = p->y - UV_DIAL_CENTER_Y;
    const float radius = g->mode == 1
        ? uv_dial_drag_radius(x, y, g->start.x - UV_DIAL_CENTER_X,
                              g->start.y - UV_DIAL_CENTER_Y, g->radius)
        : sqrtf(x * x + y * y);
    int n=uv_dial_index_from_radius(radius,g->counts[g->sector]);
    int v=(g->sector+1)*100+n;if(v!=g->value){g->value=v;lv_obj_send_event(obj,LV_EVENT_VALUE_CHANGED,NULL);}
}
static void uv_radial_event(lv_event_t *e)
{
    uv_radial_t *g=lv_event_get_user_data(e);lv_obj_t *obj=lv_event_get_target(e);lv_event_code_t code=lv_event_get_code(e);
    if(code==LV_EVENT_DELETE){free(g);return;}
    lv_indev_t *indev=lv_indev_active();if(!indev)return;
    lv_point_t p;lv_indev_get_point(indev,&p);lv_area_t area;lv_obj_get_coords(obj,&area);p.x-=area.x1;p.y-=area.y1;
    if(code==LV_EVENT_PRESSED){
        g->start=p;float x=p.x-UV_DIAL_CENTER_X,y=p.y-UV_DIAL_CENTER_Y;g->radius=sqrtf(x*x+y*y);
        g->sector=y<0&&fabsf(x)<-y*1.7320508f?0:x<0?1:2;
        g->mode=g->radius<UV_DIAL_INNER_RADIUS||g->radius>UV_DIAL_OUTER_RADIUS+1?3:0;
        lv_obj_add_flag(obj,LV_OBJ_FLAG_SCROLL_CHAIN_HOR|LV_OBJ_FLAG_SCROLL_CHAIN_VER);
    }else if(code==LV_EVENT_PRESSING&&g->mode!=3){
        float dx=p.x-g->start.x,dy=p.y-g->start.y;
        if(!g->mode&&dx*dx+dy*dy>=100){
            float rx=g->start.x-UV_DIAL_CENTER_X,ry=g->start.y-UV_DIAL_CENTER_Y;
            float radial=fabsf(dx*rx+dy*ry),tangent=fabsf(dx*ry-dy*rx);
            /* Outer horizontal strokes belong to navigation, including edge-to-edge swipes. */
            if(g->start.x<96||g->start.x>384||(g->radius>140&&fabsf(dx)>fabsf(dy)*1.4f)||radial<tangent*1.4f)g->mode=3;
            else{g->mode=1;lv_obj_remove_flag(obj,LV_OBJ_FLAG_SCROLL_CHAIN_HOR|LV_OBJ_FLAG_SCROLL_CHAIN_VER);}
        }
        if(g->mode==1)uv_radial_emit(obj,g,&p);
    }else if(code==LV_EVENT_RELEASED){
        if(g->mode==0||g->mode==1)uv_radial_emit(obj,g,&p);
        /* Restore chaining at the next press, after this release has finished processing. */
    }else if(code==LV_EVENT_PRESS_LOST)g->mode=3;
}
int lua_lvgl_radial_input(lua_State *L)
{
    int counts[3];for(int i=0;i<3;++i){counts[i]=luaL_checkinteger(L,i+2);luaL_argcheck(L,counts[i]>=2&&counts[i]<=16,i+2,"invalid radial count");}
    lua_lvgl_obj_ud_t *ud=lua_lvgl_check_ud(L,1);lv_obj_t *obj=lua_lvgl_lock_widget(L,ud,LUA_LVGL_OBJ_CONTAINER);
    if(lv_obj_get_user_data(obj)){lua_lvgl_unlock();return luaL_error(L,"radial input already initialized");}
    uv_radial_t *g=calloc(1,sizeof(*g));if(!g){lua_lvgl_unlock();return luaL_error(L,"radial input allocation failed");}
    memcpy(g->counts,counts,sizeof(counts));lv_obj_set_user_data(obj,g);lv_obj_add_flag(obj,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_PRESS_LOCK);lv_obj_remove_flag(obj,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(obj,uv_radial_event,LV_EVENT_ALL,g);lua_lvgl_unlock();return 0;
}
int lua_lvgl_radial_value(lua_State *L)
{
    lua_lvgl_obj_ud_t *ud=lua_lvgl_check_ud(L,1);lv_obj_t *obj=lua_lvgl_lock_widget(L,ud,LUA_LVGL_OBJ_CONTAINER);uv_radial_t *g=lv_obj_get_user_data(obj);int value=g?g->value:0;lua_lvgl_unlock();lua_pushinteger(L,value);return 1;
}

/* Virtualized lists keep a small row pool and query the scroll offset. */
int lua_lvgl_scroll_y(lua_State *L)
{
    lua_lvgl_obj_ud_t *ud=lua_lvgl_check_ud(L,1);bool set=!lua_isnoneornil(L,2);
    int value=set?luaL_checkinteger(L,2):0;luaL_argcheck(L,!set||(value>=0&&value<=100000),2,"invalid scroll offset");
    esp_err_t err=lua_lvgl_lock();if(err!=ESP_OK)return lua_lvgl_error_esp(L,"scroll offset lock",err);
    const char *error=NULL;lv_obj_t *obj=lua_lvgl_validate_ud_locked(ud,NULL,&error);
    if(!obj){lua_lvgl_unlock();return luaL_error(L,"%s",error);}
    if(set)lv_obj_scroll_to_y(obj,value,LV_ANIM_OFF);
    int y=lv_obj_get_scroll_y(obj);lua_lvgl_unlock();lua_pushinteger(L,y);return 1;
}

/* Board ISR/task callbacks only signal; all UI/network work stays on the Lua owner. */
static atomic_bool unveil_scan_key_pending;
void unveil_scan_key_press(void) {atomic_store(&unveil_scan_key_pending,true);}
int lua_lvgl_scan_key(lua_State *L)
{
    if(!lua_isnoneornil(L,1)){
        const char *action=luaL_checkstring(L,1);
        luaL_argcheck(L,!strcmp(action,"inject"),1,"expected inject");
        unveil_scan_key_press();lua_pushboolean(L,true);return 1;
    }
    if(!s_lvgl.runtime_initialized||s_lvgl.runtime_owner!=L)return luaL_error(L,"scan_key requires UI owner");
    lua_pushboolean(L,atomic_exchange(&unveil_scan_key_pending,false));return 1;
}
int lua_lvgl_pager_motion(lua_State *L)
{
    lua_lvgl_obj_ud_t *ud=lua_lvgl_check_ud(L,1);lv_obj_t *obj=lua_lvgl_lock_widget(L,ud,LUA_LVGL_OBJ_TILEVIEW);
    unveil_pager_motion_attach(obj);lua_lvgl_unlock();return 0;
}
static void uv_debug_rows(lua_State *L,lv_obj_t *obj,int depth,int *row)
{
    lv_area_t b;lv_obj_get_coords(obj,&b);lua_createtable(L,0,10);
    lua_pushinteger(L,depth);lua_setfield(L,-2,"depth");
    lua_pushinteger(L,b.x1);lua_setfield(L,-2,"x");lua_pushinteger(L,b.y1);lua_setfield(L,-2,"y");
    lua_pushinteger(L,lv_area_get_width(&b));lua_setfield(L,-2,"w");lua_pushinteger(L,lv_area_get_height(&b));lua_setfield(L,-2,"h");
    lua_pushinteger(L,lv_obj_get_scroll_x(obj));lua_setfield(L,-2,"sx");lua_pushinteger(L,lv_obj_get_scroll_y(obj));lua_setfield(L,-2,"sy");
    lua_pushboolean(L,lv_obj_is_visible(obj));lua_setfield(L,-2,"visible");
    if(lv_obj_check_type(obj,&lv_label_class)){lua_pushstring(L,lv_label_get_text(obj));lua_setfield(L,-2,"label");}
    lua_rawseti(L,-2,++*row);
    if(depth<2)for(uint32_t i=0;i<lv_obj_get_child_count(obj);++i)uv_debug_rows(L,lv_obj_get_child(obj,i),depth+1,row);
}
int lua_lvgl_ui_tree(lua_State *L)
{
    esp_err_t err=lua_lvgl_lock();if(err!=ESP_OK)return lua_lvgl_error_esp(L,"inspect UI",err);
    lua_newtable(L);int row=0;lv_obj_t *screen=lv_screen_active();if(screen)uv_debug_rows(L,screen,0,&row);
    lua_lvgl_unlock();return 1;
}

int lua_lvgl_pager_home(lua_State *L)
{
    lua_lvgl_obj_ud_t *ud=lua_lvgl_check_ud(L,1);lv_obj_t *obj=lua_lvgl_lock_widget(L,ud,LUA_LVGL_OBJ_TILEVIEW);
    unveil_pager_motion_home(obj);lua_lvgl_unlock();return 0;
}

int lua_lvgl_pager_home_settled(lua_State *L)
{
 lua_lvgl_obj_ud_t *ud=lua_lvgl_check_ud(L,1);
 lv_obj_t *obj=lua_lvgl_lock_widget(L,ud,LUA_LVGL_OBJ_TILEVIEW);
 bool ready=!lv_obj_is_scrolling(obj)&&lv_obj_get_scroll_x(obj)==0&&lv_obj_get_scroll_y(obj)==0;
 lua_lvgl_unlock();lua_pushboolean(L,ready);return 1;
}
int lua_lvgl_invalidate(lua_State *L)
{
 lua_lvgl_obj_ud_t *ud=lua_lvgl_check_ud(L,1);
 esp_err_t error=lua_lvgl_lock();if(error!=ESP_OK)return lua_lvgl_error_esp(L,"invalidate lock",error);
 const char *reason=NULL;lv_obj_t *obj=lua_lvgl_validate_ud_locked(ud,NULL,&reason);
 if(!obj){lua_lvgl_unlock();return luaL_error(L,"%s",reason?reason:"invalid object");}
 lv_obj_invalidate(obj);lua_lvgl_unlock();return 0;
}
