// Complete production lateattach, node_copy, release_node, switch callback,
// and both IWN TX completion bodies. Node lookup, inner join/epoch, AP/rate
// feedback, allocation, firmware input and scheduling are explicit boundaries.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <cerrno>
#include <initializer_list>
#include "kernel_memory_test_support.hpp"
#include "../itlwm/hal_iwn/IwnSaeRoamDeparture.hpp"
using u_int8_t = uint8_t;
using u_int64_t = uint64_t;
using u_int32_t = uint32_t;
using u_int = unsigned;
#define IEEE80211_STA_ONLY 1
constexpr unsigned IEEE80211_ADDR_LEN = 6;
constexpr int IPL_NET = 1;
constexpr unsigned IEEE80211_F_BGSCAN = 1, IEEE80211_F_TX_MGMT_ONLY = 2;
constexpr unsigned IEEE80211_F_DISABLE_BG_AUTO_CONNECT = 4;
constexpr unsigned IEEE80211_FC0_SUBTYPE_DEAUTH = 0xc0;
constexpr unsigned IEEE80211_REASON_AUTH_LEAVE = 3;
constexpr int IEEE80211_S_RUN = 4, IEEE80211_S_SCAN = 1;
constexpr int IEEE80211_M_STA = 0;
constexpr int IEEE80211_STA_CACHE = 1, IEEE80211_STA_COLLECT = 3;
constexpr int IEEE80211_TXPOWER_MAX = 100;
constexpr int IEEE80211_CHAN_ANYC = -1;
constexpr unsigned IEEE80211_WCL_REASSOC_OWNER_LEAF_ROAM_STARTED = 3;
constexpr unsigned IEEE80211_WCL_REASSOC_OWNER_LEAF_SCAN_FAILED = 2;
constexpr unsigned IEEE80211_NODE_HT = 1, IWN_RFLAG_MCS = 1;
constexpr unsigned IEEE80211_FC0_SUBTYPE_ASSOC_RESP = 0x10;
constexpr unsigned IWN_POST_PLTI_TRACE_TX_NONE = 0;
#define __IO80211_TARGET 0
#ifndef __MAC_26_0
#define __MAC_26_0 260000
#endif
#define KASSERT(condition, message) assert((condition) && (message))
// Keep diagnostic arguments type checked without printing private peer data.
static void fixture_log(const char *, ...) {}
#define XYLog fixture_log
#define IWX_AUTH_DIAG fixture_log
#define IEEE80211_ADDR_EQ(a, b) (std::memcmp((a), (b), 6) == 0)
#define IEEE80211_ADDR_COPY(a, b) std::memcpy((a), (b), 6)
#define container_of(ptr, type, member) static_cast<type *>((ptr)->owner)
#include "../itl80211/openbsd/net80211/ieee80211_bss_switch.h"
struct IOSimpleLock { bool held = false; };
using IOInterruptState = int;
static int IOSimpleLockLockDisableInterrupt(IOSimpleLock *lock) {
    assert(lock && !lock->held); lock->held = true; return 0;
}
static void IOSimpleLockUnlockEnableInterrupt(IOSimpleLock *lock, int) {
    assert(lock && lock->held); lock->held = false;
}
struct ieee80211com;
struct ieee80211_key { unsigned dummy = 0; };
struct ieee80211_node {
    unsigned ni_refcnt = 0;
    int ni_state = IEEE80211_STA_CACHE, ni_chan = 0;
    void (*ni_unref_cb)(ieee80211com *, ieee80211_node *, void *) = nullptr;
    void *ni_unref_arg = nullptr;
    size_t ni_unref_arg_size = 0;
    uint8_t ni_macaddr[6]{};
    uint8_t ni_bssid[6]{};
    unsigned ni_flags = IEEE80211_NODE_HT;
    int ni_txrate = 0;
    uint8_t *ni_rsnie = nullptr, *ni_rsnie_tlv = nullptr;
    size_t ni_rsnie_tlv_len = 0;
    ieee80211_key ni_pairwise_key;
};
struct Statistics { unsigned outputErrors = 0; };
struct _ifnet { Statistics *netStat = nullptr; };
struct ieee80211com {
    _ifnet ic_if;
    ieee80211_node *ic_bss = nullptr;
    unsigned ic_flags = IEEE80211_F_BGSCAN;
    unsigned ic_xflags = IEEE80211_F_TX_MGMT_ONLY;
    int ic_state = IEEE80211_S_RUN, ic_txpower = 0;
    unsigned ic_wcl_reassoc_owner_active = 1;
    unsigned ic_wcl_reassoc_owner_last_leaf = IEEE80211_WCL_REASSOC_OWNER_LEAF_ROAM_STARTED;
    uint64_t ic_wcl_reassoc_owner_serial = 31, ic_pae_assoc_epoch = 7;
    uint64_t ic_wcl_reassoc_next_serial = 31, ic_wcl_reassoc_source_epoch = 7;
    uint8_t ic_wcl_reassoc_source_bssid[6] = {2};
    uint8_t ic_wcl_reassoc_target_bssid[6] = {4};
    struct { uint64_t next_generation = 5; } ic_wcl_join_attempt;
    IOSimpleLock *ic_pae_selected_bss_lock = nullptr;
    int ic_fixed_mcs = 0;
    int ic_opmode = IEEE80211_M_STA;
};
static int splnet() { return 0; }
static void splx(int) {}
static void splassert(int) {}
static void panic(const char *, ...) { assert(false); }
static ieee80211_node *allocated, *sourceCache, *targetCache;
static unsigned joins, failures, stateChanges, frees, callbacks;
static std::function<void(ieee80211com *)> onFailure;
static std::function<void()> onJoin;
static std::function<void()> onCleanup;
static std::function<void()> onStopAmpdu, onMgmt, onEpoch, onAllocation;
static bool allocationFails;
static unsigned managementSubmissions, epochAdvances;
static int managementError;
static ieee80211_node *ieee80211_alloc_node_helper(ieee80211com *) { return allocated; }
static ieee80211_node *ieee80211_find_node(ieee80211com *, const uint8_t *address) {
    for (auto *node : {sourceCache, targetCache})
        if (node && memcmp(node->ni_macaddr, address, 6) == 0) return node;
    return nullptr;
}
static void fixture_free(void *p) { ++frees; std::free(p); }
#define free(p) fixture_free(p)
static void ieee80211_node_newstate(ieee80211_node *ni, int state) { ni->ni_state = state; }
static void ieee80211_node_join_bss(ieee80211com *ic, ieee80211_node *) {
    ++joins;
    if (onJoin) onJoin();
    // The real node-copy cleanup owns this argument on the old success path.
    if (ic->ic_bss->ni_unref_arg) free(ic->ic_bss->ni_unref_arg);
    ic->ic_bss->ni_unref_arg = nullptr;
    ic->ic_bss->ni_unref_arg_size = 0;
}
static uint64_t ieee80211_wcl_reassoc_post_failure_owned(ieee80211com *ic,
    uint64_t serial, uint32_t) {
    if (!ic->ic_wcl_reassoc_owner_active ||
        ic->ic_wcl_reassoc_owner_serial != serial) return 0;
    ++failures;
    ic->ic_wcl_reassoc_owner_active = 0;
    const uint64_t retiredEpoch = ic->ic_wcl_reassoc_owner_last_leaf ==
        IEEE80211_WCL_REASSOC_OWNER_LEAF_SCAN_FAILED ? ic->ic_pae_assoc_epoch :
        ++ic->ic_pae_assoc_epoch;
    if (onFailure) onFailure(ic);
    return retiredEpoch;
}
static void ieee80211_new_state(ieee80211com *ic, int state, int) {
    ++stateChanges; ic->ic_state = state;
}
static void ieee80211_free_node(ieee80211com *, ieee80211_node *) { assert(false); }
void ieee80211_node_cleanup_internal(ieee80211com *, ieee80211_node *, int);
static uint64_t ieee80211_pae_assoc_epoch_begin(ieee80211com *) {
    if (onCleanup) onCleanup();
    return 1;
}
static int ieee80211_ccmp_key_unpublish_retire(ieee80211com *, ieee80211_key *,
                                               ieee80211_key *) { return EOPNOTSUPP; }
static void ieee80211_delete_key(ieee80211com *, ieee80211_node *, ieee80211_key *) {}
static void ieee80211_ba_del(ieee80211_node *) {}
static void ieee80211_ba_free(ieee80211_node *) {}
static void ieee80211_save_ie(const uint8_t *, uint8_t **) { assert(false); }
static void ieee80211_save_ie_tlv(const uint8_t *, uint8_t **, size_t *, size_t) {
    assert(false);
}
static void ieee80211_node_set_timeouts(ieee80211_node *) {}
static void ieee80211_stop_ampdu_tx(ieee80211com *, ieee80211_node *, unsigned) {
    if (onStopAmpdu) onStopAmpdu();
}
static int fixture_send_mgmt(ieee80211com *, ieee80211_node *, unsigned, unsigned) {
    ++managementSubmissions; if (onMgmt) onMgmt(); return managementError;
}
#define IEEE80211_SEND_MGMT fixture_send_mgmt
static void *fixture_malloc(size_t size) {
    if (onAllocation) onAllocation();
    return allocationFails ? nullptr : std::malloc(size);
}
#define malloc(size, type, flags) fixture_malloc(size)
#include "node-ref.inc"
#include "node-switch.inc"
#undef malloc
// Epoch internals are executed separately in net80211_roam_carrier_test;
// this boundary models the real guarded advance and an out-of-lock callback.
uint64_t ieee80211_pae_assoc_epoch_begin_bss_switch(ieee80211com *ic,
    const ieee80211_bss_switch_identity *identity) {
    const auto irq = IOSimpleLockLockDisableInterrupt(ic->ic_pae_selected_bss_lock);
    const bool current = ieee80211_bss_switch_identity_current_locked(ic, identity);
    const uint64_t epoch = current ? ++ic->ic_pae_assoc_epoch : 0;
    IOSimpleLockUnlockEnableInterrupt(ic->ic_pae_selected_bss_lock, irq);
    if (current) { ++epochAdvances; if (onEpoch) onEpoch(); }
    return epoch;
}

struct iwn_node : ieee80211_node {
    unsigned lq_rate_mismatch = 0;
    struct { unsigned amn_txcnt = 0, amn_retrycnt = 0; } amn;
};
#include "tx_node_retirement_test_support.hpp"
struct iwn_tx_data {
    Packet *m = nullptr;
    ieee80211_node *ni = nullptr;
    unsigned totlen = 0, ampdu_nframes = 0, ampdu_txmcs = 0;
    int txrate = 0;
    unsigned ampdu_rate_generation = 0, ampdu_rate_rflags = 0;
    unsigned ampdu_rate_feedback_valid = 0, tx_apple_nrate = 0;
    unsigned tx_apple_nrate_valid = 0, post_plti_trace_class = 0;
    bool ap_mgmt = false, ap_data = false, sae_active = false;
    uint8_t diag_subtype = 0xff, diag_peer[6]{};
    unsigned diag_auth_seq = 0xffff;
    uint64_t wnm_tx_fence_generation = 0;
    IwnSaeRoamDepartureIdentity sae_roam_departure{};
    uint64_t auth_rxon_serial = 0;
    uint8_t wnm_tx_fence_kind = 0;
    void *map = nullptr;
};
constexpr int IWN_TX_RING_COUNT = 2;
struct IwnDma { void *vaddr = nullptr; size_t size = 0; };
struct iwn_tx_ring {
    iwn_tx_data data[2];
    unsigned queued = 0, cur = 0, read = 0, qid = 0;
    void *desc = nullptr, *cmd = nullptr, *first_tb = nullptr, *ap_payload = nullptr;
    IwnDma desc_dma, cmd_dma, first_tb_dma, ap_payload_dma;
};
struct iwn_rx_desc { unsigned idx = 0; };
struct iwn_softc {
    ieee80211com sc_ic;
    iwn_tx_ring txq[2];
    struct { const char *dv_xname = "fixture"; } sc_dev;
    void *owner = nullptr;
    void *sc_dmat = nullptr;
    unsigned qfullmsk = 0;
};
struct IwnApClientRuntime {
    bool associated = false;
    uint8_t mac[6]{};
    struct { uint32_t generation = 0; } rateControl;
};
static unsigned packetFrees, mapFrees;
static void mbuf_freem(Packet *) { ++packetFrees; }
static void bus_dmamap_destroy(void *, void *) { ++mapFrees; }
static void iwn_dma_contig_free(IwnDma *dma) { *dma = {}; }
#include "tx-node-retire.inc"
static void iwn_sae_tx_data_clear(iwn_tx_data *data) {
    data->sae_active = false;
    data->sae_roam_departure = {};
}
static unsigned departureTerminals;
static void iwn_post_plti_trace_record_completion(ieee80211com *, unsigned) {}
static void ieee80211_wnm_bss_transition_tx_fence_complete(
    ieee80211com *, ieee80211_node *, uint64_t, uint8_t) { assert(false); }
struct ItlIwn {
    iwn_softc com;
    struct { unsigned rsnIELength = 0; } apFirmwareConfig;
    ItlIwn() { com.owner = this; }
    static void iwn_tx_done_free_txdata(iwn_softc *, iwn_tx_data *, mbuf_list *);
    void iwn_reset_tx_ring(iwn_softc *, iwn_tx_ring *);
    void iwn_free_tx_ring(iwn_softc *, iwn_tx_ring *);
    void iwn_tx_done(iwn_softc *, iwn_rx_desc *, uint8_t, uint8_t, uint8_t,
                     int, int, uint16_t);
    IwnApClientRuntime *iwn_find_ap_client(const uint8_t *) { return nullptr; }
    void iwn_select_ap_client(IwnApClientRuntime *) { assert(false); }
    void iwn_begin_ap_4way() { assert(false); }
    static bool iwn_ap_rate_feedback_matches(IwnApClientRuntime *, uint8_t, uint8_t) {
        assert(false); return false;
    }
    static void iwn_ap_dvm_selected_rate_sample(uint8_t, int, uint16_t *, uint16_t *) {
        assert(false);
    }
    static int iwn_ap_rate_control_feedback(IwnApClientRuntime *, uint8_t, uint8_t,
                                            uint16_t, uint16_t, bool, uint32_t) {
        assert(false); return 0;
    }
    static void iwn_clear_oactive(iwn_softc *, iwn_tx_ring *) {}
    static void iwn_refresh_tx_timer(iwn_softc *) {}
    static void iwn_publish_apple_nrate(iwn_softc *, unsigned) {}
    static bool iwn_build_ht_apple_nrate(uint8_t, uint8_t, unsigned *) { return false; }
    static void iwn_ht_single_rate_control(iwn_softc *, ieee80211_node *, uint8_t,
                                           uint8_t, uint8_t, int) {}
    static void iwn_set_link_quality(iwn_softc *, ieee80211_node *) {}
    void iwn_sae_tx_report_terminal(iwn_softc *, iwn_tx_data *data, int error) {
        assert(error == EIO); data->sae_active = false;
    }
    void iwn_sae_roam_departure_terminal(iwn_softc *sc,
        const IwnSaeRoamDepartureIdentity *identity, bool failed) {
        assert(identity->ticket == 41 && !failed);
        const auto &ring = sc->txq[0];
        assert(ring.queued == 0 && ring.data[0].ni == nullptr &&
            ring.data[0].m == nullptr && ring.data[0].sae_roam_departure.ticket == 0);
        assert(sc->sc_ic.ic_bss->ni_refcnt == 0 && joins == 0);
        ++departureTerminals;
    }
};
#include "iwn-terminal.inc"

static unsigned successorArgument, predecessorArgument;
static void successor(ieee80211com *, ieee80211_node *, void *argument) {
    assert(argument == nullptr || argument == &successorArgument);
    ++callbacks;
}
static void rearm(ieee80211com *, ieee80211_node *ni, void *argument) {
    assert(argument == &predecessorArgument);
    assert(ni->ni_unref_cb == nullptr && ni->ni_unref_arg == nullptr &&
        ni->ni_unref_arg_size == 0);
    ni->ni_unref_cb = successor;
    ni->ni_unref_arg = &successorArgument;
    ni->ni_unref_arg_size = sizeof(successorArgument);
}
static void reacquire(ieee80211com *ic, ieee80211_node *ni, void *argument) {
    rearm(ic, ni, argument);
    ieee80211_ref_node(ni);
}
static void deliver(ieee80211com *ic, ieee80211_node *ni) {
    auto callback = ni->ni_unref_cb;
    void *argument = ni->ni_unref_arg;
    ni->ni_unref_cb = nullptr;
    ni->ni_unref_arg = nullptr;
    ni->ni_unref_arg_size = 0;
    callback(ic, ni, argument);
}

struct Fixture {
    ieee80211com ic;
    IOSimpleLock lock;
    iwn_node source;
    ieee80211_node cached, target;
    Fixture() {
        allocated = &source; sourceCache = &cached; targetCache = &target;
        source.ni_macaddr[0] = cached.ni_macaddr[0] = 2;
        target.ni_macaddr[0] = 4;
        source.ni_bssid[0] = cached.ni_bssid[0] = 2;
        target.ni_bssid[0] = 4;
        ic.ic_pae_selected_bss_lock = &lock;
        joins = failures = stateChanges = frees = callbacks = 0;
        onFailure = {}; onJoin = {}; onCleanup = {};
        onStopAmpdu = {}; onMgmt = {}; onEpoch = {};
        onAllocation = {}; allocationFails = false;
        managementSubmissions = epochAdvances = 0; managementError = 0;
        // Actual lateattach owns one reference, but actual node_copy below
        // overwrites it. A post-join test must include that production edge.
        ieee80211_node_lateattach(&ic.ic_if);
        assert(ic.ic_bss == &source && source.ni_refcnt == 1);
    }
    void arm() {
        auto *arg = static_cast<ieee80211_node_switch_bss_arg *>(
            std::calloc(1, sizeof(ieee80211_node_switch_bss_arg)));
        assert(arg);
        assert(ieee80211_bss_switch_identity_capture(&ic, &source, &target,
            ic.ic_wcl_reassoc_owner_active ? ic.ic_wcl_reassoc_owner_serial : 0,
            &arg->identity));
        source.ni_unref_arg = arg;
        source.ni_unref_arg_size = sizeof(*arg);
        source.ni_unref_cb = ieee80211_node_switch_bss;
    }
};

int main(int argc, char **argv) {
    assert(argc == 2);
    const int scenario = std::atoi(argv[1]);
    Fixture f;
    if (scenario == 0) {
        // Old zero-reference semantics still work for an ordinary cache node.
        ieee80211_node foreign;
        foreign.ni_unref_cb = successor;
        ieee80211_ref_node(&foreign);
        ieee80211_release_node(&f.ic, &foreign);
        assert(callbacks == 1 && !foreign.ni_unref_cb);
        // Positive control for the complete switch callback itself.
        f.arm();
        deliver(&f.ic, &f.source);
        assert(joins == 1 && failures == 0 && stateChanges == 0 && frees == 1);
    } else if (scenario == 1) {
        f.arm();
        ieee80211_ref_node(&f.source); // Model one TX reference with the real primitive.
        ieee80211_release_node(&f.ic, &f.source); // Complete production release.
        assert(f.source.ni_refcnt == 1);
        std::fprintf(stderr, "drained: refs=%u joins=%u callback_pending=%d\n",
            f.source.ni_refcnt, joins, f.source.ni_unref_cb != nullptr);
        assert(joins == 1 && "all transient TX references drained but roam never starts");
    } else if (scenario == 2) {
        f.arm();
        f.ic.ic_wcl_reassoc_owner_serial = 32; // Later accepted roam.
        f.ic.ic_flags = 0; // Old scan cancelled before its deferred delivery.
        deliver(&f.ic, &f.source);
        std::fprintf(stderr, "late callback: active=%u failures=%u current_serial=%llu\n",
            f.ic.ic_wcl_reassoc_owner_active, failures,
            static_cast<unsigned long long>(f.ic.ic_wcl_reassoc_owner_serial));
        assert(failures == 0 && f.ic.ic_wcl_reassoc_owner_active == 1);
    } else if (scenario == 3) {
        f.arm(); targetCache = nullptr;
        onFailure = [](ieee80211com *ic) {
            ic->ic_wcl_reassoc_owner_serial = 32;
            ic->ic_wcl_reassoc_owner_active = 1;
            ic->ic_flags = IEEE80211_F_BGSCAN;
        };
        deliver(&f.ic, &f.source);
        assert(failures == 1 && f.ic.ic_wcl_reassoc_owner_serial == 32);
        std::fprintf(stderr, "reentrant successor: state=%d old_state_requests=%u\n",
            f.ic.ic_state, stateChanges);
        assert(stateChanges == 0 && f.ic.ic_state == IEEE80211_S_RUN);
    } else if (scenario == 4) {
        ieee80211_node foreign;
        foreign.ni_unref_cb = rearm;
        foreign.ni_unref_arg = &predecessorArgument;
        foreign.ni_unref_arg_size = sizeof(predecessorArgument);
        ieee80211_ref_node(&foreign);
        ieee80211_release_node(&f.ic, &foreign);
        std::fprintf(stderr, "callback rearm retained=%d\n", foreign.ni_unref_cb == successor);
        assert(foreign.ni_unref_cb == successor);
        assert(foreign.ni_unref_arg == &successorArgument &&
            foreign.ni_unref_arg_size == sizeof(successorArgument));
        ieee80211_ref_node(&foreign);
        ieee80211_release_node(&f.ic, &foreign);
        assert(callbacks == 1 && foreign.ni_unref_cb == nullptr &&
            foreign.ni_unref_arg == nullptr && foreign.ni_unref_arg_size == 0);
    } else if (scenario == 5) {
        ieee80211_node_copy(&f.ic, &f.source, &f.cached);
        assert(f.source.ni_refcnt == 0);
        f.arm();
        ieee80211_ref_node(&f.source);
        ieee80211_release_node(&f.ic, &f.source);
        std::fprintf(stderr, "actual post-copy release: refs=%u joins=%u\n",
            f.source.ni_refcnt, joins);
        assert(joins == 1 && f.source.ni_refcnt == 0);
    } else if (scenario == 6 || scenario == 27) {
        ieee80211_node_copy(&f.ic, &f.source, &f.cached);
        assert(f.source.ni_refcnt == 0);
        if (scenario == 6) f.arm();
        ItlIwn driver;
        Statistics stats;
        driver.com.sc_ic = f.ic;
        driver.com.sc_ic.ic_if.netStat = &stats;
        auto &ring = driver.com.txq[0];
        auto &data = ring.data[0];
        Packet packet;
        data.m = &packet;
        data.ni = ieee80211_ref_node(&f.source);
        data.totlen = 1400;
        if (scenario == 27) data.sae_roam_departure.ticket = 41;
        ring.queued = 1;
        bool retiredAtSwitch = false;
        onJoin = [&] {
            retiredAtSwitch = ring.queued == 0 && data.ni == nullptr && data.totlen == 0;
            std::fprintf(stderr, "inside actual TX callback: queued=%u node_live=%d length=%u\n",
                ring.queued, data.ni != nullptr, data.totlen);
        };
        iwn_rx_desc done;
        driver.iwn_tx_done(&driver.com, &done, 0, 0, 0, 0, 0, 1400);
        assert(ring.queued == 0 && data.ni == nullptr && data.totlen == 0);
        if (scenario == 6) {
            assert(joins == 1);
            assert(retiredAtSwitch && "BSS switch reenters before the source TX descriptor retires");
        } else assert(departureTerminals == 1 && joins == 0);
    } else if (scenario == 7) {
        f.arm();
        onCleanup = [&] {
            assert(f.source.ni_unref_cb == nullptr && f.source.ni_unref_arg == nullptr &&
                f.source.ni_unref_arg_size == 0 && frees == 1);
            f.source.ni_unref_cb = successor;
            f.source.ni_unref_arg = &successorArgument;
            f.source.ni_unref_arg_size = sizeof(successorArgument);
        };
        ieee80211_node_cleanup_internal(&f.ic, &f.source, 1);
        assert(frees == 1 && f.source.ni_unref_cb == successor &&
            f.source.ni_unref_arg == &successorArgument &&
            f.source.ni_unref_arg_size == sizeof(successorArgument));
        onCleanup = {};
    } else if (scenario == 8) {
        ieee80211_node collected;
        collected.ni_state = IEEE80211_STA_COLLECT;
        collected.ni_unref_cb = reacquire;
        collected.ni_unref_arg = &predecessorArgument;
        collected.ni_unref_arg_size = sizeof(predecessorArgument);
        ieee80211_ref_node(&collected);
        ieee80211_release_node(&f.ic, &collected);
        assert(collected.ni_refcnt == 1 && collected.ni_unref_cb == successor &&
            collected.ni_unref_arg == &successorArgument);
        collected.ni_state = IEEE80211_STA_CACHE;
        ieee80211_release_node(&f.ic, &collected);
        assert(callbacks == 1 && collected.ni_refcnt == 0);
    } else if (scenario >= 9 && scenario <= 12) {
        ieee80211_node_copy(&f.ic, &f.source, &f.cached);
        ItlIwn driver;
        driver.com.sc_ic = f.ic;
        auto &ring = driver.com.txq[0];
        unsigned descriptors[2] = {1, 1};
        Packet packets[2];
        ring.desc = descriptors;
        ring.desc_dma = {descriptors, sizeof(descriptors)};
        ring.queued = 2; ring.cur = ring.read = 1;
        const bool orphan = scenario == 12;
        const bool freeRing = scenario == 10;
        packetFrees = mapFrees = 0;
        for (unsigned i = 0; i < 2; ++i) {
            auto &data = ring.data[i];
            data.m = orphan ? nullptr : &packets[i];
            data.ni = ieee80211_ref_node(&f.source);
            data.map = &packets[i];
            data.totlen = 1400;
            data.sae_active = scenario == 11;
        }
        f.arm();
        onJoin = [&] {
            assert(ring.queued == 0 && ring.cur == 0 && ring.read == 0);
            for (const auto &data : ring.data)
                assert(data.ni == nullptr && data.m == nullptr);
            if (!freeRing) assert(descriptors[0] == 0 && descriptors[1] == 0);
        };
        if (freeRing) driver.iwn_free_tx_ring(&driver.com, &ring);
        else driver.iwn_reset_tx_ring(&driver.com, &ring);
        assert(joins == 1 && f.source.ni_refcnt == 0);
        assert(packetFrees == (orphan ? 0 : 2));
        if (freeRing) assert(mapFrees == 2);
    } else if (scenario >= 13 && scenario <= 23) {
        // Execute the complete common producer, not a hand-built argument.
        ieee80211_node_copy(&f.ic, &f.source, &f.cached);
        auto replace = [&] {
            ++f.ic.ic_wcl_reassoc_next_serial;
            f.ic.ic_wcl_reassoc_owner_serial = f.ic.ic_wcl_reassoc_next_serial;
            f.ic.ic_flags = IEEE80211_F_BGSCAN;
            f.ic.ic_xflags = 0;
        };
        if (scenario == 14) onStopAmpdu = replace;
        if (scenario == 15) onMgmt = replace;
        if (scenario == 16) onEpoch = replace;
        if (scenario == 17) managementError = EIO;
        if (scenario == 22 || scenario == 23) f.ic.ic_wcl_reassoc_owner_active = 0;
        const auto serial = f.ic.ic_wcl_reassoc_owner_active ? 31 : 0;
        const auto error = ieee80211_node_defer_bss_switch(&f.ic, &f.source,
            &f.target, serial);
        if (scenario >= 14 && scenario <= 17) {
            assert(error != 0 && f.source.ni_unref_cb == nullptr && joins == 0);
            if (scenario == 14) assert(managementSubmissions == 0 && epochAdvances == 0);
            if (scenario == 15) assert(managementSubmissions == 1 && epochAdvances == 0);
            if (scenario == 16) assert(epochAdvances == 1 && f.ic.ic_xflags == 0);
            if (scenario == 17) assert(failures == 1 && stateChanges == 0 &&
                f.ic.ic_pae_assoc_epoch == 7 && f.ic.ic_state == IEEE80211_S_RUN);
        } else {
            assert(error == 0 && f.source.ni_unref_cb != nullptr);
            assert(epochAdvances == 1 && f.ic.ic_pae_assoc_epoch == 8);
            if (scenario == 18) ++f.ic.ic_wcl_join_attempt.next_generation;
            if (scenario == 19) ++f.ic.ic_pae_assoc_epoch;
            if (scenario == 20) f.target.ni_bssid[1] = 1;
            if (scenario == 21) {
                targetCache = nullptr;
                onFailure = [](ieee80211com *ic) { ++ic->ic_pae_assoc_epoch; };
            }
            if (scenario == 22) { f.ic.ic_wcl_reassoc_owner_active = 1; replace(); }
            deliver(&f.ic, &f.source);
            if (scenario == 13 || scenario == 23) assert(joins == 1 && failures == 0);
            else {
                assert(joins == 0);
                if (scenario == 20) assert(failures == 1 && stateChanges == 1);
                else if (scenario == 21) assert(failures == 1 && stateChanges == 0);
                else assert(failures == 0 && stateChanges == 0);
            }
        }
    } else if (scenario == 24) {
        // An established source has refcount zero after the actual copy.
        // Model the DEAUTH completion before SEND_MGMT returns, executing
        // the real ref/release functions and the entire producer around it.
        ieee80211_node_copy(&f.ic, &f.source, &f.cached);
        onMgmt = [&] {
            ieee80211_ref_node(&f.source);
            ieee80211_release_node(&f.ic, &f.source);
        };
        assert(ieee80211_node_defer_bss_switch(&f.ic, &f.source, &f.target, 31) == 0);
        std::fprintf(stderr, "terminal-before-arm: refs=%u joins=%u pending=%d\n",
            f.source.ni_refcnt, joins, f.source.ni_unref_cb != nullptr);
        assert(joins == 1 && "already completed source TX cannot strand the handoff");
    } else if (scenario == 25 || scenario == 26) {
        ieee80211_node_copy(&f.ic, &f.source, &f.cached);
        if (scenario == 25) onAllocation = [&] {
            ++f.ic.ic_wcl_join_attempt.next_generation;
            f.ic.ic_xflags = 0;
        };
        else allocationFails = true;
        assert(ieee80211_node_defer_bss_switch(&f.ic, &f.source, &f.target, 31) != 0);
        assert(managementSubmissions == 0 && epochAdvances == 0 &&
            f.source.ni_unref_cb == nullptr && joins == 0 && stateChanges == 0 &&
            f.ic.ic_pae_assoc_epoch == 7);
        assert(failures == (scenario == 26 ? 1 : 0));
    } else assert(false);
    std::printf("deferred BSS scenario %d PASS\n", scenario);
}
