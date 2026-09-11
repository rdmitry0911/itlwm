// Replays the complete production failure helper across its explicit,
// potentially yielding epoch-cancellation boundary. This is not RF evidence.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <utility>
#ifdef __linux__
#include <sys/prctl.h>
#endif
#include "tests/kernel_memory_test_support.hpp"
using u_int32_t = uint32_t;
#include "leaves.inc"
struct ieee80211com {
    uint32_t ic_wcl_reassoc_owner_active = 0;
    uint32_t ic_wcl_reassoc_owner_last_leaf = 0;
    // Opaque copied request bytes: the production helper only scrubs them.
    uint8_t ic_wcl_reassoc_request[156]{};
    uint8_t ic_wcl_reassoc_source_bssid[6]{};
    uint8_t ic_wcl_reassoc_target_bssid[6]{};
    void (*ic_event_handler)(ieee80211com *, int, void *) = nullptr;
};
static unsigned epochs, events;
static std::function<void(ieee80211com *)> cancelContinuation;
static uint64_t ieee80211_pae_assoc_epoch_begin(ieee80211com *ic) {
    ++epochs;
    auto action = std::move(cancelContinuation);
    cancelContinuation = {};
    if (action) action(ic);
    return epochs;
}
#include "failure.inc"
static void event(ieee80211com *, int code, void *data) {
    assert(code == IEEE80211_EVT_WCL_REASSOC_FAIL && data);
    assert(*static_cast<uint32_t *>(data) != 0);
    ++events;
}
static void admit(ieee80211com *ic, uint8_t identity, uint32_t leaf) {
    ic->ic_wcl_reassoc_owner_active = 1;
    ic->ic_wcl_reassoc_owner_last_leaf = leaf;
    memset(ic->ic_wcl_reassoc_request, identity, sizeof(ic->ic_wcl_reassoc_request));
    memset(ic->ic_wcl_reassoc_source_bssid, identity, 6);
    memset(ic->ic_wcl_reassoc_target_bssid, identity + 1, 6);
    ic->ic_event_handler = event;
}
int main(int argc, char **argv) {
#ifdef __linux__
    assert(prctl(PR_SET_DUMPABLE, 0) == 0);
#endif
    assert(argc == 2);
    const int scenario = atoi(argv[1]);
    ieee80211com ic;
    if (scenario == 0) {
        ieee80211_wcl_reassoc_post_failure(&ic, 5);
        assert(epochs == 0 && events == 0);
        admit(&ic, 1, IEEE80211_WCL_REASSOC_OWNER_LEAF_SETUP);
        ieee80211_wcl_reassoc_post_failure(&ic, 5);
        assert(epochs == 0 && events == 0);
        admit(&ic, 1, IEEE80211_WCL_REASSOC_OWNER_LEAF_SCAN_STARTED);
        ieee80211_wcl_reassoc_post_failure(&ic, 5);
        assert(epochs == 0 && events == 1 && !ic.ic_wcl_reassoc_owner_active);
        events = 0;
        admit(&ic, 1, IEEE80211_WCL_REASSOC_OWNER_LEAF_ROAM_STARTED);
        ieee80211_wcl_reassoc_post_failure(&ic, 5);
        ieee80211_wcl_reassoc_post_failure(&ic, 5);
        assert(epochs == 1 && events == 1 && !ic.ic_wcl_reassoc_owner_active);
        puts("ordinary inactive/setup/scan/switched/duplicate-call controls PASS");
        return 0;
    }
    admit(&ic, 1, IEEE80211_WCL_REASSOC_OWNER_LEAF_ROAM_STARTED);
    if (scenario == 1) {
        cancelContinuation = [](ieee80211com *value) {
            // The epoch function invokes revocation and WCL callbacks after
            // releasing its leaf. Reproduce a later owner's admission there.
            admit(value, 7, IEEE80211_WCL_REASSOC_OWNER_LEAF_SCAN_STARTED);
        };
        ieee80211_wcl_reassoc_post_failure(&ic, 5);
        fprintf(stderr, "replacement: active=%u request=%u target=%u old_events=%u\n",
            ic.ic_wcl_reassoc_owner_active, ic.ic_wcl_reassoc_request[0],
            ic.ic_wcl_reassoc_target_bssid[0], events);
        assert(ic.ic_wcl_reassoc_owner_active == 1);
        assert(ic.ic_wcl_reassoc_request[0] == 7);
        assert(ic.ic_wcl_reassoc_target_bssid[0] == 8);
    } else {
        assert(scenario == 2);
        cancelContinuation = [](ieee80211com *value) {
            ieee80211_wcl_reassoc_post_failure(value, 6);
        };
        ieee80211_wcl_reassoc_post_failure(&ic, 5);
        fprintf(stderr, "reentrant retirement: epochs=%u terminal_events=%u\n", epochs, events);
        assert(events == 1);
    }
}
