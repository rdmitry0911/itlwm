#include <assert.h>
#include <stdint.h>

#include "ieee80211_sae_policy.h"

int
main(void)
{
    const uint8_t hnp[] = { 0x00 };
    const uint8_t h2e[] = { 0x20 };
    const uint8_t two_byte[] = { 0x01, 0x00 };
    const uint8_t unknown_first[] = { 0x40 };
    const uint8_t unknown_later[] = { 0x01, 0x01 };
    const uint8_t short_field[] = { 0x01 };
    uint8_t maximum_field[16] = { 0x0f };

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
    assert(ieee80211_sae_scan_rsnxe_flags(unknown_first,
        sizeof(unknown_first)) ==
        (IEEE80211_SAE_SCAN_RSNXE_PRESENT | IEEE80211_SAE_SCAN_UNMODELED));
    assert(ieee80211_sae_scan_rsnxe_flags(unknown_later,
        sizeof(unknown_later)) ==
        (IEEE80211_SAE_SCAN_RSNXE_PRESENT | IEEE80211_SAE_SCAN_UNMODELED));
    assert(ieee80211_sae_scan_rsnxe_flags(short_field, sizeof(short_field)) ==
        IEEE80211_SAE_SCAN_MALFORMED);
    assert(ieee80211_sae_scan_rsnxe_flags(maximum_field,
        sizeof(maximum_field)) == IEEE80211_SAE_SCAN_RSNXE_PRESENT);
    assert(!ieee80211_sae_scan_akm_is_ambiguous(1, 1, 0));
    assert(ieee80211_sae_scan_akm_is_ambiguous(2, 2, 0));
    assert(ieee80211_sae_scan_akm_is_ambiguous(2, 1, 1));
    assert(ieee80211_sae_scan_akm_is_ambiguous(1, 0, 1));
    return 0;
}
