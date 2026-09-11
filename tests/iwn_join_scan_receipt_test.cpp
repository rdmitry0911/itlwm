// Production physical-lease functions with value-only hardware/lock fixtures.
// Marking command_submitted below models the doorbell; it is not an RF test.
#include <cassert>
#include "tests/kernel_memory_test_support.hpp"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <utility>
#include "itl80211/openbsd/net80211/ieee80211_join_attempt.h"
using u_int8_t = uint8_t;
using u_int32_t = uint32_t;
using u_int64_t = uint64_t;
enum { IWN_FLAG_SCANNING = 1, IFF_UP = 1, IFF_RUNNING = 2 };
struct IOSimpleLock { bool held = false; };
static void IOSimpleLockLock(IOSimpleLock *l) { assert(!l->held); l->held = true; }
static void IOSimpleLockUnlock(IOSimpleLock *l) { assert(l->held); l->held = false; }
#include "join-iwn-types.inc"
struct ieee80211com {
    struct { void *if_softc = nullptr; unsigned int if_flags = IFF_UP | IFF_RUNNING; } ic_if;
    uint64_t generation = 9;
    uint64_t reassocSerial = 0;
    bool failing = false;
};
struct iwn_softc {
    ieee80211com sc_ic;
    IOSimpleLock *sc_scan_lease_lock = nullptr;
    iwn_scan_lease sc_scan_lease{};
    iwn_wcl_initial_scan_pending sc_wcl_initial_scan_pending{};
    unsigned int sc_flags = 0;
    bool sc_ap_transition_scan_blocked = false;
    bool sc_sae_wcl_admission_reserved = false;
    bool sc_sae_wcl_admission_requires_fresh_scan = false;
    uint64_t sc_sae_join_scan_block_generation = 0;
    uint64_t sc_sae_bss_loss_join_handoff_generation = 0;
    uint64_t sc_scan_lease_next_serial = 0;
    bool sc_scan_lease_replay_pending = false;
    bool sc_scan_lease_replay_task_ready = true;
    uint64_t sc_wcl_join_cleanup_generation = 0;
    IOSimpleLock *sc_sae_engine_lock = nullptr;
    struct { bool active = false; } sc_sae_engine_owner;
    void *sc_sae_engine = nullptr;
    uint64_t sc_sae_engine_wcl_cancel_generation = 0;
    uint64_t sc_sae_engine_join_failure_generation = 0;
    bool sc_sae_engine_task_ready = true;
    bool sc_sae_engine_stopping = false;
    bool sc_sae_engine_detaching = false;
    IOSimpleLock *sc_sae_tx_lock = nullptr;
    bool sc_sae_tx_active = false;
    unsigned int sc_sae_tx_event_count = 0;
    bool sc_sae_tx_stopping = false;
    uint64_t sc_sae_tx_join_failure_generation = 0;
};
static std::function<void()> afterCapture, afterPending;
static int ieee80211_wcl_reassoc_current(ieee80211com *ic, uint64_t serial) {
    return serial != 0 && ic->reassocSerial == serial;
}
static uint64_t ieee80211_wcl_join_scan_generation(ieee80211com *ic) {
    const auto *sc = static_cast<iwn_softc *>(ic->ic_if.if_softc);
    assert(!sc->sc_scan_lease_lock->held);
    const auto result = ic->generation;
    auto action = std::move(afterCapture); afterCapture = {};
    if (action) action();
    return result;
}
static int ieee80211_wcl_join_failure_pending(ieee80211com *ic, uint64_t gen) {
    const bool pending = gen != 0 && gen == ic->generation && ic->failing;
    auto action = std::move(afterPending); afterPending = {};
    if (action) action();
    return pending;
}
static unsigned int scheduled;
static void iwn_scan_lease_schedule_replay_task(iwn_softc *sc) {
    assert(!sc->sc_scan_lease_lock->held);
    scheduled++;
}
static unsigned int saeScheduled, saeRetired, txScheduled;
static void iwn_sae_tx_schedule_task(iwn_softc *sc, bool allowClosed) {
    assert(!sc->sc_sae_tx_lock->held && !allowClosed);
    txScheduled++;
}
static void iwn_sae_engine_schedule_task(iwn_softc *sc) {
    assert(!sc->sc_sae_engine_lock->held);
    saeScheduled++;
}
static void ieee80211_wcl_join_cleanup_done(ieee80211com *ic, uint64_t gen,
    unsigned int participant) {
    const auto *sc = static_cast<iwn_softc *>(ic->ic_if.if_softc);
    assert(!sc->sc_scan_lease_lock->held);
    assert(!sc->sc_sae_engine_lock || !sc->sc_sae_engine_lock->held);
    assert(!sc->sc_sae_tx_lock || !sc->sc_sae_tx_lock->held);
    assert(participant == IEEE80211_JOIN_CLEANUP_SAE);
    if (ieee80211_wcl_join_failure_pending(ic, gen)) saeRetired++;
}
class ItlIwn {
public:
    static void iwn_wcl_join_failure_scan(ieee80211com *, uint64_t);
};
#include "join-iwn-lease.inc"

int main() {
    unsigned int cases = 0;
    for (bool directSae : {false, true}) {
        IOSimpleLock lock;
        iwn_softc sc;
        sc.sc_scan_lease_lock = &lock;
        sc.sc_ic.ic_if.if_softc = &sc;
        sc.sc_scan_lease_next_serial = UINT64_MAX;
        sc.sc_sae_wcl_admission_reserved = directSae;
        sc.sc_sae_wcl_admission_requires_fresh_scan = directSae;
        uint64_t serial = 0;
        assert(!iwn_scan_lease_reserve(&sc, IWN_SCAN_LEASE_GENERIC_FOREGROUND,
            0, 0, nullptr, &serial, directSae ? 17 : 0, 0));
        assert(serial == 0 && sc.sc_scan_lease_next_serial == UINT64_MAX);
        assert(!iwn_scan_lease_live_locked(&sc));
        assert(sc.sc_sae_wcl_admission_reserved == directSae);
        assert(sc.sc_sae_wcl_admission_requires_fresh_scan == directSae);
        assert(sc.sc_sae_join_scan_block_generation == 0);
        ++cases;
    }
    for (unsigned int owner = IWN_SCAN_LEASE_GENERIC_FOREGROUND;
         owner <= IWN_SCAN_LEASE_STANDARD_CONTROLLER; owner++) {
        for (unsigned int outcome = 0; outcome < 5; outcome++) {
            IOSimpleLock lock;
            iwn_softc sc;
            sc.sc_scan_lease_lock = &lock;
            sc.sc_ic.ic_if.if_softc = &sc;
            sc.sc_scan_lease_next_serial = UINT64_C(0x100000000);
            const uint64_t join = sc.sc_ic.generation;
            const uint64_t reassoc = owner == IWN_SCAN_LEASE_GENERIC_BACKGROUND ?
                UINT64_C(0x100000031) : 0;
            sc.sc_ic.reassocSerial = reassoc;
            uint64_t serial = 0;
            uint32_t backend = 0;
            afterCapture = [&] { sc.sc_ic.generation++; };
            assert(iwn_scan_lease_reserve(&sc,
                static_cast<iwn_scan_lease_owner>(owner), 71, 0,
                &backend, &serial, 0, 0, reassoc));
            sc.sc_ic.reassocSerial++;
            assert(sc.sc_scan_lease.reassoc_serial == reassoc);
            afterCapture = {};
            const uint64_t receipt = owner == IWN_SCAN_LEASE_GENERIC_FOREGROUND ? join : 0;
            assert(sc.sc_scan_lease.join_generation == receipt);
            assert(serial > UINT32_MAX);
            iwn_scan_lease_terminal terminal;
            assert(!iwn_scan_lease_claim_terminal(&sc, &terminal));
            sc.sc_scan_lease.command_submitted = true;
            sc.sc_scan_lease.phase = IWN_SCAN_LEASE_ACTIVE;
            uint64_t continued = 0;
            bool wcl = false;
            assert(iwn_scan_lease_begin_continuation(&sc, &continued, &wcl));
            assert(continued == serial && sc.sc_scan_lease.join_generation == receipt);
            assert(sc.sc_scan_lease.reassoc_serial == reassoc);
            assert(!iwn_scan_lease_claim_terminal(&sc, &terminal));
            assert(iwn_scan_lease_restore_continuation(&sc, continued, outcome == 1));
            assert(sc.sc_scan_lease.join_generation == receipt);
            assert(iwn_scan_lease_claim_terminal(&sc, &terminal));
            assert(terminal.join_generation == receipt && terminal.aborted == (outcome == 1));
            assert(terminal.reassoc_serial == reassoc);
            iwn_scan_lease_terminal duplicate;
            assert(!iwn_scan_lease_claim_terminal(&sc, &duplicate));
            sc.sc_wcl_join_cleanup_generation = join;
            assert(!iwn_scan_lease_take_join_cleanup(&sc));
            bool retired = true;
            assert(!iwn_scan_lease_finish_terminal(&sc, serial + 1, &retired));
            assert(!retired && iwn_scan_lease_live_locked(&sc));
            if (outcome == 2) {
                sc.sc_scan_lease.hardware_invalidated = true;
                // The reset owner discards deferred work when fencing hardware.
                sc.sc_wcl_join_cleanup_generation = 0;
            }
            assert(iwn_scan_lease_finish_terminal(&sc, serial, &retired) == (outcome != 2));
            assert(retired == (outcome != 2));
            assert(!iwn_scan_lease_live_locked(&sc));
            assert(sc.sc_scan_lease.join_generation == 0);
            if (outcome == 3) sc.sc_flags |= IWN_FLAG_SCANNING;
            if (outcome == 4) sc.sc_ic.ic_if.if_flags = 0;
            assert(iwn_scan_lease_take_join_cleanup(&sc) ==
                ((outcome == 2 || outcome == 3 || outcome == 4) ? 0 : join));
            sc.sc_flags = 0; sc.sc_ic.ic_if.if_flags = IFF_UP | IFF_RUNNING;
            assert(iwn_scan_lease_take_join_cleanup(&sc) ==
                ((outcome == 3 || outcome == 4) ? join : 0));
            assert(!iwn_scan_lease_take_join_cleanup(&sc));
            cases++;
        }
    }
    IOSimpleLock lock;
    iwn_softc sc;
    sc.sc_scan_lease_lock = &lock; sc.sc_ic.ic_if.if_softc = &sc;
    sc.sc_ic.failing = true;
    sc.sc_scan_lease_replay_pending = true;
    sc.sc_scan_lease.owner = IWN_SCAN_LEASE_WCL_INITIAL;
    sc.sc_scan_lease.phase = IWN_SCAN_LEASE_ACTIVE;
    ItlIwn::iwn_wcl_join_failure_scan(&sc.sc_ic, 9);
    assert(sc.sc_wcl_join_cleanup_generation == 9 && scheduled == 1);
    assert(sc.sc_scan_lease_replay_pending && !sc.sc_scan_lease.abort_requested);
    assert(!iwn_scan_lease_take_join_cleanup(&sc));
    afterPending = [&] {
        sc.sc_ic.generation = 10;
        sc.sc_wcl_join_cleanup_generation = 10;
    };
    ItlIwn::iwn_wcl_join_failure_scan(&sc.sc_ic, 9);
    assert(sc.sc_wcl_join_cleanup_generation == 10 && scheduled == 1);
    iwn_scan_lease_clear_locked(&sc);
    assert(iwn_scan_lease_take_join_cleanup(&sc) == 10);
    sc.sc_scan_lease_replay_task_ready = false;
    ItlIwn::iwn_wcl_join_failure_scan(&sc.sc_ic, 10);
    assert(sc.sc_wcl_join_cleanup_generation == 0 && scheduled == 1);
    sc.sc_scan_lease_replay_task_ready = true;
    sc.sc_scan_lease.hardware_invalidated = true;
    ItlIwn::iwn_wcl_join_failure_scan(&sc.sc_ic, 10);
    assert(sc.sc_wcl_join_cleanup_generation == 0 && scheduled == 1);

    IOSimpleLock engineLock, txLock;
    sc.sc_sae_engine_lock = &engineLock;
    sc.sc_sae_tx_lock = &txLock;
    iwn_sae_engine_request_join_retirement(&sc, 10);
    assert(saeScheduled == 1 && sc.sc_sae_engine_join_failure_generation == 10);
    for (unsigned int blocked = 0; blocked < 7; blocked++) {
        sc.sc_sae_engine_owner.active = blocked == 0;
        sc.sc_sae_engine = blocked == 1 ? &engineLock : nullptr;
        sc.sc_sae_engine_wcl_cancel_generation = blocked == 2 ? 83 : 0;
        sc.sc_sae_engine_stopping = blocked == 3;
        sc.sc_sae_engine_detaching = blocked == 4;
        sc.sc_sae_tx_active = blocked == 5;
        sc.sc_sae_tx_event_count = blocked == 6 ? 1 : 0;
        iwn_sae_engine_finish_join_retirement(&sc);
        assert(saeRetired == 0 && sc.sc_sae_engine_join_failure_generation == 10);
    }
    sc.sc_sae_engine_detaching = false;
    sc.sc_sae_tx_event_count = 0;
    const unsigned int scheduledBeforeWake = saeScheduled;
    iwn_sae_engine_wake_join_retirement(&sc);
    assert(saeScheduled == scheduledBeforeWake + 1);
    iwn_sae_engine_finish_join_retirement(&sc);
    assert(saeRetired == 1 && sc.sc_sae_engine_join_failure_generation == 0);
    iwn_sae_engine_finish_join_retirement(&sc);
    assert(saeRetired == 1);
    iwn_sae_engine_wake_join_retirement(&sc);
    assert(saeScheduled == scheduledBeforeWake + 1);
    iwn_sae_engine_request_join_retirement(&sc, 10);
    sc.sc_ic.generation = 11;
    iwn_sae_engine_finish_join_retirement(&sc);
    assert(saeRetired == 1); // Old worker cannot acknowledge a replacement.
    afterPending = [&] {
        sc.sc_ic.generation = 12;
        sc.sc_sae_engine_join_failure_generation = 12;
    };
    iwn_sae_engine_request_join_retirement(&sc, 11);
    assert(sc.sc_sae_engine_join_failure_generation == 12);
    iwn_sae_engine_finish_join_retirement(&sc);
    assert(saeRetired == 2);
    sc.sc_sae_engine_lock = nullptr;
    iwn_sae_engine_request_join_retirement(&sc, 12);
    assert(saeRetired == 2 && txScheduled == 1);
    assert(sc.sc_sae_tx_join_failure_generation == 12);
    for (unsigned int blocked = 0; blocked < 3; blocked++) {
        sc.sc_sae_tx_stopping = blocked == 0;
        sc.sc_sae_tx_active = blocked == 1;
        sc.sc_sae_tx_event_count = blocked == 2 ? 1 : 0;
        iwn_sae_tx_finish_join_retirement(&sc);
        assert(saeRetired == 2 && sc.sc_sae_tx_join_failure_generation == 12);
    }
    sc.sc_sae_tx_event_count = 0;
    iwn_sae_engine_wake_join_retirement(&sc);
    assert(txScheduled == 2);
    iwn_sae_tx_finish_join_retirement(&sc);
    assert(saeRetired == 3 && sc.sc_sae_tx_join_failure_generation == 0);
    iwn_sae_tx_finish_join_retirement(&sc);
    iwn_sae_engine_wake_join_retirement(&sc);
    assert(saeRetired == 3 && txScheduled == 2);
    iwn_sae_engine_request_join_retirement(&sc, 12);
    sc.sc_ic.generation = 13;
    iwn_sae_tx_finish_join_retirement(&sc);
    assert(saeRetired == 3); // An old transport terminal cannot retire a new join.
    afterPending = [&] {
        sc.sc_ic.generation = 14;
        sc.sc_sae_tx_join_failure_generation = 14;
    };
    iwn_sae_engine_request_join_retirement(&sc, 13);
    assert(sc.sc_sae_tx_join_failure_generation == 14);
    iwn_sae_tx_finish_join_retirement(&sc);
    assert(saeRetired == 4);
    sc.sc_sae_tx_stopping = true;
    const auto noScheduleAfterStop = txScheduled;
    iwn_sae_engine_request_join_retirement(&sc, 14);
    iwn_sae_engine_wake_join_retirement(&sc);
    assert(sc.sc_sae_tx_join_failure_generation == 0);
    assert(txScheduled == noScheduleAfterStop);
    sc.sc_sae_tx_lock = nullptr;
    iwn_sae_engine_request_join_retirement(&sc, 14);
    iwn_sae_tx_finish_join_retirement(&sc);
    assert(saeRetired == 4);
    std::printf("PASS: %u production IWN scan receipts/terminals, dual-band identity, cleanup queue and stale-generation fences\n", cases);
    std::puts("PASS: production IWN engine cleanup waits for owner, crypto, DMA and queued terminals; stale work and stop are fenced");
    std::puts("PASS: absent optional SAE engine retains real transport cleanup; no immediate or stale acknowledgement");
}
