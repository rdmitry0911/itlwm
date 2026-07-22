#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "AirportItlwmSaeRelayFsmV1.h"

static void
fill_target(struct AirportItlwmSaeTargetV1 *target)
{
    static const uint8_t controller_nonce[
        kAirportItlwmSaeRelayV1NonceLength] = {
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16
    };
    static const uint8_t ssid[] = { 's', 'a', 'e' };
    static const uint8_t bssid[kAirportItlwmSaeRelayV1MacLength] =
        { 0, 1, 2, 3, 4, 5 };
    static const uint8_t sta[kAirportItlwmSaeRelayV1MacLength] =
        { 6, 7, 8, 9, 10, 11 };

    memset(target, 0, sizeof(*target));
    target->version = kAirportItlwmSaeRelayV1Version;
    target->size = sizeof(*target);
    target->flags = kAirportItlwmSaeRelayTargetPureSae |
        kAirportItlwmSaeRelayTargetPmfCapable |
        kAirportItlwmSaeRelayTargetPmfRequired;
    target->sae_group = kAirportItlwmSaeRelayV1Group19;
    target->sae_method = kAirportItlwmSaeRelayMethodHuntingAndPecking;
    target->ssid_len = sizeof(ssid);
    target->generation = 17;
    target->association_epoch = 23;
    memcpy(target->controller_nonce, controller_nonce,
        sizeof(target->controller_nonce));
    memcpy(target->ssid, ssid, sizeof(ssid));
    memcpy(target->bssid, bssid, sizeof(bssid));
    memcpy(target->sta, sta, sizeof(sta));
}

static void
bind(struct AirportItlwmSaeRelayFsmV1 *state)
{
    static const uint8_t cookie[kAirportItlwmSaeRelayV1NonceLength] = {
        16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1
    };

    assert(AirportItlwmSaeRelayFsmV1BindClient(state, cookie) ==
        kAirportItlwmSaeRelayFsmAccepted);
}

static void
fill_reply(const struct AirportItlwmSaeRelayFsmV1 *state,
           struct AirportItlwmSaeAuthReplyV1 *reply, uint32_t kind,
           uint64_t event_sequence)
{
    memset(reply, 0, sizeof(*reply));
    reply->version = kAirportItlwmSaeRelayV1Version;
    reply->size = sizeof(*reply);
    reply->kind = kind;
    reply->body_len = kind == kAirportItlwmSaeRelayReplyCommit ?
        kAirportItlwmSaeRelayV1HnpCommitMinLength :
        kAirportItlwmSaeRelayV1HnpConfirmLength;
    reply->generation = state->target.generation;
    reply->association_epoch = state->target.association_epoch;
    reply->event_sequence = event_sequence;
    memcpy(reply->controller_nonce, state->target.controller_nonce,
        sizeof(reply->controller_nonce));
    memcpy(reply->client_cookie, state->target.client_cookie,
        sizeof(reply->client_cookie));
    memcpy(reply->bssid, state->target.bssid, sizeof(reply->bssid));
    memcpy(reply->sta, state->target.sta, sizeof(reply->sta));
    if (kind == kAirportItlwmSaeRelayReplyCommit) {
        reply->body[0] = kAirportItlwmSaeRelayV1Group19;
        reply->body[1] = 0;
        reply->body[2] = 1;
    } else {
        reply->body[0] = 1;
        reply->body[1] = 0;
        reply->body[2] = 2;
    }
}

static void
fill_completion(const struct AirportItlwmSaeRelayFsmV1 *state,
                struct AirportItlwmSaeCompletionV1 *completion)
{
    memset(completion, 0, sizeof(*completion));
    completion->version = kAirportItlwmSaeRelayV1Version;
    completion->size = sizeof(*completion);
    completion->generation = state->target.generation;
    completion->association_epoch = state->target.association_epoch;
    completion->event_sequence = state->event.event_sequence;
    memcpy(completion->controller_nonce, state->target.controller_nonce,
        sizeof(completion->controller_nonce));
    memcpy(completion->client_cookie, state->target.client_cookie,
        sizeof(completion->client_cookie));
    memcpy(completion->bssid, state->target.bssid, sizeof(completion->bssid));
    memcpy(completion->sta, state->target.sta, sizeof(completion->sta));
    completion->pmk[0] = 1;
    completion->pmkid[0] = 2;
}

static void
fill_peer_commit(uint8_t body[kAirportItlwmSaeRelayV1HnpCommitMinLength])
{
    memset(body, 0, kAirportItlwmSaeRelayV1HnpCommitMinLength);
    body[0] = kAirportItlwmSaeRelayV1Group19;
    body[2] = 2;
}

static void
fill_peer_confirm(uint8_t body[kAirportItlwmSaeRelayV1HnpConfirmLength])
{
    memset(body, 0, kAirportItlwmSaeRelayV1HnpConfirmLength);
    body[0] = 1;
    body[2] = 2;
}

static void
test_happy_path(void)
{
    struct AirportItlwmSaeRelayFsmV1 state;
    struct AirportItlwmSaeTargetV1 target;
    struct AirportItlwmSaeAuthReplyV1 reply;
    struct AirportItlwmSaeAuthEventV1 event;
    struct AirportItlwmSaeCompletionV1 completion;
    uint8_t peer_commit[kAirportItlwmSaeRelayV1HnpCommitMinLength];
    uint8_t peer_confirm[kAirportItlwmSaeRelayV1HnpConfirmLength];

    fill_target(&target);
    fill_peer_commit(peer_commit);
    fill_peer_confirm(peer_confirm);
    assert(AirportItlwmSaeRelayFsmV1Begin(&state, &target) ==
        kAirportItlwmSaeRelayFsmAccepted);
    bind(&state);

    fill_reply(&state, &reply, kAirportItlwmSaeRelayReplyCommit, 0);
    assert(AirportItlwmSaeRelayFsmV1AcceptReply(&state, &reply) ==
        kAirportItlwmSaeRelayFsmAccepted);
    assert(state.phase == kAirportItlwmSaeRelayFsmAwaitPeerCommit);

    assert(AirportItlwmSaeRelayFsmV1EmitPeerEvent(&state,
        kAirportItlwmSaeRelayTransactionCommit,
        kAirportItlwmSaeRelayStatusSuccess, peer_commit,
        sizeof(peer_commit)) == kAirportItlwmSaeRelayFsmAccepted);
    assert(AirportItlwmSaeRelayFsmV1TakeEvent(&state,
        state.target.client_cookie, 0, &event) ==
        kAirportItlwmSaeRelayFsmAccepted);
    assert(event.phase == kAirportItlwmSaeRelayFsmAwaitAgentConfirm);
    assert(event.event_sequence == 1);

    fill_reply(&state, &reply, kAirportItlwmSaeRelayReplyConfirm,
        event.event_sequence);
    assert(AirportItlwmSaeRelayFsmV1AcceptReply(&state, &reply) ==
        kAirportItlwmSaeRelayFsmAccepted);
    assert(state.phase == kAirportItlwmSaeRelayFsmAwaitPeerConfirm);

    assert(AirportItlwmSaeRelayFsmV1EmitPeerEvent(&state,
        kAirportItlwmSaeRelayTransactionConfirm,
        kAirportItlwmSaeRelayStatusSuccess, peer_confirm,
        sizeof(peer_confirm)) == kAirportItlwmSaeRelayFsmAccepted);
    assert(AirportItlwmSaeRelayFsmV1TakeEvent(&state,
        state.target.client_cookie, event.event_sequence, &event) ==
        kAirportItlwmSaeRelayFsmAccepted);
    assert(event.phase == kAirportItlwmSaeRelayFsmAwaitAgentComplete);
    assert(event.event_sequence == 2);

    fill_completion(&state, &completion);
    assert(AirportItlwmSaeRelayFsmV1AcceptCompletion(&state, &completion) ==
        kAirportItlwmSaeRelayFsmAccepted);
    assert(state.phase == kAirportItlwmSaeRelayFsmComplete);
}

static void
test_anti_clogging_retry(void)
{
    struct AirportItlwmSaeRelayFsmV1 state;
    struct AirportItlwmSaeTargetV1 target;
    struct AirportItlwmSaeAuthReplyV1 reply;
    struct AirportItlwmSaeAuthEventV1 event;
    const uint8_t token[] = { 19, 0, 7 };

    fill_target(&target);
    assert(AirportItlwmSaeRelayFsmV1Begin(&state, &target) ==
        kAirportItlwmSaeRelayFsmAccepted);
    bind(&state);
    fill_reply(&state, &reply, kAirportItlwmSaeRelayReplyCommit, 0);
    assert(AirportItlwmSaeRelayFsmV1AcceptReply(&state, &reply) ==
        kAirportItlwmSaeRelayFsmAccepted);

    assert(AirportItlwmSaeRelayFsmV1EmitPeerEvent(&state,
        kAirportItlwmSaeRelayTransactionCommit,
        kAirportItlwmSaeRelayStatusAntiCloggingTokenRequired, token,
        sizeof(token)) == kAirportItlwmSaeRelayFsmAccepted);
    assert(AirportItlwmSaeRelayFsmV1TakeEvent(&state,
        state.target.client_cookie, 0, &event) ==
        kAirportItlwmSaeRelayFsmAccepted);
    assert(event.phase == kAirportItlwmSaeRelayFsmAwaitAgentCommitRetry);
    fill_reply(&state, &reply, kAirportItlwmSaeRelayReplyCommit,
        event.event_sequence);
    assert(AirportItlwmSaeRelayFsmV1AcceptReply(&state, &reply) ==
        kAirportItlwmSaeRelayFsmAccepted);
    assert(state.phase == kAirportItlwmSaeRelayFsmAwaitPeerCommit);
}

static void
test_peer_event_delivery_fence(void)
{
    struct AirportItlwmSaeRelayFsmV1 state;
    struct AirportItlwmSaeTargetV1 target;
    struct AirportItlwmSaeAuthReplyV1 reply;
    struct AirportItlwmSaeAuthEventV1 event;
    struct AirportItlwmSaeCompletionV1 completion;
    uint8_t peer_commit[kAirportItlwmSaeRelayV1HnpCommitMinLength];
    uint8_t peer_confirm[kAirportItlwmSaeRelayV1HnpConfirmLength];
    const uint8_t token[] = { 19, 0, 7 };

    fill_target(&target);
    fill_peer_commit(peer_commit);
    fill_peer_confirm(peer_confirm);
    assert(AirportItlwmSaeRelayFsmV1Begin(&state, &target) ==
        kAirportItlwmSaeRelayFsmAccepted);
    bind(&state);
    fill_reply(&state, &reply, kAirportItlwmSaeRelayReplyCommit, 0);
    assert(AirportItlwmSaeRelayFsmV1AcceptReply(&state, &reply) ==
        kAirportItlwmSaeRelayFsmAccepted);

    assert(AirportItlwmSaeRelayFsmV1EmitPeerEvent(&state,
        kAirportItlwmSaeRelayTransactionCommit,
        kAirportItlwmSaeRelayStatusSuccess, peer_commit,
        sizeof(peer_commit)) == kAirportItlwmSaeRelayFsmAccepted);
    fill_reply(&state, &reply, kAirportItlwmSaeRelayReplyConfirm,
        state.event.event_sequence);
    assert(AirportItlwmSaeRelayFsmV1AcceptReply(&state, &reply) ==
        kAirportItlwmSaeRelayFsmNotReady);
    assert(state.phase == kAirportItlwmSaeRelayFsmAwaitAgentConfirm);
    assert(state.event_pending == 1);
    assert(AirportItlwmSaeRelayFsmV1TakeEvent(&state,
        state.target.client_cookie, 0, &event) ==
        kAirportItlwmSaeRelayFsmAccepted);
    assert(AirportItlwmSaeRelayFsmV1AcceptReply(&state, &reply) ==
        kAirportItlwmSaeRelayFsmAccepted);

    assert(AirportItlwmSaeRelayFsmV1EmitPeerEvent(&state,
        kAirportItlwmSaeRelayTransactionConfirm,
        kAirportItlwmSaeRelayStatusSuccess, peer_confirm,
        sizeof(peer_confirm)) == kAirportItlwmSaeRelayFsmAccepted);
    fill_completion(&state, &completion);
    assert(AirportItlwmSaeRelayFsmV1AcceptCompletion(&state, &completion) ==
        kAirportItlwmSaeRelayFsmNotReady);
    assert(state.phase == kAirportItlwmSaeRelayFsmAwaitAgentComplete);
    assert(state.event_pending == 1);
    assert(AirportItlwmSaeRelayFsmV1TakeEvent(&state,
        state.target.client_cookie, event.event_sequence, &event) ==
        kAirportItlwmSaeRelayFsmAccepted);
    assert(AirportItlwmSaeRelayFsmV1AcceptCompletion(&state, &completion) ==
        kAirportItlwmSaeRelayFsmAccepted);

    fill_target(&target);
    assert(AirportItlwmSaeRelayFsmV1Begin(&state, &target) ==
        kAirportItlwmSaeRelayFsmAccepted);
    bind(&state);
    fill_reply(&state, &reply, kAirportItlwmSaeRelayReplyCommit, 0);
    assert(AirportItlwmSaeRelayFsmV1AcceptReply(&state, &reply) ==
        kAirportItlwmSaeRelayFsmAccepted);
    assert(AirportItlwmSaeRelayFsmV1EmitPeerEvent(&state,
        kAirportItlwmSaeRelayTransactionCommit,
        kAirportItlwmSaeRelayStatusAntiCloggingTokenRequired, token,
        sizeof(token)) == kAirportItlwmSaeRelayFsmAccepted);
    fill_reply(&state, &reply, kAirportItlwmSaeRelayReplyCommit,
        state.event.event_sequence);
    assert(AirportItlwmSaeRelayFsmV1AcceptReply(&state, &reply) ==
        kAirportItlwmSaeRelayFsmNotReady);
    assert(state.phase == kAirportItlwmSaeRelayFsmAwaitAgentCommitRetry);
    assert(state.event_pending == 1);
    assert(AirportItlwmSaeRelayFsmV1TakeEvent(&state,
        state.target.client_cookie, 0, &event) ==
        kAirportItlwmSaeRelayFsmAccepted);
    assert(AirportItlwmSaeRelayFsmV1AcceptReply(&state, &reply) ==
        kAirportItlwmSaeRelayFsmAccepted);
}

static void
test_fail_closed(void)
{
    struct AirportItlwmSaeRelayFsmV1 state;
    struct AirportItlwmSaeTargetV1 target;
    struct AirportItlwmSaeAuthReplyV1 reply;
    struct AirportItlwmSaeCompletionV1 completion;
    uint8_t zero_cookie[kAirportItlwmSaeRelayV1NonceLength] = { 0 };

    fill_target(&target);
    target.rsnxe_capabilities = kAirportItlwmSaeRelayRsnxeH2e;
    assert(AirportItlwmSaeRelayFsmV1Begin(&state, &target) ==
        kAirportItlwmSaeRelayFsmBadArgument);

    fill_target(&target);
    assert(AirportItlwmSaeRelayFsmV1Begin(&state, &target) ==
        kAirportItlwmSaeRelayFsmAccepted);
    assert(AirportItlwmSaeRelayFsmV1BindClient(&state, zero_cookie) ==
        kAirportItlwmSaeRelayFsmBadArgument);
    bind(&state);

    assert(AirportItlwmSaeRelayFsmV1EmitPeerEvent(&state,
        kAirportItlwmSaeRelayTransactionConfirm,
        kAirportItlwmSaeRelayStatusSuccess, (const uint8_t *)"x", 1) ==
        kAirportItlwmSaeRelayFsmNotPermitted);
    assert(state.phase == kAirportItlwmSaeRelayFsmAborted);

    fill_target(&target);
    assert(AirportItlwmSaeRelayFsmV1Begin(&state, &target) ==
        kAirportItlwmSaeRelayFsmAccepted);
    bind(&state);
    fill_reply(&state, &reply, kAirportItlwmSaeRelayReplyCommit, 1);
    assert(AirportItlwmSaeRelayFsmV1AcceptReply(&state, &reply) ==
        kAirportItlwmSaeRelayFsmNotPermitted);
    assert(state.phase == kAirportItlwmSaeRelayFsmAborted);

    fill_target(&target);
    assert(AirportItlwmSaeRelayFsmV1Begin(&state, &target) ==
        kAirportItlwmSaeRelayFsmAccepted);
    bind(&state);
    fill_completion(&state, &completion);
    assert(AirportItlwmSaeRelayFsmV1AcceptCompletion(&state, &completion) ==
        kAirportItlwmSaeRelayFsmNotReady);
}

static void
test_wire_shape_rejections(void)
{
    struct AirportItlwmSaeRelayFsmV1 state;
    struct AirportItlwmSaeTargetV1 target;
    struct AirportItlwmSaeAuthReplyV1 reply;
    struct AirportItlwmSaeAuthEventV1 event;
    uint8_t peer_commit[kAirportItlwmSaeRelayV1HnpCommitMinLength];
    uint8_t peer_confirm[kAirportItlwmSaeRelayV1HnpConfirmLength];

    fill_target(&target);
    memset(target.bssid, 0, sizeof(target.bssid));
    assert(AirportItlwmSaeRelayFsmV1Begin(&state, &target) ==
        kAirportItlwmSaeRelayFsmBadArgument);

    fill_target(&target);
    target.sta[0] |= 1;
    assert(AirportItlwmSaeRelayFsmV1Begin(&state, &target) ==
        kAirportItlwmSaeRelayFsmBadArgument);

    fill_target(&target);
    target.ssid[target.ssid_len] = 1;
    assert(AirportItlwmSaeRelayFsmV1Begin(&state, &target) ==
        kAirportItlwmSaeRelayFsmBadArgument);

    fill_target(&target);
    assert(AirportItlwmSaeRelayFsmV1Begin(&state, &target) ==
        kAirportItlwmSaeRelayFsmAccepted);
    bind(&state);
    fill_reply(&state, &reply, kAirportItlwmSaeRelayReplyCommit, 0);
    reply.body_len = 3;
    assert(AirportItlwmSaeRelayFsmV1AcceptReply(&state, &reply) ==
        kAirportItlwmSaeRelayFsmNotPermitted);
    assert(state.phase == kAirportItlwmSaeRelayFsmAborted);

    fill_target(&target);
    assert(AirportItlwmSaeRelayFsmV1Begin(&state, &target) ==
        kAirportItlwmSaeRelayFsmAccepted);
    bind(&state);
    fill_reply(&state, &reply, kAirportItlwmSaeRelayReplyCommit, 0);
    assert(AirportItlwmSaeRelayFsmV1AcceptReply(&state, &reply) ==
        kAirportItlwmSaeRelayFsmAccepted);
    fill_peer_commit(peer_commit);
    peer_commit[0] = 20;
    assert(AirportItlwmSaeRelayFsmV1EmitPeerEvent(&state,
        kAirportItlwmSaeRelayTransactionCommit,
        kAirportItlwmSaeRelayStatusSuccess, peer_commit,
        sizeof(peer_commit)) == kAirportItlwmSaeRelayFsmResultAborted);
    assert(AirportItlwmSaeRelayFsmV1TakeEvent(&state,
        state.target.client_cookie, 0, &event) ==
        kAirportItlwmSaeRelayFsmResultAborted);

    fill_target(&target);
    assert(AirportItlwmSaeRelayFsmV1Begin(&state, &target) ==
        kAirportItlwmSaeRelayFsmAccepted);
    bind(&state);
    fill_reply(&state, &reply, kAirportItlwmSaeRelayReplyCommit, 0);
    assert(AirportItlwmSaeRelayFsmV1AcceptReply(&state, &reply) ==
        kAirportItlwmSaeRelayFsmAccepted);
    assert(AirportItlwmSaeRelayFsmV1EmitPeerEvent(&state,
        kAirportItlwmSaeRelayTransactionCommit,
        kAirportItlwmSaeRelayStatusAntiCloggingTokenRequired,
        (const uint8_t *)"\x13\0", 2) ==
        kAirportItlwmSaeRelayFsmResultAborted);

    fill_target(&target);
    assert(AirportItlwmSaeRelayFsmV1Begin(&state, &target) ==
        kAirportItlwmSaeRelayFsmAccepted);
    bind(&state);
    fill_reply(&state, &reply, kAirportItlwmSaeRelayReplyCommit, 0);
    assert(AirportItlwmSaeRelayFsmV1AcceptReply(&state, &reply) ==
        kAirportItlwmSaeRelayFsmAccepted);
    fill_peer_commit(peer_commit);
    assert(AirportItlwmSaeRelayFsmV1EmitPeerEvent(&state,
        kAirportItlwmSaeRelayTransactionCommit,
        kAirportItlwmSaeRelayStatusSuccess, peer_commit,
        sizeof(peer_commit)) == kAirportItlwmSaeRelayFsmAccepted);
    assert(AirportItlwmSaeRelayFsmV1TakeEvent(&state,
        state.target.client_cookie, 0, &event) ==
        kAirportItlwmSaeRelayFsmAccepted);
    fill_reply(&state, &reply, kAirportItlwmSaeRelayReplyConfirm,
        event.event_sequence);
    assert(AirportItlwmSaeRelayFsmV1AcceptReply(&state, &reply) ==
        kAirportItlwmSaeRelayFsmAccepted);
    fill_peer_confirm(peer_confirm);
    assert(AirportItlwmSaeRelayFsmV1EmitPeerEvent(&state,
        kAirportItlwmSaeRelayTransactionConfirm,
        kAirportItlwmSaeRelayStatusSuccess, peer_confirm,
        sizeof(peer_confirm) - 1) ==
        kAirportItlwmSaeRelayFsmResultAborted);
}

int
main(void)
{
    test_happy_path();
    test_anti_clogging_retry();
    test_peer_event_delivery_fence();
    test_fail_closed();
    test_wire_shape_rejections();
    return 0;
}
