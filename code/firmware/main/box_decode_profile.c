#include "box_decode_profile.h"
#include <stdio.h>
void box_decode_profile_record(box_decode_profile_totals_t *t,const box_decoder_profile_t *p){
 if(!t||!p||!p->valid)return;
 ++t->samples;if(p->is_idr)++t->idr_samples;else ++t->p_samples;
 t->decode_us+=p->decode_us;t->timer_reads+=p->timer_reads;
 if(p->decode_us>t->max_decode_us)t->max_decode_us=p->decode_us;
 for(unsigned i=0;i<BOX_DECODE_PROFILE_STAGES;i++)t->stage_us[i]+=p->stage_us[i];
}
size_t box_decode_profile_json(const box_decode_profile_totals_t *t,char *out,size_t cap){
 if(!t||!out||!cap)return 0;
 double scale=t->samples?1.0/(1000.0*t->samples):0;
 int n=snprintf(out,cap,"{\"sample_every\":%u,\"samples\":%u,\"idr_samples\":%u,\"p_samples\":%u,\"decode_avg_ms\":%.3f,\"decode_max_ms\":%.3f,\"timer_reads\":%llu,\"stage_avg_ms\":{\"other\":%.3f,\"cabac_bins\":%.3f,\"intra_prediction\":%.3f,\"motion_compensation\":%.3f,\"inverse_transform\":%.3f,\"deblock\":%.3f}}",
 BOX_DECODE_PROFILE_INTERVAL,t->samples,t->idr_samples,t->p_samples,t->decode_us*scale,
 t->max_decode_us/1000.0,(unsigned long long)t->timer_reads,t->stage_us[0]*scale,
 t->stage_us[1]*scale,t->stage_us[2]*scale,t->stage_us[3]*scale,t->stage_us[4]*scale,t->stage_us[5]*scale);
 return n<0?0:(size_t)n;
}
