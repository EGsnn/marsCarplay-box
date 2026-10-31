/* GPL-3.0-or-later. Host fixture around production session/crypto/decoder.
 * POSIX transport, heap and LCD are substitutes: no S3 timing is claimed. */
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "airplay_rtsp_session.h"
#include "airplay_screen.h"
#include "box_decoder.h"
#include "box_pixels.h"
#include "ed25519.h"
#include "psa/crypto.h"
#include "probe_info.h"

static airplay_rtsp_session_t *session;
static airplay_screen_stream_t *screen;
static box_decoder_t *decoder;
static uint8_t own_public[32],own_private[64],peer_public[32],peer_id[64];
static size_t peer_id_len;
static airplay_pair_verify_event_keys_t event_keys;
static uint16_t event_port,video_port,keepalive_port;
static uint16_t pixels[320*240];
static unsigned frames,decoded,errors;
static const uint8_t identity[]="host-production-fixture";

static bool random_bytes(uint8_t *p,size_t n,void *ctx)
{(void)ctx;arc4random_buf(p,n);return true;}
static bool sign_message(const uint8_t *p,size_t n,uint8_t sig[64],void *ctx)
{(void)ctx;ed25519_sign(sig,p,n,own_public,own_private);return true;}
static bool verify_message(const uint8_t key[32],const uint8_t *p,size_t n,const uint8_t sig[64],void *ctx)
{(void)ctx;return ed25519_verify(sig,p,n,key)==1;}
static bool save(const uint8_t *id,size_t n,const uint8_t key[32],void *ctx)
{(void)ctx;if(!n||n>sizeof(peer_id))return false;memcpy(peer_id,id,n);peer_id_len=n;memcpy(peer_public,key,32);return true;}
static bool lookup(const uint8_t *id,size_t n,uint8_t key[32],void *ctx)
{(void)ctx;if(n!=peer_id_len||memcmp(id,peer_id,n))return false;memcpy(key,peer_public,32);return true;}
static void output(const box_yuv_t *im,void *ctx)
{(void)ctx;if(box_pixels_fit(im,pixels,320,240))++decoded;else ++errors;}
static esp_err_t video_message(airplay_screen_message_type_t kind,const uint8_t *p,size_t n,uint64_t sender,uint64_t arrival,void *ctx)
{
 (void)sender;(void)arrival;(void)ctx;
 if(kind==AIRPLAY_SCREEN_MESSAGE_CONFIG_H264){box_decoder_destroy(decoder);decoder=box_decoder_create();}
 else ++frames;
 if(!decoder||box_decoder_feed(decoder,p,n,kind==AIRPLAY_SCREEN_MESSAGE_FRAME_H264?output:NULL,NULL)<0){++errors;return ESP_FAIL;}
 return ESP_OK;
}
void media_bridge_record_raw(const uint8_t h[128],const uint8_t *p,size_t n)
{(void)h;(void)p;(void)n;}
static void stop_screen(void *ctx)
{(void)ctx;airplay_screen_stream_destroy(screen);screen=NULL;box_decoder_destroy(decoder);decoder=NULL;}
static bool start_screen(const airplay_rtsp_pairing_t *pairing,uint64_t sid,uint16_t requested,uint16_t *port,void *ctx)
{
 (void)requested;(void)ctx;uint8_t key[32];
 if(airplay_rtsp_pairing_derive_screen_output_key(pairing,sid,key)!=AIRPLAY_PAIR_VERIFY_OK)return false;
 stop_screen(NULL);screen=airplay_screen_stream_create(key,512*1024,video_message,NULL);
 memset(key,0,sizeof(key));*port=video_port;return screen!=NULL;
}
static bool start_event(const airplay_rtsp_pairing_t *pairing,uint16_t *port,void *ctx)
{(void)ctx;*port=event_port;return airplay_rtsp_pairing_get_event_keys(pairing,&event_keys)==AIRPLAY_PAIR_VERIFY_OK;}
static bool start_timing(uint16_t peer,uint16_t *port,void *ctx)
{(void)peer;(void)ctx;*port=45000;return true;}
static bool start_keepalive(uint16_t *port,void *ctx)
{(void)ctx;*port=keepalive_port;return true;}

int fixture_init(unsigned ep,unsigned vp,unsigned kp)
{
 if(psa_crypto_init()!=0)return -1;
 event_port=ep;video_port=vp;keepalive_port=kp;
 uint8_t seed[32];arc4random_buf(seed,32);ed25519_create_keypair(own_public,own_private,seed);memset(seed,0,32);
 airplay_rtsp_session_config_t config={0};
 config.info_plist=probe_info;config.info_plist_len=sizeof(probe_info);
 config.pairing.pair_setup=(airplay_pair_setup_config_t){.pairing_code="3939",.accessory_identifier=identity,.accessory_identifier_len=sizeof(identity)-1,.accessory_public_key=own_public,.random=random_bytes,.sign_accessory=sign_message,.verify_controller=verify_message,.save_pairing=save};
 config.pairing.pair_verify=(airplay_pair_verify_config_t){.accessory_identifier=identity,.accessory_identifier_len=sizeof(identity)-1,.random=random_bytes,.sign_accessory=sign_message,.verify_controller=verify_message,.lookup_controller=lookup};
 config.start_screen=start_screen;config.stop_screen=stop_screen;
 config.start_event=start_event;config.start_timing=start_timing;config.start_keepalive=start_keepalive;
 return airplay_rtsp_session_create(&config,&session);
}
int fixture_handle(const uint8_t *request,size_t n,uint8_t *out,size_t cap,int *enable)
{
 airplay_rtsp_request_t parsed;memset(&parsed,0,sizeof(parsed));
 if(airplay_rtsp_parse_request(request,n,&parsed)!=AIRPLAY_RTSP_PARSE_COMPLETE)return -1;
 size_t written=0;bool next=false;
 if(airplay_rtsp_session_handle(session,&parsed,out,cap,&written,&next)!=AIRPLAY_RTSP_SESSION_OK)return -2;
 *enable=next;return (int)written;
}
int fixture_control_keys(uint8_t *out)
{
 airplay_pair_verify_control_keys_t keys;
 if(airplay_rtsp_session_get_control_keys(session,&keys)!=AIRPLAY_PAIR_VERIFY_OK)return -1;
 memcpy(out,keys.read_key,32);memcpy(out+32,keys.write_key,32);
 airplay_rtsp_session_mark_control_cipher_active(session);return 0;
}
void fixture_event_keys(uint8_t *out)
{memcpy(out,event_keys.read_key,32);memcpy(out+32,event_keys.write_key,32);}
int fixture_video(const uint8_t *header,const uint8_t *body,size_t n)
{return screen?airplay_screen_stream_process(screen,header,body,n,0):ESP_FAIL;}
void fixture_stats(unsigned *out){out[0]=frames;out[1]=decoded;out[2]=errors;}
void fixture_destroy(void){airplay_rtsp_session_destroy(session);session=NULL;stop_screen(NULL);}
