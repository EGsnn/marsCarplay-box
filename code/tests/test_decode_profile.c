#include "box_decode_profile.h"
#include <assert.h>
#include <stdio.h>
int main(void){
 box_decode_profile_totals_t t={0};box_decoder_profile_t p={0};char out[1024];
 box_decode_profile_record(&t,&p);assert(!t.samples);
 p.valid=true;p.is_idr=true;p.decode_us=21000;p.timer_reads=200;
 for(unsigned i=0;i<6;i++)p.stage_us[i]=(i+1)*1000;
 box_decode_profile_record(&t,&p);p.is_idr=false;p.decode_us=42000;
 for(unsigned i=0;i<6;i++)p.stage_us[i]*=2;
 box_decode_profile_record(&t,&p);
 assert(t.samples==2&&t.idr_samples==1&&t.p_samples==1&&t.max_decode_us==42000);
 assert(box_decode_profile_json(&t,out,sizeof(out))<sizeof(out));puts(out);
 char tiny[2];assert(box_decode_profile_json(&t,tiny,sizeof(tiny))>=sizeof(tiny));
 box_decode_profile_totals_t empty={0};box_decode_profile_json(&empty,out,sizeof(out));puts(out);
 return 0;
}
