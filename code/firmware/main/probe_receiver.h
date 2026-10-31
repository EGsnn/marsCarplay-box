#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
void probe_receiver_start(void);
bool probe_receiver_ready(void);
const char *probe_receiver_public_hex(void);
void probe_receiver_connect(const uint8_t ip[16],uint16_t port);
void probe_receiver_event_rx_note(const uint8_t *plaintext,size_t len,
                                  unsigned reason,int error_code);
void probe_receiver_audio_note(unsigned bytes,unsigned packets);
