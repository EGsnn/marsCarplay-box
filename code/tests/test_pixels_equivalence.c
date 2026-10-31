/* Frozen 0.1.3 scalar conversion is the oracle for the indexed implementation. */
#include "box_pixels.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned clip(int n){return n<0?0:n>255?255:(unsigned)n;}
static void reference(const box_yuv_t *im,uint16_t *out,unsigned w,unsigned h){
 box_rect_t r=box_fit(im->width,im->height,w,h);memset(out,0,w*h*2);
 for(unsigned y=0;y<r.height;y++)for(unsigned x=0;x<r.width;x++){
  unsigned sy=y*im->height/r.height,sx=x*im->width/r.width;
  int c=((int)im->y[sy*im->y_stride+sx]-16)*298;
  int d=(int)im->u[(sy/2)*im->uv_stride+sx/2]-128,e=(int)im->v[(sy/2)*im->uv_stride+sx/2]-128;
  unsigned red=clip((c+409*e+128)>>8),green=clip((c-100*d-208*e+128)>>8),blue=clip((c+516*d+128)>>8);
  uint16_t rgb=(red>>3)<<11|(green>>2)<<5|(blue>>3);out[(r.y+y)*w+r.x+x]=(rgb>>8)|(rgb<<8);
 }
}
static uint32_t seed=0x3b1b4821;
static unsigned next(void){seed=seed*1664525U+1013904223U;return seed;}
int main(void){
 uint16_t *a=malloc(320*240*2),*b=malloc(320*240*2);assert(a&&b);
 unsigned tested=0;
 for(unsigned i=0;i<180;i++){
  unsigned sw=2+2*(next()%400),sh=2+2*(next()%240),w=1+next()%320,h=1+next()%240;
  if(i==0){sw=320;sh=192;w=320;h=240;}
  if(i==1){sw=800;sh=480;w=320;h=240;}
  if(i==2){sw=128;sh=96;w=320;h=240;}
  box_rect_t r=box_fit(sw,sh,w,h);if(!r.width||!r.height)continue;
  unsigned ys=sw+next()%17,uvs=sw/2+next()%13;
  size_t yn=ys*sh,uvn=uvs*(sh/2);uint8_t *planes=malloc(yn+2*uvn);assert(planes);
  for(size_t k=0;k<yn+2*uvn;k++)planes[k]=(uint8_t)(next()>>24);
  box_yuv_t im={planes,planes+yn,planes+yn+uvn,sw,sh,ys,uvs};
  memset(a,0xa5,w*h*2);memset(b,0x5a,w*h*2);reference(&im,a,w,h);
  assert(box_pixels_fit(&im,b,w,h)&&!memcmp(a,b,w*h*2));free(planes);++tested;
 }
 free(a);free(b);printf("%u padded-stride, clipping, scaling and letterbox cases match the original RGB565 bytes\n",tested);
}
