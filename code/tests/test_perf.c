#include "box_perf.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>

static int near(double a, double b) { return fabs(a - b) < 0.001; }
int main(void)
{
    box_perf_t p;
    box_perf_init(&p, 5000000, 0);
    box_perf_decode(&p, 200000, 220000);
    box_perf_decode(&p, 400000, 430000);
    box_perf_output(&p, 5000, 15000);
    box_perf_output(&p, 10000, 20000);
    box_perf_sample(&p, 5500000, 15, 2, 1);
    assert(p.window_us == 0);  /* no fabricated subsecond FPS */
    box_perf_sample(&p, 6250000, 30, 4, 3);
    assert(near(p.rx_fps, 24) && near(p.decode_fps, 3.2) && near(p.display_fps, 2.4));
    assert(p.attempts == 2 && p.outputs == 2 && p.decode_us == 600000);
    assert(p.process_us == 650000 && p.max_decode_us == 400000);
    char json[1024];
    assert(box_perf_json(&p, json, sizeof(json)) < sizeof(json));
    puts(json); /* parsed independently by the host checker */
    box_perf_sample(&p, 7250000, 30, 4, 3);
    assert(p.rx_fps == 0 && p.decode_fps == 0 && p.display_fps == 0);
    box_perf_init(&p, 0, UINT32_MAX - 2);
    box_perf_sample(&p, 1000000, 2, 0, 0);
    assert(near(p.rx_fps, 5));
    box_perf_init(&p, 100, 0);
    assert(box_perf_json(&p, json, sizeof(json)) < sizeof(json));
    puts(json); /* idle/zero denominator is valid JSON */
    puts("Performance windows, counter wrap, idle, averages and maxima passed");
}
