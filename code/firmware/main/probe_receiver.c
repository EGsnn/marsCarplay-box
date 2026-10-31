#include "probe_receiver.h"
#include "probe.h"
#include "probe_network.h"
#include "probe_info.h"
#include "probe_crypto.h"
#include "airplay_rtsp_server.h"
#include "airplay_bplist.h"
#include "airplay_diagnostics.h"
#include "airplay_screen.h"
#include "media_bridge.h"
#include "live_store.h"
#include "ed25519.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "mbedtls/platform_util.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include "freertos/task.h"
static uint8_t pub[32],priv[64],controller_id[64],controller_key[32];
static size_t controller_id_len;
static char pub_hex[65];
static airplay_rtsp_server_t *server;
static const uint8_t identifier[]="9a307d11-8b7a-4fd6-9001-000000000146";
static bool random_bytes(uint8_t *p,size_t n,void *ctx){(void)ctx;esp_fill_random(p,n);return true;}
static bool sign_message(const uint8_t *p,size_t n,uint8_t sig[64],void *ctx)
{(void)ctx;ed25519_sign(sig,p,n,pub,priv);return true;}
static bool verify(const uint8_t key[32],const uint8_t *p,size_t n,const uint8_t sig[64],void *ctx)
{(void)ctx;return ed25519_verify(sig,p,n,key)==1;}
static bool save(const uint8_t *id,size_t n,const uint8_t key[32],void *ctx)
{
    (void)ctx;if(!n||n>sizeof(controller_id))return false;
    memcpy(controller_id,id,n);memcpy(controller_key,key,32);controller_id_len=n;return true;
}
static bool lookup(const uint8_t *id,size_t n,uint8_t key[32],void *ctx)
{
    (void)ctx;if(n!=controller_id_len||memcmp(id,controller_id,n))return false;
    memcpy(key,controller_key,32);return true;
}
static void state(const char *s,int error)
{
    xSemaphoreTake(probe_lock,portMAX_DELAY);
    snprintf(probe_status.receiver_stage,sizeof(probe_status.receiver_stage),"%s",s);
    probe_status.receiver_error=error;xSemaphoreGive(probe_lock);
}
void probe_receiver_screen_note(unsigned event,unsigned opcode,unsigned bytes,int detail)
{
    unsigned now=esp_timer_get_time()/1000;
    bool trace=true;
    /* Never replay the previous USB session while the new peer negotiates SPS/PPS. */
    if(event==PROBE_EVENT_SCREEN_CONNECTED)live_store_config(NULL,0);
    xSemaphoreTake(probe_lock,portMAX_DELAY);
    if(event==PROBE_EVENT_SCREEN_CONNECTED){
        memset(&probe_status.screen,0,sizeof(probe_status.screen));
        probe_status.screen.active=true;
    } else if(event==PROBE_EVENT_SCREEN_HEADER){
        probe_status.screen.last_opcode=opcode;
        probe_status.screen.last_body_bytes=bytes;
        trace=false;
    } else if(event==PROBE_EVENT_SCREEN_FRAME){
        probe_status.screen.frames++;
        probe_status.screen.last_frame_ms=now;
        trace=probe_status.screen.frames==1 || probe_status.screen.frames%30==0;
    } else if(event==PROBE_EVENT_SCREEN_CONFIG){
        probe_status.screen.configs++;
    } else if(event==PROBE_EVENT_SCREEN_IGNORED){
        probe_status.screen.ignored++;
        trace=probe_status.screen.ignored==1 || probe_status.screen.ignored%30==0;
    } else if(event==PROBE_EVENT_SCREEN_CLOSE_RTSP){
        probe_status.screen.local_close_ms=now;
    } else if((event>=PROBE_EVENT_SCREEN_EOF && event<=PROBE_EVENT_SCREEN_PROCESS_FAILED) ||
              event==PROBE_EVENT_SCREEN_STOPPED || event==PROBE_EVENT_SCREEN_ACCEPT_FAILED){
        if(!probe_status.screen.exit_event){
            probe_status.screen.exit_event=event;
            probe_status.screen.exit_phase=(event==PROBE_EVENT_SCREEN_EOF || event==PROBE_EVENT_SCREEN_RECV_FAILED)?opcode:0;
            probe_status.screen.exit_bytes=bytes;
            probe_status.screen.exit_ms=now;
            probe_status.screen.error=detail;
        }
        probe_status.screen.active=false;
    }
    if(trace)probe_trace_add(&probe_status.trace,event,0,bytes,
                            detail?detail:(int)opcode,now);
    xSemaphoreGive(probe_lock);
}
/* Called by the receiver core after a bounded RTSP request. */
void probe_receiver_event(unsigned event,int detail,unsigned bytes)
{
    xSemaphoreTake(probe_lock,portMAX_DELAY);
    probe_trace_add(&probe_status.trace,event,0,bytes,detail,esp_timer_get_time()/1000);
    if(event>=103&&event<=119)probe_status.receiver_error=detail?detail:-(int)event;
    xSemaphoreGive(probe_lock);
}
void probe_receiver_event_response(const uint8_t *header,size_t size,unsigned bytes)
{
    xSemaphoreTake(probe_lock,portMAX_DELAY);
    probe_trace_add(&probe_status.trace,135,probe_trace_response_code(header,size),bytes,
                    probe_trace_response_cseq(header,size),esp_timer_get_time()/1000);
    xSemaphoreGive(probe_lock);
}
/* Called only after a complete SETUP response was written successfully.
 * Inspect the actual plaintext response, rather than inferring its fields
 * from callback registration or the size of the incoming phone request. */
void probe_receiver_setup_response(const uint8_t *wire, size_t len)
{
    if (probe_trace_response_code(wire,len)!=200) return;
    size_t offset=0;
    while (offset+4<=len && memcmp(wire+offset,"\r\n\r\n",4)) ++offset;
    if (offset+4>len) return;
    offset+=4;
    airplay_bplist_doc_t doc;airplay_bplist_object_t root,value;
    if (airplay_bplist_open(wire+offset,len-offset,&doc)!=AIRPLAY_BPLIST_OK ||
        airplay_bplist_root(&doc,&root)!=AIRPLAY_BPLIST_OK) return;
    uint64_t port=0,count=0;
    airplay_bplist_type_t type;
    if (airplay_bplist_dict_get(&doc,root,"keepAlivePort",&value)==AIRPLAY_BPLIST_OK &&
        airplay_bplist_get_uint(&doc,value,&port)!=AIRPLAY_BPLIST_OK) return;
    if (port>UINT16_MAX) return;
    bool streams=airplay_bplist_dict_get(&doc,root,"streams",&value)==AIRPLAY_BPLIST_OK;
    if (streams && (airplay_bplist_get_type(&doc,value,&type,&count)!=AIRPLAY_BPLIST_OK ||
                    type!=AIRPLAY_BPLIST_ARRAY)) return;
    xSemaphoreTake(probe_lock,portMAX_DELAY);
    if (streams) {
        probe_status.stream_setup_port=(unsigned)port;
        probe_status.stream_setup_bytes=(unsigned)(len-offset);
        probe_status.stream_setup_count=(unsigned)count;
    } else {
        probe_status.initial_setup_port=(unsigned)port;
        probe_status.initial_setup_bytes=(unsigned)(len-offset);
    }
    probe_trace_add(&probe_status.trace,streams?PROBE_EVENT_STREAM_SETUP_SENT:PROBE_EVENT_INITIAL_SETUP_SENT,
                    200,(unsigned)(len-offset),(int)port,esp_timer_get_time()/1000);
    xSemaphoreGive(probe_lock);
}
void probe_receiver_keepalive_note(unsigned bytes,int error)
{
    xSemaphoreTake(probe_lock,portMAX_DELAY);
    if (error) {
        probe_status.keepalive_rx_error=error;
        probe_trace_add(&probe_status.trace,PROBE_EVENT_KEEPALIVE_RX_FAILED,0,0,error,esp_timer_get_time()/1000);
    } else {
        probe_status.keepalive_rx_packets++;
        probe_status.keepalive_rx_bytes+=bytes;
        probe_status.keepalive_last_rx_ms=esp_timer_get_time()/1000;
        if (probe_status.keepalive_rx_packets==1)
            probe_trace_add(&probe_status.trace,PROBE_EVENT_KEEPALIVE_RX,0,bytes,0,probe_status.keepalive_last_rx_ms);
    }
    xSemaphoreGive(probe_lock);
}
/* 0.6.0: record the head of every plaintext message received on the event
 * channel (first 96 chars / up to the first CRLF block) plus the channel
 * close reason. plaintext==NULL means no payload: record reason/errno only. */
void probe_receiver_event_rx_note(const uint8_t *plaintext,size_t len,
                                  unsigned reason,int error_code)
{
    xSemaphoreTake(probe_lock,portMAX_DELAY);
    probe_status.event_rx_reason=reason;
    probe_status.event_rx_errno=error_code;
    if(plaintext&&len){
        unsigned idx=probe_status.event_rx_head_index++&1;
        char *dst=probe_status.event_rx_dump[idx];
        size_t n=len;
        if(n>sizeof(probe_status.event_rx_dump[0])-1)
            n=sizeof(probe_status.event_rx_dump[0])-1;
        memcpy(dst,plaintext,n);dst[n]=0;
        probe_status.event_rx_dump_len[idx]=n;
    }
    xSemaphoreGive(probe_lock);
}
/* 0.6.3: count every UDP datagram received on the audio sink (no decode). */
void probe_receiver_audio_note(unsigned bytes,unsigned packets)
{
    xSemaphoreTake(probe_lock,portMAX_DELAY);
    probe_status.audio_rx_packets+=packets;
    probe_status.audio_rx_bytes+=bytes;
    xSemaphoreGive(probe_lock);
}
void probe_receiver_request(const char *method,const char *path,bool cipher,unsigned code,unsigned bytes,int result){
    (void)method;
    const char *stage=cipher?"CONTROL CIPHER":
        strstr(path,"pair-setup")?"PAIR SETUP REQUEST":
        strstr(path,"pair-verify")?"PAIR VERIFY REQUEST":
        strstr(path,"auth-setup")?"AUTH SETUP REQUEST":
        strstr(path,"info")?"INFO REQUEST":"RTSP REQUEST";
    state(stage,0);
    xSemaphoreTake(probe_lock,portMAX_DELAY);probe_status.receiver_requests++;
    probe_trace_add(&probe_status.trace,probe_trace_route(method,path),code,bytes,cipher?1:result,esp_timer_get_time()/1000);
    xSemaphoreGive(probe_lock);
}
void probe_receiver_connection(bool active)
{
    xSemaphoreTake(probe_lock,portMAX_DELAY);probe_status.receiver_active=active;
    if(active){
        memset(&probe_status.screen,0,sizeof(probe_status.screen));
        probe_status.initial_setup_port=probe_status.initial_setup_bytes=0;
        probe_status.stream_setup_port=probe_status.stream_setup_bytes=probe_status.stream_setup_count=0;
        probe_status.keepalive_rx_packets=probe_status.keepalive_rx_bytes=probe_status.keepalive_last_rx_ms=0;
        probe_status.keepalive_rx_error=0;
    }
    xSemaphoreGive(probe_lock);
    state(active?"RTSP CONNECTED":"RTSP DISCONNECTED",0);
    probe_receiver_event(active?100:101,0,0);
}
bool probe_receiver_ready(void)
{
    xSemaphoreTake(probe_lock,portMAX_DELAY);bool ready=probe_status.receiver_listening;xSemaphoreGive(probe_lock);return ready;
}
const char *probe_receiver_public_hex(void){return pub_hex;}
static void task(void *ctx)
{
    (void)ctx;
    uint8_t seed[32],sig[64];esp_fill_random(seed,32);ed25519_create_keypair(pub,priv,seed);
    mbedtls_platform_zeroize(seed,32);
    if(probe_crypto_self_test()||!airplay_screen_crypto_self_test()||
       !sign_message(identifier,sizeof(identifier)-1,sig,NULL)||
       !verify(pub,identifier,sizeof(identifier)-1,sig,NULL)||!media_bridge_capture_init()){
        state("RECEIVER INIT FAILED",-1);vTaskDelete(NULL);return;
    }
    for(unsigned i=0;i<32;i++)snprintf(pub_hex+2*i,3,"%02x",pub[i]);
    state("WAIT USB ADDRESS",0);
    for(;;){
        xSemaphoreTake(probe_lock,portMAX_DELAY);bool mounted=probe_status.mounted;xSemaphoreGive(probe_lock);
        char ip[48];probe_network_ip6(ip,sizeof(ip));
        if(mounted&&ip[0]&&!server){
            airplay_rtsp_server_config_t cfg={.info_plist=probe_info,.info_plist_len=sizeof(probe_info),.control_port=5000};
            cfg.pairing.pair_setup=(airplay_pair_setup_config_t){.pairing_code="3939",.accessory_identifier=identifier,.accessory_identifier_len=sizeof(identifier)-1,.accessory_public_key=pub,.random=random_bytes,.sign_accessory=sign_message,.verify_controller=verify,.save_pairing=save};
            cfg.pairing.pair_verify=(airplay_pair_verify_config_t){.accessory_identifier=identifier,.accessory_identifier_len=sizeof(identifier)-1,.random=random_bytes,.sign_accessory=sign_message,.verify_controller=verify,.lookup_controller=lookup};
            esp_err_t result=airplay_rtsp_server_start(&cfg,&server);
            state(result==ESP_OK?"RTSP LISTENING":"RTSP START FAILED",result);
            xSemaphoreTake(probe_lock,portMAX_DELAY);probe_status.receiver_listening=result==ESP_OK;xSemaphoreGive(probe_lock);
        }
        if(!mounted&&server){
            airplay_rtsp_server_stop(server);server=NULL;
            xSemaphoreTake(probe_lock,portMAX_DELAY);probe_status.receiver_listening=false;xSemaphoreGive(probe_lock);
            state("WAIT USB ADDRESS",0);
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
void probe_receiver_start(void)
{
    if(xTaskCreate(task,"receiver",16384,NULL,3,NULL)!=pdPASS)abort();
}
void probe_receiver_connect(const uint8_t ip[16],uint16_t port)
{
    if(!probe_receiver_ready())return;
    xSemaphoreTake(probe_lock,portMAX_DELAY);bool active=probe_status.receiver_active;xSemaphoreGive(probe_lock);
    if(active)return;
    int fd=socket(AF_INET6,SOCK_STREAM,IPPROTO_TCP);if(fd<0){state("CONNECT SOCKET ERROR",errno);return;}
    struct ifreq iface={0};probe_network_name(iface.ifr_name,sizeof(iface.ifr_name));
    struct timeval timeout={.tv_sec=2};
    struct sockaddr_in6 peer={.sin6_family=AF_INET6,.sin6_port=htons(port),.sin6_scope_id=probe_network_index()};
    memcpy(&peer.sin6_addr,ip,16);
    int error=0,code=0;char addr[48],request[384],response[512];
    if(setsockopt(fd,SOL_SOCKET,SO_BINDTODEVICE,&iface,sizeof(iface))||
       setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout))||
       setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout))){error=errno;goto done;}
    fcntl(fd,F_SETFL,O_NONBLOCK);
    int result=connect(fd,(struct sockaddr *)&peer,sizeof(peer));
    if(result<0&&errno!=EINPROGRESS){error=errno;goto done;}
    if(result<0){
        fd_set writefds;FD_ZERO(&writefds);FD_SET(fd,&writefds);struct timeval wait={.tv_sec=2};
        if(select(fd+1,NULL,&writefds,NULL,&wait)<=0){error=ETIMEDOUT;goto done;}
        socklen_t size=sizeof(error);if(getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&size)||error){if(!error)error=errno;goto done;}
    }
    fcntl(fd,F_SETFL,0);
    inet6_ntoa_r(peer.sin6_addr,addr,sizeof(addr));
    int n=snprintf(request,sizeof(request),"GET /ctrl-int/1/connect HTTP/1.1\r\nHost: [%s]:%u\r\nUser-Agent: AirPlay/320.17\r\nAirPlay-Receiver-Device-ID: 2207900644098\r\nConnection: keep-alive\r\n\r\n",addr,port);
    for(int at=0;at<n;){int sent=send(fd,request+at,n-at,0);if(sent<=0){error=errno;goto done;}at+=sent;}
    n=recv(fd,response,sizeof(response)-1,0);
    if(n>0){response[n]=0;if(sscanf(response,"HTTP/%*u.%*u %d",&code)!=1)error=EPROTO;}else error=errno?errno:ECONNRESET;
done:
    close(fd);
    xSemaphoreTake(probe_lock,portMAX_DELAY);probe_status.receiver_connects++;probe_status.receiver_http=code;xSemaphoreGive(probe_lock);
    state(error?"CONNECT FAILED":code==200?"CONNECT HTTP 200":"CONNECT RESPONSE",error);
}
