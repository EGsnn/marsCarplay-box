/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include "esp_err.h"

/* Register authorized certificate/signing callbacks with auth_port.
 * Public default returns ESP_ERR_NOT_SUPPORTED and has no identity. */
esp_err_t auth_software_provider_init(void);
/* Fixed stage name and numeric error only; no identity or challenge content. */
const char *auth_software_provider_stage(void);
int auth_software_provider_error(void);
