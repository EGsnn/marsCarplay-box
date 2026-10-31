#pragma once
#include <stddef.h>
#include <stdint.h>
/* PSA-managed ephemeral key, exported only as its public X25519 value. */
int probe_x25519(const uint8_t peer[32],uint8_t public_key[32],uint8_t shared[32]);
int probe_x25519_seed(const uint8_t seed[32],const uint8_t peer[32],uint8_t public_key[32],uint8_t shared[32]);
int probe_crypto_self_test(void);
