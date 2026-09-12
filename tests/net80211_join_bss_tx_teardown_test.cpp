#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>
#include <sys/types.h>
using u_int64_t = uint64_t;
#define IEEE80211_STA_ONLY
#define IEEE80211_ADDR_EQ(a,b) (std::memcmp(a,b,6)==0)
enum ieee80211_phymode { ModeA, ModeB };
enum { IEEE80211_M_STA, IEEE80211_M_HOSTAP, IEEE80211_S_SCAN,
       IEEE80211_S_AUTH, IEEE80211_S_RUN,
       IEEE80211_SAE_WCL_REQUEST_BIND_REJECTED = -1,
       IEEE80211_CIPHER_USEGROUP = 0 };
enum { IEEE80211_STA_CACHE, IEEE80211_STA_BSS, IEEE80211_STA_AUTH,
       IEEE80211_STA_ASSOC, IEEE80211_STA_COLLECT };
constexpr unsigned IEEE80211_NWID_LEN=32;
constexpr unsigned IEEE80211_ADDR_LEN=6;
enum { IEEE80211_F_RSNON=1, IEEE80211_F_WEPON=2, IEEE80211_F_BGSCAN=4,
       IEEE80211_F_DISABLE_BG_AUTO_CONNECT=8,
       IEEE80211_F_DOSORT=1, IEEE80211_F_DOFRATE=2,
       IEEE80211_F_DONEGO=4, IEEE80211_F_DODEL=8,
       IEEE80211_FC0_SUBTYPE_DEAUTH=0xc0, IEEE80211_FC0_SUBTYPE_AUTH=0xb0 };
enum { kAirportItlwmPostPltiTraceEventBssSelected,
       kAirportItlwmPostPltiTraceEventJoinBssEntered };
enum Event { Begin, StopTx, Epoch, Copy, RetireCache, Capture, Bind, Auth, End };
static std::vector<Event> events;
static bool admitted=true, bindingRejected=false;
static unsigned queued=0, releases=0;
struct ieee80211_node {
    int ni_chan=0;
    uint8_t ni_macaddr[6]={}, ni_bssid[6]={}, ni_essid[32]={}, ni_esslen=0;
    int ni_rsncipher=0, ni_state=IEEE80211_STA_CACHE;
    uint32_t ni_assoc_fail=0;
    bool aggregate=false;
};
struct ieee80211com {
    ieee80211_node *ic_bss;
    std::vector<ieee80211_node *> ic_tree;
    ieee80211_phymode ic_curmode=ModeA;
    int ic_des_esslen=0, ic_opmode=IEEE80211_M_STA,
        ic_state=IEEE80211_S_RUN, ic_flags=IEEE80211_F_RSNON|IEEE80211_F_BGSCAN,
        ic_mgt_timer=5, ic_bgscan_timeout=0;
    uint8_t ic_des_essid[32]={};
    void (*ic_node_copy)(ieee80211com *, ieee80211_node *, const ieee80211_node *);
    int (*ic_newstate_preflight)(ieee80211com *, int, int)=nullptr;
    int (*ic_newstate)(ieee80211com *, int, int);
};
static bool ieee80211_sae_wcl_request_join_begin(ieee80211com *) {
    events.push_back(Begin); return admitted;
}
static void ieee80211_sae_wcl_request_join_end(ieee80211com *) { events.push_back(End); }
static void AirportItlwmPostPltiTraceRecord(ieee80211com *, int) {}
static void AirportItlwmPostPltiTraceNoteStateRequest(ieee80211com *, uint32_t, uint32_t) {}
static ieee80211_phymode ieee80211_chan2mode(ieee80211com *, int) { return ModeA; }
static void ieee80211_setmode(ieee80211com *, ieee80211_phymode) {}
[[maybe_unused]] static void ieee80211_stop_ampdu_tx(ieee80211com *ic, ieee80211_node *ni, int mgt) {
    assert(ni == ic->ic_bss && mgt == -1);
    events.push_back(StopTx);
    releases += queued;
    queued=0;
    ni->aggregate=false;
}
static uint64_t ieee80211_pae_assoc_epoch_begin_replacement(ieee80211com *) {
    events.push_back(Epoch); return 2;
}
static void copy_node(ieee80211com *ic, ieee80211_node *dst, const ieee80211_node *src) {
    assert(dst == ic->ic_bss);
    // Replacing old BA state must not strand its hardware-owned packets.
    assert(queued == 0 && !dst->aggregate);
    events.push_back(Copy);
    *dst=*src;
}
static uint8_t ieee80211_sae_selected_bss_profile(ieee80211_node *) { return 3; }
static void ieee80211_pae_selected_bss_capture(ieee80211com *, ieee80211_node *, uint8_t, uint64_t) {
    events.push_back(Capture);
}
static int ieee80211_sae_wcl_request_bind_selected_bss(ieee80211com *, ieee80211_node *, uint64_t) {
    events.push_back(Bind); return bindingRejected ? -1 : 0;
}
static int newstate(ieee80211com *ic, int state, int) {
    assert(queued == 0); ic->ic_state=state; events.push_back(Auth); return 0;
}
static void ieee80211_new_state(ieee80211com *ic, int state, int arg) { newstate(ic,state,arg); }
static void ieee80211_fix_rate(ieee80211com *, ieee80211_node *, int) {}
static void ieee80211_choose_rsnparams(ieee80211com *) {}
static void ieee80211_node_newstate(ieee80211_node *node, int state) { node->ni_state=state; }
// Only the tree traversal and networking serialization primitives are
// fixtures. The cleanup and SSID refresh below are production functions.
static int splnet() { events.push_back(RetireCache); return 0; }
static void splx(int value) { assert(value == 0); }
static ieee80211_node *tree_first(std::vector<ieee80211_node *> *tree) {
    return tree->empty() ? nullptr : tree->front();
}
static ieee80211_node *tree_next(std::vector<ieee80211_node *> *tree, ieee80211_node *node) {
    auto pos=std::find(tree->begin(),tree->end(),node);
    assert(pos != tree->end());
    return ++pos == tree->end() ? nullptr : *pos;
}
#define RB_MIN(kind,tree) tree_first(tree)
#define RB_NEXT(kind,tree,node) tree_next(tree,node)
void ieee80211_clean_sta_bss_node(ieee80211com *);
static void timeout_del(int *) {}
// This fixture tests TX and cache ownership. Carrier behavior is exercised with
// the actual production helpers in net80211_roam_carrier_test.cpp.
[[maybe_unused]] static uint64_t ieee80211_roam_link_source_epoch(
    const ieee80211com *, const ieee80211_node *) { return 0; }
[[maybe_unused]] static void ieee80211_roam_link_begin(ieee80211com *, uint64_t, uint64_t) {}
[[maybe_unused]] static void ieee80211_roam_link_failed(ieee80211com *, uint64_t) {}
#include "production.inc"

static void run_case(int mode, int state, bool allow, bool sameIdentity, bool rejectBinding) {
    events.clear(); admitted=allow; bindingRejected=rejectBinding;
    ieee80211_node old, target, cachedOld, authenticated, associated, retiring;
    old.ni_macaddr[0]=2; target.ni_macaddr[0]=sameIdentity ? 2 : 4;
    old.ni_bssid[0]=2; target.ni_bssid[0]=sameIdentity ? 2 : 4;
    old.ni_state=IEEE80211_STA_BSS;
    old.ni_esslen=3; std::memcpy(old.ni_essid,"old",3);
    target.ni_esslen=6; std::memcpy(target.ni_essid,"target",6);
    cachedOld=old;
    authenticated.ni_state=IEEE80211_STA_AUTH;
    associated.ni_state=IEEE80211_STA_ASSOC;
    retiring.ni_state=IEEE80211_STA_COLLECT;
    const bool liveSta = mode == IEEE80211_M_STA && state == IEEE80211_S_RUN;
    old.aggregate=liveSta;
    queued=old.aggregate ? 3 : 0; releases=0;
    ieee80211com ic;
    ic.ic_bss=&old; ic.ic_node_copy=copy_node; ic.ic_newstate=newstate; ic.ic_state=state;
    ic.ic_opmode=mode;
    ic.ic_tree={&cachedOld,&target,&authenticated,&associated,&retiring};
    ieee80211_node_join_bss(&ic,&target,0);
    if (!allow) {
        assert((events == std::vector<Event>{Begin}));
        assert(queued == (liveSta ? 3U : 0U) && releases == 0);
        assert(cachedOld.ni_state == IEEE80211_STA_BSS && target.ni_state == IEEE80211_STA_CACHE);
        return;
    }
    auto expected=std::vector<Event>{Begin};
    if (liveSta) expected.push_back(StopTx);
    for (auto e : {Epoch,Copy}) expected.push_back(e);
    if (mode == IEEE80211_M_STA) expected.push_back(RetireCache);
    for (auto e : {Capture,Bind,Auth,End}) expected.push_back(e);
    assert(events == expected);
    assert(releases == (liveSta ? 3U : 0U));
    assert(ic.ic_state == (rejectBinding ? IEEE80211_S_SCAN : IEEE80211_S_AUTH));
    const bool retired = mode == IEEE80211_M_STA && !sameIdentity;
    assert(cachedOld.ni_state == (retired ? IEEE80211_STA_CACHE : IEEE80211_STA_BSS));
    assert(authenticated.ni_state == IEEE80211_STA_AUTH);
    assert(associated.ni_state == IEEE80211_STA_ASSOC);
    assert(retiring.ni_state == IEEE80211_STA_COLLECT);
    // The live regression is an already-used BSSID advertising a new name
    // after a different BSS replaced it. Preserve actual source/peer guards.
    const uint8_t renamed[] = {0,3,'n','e','w'};
    ieee80211_refresh_scan_ssid(&ic,&cachedOld,renamed);
    assert(std::memcmp(cachedOld.ni_essid,retired ? "new" : "old",3)==0);
    ieee80211_refresh_scan_ssid(&ic,ic.ic_bss,renamed);
    assert(ic.ic_bss->ni_esslen==6 && std::memcmp(ic.ic_bss->ni_essid,"target",6)==0);
    for (auto *peer : {&authenticated,&associated,&retiring}) {
        ieee80211_refresh_scan_ssid(&ic,peer,renamed);
        assert(peer->ni_esslen==0);
    }
}
int main() {
    for (int mode : {IEEE80211_M_STA,IEEE80211_M_HOSTAP})
        for (int state : {IEEE80211_S_RUN,IEEE80211_S_SCAN,IEEE80211_S_AUTH})
            for (bool allow : {true,false})
                for (bool same : {false,true})
                    for (bool reject : {false,true}) run_case(mode,state,allow,same,reject);
    std::puts("PASS: actual BSS replacement retires TX before copy and stale BSS cache ownership after copy; active/peer SSID identities stay protected");
}
