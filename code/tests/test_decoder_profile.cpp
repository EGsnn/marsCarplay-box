#define BOX_PROFILE_FAKE_CLOCK
#include "decoder_profile.hpp"
#include <cassert>
#include <cstdio>
uint32_t box_profile_fake_tick=UINT32_MAX-10;
int main(){
 BoxProfiler p;
 {BoxProfileFrame f(p,true,32,false);
  box_profile_fake_tick+=4;
  {BoxProfileScope motion(BoxStage::MOTION);box_profile_fake_tick+=7;
   {BoxProfileScope transform(BoxStage::TRANSFORM);box_profile_fake_tick+=5;}
   box_profile_fake_tick+=2;}
  box_profile_fake_tick+=3;
  {BoxProfilePause pause;box_profile_fake_tick+=1000;assert(!box_current_profiler);}
  box_profile_fake_tick+=6;
  {BoxProfileScope cabac(BoxStage::CABAC_BIN);box_profile_fake_tick+=8;}
  box_profile_fake_tick+=2;
 }
 assert(!box_current_profiler&&p.data.valid&&p.data.sample_index==32);
 assert(p.data.decode_us==37&&p.data.stage_us[0]==15);
 assert(p.data.stage_us[1]==8&&p.data.stage_us[3]==9&&p.data.stage_us[4]==5);
 assert(p.data.timer_reads==10);
 {BoxProfileFrame f(p,false,33,false);BoxProfileScope scope(BoxStage::INTRA);box_profile_fake_tick+=10;}
 assert(!p.data.valid&&!p.data.timer_reads&&!box_current_profiler);
 puts("Profile exclusive nesting, tick wrap, callback exclusion and inactive path passed");
}
