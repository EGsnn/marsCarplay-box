#include "box_decoder.h"
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#define SUB0H264_COOPERATE(mb) do{if((mb)%100U==0)vTaskDelay(1);}while(0)
#endif
#include "decoder_profile.hpp"
#define SUB0H264_PROFILE_SCOPE(stage) BoxProfileScope box_profile_scope(BoxStage::stage)
#include "decoder.hpp"
#include <new>
#include <array>
using namespace sub0h264;
struct box_decoder {
 H264Decoder decoder;
 BoxProfiler profile;
 bool allocated=false;
 unsigned width=0,height=0,refs=0;
 const char *error="";
};
extern "C" box_decoder_t *box_decoder_create(){return new(std::nothrow) box_decoder;}
extern "C" void box_decoder_destroy(box_decoder_t *h){delete h;}
extern "C" const char *box_decoder_error(const box_decoder_t *h){return h?h->error:"DECODER MEMORY";}
extern "C" bool box_decoder_profile(const box_decoder_t *h,box_decoder_profile_t *out){
 if(!h||!out)return false;
 *out=h->profile.data;return out->valid;
}
extern "C" int box_decoder_feed(box_decoder_t *h,const uint8_t *data,size_t size,box_frame_cb callback,void *ctx){
 if(h)h->profile.data={};
 if(!h||!data||!size||size>192U*1024U)return -1;
 std::vector<NalBounds> nals;findNalUnits(data,size,nals);
 if(nals.empty()||nals.size()>16){h->error="NAL BOUNDS";return -1;}
 bool slice=false,idr=false;
 for(const auto &b:nals){
  if(!b.size||b.offset>=size||b.size>size-b.offset){h->error="NAL BOUNDS";return -1;}
  auto type=data[b.offset]&31U;slice|=(type==1||type==5);idr|=(type==5);
 }
 static uint32_t sequence=0;
 uint32_t index=sequence;if(slice)++sequence;
 BoxProfileFrame profile(h->profile,slice&&index%BOX_DECODE_PROFILE_INTERVAL==0,index,idr);
 // Reject unsupported AU layouts before changing any reference pictures.
 unsigned slices=0;
 std::array<NalUnit,16> parsed;unsigned parsed_count=0;
 for(const auto &b:nals){
  NalUnit &nal=parsed[parsed_count++];
  if(!parseNalUnit(data+b.offset,b.size,nal)){h->error="NAL PARSE";return -1;}
  if(isSliceNal(nal.type)){
   BitReader bits(nal.rbspData.data(),nal.rbspData.size());
   auto first=bits.readUev(),type=bits.readUev()%5;
   if(first||++slices>1||(type!=0&&type!=2)){h->error="SLICE UNSUPPORTED";return -1;}
  }
 }
 int count=0;
 for(unsigned i=0;i<parsed_count;i++){
  const NalUnit &nal=parsed[i];
  if(nal.type==NalType::Sps){
   Sps s;BitReader bits(nal.rbspData.data(),nal.rbspData.size());
   if(parseSps(bits,s)!=Result::Ok||!s.width()||!s.height()||s.frameCropping_||s.bitDepthLuma_!=8||s.bitDepthChroma_!=8||s.chromaFormatIdc_!=1){h->error="SPS UNSUPPORTED";return -1;}
   h->width=s.width();h->height=s.height();h->refs=s.numRefFrames_;
  }
#ifdef ESP_PLATFORM
  if(isSliceNal(nal.type)&&!h->allocated){
   size_t pixels=h->width*h->height;
   size_t estimate=(h->refs+2)*pixels*3/2+(pixels/256)*600+128*1024;
   if(!pixels||heap_caps_get_free_size(MALLOC_CAP_SPIRAM)<estimate+256*1024||heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)<pixels){h->error="PSRAM BUDGET";return -1;}
  }
#endif
  auto status=h->decoder.processNal(nal);
  if(status==DecodeStatus::Error){h->error="DECODE REJECTED";return -1;}
  if(status==DecodeStatus::FrameDecoded){
   const Frame *f=h->decoder.currentFrame();if(!f){h->error="NO DECODED FRAME";return -1;}
   box_yuv_t im={f->yData(),f->uData(),f->vData(),f->width(),f->height(),f->yStride(),f->uvStride()};
   h->allocated=true;++count;if(callback){BoxProfilePause pause;callback(&im,ctx);}
  }
 }
 h->error="";return count;
}
