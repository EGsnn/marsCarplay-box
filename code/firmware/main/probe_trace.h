#pragma once
#include <stdint.h>
#include <stddef.h>
/* Numeric diagnostics only: no request body, keys, or phone identifiers. */
#define PROBE_TRACE_LIMIT 16
typedef struct {uint32_t ms,bytes;int32_t detail;uint16_t event,code;} probe_trace_entry_t;
typedef struct {unsigned count;probe_trace_entry_t entries[PROBE_TRACE_LIMIT];} probe_trace_t;
void probe_trace_add(probe_trace_t *t,unsigned event,unsigned code,unsigned bytes,int detail,uint32_t ms);
unsigned probe_trace_route(const char *method,const char *path);
unsigned probe_trace_response_code(const uint8_t *response,size_t size);
/* -1 absent, -2 malformed/duplicate; valid values are 0..INT32_MAX. */
int probe_trace_response_cseq(const uint8_t *response,size_t size);
size_t probe_trace_json(const probe_trace_t *t,char *out,size_t capacity);
