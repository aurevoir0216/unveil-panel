// SPDX-License-Identifier: Apache-2.0
#ifdef UNVEIL_DESKTOP_BUILD
/* App notifications use the shared mixer instead of reopening the system DAC. */
#include "unveil_sound.h"
#include "audio_hub.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
static QueueHandle_t notifications;
static atomic_int volume=40;
static atomic_bool muted;
static atomic_uint generation;
typedef struct {int kind;unsigned generation;} notification_t;
typedef struct {audio_mixer_track_handle_t track;uint32_t rate;uint8_t channels;int16_t *pcm;} player_t;
static bool tone(player_t *player,float hz,unsigned ms,bool success,unsigned epoch)
{
    unsigned total=(uint64_t)ms*player->rate/1000,capacity=1024/player->channels;
    for(unsigned offset=0;offset<total;){
        if(atomic_load(&muted)||atomic_load(&generation)!=epoch||atomic_load(&volume)==0)return false;
        unsigned count=total-offset<capacity?total-offset:capacity;
        float gain=atomic_load(&volume)/100.f;
        for(unsigned i=0;i<count;i++){
            float t=(offset+i)/(float)player->rate,remaining=(total-offset-i)/(float)player->rate;
            float envelope=fminf(1.f,t/.008f)*fminf(1.f,remaining/.014f);
            if(success)envelope*=expf(-t*7.f);
            float phase=6.28318530718f*hz*t;
            int16_t value=(int16_t)((hz>0?sinf(phase)+.18f*sinf(2*phase):0)*envelope*6000*gain);
            for(unsigned c=0;c<player->channels;c++)player->pcm[i*player->channels+c]=value;
        }
        size_t bytes=count*player->channels*sizeof(int16_t);
        if(audio_mixer_track_write(player->track,player->pcm,bytes)!=bytes)return false;
        offset+=count;
    }
    return true;
}
static void playback(void *argument)
{
    notification_t event;player_t player={.pcm=argument};
    for(;;){
        if(xQueueReceive(notifications,&event,portMAX_DELAY)!=pdTRUE)continue;
        if(atomic_load(&muted)||atomic_load(&volume)==0)continue;
        audio_mixer_handle_t mixer=NULL;
        esp_err_t error=audio_hub_get_mixer(&mixer);
        if(error==ESP_OK)error=audio_mixer_open_track(mixer,AUDIO_MIXER_TRACK_SYSTEM,"Unveil",&player.track);
        if(error!=ESP_OK){ESP_LOGW("unveil_sound","System notification mixer unavailable: %s",esp_err_to_name(error));continue;}
        uint8_t bits=0;error=audio_mixer_track_info(player.track,&player.rate,&player.channels,&bits);
        bool played=error==ESP_OK&&bits==16&&player.rate>0&&player.channels>0&&player.channels<=2;
        if(played&&event.kind==1)played=tone(&player,1046.5f,120,true,event.generation)&&tone(&player,0,45,false,event.generation)&&tone(&player,1318.51f,260,true,event.generation);
        else if(played)for(int i=0;i<4&&played;i++)played=tone(&player,i%2?1600:1200,170,false,event.generation)&&tone(&player,0,130,false,event.generation);
        if(played){tone(&player,0,60,false,event.generation);audio_mixer_track_flush(player.track);}else audio_mixer_track_stop(player.track);
        audio_mixer_close_track(player.track);player.track=NULL;
        ESP_LOGI("unveil_sound","%s %s",event.kind==1?"safe":"alarm",played?"played":"stopped");
    }
}
esp_err_t unveil_sound_init(void)
{
    if(notifications)return ESP_OK;
    notifications=xQueueCreate(1,sizeof(notification_t));if(!notifications)return ESP_ERR_NO_MEM;
    int16_t *pcm=malloc(1024*sizeof(int16_t));
    if(!pcm){vQueueDelete(notifications);notifications=NULL;return ESP_ERR_NO_MEM;}
    if(xTaskCreate(playback,"uv_notifications",6144,pcm,3,NULL)!=pdPASS){free(pcm);vQueueDelete(notifications);notifications=NULL;return ESP_ERR_NO_MEM;}
    return ESP_OK;
}
esp_err_t unveil_sound_command(const char *kind,int level,bool mute)
{
    if(!kind||level<0||level>100)return ESP_ERR_INVALID_ARG;
    atomic_store(&volume,level);atomic_store(&muted,mute);
    if(!strcmp(kind,"settings"))return ESP_OK;
    if(strcmp(kind,"safe")&&strcmp(kind,"alarm"))return ESP_ERR_INVALID_ARG;
    if(!notifications)return ESP_ERR_INVALID_STATE;
    notification_t event={.kind=!strcmp(kind,"safe")?1:2,.generation=atomic_fetch_add(&generation,1)+1};
    if(mute||level==0)return ESP_OK;
    return xQueueOverwrite(notifications,&event)==pdPASS?ESP_OK:ESP_FAIL;
}
#endif
