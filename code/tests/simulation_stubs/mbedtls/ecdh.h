#pragma once
#include "mbedtls/private/ecp.h"
static inline int mbedtls_ecdh_compute_shared(mbedtls_ecp_group *g,mbedtls_mpi *z,const mbedtls_ecp_point *q,const mbedtls_mpi *d,int (*rng)(void *,unsigned char *,size_t),void *ctx)
{
 mbedtls_ecp_point r;mbedtls_ecp_point_init(&r);
 int rc=mbedtls_ecp_mul(g,&r,d,q,rng,ctx);
 if(!rc)rc=mbedtls_mpi_copy(z,&r.MBEDTLS_PRIVATE(X));
 mbedtls_ecp_point_free(&r);return rc;
}
