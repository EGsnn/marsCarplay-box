/* One decode/display owner; USB reception never waits for this consumer. */
#include "box_direct.h"
#include "box_perf.h"
#include "box_decode_profile.h"
#include "sdkconfig.h"
#include "box_decoder.h"
#include "box_pixels.h"
#include "live_store.h"
#include "airplay_control_port.h"
#include "airplay_touch_report.h"
#include "airplay_display_config.h"
#include "media_bridge.h"
#include "mars_board.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static SemaphoreHandle_t lock;
static uint8_t *packet;
static uint16_t *canvas;
static unsigned decoded,shown,gaps,errors,touch_reports,touch_errors,width,height,stack_min;
static uint32_t display_epoch;
static box_perf_t perf;
static box_decode_profile_totals_t decode_profile;
static unsigned decoder_resets,skipped_waiting_idr;
static const volatile uint32_t box_clock_config[]={CONFIG_SPIRAM_SPEED,CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ};
static const volatile uint32_t box_video_config[]={AIRPLAY_DISPLAY_WIDTH,AIRPLAY_DISPLAY_HEIGHT,AIRPLAY_DISPLAY_MAX_FPS};
static bool have_frame,waiting=true,panel_has_frame;
static double decode_ms,convert_ms,present_ms,process_ms;
static int64_t callback_us;
static char phase[40]="WAIT CARPLAY";
static box_rect_t rectangle;
static uint32_t be32(const uint8_t *p){return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static bool active(void){xSemaphoreTake(probe_lock,portMAX_DELAY);bool a=probe_status.screen.active;xSemaphoreGive(probe_lock);return a;}
static void note(const char *s){xSemaphoreTake(lock,portMAX_DELAY);snprintf(phase,sizeof(phase),"%s",s);xSemaphoreGive(lock);}
bool box_direct_video_available(void){if(!lock)return false;xSemaphoreTake(lock,portMAX_DELAY);bool result=have_frame;xSemaphoreGive(lock);return result;}
void box_direct_sample(void){
 if(!lock)return;
 uint32_t received=media_bridge_capture_frames();
 xSemaphoreTake(lock,portMAX_DELAY);
 box_perf_sample(&perf,esp_timer_get_time(),received,decoded,shown);
 xSemaphoreGive(lock);
}
size_t box_direct_status_json(char *out,size_t capacity){
 if(!lock)return snprintf(out,capacity,"{\"ready\":false}");
 char queue_json[512],perf_json[1024],profile_json[1024];
 size_t q=live_store_status(queue_json,sizeof(queue_json));
 if(!q||q>=sizeof(queue_json))return 0;
 xSemaphoreTake(lock,portMAX_DELAY);
 size_t p=box_perf_json(&perf,perf_json,sizeof(perf_json));
 if(!p||p>=sizeof(perf_json)){xSemaphoreGive(lock);return 0;}
 size_t profile_size=box_decode_profile_json(&decode_profile,profile_json,sizeof(profile_json));
 if(!profile_size||profile_size>=sizeof(profile_json)){xSemaphoreGive(lock);return 0;}
 int n=snprintf(out,capacity,"{\"ready\":true,\"mode\":\"on_board_decode\",\"requested_width\":%u,\"requested_height\":%u,\"requested_max_fps\":%u,\"decoded\":%u,\"shown\":%u,\"gaps\":%u,\"errors\":%u,\"waiting_idr\":%s,\"source_width\":%u,\"source_height\":%u,\"display_width\":320,\"display_height\":240,\"holding_last_frame\":%s,\"lcd_diagnostics\":false,\"process_ms\":%.2f,\"decode_ms\":%.2f,\"convert_ms\":%.2f,\"present_ms\":%.2f,\"phase\":\"%s\",\"touch_reports\":%u,\"touch_errors\":%u,\"stack_min_bytes\":%u,\"psram_free\":%u,\"psram_config_mhz\":%u,\"cpu_config_mhz\":%u,\"decoder_resets\":%u,\"skipped_waiting_idr\":%u,\"queue\":%s,\"performance\":%s,\"decode_profile\":%s}",(unsigned)box_video_config[0],(unsigned)box_video_config[1],(unsigned)box_video_config[2],decoded,shown,gaps,errors,waiting?"true":"false",width,height,panel_has_frame&&!have_frame?"true":"false",process_ms,decode_ms,convert_ms,present_ms,phase,touch_reports,touch_errors,stack_min,(unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),(unsigned)box_clock_config[0],(unsigned)box_clock_config[1],decoder_resets,skipped_waiting_idr,queue_json,perf_json,profile_json);
 xSemaphoreGive(lock);return n<0?0:(size_t)n;
}
static void display(const box_yuv_t *im,void *unused){
 (void)unused;int64_t callback_start=esp_timer_get_time();
 xSemaphoreTake(lock,portMAX_DELAY);++decoded;xSemaphoreGive(lock);
 // A slow decode from an old USB/config generation must never paint a new one.
 if(live_store_epoch()!=display_epoch||!active()){callback_us+=esp_timer_get_time()-callback_start;return;}
 int64_t begin=esp_timer_get_time();
 if(!box_pixels_fit(im,canvas,320,240)){note("PIXEL FORMAT ERROR");callback_us+=esp_timer_get_time()-callback_start;return;}
 int64_t converted=esp_timer_get_time();
 esp_err_t r=mars_board_blit(0,0,320,240,canvas);
 int64_t end=esp_timer_get_time();
 xSemaphoreTake(lock,portMAX_DELAY);
 box_perf_output(&perf,converted-begin,end-converted);convert_ms=(converted-begin)/1000.0;present_ms=(end-converted)/1000.0;
 if(r==ESP_OK){++shown;width=im->width;height=im->height;rectangle=box_fit(width,height,320,240);have_frame=panel_has_frame=true;waiting=false;snprintf(phase,sizeof(phase),"CARPLAY ON BOX");}
 else{++errors;have_frame=false;snprintf(phase,sizeof(phase),"LCD TRANSFER ERROR");}
 xSemaphoreGive(lock);callback_us+=esp_timer_get_time()-callback_start;
}
static box_decoder_t *reset_decoder(box_decoder_t *old,const uint8_t *config,size_t size){
 xSemaphoreTake(lock,portMAX_DELAY);++decoder_resets;xSemaphoreGive(lock);
 box_decoder_destroy(old);box_decoder_t *d=box_decoder_create();
 if(!d){note("DECODER MEMORY");return NULL;}
 if(size&&box_decoder_feed(d,config,size,NULL,NULL)<0){note(box_decoder_error(d));box_decoder_destroy(d);return NULL;}
 return d;
}
static void decode_task(void *unused){
 (void)unused;box_decoder_t *decoder=NULL;uint32_t epoch=0,after=0;uint8_t config[LIVE_CONFIG_BYTES];size_t config_len=0;
 bool want_idr=true,was_active=false;int64_t key_at=0;
 // This task owns every LCD transfer. Idle artwork is drawn once, never on
 // decoder recovery: the last completed picture stays visible while waiting.
 probe_ui_draw_idle();
 for(;;){
  bool connected=active();int64_t now=esp_timer_get_time();
  if(!connected&&was_active){box_decoder_destroy(decoder);decoder=NULL;config_len=0;epoch=after=0;want_idr=true;xSemaphoreTake(lock,portMAX_DELAY);have_frame=panel_has_frame=false;waiting=true;width=height=0;xSemaphoreGive(lock);note("WAIT CARPLAY");probe_ui_draw_idle();}
  was_active=connected;
  if(connected){
   int n=live_store_copy(epoch,after,packet,LIVE_HEADER_BYTES+LIVE_MAX_PAYLOAD);
   if(n<0){note("PACKET BOUNDS ERROR");vTaskDelay(pdMS_TO_TICKS(20));continue;}
   if(n>0){
    unsigned kind=packet[4],flags=packet[5];uint32_t pe=be32(packet+8),seq=be32(packet+12),size=be32(packet+32);
    if(size+LIVE_HEADER_BYTES!=(unsigned)n){note("PACKET SIZE ERROR");continue;}
    if(kind==1){
     if(size>sizeof(config)){note("CONFIG TOO LARGE");continue;}
     memcpy(config,packet+LIVE_HEADER_BYTES,size);config_len=size;epoch=pe;after=0;want_idr=true;
     xSemaphoreTake(lock,portMAX_DELAY);have_frame=false;waiting=true;xSemaphoreGive(lock);
     decoder=reset_decoder(decoder,config,config_len);if(decoder)note("WAIT KEYFRAME");
    }else if(kind==2&&pe==epoch){
     if((flags&2)||seq!=after+1){xSemaphoreTake(lock,portMAX_DELAY);++gaps;waiting=true;have_frame=false;xSemaphoreGive(lock);want_idr=true;decoder=reset_decoder(decoder,config,config_len);note("RESYNC KEYFRAME");}
     after=seq;
     if(decoder&&(!want_idr||(flags&1))){
      display_epoch=pe;callback_us=0;int64_t start=esp_timer_get_time();int result=box_decoder_feed(decoder,packet+LIVE_HEADER_BYTES,size,display,NULL);int64_t elapsed_us=esp_timer_get_time()-start;
      uint64_t pure_us=elapsed_us>callback_us?(uint64_t)(elapsed_us-callback_us):0;
      box_decoder_profile_t sample={0};bool sampled=result>0&&box_decoder_profile(decoder,&sample);
      if(sampled){xSemaphoreTake(lock,portMAX_DELAY);box_decode_profile_record(&decode_profile,&sample);xSemaphoreGive(lock);}
      xSemaphoreTake(lock,portMAX_DELAY);box_perf_decode(&perf,pure_us,elapsed_us);process_ms=elapsed_us/1000.0;decode_ms=pure_us/1000.0;xSemaphoreGive(lock);
      if(result<0){char error[40];snprintf(error,sizeof(error),"%s",box_decoder_error(decoder));xSemaphoreTake(lock,portMAX_DELAY);++errors;waiting=true;have_frame=false;xSemaphoreGive(lock);decoder=reset_decoder(decoder,config,config_len);note(error);want_idr=true;}
      else if(result>0)want_idr=false;
     }else{xSemaphoreTake(lock,portMAX_DELAY);++skipped_waiting_idr;xSemaphoreGive(lock);}
    }
    // Let the idle task run even when catching up; each native decode also yields.
    vTaskDelay(1);
   }else vTaskDelay(pdMS_TO_TICKS(5));
   if(want_idr&&now-key_at>2000000){(void)airplay_control_port_request_main_screen_keyframe();key_at=now;}
  }else vTaskDelay(pdMS_TO_TICKS(20));
  xSemaphoreTake(lock,portMAX_DELAY);stack_min=uxTaskGetStackHighWaterMark(NULL);xSemaphoreGive(lock);
 }
}
static void touch_task(void *unused){
 (void)unused;bool held=false;uint16_t lx=0,ly=0;int64_t last=0;
 for(;;){
  uint16_t x,y,tx=0,ty=0;bool down=mars_board_touch_read(&x,&y);
  xSemaphoreTake(lock,portMAX_DELAY);bool ready=have_frame&&!waiting;box_rect_t r=rectangle;xSemaphoreGive(lock);
  bool wanted=down&&ready&&active()&&box_touch_map(r,AIRPLAY_DISPLAY_WIDTH,AIRPLAY_DISPLAY_HEIGHT,x,y,&tx,&ty);
  int64_t now=esp_timer_get_time();
  if(wanted!=held||(wanted&&(tx!=lx||ty!=ly||now-last>200000))){
   if(wanted){lx=tx;ly=ty;}uint8_t command[]={wanted,lx&255,lx>>8,ly&255,ly>>8},report[12];
   bool sent=airplay_touch_report(command,sizeof(command),report)&&airplay_control_port_send_hid_report(AIRPLAY_TOUCH_UID,report,sizeof(report),NULL);
   xSemaphoreTake(lock,portMAX_DELAY);if(sent)++touch_reports;else ++touch_errors;xSemaphoreGive(lock);
   // On failure still retry release; never invent success or replay old moves.
   if(sent){held=wanted;last=now;}else if(!wanted)held=true;
  }
  vTaskDelay(pdMS_TO_TICKS(20));
 }
}
void box_direct_start(void){
 lock=xSemaphoreCreateMutex();packet=heap_caps_malloc(LIVE_HEADER_BYTES+LIVE_MAX_PAYLOAD,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);canvas=heap_caps_malloc(320*240*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
 if(!lock||!packet||!canvas||!media_bridge_capture_init())abort();
 box_perf_init(&perf,esp_timer_get_time(),media_bridge_capture_frames());
 if(xTaskCreatePinnedToCore(decode_task,"box-video",32768,NULL,3,NULL,1)!=pdPASS)abort();
 if(xTaskCreate(touch_task,"box-touch",4096,NULL,3,NULL)!=pdPASS)abort();
}
