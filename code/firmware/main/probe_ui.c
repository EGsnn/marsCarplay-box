#include <stdio.h>
#include <stdlib.h>
#include "probe.h"
#include "mars_board.h"
#include "esp_heap_caps.h"
/* Small probe-only glyphs; no font engine or shared Mars canvas owner. */
typedef struct {char c;uint8_t row[7];} glyph_t;
static const glyph_t glyphs[]={
 {'A',{14,17,17,31,17,17,17}}, {'B',{30,17,17,30,17,17,30}},
 {'C',{14,17,16,16,16,17,14}}, {'D',{30,17,17,17,17,17,30}},
 {'E',{31,16,16,30,16,16,31}}, {'F',{31,16,16,30,16,16,16}},
 {'G',{14,17,16,23,17,17,15}}, {'H',{17,17,17,31,17,17,17}},
 {'I',{14,4,4,4,4,4,14}}, {'J',{7,2,2,2,2,18,12}},
 {'K',{17,18,20,24,20,18,17}}, {'L',{16,16,16,16,16,16,31}},
 {'M',{17,27,21,21,17,17,17}}, {'N',{17,25,21,19,17,17,17}},
 {'O',{14,17,17,17,17,17,14}}, {'P',{30,17,17,30,16,16,16}},
 {'Q',{14,17,17,17,21,18,13}}, {'R',{30,17,17,30,20,18,17}},
 {'S',{15,16,16,14,1,1,30}}, {'T',{31,4,4,4,4,4,4}},
 {'U',{17,17,17,17,17,17,14}}, {'V',{17,17,17,17,17,10,4}},
 {'W',{17,17,17,21,21,21,10}}, {'X',{17,17,10,4,10,17,17}},
 {'Y',{17,17,10,4,4,4,4}}, {'Z',{31,1,2,4,8,16,31}},
 {'0',{14,17,19,21,25,17,14}}, {'1',{4,12,4,4,4,4,14}},
 {'2',{14,17,1,2,4,8,31}}, {'3',{30,1,1,14,1,1,30}},
 {'4',{2,6,10,18,31,2,2}}, {'5',{31,16,16,30,1,1,30}},
 {'6',{14,16,16,30,17,17,14}}, {'7',{31,1,2,4,8,8,8}},
 {'8',{14,17,17,14,17,17,14}}, {'9',{14,17,17,15,1,1,14}},
 {':',{0,4,4,0,4,4,0}}, {'-',{0,0,0,31,0,0,0}},
};
static uint16_t *canvas;
static void text(int y,const char *s,uint16_t color)
{
    int x=8;
    for(;*s&&x+10<320;s++,x+=12) for(unsigned n=0;n<sizeof(glyphs)/sizeof(glyphs[0]);n++)
        if(glyphs[n].c==*s) for(int row=0;row<7;row++) for(int col=0;col<5;col++)
            if(glyphs[n].row[row]&(1<<(4-col))) for(int dy=0;dy<2;dy++)for(int dx=0;dx<2;dx++)
                canvas[(y+row*2+dy)*320+x+col*2+dx]=__builtin_bswap16(color);
}
void probe_ui_draw_idle(void)
{
    if(!canvas)canvas=heap_caps_malloc(320*240*sizeof(uint16_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!canvas)abort();
    for(unsigned i=0;i<320*240;i++)canvas[i]=0;
    text(90,"ATK BOX CARPLAY",0xffff);
    text(120,"CONNECT IPHONE",0x07ff);
    ESP_ERROR_CHECK(mars_board_blit(0,0,320,240,canvas));
    // blit waits for DMA; this temporary idle canvas can now be released.
    heap_caps_free(canvas);canvas=NULL;
}
