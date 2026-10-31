#include "live_store.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned be(const uint8_t *p){return ((unsigned)p[0]<<24)|((unsigned)p[1]<<16)|((unsigned)p[2]<<8)|p[3];}
int main(void){
 assert(live_store_init());uint8_t config[]={0,0,0,1,0x67,100,0,31};
 uint8_t frame[]={0,0,0,1,0x65,0x80},out[4096];char status[512];
 live_store_config(config,sizeof(config));unsigned epoch=live_store_epoch();
 assert(live_store_copy(0,0,out,sizeof(out))==LIVE_HEADER_BYTES+sizeof(config)&&out[4]==1);
 live_store_frame(frame,sizeof(frame),true,1,2);
 for(unsigned i=0;i<20;i++)live_store_config(config,sizeof(config));
 assert(live_store_epoch()==epoch);
 assert(live_store_copy(epoch,0,out,sizeof(out))==LIVE_HEADER_BYTES+sizeof(frame));
 assert(out[4]==2&&(out[5]&1)&&!(out[5]&2)&&be(out+12)==1);
 assert(!memcmp(out+LIVE_HEADER_BYTES,frame,sizeof(frame)));
 assert(live_store_status(status,sizeof(status))<sizeof(status));
 assert(strstr(status,"\"config_changes\":1")&&strstr(status,"\"config_duplicates\":20"));
 /* A real parameter change discards old AUs and forces config delivery. */
 config[7]=30;live_store_config(config,sizeof(config));unsigned changed=live_store_epoch();
 assert(changed!=epoch&&live_store_copy(changed,0,out,sizeof(out))==0);
 assert(live_store_copy(epoch,1,out,sizeof(out))>0&&out[4]==1&&out[LIVE_HEADER_BYTES+7]==30);
 live_store_frame(frame,sizeof(frame),true,3,4);
 /* A new connection must clear even when its next SPS/PPS will be identical. */
 live_store_config(NULL,0);assert(live_store_epoch()!=changed);
 assert(live_store_copy(0,0,out,sizeof(out))==0);
 live_store_config(config,sizeof(config));assert(live_store_epoch()!=changed);
 assert(live_store_copy(live_store_epoch(),0,out,sizeof(out))==0);
 /* Invalid lengths must never be mistaken for a duplicate or read past data. */
 live_store_config(config,LIVE_CONFIG_BYTES+1);assert(live_store_copy(0,0,out,sizeof(out))==0);
 live_store_config(config,sizeof(config));unsigned fresh=live_store_epoch();
 live_store_frame(frame,sizeof(frame),true,5,6);
 uint8_t oversized[LIVE_MAX_PAYLOAD+1];memset(oversized,0,sizeof(oversized));
 live_store_frame(oversized,sizeof(oversized),false,7,8);live_store_frame(frame,sizeof(frame),false,9,10);
 assert(live_store_copy(fresh,1,out,sizeof(out))>0&&be(out+12)==3&&(out[5]&2));
 puts("Duplicate SPS/PPS preserve epoch/AUs; changed config, reconnect, invalid size and gaps remain explicit");
}
