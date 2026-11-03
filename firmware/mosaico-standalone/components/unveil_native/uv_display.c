// SPDX-License-Identifier: Apache-2.0
/* Raw BSP panel + an independent LVGL port. DMA and TE fences belong to this port. */
#include "uv_display.h"
#include "bsp/esp_mosaico.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_lcd_touch.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
struct uv_display_session {uv_display_session_config_t config;bool active;};
static struct uv_display_session session;
static SemaphoreHandle_t mutex,tx_done,te_edge;
static esp_lcd_panel_handle_t panel;
static esp_lcd_touch_handle_t touch;
static lv_display_t *display;
static lv_obj_t *pending_screen,*boot_screen;
static lv_font_t *boot_font;
static void *boot_font_data;
static bool first_frame_ready;
static atomic_uint submitted,transferred,presented;
static atomic_bool tx_busy;
static unsigned tx_frame;
static bool tx_last,frame_start=true;
static atomic_uint tx_bytes,tx_frames;
static void tick(void *arg){(void)arg;lv_tick_inc(1);}
static void IRAM_ATTR te_irq(void *arg){(void)arg;atomic_store(&presented,atomic_load(&transferred));BaseType_t wake=pdFALSE;xSemaphoreGiveFromISR(te_edge,&wake);if(wake)portYIELD_FROM_ISR();}
static bool tx_irq(esp_lcd_panel_io_handle_t io,esp_lcd_panel_io_event_data_t *ev,void *arg){(void)io;(void)ev;(void)arg;if(tx_last){atomic_store(&transferred,tx_frame);atomic_fetch_add(&tx_frames,1);}atomic_store(&tx_busy,false);BaseType_t wake=pdFALSE;xSemaphoreGiveFromISR(tx_done,&wake);return wake==pdTRUE;}
static void flush_wait(lv_display_t *d){(void)d;if(atomic_load(&tx_busy)){if(xSemaphoreTake(tx_done,pdMS_TO_TICKS(2000))!=pdTRUE)abort();}else while(xSemaphoreTake(tx_done,0)==pdTRUE){};}
static void flush(lv_display_t *d,const lv_area_t *area,uint8_t *data){
 if(frame_start){while(xSemaphoreTake(te_edge,0)==pdTRUE){};if(xSemaphoreTake(te_edge,pdMS_TO_TICKS(100))!=pdTRUE)abort();atomic_fetch_add(&submitted,1);frame_start=false;}
 while(xSemaphoreTake(tx_done,0)==pdTRUE){};tx_frame=atomic_load(&submitted);tx_last=lv_display_flush_is_last(d);frame_start=tx_last;atomic_store(&tx_busy,true);
 atomic_fetch_add(&tx_bytes,(area->x2-area->x1+1)*(area->y2-area->y1+1)*2);
 lv_draw_sw_rgb565_swap(data,lv_area_get_size(area));
 ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel,area->x1,area->y1,area->x2+1,area->y2+1,data));
}
static void read_touch(lv_indev_t *i,lv_indev_data_t *data){(void)i;uint16_t x,y;uint8_t n=0;esp_lcd_touch_read_data(touch);bool pressed=esp_lcd_touch_get_coordinates(touch,&x,&y,NULL,&n,1);data->state=pressed&&n?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;if(pressed&&n){data->point.x=x;data->point.y=y;}}
static void boot_logo(void)
{
    boot_screen=lv_screen_active();
    FILE *file=fopen("/data/apps/unveil/assets/Doto-Regular.ttf","rb");
    if(file){if(fseek(file,0,SEEK_END)==0){long size=ftell(file);rewind(file);if(size>0&&size<262144){boot_font_data=malloc((size_t)size);if(boot_font_data&&fread(boot_font_data,1,size,file)==(size_t)size)boot_font=lv_tiny_ttf_create_data(boot_font_data,size,72);}}fclose(file);}
    lv_obj_t *word=lv_label_create(boot_screen);lv_label_set_text(word,"Unveil");lv_obj_set_style_text_color(word,lv_color_hex(0xf54b2c),0);if(boot_font)lv_obj_set_style_text_font(word,boot_font,0);lv_obj_center(word);
    lv_refr_now(display);flush_wait(display);lv_display_flush_ready(display);ESP_ERROR_CHECK(bsp_display_brightness_set(100));
}
static void lvgl_task(void *arg){(void)arg;for(;;){xSemaphoreTakeRecursive(mutex,portMAX_DELAY);uint32_t wait=lv_timer_handler();xSemaphoreGiveRecursive(mutex);vTaskDelay(pdMS_TO_TICKS(wait>5?5:wait<1?1:wait));}}
esp_err_t uv_display_init(void){
 mutex=xSemaphoreCreateRecursiveMutex();tx_done=xSemaphoreCreateBinary();te_edge=xSemaphoreCreateBinary();if(!mutex||!tx_done||!te_edge)return ESP_ERR_NO_MEM;
 bsp_display_config_t cfg=BSP_DISPLAY_DEFAULT_CONFIG();ESP_ERROR_CHECK(bsp_display_new(&cfg,&panel));ESP_ERROR_CHECK(bsp_display_brightness_set(0));ESP_ERROR_CHECK(bsp_touch_new(cfg.rotation,&touch));
 esp_lcd_panel_io_callbacks_t callbacks={.on_color_trans_done=tx_irq};ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(bsp_display_get_panel_io(),&callbacks,NULL));
 gpio_config_t te={.pin_bit_mask=1ULL<<BSP_LCD_TE,.mode=GPIO_MODE_INPUT,.intr_type=GPIO_INTR_POSEDGE};ESP_ERROR_CHECK(gpio_config(&te));esp_err_t error=gpio_install_isr_service(0);if(error!=ESP_OK&&error!=ESP_ERR_INVALID_STATE)return error;ESP_ERROR_CHECK(gpio_isr_handler_add(BSP_LCD_TE,te_irq,NULL));
 lv_init();display=lv_display_create(480,480);if(!display)return ESP_ERR_NO_MEM;size_t bytes=480*48*2;void *a=heap_caps_aligned_alloc(64,bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT),*b=heap_caps_aligned_alloc(64,bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!a||!b){free(a);free(b);return ESP_ERR_NO_MEM;}
 lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);lv_display_set_buffers(display,a,b,bytes,LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(display,flush);lv_display_set_flush_wait_cb(display,flush_wait);
 lv_obj_set_style_bg_color(lv_screen_active(),lv_color_black(),0);boot_logo();lv_indev_t *indev=lv_indev_create();if(!indev)return ESP_ERR_NO_MEM;lv_indev_set_type(indev,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(indev,read_touch);
 esp_timer_handle_t timer;const esp_timer_create_args_t args={.callback=tick,.name="uv_tick"};ESP_ERROR_CHECK(esp_timer_create(&args,&timer));ESP_ERROR_CHECK(esp_timer_start_periodic(timer,1000));return xTaskCreate(lvgl_task,"uv_lvgl",32768,NULL,5,NULL)==pdPASS?ESP_OK:ESP_ERR_NO_MEM;
}
esp_err_t uv_display_lock(void){return xSemaphoreTakeRecursive(mutex,pdMS_TO_TICKS(1000))==pdTRUE?ESP_OK:ESP_ERR_TIMEOUT;}
void uv_display_unlock(void){xSemaphoreGiveRecursive(mutex);}
bool uv_display_has_exclusive_session(void){return session.active;}
esp_err_t uv_display_open(const uv_display_session_config_t *cfg,uv_display_session_handle_t *out){if(session.active)return ESP_ERR_INVALID_STATE;session.config=*cfg;session.active=true;*out=&session;return ESP_OK;}
esp_err_t uv_display_close(uv_display_session_handle_t s){if(s!=&session)return ESP_ERR_INVALID_ARG;ESP_ERROR_CHECK(uv_display_lock());if(s->config.cleanup_cb)s->config.cleanup_cb(s,s->config.user_ctx);s->active=false;uv_display_unlock();return ESP_OK;}
lv_display_t *uv_display_session_display(uv_display_session_handle_t s){return s==&session?display:NULL;}
esp_err_t uv_display_session_load_screen_locked(uv_display_session_handle_t s,lv_obj_t *screen){if(s!=&session)return ESP_ERR_INVALID_ARG;if(!first_frame_ready)pending_screen=screen;else lv_screen_load(screen);return ESP_OK;}
extern void uv_first_frame_visible(void) __attribute__((weak));
bool unveil_release_boot_frame(lv_display_t *d){
 if(!first_frame_ready){if(!pending_screen)return false;lv_obj_update_layout(pending_screen);lv_screen_load(pending_screen);pending_screen=NULL;first_frame_ready=true;}
 lv_refr_now(d);flush_wait(d);lv_display_flush_ready(d);bool ok=bsp_display_brightness_set(100)==ESP_OK;
 if(boot_screen&&boot_screen!=lv_screen_active()){lv_obj_delete(boot_screen);boot_screen=NULL;if(boot_font){lv_tiny_ttf_destroy(boot_font);boot_font=NULL;}free(boot_font_data);boot_font_data=NULL;}
 if(ok&&uv_first_frame_visible){uv_first_frame_visible();}
 return ok;
}
bool unveil_present_state(lv_display_t *d,uint64_t values[4]){if(d!=display)return false;values[0]=1;values[1]=atomic_load(&submitted);values[2]=atomic_load(&transferred);values[3]=atomic_load(&presented);return true;}
void unveil_present_profile(uint64_t v[6],bool reset){memset(v,0,sizeof(uint64_t)*6);(void)reset;}
void unveil_te_profile(uint64_t v[6],bool reset){memset(v,0,sizeof(uint64_t)*6);v[0]=reset?atomic_exchange(&tx_frames,0):atomic_load(&tx_frames);v[1]=reset?atomic_exchange(&tx_bytes,0):atomic_load(&tx_bytes);}
