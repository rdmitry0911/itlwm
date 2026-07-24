/*
 * Internal, credential-free Algorithm-3 transport contract.
 *
 * This is not a UserClient ABI and does not describe an SAE exchange.  It
 * gives the controller, net80211 builder, and AX211 TX completion path one
 * bounded value carrier for a single public Authentication frame.  Password,
 * PWE, KCK, PMK, PMKID, scan IEs, and Agent cookies deliberately do not cross
 * this boundary.
 */
#ifndef ITL_SAE_AUTH_TRANSPORT_V1_H
#define ITL_SAE_AUTH_TRANSPORT_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define kItlSaeAuthTransportV1Version 1u
#define kItlSaeAuthTransportV1MacLength 6u
#define kItlSaeAuthTransportV1MaxBodyLength 768u

/*
 * Relay phase is deliberately distinct from the Authentication transaction
 * sequence carried on the air.  The Agent/FSM speaks semantic Commit/Confirm
 * phases (1/2); an STA emits those as SAE wire sequences 1/3 and receives the
 * peer equivalents as 2/4.  Keeping both values in this private ABI prevents
 * a semantic Confirm value from ever being serialized as wire sequence 2.
 */
#define kItlSaeAuthTransportPhaseCommit 1u
#define kItlSaeAuthTransportPhaseConfirm 2u
#define kItlSaeAuthTransportStaWireTransactionCommit 1u
#define kItlSaeAuthTransportStaWireTransactionConfirm 3u
#define kItlSaeAuthTransportPeerWireTransactionCommit 2u
#define kItlSaeAuthTransportPeerWireTransactionConfirm 4u

enum ItlSaeAuthTransportEventKindV1 {
    kItlSaeAuthTransportEventTxComplete = 1u,
};

/*
 * The controller creates this only after it has validated an Agent semantic
 * reply.  `ticket` is monotonically allocated by that controller and is the
 * sole completion/cancellation identity; it is never derived from a wire
 * sequence number or carried in an mbuf metadata field.
 */
struct ItlSaeAuthTxRequestV1 {
    uint32_t version;
    uint32_t size;
    uint64_t association_epoch;
    uint64_t relay_generation;
    uint64_t ticket;
    uint16_t phase;
    uint16_t auth_status;
    uint32_t body_len;
    uint8_t bssid[kItlSaeAuthTransportV1MacLength];
    uint8_t sta[kItlSaeAuthTransportV1MacLength];
    uint16_t wire_transaction;
    uint8_t reserved[2];
    uint8_t body[kItlSaeAuthTransportV1MaxBodyLength];
};

/*
 * Completion leaves IWX only through its bounded deferred worker.  `result`
 * is zero exclusively for the firmware's terminal TX-success status; all
 * enqueue, mapping, reset, cancellation, and other firmware outcomes are
 * nonzero terminal failures.
 */
struct ItlSaeAuthTransportEventV1 {
    uint32_t version;
    uint32_t size;
    uint32_t kind;
    int32_t result;
    uint64_t association_epoch;
    uint64_t relay_generation;
    uint64_t ticket;
    uint16_t phase;
    uint16_t auth_status;
    uint8_t bssid[kItlSaeAuthTransportV1MacLength];
    uint8_t sta[kItlSaeAuthTransportV1MacLength];
    uint16_t wire_transaction;
    uint8_t reserved[6];
};

/*
 * A selected-BSS association epoch is the cancellation fence for every
 * credential-free SAE relay value derived from that selection.  net80211
 * borrows this POD through ic_event_handler only after it has dropped its
 * selected-BSS leaf lock; the controller copies and coalesces it before it
 * reaches its workloop gate.  It intentionally carries no old node, peer-RX
 * admission, ticket, frame, scan IE, or credential.  The observed epoch is a
 * post-advance doorbell/provenance value only; the controller re-reads the
 * authoritative current epoch before it makes any cancellation decision, so
 * no numeric ordering is inferred across a 64-bit wrap.  A nonzero revoked
 * WCL generation is an independent pre-selection secret-scrub fence.  It is
 * public provenance only, never a password or derived key, and may be
 * emitted with observed_epoch == 0 after a policy-only clear.
 */
struct ItlSaeAuthEpochCancelEventV1 {
    uint32_t version;
    uint32_t size;
    uint64_t observed_epoch;
    uint64_t revoked_wcl_credential_generation;
};

/*
 * The selected-join event crosses only the post-copy net80211 -> controller
 * boundary.  It has no request-side candidate, node pointer, raw IE, PMK,
 * password, PWE, KCK, or Agent cookie: its public SSID/BSSID values are the
 * identity already copied into the current BSS.  request_generation fences a
 * delayed controller mailbox against a later WCL request; association_epoch
 * fences it against BSS replacement.
 */
struct ItlSaeSelectedJoinEventV1 {
    uint32_t version;
    uint32_t size;
    uint64_t request_generation;
    uint64_t association_epoch;
    uint16_t sae_group;
    uint16_t sae_method;
    uint32_t rsnxe_capabilities;
    uint8_t ssid_len;
    uint8_t credential_source;
    uint8_t reserved0[2];
    uint8_t bssid[kItlSaeAuthTransportV1MacLength];
    uint8_t sta[kItlSaeAuthTransportV1MacLength];
    uint8_t ssid[32];
};

/*
 * Generic net80211 emits this only after it has entered S_AUTH while holding
 * generic Open-System AUTH.  The controller turns it into one private
 * Agent-visible target only after it has atomically activated the existing
 * peer-RX admission.  It is a credential-free value-only doorbell.
 */
struct ItlSaeAuthActivatedEventV1 {
    uint32_t version;
    uint32_t size;
    uint64_t request_generation;
    uint64_t association_epoch;
    uint64_t relay_generation;
    uint8_t bssid[kItlSaeAuthTransportV1MacLength];
    uint8_t sta[kItlSaeAuthTransportV1MacLength];
    uint8_t reserved[4];
};

/*
 * IWX emits this internal, credential-free ledger event only after the
 * ordinary generic state owner has actually committed S_RUN for one direct
 * WCL CIPHER_PWD SAE association.  `event_sequence` identifies the already
 * verified engine completion but carries neither a PMK nor a PMKID.  This is
 * deliberately distinct from ItlSaePmkContinuationIdentityV1: it may cross
 * the controller's bounded mailbox, but is not a key continuation, WCL join
 * completion, retry, reassociation, link-up, or Apple-private event.
 */
struct ItlSaeAssocCommittedEventV1 {
    uint32_t version;
    uint32_t size;
    uint64_t request_generation;
    uint64_t association_epoch;
    uint64_t relay_generation;
    uint64_t event_sequence;
    uint8_t bssid[kItlSaeAuthTransportV1MacLength];
    uint8_t sta[kItlSaeAuthTransportV1MacLength];
    uint8_t reserved[4];
};

/*
 * Controller -> HAL request to resume the real IWX S_AUTH owner.  It is not a
 * UserClient ABI and does not permit a caller to select a BSS or send a frame.
 * IWX copies it into its own serialized state transition and revalidates the
 * selected-BSS epoch before it builds firmware context or reaches net80211.
 */
struct ItlSaeJoinResumeRequestV1 {
    uint32_t version;
    uint32_t size;
    uint64_t request_generation;
    uint64_t association_epoch;
    uint64_t relay_generation;
    uint8_t bssid[kItlSaeAuthTransportV1MacLength];
    uint8_t sta[kItlSaeAuthTransportV1MacLength];
    uint8_t reserved[4];
};

/*
 * A peer Authentication frame is copied by net80211 only after the exact
 * controller-published RX admission record matches its current STA BSS.
 * `relay_generation` is copied from that admission record, never inferred
 * from an on-air field, so a delayed frame from a cancelled relay cannot be
 * consumed by a new relay for the same BSS and association epoch.
 *
 * This remains a credential-free value boundary: it contains only public SAE
 * Authentication fixed fields and the bounded public body.  It owns neither
 * an mbuf nor a node reference and is valid after the RX worker returns.
 */
struct ItlSaeAuthPeerEventV1 {
    uint32_t version;
    uint32_t size;
    uint64_t association_epoch;
    uint64_t relay_generation;
    uint16_t phase;
    uint16_t wire_transaction;
    uint16_t auth_status;
    uint16_t reserved;
    uint32_t body_len;
    uint8_t bssid[kItlSaeAuthTransportV1MacLength];
    uint8_t sta[kItlSaeAuthTransportV1MacLength];
    uint8_t body[kItlSaeAuthTransportV1MaxBodyLength];
};

#if defined(__cplusplus)
#define ITL_SAE_AUTH_TRANSPORT_STATIC_ASSERT(condition, message) \
    static_assert((condition), message)
#else
#define ITL_SAE_AUTH_TRANSPORT_STATIC_ASSERT(condition, message) \
    _Static_assert((condition), message)
#endif

ITL_SAE_AUTH_TRANSPORT_STATIC_ASSERT(sizeof(struct ItlSaeAuthTxRequestV1) == 824,
    "SAE transport request ABI size");
ITL_SAE_AUTH_TRANSPORT_STATIC_ASSERT(sizeof(struct ItlSaeAuthTransportEventV1) == 64,
    "SAE transport event ABI size");
ITL_SAE_AUTH_TRANSPORT_STATIC_ASSERT(sizeof(struct ItlSaeAuthEpochCancelEventV1) == 24,
    "SAE epoch cancellation ABI size");
ITL_SAE_AUTH_TRANSPORT_STATIC_ASSERT(sizeof(struct ItlSaeSelectedJoinEventV1) == 80,
    "SAE selected-join ABI size");
ITL_SAE_AUTH_TRANSPORT_STATIC_ASSERT(sizeof(struct ItlSaeAuthActivatedEventV1) == 48,
    "SAE auth-activation ABI size");
ITL_SAE_AUTH_TRANSPORT_STATIC_ASSERT(sizeof(struct ItlSaeAssocCommittedEventV1) == 56,
    "SAE selected-BSS commit ledger ABI size");
ITL_SAE_AUTH_TRANSPORT_STATIC_ASSERT(sizeof(struct ItlSaeJoinResumeRequestV1) == 48,
    "SAE join-resume ABI size");
ITL_SAE_AUTH_TRANSPORT_STATIC_ASSERT(sizeof(struct ItlSaeAuthPeerEventV1) == 816,
    "SAE peer event ABI size");
ITL_SAE_AUTH_TRANSPORT_STATIC_ASSERT(offsetof(struct ItlSaeAuthTxRequestV1,
    body) == 56, "SAE transport request body offset");
ITL_SAE_AUTH_TRANSPORT_STATIC_ASSERT(offsetof(struct ItlSaeAuthTransportEventV1,
    association_epoch) == 16, "SAE transport event epoch offset");
ITL_SAE_AUTH_TRANSPORT_STATIC_ASSERT(offsetof(struct ItlSaeAuthEpochCancelEventV1,
    observed_epoch) == 8, "SAE epoch cancellation observation offset");
ITL_SAE_AUTH_TRANSPORT_STATIC_ASSERT(offsetof(struct ItlSaeAuthEpochCancelEventV1,
    revoked_wcl_credential_generation) == 16,
    "SAE epoch cancellation WCL generation offset");
ITL_SAE_AUTH_TRANSPORT_STATIC_ASSERT(offsetof(struct ItlSaeSelectedJoinEventV1,
    bssid) == 36, "SAE selected-join BSSID offset");
ITL_SAE_AUTH_TRANSPORT_STATIC_ASSERT(offsetof(struct ItlSaeSelectedJoinEventV1,
    ssid) == 48, "SAE selected-join SSID offset");
ITL_SAE_AUTH_TRANSPORT_STATIC_ASSERT(offsetof(struct ItlSaeAuthActivatedEventV1,
    bssid) == 32, "SAE auth-activation BSSID offset");
ITL_SAE_AUTH_TRANSPORT_STATIC_ASSERT(offsetof(struct ItlSaeAssocCommittedEventV1,
    bssid) == 40, "SAE selected-BSS commit ledger BSSID offset");
ITL_SAE_AUTH_TRANSPORT_STATIC_ASSERT(offsetof(struct ItlSaeJoinResumeRequestV1,
    bssid) == 32, "SAE join-resume BSSID offset");
ITL_SAE_AUTH_TRANSPORT_STATIC_ASSERT(offsetof(struct ItlSaeAuthTxRequestV1,
    wire_transaction) == 52, "SAE transport request wire transaction offset");
ITL_SAE_AUTH_TRANSPORT_STATIC_ASSERT(offsetof(struct ItlSaeAuthTransportEventV1,
    wire_transaction) == 56, "SAE transport event wire transaction offset");
ITL_SAE_AUTH_TRANSPORT_STATIC_ASSERT(offsetof(struct ItlSaeAuthPeerEventV1,
    body) == 48, "SAE peer event body offset");

static inline uint16_t
itl_sae_auth_transport_sta_wire_transaction_for_phase(uint16_t phase)
{
    if (phase == kItlSaeAuthTransportPhaseCommit)
        return kItlSaeAuthTransportStaWireTransactionCommit;
    if (phase == kItlSaeAuthTransportPhaseConfirm)
        return kItlSaeAuthTransportStaWireTransactionConfirm;
    return 0;
}

static inline uint16_t
itl_sae_auth_transport_peer_wire_transaction_for_phase(uint16_t phase)
{
    if (phase == kItlSaeAuthTransportPhaseCommit)
        return kItlSaeAuthTransportPeerWireTransactionCommit;
    if (phase == kItlSaeAuthTransportPhaseConfirm)
        return kItlSaeAuthTransportPeerWireTransactionConfirm;
    return 0;
}

static inline bool
itl_sae_auth_transport_bytes_all_zero(const uint8_t *bytes, size_t length)
{
    size_t index;

    if (bytes == NULL)
        return true;
    for (index = 0; index < length; index++) {
        if (bytes[index] != 0)
            return false;
    }
    return true;
}

static inline bool
itl_sae_auth_transport_mac_is_unicast_nonzero(const uint8_t mac[
    kItlSaeAuthTransportV1MacLength])
{
    return mac != NULL && (mac[0] & 0x01u) == 0 &&
        !itl_sae_auth_transport_bytes_all_zero(mac,
            kItlSaeAuthTransportV1MacLength);
}

static inline bool
itl_sae_auth_transport_request_is_well_formed(
    const struct ItlSaeAuthTxRequestV1 *request)
{
    if (request == NULL ||
        request->version != kItlSaeAuthTransportV1Version ||
        request->size != sizeof(*request) ||
        request->association_epoch == 0 ||
        request->relay_generation == 0 ||
        request->ticket == 0 ||
        (request->phase != kItlSaeAuthTransportPhaseCommit &&
         request->phase != kItlSaeAuthTransportPhaseConfirm) ||
        request->wire_transaction !=
            itl_sae_auth_transport_sta_wire_transaction_for_phase(
                request->phase) ||
        request->auth_status != 0 ||
        request->body_len == 0 ||
        request->body_len > kItlSaeAuthTransportV1MaxBodyLength ||
        !itl_sae_auth_transport_mac_is_unicast_nonzero(request->bssid) ||
        !itl_sae_auth_transport_mac_is_unicast_nonzero(request->sta) ||
        !itl_sae_auth_transport_bytes_all_zero(request->reserved,
            sizeof(request->reserved)))
        return false;
    return true;
}

static inline bool
itl_sae_auth_transport_event_is_well_formed(
    const struct ItlSaeAuthTransportEventV1 *event)
{
    if (event == NULL ||
        event->version != kItlSaeAuthTransportV1Version ||
        event->size != sizeof(*event) ||
        event->kind != kItlSaeAuthTransportEventTxComplete ||
        event->association_epoch == 0 ||
        event->relay_generation == 0 ||
        event->ticket == 0 ||
        (event->phase != kItlSaeAuthTransportPhaseCommit &&
         event->phase != kItlSaeAuthTransportPhaseConfirm) ||
        event->wire_transaction !=
            itl_sae_auth_transport_sta_wire_transaction_for_phase(
                event->phase) ||
        event->auth_status != 0 ||
        !itl_sae_auth_transport_mac_is_unicast_nonzero(event->bssid) ||
        !itl_sae_auth_transport_mac_is_unicast_nonzero(event->sta) ||
        !itl_sae_auth_transport_bytes_all_zero(event->reserved,
            sizeof(event->reserved)))
        return false;
    return true;
}

static inline bool
itl_sae_auth_transport_event_matches_request(
    const struct ItlSaeAuthTransportEventV1 *event,
    const struct ItlSaeAuthTxRequestV1 *request)
{
    return itl_sae_auth_transport_event_is_well_formed(event) &&
        itl_sae_auth_transport_request_is_well_formed(request) &&
        event->association_epoch == request->association_epoch &&
        event->relay_generation == request->relay_generation &&
        event->ticket == request->ticket &&
        event->phase == request->phase &&
        event->wire_transaction == request->wire_transaction &&
        event->auth_status == request->auth_status &&
        memcmp(event->bssid, request->bssid, sizeof(event->bssid)) == 0 &&
        memcmp(event->sta, request->sta, sizeof(event->sta)) == 0;
}

static inline bool
itl_sae_auth_epoch_cancel_event_is_well_formed(
    const struct ItlSaeAuthEpochCancelEventV1 *event)
{
    return event != NULL &&
        event->version == kItlSaeAuthTransportV1Version &&
        event->size == sizeof(*event) &&
        (event->observed_epoch != 0 ||
         event->revoked_wcl_credential_generation != 0);
}

static inline bool
itl_sae_selected_join_event_is_well_formed(
    const struct ItlSaeSelectedJoinEventV1 *event)
{
    return event != NULL &&
        event->version == kItlSaeAuthTransportV1Version &&
        event->size == sizeof(*event) &&
        event->request_generation != 0 &&
        event->association_epoch != 0 &&
        event->sae_group == 19u && event->sae_method == 1u &&
        event->ssid_len != 0 && event->ssid_len <= sizeof(event->ssid) &&
		(event->credential_source == 0u || event->credential_source == 1u) &&
        itl_sae_auth_transport_bytes_all_zero(event->reserved0,
            sizeof(event->reserved0)) &&
        itl_sae_auth_transport_mac_is_unicast_nonzero(event->bssid) &&
        itl_sae_auth_transport_mac_is_unicast_nonzero(event->sta);
}

static inline bool
itl_sae_auth_activated_event_is_well_formed(
    const struct ItlSaeAuthActivatedEventV1 *event)
{
    return event != NULL &&
        event->version == kItlSaeAuthTransportV1Version &&
        event->size == sizeof(*event) &&
        event->request_generation != 0 &&
        event->association_epoch != 0 &&
        event->relay_generation != 0 &&
        itl_sae_auth_transport_bytes_all_zero(event->reserved,
            sizeof(event->reserved)) &&
        itl_sae_auth_transport_mac_is_unicast_nonzero(event->bssid) &&
        itl_sae_auth_transport_mac_is_unicast_nonzero(event->sta);
}

static inline bool
itl_sae_assoc_committed_event_is_well_formed(
    const struct ItlSaeAssocCommittedEventV1 *event)
{
    return event != NULL &&
        event->version == kItlSaeAuthTransportV1Version &&
        event->size == sizeof(*event) &&
        event->request_generation != 0 &&
        event->association_epoch != 0 &&
        event->relay_generation != 0 &&
        event->event_sequence != 0 &&
        itl_sae_auth_transport_bytes_all_zero(event->reserved,
            sizeof(event->reserved)) &&
        itl_sae_auth_transport_mac_is_unicast_nonzero(event->bssid) &&
        itl_sae_auth_transport_mac_is_unicast_nonzero(event->sta);
}

static inline bool
itl_sae_join_resume_request_is_well_formed(
    const struct ItlSaeJoinResumeRequestV1 *request)
{
    return request != NULL &&
        request->version == kItlSaeAuthTransportV1Version &&
        request->size == sizeof(*request) &&
        request->request_generation != 0 &&
        request->association_epoch != 0 &&
        request->relay_generation != 0 &&
        itl_sae_auth_transport_bytes_all_zero(request->reserved,
            sizeof(request->reserved)) &&
        itl_sae_auth_transport_mac_is_unicast_nonzero(request->bssid) &&
        itl_sae_auth_transport_mac_is_unicast_nonzero(request->sta);
}

/*
 * Only the two peer sequences valid when this device is the STA are legal.
 * Status/body semantics deliberately remain with the controller FSM: a real
 * peer may report a non-success SAE status with an empty body, and dropping
 * that bounded failure at the parser would turn it into a misleading timeout.
 */
static inline bool
itl_sae_auth_peer_event_is_well_formed(
    const struct ItlSaeAuthPeerEventV1 *event)
{
    if (event == NULL ||
        event->version != kItlSaeAuthTransportV1Version ||
        event->size != sizeof(*event) ||
        event->association_epoch == 0 ||
        event->relay_generation == 0 ||
        (event->phase != kItlSaeAuthTransportPhaseCommit &&
         event->phase != kItlSaeAuthTransportPhaseConfirm) ||
        event->wire_transaction !=
            itl_sae_auth_transport_peer_wire_transaction_for_phase(
                event->phase) ||
        event->body_len > kItlSaeAuthTransportV1MaxBodyLength ||
        !itl_sae_auth_transport_mac_is_unicast_nonzero(event->bssid) ||
        !itl_sae_auth_transport_mac_is_unicast_nonzero(event->sta) ||
        event->reserved != 0)
        return false;
    return true;
}

/* Exact public retransmissions may be coalesced without changing FSM state. */
static inline bool
itl_sae_auth_peer_event_equals(const struct ItlSaeAuthPeerEventV1 *left,
                               const struct ItlSaeAuthPeerEventV1 *right)
{
    return itl_sae_auth_peer_event_is_well_formed(left) &&
        itl_sae_auth_peer_event_is_well_formed(right) &&
        left->association_epoch == right->association_epoch &&
        left->relay_generation == right->relay_generation &&
        left->phase == right->phase &&
        left->wire_transaction == right->wire_transaction &&
        left->auth_status == right->auth_status &&
        left->body_len == right->body_len &&
        memcmp(left->bssid, right->bssid, sizeof(left->bssid)) == 0 &&
        memcmp(left->sta, right->sta, sizeof(left->sta)) == 0 &&
        memcmp(left->body, right->body, left->body_len) == 0;
}

#undef ITL_SAE_AUTH_TRANSPORT_STATIC_ASSERT

#endif /* ITL_SAE_AUTH_TRANSPORT_V1_H */
