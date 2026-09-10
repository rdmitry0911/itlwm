#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include "itl80211/openbsd/net80211/ieee80211_join_attempt.h"

static const uint8_t bssid[6] = { 2, 4, 6, 8, 10, 12 };
static const uint8_t ssid[] = { 'L', 'a', 'b', 0, 'A', 'P' };
static unsigned int cases;

static uint64_t begin(struct ieee80211_join_attempt *attempt,
    enum ieee80211_join_phase phase)
{
    const uint64_t generation = ieee80211_join_attempt_begin(attempt,
        bssid, ssid, sizeof(ssid));
    assert(generation != 0);
    if (phase == IEEE80211_JOIN_DISCOVERY)
        return generation;
    assert(ieee80211_join_attempt_bind(attempt, generation, 71,
        bssid, ssid, sizeof(ssid)));
    if (phase > IEEE80211_JOIN_AUTH)
        assert(ieee80211_join_attempt_note_success(attempt, generation, 71,
            IEEE80211_JOIN_AUTH));
    if (phase > IEEE80211_JOIN_ASSOC)
        assert(ieee80211_join_attempt_note_success(attempt, generation, 71,
            IEEE80211_JOIN_ASSOC));
    return generation;
}

static void complete_failure(struct ieee80211_join_attempt *attempt,
    uint64_t generation, unsigned int cleanup)
{
    struct ieee80211_join_failure result;
    unsigned int bit;
    assert(!ieee80211_join_attempt_take_failure(attempt, generation, &result));
    assert(!ieee80211_join_attempt_cleanup_done(attempt, generation, 0));
    assert(!ieee80211_join_attempt_cleanup_done(attempt, generation, 3));
    assert(!ieee80211_join_attempt_cleanup_done(attempt, generation + 1, 1));
    for (bit = IEEE80211_JOIN_CLEANUP_SAE; bit != 0; bit >>= 1) {
        if ((cleanup & bit) == 0)
            continue;
        assert(ieee80211_join_attempt_cleanup_done(attempt, generation, bit));
        assert(!ieee80211_join_attempt_cleanup_done(attempt, generation, bit));
        cleanup &= ~bit;
        if (cleanup != 0)
            assert(!ieee80211_join_attempt_take_failure(attempt, generation,
                &result));
    }
    assert(!ieee80211_join_attempt_take_failure(attempt, generation, NULL));
    assert(ieee80211_join_attempt_take_failure(attempt, generation, &result));
    assert(result.generation == generation);
    assert(result.ssid_len == sizeof(ssid));
    assert(memcmp(result.ssid, ssid, sizeof(ssid)) == 0);
    assert(memcmp(result.bssid, bssid, sizeof(bssid)) == 0);
    assert(!ieee80211_join_attempt_take_failure(attempt, generation, &result));
    assert(ieee80211_join_attempt_is_current(attempt, generation));
    cases++;
}

static void failure_matrix(void)
{
    enum ieee80211_join_phase phase;
    enum ieee80211_join_failure_cause cause;
    unsigned int mask;
    for (phase = IEEE80211_JOIN_DISCOVERY; phase <= IEEE80211_JOIN_KEYS;
        phase = (enum ieee80211_join_phase)(phase + 1)) {
        for (cause = IEEE80211_JOIN_FAILURE_TIMEOUT;
            cause <= IEEE80211_JOIN_FAILURE_PEER_REASON;
            cause = (enum ieee80211_join_failure_cause)(cause + 1)) {
            const int allowed = cause == IEEE80211_JOIN_FAILURE_NO_NETWORKS ?
                phase == IEEE80211_JOIN_DISCOVERY :
                (cause != IEEE80211_JOIN_FAILURE_PEER_STATUS &&
                cause != IEEE80211_JOIN_FAILURE_PEER_REASON) ||
                phase != IEEE80211_JOIN_DISCOVERY;
            for (mask = 1; mask <= IEEE80211_JOIN_CLEANUP_ALL; mask++) {
                struct ieee80211_join_attempt attempt = {0};
                const uint64_t generation = begin(&attempt, phase);
                const struct ieee80211_join_attempt before = attempt;
                const uint64_t epoch = phase == IEEE80211_JOIN_DISCOVERY ?
                    0 : 71;
                const int accepted = ieee80211_join_attempt_fail(&attempt,
                    generation, epoch, phase, cause,
                    cause == IEEE80211_JOIN_FAILURE_PEER_STATUS ? 13 : 0,
                    cause == IEEE80211_JOIN_FAILURE_PEER_REASON ? 15 : 0,
                    cause == IEEE80211_JOIN_FAILURE_LOCAL ? EIO : 0, mask);
                assert(accepted == allowed);
                if (!allowed) {
                    assert(memcmp(&attempt, &before, sizeof(attempt)) == 0);
                    cases++;
                    continue;
                }
                assert(attempt.result.phase == phase);
                assert(attempt.result.auth.seen == (phase > IEEE80211_JOIN_AUTH ||
                    (phase == IEEE80211_JOIN_AUTH && cause != IEEE80211_JOIN_FAILURE_PEER_REASON)));
                assert(attempt.result.assoc.seen == (phase > IEEE80211_JOIN_ASSOC ||
                    (phase == IEEE80211_JOIN_ASSOC && cause != IEEE80211_JOIN_FAILURE_PEER_REASON)));
                if (phase > IEEE80211_JOIN_AUTH)
                    assert(attempt.result.auth.cause == IEEE80211_JOIN_FAILURE_NONE);
                if (phase > IEEE80211_JOIN_ASSOC)
                    assert(attempt.result.assoc.cause == IEEE80211_JOIN_FAILURE_NONE);
                assert(!ieee80211_join_attempt_fail(&attempt, generation,
                    epoch, phase, IEEE80211_JOIN_FAILURE_TIMEOUT, 0, 0, 0,
                    mask));
                assert(!ieee80211_join_attempt_note_success(&attempt,
                    generation, epoch, phase));
                complete_failure(&attempt, generation, mask);
            }
        }
    }
}

static void identities_and_success(void)
{
    struct ieee80211_join_attempt attempt = {0};
    struct ieee80211_join_attempt before;
    uint8_t other[6];
    uint64_t generation = begin(&attempt, IEEE80211_JOIN_DISCOVERY);
    memcpy(other, bssid, sizeof(other));
    other[5]++;
    before = attempt;
    assert(!ieee80211_join_attempt_bind(&attempt, generation + 1, 71,
        bssid, ssid, sizeof(ssid)));
    assert(!ieee80211_join_attempt_bind(&attempt, generation, 0,
        bssid, ssid, sizeof(ssid)));
    assert(!ieee80211_join_attempt_bind(&attempt, generation, 71,
        other, ssid, sizeof(ssid)));
    assert(!ieee80211_join_attempt_bind(&attempt, generation, 71,
        bssid, ssid, sizeof(ssid) - 1));
    assert(memcmp(&attempt, &before, sizeof(attempt)) == 0);
    assert(ieee80211_join_attempt_bind(&attempt, generation, 71,
        bssid, ssid, sizeof(ssid)));
    before = attempt;
    assert(!ieee80211_join_attempt_bind(&attempt, generation, 72,
        bssid, ssid, sizeof(ssid)));
    assert(!ieee80211_join_attempt_note_success(&attempt, generation, 72,
        IEEE80211_JOIN_AUTH));
    assert(!ieee80211_join_attempt_note_success(&attempt, generation, 71,
        IEEE80211_JOIN_ASSOC));
    assert(!ieee80211_join_attempt_fail(&attempt, generation, 72,
        IEEE80211_JOIN_AUTH, IEEE80211_JOIN_FAILURE_TIMEOUT, 0, 0, 0, 3));
    assert(memcmp(&attempt, &before, sizeof(attempt)) == 0);
    assert(ieee80211_join_attempt_note_success(&attempt, generation, 71,
        IEEE80211_JOIN_AUTH));
    assert(!ieee80211_join_attempt_note_success(&attempt, generation, 71,
        IEEE80211_JOIN_AUTH));
    assert(ieee80211_join_attempt_note_success(&attempt, generation, 71,
        IEEE80211_JOIN_ASSOC));
    assert(ieee80211_join_attempt_note_success(&attempt, generation, 71,
        IEEE80211_JOIN_KEYS));
    assert(!ieee80211_join_attempt_fail(&attempt, generation, 71,
        IEEE80211_JOIN_KEYS, IEEE80211_JOIN_FAILURE_TIMEOUT, 0, 0, 0, 3));
    assert(!ieee80211_join_attempt_cancel(&attempt, generation + 1));
    assert(ieee80211_join_attempt_cancel(&attempt, generation));
    assert(attempt.next_generation == generation);
    assert(!ieee80211_join_attempt_is_current(&attempt, generation));
    assert(begin(&attempt, IEEE80211_JOIN_DISCOVERY) == generation + 1);
    cases++;
}

static void stale_cleanup_and_alias(void)
{
    unsigned int boundary;
    for (boundary = 0; boundary < 4; boundary++) {
        struct ieee80211_join_attempt attempt = {0};
        struct ieee80211_join_failure result;
        const uint64_t old = begin(&attempt, IEEE80211_JOIN_AUTH);
        assert(ieee80211_join_attempt_fail(&attempt, old, 71,
            IEEE80211_JOIN_AUTH, IEEE80211_JOIN_FAILURE_TIMEOUT, 0, 0, 0, 7));
        if (boundary >= 1)
            assert(ieee80211_join_attempt_cleanup_done(&attempt, old, 1));
        if (boundary >= 2) {
            assert(ieee80211_join_attempt_cleanup_done(&attempt, old, 2));
            assert(ieee80211_join_attempt_cleanup_done(&attempt, old, 4));
        }
        if (boundary >= 3)
            assert(ieee80211_join_attempt_take_failure(&attempt, old, &result));
        /* Same identity, different accepted request, including aliased input. */
        const uint64_t current = ieee80211_join_attempt_begin(&attempt,
            attempt.result.bssid, attempt.result.ssid, attempt.result.ssid_len);
        assert(current == old + 1);
        const struct ieee80211_join_attempt before = attempt;
        assert(!ieee80211_join_attempt_cleanup_done(&attempt, old, 1));
        assert(!ieee80211_join_attempt_cleanup_done(&attempt, old, 2));
        assert(!ieee80211_join_attempt_cleanup_done(&attempt, old, 4));
        assert(!ieee80211_join_attempt_take_failure(&attempt, old, &result));
        assert(!ieee80211_join_attempt_cancel(&attempt, old));
        assert(!ieee80211_join_attempt_is_current(&attempt, old));
        assert(memcmp(&attempt, &before, sizeof(attempt)) == 0);
        assert(memcmp(attempt.result.ssid, ssid, sizeof(ssid)) == 0);
        cases++;
    }
}

static void rejected_ingress_and_failure(void)
{
    struct ieee80211_join_attempt attempt = {0};
    uint8_t invalid[6] = {0};
    const uint64_t generation = begin(&attempt, IEEE80211_JOIN_AUTH);
    struct ieee80211_join_attempt before = attempt;
    assert(!ieee80211_join_attempt_begin(&attempt, invalid, ssid, sizeof(ssid)));
    invalid[0] = 1;
    assert(!ieee80211_join_attempt_begin(&attempt, invalid, ssid, sizeof(ssid)));
    assert(!ieee80211_join_attempt_begin(&attempt, bssid, NULL, sizeof(ssid)));
    assert(!ieee80211_join_attempt_begin(&attempt, bssid, ssid, 0));
    assert(!ieee80211_join_attempt_begin(&attempt, bssid, ssid, 33));
    assert(!ieee80211_join_attempt_fail(&attempt, generation, 71,
        IEEE80211_JOIN_AUTH, IEEE80211_JOIN_FAILURE_PEER_STATUS, 0, 0, 0, 3));
    assert(!ieee80211_join_attempt_fail(&attempt, generation, 71,
        IEEE80211_JOIN_AUTH, IEEE80211_JOIN_FAILURE_PEER_REASON, 13, 0, 0, 3));
    assert(!ieee80211_join_attempt_fail(&attempt, generation, 71,
        IEEE80211_JOIN_AUTH, IEEE80211_JOIN_FAILURE_LOCAL, 0, 0, 0, 3));
    assert(!ieee80211_join_attempt_fail(&attempt, generation, 71,
        IEEE80211_JOIN_AUTH, IEEE80211_JOIN_FAILURE_TIMEOUT, 13, 0, 0, 3));
    assert(!ieee80211_join_attempt_fail(&attempt, generation, 71,
        IEEE80211_JOIN_AUTH, IEEE80211_JOIN_FAILURE_TIMEOUT, 0, 15, 0, 3));
    assert(!ieee80211_join_attempt_fail(&attempt, generation, 71,
        IEEE80211_JOIN_AUTH, IEEE80211_JOIN_FAILURE_TIMEOUT, 0, 0, EIO, 3));
    assert(!ieee80211_join_attempt_fail(&attempt, generation, 71,
        IEEE80211_JOIN_AUTH, IEEE80211_JOIN_FAILURE_TIMEOUT, 0, 0, 0, 0));
    assert(!ieee80211_join_attempt_fail(&attempt, generation, 71,
        IEEE80211_JOIN_AUTH, IEEE80211_JOIN_FAILURE_TIMEOUT, 0, 0, 0, 8));
    assert(memcmp(&attempt, &before, sizeof(attempt)) == 0);
    attempt.next_generation = UINT64_MAX;
    before = attempt;
    assert(!ieee80211_join_attempt_begin(&attempt, bssid, ssid, sizeof(ssid)));
    assert(memcmp(&attempt, &before, sizeof(attempt)) == 0);
    cases++;
}

int main(void)
{
    failure_matrix();
    identities_and_success();
    stale_cleanup_and_alias();
    rejected_ingress_and_failure();
    printf("join-attempt production value tests: %u cases PASS\n", cases);
    return 0;
}
