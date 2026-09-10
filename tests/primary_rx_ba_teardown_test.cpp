/* Complete production RUN-stop and RX-aggregation bodies. Firmware commands,
 * timer cancellation, packet purge and lower MAC/PHY teardown are boundaries;
 * this test does not establish workloop serialization or DMA/reset safety. */
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>
#include <vector>
#include "defines.inc"

#define nitems(a) (sizeof(a)/sizeof((a)[0]))
#define isset(a,b) ((a)[(b)/8] & (1U<<((b)%8)))
#define XYLog(...) ((void)0)
#define DEVNAME(sc) "fixture"
#define splassert(x) ((void)0)
#define IPL_NET 0
enum { IEEE80211_M_STA, IEEE80211_M_MONITOR, IEEE80211_NODE_HT=1 };
static int splnet() { return 1; }
static void splx(int) {}
struct ItlFirmwareContextReceipt {};
struct ieee80211_rx_ba { int ba_timeout_val=0; };
struct ieee80211_node {
    unsigned ni_flags=0;
    void *ni_chan=nullptr;
    ieee80211_rx_ba ni_rx_ba[16];
};
struct iwm_node { ieee80211_node in_ni; };
struct iwx_node { ieee80211_node in_ni; };
struct ieee80211com { ieee80211_node *ic_bss=nullptr; int ic_opmode=IEEE80211_M_STA; };
struct Reorder { unsigned ssn=0, window=0; };
struct RxBa {
    uint8_t sta_id=0, tid=0, baid=IWM_RX_REORDER_DATA_INVALID_BAID;
    unsigned timeout=0, last_rx=0, session_timer=0, clears=0;
    Reorder reorder_buf;
};
struct iwm_rxba_data : RxBa {};
struct iwx_rxba_data : RxBa {};
struct iwm_tx_ring { int cur=0; };
template<class Entry> struct Softc {
    ieee80211com sc_ic;
    bool sc_mqrx_supported=true;
    int sc_rx_ba_sessions=0;
    Entry sc_rxba_data[IWM_MAX_BAID];
    uint32_t agg_queue_mask=0;
    struct { unsigned start_tidmask=0, stop_tidmask=0; } ba_tx;
    uint8_t sc_enabled_capa[128]={};
    int sc_phyctxt[2]={};
    iwm_tx_ring txq[32];
    int failTid=-1, failFlush=0;
    unsigned commandCount=0, flushCount=0, laterCleanup=0;
    uint8_t nextBaid=8;
};
struct iwm_softc : Softc<iwm_rxba_data> {};
struct iwx_softc : Softc<iwx_rxba_data> {};
static unsigned accepted, refused, nodeDeletes;
static void getmicrouptime(unsigned *stamp) { *stamp=1; }
static void timeout_add_usec(unsigned *timer,int value) { *timer=value; }
static void ieee80211_addba_req_accept(ieee80211com *,ieee80211_node *,uint8_t) { ++accepted; }
static void ieee80211_addba_req_refuse(ieee80211com *,ieee80211_node *,uint8_t) { ++refused; }
static void ieee80211_ba_del(ieee80211_node *) { ++nodeDeletes; }
template<class Sc> static int baCommand(Sc *sc,uint8_t tid,bool start,uint8_t *baid)
{
    ++sc->commandCount;
    if(tid==sc->failTid) return ETIMEDOUT;
    if(start) *baid=sc->nextBaid;
    return 0;
}
template<class Sc,class Entry> static void clearEntry(Sc *,Entry *entry)
{
    assert(entry->sta_id==IWM_STATION_ID && "primary cleanup touched an AP client");
    assert(entry->baid!=IWM_RX_REORDER_DATA_INVALID_BAID && "duplicate host BA retirement");
    ++entry->clears;
    entry->baid=IWM_RX_REORDER_DATA_INVALID_BAID;
}
class ItlIwm {
public:
    iwm_softc com;
    int iwm_run_stop(iwm_softc *);
    int iwm_sta_rx_agg(iwm_softc *,ieee80211_node *,uint8_t,uint16_t,uint16_t,int,int,const ItlFirmwareContextReceipt * = nullptr);
    int iwm_sta_rx_ba_cmd(iwm_softc *sc,const ItlFirmwareContextReceipt *,uint8_t tid,uint16_t,uint16_t,bool start,uint8_t *baid)
    { return baCommand(sc,tid,start,baid); }
    void iwm_clear_reorder_buffer(iwm_softc *sc,iwm_rxba_data *entry) { clearEntry(sc,entry); }
    void iwm_init_reorder_buffer(Reorder *r,unsigned ssn,unsigned window) { r->ssn=ssn; r->window=window; }
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
class ItlIwx {
public:
    iwx_softc com;
    int iwx_run_stop(iwx_softc *);
    IWX_RX_AGG_RETURN iwx_sta_rx_agg(iwx_softc *,ieee80211_node *,uint8_t,uint16_t,uint16_t,int,int,const ItlFirmwareContextReceipt * = nullptr);
    int iwx_sta_rx_ba_cmd(iwx_softc *sc,const ItlFirmwareContextReceipt *,uint8_t tid,uint16_t,uint16_t,bool start,uint8_t *baid)
    { return baCommand(sc,tid,start,baid); }
    void iwx_clear_reorder_buffer(iwx_softc *sc,iwx_rxba_data *entry) { clearEntry(sc,entry); }
    void iwx_init_reorder_buffer(Reorder *r,unsigned ssn,unsigned window) { r->ssn=ssn; r->window=window; }
    int iwx_flush_sta(iwx_softc *sc,iwx_node *) { ++sc->flushCount; return sc->failFlush; }
    int iwx_sf_config(iwx_softc *sc,int) { ++sc->laterCleanup; return 0; }
    int iwx_disable_beacon_filter(iwx_softc *) { return 0; }
    int iwx_update_quotas(iwx_softc *,iwx_node *,int) { return 0; }
    int iwx_mac_ctxt_cmd(iwx_softc *,iwx_node *,int,int) { return 0; }
    bool iwx_mimo_enabled(iwx_softc *) { return true; }
    int iwx_phy_ctxt_update(iwx_softc *,int *,void *,int,int,int) { return 0; }
};
#include "production.inc"

template<class Driver> static int stop(Driver &d)
{
    if constexpr(std::is_same<Driver,ItlIwm>::value) return d.iwm_run_stop(&d.com);
    else return d.iwx_run_stop(&d.com);
}
template<class Driver> static void aggregate(Driver &d,uint8_t tid,bool start)
{
    if constexpr(std::is_same<Driver,ItlIwm>::value)
        (void)d.iwm_sta_rx_agg(&d.com,d.com.sc_ic.ic_bss,tid,0x345,64,32000,start);
    else (void)d.iwx_sta_rx_agg(&d.com,d.com.sc_ic.ic_bss,tid,0x345,64,32000,start);
}
static unsigned cases;
template<class Driver,class Node> static void testFamily()
{
    for(bool apFirst : {false,true}) for(int failure : {-1,3,5}) {
        Driver d; Node node; auto &sc=d.com; sc.sc_ic.ic_bss=&node.in_ni;
        const int primary=apFirst?2:0, ap=apFirst?0:2;
        for(int i=0;i<2;++i) {
            auto &p=sc.sc_rxba_data[primary+i], &a=sc.sc_rxba_data[ap+i];
            p.sta_id=IWM_STATION_ID; p.tid=3+2*i; p.baid=primary+i;
            a.sta_id=6+i; a.tid=p.tid; a.baid=ap+i;
        }
        sc.sc_rx_ba_sessions=4; sc.failTid=failure;
        assert(stop(d)==(failure<0?0:ETIMEDOUT));
        const unsigned retired=failure<0?2:failure==3?0:1;
        assert(sc.sc_rx_ba_sessions==4-static_cast<int>(retired));
        assert(sc.laterCleanup==(failure<0?1U:0U));
        for(int i=0;i<2;++i) {
            const auto &p=sc.sc_rxba_data[primary+i], &a=sc.sc_rxba_data[ap+i];
            assert(p.clears==(i<retired?1U:0U));
            assert(a.clears==0 && a.baid==ap+i && a.sta_id==6+i);
        }
        sc.failTid=-1;
        assert(stop(d)==0 && sc.sc_rx_ba_sessions==2);
        const unsigned commands=sc.commandCount;
        assert(stop(d)==0 && sc.commandCount==commands && sc.sc_rx_ba_sessions==2);
        aggregate(d,3,false); // AP still owns this TID; primary no longer does.
        assert(sc.commandCount==commands && sc.sc_rx_ba_sessions==2);
        for(int i=0;i<2;++i) {
            assert(sc.sc_rxba_data[primary+i].clears==1);
            assert(sc.sc_rxba_data[ap+i].clears==0 && sc.sc_rxba_data[ap+i].baid==ap+i);
        }
        ++cases;
    }
    for(int failure : {-1,3}) {
        Driver d; Node node; auto &sc=d.com; sc.sc_ic.ic_bss=&node.in_ni;
        accepted=refused=0; sc.failTid=failure;
        aggregate(d,3,true);
        assert(accepted==(failure<0?1U:0U) && refused==(failure<0?0U:1U));
        assert(sc.sc_rx_ba_sessions==(failure<0?1:0));
        if(failure<0) {
            assert(sc.sc_rxba_data[8].sta_id==IWM_STATION_ID && sc.sc_rxba_data[8].tid==3);
            aggregate(d,3,false);
            assert(sc.sc_rx_ba_sessions==0 && sc.sc_rxba_data[8].clears==1);
        }
        ++cases;
    }
    if constexpr(std::is_same<Driver,ItlIwx>::value) {
        Driver d; Node node; auto &sc=d.com; sc.sc_ic.ic_bss=&node.in_ni;
        sc.failFlush=EIO;
        assert(stop(d)==EIO && sc.commandCount==0 && sc.laterCleanup==0);
        ++cases;
    }
}
int main(int argc,char **argv)
{
    const char *family=argc>1?argv[1]:"all";
    if(std::strcmp(family,"all")==0 || std::strcmp(family,"iwm")==0) testFamily<ItlIwm,iwm_node>();
    if(std::strcmp(family,"all")==0 || std::strcmp(family,"iwx")==0) testFamily<ItlIwx,iwx_node>();
    assert(cases>0);
    std::printf("actual primary RX BA teardown: %u scenarios PASS\n",cases);
}
