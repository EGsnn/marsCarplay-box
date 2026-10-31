#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "box_decoder.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {unsigned x,y,width,height;} box_rect_t;
box_rect_t box_fit(unsigned sw,unsigned sh,unsigned dw,unsigned dh);
/* RGB565 in panel byte order; complete canvas, including black bars. */
bool box_pixels_fit(const box_yuv_t *,uint16_t *,unsigned,unsigned);
bool box_touch_map(box_rect_t,unsigned sw,unsigned sh,unsigned x,unsigned y,uint16_t *,uint16_t *);

#ifdef __cplusplus
}
#endif
