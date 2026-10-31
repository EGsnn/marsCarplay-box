/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef enum {ACT_IDLE,ACT_UI_READY,ACT_UI_WAIT,ACT_MODES_WAIT,ACT_TAKE_READY,ACT_TAKE_WAIT,ACT_DONE,ACT_FAILED} airplay_activation_stage_t;
typedef struct {airplay_activation_stage_t stage;bool modes_seen;uint32_t cseq;uint64_t deadline;} airplay_activation_t;
void airplay_activation_begin(airplay_activation_t *s,uint64_t now);
/* 1=requestUI, 2=TakeScreen; a reservation prevents duplicate sends. */
unsigned airplay_activation_next(airplay_activation_t *s,uint64_t now);
void airplay_activation_sent(airplay_activation_t *s,bool ok,uint32_t cseq);
void airplay_activation_modes(airplay_activation_t *s);
void airplay_activation_ui_requested(airplay_activation_t *s);
void airplay_activation_response(airplay_activation_t *s,unsigned code,int cseq,bool body_ok);
