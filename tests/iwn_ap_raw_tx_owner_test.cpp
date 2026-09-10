#include <array>
#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <sys/types.h>
#define __packed __attribute__((packed))
#include "wire.inc"
#include "itlwm/hal_iwn/if_iwnreg.h"

#define IEEE80211_ADDR_EQ(a, b) (std::memcmp((a), (b), 6) == 0)
#define IEEE80211_ADDR_COPY(a, b) std::memcpy((a), (b), 6)
#define IEEE80211_IS_MULTICAST(a) (((a)[0] & 1) != 0)
#define LE_READ_2(p) (uint16_t((p)[0]) | (uint16_t((p)[1]) << 8))
#define LE_WRITE_2(p, n) do { (p)[0] = uint8_t(n); (p)[1] = uint8_t((n) >> 8); } while (0)
constexpr unsigned MCLBYTES = 2048, MBUF_DONTWAIT = 0;
constexpr int IWN_POST_PLTI_TRACE_TX_NONE = 0;
using bus_addr_t = uint64_t;
struct Packet { uint8_t bytes[4096] = {}; size_t offset = 0, len = 0; };
using mbuf_t = Packet *;
static bool failAllocation;
static int mbuf_allocpacket(int, size_t, unsigned *, mbuf_t *out) {
    if (failAllocation) return ENOMEM;
    *out = new Packet;
    return 0;
}
static void mbuf_setlen(mbuf_t m, size_t n) { m->len = n; }
static void mbuf_pkthdr_setlen(mbuf_t m, size_t n) { m->len = n; }
static void mbuf_adj(mbuf_t m, size_t n) { m->offset += n; m->len -= n; }
#define mtod(m, type) reinterpret_cast<type>((m)->bytes + (m)->offset)

struct iwn_tx_data {
    mbuf_t m = nullptr;
    void *ni = nullptr;
    int totlen = 0, ampdu_txmcs = 0, ampdu_nframes = 0;
    int ampdu_rate_generation = 0, ampdu_rate_rflags = 0;
    int ampdu_rate_feedback_valid = 0, tx_apple_nrate = 0;
    int tx_apple_nrate_valid = 0, post_plti_trace_class = 0;
    bool ap_mgmt = false, ap_data = false;
    uint8_t diag_subtype = 0, diag_peer[6] = {};
    uint16_t diag_auth_seq = 0;
    bus_addr_t cmd_paddr = 0;
};
static void iwn_sae_tx_data_clear(iwn_tx_data *) {}
struct iwn_tx_ring {
    int qid = 0, cur = 0, queued = 0;
    std::array<iwn_tx_desc, IWN_TX_RING_COUNT> desc{};
    std::array<iwn_tx_cmd, IWN_TX_RING_COUNT> cmd{};
    std::array<iwn_tx_data, IWN_TX_RING_COUNT> data{};
    std::unique_ptr<uint8_t[]> firstStorage, payloadStorage;
    uint8_t *first_tb = nullptr, *ap_payload = nullptr;
    struct { bus_addr_t paddr = 0; } first_tb_dma, ap_payload_dma;
    void provision(int id) {
        qid = id;
        firstStorage.reset(new uint8_t[IWN_TX_RING_COUNT * IWN_TX_FIRST_TB_STRIDE]{});
        payloadStorage.reset(new uint8_t[IWN_TX_RING_COUNT * IWN_AP_DATA_PAYLOAD_SIZE]{});
        first_tb = firstStorage.get(); ap_payload = payloadStorage.get();
        first_tb_dma.paddr = 0x100000 + id * 0x100000;
        ap_payload_dma.paddr = 0x2000000 + id * 0x100000;
        for (unsigned i = 0; i < data.size(); ++i)
            data[i].cmd_paddr = 0x4000000 + id * 0x100000 + i * sizeof(iwn_tx_cmd);
    }
    ~iwn_tx_ring() { for (auto &slot : data) delete slot.m; }
};
struct iwn_softc {
    iwn_tx_ring txq[20];
    int command_queue = IWN_IPAN_CMD_QUEUE, ntxqs = 20, hw_type = 12;
    uint32_t qfullmsk = 0;
    uint8_t txchainmask = IWN_ANT_AB;
    struct { void (*update_sched)(iwn_softc *, int, int, uint8_t, uint16_t); } ops;
    unsigned schedules = 0, doorbells = 0, timerRefresh = 0;
    int lastQueue = -1, lastIndex = -1;
    uint8_t lastStation = 0;
    uint16_t lastLength = 0;
};
static void sched(iwn_softc *sc, int q, int index, uint8_t sta, uint16_t len) {
    ++sc->schedules; sc->lastQueue = q; sc->lastIndex = index;
    sc->lastStation = sta; sc->lastLength = len;
}
static void doorbell(iwn_softc *sc, int reg, int value) {
    assert(reg == IWN_HBUS_TARG_WRPTR);
    assert((value >> 8) == sc->lastQueue);
    assert((value & 255) == ((sc->lastIndex + 1) & 255));
    ++sc->doorbells;
}
#undef IWN_WRITE
#define IWN_WRITE(sc, reg, value) doorbell(sc, reg, value)
static void iwn_refresh_tx_timer(iwn_softc *sc) { ++sc->timerRefresh; }
struct IwnApClientRuntime {
    bool authorized = true, installed = true;
    uint8_t mac[6] = {2, 1, 2, 3, 4, 5}, stationId = 2;
    uint64_t pairwiseTxPn = 0;
    struct { uint8_t tk[16] = {0x12, 0x34}; } ptk;
};
class ItlIwn {
public:
    iwn_softc com;
    bool apFirmwareTransitionActive = true;
    uint8_t apFirmwareStage = IWN_AP_STAGE_RUNNING;
    struct { uint8_t bssid[6] = {2, 5, 4, 3, 2, 1}; unsigned channel = 9; } apFirmwareConfig;
    IwnApClientRuntime client;
    IwnApClientRuntime *apClientContext = &client;
    bool clientPresent = true;
    bool &apClientNodeInstalled = client.installed;
    uint8_t *apClientMac = client.mac;
    uint64_t &apPairwiseTxPn = client.pairwiseTxPn;
    decltype(client.ptk) &apPtk = client.ptk;
    ItlIwn() { com.ops.update_sched = sched; com.txq[5].provision(5); com.txq[7].provision(7); }
    IwnApClientRuntime *iwn_find_ap_client(const uint8_t *mac) {
        return clientPresent && IEEE80211_ADDR_EQ(mac, client.mac) ? &client : nullptr;
    }
    void iwn_select_ap_client(IwnApClientRuntime *selected) { assert(selected == &client); }
    int iwn_send_ap_mgmt_frame(const void *, size_t);
    int iwn_send_ap_raw_frame(const void *, size_t);
};
#include "production.inc"

static std::array<uint8_t, 64> frame(ItlIwn &hal, uint8_t subtype, bool protectedFrame) {
    std::array<uint8_t, 64> bytes{};
    auto &wh = *reinterpret_cast<ieee80211_frame *>(bytes.data());
    wh.i_fc[0] = subtype;
    wh.i_fc[1] = protectedFrame ? IEEE80211_FC1_PROTECTED : 0;
    IEEE80211_ADDR_COPY(wh.i_addr1, hal.client.mac);
    IEEE80211_ADDR_COPY(wh.i_addr2, hal.apFirmwareConfig.bssid);
    IEEE80211_ADDR_COPY(wh.i_addr3, hal.apFirmwareConfig.bssid);
    bytes[24] = 3; bytes[25] = 1; // BlockAck action body in the action case.
    return bytes;
}
static void submit_cases() {
    for (bool installed : {false, true}) for (bool protectedFrame : {false, true})
    for (unsigned channel : {9U, 153U}) for (uint8_t subtype : {
         uint8_t(IEEE80211_FC0_SUBTYPE_AUTH), uint8_t(IEEE80211_FC0_SUBTYPE_ASSOC_RESP),
         uint8_t(IEEE80211_FC0_SUBTYPE_ACTION), uint8_t(IEEE80211_FC0_SUBTYPE_PROBE_RESP)}) {
        auto hal = std::make_unique<ItlIwn>();
        hal->client.installed = installed;
        hal->client.stationId = 6; // Not a privileged first-client identity.
        hal->apFirmwareConfig.channel = channel;
        hal->com.txq[7].cur = 255;
        auto bytes = frame(*hal, subtype, protectedFrame);
        assert(hal->iwn_send_ap_mgmt_frame(bytes.data(), bytes.size()) == 0);
        const auto &sc = hal->com;
        assert(sc.schedules == 1 && sc.doorbells == 1 && sc.timerRefresh == 1);
        assert(sc.lastQueue == 7 && sc.lastIndex == 255);
        // Intel DVM's actual non-data producer never borrows a data STA ID.
        assert(sc.lastStation == IWN5000_ID_PAN_BROADCAST);
        assert(sc.txq[7].cur == 0 && sc.txq[7].queued == 1);
        const auto &cmd = sc.txq[7].cmd[255];
        const auto &tx = *reinterpret_cast<const iwn_cmd_data *>(cmd.data);
        assert(tx.id == IWN5000_ID_PAN_BROADCAST);
        assert((tx.flags & IWN_TX_NEED_ACK) != 0);
        assert(tx.security == (protectedFrame ? IWN_CIPHER_CCMP : 0));
        assert(hal->client.pairwiseTxPn == (protectedFrame ? 1U : 0U));
        if (protectedFrame) assert(std::memcmp(tx.key, hal->client.ptk.tk, sizeof(tx.key)) == 0);
        assert(sc.txq[7].desc[255].nsegs == 3);
        assert(sc.lastLength == bytes.size() + (protectedFrame ? 16U : 0U));
        assert(sc.txq[5].queued == 0 && sc.txq[5].cur == 0);
    }
}
static void rejection_cases() {
    auto hal = std::make_unique<ItlIwn>();
    auto bytes = frame(*hal, IEEE80211_FC0_SUBTYPE_ACTION, true);
    hal->clientPresent = false;
    assert(hal->iwn_send_ap_mgmt_frame(bytes.data(), bytes.size()) == EACCES);
    hal->clientPresent = true; hal->client.authorized = false;
    assert(hal->iwn_send_ap_mgmt_frame(bytes.data(), bytes.size()) == EACCES);
    hal->client.authorized = true; hal->client.pairwiseTxPn = 0xffffffffffffULL;
    assert(hal->iwn_send_ap_mgmt_frame(bytes.data(), bytes.size()) == EACCES);
    hal->client.pairwiseTxPn = 0; hal->apFirmwareStage = IWN_AP_STAGE_STOP_TX_FLUSH;
    assert(hal->iwn_send_ap_mgmt_frame(bytes.data(), bytes.size()) == EINVAL);
    hal->apFirmwareStage = IWN_AP_STAGE_RUNNING; hal->com.qfullmsk = 1U << 7;
    assert(hal->iwn_send_ap_mgmt_frame(bytes.data(), bytes.size()) == ENOBUFS);
    hal->com.qfullmsk = 0; failAllocation = true;
    assert(hal->iwn_send_ap_mgmt_frame(bytes.data(), bytes.size()) == ENOMEM);
    failAllocation = false;
    assert(hal->com.schedules == 0 && hal->com.doorbells == 0);
    assert(hal->client.pairwiseTxPn == 0);
}
static void bar_cases() {
    auto hal = std::make_unique<ItlIwn>();
    std::array<uint8_t, sizeof(ieee80211_frame_min) + 4> bytes{};
    auto &wh = *reinterpret_cast<ieee80211_frame_min *>(bytes.data());
    wh.i_fc[0] = IEEE80211_FC0_TYPE_CTL | IEEE80211_FC0_SUBTYPE_BAR;
    IEEE80211_ADDR_COPY(wh.i_addr1, hal->client.mac);
    IEEE80211_ADDR_COPY(wh.i_addr2, hal->apFirmwareConfig.bssid);
    bytes[17] = 0x70; // TID 7, unchanged control-frame wire carrier.
    assert(hal->iwn_send_ap_mgmt_frame(bytes.data(), bytes.size()) == EINVAL);
    hal->client.installed = false;
    assert(hal->iwn_send_ap_raw_frame(bytes.data(), bytes.size()) == EHOSTUNREACH);
    hal->client.installed = true;
    assert(hal->iwn_send_ap_raw_frame(bytes.data(), bytes.size()) == 0);
    const auto &sc = hal->com;
    assert(sc.lastQueue == 5 && sc.lastStation == IWN5000_ID_PAN_BROADCAST);
    const auto &tx = *reinterpret_cast<const iwn_cmd_data *>(sc.txq[5].cmd[0].data);
    assert(tx.tid == 7 && tx.security == 0);
    assert((tx.flags & (IWN_TX_NEED_ACK | IWN_TX_IMM_BA | IWN_TX_LINKQ)) ==
        (IWN_TX_NEED_ACK | IWN_TX_IMM_BA | IWN_TX_LINKQ));
    assert(sc.txq[7].queued == 0);
}
int main() {
    submit_cases(); rejection_cases(); bar_cases();
    std::puts("PASS: production AP raw TX station ownership, PMF and rejected submissions");
}
