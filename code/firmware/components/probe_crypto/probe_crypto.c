#include "probe_crypto.h"
#include "mbedtls/hkdf.h"
#include "mbedtls/platform_util.h"
#include "psa/crypto.h"
#include <string.h>
int mbedtls_hkdf(const mbedtls_md_info_t *md,const unsigned char *salt,size_t salt_len,
                 const unsigned char *ikm,size_t ikm_len,const unsigned char *info,
                 size_t info_len,unsigned char *out,size_t out_len)
{
    if(!md)return -1;
    psa_algorithm_t hash=mbedtls_md_get_type(md)==MBEDTLS_MD_SHA512?PSA_ALG_SHA_512:
                         mbedtls_md_get_type(md)==MBEDTLS_MD_SHA256?PSA_ALG_SHA_256:0;
    if(!hash)return -1;
    psa_key_derivation_operation_t op=PSA_KEY_DERIVATION_OPERATION_INIT;
    psa_status_t rc=psa_key_derivation_setup(&op,PSA_ALG_HKDF(hash));
    if(!rc)rc=psa_key_derivation_input_bytes(&op,PSA_KEY_DERIVATION_INPUT_SALT,salt,salt_len);
    if(!rc)rc=psa_key_derivation_input_bytes(&op,PSA_KEY_DERIVATION_INPUT_SECRET,ikm,ikm_len);
    if(!rc)rc=psa_key_derivation_input_bytes(&op,PSA_KEY_DERIVATION_INPUT_INFO,info,info_len);
    if(!rc)rc=psa_key_derivation_output_bytes(&op,out,out_len);
    psa_key_derivation_abort(&op);
    if(rc&&out)mbedtls_platform_zeroize(out,out_len);
    return (int)rc;
}
static int exchange(const uint8_t *seed,const uint8_t peer[32],uint8_t public_key[32],uint8_t shared[32])
{
    psa_key_attributes_t attr=PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attr,PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_MONTGOMERY));
    psa_set_key_bits(&attr,255);psa_set_key_usage_flags(&attr,PSA_KEY_USAGE_DERIVE);
    psa_set_key_algorithm(&attr,PSA_ALG_ECDH);
    mbedtls_svc_key_id_t key=0;size_t n=0;
    psa_status_t rc=seed?psa_import_key(&attr,seed,32,&key):psa_generate_key(&attr,&key);psa_reset_key_attributes(&attr);
    if(!rc)rc=psa_export_public_key(key,public_key,32,&n);
    if(!rc&&n!=32)rc=PSA_ERROR_CORRUPTION_DETECTED;
    if(!rc)rc=psa_raw_key_agreement(PSA_ALG_ECDH,key,peer,32,shared,32,&n);
    if(!rc&&n!=32)rc=PSA_ERROR_CORRUPTION_DETECTED;
    psa_destroy_key(key);
    if(rc){mbedtls_platform_zeroize(public_key,32);mbedtls_platform_zeroize(shared,32);}
    return (int)rc;
}
int probe_x25519(const uint8_t peer[32],uint8_t public_key[32],uint8_t shared[32])
{return exchange(NULL,peer,public_key,shared);}
int probe_x25519_seed(const uint8_t seed[32],const uint8_t peer[32],uint8_t public_key[32],uint8_t shared[32])
{return exchange(seed,peer,public_key,shared);}
int probe_crypto_self_test(void)
{
    /* RFC 5869 case 1; also exercises the PSA SHA-256 HKDF adapter. */
    const uint8_t expected[]={0x3c,0xb2,0x5f,0x25,0xfa,0xac,0xd5,0x7a,0x90,0x43,0x4f,0x64,0xd0,0x36,0x2f,0x2a,0x2d,0x2d,0x0a,0x90,0xcf,0x1a,0x5a,0x4c,0x5d,0xb0,0x2d,0x56,0xec,0xc4,0xc5,0xbf,0x34,0x00,0x72,0x08,0xd5,0xb8,0x87,0x18,0x58,0x65};
    uint8_t ikm[22],salt[13],info[10],out[42];memset(ikm,0x0b,sizeof(ikm));
    for(unsigned i=0;i<13;i++)salt[i]=i;
    for(unsigned i=0;i<10;i++)info[i]=0xf0+i;
    int rc=mbedtls_hkdf(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),salt,13,ikm,22,info,10,out,42);
    if(rc||memcmp(out,expected,42))return -1;
    uint8_t base[32]={9},pub[32],secret[32];
    rc=probe_x25519(base,pub,secret); /* X25519(k, basepoint) equals exported public key. */
    if(!rc&&memcmp(pub,secret,32))rc=-1;
    mbedtls_platform_zeroize(secret,32);return rc;
}
