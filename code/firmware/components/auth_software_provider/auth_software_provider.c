/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Public default: no accessory identity, no signing key, no authentication bypass. */
#include "auth_software_provider.h"
#include "auth_port.h"
esp_err_t auth_software_provider_init(void) { return ESP_ERR_NOT_SUPPORTED; }
const char *auth_software_provider_stage(void) { return "AUTH_PROVIDER_REQUIRED"; }
int auth_software_provider_error(void) { return ESP_ERR_NOT_SUPPORTED; }
