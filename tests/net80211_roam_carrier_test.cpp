#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <limits>
#include <vector>
#include "kernel_memory_test_support.hpp"
using u_int64_t = uint64_t;
using u_int8_t = uint8_t;
using u_int16_t = uint16_t;
using u_int32_t = uint32_t;
#define IEEE80211_ADDR_LEN 6
#define IEEE80211_STA_ONLY
#define __IO80211_TARGET 260000
#define __MAC_26_0 260000
#define IEEE80211_NWID_LEN 32
#define IEEE80211_ADDR_EQ(a,b) (std::memcmp(a,b,6)==0)
#define IEEE80211_ADDR_COPY(a,b) std::memcpy(a,b,6)
#include "constants.inc"
#include "../itl80211/openbsd/net80211/ieee80211_bss_switch.h"
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
enum { IEEE80211_SAE_WCL_REQUEST_BOUND=4,
       IEEE80211_NEWSTATE_ARG_SCAN_HOP=1000,
       IEEE80211_NEWSTATE_ARG_PUBLIC_ASSOCIATE=1001 };
struct IOSimpleLock { bool held=false; };
static std::function<void()> onUnlock;
using IOInterruptState = int;
static IOInterruptState IOSimpleLockLockDisableInterrupt(IOSimpleLock *lock) {
    assert(!lock->held); lock->held=true; return 0;
}
static void IOSimpleLockUnlockEnableInterrupt(IOSimpleLock *lock, int) {
    assert(lock->held); lock->held=false;
    if (onUnlock) onUnlock();
}
struct ieee80211_pae_mfp_prepared {};
struct ieee80211_sae_wcl_request_revocation {};
struct Controller {
    std::vector<int> media;
    std::function<void()> onLink;
    int getCurrentMedium() { return 0; }
    void setLinkStatus(int status, int=0) {
        media.push_back(status); if (onLink) onLink();
    }
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
    uint64_t ic_wcl_reassoc_next_serial=0, ic_wcl_reassoc_terminal_serial=0;
    uint64_t ic_wcl_reassoc_scan_accepted_serial=0;
    uint64_t ic_wcl_reassoc_owner_serial=0, ic_wcl_reassoc_source_epoch=0;
    unsigned ic_wcl_reassoc_owner_active=0, ic_wcl_reassoc_owner_last_leaf=0;
    ieee80211_wcl_reassoc_request ic_wcl_reassoc_request{};
    uint8_t ic_wcl_reassoc_source_bssid[6]={}, ic_wcl_reassoc_target_bssid[6]={};
    struct { uint64_t next_generation=0; } ic_wcl_join_attempt;
    uint64_t ic_pae_assoc_replace_epoch=0, ic_sae_wcl_policy_generation=0;
    IOSimpleLock *ic_pae_selected_bss_lock=nullptr;
    struct { uint64_t epoch=0; uint8_t bssid[6]={}; } ic_pae_selected_bss;
    struct { uint64_t association_epoch=0, configuration_epoch=0;
        int active=0, binding_pending=0; uint8_t bssid[6]={};
    } ic_public_initial_bssid_pin;
    uint8_t ic_des_bssid[6]={};
    struct { int phase=0; uint64_t generation=0; } ic_sae_wcl_request;
    int ic_sae_wcl_fresh_carrier_required=0, ic_sae_wcl_request_policy_starting=0;
    void (*ic_pae_mfp_txn_cancel)(ieee80211com *, uint64_t)=nullptr;
    void (*ic_event_handler)(ieee80211com *, int, void *)=nullptr;
    void (*ic_node_copy)(ieee80211com *, ieee80211_node *, const ieee80211_node *);
    int (*ic_newstate_preflight)(ieee80211com *, ieee80211_state, int)=nullptr;
    int (*ic_newstate)(ieee80211com *, ieee80211_state, int);
};
static uint64_t ieee80211_pae_assoc_epoch_current(const ieee80211com *ic) {
    return ic && ic->ic_opmode == IEEE80211_M_STA ? ic->ic_pae_assoc_epoch : 0;
}
/* The join ledger has its own production-function fixture. This adjacent
 * carrier fixture does not arm an ordinary join. */
static void ieee80211_wcl_join_cancel(ieee80211com *, uint64_t) {}
void ieee80211_set_link_state(ieee80211com *, int);
#ifdef ROAM_LOSS_BASELINE
uint64_t ieee80211_pae_assoc_epoch_begin_internal(ieee80211com *, int);
#else
uint64_t ieee80211_pae_assoc_epoch_begin_internal(ieee80211com *, int,
    uint64_t, uint64_t, const ieee80211_bss_switch_identity *);
#endif
void ieee80211_pae_assoc_epoch_note_newstate(ieee80211com *, ieee80211_state, int);
static uint64_t ieee80211_pae_assoc_epoch_begin(ieee80211com *ic) {
#ifdef ROAM_LOSS_BASELINE
    return ieee80211_pae_assoc_epoch_begin_internal(ic,0);
#else
    return ieee80211_pae_assoc_epoch_begin_internal(ic,0,0,0,nullptr);
#endif
}
static uint64_t ieee80211_pae_assoc_epoch_advance_locked(ieee80211com *ic) {
    if (++ic->ic_pae_assoc_epoch==0) ++ic->ic_pae_assoc_epoch;
    return ic->ic_pae_assoc_epoch;
}
static void ieee80211_pae_selected_bss_invalidate(ieee80211com *ic) {
    ic->ic_pae_selected_bss.epoch=0;
    std::memset(ic->ic_pae_selected_bss.bssid,0,6);
}
static void ieee80211_sae_peer_rx_admission_clear_locked(ieee80211com *) {}
static void ieee80211_public_initial_bssid_pin_clear_locked(ieee80211com *ic) {
    ic->ic_public_initial_bssid_pin={};
}
static void ieee80211_sae_wcl_request_clear_locked(ieee80211com *,
    ieee80211_sae_wcl_request_revocation *) {}
static uint64_t ieee80211_pae_mfp_txn_cancel_locked(ieee80211com *,
    ieee80211_pae_mfp_prepared *) { return 0; }
static std::function<void(ieee80211com *)> onRevoke;
static void ieee80211_sae_wcl_request_revocation_deliver(ieee80211com *ic,
    ieee80211_sae_wcl_request_revocation *) {
    assert(!ic->ic_pae_selected_bss_lock || !ic->ic_pae_selected_bss_lock->held);
    if (onRevoke) onRevoke(ic);
}
static void ieee80211_pae_mfp_txn_dispose_prepared(ieee80211com *,
    ieee80211_pae_mfp_prepared *) {}
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
static void ieee80211_pae_selected_bss_capture(ieee80211com *ic, ieee80211_node *ni, uint8_t, uint64_t epoch) {
    ic->ic_pae_selected_bss.epoch=epoch;
    IEEE80211_ADDR_COPY(ic->ic_pae_selected_bss.bssid,ni->ni_bssid);
}
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
    ieee80211_pae_assoc_epoch_note_newstate(ic,state,arg);
    newstate(ic,state,arg);
}
static void ieee80211_fix_rate(ieee80211com *, ieee80211_node *, int) {}
static void ieee80211_choose_rsnparams(ieee80211com *) {}
static void ieee80211_node_newstate(ieee80211_node *, int) {}
static void timeout_del(int *) {}
#include "production.inc"

static std::vector<ieee80211_roam_link_loss> losses;
static void record_loss(ieee80211com *ic, int event, void *data) {
    assert(!ic->ic_pae_selected_bss_lock || !ic->ic_pae_selected_bss_lock->held);
    assert(event==IEEE80211_EVT_STA_ROAM_LINK_LOST && data);
    losses.push_back(*static_cast<ieee80211_roam_link_loss *>(data));
}

using IOReturn = int;
enum { kIOReturnSuccess, kIOReturnBadArgument, kIOReturnNotReady };
constexpr unsigned kTahoeWclLinkChanged=0xd8;
constexpr uint8_t kTahoeWclInfraInterfaceType=1;
struct OSObject { virtual ~OSObject()=default; };
#define OSDynamicCast(T, value) dynamic_cast<T *>(value)
struct TahoeOwnerRegistry {
    struct AssociationOwner { unsigned token=0; };
    AssociationOwner association, publicAssociation;
};
struct Hal {
    ieee80211com *ic;
    ieee80211com *get80211Controller() { return ic; }
};
struct AirportItlwm : OSObject {
    void *fNetIf=this;
    Hal *fHalService=nullptr;
    TahoeOwnerRegistry registry;
    std::vector<TahoeWclLinkChangedPayload> messages;
    TahoeOwnerRegistry &getTahoeOwnerRegistry() { return registry; }
    void postMessage(void *netif, unsigned selector, const void *data,
                     size_t length, bool async) {
        assert(netif==this && selector==0xd8 && length==16 && async);
        messages.push_back(*static_cast<const TahoeWclLinkChangedPayload *>(data));
    }
};
#include "controller.inc"

struct Fixture {
    Controller controller;
    IOSimpleLock lock;
    ieee80211_node source, target;
    ieee80211com ic;
    Fixture() {
        target.ni_bssid[0]=target.ni_macaddr[0]=4;
        ic.ic_bss=&source; ic.ic_if.controller=&controller;
        ic.ic_pae_selected_bss_lock=&lock;
        ic.ic_node_copy=copy_node; ic.ic_newstate=newstate;
        ic.ic_newstate_preflight=preflight;
        admitted=true; bindingRejected=preflightRejected=false;
        cancelDuringStop=false;
        backendError=stops=copies=0;
        onRevoke={};
        onUnlock={};
        losses.clear();
        ic.ic_event_handler=record_loss;
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
    for (auto state : {IEEE80211_S_AUTH,IEEE80211_S_ASSOC,IEEE80211_S_RUN}) {
        Fixture f; f.join(); f.ic.ic_state=state;
        const auto retired=ieee80211_pae_assoc_epoch_begin(&f.ic);
        assert(retired==9 && f.ic.ic_roam_link_epoch==0);
        assert(f.ic.ic_pae_selected_bss.epoch==0);
        assert(losses.size()==1 && losses[0].epoch==9);
        assert(IEEE80211_ADDR_EQ(losses[0].bssid,f.target.ni_bssid));
        const auto copied=losses[0];
        Hal hal{&f.ic}; AirportItlwm driver; driver.fHalService=&hal;
        driver.registry.association.token=11;
        driver.registry.publicAssociation.token=12;
        assert(postTahoeWclRoamLinkLossGated(&driver,(void *)&copied,nullptr,nullptr,nullptr)==kIOReturnSuccess);
        assert(driver.registry.association.token==0 && driver.registry.publicAssociation.token==0);
        assert(driver.messages.size()==1);
        const auto &event=driver.messages[0];
        assert(event.linkState==0 && event.interfaceType==1 && event.reasonCode==5 && event.reserved==0);
        assert(IEEE80211_ADDR_EQ(event.bssid,f.target.ni_bssid));
        ieee80211_new_state(&f.ic,IEEE80211_S_SCAN,-1);
        assert(f.ic.ic_if.if_link_state==LINK_STATE_DOWN && losses.size()==1);
        // Entering the controller gate after a new same-BSSID epoch must not
        // clear that newer owner's leases or publish another link indication.
        driver.registry.association.token=22;
        assert(postTahoeWclRoamLinkLossGated(&driver,(void *)&copied,nullptr,nullptr,nullptr)==kIOReturnNotReady);
        assert(driver.registry.association.token==22 && driver.messages.size()==1);
        ++cases;
    }
    for (unsigned kind=0; kind<5; ++kind) {
        Fixture f; f.join();
        if (kind==0) ieee80211_roam_link_cancel(&f.ic);
        if (kind==1) ieee80211_sae_wcl_fresh_carrier_accepted(&f.ic);
        if (kind==2) {
            f.source.ni_port_valid=1; f.ic.ic_state=IEEE80211_S_RUN;
            ieee80211_set_link_state(&f.ic,LINK_STATE_UP);
        }
        if (kind==3) onRevoke=[](ieee80211com *ic) { ++ic->ic_pae_assoc_epoch; };
        if (kind==4) onRevoke=[](ieee80211com *ic) { ic->ic_bss->ni_bssid[0]=6; };
        ieee80211_pae_assoc_epoch_begin(&f.ic);
        assert(losses.empty());
        ++cases;
    }
    for (unsigned kind=0; kind<5; ++kind) {
        Fixture f; f.join();
        if (kind==0) ieee80211_roam_link_failed(&f.ic,8);
        if (kind==1) ieee80211_set_link_state(&f.ic,LINK_STATE_DOWN);
        if (kind==2) {
            f.controller.onLink=[&f] { ++f.ic.ic_pae_assoc_epoch; };
            ieee80211_roam_link_failed(&f.ic,8);
        }
        if (kind==3) {
            f.ic.ic_pae_selected_bss_lock=nullptr;
            ieee80211_roam_link_failed(&f.ic,8);
        }
        if (kind==4) {
            f.ic.ic_pae_selected_bss.bssid[0]=1;
            ieee80211_roam_link_failed(&f.ic,8);
        }
        assert(f.ic.ic_roam_link_epoch==0 && f.ic.ic_if.if_link_state==LINK_STATE_DOWN);
        assert(losses.size()==(kind<2 ? 1U : 0U));
        ieee80211_roam_link_failed(&f.ic,8);
        ieee80211_set_link_state(&f.ic,LINK_STATE_DOWN);
        assert(losses.size()==(kind<2 ? 1U : 0U));
        assert(f.controller.media.size()==1);
        ++cases;
    }
    {
        Fixture f; // No admitted replacement: scan failure is not link loss.
        ieee80211_pae_assoc_epoch_begin(&f.ic);
        assert(losses.empty() && f.controller.media.empty());
        ++cases;
    }
    for (unsigned invalid=0; invalid<10; ++invalid) {
        Fixture f; f.join();
        ieee80211_roam_link_loss loss{};
        assert(ieee80211_roam_link_take_loss(&f.ic,8,&loss));
        Hal hal{&f.ic}; AirportItlwm driver; driver.fHalService=&hal;
        driver.registry.association.token=31;
        OSObject *object=&driver;
        void *argument=&loss;
        switch (invalid) {
        case 0: object=nullptr; break;
        case 1: driver.fNetIf=nullptr; break;
        case 2: driver.fHalService=nullptr; break;
        case 3: argument=nullptr; break;
        case 4: hal.ic=nullptr; break;
        case 5: f.ic.ic_bss=nullptr; break;
        case 6: f.ic.ic_opmode=IEEE80211_M_HOSTAP; break;
        case 7: loss.epoch=0; break;
        case 8: loss.bssid[0]=1; break;
        case 9: loss.bssid[0]=6; break;
        }
        assert(postTahoeWclRoamLinkLossGated(object,argument,nullptr,nullptr,nullptr)!=kIOReturnSuccess);
        assert(driver.messages.empty() && driver.registry.association.token==31);
        ++cases;
    }
    {
        Fixture f; f.join();
        onUnlock=[&f] {
            f.ic.ic_pae_assoc_epoch=9;
            f.ic.ic_roam_link_epoch=9;
        };
        ieee80211_roam_link_failed(&f.ic,8);
        assert(f.ic.ic_roam_link_epoch==9 && losses.empty() && f.controller.media.empty());
        ++cases;
    }
    {
        Fixture f; f.join(); f.ic.ic_roam_link_epoch=9;
        ieee80211_roam_link_note_terminal(&f.ic,LINK_STATE_DOWN,8);
        assert(f.ic.ic_roam_link_epoch==9);
        ++cases;
    }
    {
        Fixture f; f.ic.ic_pae_selected_bss_lock=nullptr; f.join();
        assert(f.ic.ic_roam_link_epoch==0 && f.ic.ic_if.if_link_state==LINK_STATE_DOWN);
        assert(losses.empty());
        ++cases;
    }
#ifndef ROAM_LOSS_BASELINE
    for (unsigned change=0; change<14; ++change) {
        Fixture f;
        ieee80211_bss_switch_identity identity{};
        identity.source_epoch=identity.continuation_epoch=7;
        identity.reassoc_sequence=identity.reassoc_serial=31;
        identity.source_macaddr[0]=identity.source_bssid[0]=2;
        identity.target_macaddr[0]=identity.target_bssid[0]=4;
        f.ic.ic_wcl_reassoc_next_serial=f.ic.ic_wcl_reassoc_owner_serial=31;
        f.ic.ic_wcl_reassoc_source_epoch=7;
        f.ic.ic_wcl_reassoc_owner_active=1;
        f.ic.ic_wcl_reassoc_owner_last_leaf=IEEE80211_WCL_REASSOC_OWNER_LEAF_ROAM_STARTED;
        f.ic.ic_wcl_reassoc_source_bssid[0]=2;
        f.ic.ic_wcl_reassoc_target_bssid[0]=4;
        switch (change) {
        case 1: ++f.ic.ic_pae_assoc_epoch; break;
        case 2: ++f.ic.ic_wcl_join_attempt.next_generation; break;
        case 3: ++f.ic.ic_wcl_reassoc_next_serial; break;
        case 4: ++f.ic.ic_wcl_reassoc_owner_serial; break;
        case 5: f.ic.ic_wcl_reassoc_owner_active=0; break;
        case 6: f.source.ni_bssid[1]=1; break;
        case 7: f.source.ni_macaddr[1]=1; break;
        case 8: ++f.ic.ic_wcl_reassoc_source_epoch; break;
        case 9: f.ic.ic_wcl_reassoc_target_bssid[1]=1; break;
        case 10: f.ic.ic_state=IEEE80211_S_SCAN; break;
        case 11: f.ic.ic_pae_selected_bss_lock=nullptr; break;
        case 12: // Legacy background roam has no WCL serial but keeps the census sequence.
            identity.reassoc_serial=0; f.ic.ic_wcl_reassoc_owner_active=0; break;
        case 13:
            onRevoke=[](ieee80211com *ic) {
                ++ic->ic_wcl_join_attempt.next_generation;
                ic->ic_pae_assoc_epoch=10;
            }; break;
        }
        const auto oldEpoch=f.ic.ic_pae_assoc_epoch;
        const auto selected=f.ic.ic_pae_selected_bss.epoch;
        const auto result=ieee80211_pae_assoc_epoch_begin_internal(&f.ic,0,0,0,&identity);
        if (change==0 || change==12) assert(result==8 && f.ic.ic_pae_assoc_epoch==8);
        else if (change==13) {
            assert(result==8 && f.ic.ic_pae_assoc_epoch==10);
            identity.continuation_epoch=result;
            const auto irq=IOSimpleLockLockDisableInterrupt(&f.lock);
            assert(!ieee80211_bss_switch_identity_current_locked(&f.ic,&identity));
            IOSimpleLockUnlockEnableInterrupt(&f.lock,irq);
        } else assert(result==0 && f.ic.ic_pae_assoc_epoch==oldEpoch &&
            f.ic.ic_pae_selected_bss.epoch==selected);
        onRevoke={};
        ++cases;
    }
    for (unsigned change=0; change<5; ++change) {
        Fixture f;
        f.ic.ic_wcl_reassoc_next_serial=31;
        f.ic.ic_wcl_reassoc_terminal_serial=31;
        if (change==1) f.ic.ic_wcl_reassoc_next_serial=32;
        if (change==2) f.ic.ic_wcl_reassoc_terminal_serial=0;
        if (change==3) f.ic.ic_pae_assoc_epoch=8;
        if (change==4) f.ic.ic_pae_selected_bss_lock=nullptr;
        const auto oldEpoch=f.ic.ic_pae_assoc_epoch;
        const auto selected=f.ic.ic_pae_selected_bss.epoch;
        const auto result=ieee80211_pae_assoc_epoch_begin_internal(&f.ic,0,31,7);
        if (change==0) assert(result==8 && f.ic.ic_pae_assoc_epoch==8);
        else assert(result==0 && f.ic.ic_pae_assoc_epoch==oldEpoch &&
            f.ic.ic_pae_selected_bss.epoch==selected);
        assert(losses.empty());
        ++cases;
    }
    {
        Fixture f;
        f.ic.ic_wcl_reassoc_next_serial=31;
        f.ic.ic_wcl_reassoc_terminal_serial=31;
        onRevoke=[](ieee80211com *ic) {
            ic->ic_wcl_reassoc_next_serial=32;
            ic->ic_wcl_reassoc_terminal_serial=0;
            ic->ic_pae_assoc_epoch=9;
        };
        assert(ieee80211_pae_assoc_epoch_begin_internal(&f.ic,0,31,7)==8);
        assert(f.ic.ic_pae_assoc_epoch==9 && f.ic.ic_wcl_reassoc_next_serial==32);
        onRevoke={};
        ++cases;
    }
#endif
    for (unsigned leaf : {IEEE80211_WCL_REASSOC_OWNER_LEAF_SETUP,
            IEEE80211_WCL_REASSOC_OWNER_LEAF_SCAN_STARTED,
            IEEE80211_WCL_REASSOC_OWNER_LEAF_SCAN_FAILED}) {
        Fixture f;
        f.ic.ic_mgt_timer=0;
        f.ic.ic_wcl_reassoc_next_serial=31;
        f.ic.ic_wcl_reassoc_owner_serial=31;
        f.ic.ic_wcl_reassoc_source_epoch=7;
        f.ic.ic_wcl_reassoc_owner_active=1;
        f.ic.ic_wcl_reassoc_owner_last_leaf=leaf;
        f.ic.ic_wcl_reassoc_source_bssid[0]=2;
        f.ic.ic_wcl_reassoc_scan_accepted_serial=31;
        f.ic.ic_wcl_reassoc_request.feature_flags=0x34;
        f.ic.ic_flags |= IEEE80211_F_BGSCAN | IEEE80211_F_DISABLE_BG_AUTO_CONNECT;
        const auto result=ieee80211_pae_assoc_epoch_begin(&f.ic);
        assert(result==8 && f.ic.ic_pae_assoc_epoch==8);
        // Real source cancellation must not strand the old scan's logical
        // owner. The lower scan/command lease is an independent boundary.
        assert(!f.ic.ic_wcl_reassoc_owner_active);
        assert(f.ic.ic_wcl_reassoc_owner_serial==0);
        assert(f.ic.ic_wcl_reassoc_source_epoch==0);
        assert(f.ic.ic_wcl_reassoc_next_serial==31);
        assert(f.ic.ic_wcl_reassoc_scan_accepted_serial==31);
        assert(f.ic.ic_wcl_reassoc_request.feature_flags==0);
        assert((f.ic.ic_flags & (IEEE80211_F_BGSCAN |
            IEEE80211_F_DISABLE_BG_AUTO_CONNECT))==0);
        assert(losses.empty());
        assert(!ieee80211_wcl_reassoc_scan_completion_begin(&f.ic,31));
        assert(ieee80211_wcl_reassoc_scan_completion_begin(&f.ic,0));
        assert(ieee80211_pae_assoc_epoch_begin(&f.ic)==9);
        assert(!f.ic.ic_wcl_reassoc_owner_active && losses.empty());
        ++cases;
    }
    for (unsigned change=0; change<8; ++change) {
        Fixture f;
        f.ic.ic_wcl_reassoc_next_serial=31;
        f.ic.ic_wcl_reassoc_owner_serial=31;
        f.ic.ic_wcl_reassoc_source_epoch=7;
        f.ic.ic_wcl_reassoc_owner_active=1;
        f.ic.ic_wcl_reassoc_owner_last_leaf=IEEE80211_WCL_REASSOC_OWNER_LEAF_SCAN_STARTED;
        f.ic.ic_wcl_reassoc_request.feature_flags=0x34;
        switch(change) {
        case 0: f.ic.ic_wcl_reassoc_source_epoch=8; break;
        case 1: f.ic.ic_wcl_reassoc_next_serial=32; break;
        case 2: f.ic.ic_wcl_reassoc_owner_serial=0; break;
        case 3: f.ic.ic_wcl_reassoc_owner_last_leaf=IEEE80211_WCL_REASSOC_OWNER_LEAF_ROAM_STARTED; break;
        case 4: f.ic.ic_wcl_reassoc_owner_last_leaf=IEEE80211_WCL_REASSOC_OWNER_LEAF_REASSOC_REQ_SENT; break;
        case 5: f.ic.ic_wcl_reassoc_owner_last_leaf=IEEE80211_WCL_REASSOC_OWNER_LEAF_REASSOC_REQ_TIMEOUT; break;
        case 6: f.ic.ic_wcl_reassoc_owner_last_leaf=IEEE80211_WCL_REASSOC_OWNER_LEAF_IDLE; break;
        case 7: f.ic.ic_pae_selected_bss_lock=nullptr; break;
        }
        const auto flags=f.ic.ic_flags;
        assert(ieee80211_pae_assoc_epoch_begin(&f.ic)==8);
        assert(f.ic.ic_wcl_reassoc_owner_active);
        assert(f.ic.ic_wcl_reassoc_request.feature_flags==0x34);
        assert(f.ic.ic_flags==flags && losses.empty());
        ++cases;
    }
    {
        Fixture f;
        f.ic.ic_wcl_reassoc_next_serial=31;
        f.ic.ic_wcl_reassoc_owner_serial=31;
        f.ic.ic_wcl_reassoc_source_epoch=7;
        f.ic.ic_wcl_reassoc_owner_active=1;
        f.ic.ic_wcl_reassoc_owner_last_leaf=IEEE80211_WCL_REASSOC_OWNER_LEAF_SCAN_STARTED;
        onRevoke=[](ieee80211com *ic) {
            assert(!ic->ic_pae_selected_bss_lock->held);
            assert(!ic->ic_wcl_reassoc_owner_active && !(ic->ic_flags & IEEE80211_F_BGSCAN));
            ic->ic_wcl_reassoc_next_serial=32;
            ic->ic_wcl_reassoc_owner_serial=32;
            ic->ic_wcl_reassoc_source_epoch=ic->ic_pae_assoc_epoch;
            ic->ic_wcl_reassoc_owner_active=1;
            ic->ic_wcl_reassoc_owner_last_leaf=IEEE80211_WCL_REASSOC_OWNER_LEAF_SCAN_STARTED;
            ic->ic_flags |= IEEE80211_F_BGSCAN;
            ic->ic_wcl_reassoc_request.feature_flags=0x78;
        };
        assert(ieee80211_pae_assoc_epoch_begin(&f.ic)==8);
        onRevoke={};
        assert(!ieee80211_wcl_reassoc_scan_completion_begin(&f.ic,31));
        assert(!ieee80211_wcl_reassoc_scan_completion_begin(&f.ic,0));
        assert(f.ic.ic_wcl_reassoc_owner_serial==32 && (f.ic.ic_flags & IEEE80211_F_BGSCAN));
        assert(f.ic.ic_wcl_reassoc_request.feature_flags==0x78 && losses.empty());
        assert(ieee80211_wcl_reassoc_scan_completion_begin(&f.ic,32));
        ++cases;
    }
    std::printf("PASS: %u actual roam carrier ownership/bridge and BSS replacement cases\n",cases);
}
