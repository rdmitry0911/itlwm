/*
 * Sealed scan-derived admission for the first pure-SAE runtime profile.
 *
 * This profile is intentionally narrower than general SAE: it permits only
 * a selected, exact RSN/SAE BSS with group 19 and hunting-and-pecking.  It
 * preserves no raw information elements and carries no credential material.
 * The caller must separately hold the selected-BSS lifetime claim and verify
 * the local PMF/IGTK owner before it starts Algorithm 3.
 */
#ifndef _NET80211_IEEE80211_SAE_ADMISSION_H_
#define _NET80211_IEEE80211_SAE_ADMISSION_H_

#include <net80211/ieee80211_pae_selected_bss.h>
#include <net80211/ieee80211_sae_policy.h>

#define IEEE80211_SAE_ADMISSION_GROUP_19 19u
#define IEEE80211_SAE_ADMISSION_METHOD_HNP 1u

/*
 * The first live profile is purposefully HnP-only.  A plain RSNXE H2E flag is
 * not an HnP proof, so it is denied until an H2E-specific Agent/backend/FSM
 * layer owns PWE method selection end to end.  Every not-yet-modeled fact is
 * already absent from this exact allowed set.
 */
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
ieee80211_sae_admission_group19_hnp(
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
	    selected->strict_pure_sae_profile == 0)
		return 0;

	flags = selected->sae_scan_flags;
	if ((flags & IEEE80211_SAE_SCAN_CENSUS_COMPLETE) == 0 ||
	    (flags & ~IEEE80211_SAE_ADMISSION_GROUP19_HNP_ALLOWED_FLAGS) != 0)
		return 0;

	out->group = IEEE80211_SAE_ADMISSION_GROUP_19;
	out->method = IEEE80211_SAE_ADMISSION_METHOD_HNP;
	return 1;
}

#endif /* _NET80211_IEEE80211_SAE_ADMISSION_H_ */
