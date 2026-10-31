#pragma once
#include "box_decoder.h"
#include <cstdint>
#include <cstring>
#if defined(BOX_PROFILE_FAKE_CLOCK)
extern uint32_t box_profile_fake_tick;
inline uint32_t box_profile_tick(){return box_profile_fake_tick;}
inline constexpr uint32_t box_profile_ticks_per_us=1;
#elif defined(ESP_PLATFORM)
#include "esp_cpu.h"
#include "sdkconfig.h"
inline uint32_t box_profile_tick(){return esp_cpu_get_cycle_count();}
inline constexpr uint32_t box_profile_ticks_per_us=CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
#else
#include <chrono>
inline uint32_t box_profile_tick(){
 return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
  std::chrono::steady_clock::now().time_since_epoch()).count());
}
inline constexpr uint32_t box_profile_ticks_per_us=1000;
#endif

/* One pinned decode owner, matching the decoder's ownership contract.
 * Tick differences include preemption/yields. Nested scopes charge exclusive
 * time, so transforms inside predictions are not counted twice. */
enum class BoxStage:unsigned {OTHER=0,CABAC_BIN,INTRA,MOTION,TRANSFORM,DEBLOCK};
class BoxProfiler;
inline BoxProfiler *box_current_profiler=nullptr;
class BoxProfiler {
 uint64_t ticks[BOX_DECODE_PROFILE_STAGES]{};
 uint32_t last=0;
 BoxStage current=BoxStage::OTHER;
 uint32_t now(){++data.timer_reads;return box_profile_tick();}
 void charge(){uint32_t at=now();ticks[static_cast<unsigned>(current)]+=uint32_t(at-last);last=at;}
public:
 box_decoder_profile_t data{};
 void begin(unsigned index,bool idr){
  data={};std::memset(ticks,0,sizeof(ticks));data.sample_index=index;data.is_idr=idr;
  current=BoxStage::OTHER;last=now();box_current_profiler=this;
 }
 BoxStage enter(BoxStage next){charge();auto old=current;current=next;return old;}
 void leave(BoxStage old){charge();current=old;}
 void pause(){charge();box_current_profiler=nullptr;}
 void resume(){last=now();box_current_profiler=this;}
 void finish(){
  charge();box_current_profiler=nullptr;uint64_t total=0,sum=0;
  for(unsigned i=0;i<BOX_DECODE_PROFILE_STAGES;i++){
   total+=ticks[i];data.stage_us[i]=ticks[i]/box_profile_ticks_per_us;sum+=data.stage_us[i];
  }
  data.decode_us=total/box_profile_ticks_per_us;
  data.stage_us[0]+=data.decode_us-sum;data.valid=true;
 }
};
class BoxProfileScope {
 BoxProfiler *p=box_current_profiler;
 BoxStage previous=BoxStage::OTHER;
public:
 explicit BoxProfileScope(BoxStage stage){if(p)previous=p->enter(stage);}
 ~BoxProfileScope(){if(p)p->leave(previous);}
 BoxProfileScope(const BoxProfileScope&)=delete;
};
class BoxProfilePause {
 BoxProfiler *p=box_current_profiler;
public:
 BoxProfilePause(){if(p)p->pause();}
 ~BoxProfilePause(){if(p)p->resume();}
 BoxProfilePause(const BoxProfilePause&)=delete;
};
class BoxProfileFrame {
 BoxProfiler &p;bool selected;
public:
 BoxProfileFrame(BoxProfiler &profile,bool sample,unsigned index,bool idr):p(profile),selected(sample){
  p.data={};if(selected)p.begin(index,idr);
 }
 ~BoxProfileFrame(){if(selected)p.finish();}
 BoxProfileFrame(const BoxProfileFrame&)=delete;
};
