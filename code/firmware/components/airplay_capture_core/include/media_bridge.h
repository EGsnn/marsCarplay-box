#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
esp_err_t media_bridge_set_h264_config(const uint8_t *p,size_t n);
esp_err_t media_bridge_send_h264(const uint8_t *p,size_t n,uint64_t sender,uint64_t arrival);
void media_bridge_record_raw(const uint8_t header[128],const uint8_t *p,size_t n);
bool media_bridge_capture_init(void);
/* Freeze before exposing immutable RAM data to an HTTP download. */
const uint8_t *media_bridge_capture_freeze(bool raw,size_t *size);
size_t media_bridge_capture_status(char *out,size_t capacity);
unsigned media_bridge_capture_frames(void);
