/*
 * Sealed scan-derived admission for the bounded pure-SAE runtime profiles.
 *
 * This profile is intentionally narrower than general SAE: it permits only
 * a selected, exact RSN/SAE BSS with group 19 and either HnP or H2E as proven
 * by the normalized scan facts.  It preserves no raw information elements
 * and carries no credential material.  The caller must separately hold the
 * selected-BSS lifetime claim and verify the local PMF/IGTK owner before it
 * starts Algorithm 3.
 */
#ifndef _NET80211_IEEE80211_SAE_ADMISSION_H_
#define _NET80211_IEEE80211_SAE_ADMISSION_H_

#include <net80211/ieee80211_pae_selected_bss.h>
#include <net80211/ieee80211_sae_policy.h>

#define IEEE80211_SAE_ADMISSION_GROUP_19 19u
#define IEEE80211_SAE_ADMISSION_METHOD_HNP 1u
#define IEEE80211_SAE_ADMISSION_METHOD_H2E 2u
#define IEEE80211_SAE_ADMISSION_RSNXE_H2E 0x00000001u

#define IEEE80211_SAE_ADMISSION_GROUP19_ALLOWED_FLAGS \
	(IEEE80211_SAE_SCAN_CENSUS_COMPLETE | \
	 IEEE80211_SAE_SCAN_RSNXE_PRESENT | \
	 IEEE80211_SAE_SCAN_RSNXE_H2E | \
	 IEEE80211_SAE_SCAN_EXTCAP_PRESENT | \
	 IEEE80211_SAE_SCAN_H2E_ONLY_SELECTOR)

/* Compatibility name for contracts which still describe the HnP subset. */
#define IEEE80211_SAE_ADMISSION_GROUP19_HNP_ALLOWED_FLAGS \
	(IEEE80211_SAE_SCAN_CENSUS_COMPLETE | \
	 IEEE80211_SAE_SCAN_RSNXE_PRESENT | \
	 IEEE80211_SAE_SCAN_EXTCAP_PRESENT)

struct ieee80211_sae_admission {
	uint16_t group;
	uint16_t method;
	uint32_t rsnxe_capabilities;
};

static inline int
ieee80211_sae_admission_group19(
    const struct ieee80211_pae_selected_bss *selected,
    struct ieee80211_sae_admission *out)
{
	uint32_t flags;

	if (out != NULL) {
		out->group = 0;
		out->method = 0;
		out->rsnxe_capabilities = 0;
	}
	if (selected == NULL || out == NULL || selected->epoch == 0 ||
	    selected->strict_pure_sae_profile !=
	    IEEE80211_SAE_SELECTED_BSS_PROFILE_PURE)
		return 0;

	flags = selected->sae_scan_flags;
	if ((flags & IEEE80211_SAE_SCAN_CENSUS_COMPLETE) == 0 ||
	    (flags & ~IEEE80211_SAE_ADMISSION_GROUP19_ALLOWED_FLAGS) != 0)
		return 0;
	if ((flags & IEEE80211_SAE_SCAN_H2E_ONLY_SELECTOR) != 0 &&
	    ((flags & IEEE80211_SAE_SCAN_RSNXE_PRESENT) == 0 ||
	     (flags & IEEE80211_SAE_SCAN_RSNXE_H2E) == 0))
		return 0;

	out->group = IEEE80211_SAE_ADMISSION_GROUP_19;
	out->method = (flags & IEEE80211_SAE_SCAN_H2E_ONLY_SELECTOR) != 0 ?
	    IEEE80211_SAE_ADMISSION_METHOD_H2E :
	    IEEE80211_SAE_ADMISSION_METHOD_HNP;
	if ((flags & IEEE80211_SAE_SCAN_RSNXE_H2E) != 0)
		out->rsnxe_capabilities |= IEEE80211_SAE_ADMISSION_RSNXE_H2E;
	return 1;
}

static inline int
ieee80211_sae_admission_group19_hnp(
    const struct ieee80211_pae_selected_bss *selected,
    struct ieee80211_sae_admission *out)
{
	if (!ieee80211_sae_admission_group19(selected, out))
		return 0;
	if (out->method == IEEE80211_SAE_ADMISSION_METHOD_HNP)
		return 1;
	out->group = 0;
	out->method = 0;
	out->rsnxe_capabilities = 0;
	return 0;
}

#endif /* _NET80211_IEEE80211_SAE_ADMISSION_H_ */
