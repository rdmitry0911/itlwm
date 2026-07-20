#include <assert.h>
#include <stddef.h>

#include "AirportItlwmSaeRelayV1.h"

int
main(void)
{
    assert(kAirportItlwmSaeRelayWaitTargetSelector == 2);
    assert(kAirportItlwmSaeRelaySubmitReplySelector == 3);
    assert(kAirportItlwmSaeRelayWaitAuthEventSelector == 4);
    assert(kAirportItlwmSaeRelayCompleteSelector == 5);
    assert(kAirportItlwmSaeRelayAbortSelector == 6);
    assert(kAirportItlwmSaeRelaySelectorCount == 7);

#define ASSERT_OFFSET(type, field, expected) \
    assert(offsetof(struct type, field) == (expected))

    ASSERT_OFFSET(AirportItlwmSaeTargetV1, version, 0);
    ASSERT_OFFSET(AirportItlwmSaeTargetV1, size, 4);
    ASSERT_OFFSET(AirportItlwmSaeTargetV1, flags, 8);
    ASSERT_OFFSET(AirportItlwmSaeTargetV1, sae_group, 12);
    ASSERT_OFFSET(AirportItlwmSaeTargetV1, sae_method, 14);
    ASSERT_OFFSET(AirportItlwmSaeTargetV1, rsnxe_capabilities, 16);
    ASSERT_OFFSET(AirportItlwmSaeTargetV1, authtype_lower, 20);
    ASSERT_OFFSET(AirportItlwmSaeTargetV1, authtype_upper, 24);
    ASSERT_OFFSET(AirportItlwmSaeTargetV1, ssid_len, 28);
    ASSERT_OFFSET(AirportItlwmSaeTargetV1, generation, 32);
    ASSERT_OFFSET(AirportItlwmSaeTargetV1, association_epoch, 40);
    ASSERT_OFFSET(AirportItlwmSaeTargetV1, controller_nonce, 48);
    ASSERT_OFFSET(AirportItlwmSaeTargetV1, client_cookie, 64);
    ASSERT_OFFSET(AirportItlwmSaeTargetV1, ssid, 80);
    ASSERT_OFFSET(AirportItlwmSaeTargetV1, bssid, 112);
    ASSERT_OFFSET(AirportItlwmSaeTargetV1, sta, 118);
    ASSERT_OFFSET(AirportItlwmSaeTargetV1, reserved, 124);
    assert(sizeof(struct AirportItlwmSaeTargetV1) == 128);

    ASSERT_OFFSET(AirportItlwmSaeAuthEventV1, version, 0);
    ASSERT_OFFSET(AirportItlwmSaeAuthEventV1, size, 4);
    ASSERT_OFFSET(AirportItlwmSaeAuthEventV1, phase, 8);
    ASSERT_OFFSET(AirportItlwmSaeAuthEventV1, transaction, 12);
    ASSERT_OFFSET(AirportItlwmSaeAuthEventV1, status, 14);
    ASSERT_OFFSET(AirportItlwmSaeAuthEventV1, body_len, 16);
    ASSERT_OFFSET(AirportItlwmSaeAuthEventV1, reserved0, 20);
    ASSERT_OFFSET(AirportItlwmSaeAuthEventV1, generation, 24);
    ASSERT_OFFSET(AirportItlwmSaeAuthEventV1, association_epoch, 32);
    ASSERT_OFFSET(AirportItlwmSaeAuthEventV1, event_sequence, 40);
    ASSERT_OFFSET(AirportItlwmSaeAuthEventV1, controller_nonce, 48);
    ASSERT_OFFSET(AirportItlwmSaeAuthEventV1, client_cookie, 64);
    ASSERT_OFFSET(AirportItlwmSaeAuthEventV1, bssid, 80);
    ASSERT_OFFSET(AirportItlwmSaeAuthEventV1, sta, 86);
    ASSERT_OFFSET(AirportItlwmSaeAuthEventV1, reserved1, 92);
    ASSERT_OFFSET(AirportItlwmSaeAuthEventV1, body, 96);
    assert(sizeof(struct AirportItlwmSaeAuthEventV1) == 864);

    ASSERT_OFFSET(AirportItlwmSaeAuthReplyV1, version, 0);
    ASSERT_OFFSET(AirportItlwmSaeAuthReplyV1, size, 4);
    ASSERT_OFFSET(AirportItlwmSaeAuthReplyV1, kind, 8);
    ASSERT_OFFSET(AirportItlwmSaeAuthReplyV1, body_len, 12);
    ASSERT_OFFSET(AirportItlwmSaeAuthReplyV1, generation, 16);
    ASSERT_OFFSET(AirportItlwmSaeAuthReplyV1, association_epoch, 24);
    ASSERT_OFFSET(AirportItlwmSaeAuthReplyV1, event_sequence, 32);
    ASSERT_OFFSET(AirportItlwmSaeAuthReplyV1, controller_nonce, 40);
    ASSERT_OFFSET(AirportItlwmSaeAuthReplyV1, client_cookie, 56);
    ASSERT_OFFSET(AirportItlwmSaeAuthReplyV1, bssid, 72);
    ASSERT_OFFSET(AirportItlwmSaeAuthReplyV1, sta, 78);
    ASSERT_OFFSET(AirportItlwmSaeAuthReplyV1, reserved, 84);
    ASSERT_OFFSET(AirportItlwmSaeAuthReplyV1, body, 88);
    assert(sizeof(struct AirportItlwmSaeAuthReplyV1) == 856);

    ASSERT_OFFSET(AirportItlwmSaeCompletionV1, version, 0);
    ASSERT_OFFSET(AirportItlwmSaeCompletionV1, size, 4);
    ASSERT_OFFSET(AirportItlwmSaeCompletionV1, generation, 8);
    ASSERT_OFFSET(AirportItlwmSaeCompletionV1, association_epoch, 16);
    ASSERT_OFFSET(AirportItlwmSaeCompletionV1, event_sequence, 24);
    ASSERT_OFFSET(AirportItlwmSaeCompletionV1, controller_nonce, 32);
    ASSERT_OFFSET(AirportItlwmSaeCompletionV1, client_cookie, 48);
    ASSERT_OFFSET(AirportItlwmSaeCompletionV1, bssid, 64);
    ASSERT_OFFSET(AirportItlwmSaeCompletionV1, sta, 70);
    ASSERT_OFFSET(AirportItlwmSaeCompletionV1, reserved0, 76);
    ASSERT_OFFSET(AirportItlwmSaeCompletionV1, pmk, 80);
    ASSERT_OFFSET(AirportItlwmSaeCompletionV1, pmkid, 112);
    ASSERT_OFFSET(AirportItlwmSaeCompletionV1, reserved, 128);
    assert(sizeof(struct AirportItlwmSaeCompletionV1) == 144);

    ASSERT_OFFSET(AirportItlwmSaeAbortV1, version, 0);
    ASSERT_OFFSET(AirportItlwmSaeAbortV1, size, 4);
    ASSERT_OFFSET(AirportItlwmSaeAbortV1, reason, 8);
    ASSERT_OFFSET(AirportItlwmSaeAbortV1, reserved0, 12);
    ASSERT_OFFSET(AirportItlwmSaeAbortV1, generation, 16);
    ASSERT_OFFSET(AirportItlwmSaeAbortV1, association_epoch, 24);
    ASSERT_OFFSET(AirportItlwmSaeAbortV1, event_sequence, 32);
    ASSERT_OFFSET(AirportItlwmSaeAbortV1, controller_nonce, 40);
    ASSERT_OFFSET(AirportItlwmSaeAbortV1, client_cookie, 56);
    ASSERT_OFFSET(AirportItlwmSaeAbortV1, bssid, 72);
    ASSERT_OFFSET(AirportItlwmSaeAbortV1, sta, 78);
    ASSERT_OFFSET(AirportItlwmSaeAbortV1, reserved, 84);
    assert(sizeof(struct AirportItlwmSaeAbortV1) == 96);

#undef ASSERT_OFFSET
    return 0;
}
