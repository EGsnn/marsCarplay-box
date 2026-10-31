#pragma once
#include <stdbool.h>
#include <stddef.h>
void probe_network_prepare(void);
void probe_network_start(void);
void probe_network_mounted(bool mounted);
void probe_network_ip6(char *out,size_t capacity);
int probe_network_index(void);
bool probe_network_name(char *out,size_t capacity);
