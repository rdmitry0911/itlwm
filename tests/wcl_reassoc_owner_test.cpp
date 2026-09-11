// Execute complete common admission/retirement and controller dispatch.
// Epoch callbacks and physical scan firmware are explicit fixture boundaries.
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <utility>
#include <vector>
#include "tests/kernel_memory_test_support.hpp"
using u_int8_t = uint8_t;
using u_int16_t = uint16_t;
using u_int32_t = uint32_t;
using u_int64_t = uint64_t;
#define IEEE80211_ADDR_LEN 6
#define IEEE80211_ADDR_COPY(a,b) memcpy(a,b,6)
#define IEEE80211_M_STA 1
#define IEEE80211_S_RUN 4
#define IEEE80211_F_BGSCAN 1
#define IEEE80211_F_DISABLE_BG_AUTO_CONNECT 2
#define IEEE80211_F_RSNON 4
#define XYLog(...) do {} while (0)
#include "leaves.inc"
struct IOSimpleLock { bool held=false; };
using IOInterruptState = int;
static int IOSimpleLockLockDisableInterrupt(IOSimpleLock *lock) {
    assert(lock && !lock->held); lock->held=true; return 0;
}
static void IOSimpleLockUnlockEnableInterrupt(IOSimpleLock *lock, int) {
    assert(lock && lock->held); lock->held=false;
}
struct _ifnet {};
struct ieee80211_node { bool ni_port_valid=true; uint8_t ni_bssid[6]={2}; };
struct ieee80211com : _ifnet {
    IOSimpleLock lock;
    IOSimpleLock *ic_pae_selected_bss_lock=&lock;
    uint64_t ic_wcl_reassoc_next_serial=0, ic_wcl_reassoc_owner_serial=0;
    uint64_t ic_wcl_reassoc_source_epoch=0;
    uint64_t ic_wcl_reassoc_terminal_serial=0, ic_pae_assoc_epoch=11;
    uint64_t ic_wcl_reassoc_scan_accepted_serial=0;
    uint32_t ic_wcl_reassoc_owner_active=0, ic_wcl_reassoc_owner_last_leaf=0;
    ieee80211_wcl_reassoc_request ic_wcl_reassoc_request{};
    uint8_t ic_wcl_reassoc_source_bssid[6]{}, ic_wcl_reassoc_target_bssid[6]{};
    int ic_opmode=IEEE80211_M_STA, ic_state=IEEE80211_S_RUN, ic_mgt_timer=0, ic_flags=0;
    ieee80211_node node;
    ieee80211_node *ic_bss=&node;
    int (*ic_bgscan_start)(ieee80211com *, uint64_t)=nullptr;
    int (*ic_bgscan_abort)(ieee80211com *, uint64_t)=nullptr;
    void (*ic_event_handler)(ieee80211com *, int, void *)=nullptr;
};
static unsigned epochs, events, frees;
static std::function<void(ieee80211com *)> cancelContinuation, beforeEpoch, freeContinuation;
static void invoke(std::function<void(ieee80211com *)> &slot, ieee80211com *ic) {
    auto action=std::move(slot); slot={}; if(action) action(ic);
}
static uint64_t ieee80211_pae_assoc_epoch_begin_reassoc(ieee80211com *ic,
    uint64_t serial, uint64_t epoch) {
    assert(!ic->lock.held); invoke(beforeEpoch,ic);
    if(ic->ic_wcl_reassoc_next_serial!=serial ||
       ic->ic_wcl_reassoc_terminal_serial!=serial || ic->ic_pae_assoc_epoch!=epoch) return 0;
    ++epochs; const auto next=++ic->ic_pae_assoc_epoch;
    invoke(cancelContinuation,ic); return next;
}
static void ieee80211_free_allnodes(ieee80211com *ic,int) {
    assert(!ic->lock.held); ++frees; invoke(freeContinuation,ic);
}
#include "failure.inc"
using IOReturn = int;
using UInt32 = uint32_t;
constexpr int kIOReturnSuccess=0, kIOReturnNotReady=-1;
struct OSObject { virtual ~OSObject()=default; };
#define OSDynamicCast(type,object) dynamic_cast<type *>(object)
struct Hal { ieee80211com *ic; ieee80211com *get80211Controller(){ return ic; } };
struct AirportItlwm : OSObject {
    Hal *fHalService; void *fNetIf=this;
    std::vector<uint8_t> payload; uint32_t selector=0;
    void postMessage(void *,uint32_t code,void *data,size_t length,bool) {
        assert(!fHalService->ic->lock.held); selector=code;
        const auto *bytes=static_cast<uint8_t *>(data);
        payload.assign(bytes,bytes+length); ++events;
    }
};
#include "controller.inc"
static std::vector<std::pair<int,ieee80211_wcl_reassoc_completion>> queued;
static bool deferGate;
static int dispatch(ieee80211com *ic,int code,ieee80211_wcl_reassoc_completion copy) {
    Hal hal{ic}; AirportItlwm driver; driver.fHalService=&hal;
    int result=postWclReassocCompletionGated(&driver,&copy,
        reinterpret_cast<void *>(static_cast<uintptr_t>(code)),nullptr,nullptr);
    if(result==0) {
        const bool failure=code==IEEE80211_EVT_WCL_REASSOC_FAIL;
        assert(driver.selector==(failure ? IEEE80211_WCL_REASSOC_OWNER_SELECTOR_FAILURE :
            IEEE80211_WCL_REASSOC_OWNER_SELECTOR_REASSOC_EVENT));
        assert(driver.payload.size()==(failure?4U:8U));
        uint32_t value; memcpy(&value,driver.payload.data(),4); assert(value==copy.result);
    }
    return result;
}
static void event(ieee80211com *ic,int code,void *data) {
    assert(!ic->lock.held && data);
    const auto copy=*static_cast<ieee80211_wcl_reassoc_completion *>(data);
    if(deferGate) queued.emplace_back(code,copy); else (void)dispatch(ic,code,copy);
}
static void admit(ieee80211com *ic,uint8_t identity,uint32_t leaf) {
    ic->ic_wcl_reassoc_owner_serial=++ic->ic_wcl_reassoc_next_serial;
    ic->ic_wcl_reassoc_source_epoch=ic->ic_pae_assoc_epoch;
    ic->ic_wcl_reassoc_terminal_serial=0;
    ic->ic_wcl_reassoc_scan_accepted_serial=0;
    ic->ic_wcl_reassoc_owner_active=1; ic->ic_wcl_reassoc_owner_last_leaf=leaf;
    memset(&ic->ic_wcl_reassoc_request,identity,sizeof(ic->ic_wcl_reassoc_request));
    memset(ic->ic_wcl_reassoc_source_bssid,identity,6);
    memset(ic->ic_wcl_reassoc_target_bssid,identity+1,6); ic->ic_event_handler=event;
}
static void replacement(ieee80211com *ic) {
    admit(ic,7,IEEE80211_WCL_REASSOC_OWNER_LEAF_SCAN_STARTED);
}
static void preserved(const ieee80211com &ic) {
    assert(ic.ic_wcl_reassoc_owner_active && ic.ic_wcl_reassoc_request.feature_flags==7 &&
        ic.ic_wcl_reassoc_target_bssid[0]==8);
}
int main() {
    unsigned cases=0;
    for(unsigned scenario=0;scenario<22;++scenario) {
        epochs=events=frees=0; queued.clear(); deferGate=false;
        cancelContinuation={}; beforeEpoch={}; freeContinuation={};
        ieee80211com ic; admit(&ic,1,IEEE80211_WCL_REASSOC_OWNER_LEAF_ROAM_STARTED);
        if(scenario==0) {
            cancelContinuation=replacement; ieee80211_wcl_reassoc_post_failure(&ic,5);
            preserved(ic); assert(epochs==1 && events==0);
        } else if(scenario==1) {
            cancelContinuation=[](ieee80211com *v){ieee80211_wcl_reassoc_post_failure(v,6);};
            ieee80211_wcl_reassoc_post_failure(&ic,5); assert(epochs==1 && events==1);
        } else if(scenario==2) {
            ic.ic_wcl_reassoc_owner_last_leaf=IEEE80211_WCL_REASSOC_OWNER_LEAF_SCAN_STARTED;
            ic.ic_flags=IEEE80211_F_BGSCAN;
            ic.ic_bgscan_abort=[](ieee80211com *v,uint64_t serial){
                assert(serial==v->ic_wcl_reassoc_owner_serial);
                assert(!v->lock.held); ieee80211_wcl_reassoc_post_failure(v,5); replacement(v); return 0;};
            assert(ieee80211_cancel_wcl_reassoc_bgscan(&ic,6)==EBUSY);
            preserved(ic); assert(ic.ic_flags==IEEE80211_F_BGSCAN && epochs==0 && events==1);
        } else if(scenario==3 || scenario==4 || scenario==5) {
            ic.ic_wcl_reassoc_owner_active=0;
            ic.ic_bgscan_start=[](ieee80211com *v,uint64_t serial){
                assert(serial==v->ic_wcl_reassoc_owner_serial); replacement(v); return 0;};
            if(scenario==4) ic.ic_bgscan_start=[](ieee80211com *v,uint64_t){replacement(v); return EIO;};
            if(scenario==5) {
                ic.ic_bgscan_start=[](ieee80211com *,uint64_t){return 0;}; freeContinuation=replacement;
            }
            ieee80211_wcl_reassoc_request request{};
            assert(ieee80211_begin_wcl_reassoc_bgscan(&ic,&request)==ECANCELED);
            preserved(ic); assert(events==0 && frees==1);
        } else if(scenario==6) {
            deferGate=true; ieee80211_wcl_reassoc_post_failure(&ic,5);
            replacement(&ic); auto serial=ieee80211_wcl_reassoc_serial(&ic);
            ieee80211_wcl_reassoc_post_failure_owned(&ic,serial-1,8); preserved(ic);
            ieee80211_wcl_reassoc_post_failure(&ic,9); assert(queued.size()==2);
            assert(dispatch(&ic,queued[0].first,queued[0].second)!=0);
            assert(dispatch(&ic,queued[1].first,queued[1].second)==0);
            assert(dispatch(&ic,queued[1].first,queued[1].second)!=0); assert(events==1);
        } else if(scenario==7 || scenario==8) {
            beforeEpoch=replacement;
            if(scenario==8) beforeEpoch=[](ieee80211com *v){++v->ic_pae_assoc_epoch;};
            ieee80211_wcl_reassoc_post_failure(&ic,5); assert(epochs==0 && events==0);
            if(scenario==7) preserved(ic);
        } else if(scenario==9) {
            deferGate=true; ieee80211_wcl_reassoc_post_success(&ic);
            assert(queued.size()==1 && epochs==0); ++ic.ic_pae_assoc_epoch;
            assert(dispatch(&ic,queued[0].first,queued[0].second)!=0);
        } else if(scenario==10 || scenario==11) {
            ic.ic_wcl_reassoc_owner_active=0;
            ic.ic_bgscan_start=[](ieee80211com *,uint64_t){return 0;};
            ieee80211_wcl_reassoc_request request{}; request.feature_flags=4;
            if(scenario==11) {
                ic.ic_wcl_reassoc_next_serial=UINT64_MAX;
                assert(ieee80211_begin_wcl_reassoc_bgscan(&ic,&request)==EOVERFLOW);
                assert(frees==0 && ic.ic_wcl_reassoc_next_serial==UINT64_MAX);
            } else {
                assert(ieee80211_begin_wcl_reassoc_bgscan(&ic,&request)==0);
                assert(frees==1 && ic.ic_wcl_reassoc_request.feature_flags==4);
                auto serial=ieee80211_wcl_reassoc_serial(&ic); ic.ic_flags=0;
                assert(ieee80211_cancel_wcl_reassoc_bgscan(&ic,5)==0);
                assert(epochs==0 && events==1);
                assert(ieee80211_begin_wcl_reassoc_bgscan(&ic,&request)==0);
                assert(ieee80211_wcl_reassoc_serial(&ic)>serial);
            }
        } else if(scenario==12) {
            ieee80211_wcl_reassoc_post_success(&ic); ieee80211_wcl_reassoc_post_success(&ic);
            assert(events==1 && epochs==0);
        } else if(scenario==13) {
            ic.ic_pae_selected_bss_lock=nullptr; ieee80211_wcl_reassoc_post_failure(&ic,5);
            assert(ic.ic_wcl_reassoc_owner_active && events==0 && epochs==0);
        } else if(scenario==14) {
            ic.ic_wcl_reassoc_owner_last_leaf=IEEE80211_WCL_REASSOC_OWNER_LEAF_SETUP;
            ieee80211_wcl_reassoc_post_failure(&ic,5); assert(events==0 && epochs==0);
        } else if(scenario==15) {
            ic.ic_wcl_reassoc_owner_last_leaf=IEEE80211_WCL_REASSOC_OWNER_LEAF_SCAN_STARTED;
            ieee80211_wcl_reassoc_post_success(&ic); assert(events==0);
            ieee80211_wcl_reassoc_post_failure(&ic,0); ieee80211_wcl_reassoc_post_failure(&ic,5);
            assert(events==1 && epochs==0);
        } else if(scenario==16 || scenario==17) {
            ic.ic_wcl_reassoc_owner_active=0;
            ic.ic_bgscan_start=[](ieee80211com *v,uint64_t serial) {
                assert(ieee80211_wcl_reassoc_scan_completion_begin(v,serial));
                ieee80211_wcl_reassoc_post_failure_owned(v,serial,5);
                return 0;
            };
            if(scenario==17) ic.ic_bgscan_start=[](ieee80211com *v,uint64_t serial) {
                assert(ieee80211_wcl_reassoc_scan_completion_begin(v,serial));
                v->ic_wcl_reassoc_owner_last_leaf=IEEE80211_WCL_REASSOC_OWNER_LEAF_ROAM_STARTED;
                ++v->ic_pae_assoc_epoch;
                return 0;
            };
            ieee80211_wcl_reassoc_request request{};
            assert(ieee80211_begin_wcl_reassoc_bgscan(&ic,&request)==0);
            if(scenario==16) assert(!ic.ic_wcl_reassoc_owner_active && events==1);
            else assert(ic.ic_wcl_reassoc_owner_last_leaf==IEEE80211_WCL_REASSOC_OWNER_LEAF_ROAM_STARTED);
            assert(frees==1);
        } else if(scenario==18) {
            const auto old=ieee80211_wcl_reassoc_serial(&ic);
            replacement(&ic);
            assert(!ieee80211_wcl_reassoc_scan_completion_begin(&ic,old));
            assert(!ieee80211_wcl_reassoc_scan_completion_begin(&ic,0));
            preserved(ic);
        } else if(scenario==19) {
            assert(!ieee80211_wcl_reassoc_scan_completion_begin(&ic,
                ieee80211_wcl_reassoc_serial(&ic)));
            ic.ic_wcl_reassoc_owner_active=0;
            assert(ieee80211_wcl_reassoc_scan_completion_begin(&ic,0));
        } else {
            ic.ic_wcl_reassoc_owner_last_leaf=IEEE80211_WCL_REASSOC_OWNER_LEAF_SCAN_STARTED;
            const auto serial=ieee80211_wcl_reassoc_serial(&ic);
            if(scenario==20) ++ic.ic_pae_assoc_epoch;
            else ++ic.ic_wcl_reassoc_source_epoch;
            ic.ic_flags=0;
            assert(!ieee80211_wcl_reassoc_scan_completion_begin(&ic,serial));
            assert(ic.ic_flags==0 && ic.ic_wcl_reassoc_scan_accepted_serial==0);
        }
        ++cases;
    }
    printf("PASS: %u actual reassoc admission/abort/retirement/controller-gate cases\n",cases);
}
