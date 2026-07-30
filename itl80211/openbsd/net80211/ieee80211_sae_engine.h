/*
 * Driver-owned SAE group-19/HnP session core.
 *
 * This component contains the password-derived SAE state.  It neither knows
 * about a UserClient nor sends a frame itself: its caller must use the real
 * IWX Algorithm-3 TX backend and feed terminal TX completions back before a
 * peer frame may advance the session.  A successful Confirm returns the
 * existing private PMK continuation directly to the driver path.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef _NET80211_IEEE80211_SAE_ENGINE_H_
#define _NET80211_IEEE80211_SAE_ENGINE_H_

#include <stddef.h>
#include <stdint.h>

#include <HAL/ItlSaeAuthTransportV1.h>
#include <HAL/ItlSaeDriverTarget.h>
#include <HAL/ItlSaePmkContinuationV1.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IEEE80211_SAE_ENGINE_GROUP19 19u
#define IEEE80211_SAE_ENGINE_HNP_METHOD 1u
#define IEEE80211_SAE_ENGINE_PASSPHRASE_MIN 8u
#define IEEE80211_SAE_ENGINE_PASSPHRASE_MAX 63u
#define IEEE80211_SAE_ENGINE_HNP_COMMIT_BODY_LEN 98u
#define IEEE80211_SAE_ENGINE_CONFIRM_BODY_LEN 34u
/*
 * An HnP anti-clogging response carries the selected group followed by an
 * opaque, AP-chosen token.  Its length is not fixed at 32 bytes: the only
 * useful bound is the public Algorithm-3 transport body after a normal
 * group-19 Commit has been reserved.
 */
#define IEEE80211_SAE_ENGINE_ANTI_CLOGGING_TOKEN_MAX \
	(kItlSaeAuthTransportV1MaxBodyLength - \
	 IEEE80211_SAE_ENGINE_HNP_COMMIT_BODY_LEN)

struct ieee80211_sae_engine;
struct ieee80211_sae_ap;

#define IEEE80211_SAE_AP_STATUS_SUCCESS 0u
#define IEEE80211_SAE_AP_STATUS_UNSPECIFIED 1u
#define IEEE80211_SAE_AP_STATUS_CHALLENGE_FAIL 15u
#define IEEE80211_SAE_AP_STATUS_GROUP_UNSUPPORTED 77u

enum ieee80211_sae_engine_peer_result {
	IEEE80211_SAE_ENGINE_PEER_ABORT = -1,
	IEEE80211_SAE_ENGINE_PEER_NONE = 0,
	IEEE80211_SAE_ENGINE_PEER_TX_READY = 1,
	IEEE80211_SAE_ENGINE_PEER_COMPLETE = 2,
	IEEE80211_SAE_ENGINE_PEER_DROP = 3,
	/* A current, well-formed peer frame carried a terminal non-success
	 * authentication status.  The caller may publish that public status before
	 * it retires the engine; this result never requests a retry or transition. */
	IEEE80211_SAE_ENGINE_PEER_AP_REJECT = 4,
};

#if ITL_SAE_DRIVER_CRYPTO_AVAILABLE

/*
 * Begin one exact selected-BSS, active-S_AUTH group-19/HnP session.
 *
 * selected and activated are both driver-originated public values.  The
 * password is consumed while this call prepares the first Commit and is
 * never retained by this API after it returns.  The caller must scrub its
 * own lease buffer immediately after this call.
 */
int ieee80211_sae_engine_begin_hnp(
	const struct ItlSaeSelectedJoinEventV1 *selected,
	const struct ItlSaeAuthActivatedEventV1 *activated,
	const uint8_t *password, size_t password_len,
	struct ieee80211_sae_engine **out_engine);

/*
 * Materialize the one prepared public Algorithm-3 frame into the existing
 * IWX transport ABI.  ticket must be allocated monotonically by the IWX
 * backend for the entire attached interface lifetime; this engine never
 * restarts or reuses that namespace.
 */
int ieee80211_sae_engine_prepare_tx(struct ieee80211_sae_engine *engine,
	uint64_t ticket, struct ItlSaeAuthTxRequestV1 *out);

/*
 * Undo prepare_tx() only when the caller has proved that this exact ticket
 * never reached the IWX doorbell.  The original prepared public body and SAE
 * crypto state remain intact; a later retry must allocate a new ticket.
 * A terminal failure is deliberately not representable by this API.
 */
int ieee80211_sae_engine_tx_rollback_unsubmitted(
	struct ieee80211_sae_engine *engine, uint64_t ticket);

/*
 * Consume the exact terminal event for the prepared frame.  Only a
 * result==0 event changes hostapd's SAE state from prepared to sent.
 */
int ieee80211_sae_engine_tx_complete(struct ieee80211_sae_engine *engine,
	const struct ItlSaeAuthTransportEventV1 *event);

/*
 * Consume one exact selected-BSS peer Commit/Confirm.  A TX_READY result
 * requires prepare_tx(); COMPLETE fills a secret continuation which must be
 * handed directly to IWX and scrubbed by the caller after that handoff.
 * Neither user space nor a controller mailbox receives this record.
 */
enum ieee80211_sae_engine_peer_result
ieee80211_sae_engine_handle_peer(struct ieee80211_sae_engine *engine,
	const struct ItlSaeAuthPeerEventV1 *event,
	struct ItlSaePmkContinuationV1 *continuation);

int ieee80211_sae_engine_is_active(const struct ieee80211_sae_engine *);
void ieee80211_sae_engine_destroy(struct ieee80211_sae_engine **);

/*
 * Driver-resident infrastructure-BSS SAE responder.  The caller owns the
 * 802.11 management transport, while this opaque object owns all password-
 * derived state.  A successful Commit returns the AP's transaction-1 body;
 * a successful Confirm returns the AP's transaction-2 body and the PMK used
 * by the existing authenticator 4-way handshake.
 *
 * The return value is an IEEE 802.11 status code.  A non-success result never
 * returns a live object.  Group rejection may return the echoed two-byte
 * group field required in the Authentication response.
 */
uint16_t ieee80211_sae_ap_begin_hnp(
	const uint8_t *ap, const uint8_t *sta,
	const uint8_t *password, size_t password_len,
	const uint8_t *peer_commit, size_t peer_commit_len,
	struct ieee80211_sae_ap **out_ap,
	uint8_t *response, size_t response_capacity,
	size_t *response_len);
uint16_t ieee80211_sae_ap_confirm(
	struct ieee80211_sae_ap *ap,
	const uint8_t *sta,
	const uint8_t *peer_confirm, size_t peer_confirm_len,
	uint8_t *response, size_t response_capacity,
	size_t *response_len,
	uint8_t *pmk, size_t pmk_capacity,
	uint8_t *pmkid, size_t pmkid_capacity);
int ieee80211_sae_ap_is_accepted(const struct ieee80211_sae_ap *);
void ieee80211_sae_ap_destroy(struct ieee80211_sae_ap **);

#else /* ITL_SAE_DRIVER_CRYPTO_AVAILABLE */

/*
 * Older deployment targets intentionally omit the crypto source phase.
 * These inline fail-closed definitions keep their shared ItlIwx source
 * link-clean even if a lifecycle-only helper is compiled there.  Public HAL
 * entry points reject SAE before they can reach this boundary.
 */
static inline int
ieee80211_sae_engine_begin_hnp(
    const struct ItlSaeSelectedJoinEventV1 *selected,
    const struct ItlSaeAuthActivatedEventV1 *activated,
    const uint8_t *password, size_t password_len,
    struct ieee80211_sae_engine **out_engine)
{
    (void)selected;
    (void)activated;
    (void)password;
    (void)password_len;
    if (out_engine != NULL)
        *out_engine = NULL;
    return -1;
}

static inline int
ieee80211_sae_engine_prepare_tx(struct ieee80211_sae_engine *engine,
    uint64_t ticket, struct ItlSaeAuthTxRequestV1 *out)
{
    (void)engine;
    (void)ticket;
    (void)out;
    return -1;
}

static inline int
ieee80211_sae_engine_tx_rollback_unsubmitted(
    struct ieee80211_sae_engine *engine, uint64_t ticket)
{
    (void)engine;
    (void)ticket;
    return -1;
}

static inline int
ieee80211_sae_engine_tx_complete(struct ieee80211_sae_engine *engine,
    const struct ItlSaeAuthTransportEventV1 *event)
{
    (void)engine;
    (void)event;
    return -1;
}

static inline enum ieee80211_sae_engine_peer_result
ieee80211_sae_engine_handle_peer(struct ieee80211_sae_engine *engine,
    const struct ItlSaeAuthPeerEventV1 *event,
    struct ItlSaePmkContinuationV1 *continuation)
{
    (void)engine;
    (void)event;
    (void)continuation;
    return IEEE80211_SAE_ENGINE_PEER_ABORT;
}

static inline int
ieee80211_sae_engine_is_active(const struct ieee80211_sae_engine *engine)
{
    (void)engine;
    return 0;
}

static inline void
ieee80211_sae_engine_destroy(struct ieee80211_sae_engine **engine)
{
    if (engine != NULL)
        *engine = NULL;
}

static inline uint16_t
ieee80211_sae_ap_begin_hnp(
    const uint8_t *ap, const uint8_t *sta,
    const uint8_t *password, size_t password_len,
    const uint8_t *peer_commit, size_t peer_commit_len,
    struct ieee80211_sae_ap **out_ap,
    uint8_t *response, size_t response_capacity,
    size_t *response_len)
{
    (void)ap;
    (void)sta;
    (void)password;
    (void)password_len;
    (void)peer_commit;
    (void)peer_commit_len;
    (void)response;
    (void)response_capacity;
    if (out_ap != NULL)
        *out_ap = NULL;
    if (response_len != NULL)
        *response_len = 0;
    return IEEE80211_SAE_AP_STATUS_UNSPECIFIED;
}

static inline uint16_t
ieee80211_sae_ap_confirm(
    struct ieee80211_sae_ap *ap,
    const uint8_t *sta,
    const uint8_t *peer_confirm, size_t peer_confirm_len,
    uint8_t *response, size_t response_capacity,
    size_t *response_len,
    uint8_t *pmk, size_t pmk_capacity,
    uint8_t *pmkid, size_t pmkid_capacity)
{
    (void)ap;
    (void)sta;
    (void)peer_confirm;
    (void)peer_confirm_len;
    (void)response;
    (void)response_capacity;
    (void)pmk;
    (void)pmk_capacity;
    (void)pmkid;
    (void)pmkid_capacity;
    if (response_len != NULL)
        *response_len = 0;
    return IEEE80211_SAE_AP_STATUS_UNSPECIFIED;
}

static inline int
ieee80211_sae_ap_is_accepted(const struct ieee80211_sae_ap *ap)
{
    (void)ap;
    return 0;
}

static inline void
ieee80211_sae_ap_destroy(struct ieee80211_sae_ap **ap)
{
    if (ap != NULL)
        *ap = NULL;
}

#endif /* ITL_SAE_DRIVER_CRYPTO_AVAILABLE */

#ifdef __cplusplus
}
#endif

#endif /* _NET80211_IEEE80211_SAE_ENGINE_H_ */
