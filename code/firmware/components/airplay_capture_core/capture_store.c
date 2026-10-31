/* Direct-display fork: no full raw/Annex-B archive and no runtime Mac relay. */
#include "media_bridge.h"
#include "live_store.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
static SemaphoreHandle_t lock;
static unsigned frames,idrs,configs;
bool media_bridge_capture_init(void){if(!lock)lock=xSemaphoreCreateMutex();return lock&&live_store_init();}
void media_bridge_record_raw(const uint8_t header[128],const uint8_t *p,size_t n){(void)header;(void)p;(void)n;}
esp_err_t media_bridge_set_h264_config(const uint8_t *p,size_t n){
 if(!lock||!p||!n||n>LIVE_CONFIG_BYTES)return ESP_ERR_INVALID_ARG;
 live_store_config(p,n);xSemaphoreTake(lock,portMAX_DELAY);++configs;xSemaphoreGive(lock);return ESP_OK;
}
esp_err_t media_bridge_send_h264(const uint8_t *p,size_t n,uint64_t sender,uint64_t arrival){
 if(!lock||!p||!n)return ESP_ERR_INVALID_STATE;
 bool idr=false;for(size_t i=0;i+4<n;i++)if(!p[i]&&!p[i+1]&&!p[i+2]&&p[i+3]==1&&(p[i+4]&31)==5)idr=true;
 live_store_frame(p,n,idr,sender,arrival);xSemaphoreTake(lock,portMAX_DELAY);++frames;if(idr)++idrs;xSemaphoreGive(lock);return ESP_OK;
}
const uint8_t *media_bridge_capture_freeze(bool raw,size_t *n){(void)raw;if(n)*n=0;return NULL;}
size_t media_bridge_capture_status(char *out,size_t cap){
 if(!lock)return snprintf(out,cap,"{\"ready\":false}");
 xSemaphoreTake(lock,portMAX_DELAY);int n=snprintf(out,cap,"{\"ready\":true,\"mode\":\"local_display\",\"raw_bytes\":0,\"annex_b_bytes\":0,\"frames\":%u,\"configs\":%u,\"idrs_seen\":%u,\"frozen\":false}",frames,configs,idrs);xSemaphoreGive(lock);return n<0?0:(size_t)n;
}
unsigned media_bridge_capture_frames(void){if(!lock)return 0;xSemaphoreTake(lock,portMAX_DELAY);unsigned n=frames;xSemaphoreGive(lock);return n;}
