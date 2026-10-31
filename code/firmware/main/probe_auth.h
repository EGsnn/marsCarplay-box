#pragma once
#include "iap2_core.h"
void probe_auth_reset(void);
void probe_auth_message(iap2_core_t *core, uint16_t id, const uint8_t *body, size_t length);
