#include <assert.h>
#include <stdint.h>

#include "ieee80211_sae_policy.h"

int
main(void)
{
    const uint8_t hnp[] = { 0x00 };
    const uint8_t h2e[] = { 0x20 };
    const uint8_t sae_pk[] = { 0x40 };
    const uint8_t h2e_sae_pk[] = { 0x60 };
    const uint8_t two_byte[] = { 0x01, 0x00 };
    const uint8_t unknown_later[] = { 0x01, 0x01 };
    const uint8_t short_field[] = { 0x01 };
    const uint8_t extcap_empty[] = { 0 };
    uint8_t extcap_password_id[11] = { 0 };
    uint8_t extcap_password_id_exclusive[11] = { 0 };
    uint8_t extcap_sae_pk_exclusive[12] = { 0 };
    const uint8_t rates_h2e_only[] = { 0xfb };
    const uint8_t rates_unflagged_h2e_only[] = { 0x7b };
    const uint8_t rates_other[] = { 0x82, 0x0c };
    uint8_t maximum_field[16] = { 0x0f };

    extcap_password_id[10] = 0x02;
    extcap_password_id_exclusive[10] = 0x04;
    extcap_sae_pk_exclusive[11] = 0x01;

    assert(ieee80211_sae_scan_rsnxe_flags(NULL, 0) ==
        IEEE80211_SAE_SCAN_MALFORMED);
    assert(ieee80211_sae_scan_rsnxe_flags(NULL, 1) ==
        IEEE80211_SAE_SCAN_MALFORMED);
    assert(ieee80211_sae_scan_rsnxe_flags(hnp, sizeof(hnp)) ==
        IEEE80211_SAE_SCAN_RSNXE_PRESENT);
    assert(ieee80211_sae_scan_rsnxe_flags(h2e, sizeof(h2e)) ==
        (IEEE80211_SAE_SCAN_RSNXE_PRESENT | IEEE80211_SAE_SCAN_RSNXE_H2E));
    assert(ieee80211_sae_scan_rsnxe_flags(two_byte, sizeof(two_byte)) ==
        IEEE80211_SAE_SCAN_RSNXE_PRESENT);
    assert(ieee80211_sae_scan_rsnxe_flags(sae_pk, sizeof(sae_pk)) ==
        (IEEE80211_SAE_SCAN_RSNXE_PRESENT |
         IEEE80211_SAE_SCAN_RSNXE_SAE_PK |
         IEEE80211_SAE_SCAN_UNSUPPORTED));
    assert(ieee80211_sae_scan_rsnxe_flags(h2e_sae_pk,
        sizeof(h2e_sae_pk)) ==
        (IEEE80211_SAE_SCAN_RSNXE_PRESENT |
         IEEE80211_SAE_SCAN_RSNXE_H2E |
         IEEE80211_SAE_SCAN_RSNXE_SAE_PK |
         IEEE80211_SAE_SCAN_UNSUPPORTED));
    assert(ieee80211_sae_scan_rsnxe_flags(unknown_later,
        sizeof(unknown_later)) ==
        (IEEE80211_SAE_SCAN_RSNXE_PRESENT | IEEE80211_SAE_SCAN_UNMODELED));
    assert(ieee80211_sae_scan_rsnxe_flags(short_field, sizeof(short_field)) ==
        IEEE80211_SAE_SCAN_MALFORMED);
    assert(ieee80211_sae_scan_rsnxe_flags(maximum_field,
        sizeof(maximum_field)) == IEEE80211_SAE_SCAN_RSNXE_PRESENT);
    assert(ieee80211_sae_scan_extcap_flags(NULL, 0) == 0);
    assert(ieee80211_sae_scan_extcap_flags(extcap_empty, 0) ==
        IEEE80211_SAE_SCAN_EXTCAP_PRESENT);
    assert(ieee80211_sae_scan_extcap_flags(extcap_empty,
        sizeof(extcap_empty)) == IEEE80211_SAE_SCAN_EXTCAP_PRESENT);
    assert(ieee80211_sae_scan_extcap_flags(extcap_password_id,
        sizeof(extcap_password_id)) ==
        (IEEE80211_SAE_SCAN_EXTCAP_PRESENT |
         IEEE80211_SAE_SCAN_EXTCAP_PASSWORD_ID));
    assert(ieee80211_sae_scan_extcap_flags(extcap_password_id_exclusive,
        sizeof(extcap_password_id_exclusive)) ==
        (IEEE80211_SAE_SCAN_EXTCAP_PRESENT |
         IEEE80211_SAE_SCAN_EXTCAP_PASSWORD_ID_EXCLUSIVE));
    assert(ieee80211_sae_scan_extcap_flags(extcap_sae_pk_exclusive,
        sizeof(extcap_sae_pk_exclusive)) ==
        (IEEE80211_SAE_SCAN_EXTCAP_PRESENT |
         IEEE80211_SAE_SCAN_EXTCAP_SAE_PK_EXCLUSIVE));
    assert(ieee80211_sae_scan_has_h2e_only_selector(rates_h2e_only,
        sizeof(rates_h2e_only)));
    assert(!ieee80211_sae_scan_has_h2e_only_selector(
        rates_unflagged_h2e_only, sizeof(rates_unflagged_h2e_only)));
    assert(!ieee80211_sae_scan_has_h2e_only_selector(rates_other,
        sizeof(rates_other)));
    assert(!ieee80211_sae_scan_has_h2e_only_selector(NULL, 0));
    assert(ieee80211_sae_scan_is_extended_key_akm(
        IEEE80211_SAE_AKM_EXT_KEY));
    assert(ieee80211_sae_scan_is_extended_key_akm(
        IEEE80211_SAE_AKM_FT_EXT_KEY));
    assert(!ieee80211_sae_scan_is_extended_key_akm(8));
    assert(ieee80211_sae_scan_finalize_flags(
        IEEE80211_SAE_SCAN_EXTCAP_PASSWORD_ID_EXCLUSIVE) &
        IEEE80211_SAE_SCAN_PROFILE_INCONSISTENT);
    assert(ieee80211_sae_scan_finalize_flags(
        IEEE80211_SAE_SCAN_EXTCAP_SAE_PK_EXCLUSIVE) &
        IEEE80211_SAE_SCAN_PROFILE_INCONSISTENT);
    assert(ieee80211_sae_scan_finalize_flags(
        IEEE80211_SAE_SCAN_H2E_ONLY_SELECTOR) &
        IEEE80211_SAE_SCAN_PROFILE_INCONSISTENT);
    assert(ieee80211_sae_scan_finalize_flags(
        IEEE80211_SAE_SCAN_SAE_EXT_KEY) & IEEE80211_SAE_SCAN_UNSUPPORTED);
    assert((ieee80211_sae_scan_finalize_flags(
        IEEE80211_SAE_SCAN_EXTCAP_PASSWORD_ID |
        IEEE80211_SAE_SCAN_EXTCAP_PASSWORD_ID_EXCLUSIVE |
        IEEE80211_SAE_SCAN_RSNXE_H2E |
        IEEE80211_SAE_SCAN_H2E_ONLY_SELECTOR |
        IEEE80211_SAE_SCAN_RSNXE_SAE_PK |
        IEEE80211_SAE_SCAN_EXTCAP_SAE_PK_EXCLUSIVE) &
        IEEE80211_SAE_SCAN_PROFILE_INCONSISTENT) == 0);
    assert(!ieee80211_sae_scan_akm_is_ambiguous(1, 1, 0));
    assert(ieee80211_sae_scan_akm_is_ambiguous(2, 2, 0));
    assert(ieee80211_sae_scan_akm_is_ambiguous(2, 1, 1));
    assert(ieee80211_sae_scan_akm_is_ambiguous(1, 0, 1));
    return 0;
}
