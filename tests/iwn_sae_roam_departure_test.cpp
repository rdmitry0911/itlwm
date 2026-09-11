/* Execute all six production departure orchestration bodies. Hardware TX,
 * frame allocation, target join and WCL publication are explicit boundaries;
 * this is neither a radio emulator nor an on-air qualification. */
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include "itlwm/hal_iwn/IwnSaeRoamDeparture.hpp"

using IOInterruptState = unsigned;
using u_int8_t = uint8_t;
constexpr unsigned IEEE80211_M_STA=1, IEEE80211_S_RUN=4, IEEE80211_S_SCAN=1;
constexpr unsigned IEEE80211_F_RSNON=1, IEEE80211_F_MFPR=2, IEEE80211_F_PSK=4;
constexpr unsigned IEEE80211_F_BGSCAN=8, IEEE80211_F_DISABLE_BG_AUTO_CONNECT=16;
constexpr unsigned IEEE80211_F_TX_MGMT_ONLY=1, IEEE80211_AKM_SAE=8;
constexpr unsigned IEEE80211_NODE_MFP=1, IEEE80211_NODE_TXMGMTPROT=2;
constexpr unsigned IEEE80211_WCL_REASSOC_OWNER_LEAF_ROAM_STARTED=3;
constexpr unsigned IEEE80211_WCL_REASSOC_OWNER_LEAF_SCAN_FAILED=4;
constexpr unsigned IEEE80211_SAE_WCL_REQUEST_BOUND=2;
constexpr unsigned IEEE80211_NWID_LEN=32, IEEE80211_FC0_SUBTYPE_DEAUTH=0xc0;
constexpr unsigned IEEE80211_REASON_AUTH_LEAVE=3, IFF_RUNNING=1;
constexpr unsigned IWN_TX_RING_COUNT=256, IWN_HBUS_TARG_WRPTR=1;
#define IEEE80211_ADDR_EQ(a,b) (std::memcmp((a),(b),6)==0)
#define IEEE80211_ADDR_COPY(a,b) std::memcpy((a),(b),6)
#define XYLog(...) ((void)0)
struct IOSimpleLock { unsigned rank; bool held=false; };
using IOLock=IOSimpleLock;
static unsigned ranks[4], depth;
static void acquire(IOSimpleLock *lock) {
    assert(lock && !lock->held && depth<4);
    assert(depth==0 || ranks[depth-1]<lock->rank);
    ranks[depth++]=lock->rank; lock->held=true;
}
static void release(IOSimpleLock *lock) {
    assert(lock && lock->held && depth && ranks[depth-1]==lock->rank);
    --depth; lock->held=false;
}
static void IOSimpleLockLock(IOSimpleLock *lock) { acquire(lock); }
static void IOSimpleLockUnlock(IOSimpleLock *lock) { release(lock); }
static void IOLockLock(IOLock *lock) { acquire(lock); }
static void IOLockUnlock(IOLock *lock) { release(lock); }
static IOInterruptState IOSimpleLockLockDisableInterrupt(IOSimpleLock *lock) {
    acquire(lock); return depth;
}
static void IOSimpleLockUnlockEnableInterrupt(IOSimpleLock *lock, IOInterruptState irq) {
    assert(irq==depth); release(lock);
}
struct ieee80211_channel {};
#define IEEE80211_CHAN_ANYC nullptr
struct ieee80211_node {
    unsigned ni_port_valid=1, ni_flags=3, ni_fails=0, ni_esslen=3, refs=0;
    uint8_t ni_bssid[6]={2}, ni_macaddr[6]={2}, ni_essid[32]={'l','a','b'};
    ieee80211_channel channel;
    ieee80211_channel *ni_chan=&channel;
};
struct ieee80211_sae_wcl_request {
    unsigned phase=IEEE80211_SAE_WCL_REQUEST_BOUND;
    uint64_t generation=7, association_epoch=11;
    uint8_t bssid[6]={2};
};
struct NetStats { unsigned outputPackets=0; };
struct _ifnet { unsigned if_flags=IFF_RUNNING, if_timer=0; NetStats stats; NetStats *netStat=&stats; };
struct ieee80211com {
    _ifnet ifp;
    unsigned ic_opmode=IEEE80211_M_STA, ic_state=IEEE80211_S_RUN;
    unsigned ic_flags=IEEE80211_F_RSNON|IEEE80211_F_MFPR|IEEE80211_F_BGSCAN;
    unsigned ic_xflags=0, ic_rsnakms=IEEE80211_AKM_SAE;
    ieee80211_node *ic_bss=nullptr;
    IOSimpleLock *ic_pae_selected_bss_lock=nullptr;
    uint64_t ic_pae_assoc_epoch=11, ic_pae_assoc_replace_epoch=0;
    struct { uint64_t next_generation=19; } ic_wcl_join_attempt;
    uint64_t ic_wcl_reassoc_next_serial=23, ic_wcl_reassoc_owner_serial=23;
    uint64_t ic_wcl_reassoc_source_epoch=11, ic_sae_wcl_policy_generation=7;
    bool ic_wcl_reassoc_owner_active=true;
    unsigned ic_wcl_reassoc_owner_last_leaf=IEEE80211_WCL_REASSOC_OWNER_LEAF_ROAM_STARTED;
    uint8_t ic_myaddr[6]={4}, ic_wcl_reassoc_source_bssid[6]={2};
    uint8_t ic_wcl_reassoc_target_bssid[6]={6};
    ieee80211_sae_wcl_request ic_sae_wcl_request;
};
#define IC2IFP(ic) (&(ic)->ifp)
struct ItlSaeWclCredentialV1 {
    uint64_t request_generation=7;
    uint8_t bssid[6]={2}, ssid[32]={'l','a','b'};
    unsigned ssid_len=3;
};
struct iwn_tx_data { IwnSaeRoamDepartureIdentity sae_roam_departure{}; };
struct iwn_tx_ring { unsigned qid=0, cur=255; iwn_tx_data data[IWN_TX_RING_COUNT]; };
struct iwn_softc {
    ieee80211com sc_ic;
    IwnSaeRoamDepartureState sc_sae_roam_departure{};
    IOLock *sc_sae_tx_lifecycle_lock=nullptr;
    IOSimpleLock *sc_sae_wcl_credential_lock=nullptr;
    bool sc_sae_tx_lifecycle_closed=false, sc_sae_tx_detaching=false;
    bool sc_sae_wcl_credential_active=true, sc_sae_wcl_credential_staged=false;
    bool sc_sae_wcl_credential_pending=false;
    ItlSaeWclCredentialV1 sc_sae_wcl_credential;
    unsigned sc_tx_timer=0;
    struct { void (*update_sched)(iwn_softc *,unsigned,int,uint8_t,uint16_t); } ops;
};
struct Packet {};
using mbuf_t=Packet *;
struct ItlSaeAuthTxRequestV1 {};
struct WorkLoop { bool gated=true; bool inGate() const { return gated; } };
static unsigned callbacks, leases, packets, schedules, doorbells, targetStarts, failures, scans;
static bool runtimeEnabled=true, callbackAllowed=true, builderFails=false, targetWorks=true;
static bool inlineTerminal=false, failTx=false, failureReenters=false;
static int transmitError;
static ieee80211_node *candidate;
static std::function<void(iwn_softc *)> beforeDoorbell;
static std::function<void(ieee80211com *)> duringAmpduStop;
static bool iwn_sae_engine_callback_enter(iwn_softc *) {
    assert(depth==0); if (!callbackAllowed) return false; ++callbacks; return true;
}
static void iwn_sae_engine_callback_leave(iwn_softc *) { assert(depth==0 && callbacks); --callbacks; }
static bool iwn_sae_tx_lifecycle_enter(iwn_softc *sc, bool) {
    assert(depth==0); if (sc->sc_sae_tx_lifecycle_closed) return false; ++leases; return true;
}
static void iwn_sae_tx_lifecycle_leave(iwn_softc *) { assert(depth==0 && leases); --leases; }
static bool iwn_sae_engine_runtime_enabled(iwn_softc *) { return runtimeEnabled; }
static bool itl_sae_wcl_credential_is_well_formed(const ItlSaeWclCredentialV1 *c) {
    return c->request_generation && c->ssid_len && c->ssid_len<=32;
}
static ieee80211_node *ieee80211_find_node(ieee80211com *,const uint8_t *bssid) {
    return candidate && IEEE80211_ADDR_EQ(candidate->ni_bssid,bssid) ? candidate : nullptr;
}
static ieee80211_node *ieee80211_ref_node(ieee80211_node *node) { ++node->refs; return node; }
static void ieee80211_release_node(ieee80211com *,ieee80211_node *node) {
    assert(depth==0 && node->refs); --node->refs;
}
static void ieee80211_stop_ampdu_tx(ieee80211com *ic,ieee80211_node *,unsigned) {
    assert(depth==0); if (duringAmpduStop) duringAmpduStop(ic);
}
static mbuf_t ieee80211_protected_deauth_frame_build(ieee80211com *,ieee80211_node *,unsigned reason) {
    assert(depth==0 && reason==3); if (builderFails) return nullptr; ++packets; return new Packet;
}
static void mbuf_freem(mbuf_t packet) { assert(packet && packets); --packets; delete packet; }
static uint64_t ieee80211_wcl_reassoc_post_failure_owned(ieee80211com *ic,uint64_t serial,uint32_t) {
    assert(depth==0 && ic->ic_wcl_reassoc_owner_serial==serial);
    ++failures; ic->ic_wcl_reassoc_owner_active=false;
    if (failureReenters) { ++ic->ic_wcl_reassoc_next_serial; ic->ic_wcl_reassoc_owner_active=true; }
    return ic->ic_pae_assoc_epoch;
}
static void ieee80211_new_state(ieee80211com *ic,unsigned state,int) {
    assert(depth==0 && state==IEEE80211_S_SCAN); ++scans; ic->ic_state=state;
}
static void schedule(iwn_softc *sc,unsigned,int,uint8_t,uint16_t) {
    assert(depth==3 && sc->sc_sae_roam_departure.phase==IWN_SAE_ROAM_DEPARTURE_DOORBELLED);
    ++schedules;
}
static void writeDoorbell(iwn_softc *,unsigned reg,unsigned) {
    assert(depth==3 && reg==IWN_HBUS_TARG_WRPTR); ++doorbells;
}
#define IWN_WRITE(sc,reg,value) writeDoorbell(sc,reg,value)
class ItlIwn {
public:
    iwn_softc com;
    WorkLoop loop;
    iwn_tx_ring ring;
    ieee80211_node *descriptorNode=nullptr;
    mbuf_t descriptorPacket=nullptr;
    IwnSaeRoamDepartureIdentity submitted{};
    WorkLoop *getMainWorkLoop() { return &loop; }
    int iwn_sae_roam_departure_start(ieee80211com *,const ieee80211_node *,const uint8_t *);
    bool iwn_sae_roam_departure_commit(iwn_softc *,iwn_tx_ring *,int,uint8_t,uint16_t,const IwnSaeRoamDepartureIdentity *);
    void iwn_sae_roam_departure_terminal(iwn_softc *,const IwnSaeRoamDepartureIdentity *,bool);
    int iwn_tx(iwn_softc *,mbuf_t,ieee80211_node *,const ItlSaeAuthTxRequestV1 *,const IwnSaeRoamDepartureIdentity *);
    int iwn_sae_targeted_roam_start(ieee80211com *,const ieee80211_node *,const uint8_t *target,bool wnm) {
        assert(depth==0 && packets==0 && descriptorNode==nullptr && !wnm);
        assert(IEEE80211_ADDR_EQ(target,submitted.target_bssid));
        ++targetStarts; return targetWorks;
    }
    void complete() {
        assert(descriptorPacket && descriptorNode);
        mbuf_freem(descriptorPacket); descriptorPacket=nullptr;
        ieee80211_release_node(&com.sc_ic,descriptorNode); descriptorNode=nullptr;
        iwn_sae_roam_departure_terminal(&com,&submitted,failTx);
    }
};
#include "departure.inc"
int ItlIwn::iwn_tx(iwn_softc *sc,mbuf_t packet,ieee80211_node *node,
    const ItlSaeAuthTxRequestV1 *auth,const IwnSaeRoamDepartureIdentity *identity)
{
    assert(depth==0 && !auth && identity && node->refs==1);
    submitted=*identity;
    if (beforeDoorbell) beforeDoorbell(sc);
    if (transmitError) { mbuf_freem(packet); return transmitError; }
    if (!iwn_sae_roam_departure_commit(sc,&ring,ring.cur,0,42,identity)) {
        mbuf_freem(packet); return ECANCELED;
    }
    descriptorPacket=packet; descriptorNode=node;
    if (inlineTerminal) complete();
    return 0;
}

static void stateTests() {
    IwnSaeRoamDepartureState state{};
    IwnSaeRoamDepartureIdentity original{};
    original.association_epoch=1; original.reassoc_serial=2; original.source_generation=3;
    original.source_bssid[0]=2; original.target_bssid[0]=4; original.sta[0]=6;
    auto first=original;
    assert(iwn_sae_roam_departure_arm(state,first) && first.ticket==1);
    assert(!iwn_sae_roam_departure_complete(state,first));
    for (unsigned field=0; field<10; ++field) {
        auto stale=first;
        switch(field) {
        case 0: ++stale.ticket; break;
        case 1: ++stale.association_epoch; break;
        case 2: ++stale.reassoc_serial; break;
        case 3: ++stale.source_generation; break;
        case 4: ++stale.join_sequence; break;
        case 5: ++stale.reassoc_sequence; break;
        case 6: ++stale.source_bssid[5]; break;
        case 7: ++stale.target_bssid[5]; break;
        case 8: ++stale.sta[5]; break;
        case 9: stale.ticket=0; break;
        }
        assert(!iwn_sae_roam_departure_publish(state,stale));
        assert(!iwn_sae_roam_departure_cancel(state,stale));
    }
    assert(iwn_sae_roam_departure_publish(state,first));
    assert(!iwn_sae_roam_departure_publish(state,first));
    assert(iwn_sae_roam_departure_complete(state,first));
    assert(!iwn_sae_roam_departure_complete(state,first));
    auto next=original;
    assert(iwn_sae_roam_departure_arm(state,next) && next.ticket==2);
    assert(!iwn_sae_roam_departure_cancel(state,first));
    assert(iwn_sae_roam_departure_cancel(state,next));
    state.next_ticket=UINT64_MAX;
    auto exhausted=original;
    assert(!iwn_sae_roam_departure_arm(state,exhausted));
    assert(state.next_ticket==UINT64_MAX && exhausted.ticket==0);
}

int main() {
    stateTests();
    for (unsigned scenario=0; scenario<27; ++scenario) {
        ItlIwn driver;
        IOSimpleLock lifecycle{1}, credential{2}, selected{3};
        ieee80211_node source, target;
        target.ni_bssid[0]=target.ni_macaddr[0]=6;
        candidate=&target;
        auto *sc=&driver.com; auto *ic=&sc->sc_ic;
        ic->ic_bss=&source; ic->ic_pae_selected_bss_lock=&selected;
        sc->sc_sae_tx_lifecycle_lock=&lifecycle;
        sc->sc_sae_wcl_credential_lock=&credential;
        sc->ops.update_sched=schedule;
        schedules=doorbells=targetStarts=failures=scans=0;
        beforeDoorbell=nullptr; duringAmpduStop=nullptr;
        transmitError=0; builderFails=false; targetWorks=true;
        inlineTerminal=failTx=failureReenters=false;
        if (scenario==1) builderFails=true;
        if (scenario==2) transmitError=ENOBUFS;
        if (scenario==3) transmitError=ENOMEM;
        if (scenario==4) inlineTerminal=true;
        if (scenario==5) failTx=true;
        if (scenario==6 || scenario==7) targetWorks=false;
        if (scenario==7) failureReenters=true;
        if (scenario==8) beforeDoorbell=[](iwn_softc *s) { ++s->sc_ic.ic_pae_assoc_epoch; };
        if (scenario==9) beforeDoorbell=[](iwn_softc *s) { ++s->sc_ic.ic_wcl_reassoc_next_serial; };
        if (scenario==10) beforeDoorbell=[](iwn_softc *s) { s->sc_sae_tx_lifecycle_closed=true; iwn_sae_roam_departure_stop(s); };
        if (scenario==11) beforeDoorbell=[](iwn_softc *s) { s->sc_sae_wcl_credential_active=false; };
        if (scenario==12) beforeDoorbell=[](iwn_softc *s) { ++s->sc_sae_wcl_credential.request_generation; };
        if (scenario==13) beforeDoorbell=[](iwn_softc *s) { s->sc_sae_wcl_credential_staged=true; };
        if (scenario==14) beforeDoorbell=[](iwn_softc *s) { ++s->sc_ic.ic_sae_wcl_request.generation; };
        if (scenario==15) driver.loop.gated=false;
        if (scenario==16) source.ni_flags=0;
        if (scenario==17) candidate=nullptr;
        if (scenario==18) target.ni_essid[0]='x';
        if (scenario==19) sc->sc_sae_wcl_credential.ssid[0]='x';
        if (scenario==20) duringAmpduStop=[](ieee80211com *c) { ++c->ic_wcl_join_attempt.next_generation; };
        const int started=driver.iwn_sae_roam_departure_start(ic,&source,target.ni_bssid);
        assert(started==((scenario>=15 && scenario<=19) ? 0 : 1));
        if (driver.descriptorPacket) {
            assert(targetStarts==0 && sc->sc_sae_roam_departure.phase==IWN_SAE_ROAM_DEPARTURE_DOORBELLED);
            assert((ic->ic_xflags & IEEE80211_F_TX_MGMT_ONLY)!=0);
            assert(driver.ring.cur==0 && source.refs==1);
            if (scenario==21) ++ic->ic_pae_assoc_epoch;
            if (scenario==22) ++ic->ic_wcl_reassoc_next_serial;
            if (scenario==23) ++ic->ic_wcl_join_attempt.next_generation;
            if (scenario==24) { sc->sc_sae_tx_lifecycle_closed=true; iwn_sae_roam_departure_stop(sc); }
            if (scenario==25) ic->ic_sae_wcl_request.generation++;
            if (scenario==26) {
                const auto stale=driver.submitted;
                iwn_sae_roam_departure_stop(sc);
                auto successor=stale; successor.ticket=0;
                assert(iwn_sae_roam_departure_arm(sc->sc_sae_roam_departure,successor));
                assert(successor.ticket>stale.ticket);
                driver.complete();
                assert(sc->sc_sae_roam_departure.phase==IWN_SAE_ROAM_DEPARTURE_ARMED);
                assert(iwn_sae_roam_departure_cancel(sc->sc_sae_roam_departure,successor));
            } else driver.complete();
        }
        const bool published=scenario==0 || (scenario>=4 && scenario<=7) || scenario>=21;
        assert(doorbells==(published ? 1U : 0U) && schedules==doorbells);
        assert(targetStarts==(scenario==0 || (scenario>=4 && scenario<=7) ? 1U : 0U));
        assert(failures==((scenario>=1 && scenario<=3) || scenario==6 ||
            scenario==7 || (scenario>=10 && scenario<=14) ? 1U : 0U));
        assert(scans==(scenario==6 ? 1U : 0U));
        const unsigned targetsBefore=targetStarts;
        driver.iwn_sae_roam_departure_terminal(sc,&driver.submitted,false);
        assert(targetStarts==targetsBefore);
        assert(source.refs==0 && packets==0 && callbacks==0 && leases==0 && depth==0);
        std::printf("production SAE source departure scenario %u PASS\n",scenario);
    }
}
