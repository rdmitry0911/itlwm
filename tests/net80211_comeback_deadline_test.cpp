/* Complete production deadline/watchdog/admission functions. Kernel clock,
 * management submission, state changes and lower firmware preparation are
 * explicit boundaries, not an RF simulation or a second watchdog model. */
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include "tests/kernel_memory_test_support.hpp"
#include "itl80211/openbsd/net80211/ieee80211_assoc_comeback.h"
using u_int8_t=uint8_t;
using u_int32_t=uint32_t;
using u_int64_t=uint64_t;
static constexpr unsigned IEEE80211_ADDR_LEN=6;
static constexpr uint32_t kMicrosecondScale=1000;
enum { IEEE80211_M_STA=1, IEEE80211_S_SCAN=1, IEEE80211_S_AUTH=2,
    IEEE80211_S_ASSOC=3, IEEE80211_S_RUN=4, IFF_DEBUG=1,
    IEEE80211_F_AUTO_JOIN=2, IEEE80211_FC0_SUBTYPE_ASSOC_REQ=0,
    IEEE80211_FC0_SUBTYPE_REASSOC_REQ=0x20 };
#define IEEE80211_ADDR_COPY(a,b) memcpy((a),(b),6)
#define IEEE80211_ADDR_EQ(a,b) (memcmp((a),(b),6)==0)
#define ISSET(value,bit) ((value)&(bit))
#define XYLog(...) ((void)0)
struct _ifnet { unsigned if_flags=0,if_timer=0; };
struct ieee80211_node {
    uint8_t ni_bssid[6]={2,4,6,8,10,12},ni_macaddr[6]={2,4,6,8,10,12};
    unsigned ni_fails=0;
};
#include "retry.inc"
struct ieee80211com {
    _ifnet ic_if; // Production's first-member cast.
    int ic_opmode=IEEE80211_M_STA,ic_state=IEEE80211_S_ASSOC,ic_mgt_timer=0;
    bool ic_assoc_comeback_pending=false,ic_assoc_comeback_reassoc=false;
    bool ic_wcl_reassoc_owner_active=false;
    int ic_wcl_reassoc_owner_last_leaf=0;
    uint32_t ic_assoc_comeback_tu=0,ic_assoc_comeback_retries=1;
    uint32_t ic_assoc_status=30,ic_flags=0;
    uint64_t ic_assoc_comeback_deadline=0,epoch=7;
    ieee80211_node *ic_bss=nullptr;
    int (*ic_assoc_comeback_retry)(ieee80211com *,const ieee80211_assoc_comeback_retry *)=nullptr;
};
struct ieee80211_sae_driver_hook_snapshot {
    int (*auth_owned)(ieee80211com *,ieee80211_node *)=nullptr;
};
static uint64_t nowNs,sendAt,prepareAt;
static unsigned sends,prepares,epochs,scans,failures,cases;
static int sendError;
static ieee80211_assoc_comeback_retry captured;
static void clock_get_uptime(uint64_t *out) { *out=nowNs; }
static void clock_interval_to_deadline(uint32_t interval,uint32_t scale,uint64_t *out) {
    assert(scale==kMicrosecondScale);
    *out=nowNs+uint64_t(interval)*scale;
}
static int send_mgmt(ieee80211com *ic,ieee80211_node *,int subtype,int) {
    assert(subtype==IEEE80211_FC0_SUBTYPE_ASSOC_REQ || subtype==IEEE80211_FC0_SUBTYPE_REASSOC_REQ);
    ++sends; sendAt=nowNs;
    if (!sendError) ic->ic_mgt_timer=5;
    return sendError;
}
#define IEEE80211_SEND_MGMT(ic,ni,type,arg) send_mgmt(ic,ni,type,arg)
static uint64_t ieee80211_pae_assoc_epoch_current(ieee80211com *ic) { return ic->epoch; }
static uint64_t ieee80211_pae_assoc_epoch_begin(ieee80211com *ic) { ++epochs; return ++ic->epoch; }
static void ieee80211_sae_driver_hook_snapshot_copyout(ieee80211com *,ieee80211_sae_driver_hook_snapshot *) { assert(false); }
static bool ieee80211_wcl_reassoc_leaf_is_post_send(int) { return false; }
static void ieee80211_wcl_reassoc_post_failure(ieee80211com *,uint32_t) { ++failures; }
static ieee80211_node *ieee80211_find_node(ieee80211com *,const uint8_t *) { return nullptr; }
static void ieee80211_node_join_bss(ieee80211com *,ieee80211_node *) { assert(false); }
static void ieee80211_deselect_ess(ieee80211com *) { assert(false); }
static void ieee80211_new_state(ieee80211com *ic,int state,int) { assert(state==IEEE80211_S_SCAN); ++scans; ic->ic_state=state; }
static int prepare(ieee80211com *,const ieee80211_assoc_comeback_retry *retry) {
    ++prepares; prepareAt=nowNs; captured=*retry; return 0;
}
#include "functions.inc"
#include "watchdog.inc"
static void reset() {
    nowNs=5000000000ULL; sends=prepares=epochs=scans=failures=0;
    sendAt=prepareAt=0; sendError=0; captured={};
}
static ieee80211_assoc_comeback_plan arm(ieee80211com &ic,ieee80211_node &node,uint32_t tu,bool reassoc=false) {
    const uint8_t ie[]={56,5,3,uint8_t(tu),uint8_t(tu>>8),uint8_t(tu>>16),uint8_t(tu>>24)};
    ieee80211_assoc_comeback_plan plan={};
    assert(ieee80211_assoc_comeback_parse(ie,sizeof(ie),&plan));
    ic.ic_bss=&node;
    assert(ieee80211_assoc_comeback_set_deadline(&ic,plan.timeout_tu)==0);
    ic.ic_assoc_comeback_tu=plan.timeout_tu;
    ic.ic_assoc_comeback_pending=true;
    ic.ic_assoc_comeback_reassoc=reassoc;
    ic.ic_wcl_reassoc_owner_active=reassoc;
    ic.ic_state=reassoc ? IEEE80211_S_RUN:IEEE80211_S_ASSOC;
    ic.ic_mgt_timer=plan.timeout_seconds;
    return plan;
}
static ieee80211_assoc_comeback_retry identity(const ieee80211com &ic) {
    ieee80211_assoc_comeback_retry retry={};
    retry.association_epoch=ic.epoch;
    retry.not_before=ic.ic_assoc_comeback_deadline;
    retry.timeout_tu=ic.ic_assoc_comeback_tu;
    IEEE80211_ADDR_COPY(retry.bssid,ic.ic_bss->ni_bssid);
    retry.subtype=ic.ic_assoc_comeback_reassoc ? IEEE80211_FC0_SUBTYPE_REASSOC_REQ:IEEE80211_FC0_SUBTYPE_ASSOC_REQ;
    retry.retry=ic.ic_assoc_comeback_retries;
    return retry;
}
static void phase(uint32_t tu,uint32_t phaseUs,bool hook,bool reassoc,bool timingOnly=false) {
    reset();
    ieee80211com ic; ieee80211_node node;
    const uint64_t began=nowNs;
    const auto plan=arm(ic,node,tu,reassoc);
    const uint64_t deadline=ic.ic_assoc_comeback_deadline;
    assert(deadline==began+uint64_t(tu)*1024000);
    if (hook) ic.ic_assoc_comeback_retry=prepare;
    for (uint32_t tick=0; tick<plan.timeout_seconds+3 && !sends && !prepares; ++tick) {
        nowNs=began+uint64_t(phaseUs)*1000+uint64_t(tick)*1000000000;
        ieee80211_watchdog(&ic.ic_if);
        if (!sends && !prepares) {
            assert(ic.ic_assoc_comeback_pending);
            assert(ic.ic_assoc_comeback_deadline==deadline);
            assert(ic.ic_if.if_timer==1);
        }
    }
    const uint64_t actual=hook ? prepareAt:sendAt;
    std::printf("TU=%u phase_us=%u hook=%d reassoc=%d deadline_ns=%llu admission_ns=%llu\n",tu,phaseUs,hook,reassoc,
        (unsigned long long)deadline,(unsigned long long)actual);
    std::fflush(stdout);
    assert(actual>=deadline && "association retry must not precede AP comeback deadline");
    if (timingOnly) {
        // The unchanged watchdog has no new deadline-field cleanup contract.
        // This explicit control checks only timing and its existing direct-send
        // semantics; all new-source matrix cases below retain full assertions.
        assert(!hook && sends==1 && epochs==0 && scans==0 && failures==0);
        assert(!ic.ic_assoc_comeback_pending && ic.ic_assoc_comeback_tu==0);
        assert(ic.ic_mgt_timer==5 && ic.epoch==7);
        ++cases;
        return;
    }
    if (hook) {
        assert(prepares==1 && sends==0 && captured.not_before==deadline);
        assert(ieee80211_assoc_comeback_retry_ready(&ic,&captured)==0);
        nowNs+=17000;
        assert(ieee80211_assoc_comeback_retry_complete(&ic,&captured)==0);
        assert(ieee80211_assoc_comeback_retry_complete(&ic,&captured)==ENOENT);
    }
    assert(sends==1 && epochs==0 && scans==0 && failures==0);
    assert(!ic.ic_assoc_comeback_pending && ic.ic_assoc_comeback_deadline==0);
    assert(ic.ic_mgt_timer==5 && ic.epoch==7);
    ++cases;
}
static void boundaries() {
    reset(); ieee80211com ic; ieee80211_node node;
    arm(ic,node,1000);
    auto retry=identity(ic);
    nowNs=retry.not_before-1;
    assert(ieee80211_assoc_comeback_retry_ready(&ic,&retry)==EAGAIN);
    assert(ieee80211_assoc_comeback_retry_complete(&ic,&retry)==EAGAIN);
    assert(sends==0 && ic.ic_assoc_comeback_pending && ic.ic_assoc_comeback_deadline==retry.not_before);
    ++cases;
    auto stale=retry; stale.not_before++;
    assert(ieee80211_assoc_comeback_retry_ready(&ic,&stale)==ENOENT);
    assert(ieee80211_assoc_comeback_retry_abort(&ic,&stale,EIO)==0);
    assert(epochs==0 && scans==0 && ic.ic_assoc_comeback_deadline==retry.not_before);
    ++cases;
    nowNs=retry.not_before;
    assert(ieee80211_assoc_comeback_retry_ready(&ic,&retry)==0);
    assert(ieee80211_assoc_comeback_retry_complete(&ic,&retry)==0);
    assert(sendAt==retry.not_before && sends==1 && ic.ic_assoc_comeback_deadline==0);
    ++cases;
    reset(); ic={}; arm(ic,node,1000,true); retry=identity(ic);
    nowNs=retry.not_before-1;
    assert(ieee80211_assoc_comeback_retry_abort(&ic,&retry,EIO)==1);
    assert(epochs==1 && scans==1 && failures==1 && sends==0 && !ic.ic_assoc_comeback_pending && ic.ic_assoc_comeback_deadline==0);
    ++cases;
    reset(); ic={}; arm(ic,node,1000); retry=identity(ic);
    nowNs=retry.not_before; sendError=EIO;
    assert(ieee80211_assoc_comeback_retry_complete(&ic,&retry)==EIO);
    assert(sends==1 && epochs==1 && scans==1 && !ic.ic_assoc_comeback_pending && ic.ic_assoc_comeback_deadline==0);
    ++cases;
    reset(); ic={}; arm(ic,node,1000); retry=identity(ic);
    nowNs+=123456; arm(ic,node,1000);
    const auto replacement=ic.ic_assoc_comeback_deadline;
    nowNs=replacement;
    assert(ieee80211_assoc_comeback_retry_complete(&ic,&retry)==ENOENT);
    assert(ieee80211_assoc_comeback_retry_abort(&ic,&retry,EIO)==0);
    assert(ic.ic_assoc_comeback_deadline==replacement && sends==0 && epochs==0);
    ++cases;
    ic.ic_assoc_comeback_deadline=77;
    assert(ieee80211_assoc_comeback_set_deadline(nullptr,1000)==EINVAL);
    assert(ieee80211_assoc_comeback_set_deadline(&ic,0)==EINVAL);
    assert(ieee80211_assoc_comeback_set_deadline(&ic,29297)==EINVAL);
    assert(ieee80211_assoc_comeback_set_deadline(&ic,UINT32_MAX)==EINVAL);
    assert(ic.ic_assoc_comeback_deadline==77);
    ++cases;
}
static void invalidIdentities() {
    for (unsigned variant=0; variant<14; ++variant) {
        reset(); ieee80211com ic; ieee80211_node node;
        arm(ic,node,1000);
        auto retry=identity(ic);
        nowNs=retry.not_before;
        switch (variant) {
        case 0: retry.association_epoch=0; break;
        case 1: ++retry.association_epoch; break;
        case 2: retry.not_before=0; break;
        case 3: retry.timeout_tu=0; break;
        case 4: ++retry.timeout_tu; break;
        case 5: retry.retry=0; break;
        case 6: ++retry.retry; break;
        case 7: retry.bssid[5]^=1; break;
        case 8: retry.subtype=0x40; break;
        case 9: retry.subtype=IEEE80211_FC0_SUBTYPE_REASSOC_REQ; break;
        case 10: ic.ic_state=IEEE80211_S_SCAN; break;
        case 11: ic.ic_opmode=0; break;
        case 12: ic.ic_bss=nullptr; break;
        case 13: ic.ic_assoc_comeback_pending=false; break;
        }
        const auto before=ic;
        assert(ieee80211_assoc_comeback_retry_ready(&ic,&retry)==ENOENT);
        assert(ieee80211_assoc_comeback_retry_complete(&ic,&retry)==ENOENT);
        assert(ieee80211_assoc_comeback_retry_abort(&ic,&retry,EIO)==0);
        assert(ic.ic_assoc_comeback_deadline==before.ic_assoc_comeback_deadline);
        assert(ic.ic_assoc_comeback_pending==before.ic_assoc_comeback_pending);
        assert(ic.ic_mgt_timer==before.ic_mgt_timer && ic.epoch==before.epoch);
        assert(sends==0 && prepares==0 && epochs==0 && scans==0 && failures==0);
        ++cases;
    }
    reset(); ieee80211com ic; ieee80211_node node;
    arm(ic,node,1000,true); auto retry=identity(ic);
    nowNs=retry.not_before; ic.ic_wcl_reassoc_owner_active=false;
    assert(ieee80211_assoc_comeback_retry_complete(&ic,&retry)==ENOENT);
    assert(ieee80211_assoc_comeback_retry_abort(&ic,&retry,EIO)==0);
    assert(sends==0 && epochs==0 && ic.ic_assoc_comeback_deadline==retry.not_before);
    ++cases;
    assert(ieee80211_assoc_comeback_retry_ready(nullptr,&retry)==ENOENT);
    assert(ieee80211_assoc_comeback_retry_ready(&ic,nullptr)==ENOENT);
    assert(ieee80211_assoc_comeback_retry_complete(nullptr,&retry)==ENOENT);
    assert(ieee80211_assoc_comeback_retry_complete(&ic,nullptr)==ENOENT);
    assert(ieee80211_assoc_comeback_retry_abort(nullptr,&retry,EIO)==0);
    assert(ieee80211_assoc_comeback_retry_abort(&ic,nullptr,EIO)==0);
    ++cases;
    reset(); ic={}; nowNs=UINT64_C(365)*24*60*60*1000000000;
    arm(ic,node,29296); retry=identity(ic);
    nowNs=retry.not_before-1;
    assert(ieee80211_assoc_comeback_retry_complete(&ic,&retry)==EAGAIN);
    nowNs=retry.not_before;
    assert(ieee80211_assoc_comeback_retry_complete(&ic,&retry)==0);
    assert(sends==1 && sendAt==retry.not_before);
    ++cases;
}
int main(int argc,char **argv) {
    assert(argc==2);
    if (strcmp(argv[1],"early")==0) phase(1000,1,false,false);
    else if (strcmp(argv[1],"early-hook")==0) phase(1000,1,true,false);
    else if (strcmp(argv[1],"late-control")==0) phase(1000,1000000,false,false);
    else if (strcmp(argv[1],"baseline-late-timing-control")==0) phase(1000,1000000,false,false,true);
    else if (strcmp(argv[1],"all")==0) {
        for (uint32_t tu : {1U,976U,977U,1000U,1001U,29296U})
            for (uint32_t offset : {1U,271828U,999999U,1000000U})
                for (bool hook : {false,true})
                    for (bool reassoc : {false,true}) phase(tu,offset,hook,reassoc);
        boundaries();
        invalidIdentities();
    } else return 2;
    std::printf("PASS: %u actual-function deadline/admission cases; no RF claim\n",cases);
}
