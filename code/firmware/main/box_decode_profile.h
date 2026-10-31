#pragma once
#include "box_decoder.h"
typedef struct {
 uint64_t stage_us[BOX_DECODE_PROFILE_STAGES],decode_us,max_decode_us,timer_reads;
 unsigned samples,idr_samples,p_samples;
} box_decode_profile_totals_t;
void box_decode_profile_record(box_decode_profile_totals_t *,const box_decoder_profile_t *);
size_t box_decode_profile_json(const box_decode_profile_totals_t *,char *,size_t);
