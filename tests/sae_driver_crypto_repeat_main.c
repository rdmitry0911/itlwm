/* Stress the exact driver SAE/mbedTLS archive through complete HnP rounds. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils/includes.h"
#include "utils/common.h"
#include "utils/wpabuf.h"
#include "common/sae.h"
#include "common/wpa_common.h"
#include "crypto/crypto.h"

#define TEST_GROUP 19
#define TEST_COMMIT_LEN 98
#define TEST_CONFIRM_LEN 34

static int
write_commit(struct sae_data *sae, u8 output[TEST_COMMIT_LEN])
{
	struct wpabuf *buffer = wpabuf_alloc(TEST_COMMIT_LEN);
	int result = -1;

	if (buffer != NULL &&
	    sae_write_commit(sae, buffer, NULL, NULL, 0) == 0 &&
	    wpabuf_len(buffer) == TEST_COMMIT_LEN) {
		memcpy(output, wpabuf_head(buffer), TEST_COMMIT_LEN);
		result = 0;
	}
	wpabuf_clear_free(buffer);
	return result;
}

static int
write_confirm(struct sae_data *sae, u8 output[TEST_CONFIRM_LEN])
{
	struct wpabuf *buffer = wpabuf_alloc(TEST_CONFIRM_LEN);
	int result = -1;

	if (buffer != NULL && sae_write_confirm(sae, buffer) == 0 &&
	    wpabuf_len(buffer) == TEST_CONFIRM_LEN) {
		memcpy(output, wpabuf_head(buffer), TEST_CONFIRM_LEN);
		result = 0;
	}
	wpabuf_clear_free(buffer);
	return result;
}

static int
parse_commit(struct sae_data *sae, const u8 commit[TEST_COMMIT_LEN])
{
	static int groups[] = { TEST_GROUP, 0 };
	const u8 *token = NULL;
	size_t token_len = 0;
	int offset = 0;

	return sae_parse_commit(sae, commit, TEST_COMMIT_LEN, &token,
	    &token_len, groups, 0, &offset) == 0 && token == NULL &&
	    token_len == 0 && offset == TEST_COMMIT_LEN ? 0 : -1;
}

static int
one_round(const u8 station[6], const u8 access_point[6],
	const u8 *password, size_t password_len, const u8 expected_pwe[64])
{
	struct sae_data sta;
	struct sae_data ap;
	u8 sta_commit[TEST_COMMIT_LEN];
	u8 ap_commit[TEST_COMMIT_LEN];
	u8 sta_confirm[TEST_CONFIRM_LEN];
	u8 ap_confirm[TEST_CONFIRM_LEN];
	u8 pwe[64];
	int offset = 0;
	int result = -1;

	memset(&sta, 0, sizeof(sta));
	memset(&ap, 0, sizeof(ap));
	sta.no_pw_id = 1;
	ap.no_pw_id = 1;
	sta.akmp = WPA_KEY_MGMT_SAE;
	ap.akmp = WPA_KEY_MGMT_SAE;

	if (sae_set_group(&sta, TEST_GROUP) != 0 ||
	    sae_set_group(&ap, TEST_GROUP) != 0 ||
	    sae_prepare_commit(station, access_point, password, password_len,
		&sta) != 0 ||
	    crypto_ec_point_to_bin(sta.tmp->ec, sta.tmp->pwe_ecc, pwe,
		pwe + 32) != 0 ||
	    memcmp(pwe, expected_pwe, sizeof(pwe)) != 0 ||
	    write_commit(&sta, sta_commit) != 0 ||
	    parse_commit(&ap, sta_commit) != 0 ||
	    sae_prepare_commit(access_point, station, password, password_len,
		&ap) != 0 ||
	    write_commit(&ap, ap_commit) != 0 ||
	    sae_process_commit(&ap) != 0 ||
	    parse_commit(&sta, ap_commit) != 0 ||
	    sae_process_commit(&sta) != 0 ||
	    write_confirm(&sta, sta_confirm) != 0 ||
	    sae_check_confirm(&ap, sta_confirm, sizeof(sta_confirm),
		&offset) != 0 || offset != TEST_CONFIRM_LEN ||
	    write_confirm(&ap, ap_confirm) != 0 ||
	    sae_check_confirm(&sta, ap_confirm, sizeof(ap_confirm),
		&offset) != 0 || offset != TEST_CONFIRM_LEN ||
	    sta.pmk_len != SAE_PMK_LEN || ap.pmk_len != SAE_PMK_LEN ||
	    memcmp(sta.pmk, ap.pmk, SAE_PMK_LEN) != 0 ||
	    memcmp(sta.pmkid, ap.pmkid, SAE_PMKID_LEN) != 0)
		goto out;

	result = 0;
out:
	sae_clear_data(&ap);
	sae_clear_data(&sta);
	memset(pwe, 0, sizeof(pwe));
	return result;
}

int
main(int argc, char **argv)
{
	static const u8 station[6] = { 0x7e, 0xf7, 0x05, 0x0c, 0x7e, 0x5b };
	static const u8 access_point[6] = {
	    0x80, 0xe4, 0xba, 0x20, 0xef, 0xf9
	};
	static const u8 password[] = "aa00bb0900";
	/* Independently derived IEEE 802.11 group-19 HnP PWE for the fixed
	 * laboratory MAC/password tuple.  Unlike a same-backend round trip,
	 * this catches randomized blinding that accidentally changes the PWE. */
	static const u8 expected_pwe[64] = {
	    0xc1, 0xf1, 0x23, 0x6d, 0xdd, 0x28, 0x55, 0x52,
	    0x1b, 0x02, 0xe2, 0x10, 0x5a, 0xce, 0xdd, 0x4b,
	    0x8a, 0xca, 0x67, 0xc8, 0xea, 0xb2, 0x5a, 0xde,
	    0x78, 0xfa, 0x7f, 0xb9, 0xb7, 0x1b, 0x86, 0x61,
	    0x63, 0xfe, 0xd0, 0x2e, 0xba, 0x00, 0x5b, 0x6a,
	    0xfc, 0xa2, 0x3c, 0xc9, 0xca, 0x75, 0xe4, 0x26,
	    0x2c, 0x61, 0xbf, 0x1c, 0xe1, 0xfa, 0xc3, 0xd1,
	    0x97, 0x2e, 0x6b, 0xcb, 0xe8, 0xb4, 0x1b, 0x19,
	};
	unsigned long rounds = 1000;
	unsigned long round;
	char *end = NULL;

	if (argc == 2) {
		rounds = strtoul(argv[1], &end, 10);
		if (argv[1][0] == '\0' || end == NULL || *end != '\0' ||
		    rounds == 0)
			return 2;
	} else if (argc != 1) {
		return 2;
	}
	for (round = 0; round < rounds; round++) {
		if (one_round(station, access_point, password,
		    sizeof(password) - 1, expected_pwe) != 0) {
			fprintf(stderr, "driver SAE HnP round %lu/%lu: FAIL\n",
			    round + 1, rounds);
			return 1;
		}
	}
	printf("driver SAE HnP PWE determinism and full exchange: PASS (%lu rounds)\n",
	    rounds);
	return 0;
}
