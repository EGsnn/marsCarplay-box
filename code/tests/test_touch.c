#include "airplay_touch_report.h"
#include "airplay_display_config.h"
#include "airplay_hid_descriptor.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>
int main(void){
 uint8_t report[12],command[5]={1,0x3f,1,0xbf,0};
 assert(AIRPLAY_DISPLAY_WIDTH==320&&AIRPLAY_DISPLAY_HEIGHT==192);
 assert(airplay_touch_report(command,sizeof(command),report));
 assert(report[1]==1&&report[2]==0x3f&&report[3]==1&&report[4]==0xbf&&report[5]==0&&report[6]==1);
 command[1]=0x41;assert(!airplay_touch_report(command,sizeof(command),report));
 command[1]=0;command[2]=0;command[3]=0xc1;assert(!airplay_touch_report(command,sizeof(command),report));
 uint8_t descriptor[109];size_t size=0;
 assert(airplay_hid_touch_descriptor_build(320,192,descriptor,sizeof(descriptor),&size)&&size==109);
 /* Both contacts must advertise the same logical coordinate limits. */
 const uint8_t x[]={0x26,0x40,1,9,0x30},y[]={0x26,0xc0,0,9,0x31};
 unsigned nx=0,ny=0;
 for(size_t i=0;i+5<=size;i++){nx+=memcmp(descriptor+i,x,5)==0;ny+=memcmp(descriptor+i,y,5)==0;}
 assert(nx==2&&ny==2);
}
