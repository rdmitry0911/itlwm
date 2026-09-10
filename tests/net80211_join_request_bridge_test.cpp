#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <utility>
#include "itl80211/openbsd/net80211/ieee80211_join_attempt.h"

using u_int = unsigned int;
using u_int8_t = uint8_t;
using u_int16_t = uint16_t;
using u_int32_t = uint32_t;
using u_int64_t = uint64_t;
using IOInterruptState = int;
enum { IEEE80211_M_STA, IEEE80211_M_HOSTAP, IEEE80211_EVT_STA_JOIN_FAILED = 24 };
enum { IEEE80211_S_SCAN, IEEE80211_S_AUTH, IEEE80211_S_RUN };
struct IOSimpleLock { bool held = false; };
static std::function<void()> afterUnlock;
static int IOSimpleLockLockDisableInterrupt(IOSimpleLock *lock) {
    assert(!lock->held);
    lock->held = true;
    return 0;
}
static void IOSimpleLockUnlockEnableInterrupt(IOSimpleLock *lock, int) {
    assert(lock->held);
    lock->held = false;
    auto action = std::move(afterUnlock);
    afterUnlock = {};
    if (action) action();
}
struct ieee80211com {
    int ic_opmode = IEEE80211_M_STA;
    int ic_state = IEEE80211_S_SCAN;
    uint32_t ic_initial_scan_census_only = 0;
    IOSimpleLock *ic_pae_selected_bss_lock = nullptr;
    uint64_t ic_pae_assoc_epoch = 71;
    ieee80211_join_attempt ic_wcl_join_attempt{};
    void (*ic_event_handler)(ieee80211com *, int, void *) = nullptr;
};
int ieee80211_wcl_join_fail(ieee80211com *, const ieee80211_join_failure *,
    ieee80211_join_failure_cause, uint16_t, uint16_t, uint32_t, unsigned int);
#include "join-bridge.inc"

static constexpr uint8_t bssid[] = { 2, 4, 6, 8, 10, 12 };
static constexpr uint8_t ssid[] = { 'l', 'a', 'b' };
static unsigned int callbacks, accepted;
static ieee80211_join_failure delivered;
static void event(ieee80211com *ic, int code, void *data) {
    assert(!ic->ic_pae_selected_bss_lock->held);
    assert(code == IEEE80211_EVT_STA_JOIN_FAILED && data);
    callbacks++;
    const auto result = *static_cast<ieee80211_join_failure *>(data);
    if (ieee80211_wcl_join_generation_current(ic, result.generation)) {
        delivered = result;
        accepted++;
    }
}
static void bind(ieee80211com *ic, uint64_t generation) {
    const int irq = IOSimpleLockLockDisableInterrupt(ic->ic_pae_selected_bss_lock);
    assert(ieee80211_join_attempt_bind(&ic->ic_wcl_join_attempt, generation,
        ic->ic_pae_assoc_epoch, bssid, ssid, sizeof(ssid)));
    IOSimpleLockUnlockEnableInterrupt(ic->ic_pae_selected_bss_lock, irq);
}
int main() {
    IOSimpleLock lock;
    ieee80211com ic;
    ic.ic_pae_selected_bss_lock = &lock;
    ic.ic_event_handler = event;
    for (unsigned int staleBoundary = 0; staleBoundary < 4; staleBoundary++) {
        const uint64_t generation = ieee80211_wcl_join_begin(&ic,
            bssid, ssid, sizeof(ssid));
        assert(generation);
        ieee80211_join_failure request;
        assert(ieee80211_wcl_join_copy_current(&ic, 0, &request));
        assert(request.phase == IEEE80211_JOIN_DISCOVERY);
        bind(&ic, generation);
        assert(!ieee80211_wcl_join_copy_current(&ic, 0, &request));
        assert(ieee80211_wcl_join_copy_current(&ic, 71, &request));
        assert(request.phase == IEEE80211_JOIN_AUTH);
        assert(ieee80211_wcl_join_fail(&ic, &request,
            IEEE80211_JOIN_FAILURE_TIMEOUT, 0, 0, 0, 7));
        assert(ieee80211_wcl_join_failure_pending(&ic, generation));
        const unsigned int prior = accepted;
        ieee80211_wcl_join_cleanup_done(&ic, generation, 1);
        if (staleBoundary == 1)
            assert(ieee80211_wcl_join_begin(&ic, bssid, ssid, sizeof(ssid)));
        ieee80211_wcl_join_cleanup_done(&ic, generation, 2);
        if (staleBoundary == 2)
            ieee80211_wcl_join_cancel(&ic, generation);
        if (staleBoundary == 3) {
            afterUnlock = [&] {
                assert(ieee80211_wcl_join_begin(&ic, bssid, ssid, sizeof(ssid)));
            };
        }
        ieee80211_wcl_join_cleanup_done(&ic, generation, 4);
        assert(accepted == prior + (staleBoundary == 0));
        assert(!ieee80211_wcl_join_failure_pending(&ic, generation));
        ieee80211_wcl_join_cleanup_done(&ic, generation, 4);
        assert(accepted == prior + (staleBoundary == 0));
    }
    assert(callbacks == 2 && accepted == 1);
    assert(delivered.phase == IEEE80211_JOIN_AUTH);
    assert(delivered.auth.cause == IEEE80211_JOIN_FAILURE_TIMEOUT);
    assert(delivered.assoc.seen == 0);
    const uint64_t generation = ieee80211_wcl_join_begin(&ic,
        bssid, ssid, sizeof(ssid));
    bind(&ic, generation);
    ieee80211_join_failure request;
    assert(ieee80211_wcl_join_copy_current(&ic, 71, &request));
    ic.ic_pae_assoc_epoch++;
    assert(!ieee80211_wcl_join_copy_current(&ic, 71, &request));
    assert(!ieee80211_wcl_join_note_success(&ic, generation, 71,
        IEEE80211_JOIN_AUTH));
    request.generation = generation;
    request.association_epoch = 71;
    request.phase = IEEE80211_JOIN_AUTH;
    assert(!ieee80211_wcl_join_fail(&ic, &request,
        IEEE80211_JOIN_FAILURE_TIMEOUT, 0, 0, 0, 7));
    ieee80211_wcl_join_cancel(&ic, 0);
    assert(!ieee80211_wcl_join_generation_current(&ic, generation));
    for (unsigned int outcome = 0; outcome < 5; outcome++) {
        const uint64_t admitted = ieee80211_wcl_join_begin(&ic,
            bssid, ssid, sizeof(ssid));
        ic.ic_initial_scan_census_only = 1;
        assert(!ieee80211_wcl_join_scan_generation(&ic));
        ic.ic_initial_scan_census_only = 0;
        ic.ic_state = IEEE80211_S_RUN;
        assert(!ieee80211_wcl_join_scan_generation(&ic));
        ic.ic_state = IEEE80211_S_SCAN;
        assert(ieee80211_wcl_join_scan_generation(&ic) == admitted);
        const unsigned int prior = accepted;
        if (outcome == 1)
            (void)ieee80211_wcl_join_begin(&ic, bssid, ssid, sizeof(ssid));
        if (outcome == 2)
            ieee80211_wcl_join_cancel(&ic, admitted);
        if (outcome == 3)
            bind(&ic, admitted);
        if (outcome == 4)
            ic.ic_opmode = IEEE80211_M_HOSTAP;
        assert(ieee80211_wcl_join_scan_current(&ic, admitted) == (outcome == 0));
        assert(ieee80211_wcl_join_scan_failed(&ic, admitted) == (outcome == 0));
        assert(!ieee80211_wcl_join_scan_failed(&ic, 0));
        assert(accepted == prior);
        ieee80211_wcl_join_cleanup_done(&ic, admitted, 1);
        ieee80211_wcl_join_cleanup_done(&ic, admitted, 2);
        assert(accepted == prior); // Firmware retirement is not SAE retirement.
        ieee80211_wcl_join_cleanup_done(&ic, admitted, 4);
        assert(accepted == prior + (outcome == 0));
        if (outcome == 0) {
            assert(delivered.phase == IEEE80211_JOIN_DISCOVERY);
            assert(delivered.terminal.cause == IEEE80211_JOIN_FAILURE_NO_NETWORKS);
            assert(!delivered.auth.seen && !delivered.assoc.seen);
        }
        ic.ic_opmode = IEEE80211_M_STA;
    }
    uint64_t sequence = 99, stateGeneration = 99, epoch = 99;
    const uint64_t stateRequest = ieee80211_wcl_join_begin(&ic,
        bssid, ssid, sizeof(ssid));
    assert(ieee80211_wcl_join_state_identity(&ic, &sequence, &stateGeneration, &epoch));
    assert(sequence == stateRequest && stateGeneration == stateRequest &&
        epoch == ic.ic_pae_assoc_epoch);
    ieee80211_wcl_join_cancel(&ic, stateRequest);
    assert(ieee80211_wcl_join_state_identity(&ic, &sequence, &stateGeneration, &epoch));
    assert(sequence == stateRequest && stateGeneration == 0);
    afterUnlock = [&] {
        ++ic.ic_pae_assoc_epoch;
        assert(ieee80211_wcl_join_begin(&ic, bssid, ssid, sizeof(ssid)));
    };
    const uint64_t priorEpoch = ic.ic_pae_assoc_epoch;
    assert(ieee80211_wcl_join_state_identity(&ic, &sequence, &stateGeneration, &epoch));
    assert(sequence == stateRequest && stateGeneration == 0 && epoch == priorEpoch);
    assert(ic.ic_pae_assoc_epoch != epoch &&
        ic.ic_wcl_join_attempt.next_generation != sequence);
    ic.ic_opmode = IEEE80211_M_HOSTAP;
    assert(ieee80211_wcl_join_state_identity(&ic, &sequence, &stateGeneration, &epoch));
    assert(sequence == 0 && stateGeneration == 0 && epoch == 0);
    ic.ic_opmode = IEEE80211_M_STA;
    assert(!ieee80211_wcl_join_state_identity(&ic, nullptr, &stateGeneration, &epoch));
    assert(!ieee80211_wcl_join_state_identity(&ic, &sequence, nullptr, &epoch));
    assert(!ieee80211_wcl_join_state_identity(&ic, &sequence, &stateGeneration, nullptr));
    ic.ic_pae_selected_bss_lock = nullptr;
    sequence = stateGeneration = epoch = 99;
    assert(!ieee80211_wcl_join_state_identity(&ic, &sequence, &stateGeneration, &epoch));
    assert(sequence == 0 && stateGeneration == 0 && epoch == 0);
    assert(!ieee80211_wcl_join_begin(&ic, bssid, ssid, sizeof(ssid)));
    ieee80211_wcl_join_cleanup_done(&ic, generation, 1);
    std::puts("PASS: production join bridge leaf locking, exact epoch, cleanup participants and reentrant replacement");
}
