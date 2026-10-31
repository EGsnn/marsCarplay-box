#include "auth_port.h"
esp_err_t auth_port_read_certificate(uint8_t *p,size_t *n){(void)p;(void)n;return ESP_FAIL;}
esp_err_t auth_port_sign_challenge(const uint8_t *p,size_t n,uint8_t *o,size_t *l){(void)p;(void)n;(void)o;(void)l;return ESP_FAIL;}
esp_err_t auth_port_get_protocol_major(uint8_t *p){*p=3;return ESP_OK;}
int auth_port_is_configured(void){return 0;}
