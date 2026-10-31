#include "decoder.hpp"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
using namespace sub0h264;
int main(int argc,char **argv){
 if(argc<2)return 2;
 std::ifstream f(argv[1],std::ios::binary);if(!f)return 3;
 std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),{});
 auto decoder=std::make_unique<H264Decoder>();
 std::vector<NalBounds> bounds;findNalUnits(data.data(),data.size(),bounds);
 unsigned frames=0,errors=0;auto start=std::chrono::steady_clock::now();
 std::ofstream raw;if(argc>2)raw.open(argv[2],std::ios::binary);
 for(const auto &b:bounds){
  NalUnit n;if(!parseNalUnit(data.data()+b.offset,b.size,n)){++errors;continue;}
  auto status=decoder->processNal(n);
  if(n.type==NalType::Sps){auto s=decoder->paramSets().getSps(0);if(s)std::fprintf(stderr,"SPS profile=%u level=%u width=%u height=%u refs=%u depth=%d chroma=%d\n",s->profileIdc_,s->levelIdc_,s->width(),s->height(),s->numRefFrames_,s->bitDepthLuma_,s->chromaFormatIdc_);}
  if(n.type==NalType::Pps){auto p=decoder->paramSets().getPps(0);if(p)std::fprintf(stderr,"PPS cabac=%u transform8=%u weighted=%u\n",p->entropyCodingMode_,p->transform8x8Mode_,p->weightedPredFlag_);}
  if(status==DecodeStatus::Error){std::fprintf(stderr,"Decode error nal=%u at frame %u\n",unsigned(n.type),frames);++errors;break;}
  if(status==DecodeStatus::FrameDecoded){++frames;auto im=decoder->currentFrame();if(raw){raw.write((const char*)im->yData(),im->width()*im->height());raw.write((const char*)im->uData(),im->width()*im->height()/4);raw.write((const char*)im->vData(),im->width()*im->height()/4);}}
 }
 double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
 std::printf("{\"frames\":%u,\"errors\":%u,\"nals\":%zu,\"decoder_object_bytes\":%zu,\"host_ms\":%.3f}\n",frames,errors,bounds.size(),sizeof(H264Decoder),ms);
 return errors?1:0;
}
