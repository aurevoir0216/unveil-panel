#include "uv_runtime.h"
#include "bsp/audio.h"
#include <math.h>
#include <stdatomic.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"


#include "esp_log.h"
static QueueHandle_t queue;
static atomic_int level=40;
static atomic_bool mute=false;
static atomic_uint epoch=0;
typedef struct {int kind;unsigned epoch;} sound_event_t;
static esp_codec_dev_handle_t dac;
static bool tone(float frequency,int milliseconds,bool success,unsigned current)
{
    int16_t pcm[480];int total=milliseconds*48;
    int previous=-1;
    for(int offset=0;offset<total;offset+=480) {
        if(atomic_load(&mute)||atomic_load(&epoch)!=current)return false;
        int volume=atomic_load(&level);if(volume==0)return false;
        if(volume!=previous){esp_codec_dev_set_out_vol(dac,volume);previous=volume;}
        int count=total-offset<480 ? total-offset : 480;
        for(int i=0;i<count;++i) {
            float t=(offset+i)/48000.f,remain=(total-offset-i)/48000.f;
            float envelope=fminf(1.f,t/.008f)*fminf(1.f,remain/.014f);
            if(success)envelope*=expf(-t*7.f);
            float phase=6.28318530718f*frequency*t;
            float sample=frequency>0 ? sinf(phase)+.18f*sinf(2*phase) : 0;
            pcm[i]=(int16_t)(sample*envelope*6000);
        }
        if(esp_codec_dev_write(dac,pcm,count*sizeof(int16_t))!=ESP_CODEC_DEV_OK)return false;
    }
    return true;
}
static void task(void *unused)
{
    (void)unused;sound_event_t event;
    for(;;) {
        if(xQueueReceive(queue,&event,portMAX_DELAY)!=pdTRUE)continue;
        if(atomic_load(&mute)||atomic_load(&level)==0)continue;
        if(!dac) {
            dac=bsp_audio_codec_speaker_init();
            if(!dac){ESP_LOGW("unveil_sound","Speaker unavailable");continue;}
        }
        esp_codec_dev_sample_info_t format={.sample_rate=48000,.channel=1,.bits_per_sample=16};
        if(esp_codec_dev_open(dac,&format)!=ESP_CODEC_DEV_OK){ESP_LOGW("unveil_sound","DAC open failed");continue;}
        bool played=true;
        if(event.kind==1) {
            played=tone(1046.5f,120,true,event.epoch)&&tone(0,45,false,event.epoch)&&tone(1318.51f,260,true,event.epoch);
        } else {
            for(int i=0;i<4&&played;++i)played=tone(i%2 ? 1600 : 1200,170,false,event.epoch)&&tone(0,130,false,event.epoch);
        }
        if(played)tone(0,60,false,event.epoch);
        esp_codec_dev_close(dac);
        ESP_LOGI("unveil_sound","%s %s volume=%d",event.kind==1 ? "safe" : "alarm",played ? "played" : "stopped",atomic_load(&level));
    }
}
esp_err_t unveil_sound_init(void)
{
    if(queue)return ESP_OK;
    queue=xQueueCreate(1,sizeof(sound_event_t));if(!queue)return ESP_ERR_NO_MEM;
    if(xTaskCreate(task,"unveil_sound",6144,NULL,3,NULL)!=pdPASS){vQueueDelete(queue);queue=NULL;return ESP_ERR_NO_MEM;}
    return ESP_OK;
}
esp_err_t unveil_sound_command(const char *kind,int volume,bool muted)
{
    if(volume<0||volume>100)return ESP_ERR_INVALID_ARG;
    atomic_store(&level,volume);atomic_store(&mute,muted);
    if(!strcmp(kind,"settings"))return ESP_OK;
    if(strcmp(kind,"safe")&&strcmp(kind,"alarm"))return ESP_ERR_INVALID_ARG;
    if(!queue)return ESP_ERR_INVALID_STATE;
    sound_event_t event={.kind=!strcmp(kind,"safe") ? 1 : 2,.epoch=atomic_fetch_add(&epoch,1)+1};
    if(muted||volume==0)return ESP_OK;
    return xQueueOverwrite(queue,&event)==pdPASS ? ESP_OK : ESP_FAIL;
}
