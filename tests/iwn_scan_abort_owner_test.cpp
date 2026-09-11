// Complete physical abort reservation, command sender and doorbell hooks.
// DMA allocation/register writes/transport wake are explicit kernel boundaries.
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <functional>
#include <utility>
#include "tests/kernel_memory_test_support.hpp"
using u_int8_t=uint8_t;
using u_int32_t=uint32_t;
using u_int64_t=uint64_t;
using bus_addr_t=uint64_t;
using mbuf_t=void *;
struct IOSimpleLock { bool held=false; unsigned rank=2; };
static IOSimpleLock *lockStack[2];
static unsigned held;
using IOInterruptState=int;
static void IOSimpleLockLock(IOSimpleLock *l) {
    assert(l && !l->held && held<2);
    assert(!held || lockStack[held-1]->rank<l->rank);
    lockStack[held++]=l; l->held=true;
}
static void IOSimpleLockUnlock(IOSimpleLock *l) {
    assert(l && l->held && held && lockStack[held-1]==l); --held; l->held=false;
}
static int IOSimpleLockLockDisableInterrupt(IOSimpleLock *l) { IOSimpleLockLock(l); return 0; }
static void IOSimpleLockUnlockEnableInterrupt(IOSimpleLock *l,int) { IOSimpleLockUnlock(l); }
#include "types.inc"
struct IOPhysicalSegment { uint64_t location; };
struct Cursor { unsigned getPhysicalSegmentsWithCoalesce(mbuf_t,IOPhysicalSegment *,int){return 0;} };
struct Map { unsigned dm_nsegs; Cursor *cursor; };
struct iwn_tx_cmd { uint8_t code,flags,qid,idx,data[128]; };
struct iwn_tx_desc { int nsegs; struct {uint32_t addr; uint16_t len;} segs[1]; };
struct iwn_tx_data { Map *map; mbuf_t m; uint64_t cmd_paddr; uint64_t auth_rxon_serial; };
struct iwn_node_info { uint8_t id; };
struct iwn_cmd_link_quality { uint8_t id; };
constexpr unsigned IWN_TX_RING_COUNT=8;
struct iwn_tx_ring {
    iwn_tx_cmd cmd[IWN_TX_RING_COUNT]{};
    iwn_tx_desc desc[IWN_TX_RING_COUNT]{};
    iwn_tx_data data[IWN_TX_RING_COUNT]{};
    int cur=0,qid=4;
};
struct iwn_softc;
struct iwn_ops { void (*update_sched)(iwn_softc *,int,int,int,int); };
struct task {};
struct ieee80211_wcl_scan_started { uint64_t generation; uint32_t backend_generation; };
constexpr int IEEE80211_EVT_WCL_SCAN_STARTED=12;
struct ieee80211com {
    void *ic_softc=nullptr;
    IOSimpleLock *ic_pae_selected_bss_lock=nullptr;
    uint64_t ic_wcl_reassoc_owner_serial=31,ic_wcl_reassoc_source_epoch=7,ic_pae_assoc_epoch=7;
    unsigned ic_wcl_reassoc_owner_active=1;
    void (*ic_event_handler)(ieee80211com *,int,void *)=nullptr;
};
struct iwn_softc {
    ieee80211com sc_ic;
    void *driver=nullptr;
    IOSimpleLock lock;
    IOSimpleLock ownerLock{false,1};
    IOSimpleLock *sc_scan_lease_lock=&lock;
    iwn_scan_lease sc_scan_lease{};
    iwn_wcl_initial_scan_pending sc_wcl_initial_scan_pending{};
    iwn_ops ops{};
    iwn_tx_ring txq[1];
    unsigned command_queue=0,sc_flags=0;
    uint8_t broadcast_id=15;
    task init_task;
};
constexpr unsigned IWN_FLAG_BGSCAN=1,IWN_FLAG_FATAL_RECOVERY=2,IWN_FLAG_SCANNING=4;
constexpr int IWN_CMD_SCAN_ABORT=0x81,IWN_CMD_SCAN=0x80,IWN_HBUS_TARG_WRPTR=1;
constexpr int IWN_CMD_ADD_NODE=24,IWN_CMD_LINK_QUALITY=78;
constexpr int MCLBYTES=2048,MBUF_WAITOK=0,PCATCH=0;
#define SEC_TO_NSEC(x) (uint64_t(x)*1000000000ULL)
#define XYLog(...) do {} while(0)
#define IWN_LOADDR(x) uint32_t(x)
#define IWN_HIADDR(x) uint16_t((x)>>32)
#define mtod(m,type) static_cast<type>(m)
#define DEVNAME(sc) "fixture"
#define container_of(sc,type,member) static_cast<type *>((sc)->driver)
#ifndef htole32
#define htole32(x) (x)
#define htole16(x) (x)
#endif
static void mbuf_allocpacket(int,int,unsigned *,mbuf_t *out){*out=nullptr;}
static void mbuf_setlen(mbuf_t,int){}
static void mbuf_pkthdr_setlen(mbuf_t,int){}
static void mbuf_freem(mbuf_t){assert(false);}
static int tsleep_nsec(void *,int,const char *,uint64_t){assert(false);return 0;}
static unsigned writes,clears,resets;
static int wakeError;
static std::function<void(iwn_softc *)> onWake;
static void *systq;
static void task_add(void *,task *){++resets;}
static void write_register(iwn_softc *sc,int reg,int value) {
    assert(reg==IWN_HBUS_TARG_WRPTR && value==(sc->txq[0].qid<<8 | sc->txq[0].cur));
#ifndef IWN_ABORT_BASELINE
    assert(sc->lock.held);
    const auto index=(sc->txq[0].cur+IWN_TX_RING_COUNT-1)%IWN_TX_RING_COUNT;
    if(sc->txq[0].cmd[index].code==IWN_CMD_SCAN_ABORT) assert(sc->sc_scan_lease.abort_submitted);
    else {
        assert(sc->sc_scan_lease.command_submitted);
        if(sc->sc_scan_lease.reassoc_serial) assert(sc->ownerLock.held);
    }
#endif
    ++writes;
}
#define IWN_WRITE(sc,reg,value) write_register(sc,reg,value)
class ItlIwn {
public:
    iwn_softc com;
    ItlIwn() {
        com.driver=this; com.sc_ic.ic_softc=&com;
        com.sc_ic.ic_pae_selected_bss_lock=&com.ownerLock;
        com.ops.update_sched=[](iwn_softc *,int,int,int,int){};
    }
    int iwn_set_cmd_in_flight(iwn_softc *sc) {
        assert(!sc->lock.held);
        auto action=std::move(onWake); onWake={}; if(action) action(sc);
        return wakeError;
    }
    void iwn_clear_cmd_in_flight(iwn_softc *sc){assert(!sc->lock.held);++clears;}
    int iwn_cmd(iwn_softc *,int,const void *,int,int);
    int iwn_auth_preparation_cmd(iwn_softc *sc,int code,const void *buf,int size,int async) {
        return iwn_cmd_with_doorbell_hook(sc,code,buf,size,async,nullptr,nullptr,nullptr);
    }
    int iwn_cmd_with_doorbell_hook(iwn_softc *,int,const void *,int,int,
        bool (*)(iwn_softc *,void *),void (*)(iwn_softc *,void *),void *);
    int iwn_scan_abort_command(iwn_softc *,uint64_t);
    void iwn_scan_abort(iwn_softc *);
    static int iwn_wnm_bgscan_abort(ieee80211com *,uint64_t=0);
};
static bool iwn_scan_lease_mark_abort(iwn_softc *,iwn_scan_lease_owner,uint64_t,
    uint64_t *,bool *,uint64_t=0);
static bool iwn_scan_lease_initial_handoff_valid_locked(iwn_softc *) { return true; }
#include "owner.inc"
#include "sender.inc"
static void active(iwn_softc *sc,uint64_t physical,uint64_t roam) {
    sc->sc_scan_lease={};
    sc->sc_scan_lease.serial=physical;
    sc->sc_scan_lease.reassoc_serial=roam;
    sc->sc_scan_lease.owner=IWN_SCAN_LEASE_GENERIC_BACKGROUND;
    sc->sc_scan_lease.phase=IWN_SCAN_LEASE_ACTIVE;
    sc->sc_scan_lease.command_submitted=true;
    sc->sc_flags=IWN_FLAG_BGSCAN;
}
int main() {
    unsigned cases=0;
    for(unsigned scenario=0;scenario<9;++scenario) {
        writes=clears=resets=0; wakeError=0; onWake={};
        ItlIwn d; auto *sc=&d.com;
        active(sc,UINT64_C(0x100000001),UINT64_C(0x100000031));
        if(scenario==1 || scenario==6) onWake=[](iwn_softc *value){
            active(value,UINT64_C(0x100000002),UINT64_C(0x100000032));};
        if(scenario==2) onWake=[](iwn_softc *value){value->sc_scan_lease.hardware_invalidated=true;};
        if(scenario==3) onWake=[](iwn_softc *value){
            value->sc_scan_lease.terminal_claimed=true;
            value->sc_scan_lease.phase=IWN_SCAN_LEASE_DRAINING;};
        if(scenario==4) onWake=[](iwn_softc *value){value->sc_scan_lease={};};
        if(scenario==5 || scenario==6) wakeError=EIO;
        if(scenario==7) sc->sc_scan_lease.abort_requested=true;
        if(scenario==8) { sc->sc_scan_lease.phase=IWN_SCAN_LEASE_ARMING;
            sc->sc_scan_lease.command_submitted=false; }
        d.iwn_scan_abort(sc);
        assert(!sc->lock.held);
        assert(writes==(scenario==0?1U:0U));
        assert(resets==(scenario==5?1U:0U));
        if(scenario==1 || scenario==6) {
            assert(sc->sc_scan_lease.serial==UINT64_C(0x100000002));
            assert(!sc->sc_scan_lease.abort_requested && !sc->sc_scan_lease.abort_submitted);
        }
        if(scenario==0) {
            d.iwn_scan_abort(sc); assert(writes==1 && resets==0);
#ifndef IWN_ABORT_BASELINE
            iwn_scan_lease_abort_submission_failed(sc,sc->sc_scan_lease.serial);
            assert(sc->sc_scan_lease.abort_requested && sc->sc_scan_lease.abort_submitted);
#endif
        }
        ++cases;
    }
    for(unsigned scenario=0;scenario<9;++scenario) {
        ItlIwn d; auto *sc=&d.com; writes=clears=resets=0; wakeError=0; onWake={};
        active(sc,5,31);
        sc->sc_scan_lease.phase=IWN_SCAN_LEASE_ARMING;
        sc->sc_scan_lease.command_submitted=false;
        iwn_scan_doorbell_context context{};
        context.serial=5; context.reassoc_serial=31; context.background=true;
        if(scenario==1) onWake=[](iwn_softc *v){++v->sc_ic.ic_wcl_reassoc_owner_serial;};
        if(scenario==2) onWake=[](iwn_softc *v){v->sc_ic.ic_wcl_reassoc_owner_active=0;};
        if(scenario==3) onWake=[](iwn_softc *v){++v->sc_ic.ic_pae_assoc_epoch;};
        if(scenario==4) onWake=[](iwn_softc *v){v->sc_scan_lease.hardware_invalidated=true;};
        if(scenario==5) onWake=[](iwn_softc *v){++v->sc_scan_lease.serial;};
        if(scenario==6) context.reassoc_serial=0;
        if(scenario==7) sc->sc_ic.ic_pae_selected_bss_lock=nullptr;
        if(scenario==8) {
            context.reassoc_serial=0; sc->sc_scan_lease.reassoc_serial=0;
            sc->sc_ic.ic_pae_selected_bss_lock=nullptr;
        }
        const auto result=d.iwn_cmd_with_doorbell_hook(sc,IWN_CMD_SCAN,nullptr,0,1,
            iwn_scan_lease_prepare_doorbell,iwn_scan_lease_finish_doorbell,&context);
        assert(result==((scenario==0 || scenario==8)?0:ECANCELED));
        assert(writes==((scenario==0 || scenario==8)?1U:0U));
        assert(held==0 && !sc->lock.held && !sc->ownerLock.held);
        ++cases;
    }
    {
        writes=clears=resets=0; wakeError=0;
        ItlIwn d; auto *sc=&d.com;
        active(sc,5,UINT64_C(0x100000032));
        assert(d.iwn_wnm_bgscan_abort(&sc->sc_ic,UINT64_C(0x100000031))==EBUSY);
        assert(!sc->sc_scan_lease.abort_requested && writes==0);
        assert(d.iwn_wnm_bgscan_abort(&sc->sc_ic,UINT64_C(0x100000032))==0);
        assert(writes==1 && sc->sc_scan_lease.abort_submitted);
        assert(d.iwn_scan_abort_command(sc,5)==0 && writes==1);
        ++cases;
    }
    printf("PASS: %u complete IWN abort/reservation/command-doorbell scenarios\n",cases);
}
