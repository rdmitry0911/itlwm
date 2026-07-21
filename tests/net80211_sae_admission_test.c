#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "ieee80211_sae_admission.h"

static struct ieee80211_pae_selected_bss
valid_selected(void)
{
	struct ieee80211_pae_selected_bss selected;

	memset(&selected, 0, sizeof(selected));
	selected.epoch = 1;
	selected.strict_pure_sae_profile = 1;
	selected.sae_scan_flags = IEEE80211_SAE_SCAN_CENSUS_COMPLETE;
	return selected;
}

static void
assert_rejected(struct ieee80211_pae_selected_bss selected)
{
	struct ieee80211_sae_admission admission;

	admission.group = 99;
	admission.method = 99;
	admission.rsnxe_capabilities = 99;
	assert(ieee80211_sae_admission_group19_hnp(&selected, &admission) == 0);
	assert(admission.group == 0);
	assert(admission.method == 0);
	assert(admission.rsnxe_capabilities == 0);
}

int
main(void)
{
	struct ieee80211_pae_selected_bss selected;
	struct ieee80211_sae_admission admission;

	selected = valid_selected();
	assert(ieee80211_sae_admission_group19_hnp(&selected, &admission) == 1);
	assert(admission.group == IEEE80211_SAE_ADMISSION_GROUP_19);
	assert(admission.method == IEEE80211_SAE_ADMISSION_METHOD_HNP);
	assert(admission.rsnxe_capabilities == 0);

	selected = valid_selected();
	selected.sae_scan_flags |= IEEE80211_SAE_SCAN_RSNXE_PRESENT;
	assert(ieee80211_sae_admission_group19_hnp(&selected, &admission) == 1);

	selected = valid_selected();
	selected.sae_scan_flags |= IEEE80211_SAE_SCAN_EXTCAP_PRESENT;
	assert(ieee80211_sae_admission_group19_hnp(&selected, &admission) == 1);

	selected = valid_selected();
	selected.epoch = 0;
	assert_rejected(selected);
	selected = valid_selected();
	selected.strict_pure_sae_profile = 0;
	assert_rejected(selected);
	selected = valid_selected();
	selected.sae_scan_flags = 0;
	assert_rejected(selected);

	for (uint32_t flag = 1; flag != 0; flag <<= 1) {
		if ((flag & IEEE80211_SAE_ADMISSION_GROUP19_HNP_ALLOWED_FLAGS) != 0)
			continue;
		selected = valid_selected();
		selected.sae_scan_flags |= flag;
		assert_rejected(selected);
	}

	return 0;
}
