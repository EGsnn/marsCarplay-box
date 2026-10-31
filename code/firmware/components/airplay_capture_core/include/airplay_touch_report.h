/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define AIRPLAY_TOUCH_UID UINT32_C(0x2a2a2a2a)
/* Two six-byte contacts match the existing /info descriptor. Contact 1 stays up;
 * releasing contact 0 retains the coordinates supplied by the caller. */
bool airplay_touch_report(const uint8_t *command,size_t length,uint8_t report[12]);
