/*
 * Driver-owned SAE group-19/HnP session core.
 *
 * The implementation intentionally has no controller, userland credential
 * store, socket, log, or firmware dependency.  It has one caller serialization
 * requirement: all begin/TX-completion/peer/cancel operations for a session
 * run on the driver SAE worker.  That worker is introduced with the IWX
 * backend wiring; this core provides the crypto and causal frame state.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <sys/param.h>
#include <sys/systm.h>

#include <net80211/ieee80211_sae_engine.h>
#include <net80211/ieee80211_sae_platform.h>

#include "utils/includes.h"
#include "utils/common.h"
#include "utils/wpabuf.h"
#include "common/defs.h"
#include "common/ieee802_11_defs.h"
#include "common/sae.h"
#include "crypto/sha256.h"

enum ieee80211_sae_engine_state {
	IEEE80211_SAE_ENGINE_COMMIT_PREPARED = 1,
	IEEE80211_SAE_ENGINE_COMMIT_IN_FLIGHT,
	IEEE80211_SAE_ENGINE_COMMIT_SENT,
	IEEE80211_SAE_ENGINE_CONFIRM_PREPARED,
	IEEE80211_SAE_ENGINE_CONFIRM_IN_FLIGHT,
	IEEE80211_SAE_ENGINE_CONFIRM_SENT,
	IEEE80211_SAE_ENGINE_COMPLETE,
	IEEE80211_SAE_ENGINE_FAILED,
};

struct ieee80211_sae_engine {
	struct sae_data sae;
	uint64_t request_generation;
	uint64_t association_epoch;
	uint64_t relay_generation;
	uint64_t completion_sequence;
	uint8_t ssid_len;
	uint8_t ssid[sizeof(((struct ItlSaeSelectedJoinEventV1 *)0)->ssid)];
	uint8_t bssid[kItlSaeAuthTransportV1MacLength];
	uint8_t sta[kItlSaeAuthTransportV1MacLength];
	enum ieee80211_sae_engine_state state;
	uint8_t anti_clogging_retries;
	uint16_t prepared_phase;
	uint32_t prepared_body_len;
	uint8_t prepared_body[kItlSaeAuthTransportV1MaxBodyLength];
	struct ItlSaeAuthTxRequestV1 in_flight;
};

static void
ieee80211_sae_engine_clear_prepared(struct ieee80211_sae_engine *engine)
{
	if (engine == NULL)
		return;
	engine->prepared_phase = 0;
	engine->prepared_body_len = 0;
	ieee80211_sae_secure_zero(engine->prepared_body,
	    sizeof(engine->prepared_body));
}

static void
ieee80211_sae_engine_clear_in_flight(struct ieee80211_sae_engine *engine)
{
	if (engine != NULL)
		ieee80211_sae_secure_zero(&engine->in_flight,
		    sizeof(engine->in_flight));
}

static void
ieee80211_sae_engine_clear_crypto(struct ieee80211_sae_engine *engine)
{
	if (engine != NULL)
		sae_clear_data(&engine->sae);
}

static void
ieee80211_sae_engine_fail(struct ieee80211_sae_engine *engine)
{
	if (engine == NULL)
		return;
	ieee80211_sae_engine_clear_crypto(engine);
	ieee80211_sae_engine_clear_prepared(engine);
	ieee80211_sae_engine_clear_in_flight(engine);
	engine->state = IEEE80211_SAE_ENGINE_FAILED;
}

/*
 * IEEE 802.11's SHA-256 PMK Name construction for SAE.  The legacy
 * net80211 implementation has the same HMAC-SHA256("PMK Name" || AA || SPA)
 * formula, but is compiled as C++ in the Tahoe kext and therefore exports a
 * mangled symbol.  Keep the archive's C boundary self-contained instead of
 * relying on an ABI-unsafe C-to-C++ external call.
 */
static int
ieee80211_sae_engine_derive_rsn_pmkid(const uint8_t *pmk,
	const uint8_t *authenticator, const uint8_t *supplicant,
	uint8_t *pmkid)
{
	static const uint8_t label[] = "PMK Name";
	const u8 *address[3];
	size_t length[3];
	uint8_t digest[32];
	int result;

	if (pmk == NULL || authenticator == NULL || supplicant == NULL ||
	    pmkid == NULL)
		return -1;
	address[0] = label;
	address[1] = authenticator;
	address[2] = supplicant;
	length[0] = sizeof(label) - 1;
	length[1] = kItlSaeAuthTransportV1MacLength;
	length[2] = kItlSaeAuthTransportV1MacLength;
	result = hmac_sha256_vector(pmk,
	    sizeof(((struct ItlSaePmkContinuationV1 *)0)->pmk),
	    sizeof(address) / sizeof(address[0]), address, length, digest);
	if (result == 0)
		os_memcpy(pmkid, digest,
		    sizeof(((struct ItlSaePmkContinuationV1 *)0)->pmkid));
	ieee80211_sae_secure_zero(digest, sizeof(digest));
	return result;
}

static int
ieee80211_sae_engine_selected_matches_active(
	const struct ItlSaeSelectedJoinEventV1 *selected,
	const struct ItlSaeAuthActivatedEventV1 *activated)
{
	size_t index;

	if (!itl_sae_selected_join_event_is_well_formed(selected) ||
	    !itl_sae_auth_activated_event_is_well_formed(activated) ||
	    selected->request_generation != activated->request_generation ||
	    selected->association_epoch != activated->association_epoch ||
	    memcmp(selected->bssid, activated->bssid,
		sizeof(selected->bssid)) != 0 ||
	    memcmp(selected->sta, activated->sta, sizeof(selected->sta)) != 0)
		return 0;
	for (index = selected->ssid_len; index < sizeof(selected->ssid); index++)
		if (selected->ssid[index] != 0)
			return 0;
	return selected->sae_group == IEEE80211_SAE_ENGINE_GROUP19 &&
	    selected->sae_method == IEEE80211_SAE_ENGINE_HNP_METHOD;
}

static int
ieee80211_sae_engine_set_prepared(struct ieee80211_sae_engine *engine,
	uint16_t phase, const struct wpabuf *serialized, size_t exact_length)
{
	size_t length;

	if (engine == NULL || serialized == NULL ||
	    (phase != kItlSaeAuthTransportPhaseCommit &&
	     phase != kItlSaeAuthTransportPhaseConfirm))
		return -1;
	length = wpabuf_len(serialized);
	if (length != exact_length ||
	    length > sizeof(engine->prepared_body))
		return -1;
	ieee80211_sae_engine_clear_prepared(engine);
	engine->prepared_phase = phase;
	engine->prepared_body_len = (uint32_t)length;
	os_memcpy(engine->prepared_body, wpabuf_head(serialized), length);
	return 0;
}

static int
ieee80211_sae_engine_build_commit(struct ieee80211_sae_engine *engine,
	const uint8_t *token, size_t token_len)
{
	struct wpabuf *serialized = NULL;
	struct wpabuf *token_buffer = NULL;
	size_t expected_length;
	int rc = -1;

	if (engine == NULL || (token == NULL && token_len != 0))
		return -1;
	if (token_len != 0) {
		if (token_len > IEEE80211_SAE_ENGINE_ANTI_CLOGGING_TOKEN_MAX)
			return -1;
		token_buffer = wpabuf_alloc_copy(token, token_len);
		if (token_buffer == NULL)
			goto out;
	}
	serialized = wpabuf_alloc(kItlSaeAuthTransportV1MaxBodyLength);
	if (serialized == NULL ||
	    sae_write_commit(&engine->sae, serialized, token_buffer, NULL, 0) !=
		0)
		goto out;
	expected_length = IEEE80211_SAE_ENGINE_HNP_COMMIT_BODY_LEN + token_len;
	rc = ieee80211_sae_engine_set_prepared(engine,
	    kItlSaeAuthTransportPhaseCommit, serialized, expected_length);
out:
	wpabuf_clear_free(serialized);
	wpabuf_clear_free(token_buffer);
	return rc;
}

static int
ieee80211_sae_engine_build_confirm(struct ieee80211_sae_engine *engine)
{
	struct wpabuf *serialized;
	int rc;

	if (engine == NULL)
		return -1;
	serialized = wpabuf_alloc(IEEE80211_SAE_ENGINE_CONFIRM_BODY_LEN);
	if (serialized == NULL)
		return -1;
	rc = sae_write_confirm(&engine->sae, serialized) == 0 ?
	    ieee80211_sae_engine_set_prepared(engine,
		kItlSaeAuthTransportPhaseConfirm, serialized,
		IEEE80211_SAE_ENGINE_CONFIRM_BODY_LEN) : -1;
	wpabuf_clear_free(serialized);
	return rc;
}

int
ieee80211_sae_engine_begin_hnp(
	const struct ItlSaeSelectedJoinEventV1 *selected,
	const struct ItlSaeAuthActivatedEventV1 *activated,
	const uint8_t *password, size_t password_len,
	struct ieee80211_sae_engine **out_engine)
{
	struct ieee80211_sae_engine *engine;

	if (out_engine == NULL)
		return -1;
	*out_engine = NULL;
	if (password == NULL ||
	    password_len < IEEE80211_SAE_ENGINE_PASSPHRASE_MIN ||
	    password_len > IEEE80211_SAE_ENGINE_PASSPHRASE_MAX ||
	    !ieee80211_sae_engine_selected_matches_active(selected, activated))
		return -1;
	engine = os_zalloc(sizeof(*engine));
	if (engine == NULL)
		return -1;
	engine->request_generation = selected->request_generation;
	engine->association_epoch = selected->association_epoch;
	engine->relay_generation = activated->relay_generation;
	engine->ssid_len = selected->ssid_len;
	os_memcpy(engine->ssid, selected->ssid, sizeof(engine->ssid));
	os_memcpy(engine->bssid, selected->bssid, sizeof(engine->bssid));
	os_memcpy(engine->sta, selected->sta, sizeof(engine->sta));
	engine->sae.no_pw_id = 1;
	if (sae_set_group(&engine->sae, IEEE80211_SAE_ENGINE_GROUP19) != 0)
		goto fail;
	engine->sae.no_pw_id = 1;
	engine->sae.akmp = WPA_KEY_MGMT_SAE;
	if (sae_prepare_commit(engine->sta, engine->bssid, password, password_len,
	    &engine->sae) != 0 ||
	    engine->sae.group != IEEE80211_SAE_ENGINE_GROUP19 ||
	    engine->sae.h2e != 0 ||
	    ieee80211_sae_engine_build_commit(engine, NULL, 0) != 0)
		goto fail;
	engine->state = IEEE80211_SAE_ENGINE_COMMIT_PREPARED;
	*out_engine = engine;
	return 0;
fail:
	ieee80211_sae_engine_destroy(&engine);
	return -1;
}

int
ieee80211_sae_engine_prepare_tx(struct ieee80211_sae_engine *engine,
	uint64_t ticket, struct ItlSaeAuthTxRequestV1 *out)
{
	if (engine == NULL || out == NULL || ticket == 0 ||
	    (engine->state != IEEE80211_SAE_ENGINE_COMMIT_PREPARED &&
	     engine->state != IEEE80211_SAE_ENGINE_CONFIRM_PREPARED) ||
	    engine->prepared_body_len == 0 ||
	    engine->prepared_body_len > sizeof(engine->prepared_body))
		return -1;
	ieee80211_sae_secure_zero(out, sizeof(*out));
	out->version = kItlSaeAuthTransportV1Version;
	out->size = sizeof(*out);
	out->association_epoch = engine->association_epoch;
	out->relay_generation = engine->relay_generation;
	out->ticket = ticket;
	out->phase = engine->prepared_phase;
	out->wire_transaction =
	    itl_sae_auth_transport_sta_wire_transaction_for_phase(out->phase);
	out->body_len = engine->prepared_body_len;
	os_memcpy(out->bssid, engine->bssid, sizeof(out->bssid));
	os_memcpy(out->sta, engine->sta, sizeof(out->sta));
	os_memcpy(out->body, engine->prepared_body, out->body_len);
	if (!itl_sae_auth_transport_request_is_well_formed(out)) {
		ieee80211_sae_secure_zero(out, sizeof(*out));
		ieee80211_sae_engine_fail(engine);
		return -1;
	}
	engine->in_flight = *out;
	if (engine->state == IEEE80211_SAE_ENGINE_COMMIT_PREPARED)
		engine->state = IEEE80211_SAE_ENGINE_COMMIT_IN_FLIGHT;
	else
		engine->state = IEEE80211_SAE_ENGINE_CONFIRM_IN_FLIGHT;
	return 0;
}

/*
 * An IWX workloop admission failure can happen after prepare_tx() but before
 * any descriptor doorbell.  That is not a terminal Authentication result:
 * restore exactly the prepared state and retain the crypto/body so the owner
 * can retry with a new monotonic ticket.  No terminal event may use this
 * path; those must go through tx_complete() and fail the core on error.
 */
int
ieee80211_sae_engine_tx_rollback_unsubmitted(
	struct ieee80211_sae_engine *engine, uint64_t ticket)
{
	if (engine == NULL || ticket == 0 ||
	    (engine->state != IEEE80211_SAE_ENGINE_COMMIT_IN_FLIGHT &&
	     engine->state != IEEE80211_SAE_ENGINE_CONFIRM_IN_FLIGHT) ||
	    engine->in_flight.ticket != ticket ||
	    (engine->state == IEEE80211_SAE_ENGINE_COMMIT_IN_FLIGHT &&
	     engine->in_flight.phase != kItlSaeAuthTransportPhaseCommit) ||
	    (engine->state == IEEE80211_SAE_ENGINE_CONFIRM_IN_FLIGHT &&
	     engine->in_flight.phase != kItlSaeAuthTransportPhaseConfirm) ||
	    engine->prepared_body_len == 0 ||
	    engine->prepared_body_len > sizeof(engine->prepared_body))
		return -1;
	if (engine->state == IEEE80211_SAE_ENGINE_COMMIT_IN_FLIGHT)
		engine->state = IEEE80211_SAE_ENGINE_COMMIT_PREPARED;
	else
		engine->state = IEEE80211_SAE_ENGINE_CONFIRM_PREPARED;
	ieee80211_sae_engine_clear_in_flight(engine);
	return 0;
}

int
ieee80211_sae_engine_tx_complete(struct ieee80211_sae_engine *engine,
	const struct ItlSaeAuthTransportEventV1 *event)
{
	if (engine == NULL ||
	    (engine->state != IEEE80211_SAE_ENGINE_COMMIT_IN_FLIGHT &&
	     engine->state != IEEE80211_SAE_ENGINE_CONFIRM_IN_FLIGHT) ||
	    !itl_sae_auth_transport_event_matches_request(event,
		&engine->in_flight))
		return -1;
	if (event->result != 0) {
		ieee80211_sae_engine_fail(engine);
		return -1;
	}
	if (engine->state == IEEE80211_SAE_ENGINE_COMMIT_IN_FLIGHT) {
		engine->sae.state = SAE_COMMITTED;
		engine->state = IEEE80211_SAE_ENGINE_COMMIT_SENT;
	} else {
		engine->sae.state = SAE_CONFIRMED;
		engine->state = IEEE80211_SAE_ENGINE_CONFIRM_SENT;
	}
	ieee80211_sae_engine_clear_in_flight(engine);
	ieee80211_sae_engine_clear_prepared(engine);
	return 0;
}

static int
ieee80211_sae_engine_event_matches(
	const struct ieee80211_sae_engine *engine,
	const struct ItlSaeAuthPeerEventV1 *event)
{
	return engine != NULL && itl_sae_auth_peer_event_is_well_formed(event) &&
	    event->association_epoch == engine->association_epoch &&
	    event->relay_generation == engine->relay_generation &&
	    memcmp(event->bssid, engine->bssid, sizeof(engine->bssid)) == 0 &&
	    memcmp(event->sta, engine->sta, sizeof(engine->sta)) == 0;
}

static int
ieee80211_sae_engine_handle_anti_clogging(
	struct ieee80211_sae_engine *engine,
	const struct ItlSaeAuthPeerEventV1 *event)
{
	const uint8_t *token;

	if (engine == NULL || event == NULL ||
	    engine->anti_clogging_retries != 0 ||
	    event->body_len <= 2 ||
	    event->body_len > 2 + IEEE80211_SAE_ENGINE_ANTI_CLOGGING_TOKEN_MAX ||
	    event->body[0] != (IEEE80211_SAE_ENGINE_GROUP19 & 0xffu) ||
	    event->body[1] != (IEEE80211_SAE_ENGINE_GROUP19 >> 8))
		return -1;
	token = event->body + 2;
	if (ieee80211_sae_engine_build_commit(engine, token,
	    event->body_len - 2) != 0)
		return -1;
	engine->anti_clogging_retries = 1;
	engine->state = IEEE80211_SAE_ENGINE_COMMIT_PREPARED;
	return 0;
}

static enum ieee80211_sae_engine_peer_result
ieee80211_sae_engine_handle_commit(struct ieee80211_sae_engine *engine,
	const struct ItlSaeAuthPeerEventV1 *event)
{
	static int allowed_groups[] = { IEEE80211_SAE_ENGINE_GROUP19, 0 };
	const uint8_t *peer_token = NULL;
	size_t peer_token_len = 0;
	int ie_offset = 0;
	uint16_t parsed;

	if (event->phase != kItlSaeAuthTransportPhaseCommit ||
	    event->wire_transaction !=
		kItlSaeAuthTransportPeerWireTransactionCommit)
		return IEEE80211_SAE_ENGINE_PEER_ABORT;
	if (event->auth_status ==
	    WLAN_STATUS_ANTI_CLOGGING_TOKEN_REQ) {
		return ieee80211_sae_engine_handle_anti_clogging(engine, event) == 0 ?
		    IEEE80211_SAE_ENGINE_PEER_TX_READY :
		    IEEE80211_SAE_ENGINE_PEER_ABORT;
	}
	if (event->auth_status != WLAN_STATUS_SUCCESS)
		return IEEE80211_SAE_ENGINE_PEER_AP_REJECT;
	if (event->body_len != IEEE80211_SAE_ENGINE_HNP_COMMIT_BODY_LEN)
		return IEEE80211_SAE_ENGINE_PEER_ABORT;
	parsed = sae_parse_commit(&engine->sae, event->body, event->body_len,
	    &peer_token, &peer_token_len, allowed_groups, 0, &ie_offset);
	if (parsed == SAE_SILENTLY_DISCARD)
		return IEEE80211_SAE_ENGINE_PEER_DROP;
	if (parsed != WLAN_STATUS_SUCCESS || peer_token != NULL ||
	    peer_token_len != 0 || ie_offset < 0 ||
	    (uint32_t)ie_offset != event->body_len ||
	    sae_process_commit(&engine->sae) != 0 ||
	    ieee80211_sae_engine_build_confirm(engine) != 0)
		return IEEE80211_SAE_ENGINE_PEER_ABORT;
	engine->state = IEEE80211_SAE_ENGINE_CONFIRM_PREPARED;
	return IEEE80211_SAE_ENGINE_PEER_TX_READY;
}

static enum ieee80211_sae_engine_peer_result
ieee80211_sae_engine_handle_confirm(struct ieee80211_sae_engine *engine,
	const struct ItlSaeAuthPeerEventV1 *event,
	struct ItlSaePmkContinuationV1 *continuation)
{
	int ie_offset = 0;
	uint64_t sequence;

	if (event->phase != kItlSaeAuthTransportPhaseConfirm ||
	    event->wire_transaction !=
		kItlSaeAuthTransportPeerWireTransactionConfirm)
		return IEEE80211_SAE_ENGINE_PEER_ABORT;
	/* Token-required belongs to a Commit response.  Do not turn a malformed
	 * Confirm into a user-visible AP rejection merely because it reuses 76. */
	if (event->auth_status == WLAN_STATUS_ANTI_CLOGGING_TOKEN_REQ)
		return IEEE80211_SAE_ENGINE_PEER_ABORT;
	if (event->auth_status != WLAN_STATUS_SUCCESS)
		return IEEE80211_SAE_ENGINE_PEER_AP_REJECT;
	if (event->body_len != IEEE80211_SAE_ENGINE_CONFIRM_BODY_LEN ||
	    sae_check_confirm(&engine->sae, event->body, event->body_len,
		&ie_offset) != 0 || ie_offset < 0 ||
	    (uint32_t)ie_offset != event->body_len ||
	    engine->sae.pmk_len != kItlSaePmkContinuationV1PmkLength)
		return IEEE80211_SAE_ENGINE_PEER_ABORT;
	sequence = ++engine->completion_sequence;
	if (sequence == 0)
		sequence = ++engine->completion_sequence;
	ieee80211_sae_secure_zero(continuation, sizeof(*continuation));
	continuation->identity.version = kItlSaePmkContinuationV1Version;
	continuation->identity.size = sizeof(continuation->identity);
	continuation->identity.request_generation = engine->request_generation;
	continuation->identity.association_epoch = engine->association_epoch;
	continuation->identity.relay_generation = engine->relay_generation;
	continuation->identity.event_sequence = sequence;
	os_memcpy(continuation->identity.bssid, engine->bssid,
	    sizeof(continuation->identity.bssid));
	os_memcpy(continuation->identity.sta, engine->sta,
	    sizeof(continuation->identity.sta));
	os_memcpy(continuation->pmk, engine->sae.pmk, sizeof(continuation->pmk));
	/*
	 * hostapd's sae.pmkid is the SAE internal scalar-derived identifier.  The
	 * PMK owner expects the RSN PMK Name HMAC instead, so derive the canonical
	 * value here rather than smuggling hostapd-private state across the
	 * continuation boundary.
	 */
	if (ieee80211_sae_engine_derive_rsn_pmkid(continuation->pmk,
	    engine->bssid, engine->sta, continuation->pmkid) != 0) {
		ieee80211_sae_secure_zero(continuation, sizeof(*continuation));
		ieee80211_sae_engine_fail(engine);
		return IEEE80211_SAE_ENGINE_PEER_ABORT;
	}
	if (!itl_sae_pmk_continuation_is_well_formed(continuation)) {
		ieee80211_sae_secure_zero(continuation, sizeof(*continuation));
		return IEEE80211_SAE_ENGINE_PEER_ABORT;
	}
	engine->sae.state = SAE_ACCEPTED;
	ieee80211_sae_engine_clear_crypto(engine);
	engine->state = IEEE80211_SAE_ENGINE_COMPLETE;
	return IEEE80211_SAE_ENGINE_PEER_COMPLETE;
}

enum ieee80211_sae_engine_peer_result
ieee80211_sae_engine_handle_peer(struct ieee80211_sae_engine *engine,
	const struct ItlSaeAuthPeerEventV1 *event,
	struct ItlSaePmkContinuationV1 *continuation)
{
	enum ieee80211_sae_engine_peer_result result;

	if (continuation != NULL)
		ieee80211_sae_secure_zero(continuation, sizeof(*continuation));
	if (engine == NULL || event == NULL || continuation == NULL)
		return IEEE80211_SAE_ENGINE_PEER_ABORT;
	if (!ieee80211_sae_engine_event_matches(engine, event))
		return IEEE80211_SAE_ENGINE_PEER_DROP;
	if (engine->state == IEEE80211_SAE_ENGINE_COMMIT_IN_FLIGHT ||
	    engine->state == IEEE80211_SAE_ENGINE_CONFIRM_PREPARED ||
	    engine->state == IEEE80211_SAE_ENGINE_CONFIRM_IN_FLIGHT)
		return IEEE80211_SAE_ENGINE_PEER_DROP;
	if (engine->state == IEEE80211_SAE_ENGINE_CONFIRM_SENT &&
	    event->phase == kItlSaeAuthTransportPhaseCommit &&
	    event->wire_transaction ==
		kItlSaeAuthTransportPeerWireTransactionCommit &&
	    event->auth_status == WLAN_STATUS_SUCCESS)
		return IEEE80211_SAE_ENGINE_PEER_DROP;
	if (engine->state == IEEE80211_SAE_ENGINE_COMMIT_SENT)
		result = ieee80211_sae_engine_handle_commit(engine, event);
	else if (engine->state == IEEE80211_SAE_ENGINE_CONFIRM_SENT)
		result = ieee80211_sae_engine_handle_confirm(engine, event,
		    continuation);
	else
		result = IEEE80211_SAE_ENGINE_PEER_ABORT;
	if (result != IEEE80211_SAE_ENGINE_PEER_ABORT)
		return result;
	ieee80211_sae_secure_zero(continuation, sizeof(*continuation));
	ieee80211_sae_engine_fail(engine);
	return IEEE80211_SAE_ENGINE_PEER_ABORT;
}

int
ieee80211_sae_engine_is_active(const struct ieee80211_sae_engine *engine)
{
	return engine != NULL &&
	    engine->state != IEEE80211_SAE_ENGINE_COMPLETE &&
	    engine->state != IEEE80211_SAE_ENGINE_FAILED;
}

void
ieee80211_sae_engine_destroy(struct ieee80211_sae_engine **engine)
{
	struct ieee80211_sae_engine *current;

	if (engine == NULL || *engine == NULL)
		return;
	current = *engine;
	*engine = NULL;
	ieee80211_sae_engine_clear_crypto(current);
	ieee80211_sae_secure_zero(current, sizeof(*current));
	bin_clear_free(current, sizeof(*current));
}
