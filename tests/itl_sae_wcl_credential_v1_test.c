#include <assert.h>
#include <string.h>

#include <HAL/ItlSaeWclCredentialV1.h>

static void
fill_valid_credential(struct ItlSaeWclCredentialV1 *credential)
{
    static const uint8_t ssid[] = { 't', 'e', 's', 't' };
    static const uint8_t password[] = {
        'p', 'a', 's', 's', 'w', '0', 'r', 'd'
    };

    memset(credential, 0, sizeof(*credential));
    credential->version = kItlSaeWclCredentialV1Version;
    credential->size = sizeof(*credential);
    credential->request_generation = 1;
    credential->password_len = sizeof(password);
    credential->ssid_len = sizeof(ssid);
    credential->bssid[0] = 0x02;
    credential->bssid[5] = 0x01;
    memcpy(credential->ssid, ssid, sizeof(ssid));
    memcpy(credential->password, password, sizeof(password));
}

static void
assert_credential_rejected(const struct ItlSaeWclCredentialV1 *credential)
{
    assert(!itl_sae_wcl_credential_is_well_formed(credential));
}

static void
test_zero_and_bssid_helpers(void)
{
    uint8_t zeroes[3] = { 0, 0, 0 };
    uint8_t nonzeroes[3] = { 0, 1, 0 };
    uint8_t zero_bssid[kItlSaeAuthTransportV1MacLength] = { 0 };
    uint8_t unicast[kItlSaeAuthTransportV1MacLength] = {
        0x02, 0, 0, 0, 0, 1
    };
    uint8_t multicast[kItlSaeAuthTransportV1MacLength] = {
        0x03, 0, 0, 0, 0, 1
    };

    assert(itl_sae_wcl_credential_bytes_all_zero(NULL, 0));
    assert(itl_sae_wcl_credential_bytes_all_zero(NULL, sizeof(zeroes)));
    assert(itl_sae_wcl_credential_bytes_all_zero(zeroes, sizeof(zeroes)));
    assert(!itl_sae_wcl_credential_bytes_all_zero(nonzeroes,
        sizeof(nonzeroes)));

    assert(!itl_sae_wcl_credential_bssid_is_unicast_nonzero(NULL));
    assert(!itl_sae_wcl_credential_bssid_is_unicast_nonzero(zero_bssid));
    assert(!itl_sae_wcl_credential_bssid_is_unicast_nonzero(multicast));
    assert(itl_sae_wcl_credential_bssid_is_unicast_nonzero(unicast));
}

static void
test_valid_boundaries(void)
{
    struct ItlSaeWclCredentialV1 credential;

    fill_valid_credential(&credential);
    credential.password_len = kItlSaeWclCredentialV1PassphraseMinLength;
    assert(itl_sae_wcl_credential_is_well_formed(&credential));

    fill_valid_credential(&credential);
    credential.password_len = kItlSaeWclCredentialV1PassphraseMaxLength;
    memset(credential.password, 'x', credential.password_len);
    assert(itl_sae_wcl_credential_is_well_formed(&credential));

    fill_valid_credential(&credential);
    credential.ssid_len = kItlSaeWclCredentialV1SsidMaxLength;
    memset(credential.ssid, 's', credential.ssid_len);
    assert(itl_sae_wcl_credential_is_well_formed(&credential));
}

static void
test_fixed_field_rejections(void)
{
    struct ItlSaeWclCredentialV1 credential;

    assert_credential_rejected(NULL);

    fill_valid_credential(&credential);
    credential.version++;
    assert_credential_rejected(&credential);

    fill_valid_credential(&credential);
    credential.size--;
    assert_credential_rejected(&credential);

    fill_valid_credential(&credential);
    credential.request_generation = 0;
    assert_credential_rejected(&credential);

    fill_valid_credential(&credential);
    credential.ssid_len = 0;
    assert_credential_rejected(&credential);

    fill_valid_credential(&credential);
    credential.ssid_len = kItlSaeWclCredentialV1SsidMaxLength + 1u;
    assert_credential_rejected(&credential);

    fill_valid_credential(&credential);
    credential.password_len =
        kItlSaeWclCredentialV1PassphraseMinLength - 1u;
    assert_credential_rejected(&credential);

    fill_valid_credential(&credential);
    credential.password_len =
        kItlSaeWclCredentialV1PassphraseMaxLength + 1u;
    assert_credential_rejected(&credential);
}

static void
test_identity_and_reserved_rejections(void)
{
    struct ItlSaeWclCredentialV1 credential;

    fill_valid_credential(&credential);
    memset(credential.bssid, 0, sizeof(credential.bssid));
    assert_credential_rejected(&credential);

    fill_valid_credential(&credential);
    credential.bssid[0] |= 0x01u;
    assert_credential_rejected(&credential);

    fill_valid_credential(&credential);
    credential.reserved0[sizeof(credential.reserved0) - 1u] = 1;
    assert_credential_rejected(&credential);

    fill_valid_credential(&credential);
    credential.reserved1[sizeof(credential.reserved1) - 1u] = 1;
    assert_credential_rejected(&credential);

    fill_valid_credential(&credential);
    credential.reserved[sizeof(credential.reserved) - 1u] = 1;
    assert_credential_rejected(&credential);
}

static void
test_ssid_and_password_tail_scrub_requirements(void)
{
    struct ItlSaeWclCredentialV1 credential;

    fill_valid_credential(&credential);
    credential.ssid[credential.ssid_len] = 1;
    assert_credential_rejected(&credential);

    fill_valid_credential(&credential);
    credential.password[credential.password_len] = 1;
    assert_credential_rejected(&credential);

    fill_valid_credential(&credential);
    credential.ssid_len = kItlSaeWclCredentialV1SsidMaxLength;
    memset(credential.ssid, 's', credential.ssid_len);
    credential.password_len = kItlSaeWclCredentialV1PassphraseMaxLength;
    memset(credential.password, 'p', credential.password_len);
    assert(itl_sae_wcl_credential_is_well_formed(&credential));

    credential.password[credential.password_len] = 1;
    assert_credential_rejected(&credential);
}

int
main(void)
{
    test_zero_and_bssid_helpers();
    test_valid_boundaries();
    test_fixed_field_rejections();
    test_identity_and_reserved_rejections();
    test_ssid_and_password_tail_scrub_requirements();
    return 0;
}
