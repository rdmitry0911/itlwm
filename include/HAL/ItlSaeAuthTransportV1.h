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
ITL_SAE_AUTH_TRANSPORT_STATIC_ASSERT(offsetof(struct ItlSaeAuthTxRequestV1,
    body) == 56, "SAE transport request body offset");
ITL_SAE_AUTH_TRANSPORT_STATIC_ASSERT(offsetof(struct ItlSaeAuthTransportEventV1,
    association_epoch) == 16, "SAE transport event epoch offset");
ITL_SAE_AUTH_TRANSPORT_STATIC_ASSERT(offsetof(struct ItlSaeAuthTxRequestV1,
    wire_transaction) == 52, "SAE transport request wire transaction offset");
ITL_SAE_AUTH_TRANSPORT_STATIC_ASSERT(offsetof(struct ItlSaeAuthTransportEventV1,
    wire_transaction) == 56, "SAE transport event wire transaction offset");

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

#undef ITL_SAE_AUTH_TRANSPORT_STATIC_ASSERT

#endif /* ITL_SAE_AUTH_TRANSPORT_V1_H */
