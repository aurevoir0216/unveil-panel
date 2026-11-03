#!/usr/bin/env python3
"""Exercise Idea's real input callback through the shared desktop adapter.

Controller reports and platform calls are mocked; production input functions
are compiled unchanged. Lift reports have zero points, as on the real device.
"""
from pathlib import Path
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[1]
adapter = repo / 'firmware/mosaico/components/esp_lvgl_present'
native = repo / 'firmware/mosaico-core/components/unveil_native'
display = (native / 'uv_display.c').read_text()
start = display.index('static esp_err_t read_touch(')
end = display.index('\nstatic void observe_region(', start)
callback = display[start:end]
assert 'esp_lv_adapter_register_touch(&input)' in display

with tempfile.TemporaryDirectory(prefix='unveil-touch-') as temp:
    work = Path(temp)
    (work / 'esp_err.h').write_text('''#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef int esp_err_t;
enum {ESP_OK=0, ESP_ERR_INVALID_ARG=1, ESP_ERR_NO_MEM=2, ESP_ERR_INVALID_STATE=3,
 ESP_ERR_TIMEOUT=4};
const char *esp_err_to_name(int error);
''')
    (work / 'esp_lcd_touch.h').write_text('''#pragma once
#include "esp_err.h"
#define CONFIG_ESP_LCD_TOUCH_MAX_POINTS 5
typedef void *esp_lcd_touch_handle_t;
typedef struct {uint16_t x,y;uint8_t track_id;} esp_lcd_touch_point_data_t;
esp_err_t esp_lcd_touch_read_data(esp_lcd_touch_handle_t);
esp_err_t esp_lcd_touch_get_data(esp_lcd_touch_handle_t,esp_lcd_touch_point_data_t *,uint8_t *,uint8_t);
''')
    (work / 'esp_display_present_types.h').write_text('''#pragma once
typedef struct {unsigned x_pixels,y_pixels,width_pixels,height_pixels;}
 esp_display_present_render_alignment_t;
''')
    (work / 'lvgl.h').write_text('''#pragma once
#include "esp_err.h"
typedef int32_t lv_coord_t;
typedef struct {lv_coord_t x,y;} lv_point_t;
typedef struct {int32_t x1,y1,x2,y2;} lv_area_t;
typedef struct lv_display {int unused;} lv_display_t;
typedef enum {LV_INDEV_STATE_RELEASED,LV_INDEV_STATE_PRESSED} lv_indev_state_t;
typedef struct {lv_point_t point;lv_indev_state_t state;} lv_indev_data_t;
typedef struct lv_indev lv_indev_t;
struct lv_indev {void *driver_data;void (*read_cb)(lv_indev_t *,lv_indev_data_t *);};
#define LV_INDEV_TYPE_POINTER 1
lv_indev_t *lv_indev_create(void);
void lv_indev_delete(lv_indev_t *);
void lv_indev_set_type(lv_indev_t *,int);
void lv_indev_set_disp(lv_indev_t *,lv_display_t *);
void lv_indev_set_read_cb(lv_indev_t *,void (*)(lv_indev_t *,lv_indev_data_t *));
void lv_indev_set_driver_data(lv_indev_t *,void *);
void *lv_indev_get_driver_data(lv_indev_t *);
''')
    (work / 'adapter_internal.h').write_text('''#pragma once
#include "esp_lv_adapter_input.h"
#define ESP_LV_ADAPTER_INPUT_TYPE_TOUCH 0
esp_err_t esp_lv_adapter_lock(int);
void esp_lv_adapter_unlock(void);
esp_err_t esp_lv_adapter_register_input_device(lv_indev_t *,int,void *);
esp_err_t esp_lv_adapter_unregister_input_device(lv_indev_t *);
''')
    (work / 'esp_log.h').write_text('''#pragma once
#define ESP_LOGI(tag,...) ((void)(tag))
#define ESP_LOGW(tag,...) ((void)(tag))
#define ESP_LOGE(tag,...) ((void)(tag))
''')
    (work / 'esp_check.h').write_text('''#pragma once
#define ESP_RETURN_ON_FALSE(test,error,...) do {if(!(test))return(error);} while(0)
#define ESP_RETURN_ON_ERROR(expr,...) do {int e=(expr);if(e)return e;} while(0)
''')
    harness = '''#include "adapter_internal.h"
#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include "unveil_dial_mapping.h"
static bool injected_pressed,physical_pressed;
static uint16_t injected_x,injected_y,physical_x,physical_y;
static int64_t last_physical_touch,injection_time;
static atomic_uint touch_sequence;
static unsigned touch_consumed;
static unsigned reports,controller_count;
static esp_lcd_touch_point_data_t controller_point;
static esp_err_t controller_error;
static int64_t now=10000000;
static int64_t esp_timer_get_time(void){return now;}
esp_err_t esp_lcd_touch_read_data(esp_lcd_touch_handle_t h){(void)h;++reports;return controller_error;}
esp_err_t esp_lcd_touch_get_data(esp_lcd_touch_handle_t h,esp_lcd_touch_point_data_t *p,uint8_t *n,uint8_t max){
 (void)h;assert(max);*n=controller_count;if(*n)*p=controller_point;return ESP_OK;}
const char *esp_err_to_name(int e){(void)e;return "test";}
lv_indev_t *lv_indev_create(void){return calloc(1,sizeof(lv_indev_t));}
void lv_indev_delete(lv_indev_t *p){free(p);}
void lv_indev_set_type(lv_indev_t *p,int t){(void)p;(void)t;}
void lv_indev_set_disp(lv_indev_t *p,lv_display_t *d){(void)p;(void)d;}
void lv_indev_set_read_cb(lv_indev_t *p,void (*fn)(lv_indev_t *,lv_indev_data_t *)){p->read_cb=fn;}
void lv_indev_set_driver_data(lv_indev_t *p,void *d){p->driver_data=d;}
void *lv_indev_get_driver_data(lv_indev_t *p){return p->driver_data;}
esp_err_t esp_lv_adapter_lock(int t){(void)t;return ESP_OK;}
void esp_lv_adapter_unlock(void){}
esp_err_t esp_lv_adapter_register_input_device(lv_indev_t *p,int t,void *d){(void)p;(void)t;(void)d;return ESP_OK;}
esp_err_t esp_lv_adapter_unregister_input_device(lv_indev_t *p){(void)p;return ESP_OK;}
'''
    harness += callback + '''
static void sample(lv_indev_t *input,bool pressed,int x,int y){
 controller_count=pressed;controller_point=(esp_lcd_touch_point_data_t){.x=x,.y=y};
 atomic_fetch_add(&touch_sequence,1);lv_indev_data_t data={0};input->read_cb(input,&data);
 assert(data.state==(pressed?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED));
 assert(data.point.x==x&&data.point.y==y);
}
static void test_dial_overdrag(void){
 const float rays[][2]={{0,-1},{-.8660254f,.5f},{.8660254f,.5f}};
 const int counts[]={3,5,6};
 for(int q=0;q<3;++q){
  const float x=rays[q][0],y=rays[q][1],start=120;
  for(int i=0;i<counts[q];++i){
   const float radius=UV_DIAL_INNER_RADIUS+(i+.5f)*(UV_DIAL_OUTER_RADIUS-UV_DIAL_INNER_RADIUS)/counts[q];
   assert(uv_dial_index_from_radius(uv_dial_drag_radius(x*radius,y*radius,x*start,y*start,start),counts[q])==i+1);
  }
  assert(uv_dial_index_from_radius(uv_dial_drag_radius(-x*190,-y*190,x*start,y*start,start),counts[q])==1);
  assert(uv_dial_index_from_radius(uv_dial_drag_radius(x*190,y*190,x*start,y*start,start),counts[q])==counts[q]);
  assert(fabsf(uv_dial_drag_radius(x*start-y*200,y*start+x*200,x*start,y*start,start)-start)<.01f);
 }
}
int main(void){
 test_dial_overdrag();
 lv_display_t screen={0};esp_lv_adapter_touch_config_t config={.disp=&screen,.handle=&screen,
 .scale={.x=1,.y=1},.callbacks={.custom_touch_read=read_touch}};
 lv_indev_t *input=esp_lv_adapter_register_touch(&config);assert(input);
 /* Stale USB coordinates must never replace the position of a finger lift. */
 injected_x=1;injected_y=2;
 for(int y=70;y<450;y+=76)for(int x=40;x<450;x+=80){
  sample(input,true,x,y);controller_count=0;controller_point=(esp_lcd_touch_point_data_t){0};
  lv_indev_data_t data={0};input->read_cb(input,&data);
  assert(data.state==LV_INDEV_STATE_RELEASED&&data.point.x==x&&data.point.y==y);
 }
 /* Swipes and radial drags retain their final sample on release. */
 for(int x=400;x>=80;x-=40)sample(input,true,x,260);
 sample(input,false,80,260);
 unsigned idle_reports=reports;for(int i=0;i<100;++i){lv_indev_data_t d={0};input->read_cb(input,&d);
  assert(d.state==LV_INDEV_STATE_RELEASED&&d.point.x==80&&d.point.y==260);}
 assert(reports==idle_reports);
 /* The USB diagnostic path uses this same count-zero release contract. */
 injected_pressed=true;injected_x=300;injected_y=220;injection_time=now;
 lv_indev_data_t data={0};input->read_cb(input,&data);
 assert(data.state==LV_INDEV_STATE_PRESSED&&data.point.x==300&&data.point.y==220);
 injected_pressed=false;injected_x=injected_y=0;input->read_cb(input,&data);
 assert(data.state==LV_INDEV_STATE_RELEASED&&data.point.x==300&&data.point.y==220);
 /* Real input takes priority and cancels a synthetic hold. */
 injected_pressed=true;injection_time=now;sample(input,true,220,120);assert(!injected_pressed);
 controller_error=ESP_ERR_TIMEOUT;controller_count=0;input->read_cb(input,&data);
 assert(data.state==LV_INDEV_STATE_RELEASED&&data.point.x==220&&data.point.y==120);
 assert(esp_lv_adapter_unregister_touch(input)==ESP_OK);
 puts("IDEA_TOUCH_OK physical-zero-point-release, button-grid, drag-release, idle-IRQ, USB-release, physical-priority, read-error, dial-overdrag");
 return 0;
}
'''
    (work / 'harness.c').write_text(harness)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-I'+str(work), '-I'+str(native / 'lvgl'),
                    '-I'+str(adapter / 'include'), str(work / 'harness.c'),
                    str(adapter / 'src/input/esp_lv_adapter_input_touch.c'),
                    '-lm', '-o', str(work / 'touch-test')], check=True)
    subprocess.run([str(work / 'touch-test')], check=True)
