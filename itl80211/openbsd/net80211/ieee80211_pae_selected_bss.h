/*
 * Fixed-byte selected-BSS identity for a future asynchronous PAE/SAE owner.
 *
 * This record is deliberately not a node pointer and does not retain raw IEs,
 * keys, or credential material.  Its epoch is published by net80211 only
 * after the fixed fields are populated; zero means invalid.  This header
 * supplies byte-level helpers only.  A future cross-context consumer must
 * hold an explicit HAL/lifecycle lifetime claim and use the serialized
 * copy-out contract rather than read the live ieee80211com field directly.
 */
#ifndef _NET80211_IEEE80211_PAE_SELECTED_BSS_H_
#define _NET80211_IEEE80211_PAE_SELECTED_BSS_H_

#include <stddef.h>
#include <stdint.h>

#define IEEE80211_PAE_SELECTED_BSS_BSSID_LEN	6u
#define IEEE80211_PAE_SELECTED_BSS_MAX_SSID_LEN	32u

/*
 * A scan-derived AP classification.  It never supplies local SAE intent;
 * the transition value is usable only when a later exact WCL SAE request
 * binds to the same selected BSS.
 */
enum ieee80211_sae_selected_bss_profile {
	IEEE80211_SAE_SELECTED_BSS_PROFILE_NONE = 0,
	IEEE80211_SAE_SELECTED_BSS_PROFILE_PURE = 1,
	IEEE80211_SAE_SELECTED_BSS_PROFILE_TRANSITION = 2,
};

static inline int
ieee80211_sae_selected_bss_profile_is_valid(uint8_t profile)
{
	return profile == IEEE80211_SAE_SELECTED_BSS_PROFILE_NONE ||
	    profile == IEEE80211_SAE_SELECTED_BSS_PROFILE_PURE ||
	    profile == IEEE80211_SAE_SELECTED_BSS_PROFILE_TRANSITION;
}

struct ieee80211_pae_selected_bss {
	uint64_t epoch;
	uint32_t sae_scan_flags;
	uint8_t bssid[IEEE80211_PAE_SELECTED_BSS_BSSID_LEN];
	uint8_t ssid_len;
	uint8_t ssid[IEEE80211_PAE_SELECTED_BSS_MAX_SSID_LEN];
	/* Historical member name is retained for the fixed layout; its value is
	 * now one enum ieee80211_sae_selected_bss_profile. */
	uint8_t strict_pure_sae_profile;
	uint8_t reserved[3];
};

static inline void
ieee80211_pae_selected_bss_clear_payload(
    struct ieee80211_pae_selected_bss *snapshot)
{
	size_t index;

	if (snapshot == NULL)
		return;
	snapshot->sae_scan_flags = 0;
	for (index = 0; index < IEEE80211_PAE_SELECTED_BSS_BSSID_LEN;
	    index++)
		snapshot->bssid[index] = 0;
	snapshot->ssid_len = 0;
	for (index = 0; index < IEEE80211_PAE_SELECTED_BSS_MAX_SSID_LEN;
	    index++)
		snapshot->ssid[index] = 0;
	snapshot->strict_pure_sae_profile =
	    IEEE80211_SAE_SELECTED_BSS_PROFILE_NONE;
	for (index = 0; index < sizeof(snapshot->reserved); index++)
		snapshot->reserved[index] = 0;
}

/* Populate fixed fields only; the net80211 owner publishes epoch separately. */
static inline int
ieee80211_pae_selected_bss_populate(struct ieee80211_pae_selected_bss *snapshot,
    const uint8_t *bssid, const uint8_t *ssid, size_t ssid_len,
    uint32_t sae_scan_flags, uint8_t sae_profile)
{
	size_t index;

	if (snapshot == NULL)
		return 0;
	ieee80211_pae_selected_bss_clear_payload(snapshot);
	if (bssid == NULL || (ssid == NULL && ssid_len != 0) ||
	    ssid_len > IEEE80211_PAE_SELECTED_BSS_MAX_SSID_LEN ||
	    !ieee80211_sae_selected_bss_profile_is_valid(sae_profile))
		return 0;
	for (index = 0; index < IEEE80211_PAE_SELECTED_BSS_BSSID_LEN;
	    index++)
		snapshot->bssid[index] = bssid[index];
	for (index = 0; index < ssid_len; index++)
		snapshot->ssid[index] = ssid[index];
	snapshot->ssid_len = (uint8_t)ssid_len;
	snapshot->sae_scan_flags = sae_scan_flags;
	snapshot->strict_pure_sae_profile = sae_profile;
	return 1;
}

/* Pure identity predicate used by tests and a future serialized consumer. */
static inline int
ieee80211_pae_selected_bss_identity_matches(
    const struct ieee80211_pae_selected_bss *snapshot,
    uint64_t expected_epoch, const uint8_t *bssid, const uint8_t *ssid,
    size_t ssid_len)
{
	size_t index;

	if (snapshot == NULL || expected_epoch == 0 || bssid == NULL ||
	    (ssid == NULL && ssid_len != 0) ||
	    ssid_len > IEEE80211_PAE_SELECTED_BSS_MAX_SSID_LEN ||
	    snapshot->epoch != expected_epoch || snapshot->ssid_len != ssid_len)
		return 0;
	for (index = 0; index < IEEE80211_PAE_SELECTED_BSS_BSSID_LEN;
	    index++) {
		if (snapshot->bssid[index] != bssid[index])
			return 0;
	}
	for (index = 0; index < ssid_len; index++) {
		if (snapshot->ssid[index] != ssid[index])
			return 0;
	}
	return 1;
}

#endif /* _NET80211_IEEE80211_PAE_SELECTED_BSS_H_ */
