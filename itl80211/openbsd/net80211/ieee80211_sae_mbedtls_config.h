/*
 * Fixed mbedTLS 3.6.6 configuration for the in-kernel SAE group-19 core.
 *
 * This is intentionally a crypto arithmetic configuration, not a TLS,
 * X.509, PSA, ECDH, or ECDSA configuration.  The only curve is P-256 and
 * every allocation/zeroization path is supplied by the driver platform.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef _NET80211_IEEE80211_SAE_MBEDTLS_CONFIG_H_
#define _NET80211_IEEE80211_SAE_MBEDTLS_CONFIG_H_

#include <stddef.h>

#define MBEDTLS_CONFIG_VERSION 0x03060600
#define MBEDTLS_HAVE_ASM
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_UTIL_C
#define MBEDTLS_ERROR_C
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_ECP_C
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_FIXED_POINT_OPTIM 0

/*
 * A kext must not acquire a compiler helper for 128-bit division nor fall
 * back to libc.  P-256 operations receive their RNG callback explicitly.
 */
#define MBEDTLS_NO_UDBL_DIVISION
#define MBEDTLS_PLATFORM_NO_STD_FUNCTIONS
#define MBEDTLS_PLATFORM_MEMORY
#define MBEDTLS_PLATFORM_CALLOC_MACRO itl_sae_mbedtls_calloc
#define MBEDTLS_PLATFORM_FREE_MACRO itl_sae_mbedtls_free
#define MBEDTLS_PLATFORM_ZEROIZE_ALT

void *itl_sae_mbedtls_calloc(size_t count, size_t size);
void itl_sae_mbedtls_free(void *ptr);
void mbedtls_platform_zeroize(void *ptr, size_t len);

#endif /* _NET80211_IEEE80211_SAE_MBEDTLS_CONFIG_H_ */
