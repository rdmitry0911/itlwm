// Complete production BTM RX parser, retained request helpers and timer worker.
// The IRQ/callout context and lower command results are explicit doubles.
#include <algorithm>
#include <array>
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <sys/types.h>

constexpr unsigned IEEE80211_ADDR_LEN=6, IEEE80211_NWID_LEN=32;
constexpr unsigned IEEE80211_M_STA=1, IEEE80211_S_RUN=4, IEEE80211_S_SCAN=1;
constexpr unsigned IEEE80211_F_BGSCAN=1, IEEE80211_F_RSNON=2,
    IEEE80211_F_DISABLE_BG_AUTO_CONNECT=4;
constexpr unsigned IEEE80211_WNM_BSS_TM_REQ_PREF_CAND_LIST=1,
    IEEE80211_WNM_BSS_TM_REQ_BSS_TERMINATION=8,
    IEEE80211_WNM_BSS_TM_REQ_ESS_DISASSOC=16,
    IEEE80211_ELEMID_NBR_REPORT=52,
    IEEE80211_WNM_BSS_TM_REJECT_NO_SUITABLE=7;
#define IEEE80211_ADDR_EQ(a,b) (std::memcmp((a),(b),6)==0)
#define IEEE80211_ADDR_COPY(a,b) std::memcpy((a),(b),6)
#define explicit_bzero(p,n) std::memset((p),0,(n))
struct IOSimpleLock {};
using IOInterruptState=unsigned;
static bool leaf_held=false, on_irq_thread=false;
static IOInterruptState IOSimpleLockLockDisableInterrupt(IOSimpleLock *lock) {
    assert(lock && !leaf_held); leaf_held=true; return 0;
}
static void IOSimpleLockUnlockEnableInterrupt(IOSimpleLock *,IOInterruptState) {
    assert(leaf_held); leaf_held=false;
}
struct CTimeout {};
struct _ifnet { unsigned unused=0; };
struct ieee80211_node {
    u_int8_t ni_bssid[6]{2,1,2,3,4,5}, ni_essid[32]{'L','a','b'};
    u_int8_t ni_esslen=3, ni_port_valid=1;
    unsigned ni_chan=13;
};
struct ieee80211_frame { u_int8_t prefix[10],i_addr2[6],suffix[8]; };
static_assert(sizeof(ieee80211_frame)==24,"wire header");
struct Packet { std::array<u_int8_t,64> bytes{}; size_t len=46; };
using mbuf_t=Packet *;
#define mbuf_len(m) ((m)->len)
#define mtod(m,t) reinterpret_cast<t>((m)->bytes.data())
#include "wnm-record.inc"
struct ieee80211com {
    _ifnet ic_if;
    unsigned ic_opmode=IEEE80211_M_STA, ic_state=IEEE80211_S_RUN;
    unsigned ic_flags=0, ic_mgt_timer=0;
    ieee80211_node *ic_bss=nullptr;
    IOSimpleLock *ic_pae_selected_bss_lock=nullptr;
    ieee80211_wnm_bss_transition ic_wnm_bss_transition{};
    u_int64_t ic_wnm_bss_transition_next_request=0;
    u_int64_t ic_pae_assoc_epoch=1;
    CTimeout *ic_wnm_bgscan_retry_timeout=nullptr;
    int (*ic_bgscan_start)(ieee80211com *,u_int64_t)=nullptr;
    int (*ic_bgscan_abort)(ieee80211com *,u_int64_t)=nullptr;
};
int ieee80211_begin_wnm_bgscan(_ifnet *);
static unsigned ieee80211_chan2ieee(ieee80211com *,unsigned channel) { return channel; }
static unsigned starts,aborts,blocked_commands,timers,rejections,node_clears;
static int start_result,abort_result,timer_result,last_delay;
static bool async_abort;
static std::function<void(ieee80211com *)> start_hook,abort_hook,node_clear_hook;
static int timeout_add_msec(CTimeout **,int delay) {
    assert(!leaf_held); ++timers; last_delay=delay; return timer_result;
}
static int ieee80211_send_bss_transition_response(ieee80211com *,
    ieee80211_node *,u_int8_t,unsigned status,const void *) {
    assert(!leaf_held && status==IEEE80211_WNM_BSS_TM_REJECT_NO_SUITABLE);
    ++rejections; return 0;
}
static void ieee80211_free_allnodes(ieee80211com *ic,int keep) {
    assert(!leaf_held && !on_irq_thread && keep==0); ++node_clears;
    if(node_clear_hook) node_clear_hook(ic);
}
static int lower_start(ieee80211com *ic,u_int64_t serial) {
    assert(!leaf_held && !serial); ++starts;
    if(on_irq_thread) { ++blocked_commands; return ETIMEDOUT; }
    assert(ic->ic_wnm_bss_transition.scan_starting);
    assert(!ic->ic_wnm_bss_transition.fresh_scan_pending);
    assert(ic->ic_flags&IEEE80211_F_BGSCAN);
    if(start_hook) start_hook(ic);
    return start_result;
}
static int lower_abort(ieee80211com *ic,u_int64_t serial) {
    assert(!leaf_held && !serial); ++aborts;
    if(on_irq_thread) { ++blocked_commands; return ETIMEDOUT; }
    if(abort_hook) abort_hook(ic);
    if(!async_abort && !abort_result) ic->ic_flags&=~IEEE80211_F_BGSCAN;
    return abort_result;
}
#include "wnm-dispatch.inc"
struct Fixture {
    ieee80211com ic;
    ieee80211_node node;
    IOSimpleLock lock;
    Packet packet;
    Fixture() {
        ic.ic_bss=&node; ic.ic_pae_selected_bss_lock=&lock;
        ic.ic_bgscan_start=lower_start; ic.ic_bgscan_abort=lower_abort;
        auto *frame=mtod(&packet,ieee80211_frame *);
        IEEE80211_ADDR_COPY(frame->i_addr2,node.ni_bssid);
        auto *frm=&packet.bytes[24]; frm[2]=17; frm[3]=1;
        frm[7]=IEEE80211_ELEMID_NBR_REPORT; frm[8]=13;
        const u_int8_t target[6]={2,6,7,8,9,10};
        IEEE80211_ADDR_COPY(frm+9,target); frm[20]=13;
    }
    void receive() {
        on_irq_thread=true;
        ieee80211_recv_wnm_bss_transition_req(&ic,&packet,&node);
        on_irq_thread=false;
    }
    void worker() { assert(!on_irq_thread); ieee80211_wnm_bgscan_retry_timeout(&ic.ic_if); }
    u_int64_t generation() { return ieee80211_wnm_bss_transition_request_generation(&ic); }
};
static void reset() {
    assert(!leaf_held && !on_irq_thread);
    starts=aborts=blocked_commands=timers=rejections=node_clears=0;
    start_result=abort_result=last_delay=0; timer_result=1; async_abort=false;
    start_hook={}; abort_hook={}; node_clear_hook={};
}
static void rx_control(bool busy) {
    reset(); Fixture f;
    if(busy) f.ic.ic_flags|=IEEE80211_F_BGSCAN;
    f.receive();
    std::fprintf(stderr,"actual BTM RX busy=%u start=%u abort=%u blocked=%u timers=%u\n",
        busy,starts,aborts,blocked_commands,timers);
    assert(!starts && !aborts && !blocked_commands && !node_clears && !rejections);
    assert(timers==1 && last_delay==1 && f.generation());
}
static unsigned matrix() {
    unsigned cases=0;
    for(bool busy : {false,true}) {
        rx_control(busy); ++cases;
        reset(); Fixture f; if(busy) f.ic.ic_flags|=IEEE80211_F_BGSCAN;
        f.receive(); f.worker();
        assert(starts==1 && aborts==unsigned(busy) && !blocked_commands);
        assert(node_clears==1 && !rejections && f.generation());
        assert(!f.ic.ic_wnm_bss_transition.fresh_scan_pending);
        assert(f.ic.ic_flags&IEEE80211_F_BGSCAN); ++cases;
    }
    { reset(); Fixture f; f.ic.ic_flags|=IEEE80211_F_BGSCAN; async_abort=true;
      f.receive(); f.worker();
      assert(aborts==1 && !starts && timers==2 && last_delay==100 && f.generation());
      f.ic.ic_flags&=~IEEE80211_F_BGSCAN; // Explicit lower physical terminal boundary.
      f.worker(); assert(starts==1 && !f.ic.ic_wnm_bss_transition.fresh_scan_pending && !rejections); ++cases; }
    for(int error : {EBUSY,EIO,ENXIO,EOPNOTSUPP}) {
        reset(); Fixture f; f.ic.ic_flags|=IEEE80211_F_BGSCAN; abort_result=error;
        f.receive(); f.worker(); assert(aborts==1 && !starts);
        if(error==EBUSY) assert(f.generation() && !rejections && last_delay==100);
        else assert(!f.generation() && rejections==1); ++cases;
    }
    { reset(); Fixture f; f.ic.ic_flags|=IEEE80211_F_BGSCAN; f.ic.ic_bgscan_abort=nullptr;
      f.receive(); f.worker(); assert(!starts && !aborts && rejections==1 && !f.generation()); ++cases; }
    for(bool during_abort : {false,true}) for(bool replacement : {false,true}) {
        reset(); Fixture f; if(during_abort) f.ic.ic_flags|=IEEE80211_F_BGSCAN;
        f.receive(); const auto old=f.generation(); u_int64_t next=0;
        auto mutation=[&](ieee80211com *) {
            if(replacement) { f.receive(); next=f.generation(); assert(next && next!=old); }
            else ieee80211_wnm_bss_transition_clear(&f.ic);
        };
        if(during_abort) abort_hook=mutation; else start_hook=mutation;
        f.worker();
        assert(!rejections && node_clears==unsigned(!during_abort) && !blocked_commands);
        assert(f.generation()==next && !f.ic.ic_wnm_bss_transition.scan_retry_count);
        assert(!f.ic.ic_wnm_bss_transition.scan_starting);
        if(during_abort) assert(!starts); ++cases;
    }
    { reset(); Fixture f; f.receive(); start_result=EIO; f.worker();
      assert(starts==1 && rejections==1 && node_clears==1 && !f.generation()); ++cases; }
    for(bool target_confirmed : {false,true}) {
        reset(); Fixture f; f.receive();
        start_hook=[&](ieee80211com *ic) {
            ic->ic_flags&=~IEEE80211_F_BGSCAN;
            ic->ic_wnm_bss_transition.candidate_confirmed=target_confirmed;
        }; // Explicit early terminal/upper selection boundary, not firmware.
        f.worker();
        assert(starts==1 && node_clears==1 && !rejections);
        assert(!(f.ic.ic_flags&IEEE80211_F_BGSCAN));
        assert(!f.ic.ic_wnm_bss_transition.fresh_scan_pending); ++cases;
    }
    { reset(); Fixture f; f.receive(); f.ic.ic_mgt_timer=1;
      for(unsigned i=0;i<50;++i) { f.worker(); assert(f.generation() && !rejections); }
      f.worker(); assert(!starts && !aborts && rejections==1 && !f.generation()); ++cases; }
    { reset(); Fixture f; timer_result=0; f.receive();
      assert(!starts && !aborts && !f.generation() && rejections==1); ++cases; }
    { reset(); Fixture f; f.ic.ic_wnm_bss_transition_next_request=UINT64_MAX;
      f.receive(); assert(f.generation()==1); ieee80211_wnm_bss_transition_clear(&f.ic);
      f.receive(); assert(f.generation()==2); ++cases; }
    for(unsigned invalid=0;invalid<4;++invalid) {
        reset(); Fixture f;
        if(invalid==0) f.packet.len=25;
        if(invalid==1) mtod(&f.packet,ieee80211_frame *)->i_addr2[5]^=1;
        if(invalid==2) { f.ic.ic_flags|=IEEE80211_F_RSNON; f.node.ni_port_valid=0; }
        if(invalid==3) f.packet.bytes[33]=1; // Multicast target.
        f.receive(); assert(!starts && !aborts && !f.generation() && !timers); ++cases;
    }
    return cases;
}
static void source_replacement_control(bool during_abort,unsigned replacement) {
    reset(); Fixture f;
    if(during_abort) f.ic.ic_flags|=IEEE80211_F_BGSCAN;
    f.receive();
    const auto old=f.generation();
    auto source_changed=[&](ieee80211com *) {
        // Explicit current-BSS replacement boundary, not a fabricated BTM.
        // A native join can replace this public association without arming
        // another BTM request or changing its retained request generation.
        if(replacement==1) f.node.ni_essid[0]='N';
        else if(replacement==2) ++f.ic.ic_pae_assoc_epoch;
        else f.node.ni_bssid[5]^=1;
        assert(f.ic.ic_wnm_bss_transition.request_generation==old);
    };
    if(during_abort) abort_hook=source_changed; else source_changed(&f.ic);
    f.worker();
    std::fprintf(stderr,"actual BTM source replacement duringAbort=%u replacement=%u "
        "start=%u abort=%u nodeClears=%u rejects=%u active=%u\n",
        during_abort,replacement,starts,aborts,node_clears,rejections,
        f.ic.ic_wnm_bss_transition.active);
    assert(!starts && !node_clears && !rejections);
    assert(!f.ic.ic_wnm_bss_transition.active);
}
static void change_source(Fixture &f,unsigned replacement) {
    switch(replacement) {
    case 0: f.node.ni_bssid[5]^=1; break;
    case 1: f.node.ni_essid[0]='N'; break;
    case 2: ++f.ic.ic_pae_assoc_epoch; break;
    case 3: f.ic.ic_pae_assoc_epoch=0; break;
    case 4: f.ic.ic_state=0; break;
    case 5: f.ic.ic_opmode=2; break;
    case 6: f.ic.ic_bss=nullptr; break;
    case 7: f.node.ni_esslen=33; break;
    case 8: f.ic.ic_wnm_bss_transition.ssid_len=33; break;
    default: assert(false);
    }
}
static void node_clear_source_control() {
    reset(); Fixture f; f.receive();
    node_clear_hook=[&](ieee80211com *) {
        change_source(f,2);
        f.ic.ic_flags=IEEE80211_F_DISABLE_BG_AUTO_CONNECT;
    }; // Explicit node-release callback boundary; not real firmware.
    f.worker();
    std::fprintf(stderr,"actual BTM source replacement during node release "
        "starts=%u nodeClears=%u flags=%u active=%u\n",starts,node_clears,
        f.ic.ic_flags,f.ic.ic_wnm_bss_transition.active);
    assert(!starts && node_clears==1 && !rejections);
    assert(!f.ic.ic_wnm_bss_transition.active);
    assert(f.ic.ic_flags==IEEE80211_F_DISABLE_BG_AUTO_CONNECT);
}
static unsigned source_matrix() {
    unsigned cases=0;
    for(unsigned replacement=0;replacement<9;++replacement) {
        for(bool during_abort : {false,true}) {
            reset(); Fixture f; if(during_abort) f.ic.ic_flags|=IEEE80211_F_BGSCAN;
            f.receive();
            if(during_abort) abort_hook=[&](ieee80211com *) { change_source(f,replacement); };
            else change_source(f,replacement);
            f.worker();
            assert(!starts && !node_clears && !rejections);
            assert(!f.ic.ic_wnm_bss_transition.active && timers==1); ++cases;
        }
        for(int error : {0,EIO}) {
            reset(); Fixture f; f.receive(); start_result=error;
            start_hook=[&](ieee80211com *) {
                change_source(f,replacement);
                f.ic.ic_flags=IEEE80211_F_DISABLE_BG_AUTO_CONNECT;
            };
            f.worker();
            assert(starts==1 && node_clears==1 && !rejections && timers==1);
            assert(!f.ic.ic_wnm_bss_transition.active);
            assert(f.ic.ic_flags==IEEE80211_F_DISABLE_BG_AUTO_CONNECT); ++cases;
        }
        for(bool confirm : {false,true}) {
            reset(); Fixture f; f.receive(); ieee80211_node target;
            IEEE80211_ADDR_COPY(target.ni_bssid,f.ic.ic_wnm_bss_transition.target_bssid);
            change_source(f,replacement);
            u_int8_t token=99,bssid[6]={1,1,1,1,1,1};
            if(confirm) {
                assert(!ieee80211_wnm_bss_transition_confirm_candidate(&f.ic,&target,&token,bssid));
                assert(!token && std::all_of(bssid,bssid+6,[](auto b) { return b==0; }));
            } else assert(!ieee80211_wnm_bss_transition_candidate_disposition(&f.ic,&target));
            assert(!f.ic.ic_wnm_bss_transition.active && !starts && !rejections); ++cases;
        }
    }
    for(unsigned replacement=0;replacement<7;++replacement) {
        reset(); Fixture f; f.receive(); ieee80211_node target;
        IEEE80211_ADDR_COPY(target.ni_bssid,f.ic.ic_wnm_bss_transition.target_bssid);
        u_int8_t token=0,bssid[6]{}; const auto generation=f.generation();
        assert(ieee80211_wnm_bss_transition_confirm_candidate(&f.ic,&target,&token,bssid));
        change_source(f,replacement);
        // The scan-source guard must not cancel the separate confirmed
        // descriptor/retarget continuation merely because it left RUN.
        assert(f.generation()==generation && f.ic.ic_wnm_bss_transition.candidate_confirmed);
        f.worker(); assert(!starts && !aborts && !node_clears && !rejections); ++cases;
    }
    { reset(); Fixture f; f.ic.ic_pae_assoc_epoch=0; f.receive();
      assert(!f.generation() && !starts && !aborts && !timers && rejections==1); ++cases; }
    { reset(); Fixture f; f.ic.ic_pae_assoc_epoch=UINT64_MAX; f.receive();
      f.ic.ic_pae_assoc_epoch=1; f.worker();
      assert(!f.generation() && !starts && !node_clears && !rejections); ++cases; }
    { reset(); Fixture f; f.receive(); node_clear_hook=[&](ieee80211com *) { f.receive(); };
      f.worker(); assert(!starts && node_clears==1 && !rejections);
      assert(f.generation() && f.ic.ic_wnm_bss_transition.fresh_scan_pending); ++cases; }
    node_clear_source_control(); ++cases;
    return cases;
}
static void confirmed_source_control(unsigned replacement) {
    reset(); Fixture f; f.receive(); ieee80211_node target;
    IEEE80211_ADDR_COPY(target.ni_bssid,f.ic.ic_wnm_bss_transition.target_bssid);
    u_int8_t token=0,bssid[6]{};
    assert(ieee80211_wnm_bss_transition_confirm_candidate(&f.ic,&target,&token,bssid));
    const auto generation=f.generation();
    const u_int8_t ssid[3]={'L','a','b'};
    // An independent native association replaces the source, not the owned
    // BTM source-leave continuation. This explicit boundary is not a new BTM
    // or an invented management TX completion.
    change_source(f,replacement);
    assert(f.generation()==generation);
    uint64_t copiedGeneration=99;
    const int retarget=ieee80211_wnm_bss_transition_copy_retarget(&f.ic,ssid,sizeof(ssid),bssid,&copiedGeneration);
    std::fprintf(stderr,"actual confirmed BTM stale source replacement=%u "
        "retarget=%d sourceEpoch=%llu currentEpoch=%llu active=%u\n",
        replacement,retarget,
        static_cast<unsigned long long>(f.ic.ic_wnm_bss_transition.source_epoch),
        static_cast<unsigned long long>(f.ic.ic_pae_assoc_epoch),
        f.ic.ic_wnm_bss_transition.active);
    assert(!retarget && !copiedGeneration);
    assert(std::all_of(bssid,bssid+6,[](auto b) { return b==0; }));
}
int main(int argc,char **argv) {
    if(argc==2 && std::strcmp(argv[1],"rx-idle")==0) rx_control(false);
    else if(argc==2 && std::strcmp(argv[1],"rx-busy")==0) rx_control(true);
    else if(argc==2 && std::strcmp(argv[1],"source-replaced")==0) source_replacement_control(false,0);
    else if(argc==2 && std::strcmp(argv[1],"ess-replaced")==0) source_replacement_control(false,1);
    else if(argc==2 && std::strcmp(argv[1],"epoch-replaced")==0) source_replacement_control(false,2);
    else if(argc==2 && std::strcmp(argv[1],"source-replaced-during-abort")==0) source_replacement_control(true,0);
    else if(argc==2 && std::strcmp(argv[1],"ess-replaced-during-abort")==0) source_replacement_control(true,1);
    else if(argc==2 && std::strcmp(argv[1],"epoch-replaced-during-abort")==0) source_replacement_control(true,2);
    else if(argc==2 && std::strcmp(argv[1],"source-replaced-during-node-release")==0) node_clear_source_control();
    else if(argc==2 && std::strcmp(argv[1],"confirmed-source-replaced")==0) confirmed_source_control(0);
    else if(argc==2 && std::strcmp(argv[1],"confirmed-ess-replaced")==0) confirmed_source_control(1);
    else if(argc==2 && std::strcmp(argv[1],"confirmed-epoch-replaced")==0) confirmed_source_control(2);
    else {
        const unsigned cases=matrix()+source_matrix();
        for(unsigned replacement=0;replacement<3;++replacement)
            confirmed_source_control(replacement);
        std::printf("actual BTM RX/callout dispatch: %u scenarios passed\n",cases+3);
    }
}
