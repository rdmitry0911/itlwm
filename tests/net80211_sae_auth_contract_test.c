#include <assert.h>
#include <stdint.h>

#include "ieee80211_sae_auth_contract.h"

int
main(void)
{
	assert(IEEE80211_AUTH_ALG_SAE == 0x0003u);
	assert(IEEE80211_SAE_AUTH_TRANSACTION_COMMIT == 1u);
	assert(IEEE80211_SAE_AUTH_TRANSACTION_CONFIRM == 2u);

	assert(ieee80211_sae_auth_state_from_fixed_fields(
	    IEEE80211_AUTH_ALG_SAE,
	    IEEE80211_SAE_AUTH_TRANSACTION_COMMIT) ==
	    IEEE80211_SAE_AUTH_STATE_COMMIT);
	assert(ieee80211_sae_auth_state_from_fixed_fields(
	    IEEE80211_AUTH_ALG_SAE,
	    IEEE80211_SAE_AUTH_TRANSACTION_CONFIRM) ==
	    IEEE80211_SAE_AUTH_STATE_CONFIRM);

	assert(ieee80211_sae_auth_state_from_fixed_fields(
	    IEEE80211_AUTH_ALG_SAE, 0u) ==
	    IEEE80211_SAE_AUTH_STATE_INVALID);
	assert(ieee80211_sae_auth_state_from_fixed_fields(
	    IEEE80211_AUTH_ALG_SAE, 3u) ==
	    IEEE80211_SAE_AUTH_STATE_INVALID);
	assert(ieee80211_sae_auth_state_from_fixed_fields(0x0000u, 1u) ==
	    IEEE80211_SAE_AUTH_STATE_INVALID);
	assert(ieee80211_sae_auth_state_from_fixed_fields(0x0001u, 2u) ==
	    IEEE80211_SAE_AUTH_STATE_INVALID);
	assert(ieee80211_sae_auth_state_from_fixed_fields(0x0080u, 1u) ==
	    IEEE80211_SAE_AUTH_STATE_INVALID);

	return 0;
}
