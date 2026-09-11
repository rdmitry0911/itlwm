// Executes the complete production peer worker, failure claim and retirement.
// Crypto outcome, lower hardware cleanup and task scheduling are explicit
// boundaries; the independent engine suite and on-air controls cover crypto.
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <utility>
#include "itl80211/openbsd/net80211/ieee80211_join_attempt.h"
#define ITL_SAE_DRIVER_CRYPTO 1
#define __IO80211_TARGET 260000
#include "itl80211/openbsd/net80211/ieee80211_sae_engine.h"
using u_int8_t = uint8_t;
using u_int32_t = uint32_t;
using u_int64_t = uint64_t;
using IOInterruptState = unsigned;
enum { IEEE80211_M_STA = 1, IEEE80211_S_SCAN = 1, IEEE80211_S_AUTH = 2,
    IEEE80211_S_ASSOC = 3, IEEE80211_S_RUN = 4, IFF_RUNNING = 2,
    IEEE80211_SAE_WCL_REQUEST_BOUND = 2, IEEE80211_STATUS_SUCCESS = 0,
    IWN_SAE_ENGINE_PEERQ_LEN = 4, IWN_SAE_ENGINE_SUBMIT_OK = 0,
    IWN_SAE_ENGINE_SUBMIT_RETRY = 1, IWN_SAE_ENGINE_SUBMIT_RETRY_LIMIT = 3 };
#define IEEE80211_ADDR_EQ(a, b) (memcmp((a), (b), 6) == 0)
#define IC2IFP(ic) (&(ic)->ic_if)
#define IWN_DIRECT_SAE_TRACE(...) ((void)0)
#define container_of(ptr, type, member) reinterpret_cast<type *>(ptr)
struct IOSimpleLock { bool held = false; };
static unsigned leaves;
static void IOSimpleLockLock(IOSimpleLock *l) { assert(l && !l->held); l->held = true; ++leaves; }
static void IOSimpleLockUnlock(IOSimpleLock *l) { assert(l && l->held); l->held = false; --leaves; }
static IOInterruptState IOSimpleLockLockDisableInterrupt(IOSimpleLock *l) {
    assert(leaves == 0); IOSimpleLockLock(l); return 0;
}
static void IOSimpleLockUnlockEnableInterrupt(IOSimpleLock *l, IOInterruptState) {
    IOSimpleLockUnlock(l); assert(leaves == 0);
}
struct ieee80211_node { uint8_t ni_bssid[6]{}; };
struct ieee80211_sae_peer_rx_admission {
    unsigned active = 1;
    uint64_t association_epoch = 41, relay_generation = 7;
    uint8_t bssid[6]{}, sta[6]{};
};
struct ieee80211_sae_wcl_request {
    unsigned phase = IEEE80211_SAE_WCL_REQUEST_BOUND;
    uint64_t generation = 19, association_epoch = 41;
    uint8_t ssid_len = 4, bssid[6]{}, ssid[32]{};
};
struct ieee80211com {
    int ic_state = IEEE80211_S_AUTH;
    int ic_opmode = IEEE80211_M_STA;
    struct { unsigned if_flags = IFF_RUNNING; } ic_if;
    ieee80211_node *ic_bss = nullptr;
    IOSimpleLock *ic_pae_selected_bss_lock = nullptr;
    uint64_t ic_pae_assoc_epoch = 41, ic_pae_assoc_replace_epoch = 0;
    struct { uint64_t epoch = 41; } ic_pae_selected_bss;
    uint8_t ic_myaddr[6]{};
    ieee80211_sae_peer_rx_admission ic_sae_peer_rx_admission;
    ieee80211_sae_wcl_request ic_sae_wcl_request;
    ieee80211_join_attempt ic_wcl_join_attempt{};
};
static uint64_t ieee80211_pae_assoc_epoch_current(ieee80211com *ic) { return ic->ic_pae_assoc_epoch; }
#include "owner.inc"
struct ieee80211_sae_engine {};
struct iwn_softc {
    ieee80211com sc_ic;
    IOSimpleLock *sc_sae_engine_lock = nullptr, *sc_sae_tx_lock = nullptr;
    iwn_sae_engine_owner sc_sae_engine_owner{};
    ieee80211_sae_engine *sc_sae_engine = nullptr;
    bool sc_sae_engine_stopping = false, sc_sae_engine_detaching = false;
    bool sc_sae_engine_task_ready = true, sc_sae_tx_stopping = false;
    bool sc_sae_tx_active = false;
    unsigned sc_sae_tx_event_count = 0;
    uint32_t sc_sae_engine_lifecycle_generation = 1;
    uint64_t sc_sae_engine_wcl_cancel_generation = 0;
    uint64_t sc_sae_engine_join_failure_generation = 0, sc_sae_tx_join_failure_generation = 0;
};
static unsigned scheduled, genericScans, ownedScans, destroyed, delivered;
static unsigned producerAcks, credentialRetired;
static uint64_t lowerGeneration;
static bool workerLive;
static std::function<void(iwn_softc *)> duringCancel;
static std::function<void()> duringPeer;
static ieee80211_sae_engine_peer_result cryptoResult;
static const ItlSaeAuthPeerEventV1 *borrowedPeer;
static const ItlSaePmkContinuationV1 *borrowedPmk;
static ieee80211_join_failure finalFailure;
static void outsideLeaves() { assert(leaves == 0); }
static bool zeroBytes(const void *p, size_t n) {
    const auto *b = static_cast<const uint8_t *>(p);
    for (size_t i = 0; i < n; ++i) if (b[i]) return false;
    return true;
}
static bool iwn_sae_tx_lifecycle_enter(iwn_softc *, bool) { outsideLeaves(); assert(!workerLive); workerLive = true; return true; }
static void iwn_sae_tx_lifecycle_leave(iwn_softc *) { outsideLeaves(); assert(workerLive); workerLive = false; borrowedPeer = nullptr; borrowedPmk = nullptr; }
static void iwn_sae_engine_schedule_task(iwn_softc *) { outsideLeaves(); ++scheduled; }
static void iwn_sae_tx_schedule_task(iwn_softc *, bool) { outsideLeaves(); ++scheduled; }
static void iwn_sae_engine_reopen_if_current(iwn_softc *, uint32_t) { outsideLeaves(); }
static int ieee80211_wcl_join_failure_pending(ieee80211com *ic, uint64_t gen) {
    outsideLeaves(); return gen && ic->ic_wcl_join_attempt.result.generation == gen &&
        ic->ic_wcl_join_attempt.phase == IEEE80211_JOIN_FAILING;
}
static void ieee80211_wcl_join_cleanup_done(ieee80211com *ic, uint64_t gen, unsigned part) {
    outsideLeaves();
    if (part == IEEE80211_JOIN_CLEANUP_PRODUCER) {
        assert(workerLive && borrowedPeer && zeroBytes(borrowedPeer, sizeof(*borrowedPeer)));
        assert(borrowedPmk && zeroBytes(borrowedPmk, sizeof(*borrowedPmk)));
        ++producerAcks;
    }
    auto &a = ic->ic_wcl_join_attempt;
    if (ieee80211_join_attempt_cleanup_done(&a, gen, part) &&
        ieee80211_join_attempt_take_failure(&a, gen, &finalFailure)) ++delivered;
}
void ieee80211_sae_engine_destroy(ieee80211_sae_engine **engine) { outsideLeaves(); assert(*engine); *engine = nullptr; ++destroyed; }
static void ieee80211_sae_peer_rx_revoke(ieee80211com *, uint64_t, uint64_t) { outsideLeaves(); }
static void iwn_sae_wcl_credential_retire_pending_generation(iwn_softc *, uint64_t) { outsideLeaves(); ++credentialRetired; }
static int ieee80211_new_state(ieee80211com *ic, int state, int) { outsideLeaves(); ++genericScans; ic->ic_state = state; return 0; }
class ItlIwn {
public:
    iwn_softc com;
    static void iwn_sae_engine_task(void *);
    static void iwn_wcl_join_failure_scan(ieee80211com *ic, uint64_t gen) {
        outsideLeaves(); if (ieee80211_wcl_join_failure_pending(ic, gen)) { ++ownedScans; lowerGeneration = gen; }
    }
    void cancelSaeAuthFrame(uint64_t) { outsideLeaves(); }
    void cancelSaeWclCredential(uint64_t) {
        outsideLeaves(); auto action = std::move(duringCancel); duringCancel = {};
        if (action) action(&com);
    }
};
static void IOSleep(unsigned) { outsideLeaves(); }
static unsigned iwn_sae_engine_submit_retry_delay_ms(uint8_t) { return 0; }
static int iwn_sae_engine_start(iwn_softc *) { return 0; }
static int iwn_sae_engine_submit_prepared(iwn_softc *) { return 0; }
int ieee80211_sae_engine_tx_complete(ieee80211_sae_engine *, const ItlSaeAuthTransportEventV1 *) { return 0; }
ieee80211_sae_engine_peer_result ieee80211_sae_engine_handle_peer(
    ieee80211_sae_engine *, const ItlSaeAuthPeerEventV1 *peer, ItlSaePmkContinuationV1 *pmk) {
    outsideLeaves(); borrowedPeer = peer; borrowedPmk = pmk;
    auto action = std::move(duringPeer); duringPeer = {}; if (action) action();
    return cryptoResult;
}
static bool iwn_sae_engine_owner_matches_pmk_identity_locked(iwn_softc *, iwn_sae_engine_owner *, const ItlSaePmkContinuationIdentityV1 *) { return false; }
static bool ieee80211_sae_wcl_request_pmk_claim_locked(ieee80211com *, const ItlSaePmkContinuationV1 *) { return false; }
static int ieee80211_sae_wcl_request_pmk_continue_assoc(ieee80211com *, const ItlSaePmkContinuationIdentityV1 *) { return 0; }
static bool ieee80211_sae_wcl_request_pmk_claim_assoc_current_locked(ieee80211com *, ieee80211_node *, const ItlSaePmkContinuationIdentityV1 *) { return false; }
#include "worker.inc"

int main() {
    unsigned cases = 0;
    for (unsigned scenario = 0; scenario < 19; ++scenario) {
        ItlIwn device;
        auto &sc = device.com;
        auto &ic = sc.sc_ic;
        auto &owner = sc.sc_sae_engine_owner;
        IOSimpleLock bssLock, engineLock, txLock;
        ieee80211_node node;
        ieee80211_sae_engine engine;
        sc.sc_sae_engine_lock = &engineLock; sc.sc_sae_tx_lock = &txLock;
        ic.ic_pae_selected_bss_lock = &bssLock; ic.ic_bss = &node;
        sc.sc_sae_engine = &engine;
        const uint8_t bssid[6] = {2, 1, 2, 3, 4, 5}, sta[6] = {2, 6, 7, 8, 9, 10};
        memcpy(node.ni_bssid, bssid, 6); memcpy(ic.ic_myaddr, sta, 6);
        auto &admission = ic.ic_sae_peer_rx_admission;
        memcpy(admission.bssid, bssid, 6); memcpy(admission.sta, sta, 6);
        auto &request = ic.ic_sae_wcl_request;
        memcpy(request.bssid, bssid, 6); memcpy(request.ssid, "test", 4);
        owner.active = true; owner.request_generation = 19;
        owner.association_epoch = 41; owner.relay_generation = 7;
        memcpy(owner.selected.bssid, bssid, 6); memcpy(owner.selected.sta, sta, 6);
        memcpy(owner.selected.ssid, "test", 4); owner.selected.ssid_len = 4;
        auto &peer = owner.peerq[0];
        peer.version = kItlSaeAuthTransportV1Version; peer.size = sizeof(peer);
        peer.association_epoch = 41; peer.relay_generation = 7;
        peer.phase = kItlSaeAuthTransportPhaseConfirm;
        peer.wire_transaction = kItlSaeAuthTransportPeerWireTransactionConfirm;
        peer.auth_status = 1;
        memcpy(peer.bssid, bssid, 6); memcpy(peer.sta, sta, 6);
        owner.peer_count = 1; owner.peer_tail = 1;
        auto &attempt = ic.ic_wcl_join_attempt;
        attempt.next_generation = UINT64_C(0x100000005);
        const uint64_t gen = ieee80211_join_attempt_begin(&attempt, bssid,
            reinterpret_cast<const uint8_t *>("test"), 4);
        assert(ieee80211_join_attempt_bind(&attempt, gen, 41, bssid,
            reinterpret_cast<const uint8_t *>("test"), 4));
        owner.join_attempt_generation = gen;
        scheduled = genericScans = ownedScans = destroyed = delivered = 0;
        producerAcks = credentialRetired = 0; lowerGeneration = 0;
        duringPeer = {}; duringCancel = {};
        cryptoResult = IEEE80211_SAE_ENGINE_PEER_AP_REJECT;
        bool claim = true;
        switch (scenario) {
        case 1: peer.auth_status = 77; break;
        case 2: cryptoResult = IEEE80211_SAE_ENGINE_PEER_ABORT; break;
        case 3: peer.auth_status = 0; break; // Method mismatch is not peer status 0.
        case 4: peer.association_epoch++; claim = false; break;
        case 5: peer.relay_generation++; claim = false; break;
        case 6: peer.bssid[5]++; claim = false; break;
        case 7: attempt.result.bssid[5]++; claim = false; break;
        case 8: attempt.result.ssid[0]++; claim = false; break;
        case 9: attempt.phase = IEEE80211_JOIN_COMPLETE; claim = false; break;
        case 10: ic.ic_sae_peer_rx_admission.active = 0; claim = false; break;
        case 11: duringPeer = [&] { owner.cancelled = true; }; claim = false; break;
        case 12: duringCancel = [&](iwn_softc *) {
            ieee80211_join_attempt_begin(&attempt, bssid,
                reinterpret_cast<const uint8_t *>("test"), 4);
        }; break;
        case 13: sc.sc_sae_tx_event_count = 1; break;
        case 14: cryptoResult = IEEE80211_SAE_ENGINE_PEER_DROP; claim = false; break;
        case 15: cryptoResult = IEEE80211_SAE_ENGINE_PEER_TX_READY; claim = false; break;
        case 16: ic.ic_opmode = 2; claim = false; break;
        case 17: owner.join_attempt_generation = 0; claim = false; break;
        case 18: duringPeer = [&] {
            const auto next = ieee80211_join_attempt_begin(&attempt, bssid,
                reinterpret_cast<const uint8_t *>("test"), 4);
            assert(ieee80211_join_attempt_bind(&attempt, next, 41, bssid,
                reinterpret_cast<const uint8_t *>("test"), 4));
        }; claim = false; break;
        }
        ItlIwn::iwn_sae_engine_task(&sc);
        assert(!workerLive && leaves == 0 && delivered == 0);
        assert(producerAcks == unsigned(claim));
        if (claim) {
            assert(genericScans == 0 && destroyed == 1);
            assert(owner.active && owner.cancelled && owner.join_failure_generation == gen);
            if (scenario == 12) {
                assert(ownedScans == 0 && attempt.phase == IEEE80211_JOIN_DISCOVERY);
                assert(attempt.result.generation == gen + 1);
            } else {
                assert(ownedScans == 1 && lowerGeneration == gen);
                assert(attempt.cleanup_pending == (IEEE80211_JOIN_CLEANUP_LOWER | IEEE80211_JOIN_CLEANUP_SAE));
                assert(attempt.result.auth.cause == ((scenario == 2 || scenario == 3) ? IEEE80211_JOIN_FAILURE_LOCAL : IEEE80211_JOIN_FAILURE_PEER_STATUS));
                assert(attempt.result.auth.peer_status == ((scenario == 2 || scenario == 3) ? 0 : (scenario == 1 ? 77 : 1)));
                // Model the separately tested physical lower SCAN terminal.
                ic.ic_state = IEEE80211_S_SCAN;
                ieee80211_wcl_join_cleanup_done(&ic, gen, IEEE80211_JOIN_CLEANUP_LOWER);
                assert(delivered == 0);
                iwn_sae_engine_request_join_retirement(&sc, gen);
                ItlIwn::iwn_sae_engine_task(&sc);
                assert(!owner.active && sc.sc_sae_engine == nullptr);
                if (scenario == 13) {
                    assert(delivered == 0);
                    sc.sc_sae_tx_event_count = 0;
                    ItlIwn::iwn_sae_engine_task(&sc);
                }
                assert(delivered == 1 && finalFailure.generation == gen);
                ItlIwn::iwn_sae_engine_task(&sc);
                assert(delivered == 1 && genericScans == 0);
            }
        } else {
            assert(ownedScans == 0 && producerAcks == 0);
            if (scenario == 14 || scenario == 15)
                assert(sc.sc_sae_engine == &engine && genericScans == 0);
        }
        ++cases;
    }
    printf("PASS: %u complete IWN SAE worker failure/retirement scenarios\n", cases);
}
