/* Execute production IwnAuthBeacon.inc, complete iwn_newstate_impl/iwn_cmd,
 * and three complete common join helpers. IOKit, hardware leaf operations
 * and the generic state callback are explicit doubles; no RF is simulated. */
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <deque>
#include <sys/types.h>
#include "tests/kernel_memory_test_support.hpp"
#include "itlwm/hal_iwn/IwnAuthBeaconLease.hpp"
#include "itl80211/openbsd/net80211/ieee80211_join_attempt.h"

using IOReturn = int;
using IOInterruptState = unsigned;
static constexpr int kIOReturnSuccess = 0;
static constexpr int kIOReturnError = 5;
static constexpr uint64_t kSecondScale = 1000000000;
static constexpr unsigned IEEE80211_TRANS_WAIT = 5;
static constexpr unsigned IEEE80211_FC0_TYPE_MGT = 0;
static constexpr unsigned IEEE80211_FC0_SUBTYPE_BEACON = 0x80;
static constexpr unsigned IWN_CMD_RXON = 16;
static constexpr unsigned IWN_CMD_ADD_NODE = 24, IWN_CMD_LINK_QUALITY = 78;
static constexpr unsigned IWN_RX_DESC_LEN_MASK = 0x3fff;
static constexpr unsigned IFF_UP = 1, IFF_RUNNING = 2;
static constexpr unsigned IEEE80211_M_STA = 1;
static constexpr unsigned IEEE80211_EVT_STA_JOIN_FAILED = 1;
enum ieee80211_state { IEEE80211_S_INIT, IEEE80211_S_SCAN, IEEE80211_S_AUTH,
    IEEE80211_S_ASSOC, IEEE80211_S_RUN };
#define IEEE80211_ADDR_EQ(a,b) (memcmp((a),(b),6)==0)
#define IEEE80211_ADDR_COPY(a,b) memcpy((a),(b),6)
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define letoh32(x) (x)
#define XYLog(...) ((void)0)
struct IOSimpleLock { bool held = false; };
static unsigned heldLocks;
static IOSimpleLock *IOSimpleLockAlloc() { return new IOSimpleLock; }
static void IOSimpleLockFree(IOSimpleLock *p) { assert(!p->held); delete p; }
static IOInterruptState IOSimpleLockLockDisableInterrupt(IOSimpleLock *p) {
    assert(p && !p->held); p->held=true; return ++heldLocks;
}
static void IOSimpleLockUnlockEnableInterrupt(IOSimpleLock *p, IOInterruptState irq) {
    assert(p && p->held && irq == heldLocks); p->held=false; --heldLocks;
}
static uint64_t fakeNow = 100;
static void clock_get_uptime(uint64_t *now) { *now=fakeNow; }
static void clock_interval_to_deadline(uint64_t interval, uint64_t scale, uint64_t *d) {
    *d=fakeNow + interval*scale;
}
static void absolutetime_to_nanoseconds(uint64_t n, uint64_t *out) { *out=n; }
class OSObject { public: virtual ~OSObject() = default; };
static unsigned liveSources, allocatedSources, allocationFailure, addAttempts, addFailure;
class IOInterruptEventSource {
public:
    using Action = void (*)(OSObject *,IOInterruptEventSource *,int);
    OSObject *owner; Action action;
    unsigned references=1, signals=0;
    bool enabled=false, added=false;
    IOInterruptEventSource(OSObject *o, Action a):owner(o),action(a) { ++liveSources; }
    virtual ~IOInterruptEventSource() { assert(!added); --liveSources; }
    static IOInterruptEventSource *interruptEventSource(OSObject *o, Action a) {
        if (++allocatedSources==allocationFailure) return nullptr;
        return new IOInterruptEventSource(o,a);
    }
    void retain() { ++references; }
    void release() { assert(references); if (--references==0) delete this; }
    void enable() { enabled=true; }
    void disable() { enabled=false; }
    void interruptOccurred(void *,void *,int) { assert(heldLocks==0); ++signals; }
};
class IOTimerEventSource:public IOInterruptEventSource {
public:
    using TimerAction=void (*)(OSObject *,IOTimerEventSource *);
    TimerAction timerAction; unsigned milliseconds=0;
    IOTimerEventSource(OSObject *o,TimerAction a):
        IOInterruptEventSource(o,nullptr),timerAction(a) {}
    static IOTimerEventSource *timerEventSource(OSObject *o,TimerAction a) {
        if (++allocatedSources==allocationFailure) return nullptr;
        return new IOTimerEventSource(o,a);
    }
    void cancelTimeout() { milliseconds=0; }
    void setTimeoutMS(unsigned ms) { assert(heldLocks==0 && added); milliseconds=ms; }
};
class IOWorkLoop {
public:
    bool gated=true;
    bool inGate() const { return gated; }
    int addEventSource(IOInterruptEventSource *s) {
        if (++addAttempts==addFailure) return 5;
        assert(s && !s->added); s->added=true; return 0;
    }
    void removeEventSource(IOInterruptEventSource *s) {
        assert(s && s->added); s->added=false;
    }
};
struct ieee80211_channel { unsigned number; };
struct ieee80211_node { uint8_t ni_macaddr[6], ni_bssid[6]; ieee80211_channel *ni_chan; };
struct Packet;
using mbuf_t=Packet *;
struct PacketQueue { std::deque<Packet *> packets; bool active=false; };
struct NetworkStats { unsigned outputPackets=0,outputErrors=0; };
struct _ifnet {
    unsigned if_flags; void *if_softc;
    PacketQueue if_snd;
    NetworkStats stats;
    NetworkStats *netStat=&stats;
    unsigned if_timer=0;
};
struct ieee80211com {
    _ifnet ic_if; unsigned ic_opmode; int ic_state; unsigned ic_mgt_timer;
    ieee80211_node *ic_bss; uint8_t ic_myaddr[6];
    IOSimpleLock *ic_pae_selected_bss_lock;
    ieee80211_join_attempt ic_wcl_join_attempt;
    uint64_t ic_pae_assoc_epoch, ic_wcl_reassoc_next_serial, ic_wcl_reassoc_owner_serial;
    bool ic_wcl_reassoc_owner_active;
    void (*ic_event_handler)(ieee80211com *,unsigned,void *);
    bool ic_initial_scan_census_only;
    PacketQueue ic_mgtq;
    unsigned ic_xflags=0;
};
static unsigned ieee80211_chan2ieee(ieee80211com *,ieee80211_channel *c) { return c->number; }
struct ieee80211_frame {
    uint8_t i_fc[2], duration[2], i_addr1[6], i_addr2[6], i_addr3[6], sequence[2];
};
struct Packet { ieee80211_frame frame={}; ieee80211_node *node=nullptr; };
static bool ifq_is_oactive(PacketQueue *q) { return q->active; }
static void ifq_set_oactive(PacketQueue *q) { q->active=true; }
static Packet *mq_dequeue(PacketQueue *q) {
    if (q->packets.empty()) return nullptr;
    auto *p=q->packets.front(); q->packets.pop_front(); return p;
}
static Packet *ifq_dequeue(PacketQueue *q) { return mq_dequeue(q); }
static ieee80211_node *mbuf_pkthdr_rcvif(Packet *p) { return p->node; }
static size_t mbuf_len(Packet *) { return sizeof(ieee80211_frame); }
static unsigned ieee80211_get_hdrlen(ieee80211_frame *) { return sizeof(ieee80211_frame); }
static Packet *ieee80211_encap(_ifnet *,Packet *p,ieee80211_node **node) { *node=p->node; return p; }
static void ieee80211_release_node(ieee80211com *,ieee80211_node *) { assert(false); }
#define mtod(packet,type) reinterpret_cast<type>(&(packet)->frame)
#define IWX_AUTH_DIAG(...) ((void)0)
#define AIRPORT 1
#define NBPFILTER 0
static constexpr unsigned IEEE80211_FC0_TYPE_MASK=0x0c,IEEE80211_FC0_SUBTYPE_MASK=0xf0;
static constexpr unsigned IEEE80211_FC0_SUBTYPE_AUTH=0xb0,
    IEEE80211_FC0_SUBTYPE_ASSOC_REQ=0,IEEE80211_FC0_SUBTYPE_REASSOC_REQ=0x20;
static constexpr unsigned IEEE80211_F_TX_MGMT_ONLY=1;
struct iwn_rx_desc { uint32_t len; uint8_t type, flags, idx, qid; };
struct iwn_node_info { uint8_t id; };
struct iwn_cmd_link_quality { uint8_t id; };
struct iwn_tx_data { uint64_t auth_rxon_serial; };
struct iwn_tx_ring { unsigned cur; iwn_tx_data data[256]; };
struct iwn_softc {
    ieee80211com sc_ic; uint8_t bss_node_addr[6];
    struct { unsigned associd,filter,flags; } rxon;
    int rxonsz; unsigned command_queue; iwn_tx_ring txq[16];
    uint8_t broadcast_id=15;
    int (*sc_newstate)(ieee80211com *,ieee80211_state,int);
    void *driver;
    unsigned sc_flags,agg_queue_mask;
    unsigned qfullmsk=0,sc_tx_timer=0;
    int calib_to,init_task;
    struct { unsigned state; } calib;
    struct { const char *dv_xname; } sc_dev;
};
static const uint8_t etheranyaddr[6]={};
static void ieee80211_new_state(ieee80211com *,ieee80211_state,int);
static int ieee80211_begin_scan_with_result(_ifnet *);
static uint64_t ieee80211_wcl_reassoc_post_failure_owned(ieee80211com *,uint64_t,uint32_t);
#include "join.inc"

// Complete lower state function is included below. Only external leaf
// operations are doubles; unexpected scan/recovery transport is not simulated.
static constexpr int IEEE80211_NEWSTATE_ARG_SCAN_HOP=-9,
    IEEE80211_NEWSTATE_ARG_WNM_RECONNECT_HOLD=-8;
static constexpr unsigned IWN_FLAG_BGSCAN=1,IWN_FLAG_SCANNING=2,IWN_FLAG_FATAL_RECOVERY=4;
static constexpr unsigned IWN_CALIB_STATE_INIT=0,IWN_AP_STAGE_RUNNING=1,IWN_LED_LINK=1;
static constexpr unsigned IWN_FILTER_BSS=1,IWN_FILTER_NODECRYPT=2;
static constexpr unsigned IWN_RXON_HT_CHANMODE_MIXED2040=1,IWN_RXON_HT_CHANMODE_PURE40=2,IWN_RXON_HT_HT40MINUS=4;
static constexpr int LINK_STATE_DOWN=0,IEEE80211_CHAN_2GHZ=1;
#define IEEE80211_NEWSTATE_BACKEND_ARG(state,arg) (arg)
#define container_of(sc,type,member) static_cast<type *>((sc)->driver)
#ifndef htole32
#define htole32(value) (value)
#endif
#define AirportItlwmPostPltiTraceRecord(...) ((void)0)
static void *systq;
static uint64_t ieee80211_pae_assoc_epoch_current(ieee80211com *ic) { return ic->ic_pae_assoc_epoch; }
static bool ieee80211_wcl_join_failure_pending(ieee80211com *ic,uint64_t generation) {
    return ic->ic_wcl_join_attempt.phase==IEEE80211_JOIN_FAILING &&
        ic->ic_wcl_join_attempt.result.generation==generation;
}
static bool iwn_wcl_initial_scan_pending_blocks_generic(iwn_softc *) { return false; }
static int ieee80211_sae_wcl_request_scan_starting(ieee80211com *,uint64_t *) { return 0; }
static void ieee80211_stop_ampdu_tx(ieee80211com *,ieee80211_node *,int) {}
static void ieee80211_ba_del(ieee80211_node *) {}
static void timeout_del(int *) {}
static bool ieee80211_sae_wcl_request_scan_deferred(ieee80211com *,uint64_t) { assert(false); return false; }
static bool iwn_scan_lease_defer_scan(iwn_softc *,ieee80211_state,int,uint64_t,uint64_t *,bool *) { assert(false); return false; }
static void iwn_scan_lease_abort_submission_failed(iwn_softc *,uint64_t) { assert(false); }
static void iwn_scan_lease_drop_replay(iwn_softc *) { assert(false); }
static void task_add(void *,int *) { assert(false); }
static void iwn_clear_apple_nrate_cache(iwn_softc *) {}
static void ieee80211_set_link_state(ieee80211com *,int) {}
static void ieee80211_node_cleanup_scan_hop(ieee80211com *,ieee80211_node *) { assert(false); }
static bool ieee80211_node_cleanup_sae_wcl_scan_starting(ieee80211com *,ieee80211_node *,uint64_t) { assert(false); return false; }
static void ieee80211_node_cleanup(ieee80211com *,ieee80211_node *) {}
static bool ieee80211_sae_wcl_request_scan_started(ieee80211com *,uint64_t) { assert(false); return false; }
static void ieee80211_roam_link_failed(ieee80211com *,uint64_t) { assert(false); }

class ItlIwn:public OSObject {
public:
    iwn_softc com={};
    IOWorkLoop workloop;
    IOSimpleLock *authBeaconLock=nullptr;
    IOInterruptEventSource *authBeaconSource=nullptr;
    IOTimerEventSource *authBeaconTimer=nullptr;
    bool authBeaconSourceAdded=false,authBeaconTimerAdded=false;
    IwnAuthBeaconLease authBeacon={};
    bool apFirmwareTransitionActive=false,apStaBssAssociated=false;
    unsigned apFirmwareStage=0;
    struct Controller { void setProperty(const char *,const char *) {} } controller;
    Controller *getController() { return &controller; }
    unsigned transmitted=0;
    int iwn_tx(iwn_softc *,Packet *,ieee80211_node *) { ++transmitted; return 0; }
    static IOReturn _iwn_start_task(OSObject *,void *,void *,void *,void *);
    unsigned commands=0, lowerCalls=0, genericCalls=0, scans=0, initCalls=0, cleanupRequests=0;
    uint64_t cleanupGeneration=0;
    int lowerError=0;
    bool eagerReceipt=false,eagerBeacon=false,replaceDuringGeneric=false,replaceDuringInit=false;
    bool automaticPreparationReceipt=true;
    ieee80211_channel channel={13};
    ieee80211_node node={};
    IOWorkLoop *getMainWorkLoop() { return &workloop; }
    bool iwn_auth_beacon_init();
    void iwn_auth_beacon_shutdown();
    void iwn_auth_beacon_cancel(bool=false);
    bool iwn_auth_beacon_pending();
    bool iwn_auth_beacon_current(const IwnAuthBeaconRequest &);
    int iwn_auth_beacon_enqueue(int,int);
    void iwn_auth_beacon_kick();
    void iwn_auth_beacon_drain();
    void iwn_auth_beacon_fail(IwnAuthBeaconRequest,int);
    int iwn_auth_rxon(iwn_softc *);
    int iwn_auth_preparation_cmd(iwn_softc *,int,const void *,int,int);
    void iwn_auth_beacon_note_command(iwn_rx_desc *,uint64_t);
    uint64_t iwn_auth_beacon_rx_owner();
    void iwn_auth_beacon_note_rx(const ieee80211_frame *,size_t,uint16_t,uint64_t);
    static void iwn_auth_beacon_event(OSObject *,IOInterruptEventSource *,int);
    static void iwn_auth_beacon_timeout(OSObject *,IOTimerEventSource *);
    static int iwn_newstate_impl(ieee80211com *,ieee80211_state,int,uint64_t,uint64_t);
    static int iwn_newstate_preflight(ieee80211com *,ieee80211_state,int) { return 0; }
    void iwn_scan_abort(iwn_softc *) {}
    void iwn_set_led(iwn_softc *,int,int,int) {}
    int iwn_scan_abort_command(iwn_softc *,uint64_t) { assert(false); return EIO; }
    int iwn_scan(iwn_softc *,int,int,uint64_t) { assert(false); return EIO; }
    int iwn_run(iwn_softc *) { assert(false); return EIO; }
    int iwn_auth(iwn_softc *,int);
    static void iwn_wcl_join_failure_scan(ieee80211com *,uint64_t);
    int iwn_cmd(iwn_softc *,int,const void *,int,int);
    int iwn_cmd_with_doorbell_hook(iwn_softc *,int,const void *,int,int,
        bool (*)(iwn_softc *,void *),void (*)(iwn_softc *,void *),void *);
    void rxonReply(uint64_t serial=0,uint8_t flags=0) {
        iwn_rx_desc reply={4,IWN_CMD_RXON,flags,7,9};
        iwn_auth_beacon_note_command(&reply, serial ? serial:authBeacon.request.serial);
    }
    void preparationReply(unsigned slot,uint8_t status=1,uint32_t length=5) {
        struct { iwn_rx_desc desc; uint8_t status; } reply={
            {length,static_cast<uint8_t>(slot==0 ? IWN_CMD_ADD_NODE:IWN_CMD_LINK_QUALITY),
             0,static_cast<uint8_t>(slot+8),9},status};
        iwn_auth_beacon_note_command(&reply.desc,authBeacon.request.serial);
    }
    void beacon(unsigned chan=13,uint8_t type=0x80,bool wrong=false,size_t len=36) {
        ieee80211_frame frame={}; frame.i_fc[0]=type;
        memcpy(frame.i_addr2,node.ni_bssid,6); memcpy(frame.i_addr3,node.ni_bssid,6);
        if (wrong) frame.i_addr3[5]^=1;
        iwn_auth_beacon_note_rx(&frame,len,chan,iwn_auth_beacon_rx_owner());
    }
    void setup(bool fresh=false);
    void teardown() {
        iwn_auth_beacon_shutdown();
        iwn_auth_beacon_shutdown();
        IOSimpleLockFree(authBeaconLock); authBeaconLock=nullptr;
        IOSimpleLockFree(com.sc_ic.ic_pae_selected_bss_lock);
        assert(liveSources==0 && heldLocks==0);
    }
};
static ItlIwn *active;
static unsigned published;
static void event(ieee80211com *,unsigned,void *) { assert(heldLocks==0); ++published; }
static int generic(ieee80211com *ic,ieee80211_state state,int) {
    assert(active->workloop.inGate() && heldLocks==0);
    if (state==IEEE80211_S_INIT) { ic->ic_state=state; return 0; }
    ++active->genericCalls; ic->ic_state=state;
    if (active->replaceDuringGeneric) {
        ++ic->ic_pae_assoc_epoch;
        assert(active->iwn_auth_beacon_enqueue(IEEE80211_S_AUTH,-1)==0);
    }
    return 0;
}
static void ieee80211_new_state(ieee80211com *ic,ieee80211_state state,int) {
    assert(state==IEEE80211_S_INIT && heldLocks==0);
    ++active->initCalls;
    ++ic->ic_pae_assoc_epoch;
    assert(ItlIwn::iwn_newstate_impl(ic,state,-1,0,0)==0);
    if (active->replaceDuringInit)
        active->iwn_auth_beacon_cancel(true);
}
static int ieee80211_begin_scan_with_result(_ifnet *) {
    assert(heldLocks==0); ++active->scans; return 0;
}
static uint64_t ieee80211_wcl_reassoc_post_failure_owned(
    ieee80211com *ic,uint64_t serial,uint32_t) {
    assert(ic->ic_wcl_reassoc_owner_active && serial==ic->ic_wcl_reassoc_owner_serial);
    ic->ic_wcl_reassoc_owner_active=false;
    return ++ic->ic_pae_assoc_epoch;
}
void ItlIwn::setup(bool fresh) {
    active=this; published=0; fakeNow=100;
    allocationFailure=addFailure=allocatedSources=addAttempts=0;
    auto &ic=com.sc_ic;
    const uint8_t bssid[6]={0x9a,0xfb,0x5d,0x97,0xa9,2};
    memcpy(node.ni_macaddr,bssid,6); memcpy(node.ni_bssid,bssid,6);
    node.ni_chan=&channel; ic.ic_bss=&node; ic.ic_myaddr[0]=2;
    ic.ic_if.if_flags=IFF_UP|IFF_RUNNING; ic.ic_opmode=IEEE80211_M_STA;
    ic.ic_state=IEEE80211_S_SCAN; ic.ic_pae_assoc_epoch=11;
    ic.ic_pae_selected_bss_lock=IOSimpleLockAlloc(); ic.ic_event_handler=event;
    com.command_queue=9; com.txq[9].cur=7; com.sc_newstate=generic;
    com.driver=this; ic.ic_if.if_softc=&com;
    assert(iwn_auth_beacon_init()); assert(authBeacon.reopen());
    if (fresh) {
        const uint8_t ssid[3]={'L','a','b'};
        const auto generation=ieee80211_join_attempt_begin(&ic.ic_wcl_join_attempt,bssid,ssid,3);
        assert(ieee80211_join_attempt_bind(&ic.ic_wcl_join_attempt,generation,11,bssid,ssid,3));
    }
}
int ItlIwn::iwn_cmd_with_doorbell_hook(iwn_softc *sc,int code,const void *,int,int,
    bool (*pre)(iwn_softc *,void *),void (*post)(iwn_softc *,void *),void *context) {
    assert(heldLocks==0);
    if (lowerError) return lowerError;
    if (!pre) { ++commands; return 0; }
    if (!pre(sc,context)) return ECANCELED;
    assert(heldLocks==2 && sc->txq[9].data[sc->txq[9].cur].auth_rxon_serial==authBeacon.request.serial);
    ++commands; // The physical WRPTR operation is a boundary double.
    post(sc,context); assert(heldLocks==0);
    if (code==IWN_CMD_RXON) {
        if (eagerBeacon) beacon();
        if (eagerReceipt) rxonReply();
        if (eagerBeacon) beacon();
    } else if (automaticPreparationReceipt)
        preparationReply(code==IWN_CMD_ADD_NODE ? 0:1);
    return 0;
}
int ItlIwn::iwn_auth(iwn_softc *sc,int) {
    assert(sc==&active->com && heldLocks==0);
    ++active->lowerCalls;
    sc->txq[9].cur=7;
    int result=active->iwn_auth_rxon(sc);
    if (result) return result;
    iwn_node_info node={sc->broadcast_id};
    iwn_cmd_link_quality link={sc->broadcast_id};
    sc->txq[9].cur=8;
    result=active->iwn_cmd(sc,IWN_CMD_ADD_NODE,&node,sizeof(node),1);
    if (result) return result;
    sc->txq[9].cur=9;
    return active->iwn_cmd(sc,IWN_CMD_LINK_QUALITY,&link,sizeof(link),1);
}
void ItlIwn::iwn_wcl_join_failure_scan(ieee80211com *,uint64_t generation) {
    assert(heldLocks==0); ++active->cleanupRequests; active->cleanupGeneration=generation;
}
#include "itlwm/hal_iwn/IwnAuthBeacon.inc"
#include "command.inc"
#include "lower.inc"
#include "start.inc"

int main() {
    unsigned cases=0;
    {
        ItlIwn d; d.setup();
        assert(d.iwn_auth_beacon_enqueue(IEEE80211_S_AUTH,-1)==0);
        assert(d.commands==0 && d.genericCalls==0);
        d.iwn_auth_beacon_drain();
        assert(d.commands==3 && d.genericCalls==0 && d.iwn_auth_beacon_pending());
        d.beacon(); d.iwn_auth_beacon_drain(); assert(d.genericCalls==0);
        d.rxonReply(d.authBeacon.request.serial+1); d.beacon(); d.iwn_auth_beacon_drain();
        assert(d.genericCalls==0);
        d.rxonReply(); d.beacon(9); d.beacon(13,0x50); d.beacon(13,0x80,true);
        d.beacon(13,0x80,false,35); d.iwn_auth_beacon_drain(); assert(d.genericCalls==0);
        d.beacon(); d.iwn_auth_beacon_drain(); assert(d.genericCalls==1);
        d.rxonReply(); d.beacon(); d.iwn_auth_beacon_drain(); assert(d.genericCalls==1);
        d.teardown(); ++cases;
    }
    {
        ItlIwn d; d.setup();
        Packet management, data; management.node=data.node=&d.node;
        management.frame.i_fc[0]=0xb0;
        d.com.sc_ic.ic_mgtq.packets.push_back(&management);
        d.com.sc_ic.ic_if.if_snd.packets.push_back(&data);
        assert(ItlIwn::iwn_newstate_impl(&d.com.sc_ic,IEEE80211_S_AUTH,-1,0,0)==0);
        for (unsigned phase=0;phase<3;++phase) {
            assert(ItlIwn::_iwn_start_task(&d,&d.com.sc_ic.ic_if,nullptr,nullptr,nullptr)==0);
            assert(d.transmitted==0 && d.com.sc_ic.ic_mgtq.packets.size()==1 &&
                d.com.sc_ic.ic_if.if_snd.packets.size()==1 && !d.com.sc_ic.ic_if.if_snd.active);
            if (phase==0) d.iwn_auth_beacon_drain();
            if (phase==1) d.rxonReply();
        }
        d.beacon(); d.iwn_auth_beacon_drain();
        assert(ItlIwn::_iwn_start_task(&d,&d.com.sc_ic.ic_if,nullptr,nullptr,nullptr)==0);
        assert(d.transmitted==1 && d.com.sc_ic.ic_if.if_snd.packets.size()==1);
        d.com.sc_ic.ic_state=IEEE80211_S_ASSOC;
        assert(ItlIwn::_iwn_start_task(&d,&d.com.sc_ic.ic_if,nullptr,nullptr,nullptr)==0);
        assert(d.transmitted==1);
        d.com.sc_ic.ic_state=IEEE80211_S_RUN;
        assert(ItlIwn::_iwn_start_task(&d,&d.com.sc_ic.ic_if,nullptr,nullptr,nullptr)==0);
        assert(d.transmitted==2 && d.com.sc_ic.ic_if.if_snd.packets.empty());
        d.teardown(); ++cases;
    }
    for (unsigned variant=0;variant<3;++variant) {
        ItlIwn d; d.setup();
        const auto target=variant==1 ? IEEE80211_S_ASSOC:IEEE80211_S_AUTH;
        if (variant==1) d.com.sc_ic.ic_state=IEEE80211_S_RUN;
        d.workloop.gated=false;
        assert(ItlIwn::iwn_newstate_impl(&d.com.sc_ic,target,-1,0,0)==0);
        assert(d.commands==0 && d.genericCalls==0 && d.iwn_auth_beacon_pending());
        d.iwn_auth_beacon_drain(); assert(d.commands==0);
        d.workloop.gated=true;
        if (variant==2) {
            assert(ItlIwn::iwn_newstate_impl(&d.com.sc_ic,IEEE80211_S_INIT,-1,0,0)==0);
            d.iwn_auth_beacon_drain();
            assert(d.commands==0 && d.genericCalls==0 && !d.iwn_auth_beacon_pending());
        } else {
            d.iwn_auth_beacon_drain(); d.rxonReply(); d.beacon();
            d.iwn_auth_beacon_drain();
            assert(d.genericCalls==1 && d.com.sc_ic.ic_state==target);
        }
        d.teardown(); ++cases;
    }
    {
        ItlIwn d; d.setup();
        assert(ItlIwn::iwn_newstate_impl(&d.com.sc_ic,IEEE80211_S_AUTH,-1,0,99)==ECANCELED);
        assert(d.commands==0 && d.genericCalls==0);
        d.teardown(); ++cases;
    }
    {
        ItlIwn d; d.setup(); d.eagerReceipt=d.eagerBeacon=true;
        assert(d.iwn_auth_beacon_enqueue(IEEE80211_S_AUTH,-1)==0);
        d.iwn_auth_beacon_drain(); assert(d.genericCalls==1 && d.commands==3);
        d.teardown(); ++cases;
    }
    {
        ItlIwn d; d.setup();
        assert(d.iwn_auth_beacon_enqueue(IEEE80211_S_AUTH,-1)==0);
        d.iwn_auth_beacon_drain(); const auto old=d.authBeacon.request.serial;
        ++d.com.sc_ic.ic_pae_assoc_epoch;
        assert(d.iwn_auth_beacon_enqueue(IEEE80211_S_AUTH,-1)==0);
        d.iwn_auth_beacon_drain(); d.rxonReply(old); d.beacon(); d.iwn_auth_beacon_drain();
        assert(d.genericCalls==0 && d.commands==6);
        d.rxonReply(); d.beacon(); d.iwn_auth_beacon_drain(); assert(d.genericCalls==1);
        d.teardown(); ++cases;
    }
    for (int variant=0;variant<3;++variant) {
        ItlIwn d; d.setup();
        assert(d.iwn_auth_beacon_enqueue(IEEE80211_S_AUTH,-1)==0);
        if (variant!=0) d.iwn_auth_beacon_drain();
        if (variant==2) d.rxonReply();
        fakeNow=d.authBeacon.request.deadline;
        d.rxonReply(); d.beacon(); d.iwn_auth_beacon_drain();
        assert(d.genericCalls==0 && d.initCalls==1 && d.scans==1);
        assert(d.commands==(variant==0 ? 0U:3U));
        d.teardown(); ++cases;
    }
    {
        ItlIwn d; d.setup(true);
        assert(d.iwn_auth_beacon_enqueue(IEEE80211_S_AUTH,-1)==0);
        d.iwn_auth_beacon_drain(); d.rxonReply(0,0x40); d.iwn_auth_beacon_drain();
        assert(d.genericCalls==0 && d.cleanupRequests==1 && d.initCalls==0);
        auto &attempt=d.com.sc_ic.ic_wcl_join_attempt;
        assert(attempt.phase==IEEE80211_JOIN_FAILING && published==0);
        assert(attempt.cleanup_pending==(IEEE80211_JOIN_CLEANUP_LOWER|IEEE80211_JOIN_CLEANUP_SAE));
        ieee80211_wcl_join_cleanup_done(&d.com.sc_ic,d.cleanupGeneration,IEEE80211_JOIN_CLEANUP_LOWER);
        assert(published==0);
        ieee80211_wcl_join_cleanup_done(&d.com.sc_ic,d.cleanupGeneration,IEEE80211_JOIN_CLEANUP_SAE);
        assert(published==1);
        d.teardown(); ++cases;
    }
    {
        ItlIwn d; d.setup(); d.lowerError=EIO;
        assert(d.iwn_auth_beacon_enqueue(IEEE80211_S_AUTH,-1)==0);
        d.iwn_auth_beacon_drain(); assert(d.genericCalls==0 && d.commands==0 && d.scans==1);
        d.teardown(); ++cases;
    }
    {
        ItlIwn d; d.setup(); d.replaceDuringGeneric=d.eagerReceipt=d.eagerBeacon=true;
        assert(d.iwn_auth_beacon_enqueue(IEEE80211_S_AUTH,-1)==0);
        d.iwn_auth_beacon_drain();
        assert(d.genericCalls==1 && d.iwn_auth_beacon_pending());
        assert(d.authBeacon.request.serial==2);
        d.teardown(); ++cases;
    }
    {
        ItlIwn d; d.setup(); d.replaceDuringInit=true;
        assert(d.iwn_auth_beacon_enqueue(IEEE80211_S_AUTH,-1)==0);
        d.iwn_auth_beacon_drain(); fakeNow=d.authBeacon.request.deadline;
        d.iwn_auth_beacon_drain(); assert(d.initCalls==1 && d.scans==0 && d.genericCalls==0);
        d.teardown(); ++cases;
    }
    {
        ItlIwn d; d.setup();
        d.com.sc_ic.ic_state=IEEE80211_S_RUN;
        d.com.sc_ic.ic_wcl_reassoc_owner_active=true;
        d.com.sc_ic.ic_wcl_reassoc_next_serial=9;
        d.com.sc_ic.ic_wcl_reassoc_owner_serial=9;
        assert(d.iwn_auth_beacon_enqueue(IEEE80211_S_AUTH,-1)==0);
        d.iwn_auth_beacon_drain(); fakeNow=d.authBeacon.request.deadline;
        d.iwn_auth_beacon_drain(); assert(d.initCalls==1 && d.scans==1 && d.genericCalls==0);
        d.teardown(); ++cases;
    }
    for (int variant=0;variant<4;++variant) {
        ItlIwn d; d.setup();
        assert(d.iwn_auth_beacon_enqueue(IEEE80211_S_AUTH,-1)==0);
        if (variant==0) ++d.com.sc_ic.ic_pae_assoc_epoch;
        if (variant==1) ++d.com.sc_ic.ic_wcl_join_attempt.next_generation;
        if (variant==2) ++d.com.sc_ic.ic_wcl_reassoc_next_serial;
        if (variant==3) d.com.sc_ic.ic_myaddr[5]^=1;
        d.iwn_auth_beacon_drain();
        assert(d.commands==0 && d.genericCalls==0 && !d.iwn_auth_beacon_pending());
        d.teardown(); ++cases;
    }
    {
        ItlIwn d; d.setup();
        assert(d.iwn_auth_beacon_enqueue(IEEE80211_S_AUTH,-1)==0);
        d.iwn_auth_beacon_drain(); const auto old=d.authBeacon.request.serial;
        d.iwn_auth_beacon_cancel(true); assert(d.authBeacon.reopen());
        d.rxonReply(old); d.beacon(); d.iwn_auth_beacon_drain();
        assert(d.genericCalls==0 && !d.iwn_auth_beacon_pending());
        d.teardown(); ++cases;
    }
    for (unsigned fail=1;fail<=4;++fail) {
        ItlIwn d; active=&d;
        allocatedSources=addAttempts=allocationFailure=addFailure=0;
        if (fail<=2) allocationFailure=fail; else addFailure=fail-2;
        assert(!d.iwn_auth_beacon_init());
        d.iwn_auth_beacon_shutdown(); d.iwn_auth_beacon_shutdown();
        IOSimpleLockFree(d.authBeaconLock); d.authBeaconLock=nullptr;
        assert(liveSources==0 && heldLocks==0); ++cases;
    }
    for (unsigned order=0;order<2;++order) {
        ItlIwn d; d.setup(); d.automaticPreparationReceipt=false;
        assert(d.iwn_auth_beacon_enqueue(IEEE80211_S_AUTH,-1)==0);
        d.iwn_auth_beacon_drain(); d.rxonReply(); d.beacon();
        d.iwn_auth_beacon_drain(); assert(d.genericCalls==0);
        d.preparationReply(order); d.iwn_auth_beacon_drain(); assert(d.genericCalls==0);
        d.preparationReply(order); d.iwn_auth_beacon_drain(); assert(d.genericCalls==0);
        d.preparationReply(1-order); d.iwn_auth_beacon_drain(); assert(d.genericCalls==1);
        d.teardown(); ++cases;
    }
    for (unsigned variant=0;variant<3;++variant) {
        ItlIwn d; d.setup(); d.automaticPreparationReceipt=false;
        assert(d.iwn_auth_beacon_enqueue(IEEE80211_S_AUTH,-1)==0);
        d.iwn_auth_beacon_drain(); d.rxonReply(); d.beacon();
        if (variant==0) d.preparationReply(0,2); // Firmware table failure.
        if (variant==1) d.preparationReply(0,1,4); // Missing status payload.
        if (variant==2) fakeNow=d.authBeacon.request.deadline; // Missing receipts.
        d.iwn_auth_beacon_drain();
        assert(d.genericCalls==0 && d.scans==1);
        d.teardown(); ++cases;
    }
    {
        ItlIwn d; d.setup();
        assert(d.iwn_auth_beacon_enqueue(IEEE80211_S_AUTH,-1)==0);
        const auto first=d.authBeacon.request;
        fakeNow+=1000;
        assert(d.iwn_auth_beacon_enqueue(IEEE80211_S_AUTH,-1)==0);
        assert(d.authBeacon.request.serial==first.serial && d.authBeacon.request.deadline==first.deadline);
        d.iwn_auth_beacon_drain();
        fakeNow+=1000;
        assert(d.iwn_auth_beacon_enqueue(IEEE80211_S_AUTH,-1)==0);
        d.iwn_auth_beacon_drain();
        assert(d.commands==3 && d.authBeacon.request.deadline==first.deadline);
        d.teardown(); ++cases;
    }
    {
        ItlIwn d; d.setup();
        assert(d.iwn_auth_beacon_enqueue(IEEE80211_S_AUTH,0)==0);
        d.iwn_auth_beacon_drain(); d.rxonReply();
        d.iwn_auth_beacon_drain(); assert(d.genericCalls==1); // No beacon needed.
        d.teardown(); ++cases;
    }
    {
        ItlIwn d; d.setup();
        assert(d.iwn_auth_beacon_enqueue(IEEE80211_S_AUTH,-1)==0);
        d.iwn_auth_beacon_drain();
        const auto before=d.iwn_auth_beacon_rx_owner(); assert(before==0);
        d.rxonReply();
        ieee80211_frame frame={}; frame.i_fc[0]=0x80;
        memcpy(frame.i_addr2,d.node.ni_bssid,6); memcpy(frame.i_addr3,d.node.ni_bssid,6);
        d.iwn_auth_beacon_note_rx(&frame,36,13,before);
        d.iwn_auth_beacon_drain(); assert(d.genericCalls==0);
        d.beacon(); d.iwn_auth_beacon_drain(); assert(d.genericCalls==1);
        d.teardown(); ++cases;
    }
    {
        ItlIwn d; d.setup(); d.authBeacon.nextSerial=UINT64_MAX;
        assert(d.iwn_auth_beacon_enqueue(IEEE80211_S_AUTH,-1)==ECANCELED);
        d.authBeacon.hardwareGeneration=UINT64_MAX;
        assert(!d.authBeacon.reopen() && !d.authBeacon.open);
        d.teardown(); ++cases;
    }
    {
        ItlIwn d; d.setup();
        assert(d.iwn_auth_beacon_enqueue(IEEE80211_S_AUTH,-1)==0);
        d.iwn_auth_beacon_drain();
        ++d.com.sc_ic.ic_pae_assoc_epoch; fakeNow+=1000;
        assert(d.iwn_auth_beacon_enqueue(IEEE80211_S_AUTH,-1)==0);
        ItlIwn::iwn_auth_beacon_timeout(&d,d.authBeaconTimer);
        assert(d.genericCalls==0 && d.scans==0 && d.iwn_auth_beacon_pending());
        d.teardown(); ++cases;
    }
    std::printf("PASS: %u real AUTH beacon owner/helper cases; hardware/IOKit/generic commit are doubles\n",cases);
}
