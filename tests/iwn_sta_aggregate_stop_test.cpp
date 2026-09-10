#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include "registers.inc"

#define XYLog(...) ((void)0)
#define container_of(p, type, member) reinterpret_cast<type *>(p)
#ifndef htole16
#define htole16(x) (x)
#endif

struct iwn_softc;
struct ieee80211_tx_ba { uint16_t ba_winstart = 0, ba_winend = 0; uint64_t ba_bitmap = 1; };
struct ieee80211_node { ieee80211_tx_ba ni_tx_ba[16]; };
struct iwn_node { ieee80211_node ni; uint16_t disable_tid = 0; uint8_t id = 0; };
struct ieee80211com { void *ic_softc = nullptr; };
struct iwn_tx_data {
    int *m = nullptr;
    ieee80211_node *ni = nullptr;
    bool ap_mgmt = false, ap_data = false, sae_active = false;
};
struct iwn_tx_ring {
    iwn_tx_data data[IWN_TX_RING_COUNT];
    int queued = 0, cur = 0, read = 0;
};
struct iwn_node_info { uint8_t id, control; uint32_t flags; uint16_t disable_tid; };
struct iwn_ops {
    void (*reset_sched)(iwn_softc *, int, int);
    void (*ampdu_tx_stop)(iwn_softc *, uint8_t, uint16_t);
    int (*add_node)(iwn_softc *, iwn_node_info *, int);
};
struct iwn_softc {
    ieee80211com sc_ic;
    iwn_ops ops;
    int first_agg_txq = 10, agg_queue_mask = 0;
    struct { iwn_node *wn = nullptr; } sc_tx_ba[16];
    iwn_tx_ring txq[20];
    bool locked = false, stopped[20] = {}, failLock = false;
    int freed = 0, nodeReleases = 0, resets = 0;
    uint32_t sched_base = 0x900000, scheduler[0x808 / 4] = {};
    unsigned statusClears = 0, pointerWrites = 0;
};
static const uint8_t iwn_tid2fifo[8] = {1, 0, 0, 1, 2, 2, 3, 3};
static void reset_sched(iwn_softc *sc, int qid, int idx) {
    assert(sc->locked && sc->stopped[qid]);
    assert(sc->txq[qid].data[idx].m != nullptr);
    if (sc->first_agg_txq == IWN5000_FIRST_AGG_TXQUEUE)
        for (int word = 0; word < 4; ++word)
            assert(sc->scheduler[IWN5000_SCHED_TX_STATUS_OFFSET(qid) / 4 + word] == 0);
    ++sc->resets;
}
static int add_node(iwn_softc *, iwn_node_info *, int) { return 0; }
class ItlIwn {
public:
    iwn_softc com;
    ItlIwn() {
        for (size_t word = 0; word < sizeof(com.scheduler) / sizeof(uint32_t); ++word)
            com.scheduler[word] = 0x5ca00000U + word;
    }
    bool iwn_ampdu_txq_can_advance(const iwn_tx_ring *, int) const;
    bool iwn_ampdu_txq_advance(iwn_softc *, iwn_tx_ring *, int, int);
    static void iwn_ampdu_tx_stop(ieee80211com *, ieee80211_node *, uint8_t);
    static void iwn4965_ampdu_tx_stop(iwn_softc *, uint8_t, uint16_t);
    static void iwn5000_ampdu_tx_stop(iwn_softc *, uint8_t, uint16_t);
    static int iwn_nic_lock(iwn_softc *sc) {
        if (sc->failLock) return EIO;
        sc->locked = true;
        return 0;
    }
    static void iwn_nic_unlock(iwn_softc *sc) { sc->locked = false; }
    static void iwn_prph_write(iwn_softc *sc, int address, int value) {
        assert(sc->locked);
        for (int q = sc->first_agg_txq; q < 20; ++q) {
            const bool old = sc->first_agg_txq == IWN4965_FIRST_AGG_TXQUEUE;
            if (address == (old ? IWN4965_SCHED_QUEUE_STATUS(q) :
                                  IWN5000_SCHED_QUEUE_STATUS(q))) {
                if (value == (old ? IWN4965_TXQ_STATUS_CHGACT :
                                   IWN5000_TXQ_STATUS_CHGACT))
                    sc->stopped[q] = true;
            }
        }
    }
    static void iwn_prph_clrbits(iwn_softc *sc, int, int) { assert(sc->locked); }
    static void iwn_mem_set_region_4(iwn_softc *sc, uint32_t address,
                                     uint32_t value, int count) {
        assert(sc->locked && sc->first_agg_txq == IWN5000_FIRST_AGG_TXQUEUE);
        assert(value == 0 && count == 4);
        const uint32_t offset = address - sc->sched_base;
        assert(offset >= 0x6a0 && offset + 16 <= 0x7e0);
        assert((offset - 0x6a0) % 16 == 0);
        const int qid = (offset - 0x6a0) / 16;
        assert(sc->stopped[qid]);
        for (int word = 0; word < count; ++word)
            sc->scheduler[offset / 4 + word] = value;
        ++sc->statusClears;
    }
    static void write(iwn_softc *sc, int, int value) {
        const int qid = value >> 8;
        assert(sc->locked && sc->stopped[qid]);
        assert(sc->first_agg_txq == IWN4965_FIRST_AGG_TXQUEUE);
        ++sc->pointerWrites;
        // No software descriptor may be abandoned when cursors are rebased.
        assert(sc->txq[qid].queued == 0);
        for (const auto &data : sc->txq[qid].data)
            assert(data.m == nullptr && data.ni == nullptr);
    }
    static void iwn_sae_tx_report_terminal(iwn_softc *, iwn_tx_data *, int) {
        assert(false && "STA data must not acquire a synthetic SAE terminal");
    }
    static void iwn_tx_done_free_txdata(iwn_softc *sc, iwn_tx_data *data) {
        assert(data->m != nullptr && data->ni != nullptr);
        delete data->m;
        data->m = nullptr;
        data->ni = nullptr;
        ++sc->freed;
        ++sc->nodeReleases;
    }
    int iwn_set_link_quality(iwn_softc *, ieee80211_node *) { return 0; }
};
#define IWN_WRITE(sc, reg, value) ItlIwn::write(sc, reg, value)
#include "production.inc"

static void stop_case(bool old, int read, int pending, uint16_t winstart,
                      uint16_t winend, uint8_t tid, bool lockFailure = false) {
    ItlIwn hal;
    auto &sc = hal.com;
    sc.sc_ic.ic_softc = &sc;
    sc.first_agg_txq = old ? IWN4965_FIRST_AGG_TXQUEUE : IWN5000_FIRST_AGG_TXQUEUE;
    sc.ops = {reset_sched, old ? ItlIwn::iwn4965_ampdu_tx_stop :
                               ItlIwn::iwn5000_ampdu_tx_stop, add_node};
    sc.failLock = lockFailure;
    iwn_node node;
    node.ni.ni_tx_ba[tid].ba_winstart = winstart;
    node.ni.ni_tx_ba[tid].ba_winend = winend;
    const int qid = sc.first_agg_txq + tid;
    sc.agg_queue_mask = 1 << qid;
    sc.sc_tx_ba[tid].wn = &node;
    auto &ring = sc.txq[qid];
    ring.read = read;
    ring.cur = (read + pending) & 255;
    ring.queued = pending;
    for (int n = 0; n < pending; ++n) {
        auto &data = ring.data[(read+n) & 255];
        data.m = new int(n);
        data.ni = &node.ni;
    }
    ItlIwn::iwn_ampdu_tx_stop(&sc.sc_ic, &node.ni, tid);
    if (lockFailure) {
        assert(ring.queued == pending && ring.read == read);
        assert(sc.freed == 0 && sc.sc_tx_ba[tid].wn == &node);
        assert(!sc.stopped[qid]);
        assert(sc.statusClears == 0 && sc.pointerWrites == 0);
        sc.failLock = false;
        ItlIwn::iwn_ampdu_tx_stop(&sc.sc_ic, &node.ni, tid);
    }
    assert(ring.queued == 0);
    assert(ring.cur == (old ? (winstart & 255) : (read + pending) % 256));
    assert(ring.read == ring.cur);
    assert(sc.freed == pending && sc.nodeReleases == pending && sc.resets == pending);
    assert(sc.agg_queue_mask == 0 && sc.sc_tx_ba[tid].wn == nullptr);
    assert(node.ni.ni_tx_ba[tid].ba_bitmap == 0 && (node.disable_tid & (1U << tid)));
    assert(!sc.locked);
    assert(sc.statusClears == (old ? 0U : 1U));
    assert(sc.pointerWrites == (old ? 1U : 0U));
    const size_t statusOffset = IWN5000_SCHED_TX_STATUS_OFFSET(qid);
    for (size_t word = 0; word < sizeof(sc.scheduler) / sizeof(uint32_t); ++word) {
        const bool selected = !old && word * 4 >= statusOffset &&
            word * 4 < statusOffset + 16U;
        assert(sc.scheduler[word] == (selected ? 0 : 0x5ca00000U + word));
    }
    // Empty repeated teardown cannot release a descriptor a second time.
    ItlIwn::iwn_ampdu_tx_stop(&sc.sc_ic, &node.ni, tid);
    assert(sc.freed == pending && ring.queued == 0);
}

int main() {
    for (bool old : {false, true}) for (uint8_t tid = 0; tid < 8; ++tid) {
        // One submitted MPDU, but a 64-entry logical BA window: old stop
        // refuses reclaim at 92, then resets both cursors to 29, leaving 1.
        stop_case(old, 29, 1, 29, 92, tid);
        stop_case(old, 250, 8, 250, 313, tid);
        stop_case(old, 255, 1, 4095, 62, tid);
        stop_case(old, 17, 0, 17, 80, tid);
        stop_case(old, 12, 3, 12, 14, tid); // logical end is not exclusive cur
        stop_case(old, 29, 1, 29, 92, tid, true);
    }
    ItlIwn hal;
    iwn_tx_ring ring;
    ring.read = 29; ring.cur = 30; ring.queued = 1;
    assert(!hal.iwn_ampdu_txq_can_advance(&ring, 92));
    assert(hal.iwn_ampdu_txq_can_advance(&ring, 30));
    ring.read = 250; ring.cur = 2;
    assert(hal.iwn_ampdu_txq_can_advance(&ring, 2));
    assert(!hal.iwn_ampdu_txq_can_advance(&ring, 3));
    std::puts("PASS: actual STA BA stop clears DVM SCD status before draining, preserves cursor and adjacent SRAM; 4965 unchanged");
}
