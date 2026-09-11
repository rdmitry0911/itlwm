// Complete production lateattach, release_node and deferred-switch callback.
// Node lookup, the inner join/epoch machinery, driver TX and scheduler are
// explicit fixture boundaries. No hardware completion is synthesized here.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <cerrno>
using u_int8_t = uint8_t;
using u_int64_t = uint64_t;
using u_int = unsigned;
#define IEEE80211_STA_ONLY 1
constexpr unsigned IEEE80211_ADDR_LEN = 6;
constexpr int IPL_NET = 1;
constexpr unsigned IEEE80211_F_BGSCAN = 1, IEEE80211_F_TX_MGMT_ONLY = 2;
constexpr int IEEE80211_S_RUN = 4, IEEE80211_S_SCAN = 1;
constexpr int IEEE80211_STA_CACHE = 1, IEEE80211_STA_COLLECT = 3;
constexpr int IEEE80211_TXPOWER_MAX = 100;
constexpr int IEEE80211_CHAN_ANYC = -1;
constexpr unsigned IEEE80211_WCL_REASSOC_OWNER_LEAF_ROAM_STARTED = 3;
struct ieee80211com;
struct ieee80211_node {
    unsigned ni_refcnt = 0;
    int ni_state = IEEE80211_STA_CACHE, ni_chan = 0;
    void (*ni_unref_cb)(ieee80211com *, ieee80211_node *) = nullptr;
    void *ni_unref_arg = nullptr;
    size_t ni_unref_arg_size = 0;
    uint8_t ni_macaddr[6]{};
};
struct _ifnet {};
struct ieee80211com {
    _ifnet ic_if;
    ieee80211_node *ic_bss = nullptr;
    unsigned ic_flags = IEEE80211_F_BGSCAN;
    unsigned ic_xflags = IEEE80211_F_TX_MGMT_ONLY;
    int ic_state = IEEE80211_S_RUN, ic_txpower = 0;
    unsigned ic_wcl_reassoc_owner_active = 1;
    unsigned ic_wcl_reassoc_owner_last_leaf = IEEE80211_WCL_REASSOC_OWNER_LEAF_ROAM_STARTED;
    uint64_t ic_wcl_reassoc_owner_serial = 31, ic_pae_assoc_epoch = 7;
};
static int splnet() { return 0; }
static void splx(int) {}
static void splassert(int) {}
static void panic(const char *) { assert(false); }
static ieee80211_node *allocated, *sourceCache, *targetCache;
static unsigned joins, failures, stateChanges, frees, callbacks;
static std::function<void(ieee80211com *)> onFailure;
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
    // The real node-copy cleanup owns this argument on the old success path.
    free(ic->ic_bss->ni_unref_arg);
    ic->ic_bss->ni_unref_arg = nullptr;
    ic->ic_bss->ni_unref_arg_size = 0;
}
static void ieee80211_wcl_reassoc_post_failure(ieee80211com *ic, uint32_t) {
    ++failures;
    ic->ic_wcl_reassoc_owner_active = 0;
    if (onFailure) onFailure(ic);
}
static void ieee80211_new_state(ieee80211com *ic, int state, int) {
    ++stateChanges; ic->ic_state = state;
}
static void ieee80211_free_node(ieee80211com *, ieee80211_node *) { assert(false); }
#include "node-ref.inc"
#include "node-switch.inc"

static void successor(ieee80211com *, ieee80211_node *) { ++callbacks; }
static void rearm(ieee80211com *, ieee80211_node *ni) { ni->ni_unref_cb = successor; }

struct Fixture {
    ieee80211com ic;
    ieee80211_node source, cached, target;
    Fixture() {
        allocated = &source; sourceCache = &cached; targetCache = &target;
        source.ni_macaddr[0] = cached.ni_macaddr[0] = 2;
        target.ni_macaddr[0] = 4;
        joins = failures = stateChanges = frees = callbacks = 0; onFailure = {};
        // Execute actual lateattach: the permanent BSS reference is not a
        // guessed fixture constant and survives every following TX release.
        ieee80211_node_lateattach(&ic.ic_if);
        assert(ic.ic_bss == &source && source.ni_refcnt == 1);
    }
    void arm() {
        auto *arg = static_cast<ieee80211_node_switch_bss_arg *>(
            std::calloc(1, sizeof(ieee80211_node_switch_bss_arg)));
        assert(arg);
        memcpy(arg->cur_macaddr, cached.ni_macaddr, 6);
        memcpy(arg->sel_macaddr, target.ni_macaddr, 6);
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
        ieee80211_node_switch_bss(&f.ic, &f.source);
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
        ieee80211_node_switch_bss(&f.ic, &f.source);
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
        ieee80211_node_switch_bss(&f.ic, &f.source);
        assert(failures == 1 && f.ic.ic_wcl_reassoc_owner_serial == 32);
        std::fprintf(stderr, "reentrant successor: state=%d old_state_requests=%u\n",
            f.ic.ic_state, stateChanges);
        assert(stateChanges == 0 && f.ic.ic_state == IEEE80211_S_RUN);
    } else if (scenario == 4) {
        ieee80211_node foreign;
        foreign.ni_unref_cb = rearm;
        ieee80211_ref_node(&foreign);
        ieee80211_release_node(&f.ic, &foreign);
        std::fprintf(stderr, "callback rearm retained=%d\n", foreign.ni_unref_cb == successor);
        assert(foreign.ni_unref_cb == successor);
    } else assert(false);
    std::printf("deferred BSS scenario %d PASS\n", scenario);
}
