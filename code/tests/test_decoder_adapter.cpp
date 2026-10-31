#include "box_decoder.h"
#include "box_pixels.h"
#include "annexb.hpp"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <memory>
#include <vector>
static unsigned frames,profiles;
static uint16_t pixels[320*240];
static std::ofstream all_frames;
static void output(const box_yuv_t *im,void *){
 if(im->width!=800||im->height!=480||!box_pixels_fit(im,pixels,320,240))std::abort();
 for(unsigned y=0;y<24;y++)for(unsigned x=0;x<320;x++)if(pixels[y*320+x])std::abort();
 ++frames;
 if(all_frames)all_frames.write((const char *)pixels,sizeof(pixels));
}
int main(int argc,char **argv){
 if(argc<2)return 2;
 if(argc>3){all_frames.open(argv[3],std::ios::binary);if(!all_frames)return 10;}
 std::ifstream f(argv[1],std::ios::binary);if(!f)return 3;
 std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),{});
 auto *d=box_decoder_create();if(!d)return 4;
 if(box_decoder_feed(d,nullptr,0,output,nullptr)>=0)return 5;
 std::vector<sub0h264::NalBounds> nals;sub0h264::findNalUnits(data.data(),data.size(),nals);
 for(const auto &n:nals){
  std::vector<uint8_t> packet={0,0,0,1};packet.insert(packet.end(),data.begin()+n.offset,data.begin()+n.offset+n.size);
  int count=box_decoder_feed(d,packet.data(),packet.size(),output,nullptr);
  box_decoder_profile_t profile{};
  if(box_decoder_profile(d,&profile)){
   uint64_t sum=0;for(auto us:profile.stage_us)sum+=us;
   if(!profile.valid||sum!=profile.decode_us||!profile.timer_reads||profile.sample_index%BOX_DECODE_PROFILE_INTERVAL)return 11;
   if(count>0)++profiles;
  }
  if(count<0){std::fprintf(stderr,"Adapter error: %s\n",box_decoder_error(d));return 6;}
 }
 if(frames!=124||profiles!=4)return 7;
 std::ofstream image;if(argc>2){image.open(argv[2],std::ios::binary);image.write((const char *)pixels,sizeof(pixels));}
 box_decoder_destroy(d);d=box_decoder_create();if(!d)return 8;
 uint8_t b_slice[]={0,0,0,1,0x41,0xa0};
 if(box_decoder_feed(d,b_slice,sizeof(b_slice),output,nullptr)>=0)return 9;
 box_decoder_destroy(d);
 std::printf("Production decoder adapter and RGB path: %u real frames; four sampled profiles; reset/unsupported-slice guards passed\n",frames);
}
