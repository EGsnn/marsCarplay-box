#include "box_perf.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

void box_perf_init(box_perf_t *p, uint64_t now, uint32_t received)
{
    memset(p, 0, sizeof(*p));
    p->started_us = p->sampled_us = now;
    p->received = received;
}

void box_perf_decode(box_perf_t *p, uint64_t decode, uint64_t process)
{
    ++p->attempts;
    p->decode_us += decode; p->process_us += process;
    if (decode > p->max_decode_us) p->max_decode_us = decode;
    if (process > p->max_process_us) p->max_process_us = process;
}

void box_perf_output(box_perf_t *p, uint64_t convert, uint64_t present)
{
    ++p->outputs;
    p->convert_us += convert; p->present_us += present;
    if (convert > p->max_convert_us) p->max_convert_us = convert;
    if (present > p->max_present_us) p->max_present_us = present;
}

void box_perf_sample(box_perf_t *p, uint64_t now, uint32_t received,
                     uint32_t decoded, uint32_t shown)
{
    if (now <= p->sampled_us || now - p->sampled_us < 1000000) return;
    p->window_us = now - p->sampled_us;
    /* Unsigned deltas retain valid counts across a uint32 counter wrap. */
    p->rx_fps = (uint32_t)(received - p->received) * 1000000.0 / p->window_us;
    p->decode_fps = (uint32_t)(decoded - p->decoded) * 1000000.0 / p->window_us;
    p->display_fps = (uint32_t)(shown - p->shown) * 1000000.0 / p->window_us;
    p->received = received; p->decoded = decoded; p->shown = shown;
    p->sampled_us = now;
}

size_t box_perf_json(const box_perf_t *p, char *out, size_t capacity)
{
    double decode_n = p->attempts ? p->attempts * 1000.0 : 1.0;
    double output_n = p->outputs ? p->outputs * 1000.0 : 1.0;
    int n = snprintf(out, capacity,
        "{\"window_ms\":%.2f,\"sampled_uptime_ms\":%.2f,"
        "\"rx_fps\":%.2f,\"decode_fps\":%.2f,\"display_fps\":%.2f,"
        "\"decode_attempts\":%" PRIu64 ",\"output_attempts\":%" PRIu64 ","
        "\"decode_avg_ms\":%.2f,\"decode_max_ms\":%.2f,"
        "\"process_avg_ms\":%.2f,\"process_max_ms\":%.2f,"
        "\"convert_avg_ms\":%.2f,\"convert_max_ms\":%.2f,"
        "\"present_avg_ms\":%.2f,\"present_max_ms\":%.2f}",
        p->window_us / 1000.0, (p->sampled_us - p->started_us) / 1000.0,
        p->rx_fps, p->decode_fps, p->display_fps, p->attempts, p->outputs,
        p->decode_us / decode_n, p->max_decode_us / 1000.0,
        p->process_us / decode_n, p->max_process_us / 1000.0,
        p->convert_us / output_n, p->max_convert_us / 1000.0,
        p->present_us / output_n, p->max_present_us / 1000.0);
    return n < 0 ? 0 : (size_t)n;
}
