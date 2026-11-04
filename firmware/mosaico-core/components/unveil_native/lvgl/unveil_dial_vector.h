#pragma once
#include "lvgl.h"
bool unveil_dial_vector_attach(lv_obj_t *obj,bool dark,uint32_t light_fill);
bool unveil_dial_vector_set(lv_obj_t *obj,const int levels[3]);
void unveil_dial_vector_paging(lv_obj_t *obj,bool active);
bool unveil_dial_vector_prepare_motion(lv_obj_t *obj);
