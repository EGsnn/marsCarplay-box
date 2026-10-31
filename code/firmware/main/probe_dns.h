#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Bounded, stateless DNS decoder; peer names stay internal to discovery. */
typedef struct {
    bool response, airplay_question, control_ptr;
    uint16_t control_port;
    char instance[256], target[256];
} probe_dns_result_t;
bool probe_dns_parse(const uint8_t *packet, size_t size, probe_dns_result_t *out);
size_t probe_dns_query(const char *name, uint16_t type, uint8_t *out, size_t capacity);
bool probe_dns_address(const uint8_t *packet,size_t size,const char *host,uint8_t ip6[16]);
size_t probe_dns_announce(const uint8_t ip6[16],const char *public_hex,uint8_t *out,size_t capacity);
