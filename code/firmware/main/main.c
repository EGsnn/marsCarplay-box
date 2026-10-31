#include "esp_app_desc.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "probe.h"
#include "mars_board.h"
#include "iap2_core.h"
#include "probe_auth.h"
#include "probe_network.h"
#include "probe_discovery.h"
#include "probe_receiver.h"
#include "box_direct.h"
#include "media_bridge.h"
#include "live_store.h"
#include "auth_software_provider.h"
#include "tusb.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_heap_caps.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/task.h"

SemaphoreHandle_t probe_lock;
probe_status_t probe_status;
static iap2_core_t *link_core;
static size_t json_escape(const char *in,size_t len,char *out,size_t cap)
{
    static const char hex[]="0123456789abcdef";
    size_t o=0;
    for(size_t i=0;i<len&&o+1<cap;i++){
        unsigned char c=(unsigned char)in[i];
        switch(c){
        case '\\': if(o+2<cap){out[o++]='\\';out[o++]='\\';}break;
        case '"':  if(o+2<cap){out[o++]='\\';out[o++]='"';}break;
        case '\r': if(o+2<cap){out[o++]='\\';out[o++]='r';}break;
        case '\n': if(o+2<cap){out[o++]='\\';out[o++]='n';}break;
        case '\t': if(o+2<cap){out[o++]='\\';out[o++]='t';}break;
        default:
            if(c<0x20U||c>=0x7fU){
                if(o+6<cap){out[o++]='\\';out[o++]='u';out[o++]='0';out[o++]='0';
                    out[o++]=hex[(c>>4)&0xfU];out[o++]=hex[c&0xfU];}
            }else if(o+1<cap)out[o++]=(char)c;
        }
    }
    out[o]=0;
    return o;
}

static bool transmit(const uint8_t *bytes,size_t length,void *unused)
{
    (void)unused;
    if(!tud_vendor_mounted()||tud_vendor_write_available()<length) return false;
    uint32_t sent=tud_vendor_write(bytes,length);
    tud_vendor_write_flush();
    xSemaphoreTake(probe_lock,portMAX_DELAY);probe_status.tx_bytes+=sent;xSemaphoreGive(probe_lock);
    return sent==length;
}
static void message(uint16_t id,const uint8_t *body,size_t length,void *unused)
{
    (void)unused;
    xSemaphoreTake(probe_lock,portMAX_DELAY);
    probe_status.messages++;probe_status.last_message=id;
    memmove(probe_status.message_history,probe_status.message_history+1,7*sizeof(uint16_t));
    probe_status.message_history[7]=id;
    xSemaphoreGive(probe_lock);
    probe_auth_message(link_core,id,body,length);
}
static void state(iap2_core_state_t value,const char *reason,void *unused)
{
    (void)unused;xSemaphoreTake(probe_lock,portMAX_DELAY);
    probe_status.state=value;strlcpy(probe_status.reason,reason,sizeof(probe_status.reason));
    xSemaphoreGive(probe_lock);
}
static void link_task(void *unused)
{
    (void)unused;
    iap2_core_t *core=iap2_core_create(transmit,message,state,NULL);
    if(!core) abort();
    link_core=core;
    bool connected=false;
    for(;;) {
        bool mounted=tud_vendor_mounted();
        uint64_t now=esp_timer_get_time()/1000;
        if(mounted!=connected) {
            iap2_core_reset(core,mounted?"usb_configured":"usb_disconnected");
            probe_auth_reset();
            probe_network_mounted(mounted);
            xSemaphoreTake(probe_lock,portMAX_DELAY);
            probe_status.mounted=mounted;
            probe_status.rx_frames=probe_status.bad_frames=0;
            memset(probe_status.message_history,0,sizeof(probe_status.message_history));
            memset(&probe_status.trace,0,sizeof(probe_status.trace));
            if(mounted)probe_status.sessions++;
            xSemaphoreGive(probe_lock);
            if(mounted)iap2_core_start(core,now);
            connected=mounted;
        }
        if(mounted) {
            uint8_t bytes[512];uint32_t count;
            while((count=tud_vendor_read(bytes,sizeof(bytes)))>0) {
                xSemaphoreTake(probe_lock,portMAX_DELAY);probe_status.rx_bytes+=count;xSemaphoreGive(probe_lock);
                iap2_core_feed(core,bytes,count,now);
            }
            iap2_core_advance_time(core,now);
            xSemaphoreTake(probe_lock,portMAX_DELAY);
            probe_status.rx_frames=iap2_core_rx_frames(core);probe_status.bad_frames=iap2_core_bad_frames(core);
            xSemaphoreGive(probe_lock);
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
static esp_err_t status_json_buffer(httpd_req_t *request,char *json,size_t capacity)
{
    xSemaphoreTake(probe_lock,portMAX_DELAY);probe_status_t s=probe_status;xSemaphoreGive(probe_lock);
    char usb_ip6[48];
    probe_network_ip6(usb_ip6,sizeof(usb_ip6));
    snprintf(json,capacity,"{\"mode\":\"atk-box-direct\",\"carplay_video\":%s,\"mounted\":%s,\"sessions\":%u,\"state\":%d,\"reason\":\"%s\",\"rx_bytes\":%u,\"tx_bytes\":%u,\"rx_frames\":%u,\"bad_frames\":%u,\"messages\":%u,\"last_message\":%u,\"control_count\":%u,\"last_control\":{\"type\":%u,\"request\":%u,\"value\":%u,\"index\":%u}}",
        box_direct_video_available()?"true":"false",s.mounted?"true":"false",s.sessions,s.state,s.reason,s.rx_bytes,s.tx_bytes,s.rx_frames,s.bad_frames,s.messages,s.last_message,s.control_count,s.last_type,s.last_request,s.last_value,s.last_index);
    httpd_resp_set_type(request,"application/json");httpd_resp_set_hdr(request,"Cache-Control","no-store");
    /* auth strings are fixed internal constants. Append without message bodies. */
    size_t len=strlen(json);
    if(len>0&&json[len-1]=='}')snprintf(json+len-1,capacity-len+1,",\"firmware\":\"%s\",\"auth\":\"%s\",\"identity\":\"%s\",\"signer_ready\":%s,\"signer_stage\":\"%s\",\"signer_error\":%d,\"ncm\":{\"ip6\":\"%s\",\"rx\":%u,\"tx\":%u,\"ipv6_rx\":%u,\"drop\":%u,\"filter\":%u},\"message_history\":[%u,%u,%u,%u,%u,%u,%u,%u]}",esp_app_get_description()->version,s.auth,s.identity,s.signer_ready?"true":"false",auth_software_provider_stage(),auth_software_provider_error(),usb_ip6,s.net_rx,s.net_tx,s.net_ipv6_rx,s.net_drop,s.net_filter,s.message_history[0],s.message_history[1],s.message_history[2],s.message_history[3],s.message_history[4],s.message_history[5],s.message_history[6],s.message_history[7]);
    len=strlen(json);
    char key[96];
    snprintf(key,sizeof(key),",\"main_stack_min_bytes\":%u,\"identity_rejection\":",s.main_stack_min_bytes);
    if(!len||json[len-1]!='}'||len+strlen(key)+2>=capacity)return httpd_resp_send_err(request,HTTPD_500_INTERNAL_SERVER_ERROR,"Status overflow");
    memcpy(json+len-1,key,strlen(key)+1);len=len-1+strlen(key);
    if(!probe_identity_rejection_json(&s.rejection,json+len,capacity-len-1))return httpd_resp_send_err(request,HTTPD_500_INTERNAL_SERVER_ERROR,"Diagnostic overflow");
    len=strlen(json);
    int appended=snprintf(json+len,capacity-len,",\"discovery\":{\"stage\":\"%s\",\"error\":%d,\"rx\":%u,\"tx\":%u,\"bad\":%u,\"responses\":%u,\"airplay_queries\":%u,\"control_ptr\":%u,\"control_port\":%u}}",s.discovery_stage,s.discovery_error,s.discovery_rx,s.discovery_tx,s.discovery_bad,s.discovery_responses,s.discovery_airplay_queries,s.discovery_control_ptr,s.discovery_port);
    if(appended<0||(size_t)appended>=capacity-len)return httpd_resp_send_err(request,HTTPD_500_INTERNAL_SERVER_ERROR,"Discovery overflow");
    len=strlen(json);
    if(!len||json[len-1]!='}')return ESP_FAIL;
    int audio_appended=snprintf(json+len-1,capacity-len+1,",\"audio\":{\"packets\":%u,\"bytes\":%u}}",s.audio_rx_packets,s.audio_rx_bytes);
    if(audio_appended<0||(size_t)audio_appended>=capacity-len+1)return ESP_FAIL;
    len=strlen(json);
    int setup_appended=snprintf(json+len-1,capacity-len+1,
        ",\"setup\":{\"initial_port\":%u,\"initial_body_bytes\":%u,\"stream_port\":%u,\"stream_body_bytes\":%u,\"stream_count\":%u,\"keepalive_packets\":%u,\"keepalive_bytes\":%u,\"keepalive_last_rx_ms\":%u,\"keepalive_rx_error\":%d}}",
        s.initial_setup_port,s.initial_setup_bytes,s.stream_setup_port,s.stream_setup_bytes,s.stream_setup_count,
        s.keepalive_rx_packets,s.keepalive_rx_bytes,s.keepalive_last_rx_ms,s.keepalive_rx_error);
    if(setup_appended<0||(size_t)setup_appended>=capacity-len+1)return ESP_FAIL;
    len=strlen(json);
    int screen_appended=snprintf(json+len-1,capacity-len+1,
        ",\"screen\":{\"active\":%s,\"frames\":%u,\"configs\":%u,\"ignored\":%u,\"last_opcode\":%u,\"last_body_bytes\":%u,\"last_frame_ms\":%u,\"exit_event\":%u,\"exit_phase\":%u,\"exit_bytes\":%u,\"exit_ms\":%u,\"error\":%d,\"local_close_ms\":%u}}",
        s.screen.active?"true":"false",s.screen.frames,s.screen.configs,s.screen.ignored,
        s.screen.last_opcode,s.screen.last_body_bytes,s.screen.last_frame_ms,
        s.screen.exit_event,s.screen.exit_phase,s.screen.exit_bytes,s.screen.exit_ms,s.screen.error,s.screen.local_close_ms);
    if(screen_appended<0||(size_t)screen_appended>=capacity-len+1)return ESP_FAIL;
    len=strlen(json);
    char esc0[1600],esc1[1600];
    json_escape(s.event_rx_dump[0],s.event_rx_dump_len[0],esc0,sizeof(esc0));
    json_escape(s.event_rx_dump[1],s.event_rx_dump_len[1],esc1,sizeof(esc1));
    int extra=snprintf(json+len-1,capacity-len+1,",\"receiver\":{\"stage\":\"%s\",\"error\":%d,\"http\":%d,\"listening\":%s,\"active\":%s,\"requests\":%u,\"connects\":%u,\"event_rx\":{\"reason\":%u,\"errno\":%d,\"dump\":[\"%s\",\"%s\"],\"len\":[%u,%u]}},\"capture\":",s.receiver_stage,s.receiver_error,s.receiver_http,s.receiver_listening?"true":"false",s.receiver_active?"true":"false",s.receiver_requests,s.receiver_connects,s.event_rx_reason,s.event_rx_errno,esc0,esc1,(unsigned)s.event_rx_dump_len[0],(unsigned)s.event_rx_dump_len[1]);
    if(extra<0||(size_t)extra>=capacity-len)return ESP_FAIL;
    len=strlen(json);size_t count=media_bridge_capture_status(json+len,capacity-len-1);
    if(count>=capacity-len-1)return ESP_FAIL;
    len+=count;
    const char *trace_key=",\"rtsp_trace\":";
    if(len+strlen(trace_key)+2>=capacity)return ESP_FAIL;
    memcpy(json+len,trace_key,strlen(trace_key));len+=strlen(trace_key);
    count=probe_trace_json(&s.trace,json+len,capacity-len-1);
    if(!count)return ESP_FAIL;
    len+=count;json[len++]='}';json[len]=0;
    len=strlen(json);
    if(!len||json[len-1]!='}')return ESP_FAIL;
    int added=snprintf(json+len-1,capacity-len+1,",\"local_display\":");
    if(added<0||(size_t)added>=capacity-len+1)return ESP_FAIL;
    len=len-1+(size_t)added;
    size_t written=box_direct_status_json(json+len,capacity-len-1);
    if(written>=capacity-len-1)return ESP_FAIL;
    len+=written;json[len++]='}';json[len]=0;
    return httpd_resp_sendstr(request,json);
}
static esp_err_t status_json(httpd_req_t *request)
{
    /* Keep the growing diagnostic JSON off the HTTP worker's stack. */
    const size_t capacity=12288;
    char *json=heap_caps_malloc(capacity,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!json)return httpd_resp_send_err(request,HTTPD_500_INTERNAL_SERVER_ERROR,"Status allocation failed");
    esp_err_t result=status_json_buffer(request,json,capacity);
    heap_caps_free(json);
    return result;
}
static esp_err_t video_config(httpd_req_t *request)
{
    /* Export only SPS/PPS, never decoded pixels, video frames or pairing keys. */
    const size_t capacity=LIVE_HEADER_BYTES+LIVE_CONFIG_BYTES;
    uint8_t *packet=heap_caps_malloc(capacity,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!packet)return httpd_resp_send_err(request,HTTPD_500_INTERNAL_SERVER_ERROR,"Config allocation failed");
    int n=live_store_copy(0,0,packet,capacity);
    esp_err_t result;
    if(n<=(int)LIVE_HEADER_BYTES||packet[4]!=1){
        result=httpd_resp_send_err(request,HTTPD_404_NOT_FOUND,"No current H.264 configuration");
    }else{
        httpd_resp_set_type(request,"application/octet-stream");
        httpd_resp_set_hdr(request,"Cache-Control","no-store");
        result=httpd_resp_send(request,(const char *)packet+LIVE_HEADER_BYTES,n-LIVE_HEADER_BYTES);
    }
    heap_caps_free(packet);return result;
}
static void wifi_status(void)
{
    ESP_ERROR_CHECK(esp_netif_init());ESP_ERROR_CHECK(esp_event_loop_create_default());esp_netif_create_default_wifi_ap();
    wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT();init.nvs_enable=false;
    ESP_ERROR_CHECK(esp_wifi_init(&init));ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    wifi_config_t config={.ap={.ssid="ATK-CarPlay",.channel=6,.max_connection=2,.authmode=WIFI_AUTH_WPA2_PSK}};
    strlcpy((char *)config.ap.password,CONFIG_PROBE_AP_PASSWORD,sizeof(config.ap.password));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP,&config));ESP_ERROR_CHECK(esp_wifi_start());
    httpd_handle_t server;httpd_config_t cfg=HTTPD_DEFAULT_CONFIG();cfg.stack_size=12288;ESP_ERROR_CHECK(httpd_start(&server,&cfg));
    httpd_uri_t endpoints[]={{.uri="/",.method=HTTP_GET,.handler=status_json},{.uri="/status",.method=HTTP_GET,.handler=status_json},
        {.uri="/video-config.h264",.method=HTTP_GET,.handler=video_config}};
    for(unsigned i=0;i<sizeof(endpoints)/sizeof(endpoints[0]);i++)ESP_ERROR_CHECK(httpd_register_uri_handler(server,&endpoints[i]));

}
void app_main(void)
{
    if(strlen(CONFIG_PROBE_AP_PASSWORD)<8) abort();
    probe_lock=xSemaphoreCreateMutex();if(!probe_lock)abort();
    ESP_ERROR_CHECK(mars_board_init());
    const mars_board_info_t *board=mars_board_info();
    if(board->width!=320||board->height!=240||board->flash_bytes!=16*1024*1024||board->psram_bytes!=8*1024*1024)abort();
    printf("ATK BOARD %s LCD %ux%u FLASH %u PSRAM %u\n",board->id,board->width,board->height,(unsigned)board->flash_bytes,(unsigned)board->psram_bytes);
    ESP_ERROR_CHECK(mars_board_fill_rect(0,0,320,240,0));
    probe_auth_reset();
    probe_status.signer_ready=auth_software_provider_init()==ESP_OK;
    if(!probe_status.signer_ready)strlcpy(probe_status.auth,"SIGNER INIT FAILED",sizeof(probe_status.auth));
    box_direct_start();
    wifi_status();
    probe_network_prepare();
    probe_usb_install(); /* Internal PHY moves from Serial/JTAG to OTG. */
    probe_network_start();
    probe_receiver_start();
    probe_discovery_start();
    if(xTaskCreate(link_task,"iap2-link",12288,NULL,5,NULL)!=pdPASS)abort();
    for(;;) {
        unsigned stack_min=uxTaskGetStackHighWaterMark(NULL);
        xSemaphoreTake(probe_lock,portMAX_DELAY);probe_status.main_stack_min_bytes=stack_min;xSemaphoreGive(probe_lock);
        box_direct_sample();vTaskDelay(pdMS_TO_TICKS(250));
    }
}
