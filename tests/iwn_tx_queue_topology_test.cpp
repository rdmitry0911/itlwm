#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "registers.inc"

#define MIN(a, b) std::min((a), (b))
#define container_of(p, type, member) reinterpret_cast<type *>(p)
struct iwn_softc;
struct ieee80211_tx_ba {
    uint16_t ba_winsize = 64, ba_winstart = 0;
    uint64_t ba_bitmap = 1;
};
struct ieee80211_node { ieee80211_tx_ba ni_tx_ba[16]; };
struct iwn_node { ieee80211_node ni; uint16_t disable_tid = 0xffff; uint8_t id = 0; };
struct ieee80211com { void *ic_softc = nullptr; };
struct iwn_node_info { uint8_t id, control; uint32_t flags; uint16_t disable_tid; };
struct iwn_ops {
    int (*add_node)(iwn_softc *, iwn_node_info *, int);
    void (*ampdu_tx_start)(iwn_softc *, ieee80211_node *, uint8_t, uint16_t);
    void (*ampdu_tx_stop)(iwn_softc *, uint8_t, uint16_t);
};
struct iwn_tx_ring { int cur = 0, read = 0, queued = 0; };
struct iwn_softc {
    int hw_type = IWN_HW_REV_TYPE_6005;
    bool eeprom_pan_capable = false;
    uint32_t tlv_feature_flags = 0;
    int command_queue = 4, first_agg_txq = 10, ntxqs = 20;
    uint32_t agg_queue_mask = 0;
    ieee80211com sc_ic;
    iwn_ops ops;
    struct { iwn_node *wn = nullptr; } sc_tx_ba[16];
    iwn_tx_ring txq[20];
    uint32_t sched_base = 0x900000;
    uint8_t sram[0x900] = {};
    bool locked = false, deactivated = false, activated = false;
    int expectedQueue = -1, nodeCalls = 0, writes = 0, linkCalls = 0;
    uint16_t expectedSsn = 0;
    uint8_t expectedTid = 0, expectedStation = 0;
};
static const uint8_t iwn_tid2fifo[8] = {1, 0, 0, 1, 2, 2, 3, 3};
static int add_node(iwn_softc *sc, iwn_node_info *node, int async) {
    assert(async == 1 && node->id == sc->expectedStation);
    assert(node->control == IWN_NODE_UPDATE && node->flags == IWN_FLAG_SET_DISABLE_TID);
    assert((node->disable_tid & (1U << sc->expectedTid)) == 0);
    ++sc->nodeCalls;
    return 0;
}
static void unused_stop(iwn_softc *, uint8_t, uint16_t) { assert(false); }
class ItlIwn {
public:
    iwn_softc com;
    static int iwn_ampdu_tx_start(ieee80211com *, ieee80211_node *, uint8_t);
    static void iwn5000_ampdu_tx_start(iwn_softc *, ieee80211_node *, uint8_t, uint16_t);
    static int iwn_nic_lock(iwn_softc *sc) { assert(!sc->locked); sc->locked = true; return 0; }
    static void iwn_nic_unlock(iwn_softc *sc) { assert(sc->locked); sc->locked = false; }
    static void iwn_prph_write(iwn_softc *sc, uint32_t address, uint32_t value) {
        assert(sc->locked);
        ++sc->writes;
        if (address == static_cast<uint32_t>(IWN5000_SCHED_QUEUE_STATUS(sc->expectedQueue))) {
            if (!sc->deactivated) {
                assert(value == IWN5000_TXQ_STATUS_CHGACT);
                sc->deactivated = true;
            } else {
                assert(value == (IWN5000_TXQ_STATUS_ACTIVE | iwn_tid2fifo[sc->expectedTid]));
                sc->activated = true;
            }
        } else {
            assert(address == static_cast<uint32_t>(IWN5000_SCHED_QUEUE_RDPTR(sc->expectedQueue)));
            assert(sc->deactivated && value == sc->expectedSsn);
        }
    }
    static void iwn_prph_setbits(iwn_softc *sc, uint32_t address, uint32_t bits) {
        assert(sc->locked && sc->deactivated);
        assert(address == IWN5000_SCHED_QCHAIN_SEL || address == IWN5000_SCHED_AGGR_SEL ||
               address == IWN5000_SCHED_INTR_MASK);
        assert(bits == (1U << sc->expectedQueue));
        ++sc->writes;
    }
    static void iwn_mem_write_2(iwn_softc *sc, uint32_t address, uint16_t value) {
        assert(sc->locked && sc->deactivated);
        assert(address == sc->sched_base + IWN5000_SCHED_TRANS_TBL(sc->expectedQueue));
        assert(value == (sc->expectedStation << 4 | sc->expectedTid));
        std::memcpy(sc->sram + (address - sc->sched_base), &value, sizeof(value));
        ++sc->writes;
    }
    static void iwn_mem_write(iwn_softc *sc, uint32_t address, uint32_t value) {
        assert(sc->locked && sc->deactivated);
        const uint32_t context = sc->sched_base + IWN5000_SCHED_QUEUE_OFFSET(sc->expectedQueue);
        assert(address == context || address == context + 4);
        std::memcpy(sc->sram + (address - sc->sched_base), &value, sizeof(value));
        ++sc->writes;
    }
    static void write(iwn_softc *sc, uint32_t reg, uint32_t value) {
        assert(reg == IWN_HBUS_TARG_WRPTR && sc->locked && sc->deactivated);
        assert(value == ((static_cast<uint32_t>(sc->expectedQueue) << 8) | (sc->expectedSsn & 255)));
        ++sc->writes;
    }
    int iwn_set_link_quality(iwn_softc *sc, ieee80211_node *ni) {
        assert(!sc->locked && sc->activated);
        assert(sc->sc_tx_ba[sc->expectedTid].wn == reinterpret_cast<iwn_node *>(ni));
        assert((sc->agg_queue_mask & (1U << sc->expectedQueue)) != 0);
        ++sc->linkCalls;
        return 0;
    }
};
#define IWN_WRITE(sc, reg, value) ItlIwn::write(sc, reg, value)
#include "production.inc"

static void start_case(bool pan, uint8_t tid, uint16_t ssn, uint16_t window, bool occupied) {
    ItlIwn hal;
    auto &sc = hal.com;
    sc.eeprom_pan_capable = pan;
    sc.tlv_feature_flags = IWN_UCODE_TLV_FLAGS_PAN;
    iwn_configure_tx_queue_topology(&sc);
    sc.sc_ic.ic_softc = &sc;
    sc.ops = {add_node, ItlIwn::iwn5000_ampdu_tx_start, unused_stop};
    sc.expectedQueue = (pan ? 11 : 10) + tid;
    sc.expectedTid = tid; sc.expectedSsn = ssn; sc.expectedStation = 0;
    std::memset(sc.sram, 0xa7, sizeof(sc.sram));
    for (int qid = 0; qid < 20; ++qid) {
        sc.txq[qid].cur = sc.txq[qid].read = 80 + qid;
        if (qid != sc.expectedQueue) sc.agg_queue_mask |= 1U << qid;
    }
    const uint32_t otherOwners = sc.agg_queue_mask;
    if (occupied) sc.agg_queue_mask |= 1U << sc.expectedQueue;
    iwn_node node;
    node.ni.ni_tx_ba[tid].ba_winstart = ssn;
    node.ni.ni_tx_ba[tid].ba_winsize = window;
    const int result = ItlIwn::iwn_ampdu_tx_start(&sc.sc_ic, &node.ni, tid);
    if (occupied) {
        assert(result == ENOSPC && sc.writes == 0 && sc.nodeCalls == 0);
        assert(!sc.locked && !sc.activated && sc.linkCalls == 0);
    } else {
        assert(result == 0 && sc.nodeCalls == 1 && sc.linkCalls == 1);
        assert(sc.activated && !sc.locked && sc.writes == 10);
        assert((sc.agg_queue_mask & otherOwners) == otherOwners);
    }
    for (int qid = 0; qid < 20; ++qid) {
        const int expected = !occupied && qid == sc.expectedQueue ? (ssn & 255) : 80 + qid;
        assert(sc.txq[qid].cur == expected && sc.txq[qid].read == expected);
        assert(sc.txq[qid].queued == 0);
    }
    uint8_t expectedSram[sizeof(sc.sram)];
    std::memset(expectedSram, 0xa7, sizeof(expectedSram));
    if (!occupied) {
        const uint16_t ratid = sc.expectedStation << 4 | tid;
        std::memcpy(expectedSram + IWN5000_SCHED_TRANS_TBL(sc.expectedQueue), &ratid, 2);
        const uint32_t zero = 0;
        const uint32_t frames = window == 0 ? 63 : std::min<unsigned>(window, 63);
        const uint32_t limit = frames << 16 | frames;
        std::memcpy(expectedSram + IWN5000_SCHED_QUEUE_OFFSET(sc.expectedQueue), &zero, 4);
        std::memcpy(expectedSram + IWN5000_SCHED_QUEUE_OFFSET(sc.expectedQueue) + 4, &limit, 4);
    }
    assert(std::memcmp(sc.sram, expectedSram, sizeof(sc.sram)) == 0);
}

int main() {
    // Each reset/firmware read must replace both fields, including stale PAN state.
    for (int hw = 0; hw < 32; ++hw) for (bool eeprom : {false, true})
      for (bool firmware : {false, true}) {
        iwn_softc sc;
        sc.hw_type = hw; sc.eeprom_pan_capable = eeprom;
        sc.tlv_feature_flags = firmware ? IWN_UCODE_TLV_FLAGS_PAN : 0;
        sc.command_queue = -1; sc.first_agg_txq = -1;
        iwn_configure_tx_queue_topology(&sc);
        const bool pan = hw != IWN_HW_REV_TYPE_4965 && eeprom && firmware;
        assert(sc.command_queue == (pan ? 9 : 4));
        assert(sc.first_agg_txq == (hw == IWN_HW_REV_TYPE_4965 ? 7 : pan ? 11 : 10));
        sc.tlv_feature_flags = 0;
        iwn_configure_tx_queue_topology(&sc);
        assert(sc.command_queue == 4);
        assert(sc.first_agg_txq == (hw == IWN_HW_REV_TYPE_4965 ? 7 : 10));
    }
    for (bool pan : {false, true}) for (uint8_t tid = 0; tid < 8; ++tid)
      for (uint16_t ssn : {0, 29, 250, 255, 4095})
        for (uint16_t window : {0, 1, 32, 63, 64}) {
            start_case(pan, tid, ssn, window, false);
            start_case(pan, tid, ssn, window, true);
        }
    std::puts("PASS: production queue topology and STA scheduler preserve PAN AUX, all TIDs, neighbor SRAM/rings and existing queue owners");
}
