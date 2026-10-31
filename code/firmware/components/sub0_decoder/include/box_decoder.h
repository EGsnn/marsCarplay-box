#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct box_decoder box_decoder_t;
#define BOX_DECODE_PROFILE_STAGES 6U
#define BOX_DECODE_PROFILE_INTERVAL 32U
/* Exclusive sampled wall-time buckets: other, CABAC bin engine, intra
 * prediction, motion compensation, inverse quantization/transform, deblock.
 * Other includes syntax/context derivation, CAVLC and uninstrumented work.
 * Samples contain instrumentation overhead; they are NOT pure CPU time. */
typedef struct {
 uint64_t stage_us[BOX_DECODE_PROFILE_STAGES],decode_us;
 uint32_t timer_reads,sample_index;
 bool is_idr,valid;
} box_decoder_profile_t;
typedef struct {
 const uint8_t *y,*u,*v;
 unsigned width,height,y_stride,uv_stride;
} box_yuv_t;
typedef void (*box_frame_cb)(const box_yuv_t *,void *);
box_decoder_t *box_decoder_create(void);
void box_decoder_destroy(box_decoder_t *);
/* A complete Annex-B config or single-slice AU; callback data only valid in call.
 * 0=config/no picture, >0=decoded pictures, <0=explicit unsupported/error. */
int box_decoder_feed(box_decoder_t *,const uint8_t *,size_t,box_frame_cb,void *);
const char *box_decoder_error(const box_decoder_t *);
/* Read after feed, on the same decode owner; false on nonsampled calls. */
bool box_decoder_profile(const box_decoder_t *,box_decoder_profile_t *);
#ifdef __cplusplus
}
#endif
