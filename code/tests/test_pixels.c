#include "box_pixels.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void){
 box_rect_t r=box_fit(800,480,320,240);assert(r.x==0&&r.y==24&&r.width==320&&r.height==192);
 uint16_t x=0,y=0;assert(!box_touch_map(r,800,480,0,23,&x,&y));assert(!box_touch_map(r,800,480,320,24,&x,&y));
 assert(box_touch_map(r,800,480,0,24,&x,&y)&&x==0&&y==0);
 assert(box_touch_map(r,800,480,319,215,&x,&y)&&x==797&&y==477);
 r=box_fit(480,800,320,240);assert(r.x==88&&r.y==0&&r.width==144&&r.height==240);
 uint8_t luma[24]={81,81,145,145,41,41,235,235,0,0,0,0,81,81,145,145,41,41,235,235,0,0,0,0};
 uint8_t u[6]={90,54,240,128,0,0},v[6]={240,34,110,128,0,0};
 box_yuv_t im={luma,u,v,8,2,12,6};uint16_t *out=malloc(320*240*2);assert(out);memset(out,0xff,320*240*2);
 assert(box_pixels_fit(&im,out,320,240));
 assert(out[0]==0&&out[239*320]==0);
 unsigned px[]={10,90,170,250};uint16_t expected[]={0xf800,0x07e0,0x001f,0xffff};
 for(unsigned i=0;i<4;i++){uint16_t wire=out[100*320+px[i]];uint16_t cpu=(wire>>8)|(wire<<8);unsigned a=cpu,b=expected[i];assert(abs((int)((a>>11)&31)-(int)((b>>11)&31))<=1);assert(abs((int)((a>>5)&63)-(int)((b>>5)&63))<=1);assert(abs((int)(a&31)-(int)(b&31))<=1);}
 assert(!box_pixels_fit(NULL,out,320,240));im.y_stride=7;assert(!box_pixels_fit(&im,out,320,240));
 free(out);puts("RGB565 panel order, YUV strides, letterboxing and touch inverse mapping passed");
}
