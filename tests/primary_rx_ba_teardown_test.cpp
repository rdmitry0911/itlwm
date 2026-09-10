/* Complete RX ingress, hardware worker, mailbox, main completion, physical
 * retirement, reset and RUN-stop bodies. Command transport, other MAC/PHY/TX
 * teardown and real timer/packet storage are explicit fixture boundaries.
 * The 581-case station suite separately executes the actual BA wire helpers. */
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <thread>
#include <type_traits>
#include <vector>
#include <HAL/ItlStationRxBa.hpp>
#include "defines.inc"

#define nitems(a) (sizeof(a)/sizeof((a)[0]))
#define isset(a,b) ((a)[(b)/8] & (1U<<((b)%8)))
#define XYLog(...) ((void)0)
#define DEVNAME(sc) "fixture"
#define splassert(x) ((void)0)
#define IPL_NET 0
#define IEEE80211_ADDR_LEN 6
#define container_of(p,t,m) static_cast<t *>((p)->testOwner)
enum { IEEE80211_M_STA, IEEE80211_M_MONITOR, IEEE80211_NODE_HT=1,
       IEEE80211_S_RUN=4, IEEE80211_BA_REQUESTED=1, IEEE80211_BA_AGREED=2 };
using Lease=ItlFirmwareContextLease;
using Runtime=ItlStationRxBa;
static bool mainGate;
static std::vector<unsigned> locks;
struct IOSimpleLock { unsigned rank; };
using IOInterruptState=unsigned;
static IOSimpleLock ownerLeaf{1}, scanLeaf{2};
static IOInterruptState IOSimpleLockLockDisableInterrupt(IOSimpleLock *lock)
{ assert(lock && (locks.empty() || locks.back()<lock->rank)); auto depth=locks.size(); locks.push_back(lock->rank); return depth; }
static void IOSimpleLockUnlockEnableInterrupt(IOSimpleLock *lock,IOInterruptState depth)
{ assert(!locks.empty() && locks.back()==lock->rank); locks.pop_back(); assert(locks.size()==depth); }
static int splnet() { return 1; }
static void splx(int) {}
struct IOWorkLoop { bool inGate() const { return mainGate; } };
struct IOInterruptEventSource {
    unsigned references=1, signals=0;
    void retain() { ++references; }
    void release() { assert(locks.empty() && references>1); --references; }
    void interruptOccurred(void *,void *,int) { assert(locks.empty()); ++signals; }
};
struct ieee80211_rx_ba {
    int ba_timeout_val=32000, ba_state=IEEE80211_BA_REQUESTED;
    uint16_t ba_winstart=0x345, ba_winsize=64;
    uint8_t ba_token=7;
};
struct ieee80211_node {
    unsigned ni_flags=0;
    void *ni_chan=nullptr;
    ieee80211_rx_ba ni_rx_ba[16];
};
struct iwm_node { ieee80211_node in_ni; unsigned in_id=1,in_color=2; uint8_t in_macaddr[6]={2,3,4,5,6,7}; };
struct iwx_node { ieee80211_node in_ni; unsigned in_id=1,in_color=2; uint8_t in_macaddr[6]={2,3,4,5,6,7}; };
struct ieee80211com {
    ieee80211_node *ic_bss=nullptr;
    int ic_opmode=IEEE80211_M_STA, ic_state=IEEE80211_S_RUN;
    struct { void *if_softc=nullptr; } ic_if;
    IOSimpleLock *ic_pae_selected_bss_lock=&ownerLeaf;
    ItlStateTransitionIdentity identity{11,11,12};
};
#define IC2IFP(ic) (&(ic)->ic_if)
struct ItlScanCommandPolicy {
    static ItlStateTransitionIdentity identityLocked(ieee80211com *ic)
    { assert(!locks.empty() && locks.front()==1); return ic->identity; }
};
struct Reorder { unsigned ssn=0,window=0,reorder_timer=1; };
struct RxBa {
    uint8_t sta_id=0,tid=0,baid=IWM_RX_REORDER_DATA_INVALID_BAID;
    unsigned timeout=0,last_rx=0,session_timer=1,clears=0;
    Reorder reorder_buf;
};
struct iwm_rxba_data : RxBa {};
struct iwx_rxba_data : RxBa {};
struct iwm_tx_ring { int cur=0; };
template<class Entry> struct Softc {
    void *testOwner=nullptr;
    ieee80211com sc_ic;
    bool sc_mqrx_supported=true,taskAdmission=true;
    unsigned taskActive=0;
    uint32_t sc_flags=0;
    int sc_generation=7,sc_rx_ba_sessions=0;
    Entry sc_rxba_data[IWM_MAX_BAID];
    uint32_t agg_queue_mask=0;
    struct { unsigned start_tidmask=0,stop_tidmask=0; } ba_tx,ba_rx;
    uint8_t sc_enabled_capa[128]={};
    int sc_phyctxt[2]={},ba_task=1,init_task=2;
    iwm_tx_ring txq[32];
    int failTid=-1,failFlush=0,commandError=ETIMEDOUT;
    bool ambiguous=true;
    unsigned commandCount=0,flushCount=0,laterCleanup=0,tasks=0,recoveries=0;
    unsigned lastTid=0,lastSsn=0,lastWindow=0;
    bool lastStart=false;
    uint8_t nextBaid=10;
};
struct iwm_softc : Softc<iwm_rxba_data> {};
struct iwx_softc : Softc<iwx_rxba_data> {};
static int systq;
static unsigned accepted,refused,timers;
static std::function<void()> commandHook,callbackHook;
static void getmicrouptime(unsigned *stamp) { assert(mainGate && locks.empty()); *stamp=1; }
static void timeout_add_usec(unsigned *timer,int value) {
    assert(mainGate && locks.empty());
    // CTimeout::timeout_add_msec rejects a pointer nulled by timeout_free.
    assert(*timer!=0 && "RX restart did not recreate its retired timer");
    *timer=value; ++timers;
}
static bool timeout_initialized(unsigned *timer) { return *timer!=0; }
static void timeout_set(unsigned *timer,void (*)(void *),void *) {
    assert(mainGate && locks.empty()); *timer=1;
}
static void ieee80211_addba_req_accept(ieee80211com *,ieee80211_node *n,uint8_t tid)
{
    assert(mainGate && locks.empty()); ++accepted;
    n->ni_rx_ba[tid].ba_state=IEEE80211_BA_AGREED;
    auto hook=std::move(callbackHook); callbackHook={}; if(hook) hook();
}
static void ieee80211_addba_req_refuse(ieee80211com *,ieee80211_node *,uint8_t)
{ assert(mainGate && locks.empty()); ++refused; }
static void ieee80211_ba_del(ieee80211_node *) { assert(mainGate); }
struct DriverState {
    IOWorkLoop loop;
    IOInterruptEventSource event;
    IOInterruptEventSource *stateTransitionSource=&event;
    IOSimpleLock *wclScanLock=&scanLeaf;
    Runtime primaryRxBa{};
    Lease primaryStationContext{};
    ItlFirmwareStationUses primaryStationUses{};
    struct { bool open=true; } scanCommand;
    unsigned resumes=0;
    IOWorkLoop *getMainWorkLoop() { return &loop; }
    void resumePrimaryStationUsers() { assert(locks.empty()); ++resumes; }
};
template<class Driver,class Sc> static int baCommand(Driver *d,Sc *sc,const ItlFirmwareContextReceipt *use,
                                                    uint8_t tid,uint16_t ssn,uint16_t window,bool start,uint8_t *baid)
{
    assert(!mainGate && locks.empty());
    if(use) assert(use->serial==d->primaryStationUses.owner.serial && d->primaryStationUses.active>0);
    else assert(d->primaryStationUses.active==0);
    ++sc->commandCount; sc->lastTid=tid; sc->lastStart=start;
    sc->lastSsn=ssn; sc->lastWindow=window;
    const int generation=sc->sc_generation;
    auto hook=std::move(commandHook); commandHook={}; if(hook) hook();
    if(generation!=sc->sc_generation) return ENXIO;
    if(tid==sc->failTid) {
        d->primaryStationContext.uncertain=sc->ambiguous;
        return sc->commandError;
    }
    if(start) *baid=sc->sc_mqrx_supported?sc->nextBaid:IWM_RX_REORDER_DATA_INVALID_BAID;
    return 0;
}
template<class Sc,class Entry> static void clearEntry(Sc *,Entry *entry)
{
    assert(mainGate && locks.empty());
    assert(entry->sta_id==IWM_STATION_ID && "primary cleanup touched an AP client");
    assert(entry->baid!=IWM_RX_REORDER_DATA_INVALID_BAID && "duplicate host BA retirement");
    ++entry->clears; entry->baid=IWM_RX_REORDER_DATA_INVALID_BAID;
    entry->session_timer=entry->reorder_buf.reorder_timer=0;
}
#define RX_DECLS(prefix,soft) \
    static void prefix##_rx_ba_session_expired(void *) {} \
    static void prefix##_reorder_timer_expired(void *) {} \
    int queuePrimaryRxBa(ieee80211_node *,uint8_t,bool); \
    void runPrimaryRxBa(); \
    void postPrimaryRxBa(const ItlStationRxBaRequest &,ItlFirmwareContextReceipt *,int,uint8_t,bool); \
    void drainPrimaryRxBa(IOInterruptEventSource *); \
    int retirePrimaryRxBa(); \
    void resetPrimaryRxBaLocked(); \
    bool releasePrimaryStationReader(ItlFirmwareContextReceipt *); \
    static int prefix##_ampdu_rx_start(ieee80211com *,ieee80211_node *,uint8_t); \
    static void prefix##_ampdu_rx_stop(ieee80211com *,ieee80211_node *,uint8_t); \
    void prefix##_add_task(soft *sc,int,int *task) { assert(locks.empty()); if(task==&sc->init_task) ++sc->recoveries; else ++sc->tasks; }
class ItlIwm : public DriverState {
public:
    iwm_softc com;
    RX_DECLS(iwm,iwm_softc);
    int iwm_run_stop(iwm_softc *);
    int iwm_sta_rx_ba_cmd(iwm_softc *sc,const ItlFirmwareContextReceipt *use,uint8_t tid,uint16_t ssn,uint16_t window,bool start,uint8_t *baid)
    { return baCommand(this,sc,use,tid,ssn,window,start,baid); }
    void iwm_clear_reorder_buffer(iwm_softc *sc,iwm_rxba_data *e) { clearEntry(sc,e); }
    void iwm_init_reorder_buffer(Reorder *r,unsigned ssn,unsigned window) { assert(mainGate); r->ssn=ssn; r->window=window; }
    int iwm_sta_tx_agg(iwm_softc *,ieee80211_node *,uint8_t,uint8_t,uint16_t,int) { assert(false); return EIO; }
    void iwm_ampdu_txq_advance(iwm_softc *,iwm_tx_ring *,int) { assert(false); }
    void iwm_clear_oactive(iwm_softc *,iwm_tx_ring *) { assert(false); }
    void iwm_led_blink_stop(iwm_softc *) {}
    int iwm_sf_config(iwm_softc *sc,int) { ++sc->laterCleanup; return 0; }
    void iwm_disable_beacon_filter(iwm_softc *) {}
    int iwm_update_quotas(iwm_softc *,iwm_node *,int) { return 0; }
    int iwm_mac_ctxt_cmd(iwm_softc *,iwm_node *,int,int) { return 0; }
    bool iwm_mimo_enabled(iwm_softc *) { return true; }
    int iwm_phy_ctxt_update(iwm_softc *,int *,void *,int,int,int) { return 0; }
};
class ItlIwx : public DriverState {
public:
    iwx_softc com;
    RX_DECLS(iwx,iwx_softc);
    bool iwx_task_gate_enter(iwx_softc *sc,bool) { if(!sc->taskAdmission)return false; ++sc->taskActive; return true; }
    void iwx_task_gate_leave(iwx_softc *sc) { assert(sc->taskActive); --sc->taskActive; }
    int iwx_run_stop(iwx_softc *);
    int iwx_sta_rx_ba_cmd(iwx_softc *sc,const ItlFirmwareContextReceipt *use,uint8_t tid,uint16_t ssn,uint16_t window,bool start,uint8_t *baid)
    { return baCommand(this,sc,use,tid,ssn,window,start,baid); }
    void iwx_clear_reorder_buffer(iwx_softc *sc,iwx_rxba_data *e) { clearEntry(sc,e); }
    void iwx_init_reorder_buffer(Reorder *r,unsigned ssn,unsigned window) { assert(mainGate); r->ssn=ssn; r->window=window; }
    int iwx_flush_sta(iwx_softc *sc,iwx_node *) { assert(!mainGate); ++sc->flushCount; return sc->failFlush; }
    int iwx_sf_config(iwx_softc *sc,int) { ++sc->laterCleanup; return 0; }
    int iwx_disable_beacon_filter(iwx_softc *) { return 0; }
    int iwx_update_quotas(iwx_softc *,iwx_node *,int) { return 0; }
    int iwx_mac_ctxt_cmd(iwx_softc *,iwx_node *,int,int) { return 0; }
    bool iwx_mimo_enabled(iwx_softc *) { return true; }
    int iwx_phy_ctxt_update(iwx_softc *,int *,void *,int,int,int) { return 0; }
};
#include "production.inc"

template<class Driver,class Node> static void prepare(Driver &d,Node &n,bool hardware=true)
{
    assert(locks.empty()); mainGate=false; accepted=refused=timers=0; commandHook={}; callbackHook={};
    d.com.sc_ic.ic_bss=&n.in_ni; d.com.sc_ic.ic_if.if_softc=&d.com;
    d.com.testOwner=&d;
    d.com.sc_mqrx_supported=hardware;
    auto &station=d.primaryStationContext.owner;
    station.serial=37; station.generation=d.com.sc_generation;
    station.identity.attempt=d.com.sc_ic.identity;
    station.identity.mac=IWM_FW_CMD_ID_AND_COLOR(n.in_id,n.in_color);
    station.identity.station=IWM_STATION_ID; station.identity.mode=IEEE80211_M_STA;
    std::memcpy(station.identity.peer,n.in_macaddr,6);
    d.primaryStationContext.stage=Lease::Stage::Active; d.primaryStationContext.confirmed=true;
    assert(d.primaryStationUses.start(station));
    for(unsigned id : {1U,30U}) {
        auto &ap=d.com.sc_rxba_data[id]; ap.sta_id=6; ap.tid=3; ap.baid=id;
        ++d.com.sc_rx_ba_sessions;
    }
}
template<class Driver> static int start(Driver &d,uint8_t tid=3)
{
    mainGate=true; int error;
    if constexpr(std::is_same<Driver,ItlIwm>::value)
        error=Driver::iwm_ampdu_rx_start(&d.com.sc_ic,d.com.sc_ic.ic_bss,tid);
    else error=Driver::iwx_ampdu_rx_start(&d.com.sc_ic,d.com.sc_ic.ic_bss,tid);
    mainGate=false; return error;
}
template<class Driver> static void stopTid(Driver &d,uint8_t tid=3)
{
    mainGate=true;
    if constexpr(std::is_same<Driver,ItlIwm>::value)
        Driver::iwm_ampdu_rx_stop(&d.com.sc_ic,d.com.sc_ic.ic_bss,tid);
    else Driver::iwx_ampdu_rx_stop(&d.com.sc_ic,d.com.sc_ic.ic_bss,tid);
    mainGate=false;
}
template<class Driver> static int stop(Driver &d)
{
    assert(!mainGate);
    if constexpr(std::is_same<Driver,ItlIwm>::value) return d.iwm_run_stop(&d.com);
    else return d.iwx_run_stop(&d.com);
}
template<class Driver> static void complete(Driver &d)
{
    assert(d.com.taskActive==0); // Pending host work must not keep hardware stop pinned.
    mainGate=true; d.drainPrimaryRxBa(&d.event); mainGate=false;
    assert(d.event.references==1 && d.primaryStationUses.active==0);
}
template<class Driver> static void reset(Driver &d)
{
    const auto irq=IOSimpleLockLockDisableInterrupt(d.wclScanLock);
    d.scanCommand.open=false;
    d.primaryStationUses.close();
    d.resetPrimaryRxBaLocked();
    ++d.com.sc_generation;
    d.primaryStationContext.clear();
    IOSimpleLockUnlockEnableInterrupt(d.wclScanLock,irq);
    d.com.sc_rx_ba_sessions=0;
    for(auto &entry:d.com.sc_rxba_data) {
        entry={}; entry.session_timer=entry.reorder_buf.reorder_timer=0;
    }
}
template<class Driver> static void assertAp(const Driver &d)
{
    for(unsigned id : {1U,30U}) {
        const auto &ap=d.com.sc_rxba_data[id];
        assert(ap.sta_id==6 && ap.baid==id && ap.clears==0);
    }
}
template<class Driver> static void cleanup(Driver &d)
{
    d.primaryStationUses.close();
    assert(stop(d)==EINPROGRESS);
    assert(d.primaryStationUses.active==1 && d.primaryRxBa.phase==Runtime::Phase::Ready);
    complete(d);
    assert(stop(d)==0);
    assert(!d.primaryRxBa.occupied() && d.com.sc_rx_ba_sessions==2);
    assertAp(d);
}
static unsigned cases;
template<class Driver,class Node> static void family()
{
    for(bool hardware : {false,true}) {
        if constexpr(std::is_same<Driver,ItlIwx>::value) if(!hardware) continue;
        for(int race=0;race<7;++race) {
            Driver d; Node n; prepare(d,n,hardware);
            assert(start(d)==EBUSY);
            assert(d.primaryRxBa.pending[1][3].serial!=0 && d.primaryStationUses.active==0);
            if(race==1) n.in_ni.ni_rx_ba[3].ba_winstart++;
            if(race==2) commandHook=[&] { ++d.com.sc_ic.identity.associationEpoch; ++n.in_macaddr[5]; };
            if(race==3) commandHook=[&] { reset(d); };
            if(race==4) { d.com.failTid=3; d.com.ambiguous=false; d.com.commandError=ENOSPC; }
            if(race==5) d.com.failTid=3;
            d.runPrimaryRxBa();
            assert(d.com.taskActive==0);
            if(race==3) {
                assert(d.primaryStationUses.active==0 && !d.primaryRxBa.occupied());
                ++cases; continue;
            }
            assert(d.primaryRxBa.phase==Runtime::Phase::Ready && d.primaryStationUses.active==1);
            assert(accepted==0 && refused==0 && timers==0);
            assert(d.com.lastSsn==0x345 && d.com.lastWindow==64);
            if(race==6) { reset(d); complete(d); assert(accepted==0 && !d.primaryRxBa.occupied()); ++cases; continue; }
            assert(d.retirePrimaryRxBa()==EINPROGRESS);
            complete(d);
            assert(accepted==(race==0?1U:0U) && refused==((race==4||race==5)?1U:0U));
            if(race==4) { assert(!d.primaryRxBa.occupied() && d.com.sc_rx_ba_sessions==2); }
            else if(race==5) { assert(d.primaryRxBa.resource[3].uncertain && d.com.recoveries==1); reset(d); }
            else {
                assert(d.primaryRxBa.resource[3].occupied && d.primaryRxBa.resource[3].hostPublished);
                assert(d.com.sc_rx_ba_sessions==3); cleanup(d);
            }
            ++cases;
        }
        {
            Driver d; Node n; prepare(d,n,hardware);
            assert(start(d)==EBUSY); d.runPrimaryRxBa(); complete(d);
            stopTid(d); n.in_ni.ni_rx_ba[3]={}; n.in_ni.ni_rx_ba[3].ba_token=8;
            assert(start(d)==EBUSY);
            d.runPrimaryRxBa(); assert(!d.com.lastStart); complete(d);
            d.runPrimaryRxBa(); assert(d.com.lastStart); complete(d);
            assert(accepted==2 && d.com.sc_rx_ba_sessions==3);
            if(hardware) assert(d.com.sc_rxba_data[d.com.nextBaid].reorder_buf.reorder_timer!=0);
            cleanup(d); ++cases;
        }
        {
            Driver d; Node n; prepare(d,n,hardware);
            for(uint8_t tid : {uint8_t(3),uint8_t(5)}) {
                d.com.nextBaid=tid+7; assert(start(d,tid)==EBUSY); d.runPrimaryRxBa(); complete(d);
            }
            d.primaryStationUses.close(); d.com.failTid=5; d.com.ambiguous=false;
            assert(stop(d)==EINPROGRESS);
            const unsigned commands=d.com.commandCount;
            assert(d.primaryRxBa.resource[3].firmwareRemoved && !d.primaryRxBa.resource[5].firmwareRemoved);
            assert(d.com.sc_rx_ba_sessions==4);
            complete(d);
            assert(d.com.sc_rx_ba_sessions==3 && d.primaryRxBa.resource[5].occupied);
            assert(stop(d)==ETIMEDOUT && d.com.commandCount==commands);
            d.com.failTid=-1; cleanup(d);
            const auto count=d.com.commandCount; assert(stop(d)==0 && d.com.commandCount==count);
            ++cases;
        }
        {
            Driver d; Node n; prepare(d,n,hardware);
            assert(start(d)==EBUSY); d.runPrimaryRxBa();
            callbackHook=[&] { reset(d); auto next=d.primaryStationUses.owner; ++next.serial; assert(!d.primaryStationUses.start(next)); };
            complete(d); assert(!d.primaryRxBa.occupied() && d.primaryStationUses.active==0);
            ++cases;
        }
        {
            Driver d; Node n; prepare(d,n,hardware);
            stopTid(d); d.runPrimaryRxBa(); complete(d);
            assert(d.com.commandCount==0 && d.com.sc_rx_ba_sessions==2);
            assert(start(d,8)==EINVAL); assertAp(d);
            ++cases;
        }
        for (unsigned race=0;race<7;++race) {
            Driver d; Node n; prepare(d,n,hardware);
            assert(start(d)==EBUSY);
            if(race==0) ++d.com.sc_ic.identity.associationEpoch;
            if(race==1) ++d.com.sc_generation;
            if(race==2) ++d.primaryStationUses.owner.serial;
            if(race==3) d.primaryStationUses.close();
            if(race==4) d.scanCommand.open=false;
            if(race==5) d.stateTransitionSource=nullptr;
            if(race==6) d.com.sc_flags|=IWM_FLAG_SHUTDOWN;
            d.runPrimaryRxBa();
            assert(!d.com.commandCount && !d.com.taskActive && !d.primaryStationUses.active);
            assert(d.primaryRxBa.phase==Runtime::Phase::Idle && !d.primaryRxBa.occupied());
            assert(!accepted && !refused && !d.event.signals);
            assertAp(d); reset(d); ++cases;
        }
        for (unsigned race=0;race<3;++race) {
            Driver d; Node n; prepare(d,n,hardware);
            assert(start(d)==EBUSY);
            commandHook=[&] {
                if(race==0) d.scanCommand.open=false;
                if(race==1) d.stateTransitionSource=nullptr;
                if(race==2) d.com.sc_flags|=IWM_FLAG_SHUTDOWN;
            };
            d.runPrimaryRxBa();
            assert(d.com.commandCount==1 && !d.com.taskActive && !d.primaryStationUses.active);
            assert(d.primaryRxBa.phase==Runtime::Phase::Idle);
            assert(d.primaryRxBa.resource[3].occupied && !d.primaryRxBa.resource[3].hostPublished);
            assert(d.com.sc_rx_ba_sessions==3 && !d.event.signals);
            assertAp(d); reset(d); assert(!d.primaryRxBa.occupied()); ++cases;
        }
        {
            Driver d; Node n; prepare(d,n,hardware);
            assert(start(d)==EBUSY);
            commandHook=[&] {
                stopTid(d);
                n.in_ni.ni_rx_ba[3].ba_token=9;
                n.in_ni.ni_rx_ba[3].ba_winstart=0x456;
                assert(start(d)==EBUSY);
            };
            d.runPrimaryRxBa();
            d.runPrimaryRxBa(); // A duplicate task cannot consume a pending completion.
            assert(d.com.commandCount==1 && d.primaryStationUses.active==1);
            complete(d);
            assert(!accepted && d.primaryRxBa.resource[3].hostPublished);
            d.runPrimaryRxBa(); assert(!d.com.lastStart); complete(d);
            d.runPrimaryRxBa(); assert(d.com.lastStart && d.com.lastSsn==0x456); complete(d);
            assert(accepted==1 && d.com.commandCount==3);
            cleanup(d); ++cases;
        }
        {
            Driver d; Node n; prepare(d,n,hardware);
            assert(start(d)==EBUSY); d.runPrimaryRxBa(); complete(d);
            stopTid(d); d.com.failTid=3;
            d.runPrimaryRxBa(); complete(d);
            assert(d.primaryRxBa.resource[3].occupied && d.primaryRxBa.resource[3].uncertain);
            assert(!d.primaryRxBa.resource[3].firmwareRemoved && d.com.sc_rx_ba_sessions==3);
            assert(d.com.recoveries==1 && accepted==1);
            assertAp(d); reset(d); ++cases;
        }
        for(bool lifecycle : {false,true}) {
            Driver d; Node n; prepare(d,n,hardware);
            (lifecycle?d.primaryRxBa.lifecycle:d.primaryRxBa.nextSerial)=UINT64_MAX;
            assert(start(d)==ECANCELED);
            assert(!d.com.tasks && !d.primaryStationUses.active);
            ++cases;
        }
        if(hardware) {
            Driver d; Node n; prepare(d,n,hardware);
            d.com.nextBaid=1; // Firmware must not let a primary allocation overwrite AP ownership.
            assert(start(d)==EBUSY); d.runPrimaryRxBa(); complete(d);
            assert(!accepted && refused==1 && d.com.recoveries==1);
            assert(d.primaryRxBa.resource[3].occupied && !d.primaryRxBa.resource[3].hostPublished);
            assertAp(d); reset(d); ++cases;
            Driver restarted; Node peer; prepare(restarted,peer,hardware);
            assert(start(restarted)==EBUSY); restarted.runPrimaryRxBa(); complete(restarted);
            reset(restarted);
            assert(!restarted.com.sc_rxba_data[10].session_timer);
            prepare(restarted,peer,hardware); restarted.scanCommand.open=true;
            peer.in_ni.ni_rx_ba[3]={};
            assert(start(restarted)==EBUSY); restarted.runPrimaryRxBa(); complete(restarted);
            assert(accepted==1 && restarted.com.sc_rxba_data[10].session_timer!=0);
            assert(restarted.com.sc_rxba_data[10].reorder_buf.reorder_timer!=0);
            cleanup(restarted); ++cases;
        }
    }
    if constexpr(std::is_same<Driver,ItlIwx>::value) {
        Driver d; Node n; prepare(d,n); d.com.failFlush=EIO;
        assert(stop(d)==EIO && d.com.commandCount==0); ++cases;
        Driver blocked; Node other; prepare(blocked,other);
        assert(start(blocked)==EBUSY); blocked.com.taskAdmission=false;
        blocked.runPrimaryRxBa();
        assert(!blocked.com.commandCount && !blocked.primaryStationUses.active);
        assert(blocked.primaryRxBa.pending[1][3].serial!=0);
        blocked.com.taskAdmission=true; blocked.runPrimaryRxBa(); complete(blocked);
        assert(accepted==1); cleanup(blocked); ++cases;
    }
}
int main(int argc,char **argv)
{
    // Concurrent AP/primary accounting, independent of the hardware fixture.
    int sessions=2;
    auto account=[&] {
        for(unsigned i=0;i<50000;++i) {
            ItlRxBaSessionCount::add(&sessions);
            ItlRxBaSessionCount::drop(&sessions);
        }
    };
    std::thread first(account),second(account),third(account);
    first.join(); second.join(); third.join();
    assert(ItlRxBaSessionCount::load(&sessions)==2);
    ItlRxBaSessionCount::reset(&sessions);
    ItlRxBaSessionCount::drop(&sessions);
    assert(ItlRxBaSessionCount::load(&sessions)==0);
    const char *name=argc>1?argv[1]:"all";
    if(std::strcmp(name,"all")==0 || std::strcmp(name,"iwm")==0) family<ItlIwm,iwm_node>();
    if(std::strcmp(name,"all")==0 || std::strcmp(name,"iwx")==0) family<ItlIwx,iwx_node>();
    assert(cases>0 && locks.empty());
    std::printf("actual immutable RX BA lifecycle and teardown: %u scenarios PASS\n",cases);
}
