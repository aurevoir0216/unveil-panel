// SPDX-License-Identifier: Apache-2.0
/* Firmware-owned desktop resources, independent of Vibe Mode's installer.
 * Every file is replaced atomically, launcher and version marker last.
 * config.json is never part of a seed, including on an existing installation. */
#include "unveil_seed.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#ifndef UNVEIL_SEED_HOST_TEST
#include "app_fs.h"
#include "esp_log.h"
#else
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_INVALID_CRC 0x109
#define ESP_LOGI(...) ((void)0)
#endif

#define SEED_HEADER_SIZE 42U
#define SEED_PATH_SIZE 256U
typedef struct {const uint8_t *data;uint32_t size,crc;char name[160];} seed_entry_t;
static uint32_t u32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint16_t u16(const uint8_t *p){return (uint16_t)p[0]|(uint16_t)p[1]<<8;}
static uint32_t crc_update(uint32_t crc,const uint8_t *data,size_t size)
{
 for(size_t i=0;i<size;++i){crc^=data[i];for(unsigned bit=0;bit<8;++bit)crc=(crc>>1)^(0xedb88320U&(0U-(crc&1U)));}
 return crc;
}
static bool entry_read(const uint8_t **cursor,const uint8_t *end,seed_entry_t *entry)
{
 const uint8_t *p=*cursor;if((size_t)(end-p)<10)return false;
 unsigned length=u16(p);entry->size=u32(p+2);entry->crc=u32(p+6);p+=10;
 if(!length||length>=sizeof(entry->name)||(size_t)(end-p)<length)return false;
 memcpy(entry->name,p,length);entry->name[length]=0;p+=length;
 if(strlen(entry->name)!=length||entry->name[0]=='/'||strchr(entry->name,'\\')||strstr(entry->name,"..")||!strcmp(entry->name,"config.json"))return false;
 if((size_t)(end-p)<entry->size)return false;
 entry->data=p;*cursor=p+entry->size;return true;
}
static bool make_parents(char *path)
{
 for(char *p=path+1;*p;++p){if(*p!='/')continue;*p=0;int result=mkdir(path,0777);*p='/';if(result&&errno!=EEXIST)return false;}
 return true;
}
static esp_err_t replace_file(char *path,const uint8_t *data,size_t size,uint32_t crc)
{
 if(!make_parents(path))return ESP_FAIL;
 char temporary[SEED_PATH_SIZE+8];int n=snprintf(temporary,sizeof(temporary),"%s.uvtmp",path);
 if(n<0||(size_t)n>=sizeof(temporary))return ESP_ERR_INVALID_SIZE;
 FILE *file=fopen(temporary,"wb");if(!file)return ESP_FAIL;
 bool ok=fwrite(data,1,size,file)==size;
 if(fclose(file)!=0)ok=false;
 file=ok?fopen(temporary,"rb"):NULL;
 if(file){uint8_t block[512];size_t count;uint32_t actual=0xffffffffU;
  while((count=fread(block,1,sizeof(block),file))>0)actual=crc_update(actual,block,count);
  ok=!ferror(file)&&~actual==crc;fclose(file);
 }else ok=false;
 if(ok)ok=rename(temporary,path)==0;
 if(!ok)remove(temporary);
 return ok?ESP_OK:ESP_FAIL;
}
esp_err_t unveil_seed_install(const uint8_t *archive,size_t size,const char *directory)
{
 if(!archive||!directory||size<SEED_HEADER_SIZE||memcmp(archive,"UVSEED01",8))return ESP_ERR_INVALID_ARG;
 const uint8_t *cursor=archive+SEED_HEADER_SIZE,*end=archive+size;unsigned count=u16(archive+40);
 if(!count||count>64)return ESP_ERR_INVALID_SIZE;
 char version[65],path[SEED_PATH_SIZE],marker[SEED_PATH_SIZE];
 for(unsigned i=0;i<32;++i)snprintf(version+2*i,3,"%02x",archive[8+i]);
 int n=snprintf(marker,sizeof(marker),"%s/.unveil-fw-seed",directory);
 if(n<0||(size_t)n>=sizeof(marker))return ESP_ERR_INVALID_SIZE;
 bool current=false;FILE *file=fopen(marker,"rb");
 if(file){char saved[65]={0};current=fread(saved,1,64,file)==64&&!memcmp(saved,version,64);fclose(file);}
 for(unsigned i=0;i<count;++i){
  seed_entry_t entry;if(!entry_read(&cursor,end,&entry))return ESP_ERR_INVALID_ARG;
  if(~crc_update(0xffffffffU,entry.data,entry.size)!=entry.crc)return ESP_ERR_INVALID_CRC;
  n=snprintf(path,sizeof(path),"%s/%s",directory,entry.name);
  if(n<0||(size_t)n>=sizeof(path))return ESP_ERR_INVALID_SIZE;
  struct stat st;if(stat(path,&st)!=0||!S_ISREG(st.st_mode))current=false;
 }
 if(cursor!=end)return ESP_ERR_INVALID_SIZE;
 if(current)return ESP_OK;
 cursor=archive+SEED_HEADER_SIZE;
 for(unsigned i=0;i<count;++i){
  seed_entry_t entry;if(!entry_read(&cursor,end,&entry))return ESP_ERR_INVALID_ARG;
  snprintf(path,sizeof(path),"%s/%s",directory,entry.name);
  esp_err_t error=replace_file(path,entry.data,entry.size,entry.crc);if(error!=ESP_OK)return error;
 }
 esp_err_t error=replace_file(marker,(const uint8_t*)version,64,~crc_update(0xffffffffU,(const uint8_t*)version,64));
 if(error==ESP_OK)ESP_LOGI("unveil_seed","Desktop app resources verified: %u files",count);
 return error;
}
esp_err_t unveil_seed_bootstrap(void)
{
#if defined(UNVEIL_DESKTOP_BUILD) && !defined(UNVEIL_SEED_HOST_TEST)
 extern const uint8_t start[] asm("_binary_unveil_app_seed_bin_start");
 extern const uint8_t end[] asm("_binary_unveil_app_seed_bin_end");
 char directory[64];snprintf(directory,sizeof(directory),"%s/apps/unveil",app_fs_storage_base_path());
 return unveil_seed_install(start,(size_t)(end-start),directory);
#else
 return ESP_OK;
#endif
}
