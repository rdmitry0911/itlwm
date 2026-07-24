/*
 * Narrow kernel platform boundary for the driver-owned SAE group-19 core.
 *
 * The imported hostapd and mbedTLS sources are compiled only through this
 * boundary.  It deliberately exposes no logging, file, socket, time, or
 * ambient user-space allocator facility.  Secret buffers use the matching
 * allocation family and are wiped before release.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef _NET80211_IEEE80211_SAE_PLATFORM_H_
#define _NET80211_IEEE80211_SAE_PLATFORM_H_

#include <stddef.h>

void ieee80211_sae_secure_zero(void *ptr, size_t len);
int ieee80211_sae_mbedtls_random(void *context, unsigned char *output,
    size_t len);

void *itl_sae_malloc(size_t size);
void *itl_sae_realloc(void *ptr, size_t size);
void itl_sae_free(void *ptr);
void *itl_sae_mbedtls_calloc(size_t count, size_t size);
void itl_sae_mbedtls_free(void *ptr);

#endif /* _NET80211_IEEE80211_SAE_PLATFORM_H_ */
