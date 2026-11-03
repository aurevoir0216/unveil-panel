/* LVGL vector paths only: no source images, pixel buffers or asset reads. */
#include "unveil_dial_vector.h"
#include "unveil_dial_mapping.h"
#include <math.h>
#include <string.h>
#define UV_DIAL_MAGIC 0x55445631u
#define UV_DIAL_ROUND 9.f
#define UV_DIAL_GAP 4.f
#define UV_DIAL_HOLE 30.f
#define UV_DIAL_LOCAL_CENTER 175.f
#define UV_PI 3.14159265358979323846f
static const int counts[3]={3,5,6};
static const float angles[3]={-90.f,150.f,30.f};
typedef struct {
    lv_vector_path_t *base,*fill[6];
    lv_obj_t *obj;
    lv_area_t offset;
    int q,level;
} uv_dial_sector_t;
typedef struct { uint32_t magic; uv_dial_sector_t *sector[3]; } uv_dial_vector_t;
static lv_fpoint_t polar(float r,float a)
{
    return (lv_fpoint_t){UV_DIAL_LOCAL_CENTER+r*cosf(a),UV_DIAL_LOCAL_CENTER+r*sinf(a)};
}
static void append_arc_at(lv_vector_path_t *path,float cx,float cy,float radius,float start,float sweep)
{
    int segments=(int)ceilf(fabsf(sweep)/(UV_PI/3.f));
    float step=sweep/segments;
    for(int i=0;i<segments;++i){
        float a=start+i*step,b=a+step,k=4.f/3.f*tanf(step/4.f);
        lv_fpoint_t p0={cx+radius*cosf(a),cy+radius*sinf(a)};
        lv_fpoint_t p3={cx+radius*cosf(b),cy+radius*sinf(b)};
        lv_fpoint_t p1={p0.x-k*radius*sinf(a),p0.y+k*radius*cosf(a)};
        lv_fpoint_t p2={p3.x+k*radius*sinf(b),p3.y-k*radius*cosf(b)};
        lv_vector_path_cubic_to(path,&p1,&p2,&p3);
    }
}
static void arc(lv_vector_path_t *path,float radius,float start,float sweep)
{
    append_arc_at(path,UV_DIAL_LOCAL_CENTER,UV_DIAL_LOCAL_CENTER,radius,start,sweep);
}
static void corner(lv_vector_path_t *path,float radius,float angle,float start,float sweep)
{
    lv_fpoint_t center=polar(radius,angle);
    append_arc_at(path,center.x,center.y,UV_DIAL_ROUND,start,sweep);
}
static void side_end(lv_vector_path_t *path,float radius,float angle,float normal)
{
    lv_fpoint_t p=polar(radius,angle);
    p.x+=UV_DIAL_ROUND*cosf(normal);p.y+=UV_DIAL_ROUND*sinf(normal);
    lv_vector_path_line_to(path,&p);
}
static void shape(lv_vector_path_t *path,int q,float radius)
{
    /* Analytic offset contour: identical rounded sector, a single fill, no stroker. */
    const float outer=radius-UV_DIAL_ROUND,inner=UV_DIAL_HOLE+UV_DIAL_ROUND;
    const float offset=UV_DIAL_GAP+UV_DIAL_ROUND,axis=angles[q]*UV_PI/180.f;
    float bo=UV_PI/3.f-asinf(offset/outer),bi=UV_PI/3.f-asinf(offset/inner);
    float right=axis+5*UV_PI/6.f,left=axis+7*UV_PI/6.f;
    lv_vector_path_clear(path);
    lv_fpoint_t p=polar(radius,axis-bo);lv_vector_path_move_to(path,&p);
    arc(path,radius,axis-bo,2*bo);
    corner(path,outer,axis+bo,axis+bo,5*UV_PI/6.f-bo);
    side_end(path,inner,axis+bi,right);
    corner(path,inner,axis+bi,right,bi+UV_PI/6.f);
    arc(path,UV_DIAL_HOLE,axis+bi,-2*bi);
    corner(path,inner,axis-bi,axis-bi+UV_PI,bi+UV_PI/6.f);
    side_end(path,outer,axis-bo,left);
    corner(path,outer,axis-bo,left,5*UV_PI/6.f-bo);
    lv_vector_path_close(path);
}
static float level_radius(int q,int level)
{
    return UV_DIAL_INNER_RADIUS+(float)(UV_DIAL_OUTER_RADIUS-UV_DIAL_INNER_RADIUS)*level/counts[q];
}
static bool clip_box(lv_area_t *out,const lv_area_t *a,const lv_area_t *b)
{
    out->x1=LV_MAX(a->x1,b->x1);out->y1=LV_MAX(a->y1,b->y1);
    out->x2=LV_MIN(a->x2,b->x2);out->y2=LV_MIN(a->y2,b->y2);
    return out->x1<=out->x2&&out->y1<=out->y2;
}
static void color_path(lv_draw_vector_dsc_t *dsc,lv_layer_t *layer,const lv_area_t *origin,
                       lv_vector_path_t *path,uint32_t color)
{
    lv_area_t bounds,clip,saved=layer->_clip_area;lv_vector_path_get_bounding(path,&bounds);
    bounds.x1+=origin->x1-1;bounds.x2+=origin->x1+1;
    bounds.y1+=origin->y1-1;bounds.y2+=origin->y1+1;
    if(!clip_box(&clip,&saved,&bounds))return;
    lv_draw_vector_dsc_set_fill_color(dsc,lv_color_hex(color));
    lv_draw_vector_dsc_add_path(dsc,path);
}
static void sector_event(lv_event_t *e)
{
    uv_dial_sector_t *d=lv_event_get_user_data(e);
    if(lv_event_get_code(e)==LV_EVENT_DELETE){
        lv_vector_path_delete(d->base);
        for(int i=0;i<counts[d->q];++i)if(d->fill[i])lv_vector_path_delete(d->fill[i]);
        lv_free(d);return;
    }
    if(lv_event_get_code(e)!=LV_EVENT_DRAW_MAIN)return;
    lv_layer_t *layer=lv_event_get_layer(e);lv_area_t box;lv_obj_get_coords(d->obj,&box);
    lv_area_t saved_clip=layer->_clip_area,clip;
    if(!clip_box(&clip,&saved_clip,&box))return;
    layer->_clip_area=clip;
    lv_draw_vector_dsc_t *v=lv_draw_vector_dsc_create(layer);
    if(!v){layer->_clip_area=saved_clip;return;}
    lv_area_t origin=box;origin.x1-=d->offset.x1;origin.y1-=d->offset.y1;
    lv_draw_vector_dsc_translate(v,origin.x1,origin.y1);
    lv_draw_vector_dsc_set_fill_opa(v,LV_OPA_COVER);
    lv_draw_vector_dsc_set_stroke_width(v,0);
    if(d->level!=counts[d->q])color_path(v,layer,&origin,d->base,0x625e63);
    if(d->level&&d->fill[d->level-1])color_path(v,layer,&origin,d->fill[d->level-1],0xdcd5e0);
    lv_draw_vector(v);lv_draw_vector_dsc_delete(v);layer->_clip_area=saved_clip;
}
static void root_deleted(lv_event_t *e)
{
    lv_obj_set_user_data(lv_event_get_target(e),NULL);
    lv_free(lv_event_get_user_data(e));
}
bool unveil_dial_vector_attach(lv_obj_t *obj)
{
    if(lv_obj_get_user_data(obj))return false;
    uv_dial_vector_t *d=lv_malloc_zeroed(sizeof(*d));if(!d)return false;
    d->magic=UV_DIAL_MAGIC;lv_obj_set_user_data(obj,d);
    lv_obj_remove_flag(obj,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(obj,root_deleted,LV_EVENT_DELETE,d);
    for(int q=0;q<3;++q){
        uv_dial_sector_t *sector=lv_malloc_zeroed(sizeof(*sector));
        if(!sector)return false;
        sector->q=q;
        sector->base=lv_vector_path_create(LV_VECTOR_PATH_QUALITY_HIGH);
        if(!sector->base){lv_free(sector);return false;}
        shape(sector->base,q,UV_DIAL_OUTER_RADIUS);
        /* Geometry is constructed once at runtime, then selected without rebuilding paths. */
        for(int i=0;i<counts[q];++i){
            if(q==2&&i==0)continue;
            sector->fill[i]=lv_vector_path_create(LV_VECTOR_PATH_QUALITY_HIGH);
            if(!sector->fill[i]){
                for(int j=0;j<i;++j)if(sector->fill[j])lv_vector_path_delete(sector->fill[j]);
                lv_vector_path_delete(sector->base);lv_free(sector);return false;
            }
            shape(sector->fill[i],q,level_radius(q,i+1));
        }
        lv_vector_path_get_bounding(sector->base,&sector->offset);
        sector->offset.x1=LV_MAX(0,sector->offset.x1-2);sector->offset.y1=LV_MAX(0,sector->offset.y1-2);
        sector->offset.x2=LV_MIN(349,sector->offset.x2+2);sector->offset.y2=LV_MIN(349,sector->offset.y2+2);
        sector->obj=lv_obj_create(obj);lv_obj_remove_style_all(sector->obj);
        lv_obj_set_pos(sector->obj,sector->offset.x1,sector->offset.y1);
        lv_obj_set_size(sector->obj,lv_area_get_width(&sector->offset),lv_area_get_height(&sector->offset));
        lv_obj_remove_flag(sector->obj,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(sector->obj,sector_event,LV_EVENT_ALL,sector);
        d->sector[q]=sector;
    }
    return true;
}
static void invalidate_band(uv_dial_sector_t *sector,int q,float old_radius,float new_radius,bool was_off,bool is_off)
{
    /* Rounded corners can change up to one stroke diameter behind the front. */
    float inner=was_off||is_off?UV_DIAL_HOLE:fminf(old_radius,new_radius)-2*UV_DIAL_ROUND-2;
    float outer=fmaxf(old_radius,new_radius)+2;
    /* Exact cardinal extrema of the three 120-degree wedges; no per-drag trig. */
    float minx,miny,maxx,maxy;
    if(q==0){minx=175.f-.866025404f*outer;maxx=175.f+.866025404f*outer;miny=175.f-outer;maxy=175.f-.5f*inner;}
    else if(q==1){minx=175.f-outer;maxx=175.f;miny=175.f-.5f*outer;maxy=175.f+outer;}
    else{minx=175.f;maxx=175.f+outer;miny=175.f-.5f*outer;maxy=175.f+outer;}
    lv_obj_t *obj=sector->obj;lv_area_t area;lv_obj_get_coords(obj,&area);
    area.x1-=sector->offset.x1;area.y1-=sector->offset.y1;
    area.x2=area.x1+(int)ceilf(maxx);area.y2=area.y1+(int)ceilf(maxy);
    area.x1+=(int)floorf(minx);area.y1+=(int)floorf(miny);lv_obj_invalidate_area(obj,&area);
}
bool unveil_dial_vector_set(lv_obj_t *obj,const int levels[3])
{
    uv_dial_vector_t *d=lv_obj_get_user_data(obj);if(!d||d->magic!=UV_DIAL_MAGIC)return false;
    for(int q=0;q<3;++q)if(levels[q]<1||levels[q]>counts[q])return false;
    for(int q=0;q<3;++q){
        uv_dial_sector_t *sector=d->sector[q];
        if(sector->level==levels[q])continue;
        int old=sector->level;sector->level=levels[q];
        if(!old)lv_obj_invalidate(sector->obj);
        else invalidate_band(sector,q,level_radius(q,old),level_radius(q,levels[q]),old==counts[q]||(q==2&&old==1),levels[q]==counts[q]||(q==2&&levels[q]==1));
    }
    return true;
}
