#include "box_pixels.h"
#include <string.h>
box_rect_t box_fit(unsigned sw,unsigned sh,unsigned dw,unsigned dh){
 box_rect_t r={0};if(!sw||!sh||!dw||!dh)return r;
 if((uint64_t)sw*dh>(uint64_t)sh*dw){r.width=dw;r.height=(uint64_t)sh*dw/sw;}
 else{r.height=dh;r.width=(uint64_t)sw*dh/sh;}
 r.x=(dw-r.width)/2;r.y=(dh-r.height)/2;return r;
}
static unsigned clip(int n){return n<0?0:n>255?255:(unsigned)n;}
bool box_pixels_fit(const box_yuv_t *im,uint16_t *out,unsigned w,unsigned h){
 if(!im||!out||!im->y||!im->u||!im->v||!im->width||!im->height||im->width>800||im->height>480||im->width%2||im->height%2||im->y_stride<im->width||im->uv_stride<im->width/2||!w||!h||w>320||h>240)return false;
 box_rect_t r=box_fit(im->width,im->height,w,h);if(!r.width||!r.height)return false;
 /* Compute the same nearest-neighbour positions once per column, instead of
  * dividing for every displayed pixel. The map fits in the video stack. */
 uint16_t columns[320];
 for(unsigned x=0;x<r.width;x++)columns[x]=x*im->width/r.width;
 memset(out,0,w*h*2);
 for(unsigned y=0;y<r.height;y++){
  unsigned sy=y*im->height/r.height;
  const uint8_t *yr=im->y+sy*im->y_stride;
  const uint8_t *ur=im->u+(sy/2)*im->uv_stride,*vr=im->v+(sy/2)*im->uv_stride;
  uint16_t *row=out+(r.y+y)*w+r.x;
  for(unsigned x=0;x<r.width;x++){
   unsigned sx=columns[x];
   int c=((int)yr[sx]-16)*298;
   int d=(int)ur[sx/2]-128,e=(int)vr[sx/2]-128;
   unsigned red=clip((c+409*e+128)>>8),green=clip((c-100*d-208*e+128)>>8),blue=clip((c+516*d+128)>>8);
   uint16_t rgb=(red>>3)<<11|(green>>2)<<5|(blue>>3);row[x]=(rgb>>8)|(rgb<<8);
  }
 }
 return true;
}
bool box_touch_map(box_rect_t r,unsigned sw,unsigned sh,unsigned x,unsigned y,uint16_t *ox,uint16_t *oy){
 if(!r.width||!r.height||!sw||!sh||!ox||!oy||x<r.x||y<r.y||x-r.x>=r.width||y-r.y>=r.height)return false;
 *ox=(uint64_t)(x-r.x)*sw/r.width;*oy=(uint64_t)(y-r.y)*sh/r.height;return true;
}
