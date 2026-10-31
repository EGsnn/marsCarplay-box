#pragma once
#include <stddef.h>
#include <stdint.h>

/* Caller serializes access. Timing is wall time, not CPU or end-to-end latency. */
typedef struct {
    uint64_t started_us, sampled_us, window_us;
    uint32_t received, decoded, shown;
    double rx_fps, decode_fps, display_fps;
    uint64_t attempts, outputs, decode_us, process_us, convert_us, present_us;
    uint64_t max_decode_us, max_process_us, max_convert_us, max_present_us;
} box_perf_t;

void box_perf_init(box_perf_t *, uint64_t now_us, uint32_t received);
void box_perf_decode(box_perf_t *, uint64_t decode_us, uint64_t process_us);
void box_perf_output(box_perf_t *, uint64_t convert_us, uint64_t present_us);
void box_perf_sample(box_perf_t *, uint64_t now_us, uint32_t received,
                     uint32_t decoded, uint32_t shown);
size_t box_perf_json(const box_perf_t *, char *, size_t capacity);
