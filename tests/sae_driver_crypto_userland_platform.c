/*
 * User-space platform shim for exercising the exact driver SAE archive.
 *
 * This file is linked only by tests.  It deliberately mirrors the bounded
 * kernel platform entry points while using libc/getrandom so the vendored
 * hostap/mbedTLS objects can be stress-tested without a kext or radio.
 */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>

#include <net80211/ieee80211_sae_platform.h>

#include "utils/includes.h"
#include "utils/common.h"
#include "common/wpa_common.h"
#include "crypto/dh_groups.h"

void
ieee80211_sae_secure_zero(void *ptr, size_t len)
{
	volatile unsigned char *byte = ptr;

	while (byte != NULL && len-- != 0)
		*byte++ = 0;
}

void *
itl_sae_malloc(size_t size)
{
	return malloc(size);
}

void *
itl_sae_realloc(void *ptr, size_t size)
{
	return realloc(ptr, size);
}

void
itl_sae_free(void *ptr)
{
	free(ptr);
}

void *
itl_sae_mbedtls_calloc(size_t count, size_t size)
{
	return calloc(count, size);
}

void
itl_sae_mbedtls_free(void *ptr)
{
	free(ptr);
}

void
mbedtls_platform_zeroize(void *ptr, size_t len)
{
	ieee80211_sae_secure_zero(ptr, len);
}

int
os_get_random(unsigned char *buf, size_t len)
{
	unsigned char *position = buf;

	if (buf == NULL && len != 0)
		return -1;
	while (len != 0) {
		ssize_t received = getrandom(position, len, 0);

		if (received < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (received == 0)
			return -1;
		position += (size_t)received;
		len -= (size_t)received;
	}
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
	return os_get_random(buf, len);
}

void *
os_zalloc(size_t size)
{
	return calloc(1, size);
}

int
os_memcmp_const(const void *left, const void *right, size_t len)
{
	const unsigned char *a = left;
	const unsigned char *b = right;
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
	free(ptr);
}

u32
wpa_akm_to_suite(int akm)
{
	return akm == WPA_KEY_MGMT_SAE ? RSN_AUTH_KEY_MGMT_SAE : 0;
}

static int
sae_test_hex_digit(unsigned char value)
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
		int high = sae_test_hex_digit((unsigned char)hex[index * 2]);
		int low = sae_test_hex_digit((unsigned char)hex[index * 2 + 1]);

		if (high < 0 || low < 0)
			return -1;
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

int
crypto_dh_init(u8 generator, const u8 *prime, size_t prime_len, u8 *privkey,
    u8 *pubkey)
{
	(void)generator;
	(void)prime;
	(void)prime_len;
	(void)privkey;
	(void)pubkey;
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
	(void)secret;
	(void)len;
	return -1;
}

const struct dh_group *
dh_groups_get(int id)
{
	(void)id;
	return NULL;
}
