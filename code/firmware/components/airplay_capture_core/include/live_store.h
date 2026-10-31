/* Bounded, non-blocking-to-network H.264 preview queue. No keys or raw USB data. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifndef LIVE_ARENA_BYTES
#define LIVE_ARENA_BYTES (384U*1024U)
#endif
#ifndef LIVE_MAX_PAYLOAD
#define LIVE_MAX_PAYLOAD (192U*1024U)
#endif
#define LIVE_HEADER_BYTES 36U
#define LIVE_CONFIG_BYTES 4096U
bool live_store_init(void);
void live_store_config(const uint8_t *data,size_t len);
/* Exact repeated nonempty configuration preserves epoch/queue; NULL/empty or
 * invalid size still clears it, including when reconnecting the same phone. */
void live_store_frame(const uint8_t *data,size_t len,bool idr,uint64_t sender_ns,uint64_t arrival_us);
/* Stateless per-reader cursor: epoch mismatch returns config(seq=0), then AUs.
 * Return wire bytes, 0 when no data, -1 for invalid output capacity. */
int live_store_copy(uint32_t epoch,uint32_t after,uint8_t *out,size_t capacity);
/* Complete concatenated packets, at most 8. No network I/O or new allocation. */
int live_store_copy_batch(uint32_t epoch,uint32_t after,uint8_t *out,size_t capacity,unsigned max_packets);
size_t live_store_status(char *out,size_t capacity);

uint32_t live_store_epoch(void);
