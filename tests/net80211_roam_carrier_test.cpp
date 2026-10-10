#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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
#include "../itl80211/openbsd/net80211/ieee80211_pae_selected_bss.h"
#define _KASSERT(value) assert(value)
#define XYLog(...) do {} while (0)
enum ieee80211_phymode { ModeA };
enum ieee80211_state { IEEE80211_S_INIT, IEEE80211_S_SCAN, IEEE80211_S_AUTH,
                       IEEE80211_S_ASSOC, IEEE80211_S_RUN };
enum { IEEE80211_M_STA, IEEE80211_M_HOSTAP, IEEE80211_M_MONITOR,
       IEEE80211_STA_BSS,
       IEEE80211_SAE_WCL_REQUEST_BIND_REJECTED=-1 };
enum { LINK_STATE_DOWN, LINK_STATE_UP, LINK_STATE_UNKNOWN };
enum { IEEE80211_F_BGSCAN=4, IEEE80211_F_DISABLE_BG_AUTO_CONNECT=8,
       IEEE80211_F_DOSORT=1, IEEE80211_F_DOFRATE=2, IEEE80211_F_DONEGO=4,
       IEEE80211_F_DODEL=8, IEEE80211_FC0_SUBTYPE_DEAUTH=0xc0,
       IEEE80211_FC0_SUBTYPE_AUTH=0xb0, IEEE80211_F_TX_MGMT_ONLY=16 };
enum { kAirportItlwmPostPltiTraceEventBssSelected,
       kAirportItlwmPostPltiTraceEventJoinBssEntered };
enum { kIONetworkLinkValid=1, kIONetworkLinkActive=2 };
enum { IEEE80211_NEWSTATE_ARG_SCAN_HOP=1000,
       IEEE80211_NEWSTATE_ARG_PUBLIC_ASSOCIATE=1001 };
struct IOSimpleLock { bool held=false; };
static std::function<void()> onUnlock;
using IOInterruptState = int;
static IOInterruptState IOSimpleLockLockDisableInterrupt(IOSimpleLock *lock) {
    assert(!lock->held); lock->held=true; return 0;
}
static void IOSimpleLockUnlockEnableInterrupt(IOSimpleLock *lock, int) {
    assert(lock->held); lock->held=false;
    const auto callback=onUnlock;
    if (callback) callback();
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
    int ni_chan=1, ni_port_valid=1;
    uint8_t ni_esslen=3;
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
    ieee80211_wnm_bss_transition ic_wnm_bss_transition{};
    uint64_t ic_wnm_bss_transition_next_request=0;
    uint64_t ic_wnm_bss_transition_next_tx_fence=0;
    unsigned ic_xflags=0;
    int (*ic_sae_wnm_roam_start)(ieee80211com *,const ieee80211_node *,uint64_t,uint64_t)=nullptr;
    uint64_t ic_wcl_reassoc_next_serial=0, ic_wcl_reassoc_terminal_serial=0;
    uint64_t ic_wcl_reassoc_scan_accepted_serial=0;
    uint32_t ic_wcl_reassoc_published_stages=0;
    ieee80211_wcl_reassoc_observation ic_wcl_reassoc_observation{};
    uint64_t ic_wcl_reassoc_owner_serial=0, ic_wcl_reassoc_source_epoch=0;
    unsigned ic_wcl_reassoc_owner_active=0, ic_wcl_reassoc_owner_last_leaf=0;
    ieee80211_wcl_reassoc_request ic_wcl_reassoc_request{};
    uint8_t ic_wcl_reassoc_source_bssid[6]={}, ic_wcl_reassoc_target_bssid[6]={};
    struct { uint64_t next_generation=0; } ic_wcl_join_attempt;
    uint64_t ic_pae_assoc_replace_epoch=0, ic_sae_wcl_policy_generation=0;
    IOSimpleLock *ic_pae_selected_bss_lock=nullptr;
    ieee80211_pae_selected_bss ic_pae_selected_bss{};
    struct { uint64_t association_epoch=0, configuration_epoch=0;
        int active=0, binding_pending=0; uint8_t bssid[6]={};
    } ic_public_initial_bssid_pin;
    uint8_t ic_des_bssid[6]={};
    ieee80211_sae_wcl_request ic_sae_wcl_request{};
    uint64_t ic_sae_wcl_request_next_generation=0;
    unsigned ic_sae_wcl_request_join_active=0;
    unsigned ic_pae_mfp_requested=0, ic_rsnprotos=0, ic_rsnakms=0,
        ic_rsnciphers=0, ic_rsngroupcipher=0, ic_rsngroupmgmtcipher=0;
    // These four hook slots are registration identities only. The actual
    // owner predicate checks them; no authentication callback is simulated.
    void (*ic_sae_auth_hold)()=nullptr, (*ic_sae_auth_owned)()=nullptr,
        (*ic_sae_engine_peer_event)()=nullptr, (*ic_sae_wcl_request_revoke)()=nullptr;
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
uint64_t ieee80211_pae_assoc_epoch_begin_internal(ieee80211com *, int,
    uint64_t, uint64_t, const ieee80211_bss_switch_identity *,uint64_t,uint64_t);
void ieee80211_pae_assoc_epoch_note_newstate(ieee80211com *, ieee80211_state, int);
void ieee80211_node_wnm_reconnect(ieee80211com *,ieee80211_node *,uint64_t,uint64_t);
static uint64_t ieee80211_pae_assoc_epoch_begin(ieee80211com *ic) {
    return ieee80211_pae_assoc_epoch_begin_internal(ic,0,0,0,nullptr,0,0);
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
    const auto callback=onRevoke;
    if (callback) callback(ic);
}
static void ieee80211_pae_mfp_txn_dispose_prepared(ieee80211com *,
    ieee80211_pae_mfp_prepared *) {}
int ieee80211_roam_link_progress(ieee80211com *, ieee80211_state, ieee80211_state);
static void AirportItlwmRegDiagNet80211LinkContext(ieee80211com *, uint32_t, uint64_t) {}
static bool admitted=true, bindingRejected=false, preflightRejected=false;
static bool cancelDuringStop=false;
static int backendError=0, stops=0, copies=0;
static unsigned stateRequests=0;
static std::function<int(ieee80211com *)> onPreflight, onNewstate, onSaeWnm;
static bool ieee80211_sae_wcl_request_join_begin(ieee80211com *) { return admitted; }
static void ieee80211_sae_wcl_request_join_end(ieee80211com *) {}
static void AirportItlwmPostPltiTraceRecord(ieee80211com *, int) {}
static void AirportItlwmPostPltiTraceNoteStateRequest(ieee80211com *, uint32_t, uint32_t) {}
static ieee80211_phymode ieee80211_chan2mode(ieee80211com *, int) { return ModeA; }
static unsigned ieee80211_chan2ieee(ieee80211com *, int channel) { return channel; }
static void ieee80211_setmode(ieee80211com *, ieee80211_phymode) {}
static void ieee80211_stop_ampdu_tx(ieee80211com *ic, ieee80211_node *, int) {
    ++stops;
    if (cancelDuringStop) ++ic->ic_pae_assoc_epoch;
}
uint64_t ieee80211_pae_assoc_epoch_begin_replacement(ieee80211com *);
static void ieee80211_sae_wcl_pmk_claim_retire_replacement_locked(ieee80211com *ic, uint64_t epoch) {
    assert(ic->ic_pae_selected_bss_lock->held && ic->ic_pae_assoc_epoch==epoch);
}
static bool ieee80211_sae_wcl_request_scan_issued_locked(ieee80211com *ic, uint64_t generation) {
    return generation!=0 && ic->ic_sae_wcl_request.phase==IEEE80211_SAE_WCL_REQUEST_SCAN_ISSUED;
}
static bool ieee80211_sae_wcl_request_run_retarget_issued_locked(ieee80211com *ic, uint64_t generation) {
    return generation!=0 && ic->ic_sae_wcl_request.phase==IEEE80211_SAE_WCL_REQUEST_RUN_RETARGET_ISSUED;
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
static int preflight(ieee80211com *ic, ieee80211_state, int) {
    const auto callback=onPreflight;
    if (callback) return callback(ic);
    return preflightRejected ? 1 : 0;
}
static int newstate(ieee80211com *ic, ieee80211_state state, int) {
    ++stateRequests;
    const auto callback=onNewstate;
    if (callback) return callback(ic);
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
/* This carrier fixture has no scan-cache tree. The complete production
 * retirement helper and its join ordering are exercised separately by
 * net80211_join_bss_tx_teardown_test.cpp. */
static void ieee80211_clean_sta_bss_node(ieee80211com *ic) {
    assert(ic->ic_opmode == IEEE80211_M_STA && ic->ic_bss);
}
static void timeout_del(int *) {}
/* This carrier fixture's event handler covers only the roam link-loss
 * indication; the shared owner's progress publication (0x89/0x8b) is
 * exercised end to end by wcl_reassoc_owner_test.cpp. The scan-completion
 * helper's progress hook is therefore a no-op here, while its return value
 * still reflects the real serial/owner check below. */
void ieee80211_wcl_reassoc_post_progress(ieee80211com *, uint64_t) {}
static int ieee80211_wcl_reassoc_current(ieee80211com *ic, uint64_t serial) {
    return serial != 0 && ic->ic_wcl_reassoc_owner_active &&
        ic->ic_wcl_reassoc_owner_serial == serial;
}
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
        stateRequests=0;
        onPreflight={}; onNewstate={}; onSaeWnm={};
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

static void btm_cancel_control() {
    Fixture f;
    assert(ieee80211_wnm_bss_transition_arm(&f.ic,f.source.ni_bssid,
        f.target.ni_bssid,f.source.ni_essid,f.source.ni_esslen,17,f.target.ni_chan));
    uint8_t token=0,bssid[6]{};
    assert(ieee80211_wnm_bss_transition_confirm_candidate(&f.ic,&f.target,&token,bssid));
    assert(f.ic.ic_wnm_bss_transition.source_epoch==7);
    // Execute the complete production ordinary cancellation, including its
    // real epoch advance and out-of-leaf revocation boundary. No bare epoch
    // increment substitutes for the cancellation body in this composition.
    assert(ieee80211_pae_assoc_epoch_begin(&f.ic)==8);
    uint64_t copiedGeneration=99;
    const int retarget=ieee80211_wnm_bss_transition_copy_retarget(&f.ic,
        f.source.ni_essid,f.source.ni_esslen,bssid,&copiedGeneration);
    std::fprintf(stderr,"actual full epoch cancel + BTM retarget: old=7 current=%llu "
        "active=%u retarget=%d\n",static_cast<unsigned long long>(f.ic.ic_pae_assoc_epoch),
        f.ic.ic_wnm_bss_transition.active,retarget);
    assert(!retarget && !copiedGeneration && !f.ic.ic_wnm_bss_transition.active);
}
static void btm_terminal_cancel_control() {
    Fixture f;
    assert(ieee80211_wnm_bss_transition_arm(&f.ic,f.source.ni_bssid,
        f.target.ni_bssid,f.source.ni_essid,f.source.ni_esslen,17,f.target.ni_chan));
    uint8_t token=0,bssid[6]{}; uint64_t fence=0;
    assert(ieee80211_wnm_bss_transition_confirm_candidate(&f.ic,&f.target,&token,bssid));
    assert(ieee80211_wnm_bss_transition_tx_fence_arm(&f.ic,&f.source,token,bssid,&fence));
    assert(ieee80211_wnm_bss_transition_tx_fence_submit(&f.ic,fence,IEEE80211_WNM_TX_FENCE_RESPONSE));
    assert(ieee80211_wnm_bss_transition_tx_fence_submit(&f.ic,fence,IEEE80211_WNM_TX_FENCE_DEAUTH));
    ieee80211_wnm_bss_transition_tx_fence_complete(&f.ic,&f.source,fence,IEEE80211_WNM_TX_FENCE_RESPONSE);
    // Explicit scheduling boundary after the actual terminal owner's leaf,
    // before its real node reconnect function. Execute complete cancellation
    // and state-note bodies, not a fabricated TX result or epoch increment.
    onUnlock=[&] {
        onUnlock={};
        ieee80211_new_state(&f.ic,IEEE80211_S_INIT,-1);
    };
    ieee80211_wnm_bss_transition_tx_fence_complete(&f.ic,&f.source,fence,IEEE80211_WNM_TX_FENCE_DEAUTH);
    std::fprintf(stderr,"actual BTM terminal/cancel/reconnect: state=%u epoch=%llu active=%u\n",
        unsigned(f.ic.ic_state),static_cast<unsigned long long>(f.ic.ic_pae_assoc_epoch),
        f.ic.ic_wnm_bss_transition.active);
    assert(f.ic.ic_state==IEEE80211_S_INIT && f.ic.ic_pae_assoc_epoch==8);
}
struct BtmFence { uint64_t request=0, sourceEpoch=0, tx=0; };
static BtmFence arm_btm(Fixture &f, bool submit=true) {
    assert(ieee80211_wnm_bss_transition_arm(&f.ic,f.source.ni_bssid,
        f.target.ni_bssid,f.source.ni_essid,f.source.ni_esslen,17,f.target.ni_chan));
    uint8_t token=0,target[6]{};
    assert(ieee80211_wnm_bss_transition_confirm_candidate(&f.ic,&f.target,&token,target));
    BtmFence receipt{f.ic.ic_wnm_bss_transition.request_generation,
        f.ic.ic_wnm_bss_transition.source_epoch,0};
    assert(ieee80211_wnm_bss_transition_tx_fence_arm(&f.ic,&f.source,token,target,&receipt.tx));
    if(submit) {
        assert(ieee80211_wnm_bss_transition_tx_fence_submit(&f.ic,receipt.tx,IEEE80211_WNM_TX_FENCE_RESPONSE));
        assert(ieee80211_wnm_bss_transition_tx_fence_submit(&f.ic,receipt.tx,IEEE80211_WNM_TX_FENCE_DEAUTH));
    }
    return receipt;
}
static void complete_btm(Fixture &f,const BtmFence &receipt,bool reverse=false) {
    ieee80211_wnm_bss_transition_tx_fence_complete(&f.ic,&f.source,receipt.tx,
        reverse ? IEEE80211_WNM_TX_FENCE_DEAUTH : IEEE80211_WNM_TX_FENCE_RESPONSE);
    ieee80211_wnm_bss_transition_tx_fence_complete(&f.ic,&f.source,receipt.tx,
        reverse ? IEEE80211_WNM_TX_FENCE_RESPONSE : IEEE80211_WNM_TX_FENCE_DEAUTH);
}
static bool copy_btm_target(Fixture &f) {
    uint8_t target[6]{};
    uint64_t generation=99;
    const bool copied=ieee80211_wnm_bss_transition_copy_retarget(&f.ic,
        f.source.ni_essid,f.source.ni_esslen,target,&generation)!=0;
    if(copied) {
        assert(IEEE80211_ADDR_EQ(target,f.target.ni_bssid));
        assert(generation && generation==f.ic.ic_wnm_bss_transition.request_generation);
    } else { const uint8_t zero[6]{}; assert(IEEE80211_ADDR_EQ(target,zero) && !generation); }
    return copied;
}
static void hook_identity() {}
static void sae_policy(Fixture &f,uint64_t generation,bool bound) {
    auto &ic=f.ic;
    ic.ic_flags|=IEEE80211_F_RSNON|IEEE80211_F_MFPR;
    ic.ic_flags&=~IEEE80211_F_PSK;
    ic.ic_pae_mfp_requested=1;
    ic.ic_rsnprotos=IEEE80211_PROTO_RSN; ic.ic_rsnakms=IEEE80211_AKM_SAE;
    ic.ic_rsnciphers=ic.ic_rsngroupcipher=IEEE80211_CIPHER_CCMP;
    ic.ic_rsngroupmgmtcipher=IEEE80211_CIPHER_BIP;
    ic.ic_sae_auth_hold=ic.ic_sae_auth_owned=ic.ic_sae_engine_peer_event=
        ic.ic_sae_wcl_request_revoke=hook_identity;
    ic.ic_sae_wcl_policy_generation=ic.ic_sae_wcl_request_next_generation=generation;
    ic.ic_sae_wcl_request={};
    auto &request=ic.ic_sae_wcl_request;
    request.generation=generation; request.ssid_len=f.source.ni_esslen;
    std::memcpy(request.ssid,f.source.ni_essid,request.ssid_len);
    request.phase=bound ? IEEE80211_SAE_WCL_REQUEST_BOUND : IEEE80211_SAE_WCL_REQUEST_PENDING;
    request.association_epoch=bound ? ic.ic_pae_assoc_epoch : 0;
    IEEE80211_ADDR_COPY(request.bssid,bound ? f.source.ni_bssid : f.target.ni_bssid);
    if(bound) {
        assert(ieee80211_pae_selected_bss_populate(&ic.ic_pae_selected_bss,
            f.source.ni_bssid,f.source.ni_essid,f.source.ni_esslen,0,
            IEEE80211_SAE_SELECTED_BSS_PROFILE_PURE));
        ic.ic_pae_selected_bss.epoch=ic.ic_pae_assoc_epoch;
    }
}
static int sae_wnm_boundary(ieee80211com *ic,const ieee80211_node *source,
    uint64_t generation,uint64_t sourceEpoch) {
    assert(onSaeWnm);
    assert(ieee80211_wnm_bss_transition_reconnect_current(ic,source,generation,sourceEpoch));
    const auto callback=onSaeWnm;
    return callback(ic);
}

static unsigned halTargetCalls;
static uint64_t halTargetGeneration;
// Only the lower credential/staging boundary is a double. It records the
// wrapper's copied owner and executes the complete production admission leaf.
static int hal_target_boundary(ieee80211com *ic,const ieee80211_node *source,
    const uint8_t *target,bool wnm,uint64_t generation) {
    ++halTargetCalls; halTargetGeneration=generation;
    assert(wnm && generation);
    return ieee80211_sae_wcl_request_retarget_run(ic,source,42,target,
        source->ni_essid,source->ni_esslen,1,generation)!=0;
}
#define BTM_HAL_FIXTURE(Owner, backend) \
class Owner { public: \
    static int backend##_sae_wnm_roam_start(ieee80211com *,const ieee80211_node *,uint64_t,uint64_t); \
    static int backend##_sae_targeted_roam_start(ieee80211com *ic,const ieee80211_node *source, \
        const uint8_t *target,bool wnm,uint64_t generation) { \
        return hal_target_boundary(ic,source,target,wnm,generation); \
    } \
};
BTM_HAL_FIXTURE(ItlIwn,iwn)
BTM_HAL_FIXTURE(ItlIwm,iwm)
BTM_HAL_FIXTURE(ItlIwx,iwx)
#undef BTM_HAL_FIXTURE
#include "hal.inc"

static BtmFence completed_btm(Fixture &f,bool scan) {
    if(!scan) {
        f.ic.ic_sae_wnm_roam_start=sae_wnm_boundary;
        onSaeWnm=[](ieee80211com *) { return 1; };
    }
    const auto receipt=arm_btm(f); complete_btm(f,receipt);
    assert(copy_btm_target(f));
    return receipt;
}

static unsigned btm_generation_matrix() {
    unsigned cases=0;
    for(bool scan : {false,true}) for(unsigned mutation=0;mutation<12;++mutation) {
        Fixture f; const auto receipt=completed_btm(f,scan);
        uint8_t target[6],ssid[32]; uint8_t length=f.source.ni_esslen;
        std::memcpy(target,f.target.ni_bssid,6); std::memcpy(ssid,f.source.ni_essid,32);
        const uint8_t *ssidArg=ssid,*targetArg=target;
        auto *ic=&f.ic; uint64_t generation=receipt.request;
        switch(mutation) {
            case 1: generation=0; break;
            case 2: ++generation; break;
            case 3: ssid[0]^=1; break;
            case 4: target[1]^=1; break;
            case 5: length=0; break;
            case 6: length=33; break;
            case 7: ssidArg=nullptr; break;
            case 8: targetArg=nullptr; break;
            case 9: ic=nullptr; break;
            case 10: f.ic.ic_pae_selected_bss_lock=nullptr; break;
            case 11: f.ic.ic_wnm_bss_transition.candidate_confirmed=0; break;
        }
        ieee80211_wnm_bss_transition_consume(ic,ssidArg,length,targetArg,generation);
        assert(bool(f.ic.ic_wnm_bss_transition.active)==(mutation!=0));
        assert(f.ic.ic_wnm_bss_transition_next_request==receipt.request &&
            f.ic.ic_wnm_bss_transition_next_tx_fence==receipt.tx);
        if(mutation==0) {
            ieee80211_wnm_bss_transition_consume(ic,ssidArg,length,targetArg,generation);
            assert(!f.ic.ic_wnm_bss_transition.active);
        }
        ++cases;
    }
    for(bool scan : {false,true}) for(unsigned mutation=0;mutation<8;++mutation) {
        Fixture f; const auto receipt=completed_btm(f,scan);
        uint8_t target[6]; std::memset(target,0xa5,6);
        uint64_t generation=99,*generationArg=&generation;
        auto *ic=&f.ic; const uint8_t *ssid=f.source.ni_essid;
        uint8_t length=f.source.ni_esslen,*targetArg=target;
        switch(mutation) {
            case 1: ic=nullptr; break;
            case 2: ssid=nullptr; break;
            case 3: length=0; break;
            case 4: length=33; break;
            case 5: targetArg=nullptr; break;
            case 6: generationArg=nullptr; break;
            case 7: f.ic.ic_pae_selected_bss_lock=nullptr; break;
        }
        const auto copied=ieee80211_wnm_bss_transition_copy_retarget(ic,ssid,length,targetArg,generationArg);
        assert(bool(copied)==(mutation==0));
        if(copied) assert(generation==receipt.request && IEEE80211_ADDR_EQ(target,f.target.ni_bssid));
        else {
            if(targetArg) { const uint8_t zero[6]{}; assert(IEEE80211_ADDR_EQ(target,zero)); }
            if(generationArg) assert(!generation);
        }
        assert(f.ic.ic_wnm_bss_transition.active);
        ++cases;
    }
    for(unsigned boundary=0;boundary<3;++boundary) for(bool completed : {false,true}) {
        Fixture f; const auto old=completed_btm(f,false);
        uint8_t target[6]{}; uint64_t generation=0;
        assert(ieee80211_wnm_bss_transition_copy_retarget(&f.ic,f.source.ni_essid,
            f.source.ni_esslen,target,&generation) && generation==old.request);
        if(boundary==1) ieee80211_pae_assoc_epoch_begin(&f.ic);
        if(boundary==2) ieee80211_new_state(&f.ic,IEEE80211_S_SCAN,-1);
        if(boundary==2) ieee80211_new_state(&f.ic,IEEE80211_S_RUN,-1);
        const auto next=arm_btm(f);
        if(completed) complete_btm(f,next);
        ieee80211_wnm_bss_transition_consume(&f.ic,f.source.ni_essid,
            f.source.ni_esslen,target,generation);
        assert(f.ic.ic_wnm_bss_transition.active &&
            f.ic.ic_wnm_bss_transition.request_generation==next.request);
        assert(f.ic.ic_wnm_bss_transition_next_request==next.request &&
            f.ic.ic_wnm_bss_transition_next_tx_fence==next.tx);
        ++cases;
    }
    for(bool scan : {false,true}) {
        Fixture f; const auto old=completed_btm(f,scan);
        const auto oldTarget=f.target;
        BtmFence next;
        onUnlock=[&] {
            onUnlock={};
            if(scan) ieee80211_new_state(&f.ic,IEEE80211_S_RUN,-1);
            f.target.ni_bssid[1]=0x77;
            next=arm_btm(f);
        };
        uint8_t target[6]{}; uint64_t generation=0;
        assert(ieee80211_wnm_bss_transition_copy_retarget(&f.ic,f.source.ni_essid,
            f.source.ni_esslen,target,&generation));
        assert(generation==old.request && IEEE80211_ADDR_EQ(target,oldTarget.ni_bssid));
        assert(next.request>generation && !IEEE80211_ADDR_EQ(target,f.target.ni_bssid));
        ieee80211_wnm_bss_transition_consume(&f.ic,f.source.ni_essid,
            f.source.ni_esslen,target,generation);
        assert(f.ic.ic_wnm_bss_transition.request_generation==next.request);
        ++cases;
    }
    for(bool scan : {false,true}) for(unsigned mutation=0;mutation<4;++mutation) {
        Fixture f; const auto receipt=completed_btm(f,scan); sae_policy(f,42,!scan);
        const auto before=f.ic.ic_sae_wcl_request;
        const auto flags=f.ic.ic_flags;
        uint64_t generation=receipt.request;
        if(mutation==1) generation=0;
        if(mutation==2) ++generation;
        if(mutation==3) generation=UINT64_MAX;
        const auto accepted=scan ?
            uint64_t(ieee80211_sae_wcl_request_admit_confirmed_wnm_candidate(&f.ic,42,generation)) :
            ieee80211_sae_wcl_request_retarget_run(&f.ic,&f.source,42,
                f.target.ni_bssid,f.source.ni_essid,f.source.ni_esslen,1,generation);
        assert(bool(accepted)==(mutation==0));
        if(!accepted) {
            assert(!std::memcmp(&before,&f.ic.ic_sae_wcl_request,sizeof(before)));
            assert(f.ic.ic_sae_wcl_request_next_generation==42 &&
                f.ic.ic_sae_wcl_policy_generation==42 && f.ic.ic_flags==flags);
        }
        ++cases;
    }
    for(unsigned domain=0;domain<3;++domain) {
        Fixture f; completed_btm(f,false); sae_policy(f,42,true);
        f.ic.ic_wcl_reassoc_owner_active=1;
        f.ic.ic_wcl_reassoc_owner_last_leaf=IEEE80211_WCL_REASSOC_OWNER_LEAF_ROAM_STARTED;
        IEEE80211_ADDR_COPY(f.ic.ic_wcl_reassoc_target_bssid,f.target.ni_bssid);
        const auto accepted=ieee80211_sae_wcl_request_retarget_run(&f.ic,&f.source,42,
            f.target.ni_bssid,f.source.ni_essid,f.source.ni_esslen,domain==1,domain==2 ? 1 : 0);
        assert(bool(accepted)==(domain==0));
        if(accepted) assert(accepted==43);
        else assert(f.ic.ic_sae_wcl_request.generation==42 && f.ic.ic_sae_wcl_request_next_generation==42);
        ++cases;
    }
    using Wrapper=int (*)(ieee80211com *,const ieee80211_node *,uint64_t,uint64_t);
    for(Wrapper wrapper : {ItlIwn::iwn_sae_wnm_roam_start,ItlIwm::iwm_sae_wnm_roam_start,
                           ItlIwx::iwx_sae_wnm_roam_start}) {
        for(unsigned mutation=0;mutation<8;++mutation) {
            Fixture f; const auto old=completed_btm(f,false); sae_policy(f,42,true);
            halTargetCalls=0; halTargetGeneration=0;
            uint64_t generation=old.request,epoch=old.sourceEpoch;
            const auto *source=&f.source;
            if(mutation==1) ++generation;
            if(mutation==2) ++epoch;
            if(mutation==3) source=&f.target;
            if(mutation==4) source=nullptr;
            if(mutation==5) f.source.ni_esslen=0;
            unsigned unlocks=0; BtmFence next;
            if(mutation>=6) onUnlock=[&] {
                if(++unlocks!=(mutation==6 ? 1u : 2u)) return;
                onUnlock={}; next=arm_btm(f); complete_btm(f,next);
            };
            const auto started=wrapper(&f.ic,source,generation,epoch);
            assert(bool(started)==(mutation==0));
            assert(halTargetCalls==unsigned(mutation==0 || mutation==7));
            if(halTargetCalls) assert(halTargetGeneration==old.request);
            if(mutation>=6) assert(next.request>old.request &&
                f.ic.ic_wnm_bss_transition.request_generation==next.request);
            assert(f.ic.ic_sae_wcl_request.generation==(mutation==0 ? 43u : 42u));
            ++cases;
        }
    }
    std::printf("PASS: %u actual BTM generation copy/consume/admission and three-HAL wrapper cases\n",cases);
    return cases;
}
static unsigned btm_handoff_matrix() {
    unsigned cases=0;
    for(bool open : {false,true}) for(bool reverse : {false,true}) {
        Fixture f; if(open) f.open();
        const auto receipt=arm_btm(f);
        assert(!copy_btm_target(f) && f.ic.ic_wnm_bss_transition.active);
        const auto first=reverse ? IEEE80211_WNM_TX_FENCE_DEAUTH : IEEE80211_WNM_TX_FENCE_RESPONSE;
        const auto second=reverse ? IEEE80211_WNM_TX_FENCE_RESPONSE : IEEE80211_WNM_TX_FENCE_DEAUTH;
        ieee80211_wnm_bss_transition_tx_fence_complete(&f.ic,&f.source,receipt.tx,first);
        assert(!copy_btm_target(f) && stateRequests==0 && f.ic.ic_pae_assoc_epoch==7);
        ieee80211_wnm_bss_transition_tx_fence_complete(&f.ic,&f.source,receipt.tx,first);
        ieee80211_wnm_bss_transition_tx_fence_complete(&f.ic,&f.source,receipt.tx+1,second);
        ieee80211_wnm_bss_transition_tx_fence_complete(&f.ic,&f.target,receipt.tx,second);
        ieee80211_wnm_bss_transition_tx_fence_complete(&f.ic,&f.source,receipt.tx,4);
        assert(stateRequests==0);
        ieee80211_wnm_bss_transition_tx_fence_complete(&f.ic,&f.source,receipt.tx,second);
        assert(f.ic.ic_state==IEEE80211_S_SCAN && f.ic.ic_pae_assoc_epoch==8);
        assert(stateRequests==1 && copy_btm_target(f));
        assert(ieee80211_wnm_bss_transition_handoff_current(&f.ic,receipt.request,8));
        complete_btm(f,receipt);
        ieee80211_node_wnm_reconnect(&f.ic,&f.source,receipt.request,receipt.sourceEpoch);
        assert(stateRequests==1 && copy_btm_target(f));
        assert(!(f.ic.ic_flags&(IEEE80211_F_BGSCAN|IEEE80211_F_DISABLE_BG_AUTO_CONNECT)));
        ++cases;
    }
    for(unsigned stage=0;stage<4;++stage) for(unsigned cancel=0;cancel<4;++cancel) {
        Fixture f; const auto receipt=arm_btm(f,stage!=0);
        if(stage==2) ieee80211_wnm_bss_transition_tx_fence_complete(&f.ic,&f.source,receipt.tx,IEEE80211_WNM_TX_FENCE_RESPONSE);
        if(stage==3) complete_btm(f,receipt);
        const auto before=f.ic.ic_pae_assoc_epoch;
        if(cancel<2) ieee80211_new_state(&f.ic,cancel ? IEEE80211_S_SCAN : IEEE80211_S_INIT,-1);
        else if(cancel==2) assert(ieee80211_pae_assoc_epoch_begin(&f.ic)==before+1);
        else assert(ieee80211_pae_assoc_epoch_begin_replacement(&f.ic)==before+1);
        assert(!f.ic.ic_wnm_bss_transition.active && !copy_btm_target(f));
        const auto requests=stateRequests;
        complete_btm(f,receipt);
        assert(stateRequests==requests && f.ic.ic_pae_assoc_epoch==before+1);
        assert(f.ic.ic_wnm_bss_transition_next_request==receipt.request);
        assert(f.ic.ic_wnm_bss_transition_next_tx_fence==receipt.tx);
        ++cases;
    }
    for(unsigned mutation=0;mutation<13;++mutation) {
        Fixture f; const auto receipt=arm_btm(f);
        // Hold the real completed-leave phase at the registered SAE boundary,
        // rather than directly inventing descriptor completion in the record.
        f.ic.ic_sae_wnm_roam_start=sae_wnm_boundary;
        onSaeWnm=[](ieee80211com *) { return 1; };
        complete_btm(f,receipt);
        assert(f.ic.ic_wnm_bss_transition.handoff_phase==IEEE80211_WNM_HANDOFF_LEAVE_DONE);
        auto generation=receipt.request, epoch=receipt.sourceEpoch;
        switch(mutation) {
            case 0: ++generation; break;
            case 1: ++epoch; break;
            case 2: f.ic.ic_wnm_bss_transition.handoff_phase=0; break;
            case 3: f.ic.ic_wnm_bss_transition.candidate_confirmed=0; break;
            case 4: f.source.ni_bssid[1]=1; break;
            case 5: f.source.ni_essid[0]='x'; break;
            case 6: f.ic.ic_state=IEEE80211_S_AUTH; break;
            case 7: f.ic.ic_opmode=IEEE80211_M_HOSTAP; break;
            case 8: f.ic.ic_bss=nullptr; break;
            case 9: f.ic.ic_pae_selected_bss_lock=nullptr; break;
            case 10: epoch=0; break;
            case 11: generation=0; break;
            case 12: generation=epoch=0; break;
        }
        assert(ieee80211_pae_assoc_epoch_begin_wnm_handoff(&f.ic,generation,epoch)==0);
        assert(f.ic.ic_pae_assoc_epoch==7 && stateRequests==0);
        ++cases;
    }
    for(unsigned boundary=0;boundary<4;++boundary) for(bool successor : {false,true}) {
        Fixture f; const auto receipt=arm_btm(f);
        auto replace=[&](ieee80211com *) {
            onUnlock={}; onRevoke={}; onPreflight={}; onSaeWnm={};
            assert(ieee80211_pae_assoc_epoch_begin(&f.ic)!=0);
            if(successor) { const auto next=arm_btm(f); assert(next.request>receipt.request); }
        };
        if(boundary==0) {
            ieee80211_wnm_bss_transition_tx_fence_complete(&f.ic,&f.source,receipt.tx,IEEE80211_WNM_TX_FENCE_RESPONSE);
            onUnlock=[&] { replace(&f.ic); };
            ieee80211_wnm_bss_transition_tx_fence_complete(&f.ic,&f.source,receipt.tx,IEEE80211_WNM_TX_FENCE_DEAUTH);
        } else {
            if(boundary==1) {
                f.ic.ic_sae_wnm_roam_start=sae_wnm_boundary;
                onSaeWnm=[&](ieee80211com *ic) { replace(ic); return 0; };
            } else if(boundary==2) onPreflight=[&](ieee80211com *ic) { replace(ic); return 0; };
            else onRevoke=replace;
            complete_btm(f,receipt);
        }
        assert(stateRequests==0 && f.ic.ic_state==IEEE80211_S_RUN);
        assert(f.ic.ic_pae_assoc_epoch==(boundary==3 ? 9u : 8u));
        assert(bool(f.ic.ic_wnm_bss_transition.active)==successor);
        if(successor) assert(f.ic.ic_wnm_bss_transition.request_generation>receipt.request);
        ++cases;
    }
    for(unsigned refusal=0;refusal<3;++refusal) {
        Fixture f; const auto receipt=arm_btm(f);
        if(refusal==0) preflightRejected=true;
        else if(refusal==1) backendError=5;
        else onNewstate=[&](ieee80211com *) { arm_btm(f); return 5; };
        complete_btm(f,receipt);
        assert(f.ic.ic_state==IEEE80211_S_RUN && f.ic.ic_pae_assoc_epoch==(refusal==0 ? 7u : 8u));
        assert(bool(f.ic.ic_wnm_bss_transition.active)==(refusal==2));
        if(refusal==2) assert(f.ic.ic_wnm_bss_transition.request_generation>receipt.request);
        ++cases;
    }
    for(unsigned mutation=0;mutation<8;++mutation) {
        Fixture f; const auto receipt=arm_btm(f); complete_btm(f,receipt);
        sae_policy(f,42,false);
        if(mutation==1) ++f.ic.ic_pae_assoc_epoch;
        if(mutation==2) f.source.ni_bssid[1]=1;
        if(mutation==3) f.ic.ic_wnm_bss_transition.handoff_phase=IEEE80211_WNM_HANDOFF_LEAVE_DONE;
        if(mutation==4) f.ic.ic_wnm_bss_transition.candidate_confirmed=0;
        if(mutation==5) f.ic.ic_sae_wcl_request.bssid[1]=1;
        if(mutation==6) f.ic.ic_sae_auth_owned=nullptr;
        if(mutation==7) f.ic.ic_flags|=IEEE80211_F_PSK;
        const int accepted=ieee80211_sae_wcl_request_admit_confirmed_wnm_candidate(&f.ic,42,receipt.request);
        assert(bool(accepted)==(mutation==0));
        if(accepted) {
            assert(f.ic.ic_sae_wcl_request.phase==IEEE80211_SAE_WCL_REQUEST_SCAN_ISSUED);
            assert(ieee80211_pae_assoc_epoch_begin_replacement(&f.ic)==9);
            assert(f.ic.ic_sae_wcl_request.phase==IEEE80211_SAE_WCL_REQUEST_SCAN_ISSUED);
            assert(!f.ic.ic_wnm_bss_transition.active);
        }
        ++cases;
    }
    for(unsigned mutation=0;mutation<8;++mutation) {
        Fixture f; const auto receipt=arm_btm(f); sae_policy(f,42,true);
        f.ic.ic_sae_wnm_roam_start=sae_wnm_boundary;
        onSaeWnm=[](ieee80211com *) { return 1; };
        complete_btm(f,receipt);
        if(mutation==1) ++f.ic.ic_pae_assoc_epoch;
        if(mutation==2) f.source.ni_bssid[1]=1;
        if(mutation==3) f.ic.ic_wnm_bss_transition.handoff_phase=0;
        if(mutation==4) f.ic.ic_wnm_bss_transition.candidate_confirmed=0;
        if(mutation==5) f.ic.ic_sae_auth_owned=nullptr;
        if(mutation==6) f.ic.ic_flags|=IEEE80211_F_PSK;
        if(mutation==7) f.ic.ic_sae_wcl_request_join_active=1;
        const auto next=ieee80211_sae_wcl_request_retarget_run(&f.ic,&f.source,42,
            f.target.ni_bssid,f.source.ni_essid,f.source.ni_esslen,1,receipt.request);
        assert(bool(next)==(mutation==0));
        if(next) {
            assert(next==43 && f.ic.ic_state==IEEE80211_S_RUN && f.ic.ic_pae_assoc_epoch==7);
            assert(f.ic.ic_sae_wcl_request.phase==IEEE80211_SAE_WCL_REQUEST_RUN_RETARGET_ISSUED);
            assert(ieee80211_pae_assoc_epoch_begin_replacement(&f.ic)==8);
            assert(f.ic.ic_sae_wcl_request.phase==IEEE80211_SAE_WCL_REQUEST_RUN_RETARGET_ISSUED);
            assert(!f.ic.ic_wnm_bss_transition.active);
        }
        ++cases;
    }
    std::printf("PASS: %u actual BTM TX/cancel/handoff and SAE admission cases\n",cases);
    return cases;
}
static void btm_consume_successor_control() {
    Fixture f; const auto old=arm_btm(f);
    // The explicit lower callback holds completed leave at RUN. The actual
    // two TX terminals and production copy-out still run; this is not a
    // fabricated descriptor result or an on-air SAE success substitute.
    f.ic.ic_sae_wnm_roam_start=sae_wnm_boundary;
    onSaeWnm=[](ieee80211com *) { return 1; };
    complete_btm(f,old);
    uint8_t oldTarget[6]{};
    uint64_t oldGeneration=0;
    assert(ieee80211_wnm_bss_transition_copy_retarget(&f.ic,
        f.source.ni_essid,f.source.ni_esslen,oldTarget,&oldGeneration));
    assert(oldGeneration==old.request);
    // An admitted successor has the same public ESS/target and a distinct
    // request/fence identity. The old carrier reaches consume after that
    // real arm/confirm boundary. No old command owns the new logical record.
    const auto next=arm_btm(f);
    assert(next.request>old.request && next.tx>old.tx);
    ieee80211_wnm_bss_transition_consume(&f.ic,f.source.ni_essid,
        f.source.ni_esslen,oldTarget,oldGeneration);
    std::fprintf(stderr,"actual BTM consume successor: old=%llu successor=%llu "
        "remaining=%llu active=%u\n",static_cast<unsigned long long>(old.request),
        static_cast<unsigned long long>(next.request),
        static_cast<unsigned long long>(f.ic.ic_wnm_bss_transition.request_generation),
        f.ic.ic_wnm_bss_transition.active);
    assert(f.ic.ic_wnm_bss_transition.active &&
        f.ic.ic_wnm_bss_transition.request_generation==next.request);
}
static void btm_admission_successor_control(bool scan) {
    Fixture f; const auto old=arm_btm(f);
    if(!scan) {
        f.ic.ic_sae_wnm_roam_start=sae_wnm_boundary;
        onSaeWnm=[](ieee80211com *) { return 1; };
    }
    complete_btm(f,old);
    assert(copy_btm_target(f));
    if(scan) ieee80211_new_state(&f.ic,IEEE80211_S_RUN,-1);
    const auto next=arm_btm(f); complete_btm(f,next);
    assert(next.request>old.request && copy_btm_target(f));
    sae_policy(f,42,!scan);
    // Policy and lower-hook registration are explicit boundary doubles.
    // Execute the complete admission leaf with the old copied public target
    // after another real BTM owns that same target. No raw epoch increment or
    // fabricated admission result replaces either producer's implementation.
    const auto admitted=scan ?
        uint64_t(ieee80211_sae_wcl_request_admit_confirmed_wnm_candidate(&f.ic,42,old.request)) :
        ieee80211_sae_wcl_request_retarget_run(&f.ic,&f.source,42,
            f.target.ni_bssid,f.source.ni_essid,f.source.ni_esslen,1,old.request);
    std::fprintf(stderr,"actual BTM stale admission: scan=%d old=%llu successor=%llu admitted=%llu\n",
        int(scan),static_cast<unsigned long long>(old.request),
        static_cast<unsigned long long>(next.request),static_cast<unsigned long long>(admitted));
    assert(!admitted && f.ic.ic_sae_wcl_request.generation==42);
}
int main() {
    if(std::getenv("BTM_CANCEL_REQUIRE")!=nullptr) { btm_cancel_control(); return 0; }
    if(std::getenv("BTM_TERMINAL_CANCEL_REQUIRE")!=nullptr) { btm_terminal_cancel_control(); return 0; }
    if(std::getenv("BTM_CONSUME_REQUIRE")!=nullptr) { btm_consume_successor_control(); return 0; }
    if(std::getenv("BTM_RETARGET_SUCCESSOR_REQUIRE")!=nullptr) { btm_admission_successor_control(false); return 0; }
    if(std::getenv("BTM_ADMIT_SUCCESSOR_REQUIRE")!=nullptr) { btm_admission_successor_control(true); return 0; }
    if(std::getenv("ROAM_LOSS_BASELINE")==nullptr && std::getenv("ROAM_EPOCH_BASELINE")==nullptr) {
        btm_handoff_matrix();
        btm_consume_successor_control();
        btm_admission_successor_control(false);
        btm_admission_successor_control(true);
        btm_generation_matrix();
    }
    {
        // Reproduce the radio failure without an AP/authentication double:
        // a selected target has crossed the controlled replacement epoch,
        // then an ordinary INIT/SCAN cancellation invalidates that attempt.
        // The complete production epoch/newstate functions run here. The
        // fixture does not claim to execute firmware or the AP controller.
        for (auto phase : {IEEE80211_WCL_REASSOC_OWNER_LEAF_ROAM_STARTED,
                           IEEE80211_WCL_REASSOC_OWNER_LEAF_SAME_BSS_TRANSPARENT,
                           IEEE80211_WCL_REASSOC_OWNER_LEAF_REASSOC_REQ_SENT,
                           IEEE80211_WCL_REASSOC_OWNER_LEAF_REASSOC_REQ_SEND_FAIL,
                           IEEE80211_WCL_REASSOC_OWNER_LEAF_REASSOC_REQ_TIMEOUT}) {
        for (auto state : {IEEE80211_S_RUN, IEEE80211_S_AUTH,
                           IEEE80211_S_ASSOC}) {
            for (auto next : {IEEE80211_S_INIT, IEEE80211_S_SCAN}) {
                Fixture f;
                f.ic.ic_state=state;
                f.ic.ic_pae_assoc_epoch=242;
                f.ic.ic_wcl_reassoc_next_serial=17;
                f.ic.ic_wcl_reassoc_owner_serial=17;
                f.ic.ic_wcl_reassoc_source_epoch=241;
                f.ic.ic_wcl_reassoc_owner_active=1;
                f.ic.ic_wcl_reassoc_owner_last_leaf=phase;
                f.ic.ic_wcl_reassoc_request.feature_flags=0x34;
                ieee80211_pae_assoc_epoch_note_newstate(&f.ic,next,-1);
                std::printf("POST_TARGET_CANCEL phase=%u state=%u next=%u active=%u serial=%llu source=%llu current=%llu\n",
                    unsigned(phase),unsigned(state),unsigned(next),f.ic.ic_wcl_reassoc_owner_active,
                    static_cast<unsigned long long>(f.ic.ic_wcl_reassoc_owner_serial),
                    static_cast<unsigned long long>(f.ic.ic_wcl_reassoc_source_epoch),
                    static_cast<unsigned long long>(f.ic.ic_pae_assoc_epoch));
                std::fflush(stdout);
                assert(f.ic.ic_pae_assoc_epoch==243);
                assert(!f.ic.ic_wcl_reassoc_owner_active);
                assert(f.ic.ic_wcl_reassoc_owner_serial==0);
                assert(f.ic.ic_wcl_reassoc_next_serial==17);
                assert(f.ic.ic_wcl_reassoc_request.feature_flags==0);
                assert(losses.empty());
                assert(!ieee80211_wcl_reassoc_scan_completion_begin(&f.ic,17));
            }
        }
        }
        std::puts("PASS: post-target cancellation requirements (30 phase/state edges)");
        if (std::getenv("ROAM_CANCEL_REQUIRE") != nullptr)
            return 0;
    }
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
        if (change==0 || change==12) {
            assert(result==8 && f.ic.ic_pae_assoc_epoch==8);
            assert(f.ic.ic_wcl_reassoc_owner_active==(change==0 ? 1U : 0U));
            identity.continuation_epoch=result;
            const auto irq=IOSimpleLockLockDisableInterrupt(&f.lock);
            assert(ieee80211_bss_switch_identity_current_locked(&f.ic,&identity));
            IOSimpleLockUnlockEnableInterrupt(&f.lock,irq);
        }
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
        // These are the narrow scan-only helper's unchanged exclusions,
        // not an assertion that hard cancellation preserves target owners.
        if (f.ic.ic_pae_selected_bss_lock != nullptr) {
            const auto irq=IOSimpleLockLockDisableInterrupt(&f.lock);
            ieee80211_wcl_reassoc_cancel_scan_epoch_locked(&f.ic,7);
            IOSimpleLockUnlockEnableInterrupt(&f.lock,irq);
        } else ieee80211_wcl_reassoc_cancel_scan_epoch_locked(&f.ic,7);
        assert(f.ic.ic_pae_assoc_epoch==7);
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
    for (unsigned change=0; change<9; ++change) {
        Fixture f;
        f.ic.ic_wcl_reassoc_next_serial=f.ic.ic_wcl_reassoc_owner_serial=31;
        f.ic.ic_wcl_reassoc_owner_active=1;
        f.ic.ic_wcl_reassoc_source_epoch=7;
        f.ic.ic_wcl_reassoc_owner_last_leaf=IEEE80211_WCL_REASSOC_OWNER_LEAF_ROAM_STARTED;
        f.ic.ic_wcl_reassoc_request.feature_flags=0x34;
        uint64_t expected=7;
        switch (change) {
        case 0: expected=0; break;
        case 1: expected=6; break;
        case 2: expected=8; break;
        case 3: f.ic.ic_wcl_reassoc_owner_serial=0; break;
        case 4: f.ic.ic_wcl_reassoc_next_serial=32; break;
        case 5: f.ic.ic_wcl_reassoc_owner_active=0; break;
        case 6: f.ic.ic_wcl_reassoc_owner_last_leaf=IEEE80211_WCL_REASSOC_OWNER_LEAF_IDLE; break;
        case 7: f.ic.ic_wcl_reassoc_owner_last_leaf=IEEE80211_WCL_REASSOC_OWNER_LEAF_SCAN_STARTED; break;
        case 8: f.ic.ic_pae_selected_bss_lock=nullptr; break;
        }
        const auto active=f.ic.ic_wcl_reassoc_owner_active;
        const auto serial=f.ic.ic_wcl_reassoc_owner_serial;
        if (f.ic.ic_pae_selected_bss_lock) {
            const auto irq=IOSimpleLockLockDisableInterrupt(&f.lock);
            ieee80211_wcl_reassoc_cancel_target_epoch_locked(&f.ic,expected);
            IOSimpleLockUnlockEnableInterrupt(&f.lock,irq);
        } else ieee80211_wcl_reassoc_cancel_target_epoch_locked(&f.ic,expected);
        assert(f.ic.ic_wcl_reassoc_owner_active==active);
        assert(f.ic.ic_wcl_reassoc_owner_serial==serial);
        assert(f.ic.ic_wcl_reassoc_request.feature_flags==0x34);
        assert(f.ic.ic_pae_assoc_epoch==7 && losses.empty());
        ++cases;
    }
    for (unsigned reentry=0; reentry<3; ++reentry) {
        Fixture f;
        f.ic.ic_wcl_reassoc_next_serial=f.ic.ic_wcl_reassoc_owner_serial=31;
        f.ic.ic_wcl_reassoc_owner_active=1;
        f.ic.ic_wcl_reassoc_source_epoch=7;
        f.ic.ic_wcl_reassoc_owner_last_leaf=IEEE80211_WCL_REASSOC_OWNER_LEAF_ROAM_STARTED;
        f.ic.ic_wcl_reassoc_request.feature_flags=0x34;
        f.ic.ic_sae_wcl_request.phase=IEEE80211_SAE_WCL_REQUEST_RUN_RETARGET_ISSUED;
        f.ic.ic_sae_wcl_request.generation=19;
        f.ic.ic_sae_wcl_request.association_epoch=7;
        // Execute the complete real replacement-epoch helper, not the old
        // one-line double. Credential/PMF callbacks remain explicit doubles.
        assert(ieee80211_pae_assoc_epoch_begin_replacement(&f.ic)==8);
        assert(f.ic.ic_pae_assoc_epoch==8 && f.ic.ic_pae_assoc_replace_epoch==8);
        assert(f.ic.ic_wcl_reassoc_owner_active && f.ic.ic_wcl_reassoc_owner_serial==31);
        assert(f.ic.ic_wcl_reassoc_source_epoch==7);
        assert(f.ic.ic_sae_wcl_request.association_epoch==0);
        assert(f.ic.ic_sae_wcl_request.generation==19);
        f.ic.ic_state=IEEE80211_S_AUTH;
        if (reentry) onRevoke=[reentry](ieee80211com *ic) {
            assert(!ic->ic_wcl_reassoc_owner_active);
            if (reentry==1) {
                // One nested ordinary cancellation must not re-publish or
                // revive the retired owner after the outer callback returns.
                onRevoke={};
                assert(ieee80211_pae_assoc_epoch_begin(ic)==10);
            } else {
                ic->ic_wcl_reassoc_next_serial=32;
                ic->ic_wcl_reassoc_owner_serial=32;
                ic->ic_wcl_reassoc_source_epoch=ic->ic_pae_assoc_epoch;
                ic->ic_wcl_reassoc_owner_active=1;
                ic->ic_wcl_reassoc_owner_last_leaf=IEEE80211_WCL_REASSOC_OWNER_LEAF_SCAN_STARTED;
                ic->ic_wcl_reassoc_request.feature_flags=0x78;
                ic->ic_flags |= IEEE80211_F_BGSCAN;
            }
        };
        assert(ieee80211_pae_assoc_epoch_begin(&f.ic)==9);
        onRevoke={};
        assert(!ieee80211_wcl_reassoc_scan_completion_begin(&f.ic,31));
        if (reentry==2) {
            assert(f.ic.ic_wcl_reassoc_owner_serial==32 && f.ic.ic_wcl_reassoc_owner_active);
            assert(f.ic.ic_wcl_reassoc_request.feature_flags==0x78);
            assert(f.ic.ic_flags & IEEE80211_F_BGSCAN);
            assert(ieee80211_wcl_reassoc_scan_completion_begin(&f.ic,32));
        } else assert(!f.ic.ic_wcl_reassoc_owner_active &&
            f.ic.ic_pae_assoc_epoch==(reentry==1 ? 10U : 9U));
        assert(losses.empty());
        ++cases;
    }
    for (auto state : {IEEE80211_S_SCAN, IEEE80211_S_AUTH, IEEE80211_S_ASSOC}) {
        Fixture f;
        f.ic.ic_state=state;
        f.ic.ic_wcl_reassoc_next_serial=f.ic.ic_wcl_reassoc_owner_serial=31;
        f.ic.ic_wcl_reassoc_owner_active=1;
        f.ic.ic_wcl_reassoc_source_epoch=6;
        f.ic.ic_wcl_reassoc_owner_last_leaf=IEEE80211_WCL_REASSOC_OWNER_LEAF_ROAM_STARTED;
        const auto next=static_cast<ieee80211_state>(unsigned(state)+1);
        ieee80211_pae_assoc_epoch_note_newstate(&f.ic,next,-1);
        assert(f.ic.ic_pae_assoc_epoch==7 && f.ic.ic_wcl_reassoc_owner_active);
        assert(f.ic.ic_wcl_reassoc_owner_serial==31 && losses.empty());
        ++cases;
    }
    std::printf("PASS: %u actual roam carrier ownership/bridge and BSS replacement cases\n",cases);
}
