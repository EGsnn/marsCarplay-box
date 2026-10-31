/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct {
    unsigned kind; /* 1 modesChanged, 2 requestUI; 0 unsupported */
    bool screen_present, audio_present;
    bool screen_entity_known,audio_entity_known;
    uint32_t screen_entity, audio_entity;
} airplay_command_t;
/* 200 recognized, 400 malformed, 501 unsupported. No side effects. */
unsigned airplay_command_parse(const uint8_t *body,size_t len,airplay_command_t *out);
bool airplay_command_response_ok(const uint8_t *body,size_t len);
