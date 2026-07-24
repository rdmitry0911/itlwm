/*
 * Kernel-only hostapd/mbedTLS support for driver-owned SAE.
 *
 * The imported SAE subset has its debug output compiled out.  This file is
 * the sole bridge for allocation, random bytes, and zeroization; it makes no
 * user-space, Keychain, filesystem, socket, or logging call.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <sys/param.h>
#include <sys/systm.h>

#include <IOKit/IOLib.h>
#include <libkern/crypto/rand.h>

#include <net80211/ieee80211_sae_platform.h>

#include "utils/includes.h"
#include "utils/common.h"
#include "common/wpa_common.h"
#include "crypto/crypto.h"
#include "crypto/dh_groups.h"

struct itl_sae_allocation_header {
	size_t length;
};

static void *
itl_sae_allocate(size_t length, int clear)
{
	struct itl_sae_allocation_header *header;
	size_t total;

	if (length > (size_t)-1 - sizeof(*header))
		return NULL;
	total = sizeof(*header) + length;
	header = (struct itl_sae_allocation_header *)IOMalloc(total);
	if (header == NULL)
		return NULL;
	header->length = length;
	if (clear && length != 0)
		bzero(header + 1, length);
	return header + 1;
}

void
ieee80211_sae_secure_zero(void *ptr, size_t len)
{
	volatile unsigned char *byte = (volatile unsigned char *)ptr;

	while (byte != NULL && len-- != 0)
		*byte++ = 0;
}

void *
itl_sae_malloc(size_t size)
{
	return itl_sae_allocate(size, 0);
}

void *
itl_sae_realloc(void *ptr, size_t size)
{
	struct itl_sae_allocation_header *old_header;
	void *replacement;
	size_t copy_length;

	if (ptr == NULL)
		return itl_sae_malloc(size);
	if (size == 0) {
		itl_sae_free(ptr);
		return NULL;
	}
	old_header = ((struct itl_sae_allocation_header *)ptr) - 1;
	replacement = itl_sae_allocate(size, 0);
	if (replacement == NULL)
		return NULL;
	copy_length = old_header->length < size ? old_header->length : size;
	if (copy_length != 0)
		memcpy(replacement, ptr, copy_length);
	itl_sae_free(ptr);
	return replacement;
}

void
itl_sae_free(void *ptr)
{
	struct itl_sae_allocation_header *header;
	size_t total;

	if (ptr == NULL)
		return;
	header = ((struct itl_sae_allocation_header *)ptr) - 1;
	total = sizeof(*header) + header->length;
	ieee80211_sae_secure_zero(header, total);
    IOFree(header, total);
}

/*
 * hostapd's OS_NO_C_LIB_DEFINES path declares these names as functions.  Do
 * not turn them into preprocessor aliases for XNU's fortified memcpy family:
 * those aliases expand inside utils/os.h's declarations.  These leaf wrappers
 * retain the kernel implementation and the strict no-libc boundary.
 */
void *
os_memcpy(void *destination, const void *source, size_t length)
{
    return memcpy(destination, source, length);
}

void *
os_memmove(void *destination, const void *source, size_t length)
{
    return memmove(destination, source, length);
}

void *
os_memset(void *destination, int value, size_t length)
{
    return memset(destination, value, length);
}

int
os_memcmp(const void *left, const void *right, size_t length)
{
    return memcmp(left, right, length);
}

size_t
os_strlen(const char *string)
{
    return strlen(string);
}

void *
itl_sae_mbedtls_calloc(size_t count, size_t size)
{
	if (size != 0 && count > (size_t)-1 / size)
		return NULL;
	return itl_sae_allocate(count * size, 1);
}

void
itl_sae_mbedtls_free(void *ptr)
{
	itl_sae_free(ptr);
}

void
mbedtls_platform_zeroize(void *ptr, size_t len)
{
	ieee80211_sae_secure_zero(ptr, len);
}

int
os_get_random(unsigned char *buf, size_t len)
{
	if (buf == NULL && len != 0)
		return -1;
	if (len != 0)
		random_buf(buf, len);
	return 0;
}

int
ieee80211_sae_mbedtls_random(void *context, unsigned char *output, size_t len)
{
	(void)context;
	return os_get_random(output, len);
}

int
crypto_get_random(void *buf, size_t len)
{
	return os_get_random((unsigned char *)buf, len);
}

void *
os_zalloc(size_t size)
{
	return itl_sae_allocate(size, 1);
}

int
os_memcmp_const(const void *left, const void *right, size_t len)
{
	const unsigned char *a = (const unsigned char *)left;
	const unsigned char *b = (const unsigned char *)right;
	unsigned char difference = 0;

	if ((a == NULL || b == NULL) && len != 0)
		return -1;
	while (len-- != 0)
		difference |= *a++ ^ *b++;
	return difference;
}

void
forced_memzero(void *ptr, size_t len)
{
	ieee80211_sae_secure_zero(ptr, len);
}

void
bin_clear_free(void *ptr, size_t len)
{
	ieee80211_sae_secure_zero(ptr, len);
	itl_sae_free(ptr);
}

u32
wpa_akm_to_suite(int akm)
{
	return akm == WPA_KEY_MGMT_SAE ? RSN_AUTH_KEY_MGMT_SAE : 0;
}

static int
ieee80211_sae_hex_digit(unsigned char value)
{
	if (value >= '0' && value <= '9')
		return value - '0';
	if (value >= 'a' && value <= 'f')
		return value - 'a' + 10;
	if (value >= 'A' && value <= 'F')
		return value - 'A' + 10;
	return -1;
}

int
hexstr2bin(const char *hex, u8 *buf, size_t len)
{
	size_t index;

	if (hex == NULL || (buf == NULL && len != 0))
		return -1;
	for (index = 0; index < len; index++) {
		int high = ieee80211_sae_hex_digit((unsigned char)hex[index * 2]);
		int low = ieee80211_sae_hex_digit(
		    (unsigned char)hex[index * 2 + 1]);

		if (high < 0 || low < 0) {
			ieee80211_sae_secure_zero(buf, len);
			return -1;
		}
		buf[index] = (u8)((high << 4) | low);
	}
	return 0;
}

void
buf_shift_right(u8 *buf, size_t len, size_t bits)
{
	size_t index;

	if (buf == NULL || len == 0 || bits == 0 || bits >= 8)
		return;
	for (index = len - 1; index > 0; index--)
		buf[index] = (u8)((buf[index - 1] << (8 - bits)) |
		    (buf[index] >> bits));
	buf[0] >>= bits;
}

size_t
int_array_len(const int *values)
{
	size_t length = 0;

	while (values != NULL && values[length] != 0)
		length++;
	return length;
}

/*
 * The imported generic core retains FFC symbols, but driver admission never
 * permits them: the only engine profile is P-256 group 19.
 */
int
crypto_dh_init(u8 generator, const u8 *prime, size_t prime_len, u8 *privkey,
    u8 *pubkey)
{
	(void)generator;
	(void)prime;
	if (privkey != NULL)
		ieee80211_sae_secure_zero(privkey, prime_len);
	if (pubkey != NULL)
		ieee80211_sae_secure_zero(pubkey, prime_len);
	return -1;
}

int
crypto_dh_derive_secret(u8 generator, const u8 *prime, size_t prime_len,
    const u8 *order, size_t order_len, const u8 *privkey, size_t privkey_len,
    const u8 *pubkey, size_t pubkey_len, u8 *secret, size_t *len)
{
	(void)generator;
	(void)prime;
	(void)prime_len;
	(void)order;
	(void)order_len;
	(void)privkey;
	(void)privkey_len;
	(void)pubkey;
	(void)pubkey_len;
	if (secret != NULL && len != NULL)
		ieee80211_sae_secure_zero(secret, *len);
	if (len != NULL)
		*len = 0;
	return -1;
}

/*
 * sae_set_group() probes the generic finite-field registry after attempting
 * ECC.  Linking hostapd's broad DH registry merely to make that dead probe
 * resolve would make unsupported groups reachable from this imported core.
 * Group 19 succeeds through crypto_ec_init() before this function is reached;
 * every finite-field group is rejected here, in addition to the public
 * engine's fixed group-19 admission check.
 */
const struct dh_group *
dh_groups_get(int id)
{
	(void)id;
	return NULL;
}
