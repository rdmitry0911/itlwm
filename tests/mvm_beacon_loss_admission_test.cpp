#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>
#if defined(__linux__)
#include <endian.h>
#elif defined(__APPLE__)
#include <libkern/OSByteOrder.h>
#define le32toh OSSwapLittleToHostInt32
#define htole32 OSSwapHostToLittleInt32
#endif
#define __packed __attribute__((packed))
#include "registers.inc"
struct iwm_rx_packet;
struct iwx_rx_packet;
enum { IEEE80211_M_STA, IEEE80211_M_HOSTAP, IEEE80211_M_MONITOR };
enum { IEEE80211_S_INIT, IEEE80211_S_SCAN, IEEE80211_S_AUTH,
       IEEE80211_S_ASSOC, IEEE80211_S_RUN };
enum { IFF_DEBUG=1, IEEE80211_EVT_STA_BEACON_LOSS=21,
       IWX_MAX_QUEUES=4, IWX_DQA_CMD_QUEUE=0 };
struct ieee80211_node {
    unsigned ni_associd=1, ni_dtimcount=0, ni_dtimperiod=1;
    uint8_t ni_macaddr[6]={2};
};
struct ieee80211com {
    int ic_opmode=IEEE80211_M_STA, ic_state=IEEE80211_S_RUN;
    unsigned ic_bmissthres=10, ic_mgt_timer=0, ic_flags=0;
    struct { unsigned if_flags=0; } ic_if;
    ieee80211_node *ic_bss=nullptr;
    void (*ic_event_handler)(ieee80211com *,int,void *)=nullptr;
};
struct iwm_node { ieee80211_node in_ni; uint16_t in_id=0, in_color=7; };
struct iwx_node { ieee80211_node in_ni; uint16_t in_id=0, in_color=7; };
struct iwm_rx_data {};
struct iwx_rx_data {};
struct iwx_tx_ring { int qid=0, cur=0, queued=0; };
struct iwm_softc { ieee80211com sc_ic; };
struct iwx_softc {
    ieee80211com sc_ic;
    unsigned sc_flags=0;
    iwx_tx_ring txq[IWX_MAX_QUEUES];
    struct { int cur=0; } rxq;
};
static unsigned armed=0, events=0, scans=0;
static std::vector<unsigned> sequence;
static void logBoundary(const char *,...) {}
#define XYLog(...) logBoundary(__VA_ARGS__)
#define DEVNAME(sc) "beacon-fixture"
#define IWX_FW_CMD_ID_AND_COLOR(id,color) ((id)|((color)<<8))
static const char *ether_sprintf(const uint8_t *) { return "public-bssid"; }
[[maybe_unused]] static const char *ieee80211_state_name[]={"INIT","SCAN","AUTH","ASSOC","RUN"};
static void ieee80211_new_state(ieee80211com *ic,int state,int arg) {
    assert(state==IEEE80211_S_SCAN && arg==-1);
    ++scans; sequence.push_back(3); ic->ic_state=state;
}
static void eventBoundary(ieee80211com *ic,int event,void *data) {
    assert(ic->ic_state==IEEE80211_S_RUN);
    assert(event==IEEE80211_EVT_STA_BEACON_LOSS && data==nullptr);
    ++events; sequence.push_back(2);
}
class ItlIwm {
public:
    iwm_softc com;
    void iwm_rx_bmiss(iwm_softc *,iwm_rx_packet *,iwm_rx_data *);
    bool iwm_sae_bss_loss_arm(ieee80211com *,const ieee80211_node *) {
        ++armed; sequence.push_back(1); return true;
    }
};
class ItlIwx {
public:
    iwx_softc com;
    void iwx_rx_bmiss(iwx_softc *,iwx_rx_packet *,iwx_rx_data *);
    bool iwx_sae_bss_loss_arm(ieee80211com *,const ieee80211_node *) {
        ++armed; sequence.push_back(1); return true;
    }
    void iwx_cmdq_snapshot(iwx_softc *,int *cur,int *queued) {
        *cur=0; *queued=0;
    }
};
#define container_of(ptr,type,member) reinterpret_cast<type *>(ptr)
#include "handler.inc"
#if defined(BEACON_FAMILY_iwm)
using Hal=ItlIwm; using Node=iwm_node; using Packet=iwm_rx_packet;
using Notification=iwm_missed_beacons_notif;
static void deliver(Hal &hal,Packet *packet) {
    hal.iwm_rx_bmiss(&hal.com,packet,nullptr);
}
static const char *family="iwm";
#else
using Hal=ItlIwx; using Node=iwx_node; using Packet=iwx_rx_packet;
using Notification=iwx_missed_beacons_notif;
static void deliver(Hal &hal,Packet *packet) {
    hal.iwx_rx_bmiss(&hal.com,packet,nullptr);
}
static const char *family="iwx";
#endif
static_assert(sizeof(Notification)==20,"missed beacon API v3 size");
struct Fixture {
    Hal hal;
    Node node;
    std::unique_ptr<uint8_t[]> bytes;
    Packet *packet;
    Fixture(unsigned payload=sizeof(Notification),unsigned header=sizeof(packet->hdr)) {
        // Physical storage is exact. Short notifications cannot be inspected
        // beyond their advertised payload, even if the threshold would match.
        const unsigned size=sizeof(Packet)+payload;
        bytes.reset(new uint8_t[size]{});
        packet=reinterpret_cast<Packet *>(bytes.get());
        packet->len_n_flags=htole32(header+payload);
        hal.com.sc_ic.ic_bss=&node.in_ni;
        hal.com.sc_ic.ic_event_handler=eventBoundary;
        if (payload>=sizeof(Notification)) {
            Notification notification{};
            notification.mac_id=htole32(node.in_id);
            notification.consec_missed_beacons_since_last_rx=htole32(11);
            std::memcpy(packet->data,&notification,sizeof(notification));
        }
        armed=events=scans=0; sequence.clear();
    }
    void mac(unsigned id) {
        std::memcpy(packet->data,&id,sizeof(id));
        auto *notification=reinterpret_cast<Notification *>(packet->data);
        notification->mac_id=htole32(id);
    }
    void unchanged() {
        const auto state=hal.com.sc_ic.ic_state;
        deliver(hal,packet);
        assert(armed==0 && events==0 && scans==0);
        assert(hal.com.sc_ic.ic_state==state && sequence.empty());
    }
};
int main() {
    if (std::getenv("BEACON_MAC_REQUIRE")) {
        Fixture f; f.mac(1); f.unchanged();
        std::printf("%s other-MAC negative: PASS\n",family); return 0;
    }
    if (std::getenv("BEACON_SHORT_REQUIRE")) {
        Fixture f(0); f.unchanged();
        std::printf("%s short-payload negative: PASS\n",family); return 0;
    }
    unsigned cases=0;
    for (unsigned id: {0U,1U,2U,3U,255U}) {
        for (unsigned color: {0U,1U,7U,255U}) {
            Fixture f; f.node.in_id=id; f.node.in_color=color; f.mac(id);
            deliver(f.hal,f.packet);
            assert(armed==1 && events==1 && scans==1);
            assert(sequence==std::vector<unsigned>({1,2,3}));
            deliver(f.hal,f.packet);
            assert(armed==1 && events==1 && scans==1); ++cases;
        }
    }
    for (unsigned id: {1U,2U,3U,255U,256U,0xffffffffU}) {
        Fixture f; f.mac(id); f.unchanged(); ++cases;
    }
    for (unsigned payload=0;payload<sizeof(Notification);++payload) {
        Fixture f(payload); f.unchanged(); ++cases;
    }
    for (unsigned header=0;header<sizeof(Packet::hdr);++header) {
        Fixture f(0,header); f.unchanged(); ++cases;
    }
    {
        Fixture f; f.hal.com.sc_ic.ic_bss=nullptr; f.unchanged(); ++cases;
    }
    {
        Fixture f; deliver(f.hal,nullptr);
        assert(armed==0 && events==0 && scans==0); ++cases;
    }
    for (int mode: {IEEE80211_M_HOSTAP,IEEE80211_M_MONITOR}) {
        Fixture f; f.hal.com.sc_ic.ic_opmode=mode; f.unchanged(); ++cases;
    }
    for (int state: {IEEE80211_S_INIT,IEEE80211_S_SCAN,
                    IEEE80211_S_AUTH,IEEE80211_S_ASSOC}) {
        Fixture f; f.hal.com.sc_ic.ic_state=state; f.unchanged(); ++cases;
    }
    for (unsigned missed: {0U,9U,10U}) {
        Fixture f;
        reinterpret_cast<Notification *>(f.packet->data)->
            consec_missed_beacons_since_last_rx=htole32(missed);
        f.unchanged(); ++cases;
    }
    {
        Fixture f; f.hal.com.sc_ic.ic_mgt_timer=1; f.unchanged(); ++cases;
    }
    for (unsigned payload: {20U,24U,64U}) {
        Fixture f(payload); f.hal.com.sc_ic.ic_if.if_flags=IFF_DEBUG;
        deliver(f.hal,f.packet);
        assert(armed==1 && events==1 && scans==1); ++cases;
    }
    std::printf("MVM firmware beacon-loss admission %s: PASS (%u cases; complete handler, real ABI/length functions; callback/firmware boundaries explicit)\n",family,cases);
}
