/*
 * Minimal host-order taxonomy for IEEE 802.11 SAE Authentication frames.
 *
 * This declaration is deliberately self-contained so C and C++ unit tests
 * can pin the wire-defined Algorithm-3 and transaction values without
 * pulling in the net80211 runtime.  It receives already decoded host-order
 * fixed fields only.  It is not an SAE state machine: it does not interpret
 * status, body, pointers, credentials, group selection, retries,
 * anti-clogging, epochs, or authorization.
 */
#ifndef _NET80211_IEEE80211_SAE_AUTH_CONTRACT_H_
#define _NET80211_IEEE80211_SAE_AUTH_CONTRACT_H_

#include <stdint.h>

/* IEEE 802.11 Authentication Algorithm Number: SAE (Algorithm 3). */
#define IEEE80211_AUTH_ALG_SAE 0x0003u

/* SAE Authentication Transaction Sequence Number fixed-field values. */
enum ieee80211_sae_auth_transaction {
	IEEE80211_SAE_AUTH_TRANSACTION_COMMIT = 1u,
	IEEE80211_SAE_AUTH_TRANSACTION_CONFIRM = 2u,
};

/*
 * A taxonomy result, not an exchange-progress state.  INVALID is returned
 * both for a non-SAE algorithm and for an SAE transaction this declaration
 * does not classify; a future owner must define any additional semantics.
 */
enum ieee80211_sae_auth_state {
	IEEE80211_SAE_AUTH_STATE_INVALID = 0,
	IEEE80211_SAE_AUTH_STATE_COMMIT,
	IEEE80211_SAE_AUTH_STATE_CONFIRM,
};

static inline enum ieee80211_sae_auth_state
ieee80211_sae_auth_state_from_fixed_fields(uint16_t algorithm,
    uint16_t transaction)
{
	if (algorithm != IEEE80211_AUTH_ALG_SAE)
		return IEEE80211_SAE_AUTH_STATE_INVALID;

	if (transaction == IEEE80211_SAE_AUTH_TRANSACTION_COMMIT)
		return IEEE80211_SAE_AUTH_STATE_COMMIT;
	if (transaction == IEEE80211_SAE_AUTH_TRANSACTION_CONFIRM)
		return IEEE80211_SAE_AUTH_STATE_CONFIRM;

	return IEEE80211_SAE_AUTH_STATE_INVALID;
}

#endif /* _NET80211_IEEE80211_SAE_AUTH_CONTRACT_H_ */
