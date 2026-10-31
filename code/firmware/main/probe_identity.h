#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
bool probe_identity_build(uint8_t *out,size_t capacity,size_t *length);
#define PROBE_REJECTION_LIMIT 16
typedef struct {
    bool present,malformed,truncated;
    size_t bytes,field_count,sent_count,received_count;
    uint16_t fields[PROBE_REJECTION_LIMIT],sizes[PROBE_REJECTION_LIMIT];
    uint16_t sent[PROBE_REJECTION_LIMIT],received[PROBE_REJECTION_LIMIT];
} probe_identity_rejection_t;
bool probe_identity_parse_rejection(const uint8_t *body,size_t length,probe_identity_rejection_t *result);
bool probe_identity_rejection_json(const probe_identity_rejection_t *result,char *out,size_t capacity);
