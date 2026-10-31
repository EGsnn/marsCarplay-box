#pragma once
#include "probe.h"
#include <stddef.h>
#include <stdbool.h>
void box_direct_start(void);
void box_direct_sample(void);
bool box_direct_video_available(void);
size_t box_direct_status_json(char *,size_t);
