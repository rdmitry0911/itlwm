#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>
using u_int64_t = uint64_t;
#define IEEE80211_STA_ONLY
#define __IO80211_TARGET 260000
#define __MAC_26_0 260000
#define IEEE80211_NWID_LEN 32
#define IEEE80211_ADDR_EQ(a,b) (std::memcmp(a,b,6)==0)
#include "constants.inc"
enum ieee80211_phymode { ModeA };
enum ieee80211_state { IEEE80211_S_INIT, IEEE80211_S_SCAN, IEEE80211_S_AUTH,
                       IEEE80211_S_ASSOC, IEEE80211_S_RUN };
enum { IEEE80211_M_STA, IEEE80211_M_HOSTAP, IEEE80211_M_MONITOR,
       IEEE80211_STA_BSS, IEEE80211_CIPHER_USEGROUP=0,
       IEEE80211_SAE_WCL_REQUEST_BIND_REJECTED=-1 };
enum { LINK_STATE_DOWN, LINK_STATE_UP, LINK_STATE_UNKNOWN };
enum { IEEE80211_F_BGSCAN=4, IEEE80211_F_DISABLE_BG_AUTO_CONNECT=8,
       IEEE80211_F_DOSORT=1, IEEE80211_F_DOFRATE=2, IEEE80211_F_DONEGO=4,
       IEEE80211_F_DODEL=8, IEEE80211_FC0_SUBTYPE_DEAUTH=0xc0,
       IEEE80211_FC0_SUBTYPE_AUTH=0xb0 };
enum { kAirportItlwmPostPltiTraceEventBssSelected,
       kAirportItlwmPostPltiTraceEventJoinBssEntered };
enum { kIONetworkLinkValid=1, kIONetworkLinkActive=2 };
struct Controller {
    std::vector<int> media;
    int getCurrentMedium() { return 0; }
    void setLinkStatus(int status, int=0) { media.push_back(status); }
};
struct _ifnet { int if_link_state=LINK_STATE_UP; Controller *controller; };
struct ieee80211_node {
    int ni_chan=1, ni_esslen=3, ni_port_valid=1;
    uint8_t ni_macaddr[6]={2}, ni_bssid[6]={2}, ni_essid[32]={'n','e','t'};
    uint32_t ni_assoc_fail=0, ni_capinfo=IEEE80211_CAPINFO_PRIVACY,
        ni_flags=IEEE80211_NODE_MFP, ni_rsnprotos=IEEE80211_PROTO_RSN,
        ni_rsnakms=4, ni_rsnciphers=8, ni_rsncipher=8,
        ni_rsngroupcipher=8, ni_rsncaps=IEEE80211_RSNCAP_MFPC;
};
struct ieee80211com {
    _ifnet ic_if;
    ieee80211_node *ic_bss;
    ieee80211_phymode ic_curmode=ModeA;
    ieee80211_state ic_state=IEEE80211_S_RUN;
    int ic_opmode=IEEE80211_M_STA, ic_des_esslen=3,
        ic_flags=IEEE80211_F_RSNON|IEEE80211_F_BGSCAN,
        ic_mgt_timer=5, ic_bgscan_timeout=0;
    uint8_t ic_des_essid[32]={'n','e','t'};
    uint64_t ic_pae_assoc_epoch=7, ic_roam_link_epoch=0;
    void (*ic_node_copy)(ieee80211com *, ieee80211_node *, const ieee80211_node *);
    int (*ic_newstate_preflight)(ieee80211com *, ieee80211_state, int)=nullptr;
    int (*ic_newstate)(ieee80211com *, ieee80211_state, int);
};
static uint64_t ieee80211_pae_assoc_epoch_current(const ieee80211com *ic) {
    return ic && ic->ic_opmode == IEEE80211_M_STA ? ic->ic_pae_assoc_epoch : 0;
}
void ieee80211_set_link_state(ieee80211com *, int);
int ieee80211_roam_link_progress(ieee80211com *, ieee80211_state, ieee80211_state);
static void AirportItlwmRegDiagNet80211LinkContext(ieee80211com *, uint32_t, uint64_t) {}
static bool admitted=true, bindingRejected=false, preflightRejected=false;
static bool cancelDuringStop=false;
static int backendError=0, stops=0, copies=0;
static bool ieee80211_sae_wcl_request_join_begin(ieee80211com *) { return admitted; }
static void ieee80211_sae_wcl_request_join_end(ieee80211com *) {}
static void AirportItlwmPostPltiTraceRecord(ieee80211com *, int) {}
static void AirportItlwmPostPltiTraceNoteStateRequest(ieee80211com *, uint32_t, uint32_t) {}
static ieee80211_phymode ieee80211_chan2mode(ieee80211com *, int) { return ModeA; }
static void ieee80211_setmode(ieee80211com *, ieee80211_phymode) {}
static void ieee80211_stop_ampdu_tx(ieee80211com *ic, ieee80211_node *, int) {
    ++stops;
    if (cancelDuringStop) ++ic->ic_pae_assoc_epoch;
}
static uint64_t ieee80211_pae_assoc_epoch_begin_replacement(ieee80211com *ic) {
    if (++ic->ic_pae_assoc_epoch == 0) ++ic->ic_pae_assoc_epoch;
    return ic->ic_pae_assoc_epoch;
}
static void copy_node(ieee80211com *, ieee80211_node *dst, const ieee80211_node *src) {
    ++copies; *dst=*src; dst->ni_port_valid=0;
}
static uint8_t ieee80211_sae_selected_bss_profile(ieee80211_node *) { return 3; }
static void ieee80211_pae_selected_bss_capture(ieee80211com *, ieee80211_node *, uint8_t, uint64_t) {}
static int ieee80211_sae_wcl_request_bind_selected_bss(ieee80211com *, ieee80211_node *, uint64_t) {
    return bindingRejected ? -1 : 0;
}
static int preflight(ieee80211com *, ieee80211_state, int) { return preflightRejected ? 1 : 0; }
static int newstate(ieee80211com *ic, ieee80211_state state, int) {
    if (backendError) return backendError;
    const auto old=ic->ic_state;
    ic->ic_state=state;
    if (!ieee80211_roam_link_progress(ic,old,state))
        ieee80211_set_link_state(ic,LINK_STATE_DOWN);
    return 0;
}
static void ieee80211_new_state(ieee80211com *ic, ieee80211_state state, int arg) {
    if (state == IEEE80211_S_SCAN || state == IEEE80211_S_INIT)
        ++ic->ic_pae_assoc_epoch;
    newstate(ic,state,arg);
}
static void ieee80211_fix_rate(ieee80211com *, ieee80211_node *, int) {}
static void ieee80211_choose_rsnparams(ieee80211com *) {}
static void ieee80211_node_newstate(ieee80211_node *, int) {}
static void timeout_del(int *) {}
#include "production.inc"

struct Fixture {
    Controller controller;
    ieee80211_node source, target;
    ieee80211com ic;
    Fixture() {
        target.ni_bssid[0]=target.ni_macaddr[0]=4;
        ic.ic_bss=&source; ic.ic_if.controller=&controller;
        ic.ic_node_copy=copy_node; ic.ic_newstate=newstate;
        ic.ic_newstate_preflight=preflight;
        admitted=true; bindingRejected=preflightRejected=false;
        cancelDuringStop=false;
        backendError=stops=copies=0;
    }
    void open() {
        ic.ic_flags &= ~IEEE80211_F_RSNON;
        source.ni_capinfo=target.ni_capinfo=0;
        source.ni_port_valid=0;
    }
    void join() { ieee80211_node_join_bss(&ic,&target,0); }
};

int main() {
    unsigned cases=0;
    for (bool protectedNet : {false,true}) {
        for (unsigned mutation=0; mutation<17; ++mutation) {
            Fixture f;
            if (!protectedNet) f.open();
            switch (mutation) {
            case 1: f.ic.ic_state=IEEE80211_S_SCAN; break;
            case 2: f.ic.ic_opmode=IEEE80211_M_HOSTAP; break;
            case 3: f.ic.ic_if.if_link_state=LINK_STATE_DOWN; break;
            case 4: f.source.ni_esslen=0; break;
            case 5: f.source.ni_esslen=33; break;
            case 6: f.target.ni_esslen=2; break;
            case 7: f.target.ni_essid[0]='x'; break;
            case 8: f.target.ni_bssid[0]=2; break;
            case 9: f.target.ni_capinfo ^= IEEE80211_CAPINFO_PRIVACY; break;
            case 10: f.ic.ic_pae_assoc_epoch=0; break;
            case 11: f.source.ni_port_valid=0; break;
            case 12: f.target.ni_rsnprotos=0; break;
            case 13: f.target.ni_rsnakms=0; break;
            case 14: f.target.ni_rsnciphers=0; break;
            case 15: f.target.ni_rsngroupcipher=0; break;
            case 16: f.target.ni_rsncaps=0; break;
            }
            const bool allowed=mutation==0 || (!protectedNet && mutation>=11);
            assert((ieee80211_roam_link_source_epoch(&f.ic,&f.target)!=0)==allowed);
            ++cases;
        }
        for (bool earlyPort : {false,true}) {
            Fixture f;
            if (!protectedNet) f.open();
            f.join();
            assert(stops==1 && copies==1 && f.ic.ic_roam_link_epoch==8);
            assert(f.controller.media.empty());
            newstate(&f.ic,IEEE80211_S_ASSOC,0);
            if (earlyPort) {
                f.source.ni_port_valid=1;
                ieee80211_set_link_state(&f.ic,LINK_STATE_UP);
                assert(f.ic.ic_roam_link_epoch==8);
            }
            newstate(&f.ic,IEEE80211_S_RUN,0);
            assert(f.controller.media.empty());
            if (protectedNet && !earlyPort) {
                ieee80211_set_link_state(&f.ic,LINK_STATE_UP);
                assert(f.ic.ic_roam_link_epoch==8);
            }
            f.source.ni_port_valid=1;
            ieee80211_set_link_state(&f.ic,LINK_STATE_UP);
            assert(f.ic.ic_roam_link_epoch==0 && f.controller.media.empty());
            ieee80211_set_link_state(&f.ic,LINK_STATE_DOWN);
            assert(f.controller.media==std::vector<int>{kIONetworkLinkValid});
            ++cases;
        }
    }
    for (int failure=0; failure<5; ++failure) {
        Fixture f;
        if (failure==0) bindingRejected=true;
        if (failure==1) preflightRejected=true;
        if (failure==2) backendError=5;
        if (failure==3) admitted=false;
        if (failure==4) cancelDuringStop=true;
        f.join();
        if (failure<3) assert(f.ic.ic_if.if_link_state==LINK_STATE_DOWN && f.ic.ic_roam_link_epoch==0);
        if (failure==3) assert(copies==0 && f.controller.media.empty() && f.ic.ic_roam_link_epoch==0);
        if (failure==4) assert(f.ic.ic_if.if_link_state==LINK_STATE_DOWN && f.ic.ic_roam_link_epoch==0);
        ++cases;
    }
    for (auto old : {IEEE80211_S_INIT,IEEE80211_S_SCAN,IEEE80211_S_AUTH,IEEE80211_S_ASSOC,IEEE80211_S_RUN})
        for (auto next : {IEEE80211_S_INIT,IEEE80211_S_SCAN,IEEE80211_S_AUTH,IEEE80211_S_ASSOC,IEEE80211_S_RUN}) {
            Fixture f; f.ic.ic_roam_link_epoch=7;
            const bool forward=(old==IEEE80211_S_RUN && next==IEEE80211_S_AUTH) ||
                (old==IEEE80211_S_AUTH && next==IEEE80211_S_ASSOC) ||
                (old==IEEE80211_S_ASSOC && next==IEEE80211_S_RUN);
            assert(bool(ieee80211_roam_link_progress(&f.ic,old,next))==forward);
            ++f.ic.ic_pae_assoc_epoch;
            assert(!ieee80211_roam_link_progress(&f.ic,old,next));
            ++cases;
        }
    {
        Fixture f; f.ic.ic_pae_assoc_epoch=std::numeric_limits<uint64_t>::max();
        f.join(); assert(f.ic.ic_roam_link_epoch==1 && f.controller.media.empty());
        ieee80211_roam_link_failed(&f.ic,99);
        assert(f.ic.ic_roam_link_epoch==1 && f.controller.media.empty());
        ieee80211_roam_link_failed(&f.ic,1);
        assert(f.ic.ic_roam_link_epoch==0 && f.ic.ic_if.if_link_state==LINK_STATE_DOWN);
        ++cases;
    }
    {
        Fixture f; f.ic.ic_pae_assoc_epoch=9;
        ieee80211_roam_link_begin(&f.ic,7,9);
        assert(f.ic.ic_roam_link_epoch==0);
        f.ic.ic_roam_link_epoch=9;
        ieee80211_roam_link_failed(&f.ic,8);
        assert(f.ic.ic_roam_link_epoch==9 && f.controller.media.empty());
        ++cases;
    }
    assert(ieee80211_roam_link_source_epoch(nullptr,nullptr)==0);
    {
        Fixture f; f.open(); f.ic.ic_flags|=IEEE80211_F_WEPON;
        assert(ieee80211_roam_link_source_epoch(&f.ic,&f.target)==0);
        ++cases;
    }
    std::printf("PASS: %u actual roam carrier ownership/bridge and BSS replacement cases\n",cases);
}
