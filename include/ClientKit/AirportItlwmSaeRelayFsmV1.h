/*
 * Bounded, credential-free state machine for the first AirportItlwm SAE
 * relay runtime profile.
 *
 * This is deliberately a shared value-only contract: it stores public
 * Algorithm-3 bodies, never password/PWE/KCK material, and it cannot start
 * an association by itself.  The kext owns frame addresses and lifecycle;
 * the Agent owns SAE cryptography.  The first enabled consumer is restricted
 * to pure SAE group 19 / hunting-and-pecking with mandatory PMF.
 */
#ifndef AIRPORT_ITLWM_SAE_RELAY_FSM_V1_H
#define AIRPORT_ITLWM_SAE_RELAY_FSM_V1_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <ClientKit/AirportItlwmSaeRelayV1.h>

#define kAirportItlwmSaeRelayV1Group19 19u

/* IEEE 802.11 status values used by the first bounded relay profile. */
#define kAirportItlwmSaeRelayStatusSuccess 0u
#define kAirportItlwmSaeRelayStatusAntiCloggingTokenRequired 76u
#define kAirportItlwmSaeRelayTransactionCommit 1u
#define kAirportItlwmSaeRelayTransactionConfirm 2u

/*
 * The only enabled wire profile is SAE group 19 with SHA-256 and HnP.  A
 * Commit contains its little-endian group number, a 32-byte scalar, and a
 * 64-byte P-256 element.  A Confirm contains a little-endian send-confirm
 * value and the 32-byte confirm.  Keeping these bounds here makes a bad
 * userspace reply or peer frame terminal before it can reach a management
 * frame builder.
 */
#define kAirportItlwmSaeRelayV1HnpCommitMinLength 98u
#define kAirportItlwmSaeRelayV1HnpConfirmLength 34u

enum AirportItlwmSaeRelayFsmPhaseV1 {
    kAirportItlwmSaeRelayFsmIdle = 0,
    kAirportItlwmSaeRelayFsmAwaitAgentInitialCommit,
    kAirportItlwmSaeRelayFsmAwaitPeerCommit,
    kAirportItlwmSaeRelayFsmAwaitAgentCommitRetry,
    kAirportItlwmSaeRelayFsmAwaitAgentConfirm,
    kAirportItlwmSaeRelayFsmAwaitPeerConfirm,
    kAirportItlwmSaeRelayFsmAwaitAgentComplete,
    kAirportItlwmSaeRelayFsmComplete,
    kAirportItlwmSaeRelayFsmAborted,
};

enum AirportItlwmSaeRelayFsmResultV1 {
    kAirportItlwmSaeRelayFsmAccepted = 0,
    kAirportItlwmSaeRelayFsmBadArgument,
    kAirportItlwmSaeRelayFsmNotReady,
    kAirportItlwmSaeRelayFsmNotPermitted,
    kAirportItlwmSaeRelayFsmResultAborted,
};

struct AirportItlwmSaeRelayFsmV1 {
    uint32_t phase;
    uint32_t event_pending;
    uint64_t next_event_sequence;
    struct AirportItlwmSaeTargetV1 target;
    struct AirportItlwmSaeAuthEventV1 event;
};

static inline void
AirportItlwmSaeRelayFsmV1Clear(struct AirportItlwmSaeRelayFsmV1 *state)
{
    if (state != NULL)
        memset(state, 0, sizeof(*state));
}

static inline int
AirportItlwmSaeRelayFsmV1BytesEqual(const uint8_t *left,
                                     const uint8_t *right, size_t length)
{
    return left != NULL && right != NULL && memcmp(left, right, length) == 0;
}

static inline int
AirportItlwmSaeRelayFsmV1BytesAllZero(const uint8_t *value, size_t length)
{
    size_t index;

    if (value == NULL)
        return 1;
    for (index = 0; index < length; index++) {
        if (value[index] != 0)
            return 0;
    }
    return 1;
}

static inline int
AirportItlwmSaeRelayFsmV1MacIsUnicastNonzero(const uint8_t *address)
{
    return address != NULL && (address[0] & 0x01u) == 0 &&
        !AirportItlwmSaeRelayFsmV1BytesAllZero(address,
            kAirportItlwmSaeRelayV1MacLength);
}

static inline int
AirportItlwmSaeRelayFsmV1HnpCommitWellFormed(const uint8_t *body,
                                               uint32_t body_len)
{
    return body != NULL &&
        body_len >= kAirportItlwmSaeRelayV1HnpCommitMinLength &&
        body[0] == (uint8_t)kAirportItlwmSaeRelayV1Group19 && body[1] == 0;
}

static inline int
AirportItlwmSaeRelayFsmV1HnpConfirmWellFormed(const uint8_t *body,
                                                uint32_t body_len)
{
    return body != NULL &&
        body_len == kAirportItlwmSaeRelayV1HnpConfirmLength;
}

static inline int
AirportItlwmSaeRelayFsmV1AntiCloggingWellFormed(const uint8_t *body,
                                                  uint32_t body_len)
{
    /* A status-76 response must carry group 19 plus a nonempty token. */
    return body != NULL && body_len >= 3 &&
        body[0] == (uint8_t)kAirportItlwmSaeRelayV1Group19 && body[1] == 0;
}

static inline int
AirportItlwmSaeRelayFsmV1TargetWellFormed(
    const struct AirportItlwmSaeTargetV1 *target)
{
    const uint32_t required_flags =
        kAirportItlwmSaeRelayTargetPureSae |
        kAirportItlwmSaeRelayTargetPmfCapable |
        kAirportItlwmSaeRelayTargetPmfRequired;
    const uint32_t known_flags =
        required_flags | kAirportItlwmSaeRelayTargetRsnxePresent;

    if (target == NULL ||
        target->version != kAirportItlwmSaeRelayV1Version ||
        target->size != sizeof(*target) ||
        target->ssid_len == 0 ||
        target->ssid_len > kAirportItlwmSaeRelayV1SsidMaxLength ||
        target->generation == 0 ||
        target->association_epoch == 0 ||
        target->sae_group != kAirportItlwmSaeRelayV1Group19 ||
        target->sae_method != kAirportItlwmSaeRelayMethodHuntingAndPecking ||
        (target->flags & required_flags) != required_flags ||
        (target->flags & ~known_flags) != 0 ||
        (target->rsnxe_capabilities &
         ~kAirportItlwmSaeRelayRsnxeH2e) != 0 ||
        (target->rsnxe_capabilities & kAirportItlwmSaeRelayRsnxeH2e) != 0 ||
        AirportItlwmSaeRelayFsmV1BytesAllZero(
            target->controller_nonce,
            kAirportItlwmSaeRelayV1NonceLength) ||
        !AirportItlwmSaeRelayFsmV1MacIsUnicastNonzero(target->bssid) ||
        !AirportItlwmSaeRelayFsmV1MacIsUnicastNonzero(target->sta) ||
        !AirportItlwmSaeRelayFsmV1BytesAllZero(
            target->ssid + target->ssid_len,
            kAirportItlwmSaeRelayV1SsidMaxLength - target->ssid_len) ||
        !AirportItlwmSaeRelayFsmV1BytesAllZero(
            target->client_cookie,
            kAirportItlwmSaeRelayV1NonceLength) ||
        !AirportItlwmSaeRelayFsmV1BytesAllZero(
            target->reserved, sizeof(target->reserved)))
        return 0;
    return 1;
}

static inline int
AirportItlwmSaeRelayFsmV1IdentityMatches(
    const struct AirportItlwmSaeRelayFsmV1 *state, uint64_t generation,
    uint64_t association_epoch, const uint8_t controller_nonce[
        kAirportItlwmSaeRelayV1NonceLength],
    const uint8_t client_cookie[kAirportItlwmSaeRelayV1NonceLength],
    const uint8_t bssid[kAirportItlwmSaeRelayV1MacLength],
    const uint8_t sta[kAirportItlwmSaeRelayV1MacLength])
{
    const struct AirportItlwmSaeTargetV1 *target;

    if (state == NULL)
        return 0;
    target = &state->target;
    return generation == target->generation &&
        association_epoch == target->association_epoch &&
        AirportItlwmSaeRelayFsmV1BytesEqual(controller_nonce,
            target->controller_nonce, kAirportItlwmSaeRelayV1NonceLength) &&
        AirportItlwmSaeRelayFsmV1BytesEqual(client_cookie,
            target->client_cookie, kAirportItlwmSaeRelayV1NonceLength) &&
        AirportItlwmSaeRelayFsmV1BytesEqual(bssid, target->bssid,
            kAirportItlwmSaeRelayV1MacLength) &&
        AirportItlwmSaeRelayFsmV1BytesEqual(sta, target->sta,
            kAirportItlwmSaeRelayV1MacLength);
}

static inline enum AirportItlwmSaeRelayFsmResultV1
AirportItlwmSaeRelayFsmV1Begin(struct AirportItlwmSaeRelayFsmV1 *state,
                                const struct AirportItlwmSaeTargetV1 *target)
{
    if (state == NULL || !AirportItlwmSaeRelayFsmV1TargetWellFormed(target))
        return kAirportItlwmSaeRelayFsmBadArgument;
    AirportItlwmSaeRelayFsmV1Clear(state);
    memcpy(&state->target, target, sizeof(state->target));
    state->phase = kAirportItlwmSaeRelayFsmAwaitAgentInitialCommit;
    return kAirportItlwmSaeRelayFsmAccepted;
}

static inline enum AirportItlwmSaeRelayFsmResultV1
AirportItlwmSaeRelayFsmV1BindClient(struct AirportItlwmSaeRelayFsmV1 *state,
    const uint8_t client_cookie[kAirportItlwmSaeRelayV1NonceLength])
{
    if (state == NULL || client_cookie == NULL ||
        state->phase == kAirportItlwmSaeRelayFsmIdle ||
        state->phase == kAirportItlwmSaeRelayFsmAborted ||
        state->phase == kAirportItlwmSaeRelayFsmComplete)
        return kAirportItlwmSaeRelayFsmNotReady;
    if (AirportItlwmSaeRelayFsmV1BytesAllZero(client_cookie,
        kAirportItlwmSaeRelayV1NonceLength))
        return kAirportItlwmSaeRelayFsmBadArgument;
    if (AirportItlwmSaeRelayFsmV1BytesAllZero(
        state->target.client_cookie, kAirportItlwmSaeRelayV1NonceLength)) {
        memcpy(state->target.client_cookie, client_cookie,
            kAirportItlwmSaeRelayV1NonceLength);
        return kAirportItlwmSaeRelayFsmAccepted;
    }
    return AirportItlwmSaeRelayFsmV1BytesEqual(state->target.client_cookie,
        client_cookie, kAirportItlwmSaeRelayV1NonceLength) ?
        kAirportItlwmSaeRelayFsmAccepted :
        kAirportItlwmSaeRelayFsmNotPermitted;
}

static inline int
AirportItlwmSaeRelayFsmV1TargetBound(
    const struct AirportItlwmSaeRelayFsmV1 *state)
{
    return state != NULL &&
        !AirportItlwmSaeRelayFsmV1BytesAllZero(state->target.client_cookie,
            kAirportItlwmSaeRelayV1NonceLength);
}

static inline enum AirportItlwmSaeRelayFsmResultV1
AirportItlwmSaeRelayFsmV1AcceptReply(
    struct AirportItlwmSaeRelayFsmV1 *state,
    const struct AirportItlwmSaeAuthReplyV1 *reply)
{
    uint32_t expected_kind;
    uint64_t expected_sequence;

    if (state == NULL || reply == NULL)
        return kAirportItlwmSaeRelayFsmBadArgument;
    if (!AirportItlwmSaeRelayFsmV1TargetBound(state))
        return kAirportItlwmSaeRelayFsmNotReady;
    if (reply->version != kAirportItlwmSaeRelayV1Version ||
        reply->size != sizeof(*reply) ||
        reply->body_len == 0 ||
        reply->body_len > kAirportItlwmSaeRelayV1MaxAuthBodyLength ||
        !AirportItlwmSaeRelayFsmV1BytesAllZero(
            reply->reserved, sizeof(reply->reserved)) ||
        !AirportItlwmSaeRelayFsmV1IdentityMatches(state,
            reply->generation, reply->association_epoch,
            reply->controller_nonce, reply->client_cookie, reply->bssid,
            reply->sta))
        return kAirportItlwmSaeRelayFsmNotPermitted;

    switch (state->phase) {
    case kAirportItlwmSaeRelayFsmAwaitAgentInitialCommit:
        expected_kind = kAirportItlwmSaeRelayReplyCommit;
        expected_sequence = 0;
        break;
    case kAirportItlwmSaeRelayFsmAwaitAgentCommitRetry:
        expected_kind = kAirportItlwmSaeRelayReplyCommit;
        expected_sequence = state->event.event_sequence;
        break;
    case kAirportItlwmSaeRelayFsmAwaitAgentConfirm:
        expected_kind = kAirportItlwmSaeRelayReplyConfirm;
        expected_sequence = state->event.event_sequence;
        break;
    default:
        return kAirportItlwmSaeRelayFsmNotReady;
    }

    if (reply->kind != expected_kind ||
        reply->event_sequence != expected_sequence ||
        (expected_kind == kAirportItlwmSaeRelayReplyCommit &&
         !AirportItlwmSaeRelayFsmV1HnpCommitWellFormed(reply->body,
                                                        reply->body_len)) ||
        (expected_kind == kAirportItlwmSaeRelayReplyConfirm &&
         !AirportItlwmSaeRelayFsmV1HnpConfirmWellFormed(reply->body,
                                                         reply->body_len))) {
        state->phase = kAirportItlwmSaeRelayFsmAborted;
        state->event_pending = 0;
        return kAirportItlwmSaeRelayFsmNotPermitted;
    }
    state->phase = expected_kind == kAirportItlwmSaeRelayReplyCommit ?
        kAirportItlwmSaeRelayFsmAwaitPeerCommit :
        kAirportItlwmSaeRelayFsmAwaitPeerConfirm;
    return kAirportItlwmSaeRelayFsmAccepted;
}

static inline enum AirportItlwmSaeRelayFsmResultV1
AirportItlwmSaeRelayFsmV1EmitPeerEvent(
    struct AirportItlwmSaeRelayFsmV1 *state, uint16_t transaction,
    uint16_t status, const uint8_t *body, uint32_t body_len)
{
    struct AirportItlwmSaeAuthEventV1 *event;
    uint64_t sequence;

    if (state == NULL || !AirportItlwmSaeRelayFsmV1TargetBound(state) ||
        body_len > kAirportItlwmSaeRelayV1MaxAuthBodyLength ||
        (body == NULL && body_len != 0) || state->event_pending != 0)
        return kAirportItlwmSaeRelayFsmBadArgument;

    if (state->phase == kAirportItlwmSaeRelayFsmAwaitPeerCommit &&
        transaction == kAirportItlwmSaeRelayTransactionCommit) {
        if (status == kAirportItlwmSaeRelayStatusSuccess &&
            AirportItlwmSaeRelayFsmV1HnpCommitWellFormed(body, body_len))
            state->phase = kAirportItlwmSaeRelayFsmAwaitAgentConfirm;
        else if (status == kAirportItlwmSaeRelayStatusAntiCloggingTokenRequired &&
            AirportItlwmSaeRelayFsmV1AntiCloggingWellFormed(body, body_len))
            state->phase = kAirportItlwmSaeRelayFsmAwaitAgentCommitRetry;
        else {
            state->phase = kAirportItlwmSaeRelayFsmAborted;
            state->event_pending = 0;
            return kAirportItlwmSaeRelayFsmResultAborted;
        }
    } else if (state->phase == kAirportItlwmSaeRelayFsmAwaitPeerConfirm &&
        transaction == kAirportItlwmSaeRelayTransactionConfirm) {
        if (status == kAirportItlwmSaeRelayStatusSuccess &&
            AirportItlwmSaeRelayFsmV1HnpConfirmWellFormed(body, body_len))
            state->phase = kAirportItlwmSaeRelayFsmAwaitAgentComplete;
        else {
            state->phase = kAirportItlwmSaeRelayFsmAborted;
            state->event_pending = 0;
            return kAirportItlwmSaeRelayFsmResultAborted;
        }
    } else {
        state->phase = kAirportItlwmSaeRelayFsmAborted;
        state->event_pending = 0;
        return kAirportItlwmSaeRelayFsmNotPermitted;
    }

    sequence = ++state->next_event_sequence;
    if (sequence == 0)
        sequence = ++state->next_event_sequence;
    event = &state->event;
    memset(event, 0, sizeof(*event));
    event->version = kAirportItlwmSaeRelayV1Version;
    event->size = sizeof(*event);
    event->phase = state->phase;
    event->transaction = transaction;
    event->status = status;
    event->body_len = body_len;
    event->generation = state->target.generation;
    event->association_epoch = state->target.association_epoch;
    event->event_sequence = sequence;
    memcpy(event->controller_nonce, state->target.controller_nonce,
        sizeof(event->controller_nonce));
    memcpy(event->client_cookie, state->target.client_cookie,
        sizeof(event->client_cookie));
    memcpy(event->bssid, state->target.bssid, sizeof(event->bssid));
    memcpy(event->sta, state->target.sta, sizeof(event->sta));
    if (body_len != 0)
        memcpy(event->body, body, body_len);
    state->event_pending = 1;
    return kAirportItlwmSaeRelayFsmAccepted;
}

static inline enum AirportItlwmSaeRelayFsmResultV1
AirportItlwmSaeRelayFsmV1TakeEvent(
    struct AirportItlwmSaeRelayFsmV1 *state,
    const uint8_t client_cookie[kAirportItlwmSaeRelayV1NonceLength],
    uint64_t last_seen_sequence, struct AirportItlwmSaeAuthEventV1 *out)
{
    if (state == NULL || out == NULL || !AirportItlwmSaeRelayFsmV1TargetBound(state))
        return kAirportItlwmSaeRelayFsmBadArgument;
    if (!AirportItlwmSaeRelayFsmV1BytesEqual(client_cookie,
        state->target.client_cookie, kAirportItlwmSaeRelayV1NonceLength))
        return kAirportItlwmSaeRelayFsmNotPermitted;
    if (state->phase == kAirportItlwmSaeRelayFsmAborted)
        return kAirportItlwmSaeRelayFsmResultAborted;
    if (state->event_pending == 0 ||
        state->event.event_sequence == 0 ||
        state->event.event_sequence == last_seen_sequence)
        return kAirportItlwmSaeRelayFsmNotReady;
    memcpy(out, &state->event, sizeof(*out));
    state->event_pending = 0;
    return kAirportItlwmSaeRelayFsmAccepted;
}

static inline enum AirportItlwmSaeRelayFsmResultV1
AirportItlwmSaeRelayFsmV1AcceptCompletion(
    struct AirportItlwmSaeRelayFsmV1 *state,
    const struct AirportItlwmSaeCompletionV1 *completion)
{
    if (state == NULL || completion == NULL)
        return kAirportItlwmSaeRelayFsmBadArgument;
    if (state->phase != kAirportItlwmSaeRelayFsmAwaitAgentComplete ||
        !AirportItlwmSaeRelayFsmV1TargetBound(state))
        return kAirportItlwmSaeRelayFsmNotReady;
    if (completion->version != kAirportItlwmSaeRelayV1Version ||
        completion->size != sizeof(*completion) ||
        completion->event_sequence != state->event.event_sequence ||
        !AirportItlwmSaeRelayFsmV1BytesAllZero(
            completion->reserved0, sizeof(completion->reserved0)) ||
        !AirportItlwmSaeRelayFsmV1BytesAllZero(
            completion->reserved, sizeof(completion->reserved)) ||
        AirportItlwmSaeRelayFsmV1BytesAllZero(completion->pmk,
            kAirportItlwmSaeRelayV1PmkLength) ||
        !AirportItlwmSaeRelayFsmV1IdentityMatches(state,
            completion->generation, completion->association_epoch,
            completion->controller_nonce, completion->client_cookie,
            completion->bssid, completion->sta))
        return kAirportItlwmSaeRelayFsmNotPermitted;
    state->phase = kAirportItlwmSaeRelayFsmComplete;
    return kAirportItlwmSaeRelayFsmAccepted;
}

static inline enum AirportItlwmSaeRelayFsmResultV1
AirportItlwmSaeRelayFsmV1AcceptAbort(
    struct AirportItlwmSaeRelayFsmV1 *state,
    const struct AirportItlwmSaeAbortV1 *abort_message)
{
    if (state == NULL || abort_message == NULL)
        return kAirportItlwmSaeRelayFsmBadArgument;
    if (!AirportItlwmSaeRelayFsmV1TargetBound(state))
        return kAirportItlwmSaeRelayFsmNotReady;
    if (state->phase == kAirportItlwmSaeRelayFsmIdle ||
        state->phase == kAirportItlwmSaeRelayFsmAborted ||
        state->phase == kAirportItlwmSaeRelayFsmComplete)
        return kAirportItlwmSaeRelayFsmNotReady;
    if (abort_message->version != kAirportItlwmSaeRelayV1Version ||
        abort_message->size != sizeof(*abort_message) ||
        abort_message->reason == 0 ||
        abort_message->event_sequence != state->event.event_sequence ||
        abort_message->reserved0 != 0 ||
        !AirportItlwmSaeRelayFsmV1BytesAllZero(
            abort_message->reserved, sizeof(abort_message->reserved)) ||
        !AirportItlwmSaeRelayFsmV1IdentityMatches(state,
            abort_message->generation, abort_message->association_epoch,
            abort_message->controller_nonce, abort_message->client_cookie,
            abort_message->bssid, abort_message->sta))
        return kAirportItlwmSaeRelayFsmNotPermitted;
    state->phase = kAirportItlwmSaeRelayFsmAborted;
    state->event_pending = 0;
    return kAirportItlwmSaeRelayFsmAccepted;
}

#endif /* AIRPORT_ITLWM_SAE_RELAY_FSM_V1_H */
