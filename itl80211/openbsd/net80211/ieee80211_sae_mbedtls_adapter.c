/*
 * AirportItlwm driver -- group-19-only hostapd crypto adapter for mbedTLS.
 *
 * This is intentionally not the broad OpenWrt crypto_mbedtls.c wrapper.
 * It implements only the opaque crypto_bignum/crypto_ec operations reached
 * by the pinned SAE/Dragonfly core for P-256 group 19.  All random and ECP
 * blinding requests use ieee80211_sae_mbedtls_random(), which resolves to
 * the driver's kernel random_buf() source.  No password, scalar, point, KCK, PMK, or
 * frame body is logged here.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <net80211/ieee80211_sae_platform.h>

#include "utils/includes.h"
#include "utils/common.h"
#include "utils/const_time.h"
#include "crypto/crypto.h"

#ifndef MBEDTLS_CONFIG_FILE
#error "The SAE adapter and libmbedcrypto must share MBEDTLS_CONFIG_FILE"
#endif

#include <mbedtls/bignum.h>
#include <mbedtls/ecp.h>

struct crypto_bignum {
    mbedtls_mpi value;
};

struct crypto_ec_point {
    mbedtls_ecp_point value;
};

struct crypto_ec {
    mbedtls_ecp_group group;
    struct crypto_bignum prime;
    struct crypto_bignum order;
    struct crypto_bignum a;
    struct crypto_bignum b;
    struct crypto_ec_point generator;
};

static const u8 kSaeP256A[32] = {
    0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfc,
};

/* SEC 2 / FIPS 186-4 parameters for the sole permitted SAE group (19). */
static const u8 kSaeP256Prime[32] = {
    0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};

static const u8 kSaeP256Order[32] = {
    0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xbc, 0xe6, 0xfa, 0xad, 0xa7, 0x17, 0x9e, 0x84,
    0xf3, 0xb9, 0xca, 0xc2, 0xfc, 0x63, 0x25, 0x51,
};

static const u8 kSaeP256B[32] = {
    0x5a, 0xc6, 0x35, 0xd8, 0xaa, 0x3a, 0x93, 0xe7,
    0xb3, 0xeb, 0xbd, 0x55, 0x76, 0x98, 0x86, 0xbc,
    0x65, 0x1d, 0x06, 0xb0, 0xcc, 0x53, 0xb0, 0xf6,
    0x3b, 0xce, 0x3c, 0x3e, 0x27, 0xd2, 0x60, 0x4b,
};

static const u8 kSaeP256GeneratorX[32] = {
    0x6b, 0x17, 0xd1, 0xf2, 0xe1, 0x2c, 0x42, 0x47,
    0xf8, 0xbc, 0xe6, 0xe5, 0x63, 0xa4, 0x40, 0xf2,
    0x77, 0x03, 0x7d, 0x81, 0x2d, 0xeb, 0x33, 0xa0,
    0xf4, 0xa1, 0x39, 0x45, 0xd8, 0x98, 0xc2, 0x96,
};

static const u8 kSaeP256GeneratorY[32] = {
    0x4f, 0xe3, 0x42, 0xe2, 0xfe, 0x1a, 0x7f, 0x9b,
    0x8e, 0xe7, 0xeb, 0x4a, 0x7c, 0x0f, 0x9e, 0x16,
    0x2b, 0xce, 0x33, 0x57, 0x6b, 0x31, 0x5e, 0xce,
    0xcb, 0xb6, 0x40, 0x68, 0x37, 0xbf, 0x51, 0xf5,
};

static int
agent_sae_point_import(const mbedtls_ecp_group *group,
                       mbedtls_ecp_point *point,
                       const u8 *x, const u8 *y)
{
    u8 encoded[1 + sizeof(kSaeP256Prime) * 2];
    int rc;

    if (group == NULL || point == NULL || x == NULL || y == NULL)
        return -1;
    encoded[0] = 0x04;
    os_memcpy(encoded + 1, x, sizeof(kSaeP256Prime));
    os_memcpy(encoded + 1 + sizeof(kSaeP256Prime), y,
              sizeof(kSaeP256Prime));
    rc = mbedtls_ecp_point_read_binary(group, point, encoded,
                                       sizeof(encoded));
    if (rc == 0)
        rc = mbedtls_ecp_check_pubkey(group, point);
    ieee80211_sae_secure_zero(encoded, sizeof(encoded));
    return rc == 0 ? 0 : -1;
}

static int
agent_sae_point_export(const mbedtls_ecp_group *group,
                       const mbedtls_ecp_point *point,
                       u8 *x, u8 *y)
{
    u8 encoded[1 + sizeof(kSaeP256Prime) * 2];
    size_t length = 0;
    int rc;

    if (group == NULL || point == NULL)
        return -1;
    rc = mbedtls_ecp_point_write_binary(group, point,
                                        MBEDTLS_ECP_PF_UNCOMPRESSED, &length,
                                        encoded, sizeof(encoded));
    if (rc == 0 && (length != sizeof(encoded) || encoded[0] != 0x04))
        rc = -1;
    if (rc == 0 && x != NULL)
        os_memcpy(x, encoded + 1, sizeof(kSaeP256Prime));
    if (rc == 0 && y != NULL)
        os_memcpy(y, encoded + 1 + sizeof(kSaeP256Prime),
                  sizeof(kSaeP256Prime));
    ieee80211_sae_secure_zero(encoded, sizeof(encoded));
    return rc == 0 ? 0 : -1;
}

static mbedtls_mpi *
agent_sae_mpi(struct crypto_bignum *value)
{
    return value == NULL ? NULL : &value->value;
}

static const mbedtls_mpi *
agent_sae_const_mpi(const struct crypto_bignum *value)
{
    return value == NULL ? NULL : &value->value;
}

static int
agent_sae_copy_result(struct crypto_bignum *out, mbedtls_mpi *temporary)
{
    int rc = out == NULL ? -1 :
        mbedtls_mpi_copy(agent_sae_mpi(out), temporary);
    mbedtls_mpi_free(temporary);
    return rc == 0 ? 0 : -1;
}

static int
agent_sae_binary_mod(const struct crypto_bignum *a,
                     const struct crypto_bignum *b,
                     const struct crypto_bignum *modulus,
                     struct crypto_bignum *out, int multiply)
{
    mbedtls_mpi temporary;
    int rc;

    if (a == NULL || b == NULL || modulus == NULL || out == NULL)
        return -1;
    mbedtls_mpi_init(&temporary);
    rc = multiply ?
        mbedtls_mpi_mul_mpi(&temporary, agent_sae_const_mpi(a),
                            agent_sae_const_mpi(b)) :
        mbedtls_mpi_add_mpi(&temporary, agent_sae_const_mpi(a),
                            agent_sae_const_mpi(b));
    if (rc == 0)
        rc = mbedtls_mpi_mod_mpi(&temporary, &temporary,
                                 agent_sae_const_mpi(modulus));
    if (rc != 0) {
        mbedtls_mpi_free(&temporary);
        return -1;
    }
    return agent_sae_copy_result(out, &temporary);
}

struct crypto_bignum *
crypto_bignum_init(void)
{
    struct crypto_bignum *value = os_zalloc(sizeof(*value));

    if (value != NULL)
        mbedtls_mpi_init(&value->value);
    return value;
}

struct crypto_bignum *
crypto_bignum_init_set(const u8 *buf, size_t len)
{
    struct crypto_bignum *value;

    if (buf == NULL && len != 0)
        return NULL;
    value = crypto_bignum_init();
    if (value == NULL)
        return NULL;
    if (len != 0 && mbedtls_mpi_read_binary(&value->value, buf, len) != 0) {
        crypto_bignum_deinit(value, 1);
        return NULL;
    }
    return value;
}

struct crypto_bignum *
crypto_bignum_init_uint(unsigned int value)
{
    u8 bytes[sizeof(value)];
    size_t i;

    for (i = 0; i < sizeof(bytes); i++)
        bytes[sizeof(bytes) - 1 - i] = (u8)(value >> (i * 8));
    return crypto_bignum_init_set(bytes, sizeof(bytes));
}

void
crypto_bignum_deinit(struct crypto_bignum *value, int clear)
{
    (void)clear;
    if (value == NULL)
        return;
    /* mbedtls_mpi_free() uses mbedtls_platform_zeroize() before free. */
    mbedtls_mpi_free(&value->value);
    ieee80211_sae_secure_zero(value, sizeof(*value));
    os_free(value);
}

int
crypto_bignum_to_bin(const struct crypto_bignum *a, u8 *buf, size_t buflen,
                     size_t padlen)
{
    size_t len;

    if (a == NULL)
        return -1;
    len = mbedtls_mpi_size(agent_sae_const_mpi(a));
    if (len < padlen)
        len = padlen;
    if (len > buflen || (len != 0 && buf == NULL))
        return -1;
    return len == 0 ||
        mbedtls_mpi_write_binary(agent_sae_const_mpi(a), buf, len) == 0 ?
        (int)len : -1;
}

int
crypto_bignum_rand(struct crypto_bignum *out, const struct crypto_bignum *mod)
{
    if (out == NULL || mod == NULL ||
        mbedtls_mpi_cmp_int(agent_sae_const_mpi(mod), 1) < 0)
        return -1;
    return mbedtls_mpi_random(agent_sae_mpi(out), 0,
                              agent_sae_const_mpi(mod),
                              ieee80211_sae_mbedtls_random, NULL) == 0 ? 0 : -1;
}

int
crypto_bignum_add(const struct crypto_bignum *a, const struct crypto_bignum *b,
                  struct crypto_bignum *out)
{
    mbedtls_mpi temporary;
    int rc;

    if (a == NULL || b == NULL || out == NULL)
        return -1;
    mbedtls_mpi_init(&temporary);
    rc = mbedtls_mpi_add_mpi(&temporary, agent_sae_const_mpi(a),
                             agent_sae_const_mpi(b));
    return rc == 0 ? agent_sae_copy_result(out, &temporary) :
        (mbedtls_mpi_free(&temporary), -1);
}

int
crypto_bignum_mod(const struct crypto_bignum *a, const struct crypto_bignum *b,
                  struct crypto_bignum *out)
{
    mbedtls_mpi temporary;
    int rc;

    if (a == NULL || b == NULL || out == NULL)
        return -1;
    mbedtls_mpi_init(&temporary);
    rc = mbedtls_mpi_mod_mpi(&temporary, agent_sae_const_mpi(a),
                             agent_sae_const_mpi(b));
    return rc == 0 ? agent_sae_copy_result(out, &temporary) :
        (mbedtls_mpi_free(&temporary), -1);
}

int
crypto_bignum_exptmod(const struct crypto_bignum *a,
                      const struct crypto_bignum *b,
                      const struct crypto_bignum *modulus,
                      struct crypto_bignum *out)
{
    mbedtls_mpi temporary;
    int rc;

    if (a == NULL || b == NULL || modulus == NULL || out == NULL)
        return -1;
    mbedtls_mpi_init(&temporary);
    rc = mbedtls_mpi_exp_mod(&temporary, agent_sae_const_mpi(a),
                             agent_sae_const_mpi(b),
                             agent_sae_const_mpi(modulus), NULL);
    return rc == 0 ? agent_sae_copy_result(out, &temporary) :
        (mbedtls_mpi_free(&temporary), -1);
}

int
crypto_bignum_inverse(const struct crypto_bignum *a,
                      const struct crypto_bignum *modulus,
                      struct crypto_bignum *out)
{
    mbedtls_mpi temporary;
    int rc;

    if (a == NULL || modulus == NULL || out == NULL)
        return -1;
    mbedtls_mpi_init(&temporary);
    rc = mbedtls_mpi_inv_mod(&temporary, agent_sae_const_mpi(a),
                             agent_sae_const_mpi(modulus));
    return rc == 0 ? agent_sae_copy_result(out, &temporary) :
        (mbedtls_mpi_free(&temporary), -1);
}

int
crypto_bignum_sub(const struct crypto_bignum *a, const struct crypto_bignum *b,
                  struct crypto_bignum *out)
{
    mbedtls_mpi temporary;
    int rc;

    if (a == NULL || b == NULL || out == NULL)
        return -1;
    mbedtls_mpi_init(&temporary);
    rc = mbedtls_mpi_sub_mpi(&temporary, agent_sae_const_mpi(a),
                             agent_sae_const_mpi(b));
    return rc == 0 ? agent_sae_copy_result(out, &temporary) :
        (mbedtls_mpi_free(&temporary), -1);
}

int
crypto_bignum_div(const struct crypto_bignum *a, const struct crypto_bignum *b,
                  struct crypto_bignum *out)
{
    mbedtls_mpi temporary;
    int rc;

    if (a == NULL || b == NULL || out == NULL)
        return -1;
    mbedtls_mpi_init(&temporary);
    rc = mbedtls_mpi_div_mpi(&temporary, NULL, agent_sae_const_mpi(a),
                             agent_sae_const_mpi(b));
    return rc == 0 ? agent_sae_copy_result(out, &temporary) :
        (mbedtls_mpi_free(&temporary), -1);
}

int
crypto_bignum_addmod(const struct crypto_bignum *a,
                     const struct crypto_bignum *b,
                     const struct crypto_bignum *modulus,
                     struct crypto_bignum *out)
{
    return agent_sae_binary_mod(a, b, modulus, out, 0);
}

int
crypto_bignum_mulmod(const struct crypto_bignum *a,
                     const struct crypto_bignum *b,
                     const struct crypto_bignum *modulus,
                     struct crypto_bignum *out)
{
    return agent_sae_binary_mod(a, b, modulus, out, 1);
}

int
crypto_bignum_sqrmod(const struct crypto_bignum *a,
                     const struct crypto_bignum *modulus,
                     struct crypto_bignum *out)
{
    return agent_sae_binary_mod(a, a, modulus, out, 1);
}

int
crypto_bignum_rshift(const struct crypto_bignum *a, int count,
                     struct crypto_bignum *out)
{
    mbedtls_mpi temporary;
    int rc;

    if (a == NULL || out == NULL || count < 0)
        return -1;
    mbedtls_mpi_init(&temporary);
    rc = mbedtls_mpi_copy(&temporary, agent_sae_const_mpi(a));
    if (rc == 0)
        rc = mbedtls_mpi_shift_r(&temporary, (size_t)count);
    return rc == 0 ? agent_sae_copy_result(out, &temporary) :
        (mbedtls_mpi_free(&temporary), -1);
}

int
crypto_bignum_cmp(const struct crypto_bignum *a, const struct crypto_bignum *b)
{
    return a == NULL || b == NULL ? -1 :
        mbedtls_mpi_cmp_mpi(agent_sae_const_mpi(a), agent_sae_const_mpi(b));
}

int
crypto_bignum_is_zero(const struct crypto_bignum *a)
{
    return a != NULL && mbedtls_mpi_cmp_int(agent_sae_const_mpi(a), 0) == 0;
}

int
crypto_bignum_is_one(const struct crypto_bignum *a)
{
    return a != NULL && mbedtls_mpi_cmp_int(agent_sae_const_mpi(a), 1) == 0;
}

int
crypto_bignum_is_odd(const struct crypto_bignum *a)
{
    return a != NULL && mbedtls_mpi_get_bit(agent_sae_const_mpi(a), 0) != 0;
}

int
crypto_bignum_legendre(const struct crypto_bignum *a,
                       const struct crypto_bignum *prime)
{
    mbedtls_mpi exponent;
    mbedtls_mpi value;
    int result = -2;

    if (a == NULL || prime == NULL)
        return -2;
    mbedtls_mpi_init(&exponent);
    mbedtls_mpi_init(&value);
    if (mbedtls_mpi_sub_int(&exponent, agent_sae_const_mpi(prime), 1) == 0 &&
        mbedtls_mpi_shift_r(&exponent, 1) == 0 &&
        mbedtls_mpi_exp_mod(&value, agent_sae_const_mpi(a), &exponent,
                            agent_sae_const_mpi(prime), NULL) == 0) {
        unsigned int is_one = const_time_eq(
            (unsigned int)(mbedtls_mpi_cmp_int(&value, 1) == 0), 1);
        unsigned int is_zero = const_time_eq(
            (unsigned int)(mbedtls_mpi_cmp_int(&value, 0) == 0), 1);
        result = const_time_select_int(is_one, 1, -1);
        result = const_time_select_int(is_zero, 0, result);
    }
    mbedtls_mpi_free(&value);
    mbedtls_mpi_free(&exponent);
    return result;
}

struct crypto_ec *
crypto_ec_init(int group)
{
    struct crypto_ec *ec;
    int rc;

    if (group != 19)
        return NULL;
    ec = os_zalloc(sizeof(*ec));
    if (ec == NULL)
        return NULL;
    mbedtls_ecp_group_init(&ec->group);
    mbedtls_mpi_init(&ec->prime.value);
    mbedtls_mpi_init(&ec->order.value);
    mbedtls_mpi_init(&ec->a.value);
    mbedtls_mpi_init(&ec->b.value);
    mbedtls_ecp_point_init(&ec->generator.value);
    rc = mbedtls_ecp_group_load(&ec->group, MBEDTLS_ECP_DP_SECP256R1);
    if (rc == 0)
        rc = mbedtls_mpi_read_binary(&ec->prime.value, kSaeP256Prime,
                                     sizeof(kSaeP256Prime));
    if (rc == 0)
        rc = mbedtls_mpi_read_binary(&ec->order.value, kSaeP256Order,
                                     sizeof(kSaeP256Order));
    if (rc == 0)
        rc = mbedtls_mpi_read_binary(&ec->a.value, kSaeP256A,
                                     sizeof(kSaeP256A));
    if (rc == 0)
        rc = mbedtls_mpi_read_binary(&ec->b.value, kSaeP256B,
                                     sizeof(kSaeP256B));
    if (rc == 0)
        rc = agent_sae_point_import(&ec->group, &ec->generator.value,
                                    kSaeP256GeneratorX, kSaeP256GeneratorY);
    if (rc == 0)
        return ec;
    crypto_ec_deinit(ec);
    return NULL;
}

void
crypto_ec_deinit(struct crypto_ec *ec)
{
    if (ec == NULL)
        return;
    mbedtls_ecp_point_free(&ec->generator.value);
    mbedtls_mpi_free(&ec->b.value);
    mbedtls_mpi_free(&ec->a.value);
    mbedtls_mpi_free(&ec->order.value);
    mbedtls_mpi_free(&ec->prime.value);
    mbedtls_ecp_group_free(&ec->group);
    ieee80211_sae_secure_zero(ec, sizeof(*ec));
    os_free(ec);
}

size_t
crypto_ec_prime_len(struct crypto_ec *ec)
{
    return ec == NULL ? 0 : mbedtls_mpi_size(&ec->prime.value);
}

size_t
crypto_ec_prime_len_bits(struct crypto_ec *ec)
{
    return ec == NULL ? 0 :
        mbedtls_mpi_bitlen(&ec->prime.value);
}

size_t
crypto_ec_order_len(struct crypto_ec *ec)
{
    return ec == NULL ? 0 : mbedtls_mpi_size(&ec->order.value);
}

const struct crypto_bignum *
crypto_ec_get_prime(struct crypto_ec *ec)
{
    return ec == NULL ? NULL : &ec->prime;
}

const struct crypto_bignum *
crypto_ec_get_order(struct crypto_ec *ec)
{
    return ec == NULL ? NULL : &ec->order;
}

const struct crypto_bignum *
crypto_ec_get_a(struct crypto_ec *ec)
{
    return ec == NULL ? NULL : &ec->a;
}

const struct crypto_bignum *
crypto_ec_get_b(struct crypto_ec *ec)
{
    return ec == NULL ? NULL : &ec->b;
}

const struct crypto_ec_point *
crypto_ec_get_generator(struct crypto_ec *ec)
{
    return ec == NULL ? NULL : &ec->generator;
}

struct crypto_ec_point *
crypto_ec_point_init(struct crypto_ec *ec)
{
    struct crypto_ec_point *point;

    if (ec == NULL)
        return NULL;
    point = os_zalloc(sizeof(*point));
    if (point != NULL)
        mbedtls_ecp_point_init(&point->value);
    return point;
}

void
crypto_ec_point_deinit(struct crypto_ec_point *point, int clear)
{
    (void)clear;
    if (point == NULL)
        return;
    mbedtls_ecp_point_free(&point->value);
    ieee80211_sae_secure_zero(point, sizeof(*point));
    os_free(point);
}

int
crypto_ec_point_x(struct crypto_ec *ec, const struct crypto_ec_point *point,
                  struct crypto_bignum *x)
{
    u8 x_bytes[sizeof(kSaeP256Prime)];
    int rc;

    if (ec == NULL || point == NULL || x == NULL)
        return -1;
    rc = agent_sae_point_export(&ec->group, &point->value, x_bytes, NULL);
    if (rc == 0)
        rc = mbedtls_mpi_read_binary(&x->value, x_bytes, sizeof(x_bytes));
    ieee80211_sae_secure_zero(x_bytes, sizeof(x_bytes));
    return rc == 0 ? 0 : -1;
}

int
crypto_ec_point_to_bin(struct crypto_ec *ec, const struct crypto_ec_point *p,
                       u8 *x, u8 *y)
{
    if (ec == NULL || p == NULL)
        return -1;
    if (crypto_ec_prime_len(ec) != sizeof(kSaeP256Prime) ||
        agent_sae_point_export(&ec->group, &p->value, x, y) != 0)
        return -1;
    return 0;
}

struct crypto_ec_point *
crypto_ec_point_from_bin(struct crypto_ec *ec, const u8 *value)
{
    struct crypto_ec_point *point;
    if (ec == NULL || value == NULL || crypto_ec_prime_len(ec) != 32)
        return NULL;
    point = crypto_ec_point_init(ec);
    if (point == NULL)
        return NULL;
    if (agent_sae_point_import(&ec->group, &point->value, value,
                               value + sizeof(kSaeP256Prime)) != 0) {
        crypto_ec_point_deinit(point, 1);
        return NULL;
    }
    return point;
}

int
crypto_ec_point_add(struct crypto_ec *ec, const struct crypto_ec_point *a,
                    const struct crypto_ec_point *b,
                    struct crypto_ec_point *out)
{
    mbedtls_mpi one;
    mbedtls_ecp_point temporary;
    int rc;

    if (ec == NULL || a == NULL || b == NULL || out == NULL)
        return -1;
    mbedtls_mpi_init(&one);
    mbedtls_ecp_point_init(&temporary);
    rc = mbedtls_mpi_lset(&one, 1);
    if (rc == 0)
        rc = mbedtls_ecp_muladd(&ec->group, &temporary, &one, &a->value,
                                &one, &b->value);
    if (rc == 0)
        rc = mbedtls_ecp_copy(&out->value, &temporary);
    mbedtls_ecp_point_free(&temporary);
    mbedtls_mpi_free(&one);
    return rc == 0 ? 0 : -1;
}

int
crypto_ec_point_mul(struct crypto_ec *ec, const struct crypto_ec_point *point,
                    const struct crypto_bignum *scalar,
                    struct crypto_ec_point *out)
{
    mbedtls_ecp_point temporary;
    int rc;

    if (ec == NULL || point == NULL || scalar == NULL || out == NULL)
        return -1;
    mbedtls_ecp_point_init(&temporary);
    rc = mbedtls_ecp_mul(&ec->group, &temporary,
                         agent_sae_const_mpi(scalar), &point->value,
                         ieee80211_sae_mbedtls_random, NULL);
    if (rc == 0)
        rc = mbedtls_ecp_copy(&out->value, &temporary);
    mbedtls_ecp_point_free(&temporary);
    return rc == 0 ? 0 : -1;
}

int
crypto_ec_point_invert(struct crypto_ec *ec, struct crypto_ec_point *point)
{
    mbedtls_mpi temporary;
    u8 x[sizeof(kSaeP256Prime)];
    u8 y[sizeof(kSaeP256Prime)];
    int rc;

    if (ec == NULL || point == NULL)
        return -1;
    if (mbedtls_ecp_is_zero(&point->value))
        return 0;
    if (agent_sae_point_export(&ec->group, &point->value, x, y) != 0)
        return -1;
    mbedtls_mpi_init(&temporary);
    rc = mbedtls_mpi_read_binary(&temporary, y, sizeof(y));
    if (rc == 0 && mbedtls_mpi_cmp_int(&temporary, 0) != 0)
        rc = mbedtls_mpi_sub_mpi(&temporary, &ec->prime.value, &temporary);
    if (rc == 0 && mbedtls_mpi_cmp_int(&temporary, 0) != 0)
        rc = mbedtls_mpi_write_binary(&temporary, y, sizeof(y));
    if (rc == 0 && mbedtls_mpi_cmp_int(&temporary, 0) != 0)
        rc = agent_sae_point_import(&ec->group, &point->value, x, y);
    mbedtls_mpi_free(&temporary);
    ieee80211_sae_secure_zero(x, sizeof(x));
    ieee80211_sae_secure_zero(y, sizeof(y));
    return rc == 0 ? 0 : -1;
}

struct crypto_bignum *
crypto_ec_point_compute_y_sqr(struct crypto_ec *ec,
                               const struct crypto_bignum *x)
{
    struct crypto_bignum *out;
    mbedtls_mpi temporary;
    int rc;

    if (ec == NULL || x == NULL)
        return NULL;
    out = crypto_bignum_init();
    if (out == NULL)
        return NULL;
    mbedtls_mpi_init(&temporary);
    rc = mbedtls_mpi_mul_mpi(&temporary, agent_sae_const_mpi(x),
                             agent_sae_const_mpi(x));
    if (rc == 0)
        rc = mbedtls_mpi_mod_mpi(&temporary, &temporary, &ec->prime.value);
    if (rc == 0)
        rc = mbedtls_mpi_add_mpi(&temporary, &temporary, &ec->a.value);
    if (rc == 0)
        rc = mbedtls_mpi_mod_mpi(&temporary, &temporary, &ec->prime.value);
    if (rc == 0)
        rc = mbedtls_mpi_mul_mpi(&temporary, &temporary,
                                 agent_sae_const_mpi(x));
    if (rc == 0)
        rc = mbedtls_mpi_mod_mpi(&temporary, &temporary, &ec->prime.value);
    if (rc == 0)
        rc = mbedtls_mpi_add_mpi(&temporary, &temporary, &ec->b.value);
    if (rc == 0)
        rc = mbedtls_mpi_mod_mpi(&temporary, &temporary, &ec->prime.value);
    if (rc == 0)
        rc = mbedtls_mpi_copy(&out->value, &temporary);
    mbedtls_mpi_free(&temporary);
    if (rc == 0)
        return out;
    crypto_bignum_deinit(out, 1);
    return NULL;
}

int
crypto_ec_point_is_at_infinity(struct crypto_ec *ec,
                                const struct crypto_ec_point *point)
{
    (void)ec;
    /* mbedTLS 3.6 exposes this read-only predicate without a const input. */
    return point != NULL &&
        mbedtls_ecp_is_zero((mbedtls_ecp_point *)&point->value);
}

int
crypto_ec_point_is_on_curve(struct crypto_ec *ec,
                            const struct crypto_ec_point *point)
{
    return ec != NULL && point != NULL &&
        mbedtls_ecp_check_pubkey(&ec->group, &point->value) == 0;
}

int
crypto_ec_point_cmp(const struct crypto_ec *ec,
                    const struct crypto_ec_point *a,
                    const struct crypto_ec_point *b)
{
    (void)ec;
    return a == NULL || b == NULL ? -1 :
        mbedtls_ecp_point_cmp(&a->value, &b->value);
}

void
crypto_ec_point_debug_print(const struct crypto_ec *ec,
                            const struct crypto_ec_point *point,
                            const char *title)
{
    (void)ec;
    (void)point;
    (void)title;
}
