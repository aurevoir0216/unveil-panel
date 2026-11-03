#include "unveil_pager_motion.h"
static void uv_pager_press(lv_event_t *e)
{
    lv_obj_t *obj=lv_event_get_user_data(e);
    if(!lv_obj_is_visible(obj))return;
    lv_indev_t *indev=lv_event_get_target(e);lv_point_t p;lv_area_t box;
    lv_indev_get_point(indev,&p);lv_obj_get_coords(obj,&box);
    if(p.x>=box.x1&&p.x<=box.x2&&p.y>=box.y1&&p.y<=box.y2){lv_obj_stop_scroll_anim(obj);lv_obj_set_scroll_dir(obj,LV_DIR_HOR);}
}
static void uv_pager_event(lv_event_t *e)
{
    lv_obj_t *obj=lv_event_get_target(e);
    if(lv_event_get_code(e)==LV_EVENT_DELETE){
        lv_indev_t *indev=NULL;while((indev=lv_indev_get_next(indev)))lv_indev_remove_event_cb_with_user_data(indev,uv_pager_press,obj);
    }else{
        lv_anim_t *a=lv_event_get_param(e);
        if(a){lv_anim_set_duration(a,180);lv_anim_set_path_cb(a,lv_anim_path_ease_out);}
    }
}

void unveil_pager_motion_attach(lv_obj_t *obj)
{
    lv_obj_remove_flag(obj,LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_add_event_cb(obj,uv_pager_event,LV_EVENT_SCROLL_BEGIN,NULL);
    lv_obj_add_event_cb(obj,uv_pager_event,LV_EVENT_DELETE,NULL);
    lv_indev_t *indev=NULL;
    while((indev=lv_indev_get_next(indev)))if(lv_indev_get_type(indev)==LV_INDEV_TYPE_POINTER&&lv_indev_get_display(indev)==lv_obj_get_display(obj))lv_indev_add_event_cb(indev,uv_pager_press,LV_EVENT_PRESSED,obj);
}
void unveil_pager_motion_home(lv_obj_t *obj)
{
    lv_obj_stop_scroll_anim(obj);
    lv_indev_t *indev=NULL;
    while((indev=lv_indev_get_next(indev)))if(lv_indev_get_type(indev)==LV_INDEV_TYPE_POINTER&&lv_indev_get_display(indev)==lv_obj_get_display(obj)&&lv_indev_get_state(indev)==LV_INDEV_STATE_PRESSED)lv_indev_wait_release(indev);
    lv_tileview_set_tile_by_index(obj,0,0,LV_ANIM_ON);
}
