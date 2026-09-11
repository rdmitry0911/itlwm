#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "registers.inc"

#define XYLog(...) do { if (false) std::printf(__VA_ARGS__); } while (0)
#define container_of(p, type, member) reinterpret_cast<type *>(p)
#ifndef htole16
#define htole16(x) (x)
#define htole32(x) (x)
#endif
constexpr size_t kItlApFirmwareMaxClients = 5;
using IOReturn = int;
constexpr int kIOReturnSuccess = 0, kIOReturnError = 1;
constexpr int kIOReturnBusy = 2, kIOReturnNotReady = 3;
struct iwn_softc;
struct mbuf_list {};
#define MBUF_LIST_INITIALIZER() {}
[[maybe_unused]] static void ieee80211_tx_node_retire_drain(int *, mbuf_list *) {}
struct ItlApTxBaRuntime { bool active = true; };
static void itl_ap_tx_ba_reset(ItlApTxBaRuntime *ba) { ba->active = false; }
struct IwnApClientRuntime {
    bool inUse = false;
    uint16_t txBaMask = 0;
    uint8_t txBaQueue[8] = {}, txBaPendingTid = 0, txBaPendingQueue = 0;
    uint16_t txSequence[8] = {}, txBaPendingSsn = 0, txBaPendingOldDisableTid = 0;
    bool txBaEnablePending = false;
    ItlApTxBaRuntime txBa[8];
    struct { int pendingAggregate[8] = {}; } rateControl;
};
struct iwn_tx_data {
    int *m = nullptr;
    bool ap_mgmt = false, ap_data = false, sae_active = false;
};
struct iwn_tx_ring {
    iwn_tx_data data[IWN_TX_RING_COUNT];
    int queued = 0, cur = 0, read = 0;
};
struct iwn_rxon { uint8_t bssid[6] = {}, wlap[6] = {}; uint32_t filter = 0; int mode = 0; };
struct iwn_ops { void (*reset_sched)(iwn_softc *, int, int); };
struct iwn_softc {
    int sc_ic = 0;
    iwn_ops ops;
    struct { char dv_xname[8] = "test"; } sc_dev;
    int command_queue = IWN_IPAN_CMD_QUEUE, ntxqs = 20, first_agg_txq = 10;
    uint32_t agg_queue_mask = 0, qfullmsk = 0;
    struct { void *wn = nullptr; } sc_tx_ba[8];
    iwn_tx_ring txq[20];
    int hw_type = 12, rxonsz = sizeof(iwn_rxon);
    bool locked = false, stopped[20] = {}, failLock = false;
    bool flushDone = false, demandFlush = true;
    int freed = 0, resets = 0, commandError = 0, rxonError = 0;
    std::vector<int> commands;
    uint32_t flushedMask = 0;
    uint32_t sched_base = 0x800000;
    uint32_t scheduler[0x808 / 4] = {};
    unsigned statusClears = 0, pointerWrites = 0;
};
static void reset_sched(iwn_softc *sc, int qid, int) {
    assert(sc->locked && sc->stopped[qid]);
    assert(!sc->demandFlush || sc->flushDone);
    if (sc->hw_type != IWN_HW_REV_TYPE_4965)
        for (int word = 0; word < 4; ++word)
            assert(sc->scheduler[IWN5000_SCHED_TX_STATUS_OFFSET(qid) / 4 + word] == 0);
    ++sc->resets;
}
class ItlIwn {
public:
    iwn_softc com;
    IwnApClientRuntime apClients[kItlApFirmwareMaxClients];
    bool apFirmwareTransitionActive = true, apFirmwareDeactivationReplySeen = false;
    bool apFirmwareDeactivationNotificationSeen = false, apFirmwarePostDeactivateQueued = false;
    uint8_t apFirmwareStage = IWN_AP_STAGE_RUNNING;
    uint16_t apStopTxFlushIndex = 0;
    uint32_t apStopTxQueueMask = 0;
    iwn_rxon apFirmwareRxon;
    bool supported = true, scanBlocked = false;
    int scanResult = kIOReturnSuccess, resets = 0;
    ItlIwn() {
        com.ops.reset_sched = reset_sched;
        for (size_t word = 0; word < sizeof(com.scheduler) / sizeof(uint32_t); ++word)
            com.scheduler[word] = 0x5ca00000U + word;
    }
    ~ItlIwn() { for (auto &ring : com.txq) for (auto &data : ring.data) delete data.m; }
    bool iwn_ampdu_txq_can_advance(const iwn_tx_ring *, int) const;
    bool iwn_ampdu_txq_advance(iwn_softc *, iwn_tx_ring *, int, int
#if IWN_AP_STOP_BATCH
        , mbuf_list * = nullptr
#endif
    );
    void iwn_ap_ampdu_tx_stop(int, uint8_t, uint16_t);
    int iwn_ap_stop_tx_queue_mask(uint32_t *) const;
    int iwn_retire_flushed_ap_tx();
    int iwn_continue_ap_stop_after_flush();
    void iwn_note_ap_stop_tx_flush(int, uint16_t, bool);
    IOReturn stopAPMode();
    bool supportsAPMode() const { return supported; }
    IOReturn iwn_quiesce_scan_for_ap_transition() { scanBlocked = true; return scanResult; }
    void iwn_select_ap_client(IwnApClientRuntime *) {}
    void iwn_clear_ap_sae_pmksa() {}
    void iwn_set_ap_scan_transition_blocked(bool value) { scanBlocked = value; }
    void iwn_reset_ap_runtime_state() { ++resets; apFirmwareStage = IWN_AP_STAGE_IDLE; }
    static int iwn_nic_lock(iwn_softc *sc) {
        if (sc->failLock) return EIO;
        assert(!sc->locked);
        sc->locked = true;
        return 0;
    }
    static void iwn_nic_unlock(iwn_softc *sc) { assert(sc->locked); sc->locked = false; }
    static void iwn_prph_write(iwn_softc *sc, int address, int value) {
        assert(sc->locked);
        for (int q = 0; q < sc->ntxqs; ++q) {
            const bool old = sc->hw_type == IWN_HW_REV_TYPE_4965;
            if (address == (old ? IWN4965_SCHED_QUEUE_STATUS(q) : IWN5000_SCHED_QUEUE_STATUS(q)) &&
                value == (old ? IWN4965_TXQ_STATUS_CHGACT : IWN5000_TXQ_STATUS_CHGACT))
                sc->stopped[q] = true;
        }
    }
    static void iwn_prph_clrbits(iwn_softc *sc, int, int) { assert(sc->locked); }
    static void iwn_mem_set_region_4(iwn_softc *sc, uint32_t address,
                                     uint32_t value, int count) {
        assert(sc->locked && sc->hw_type != IWN_HW_REV_TYPE_4965);
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
        assert(sc->hw_type == IWN_HW_REV_TYPE_4965);
        ++sc->pointerWrites;
        assert(sc->txq[qid].queued == 0);
        for (const auto &data : sc->txq[qid].data)
            assert(data.m == nullptr && !data.ap_data && !data.ap_mgmt);
    }
    static void iwn_sae_tx_report_terminal(iwn_softc *, iwn_tx_data *, int) { assert(false); }
    static void iwn_tx_done_free_txdata(iwn_softc *sc, iwn_tx_data *data
#if IWN_AP_STOP_BATCH
        , mbuf_list *
#endif
    ) {
        assert(data->ap_data || data->ap_mgmt);
        if (data->m) { delete data->m; ++sc->freed; }
        *data = {};
    }
    static int iwn_cmd(iwn_softc *sc, int code, const void *buf, int size, int async) {
        assert(async == 1 && code == IWN_CMD_WIPAN_RXON);
        assert(sc->flushDone); // Never destroy PAN while its DMA is still live.
        assert(size == sizeof(iwn_rxon));
        const auto &rxon = *static_cast<const iwn_rxon *>(buf);
        assert(rxon.mode == IWN_MODE_P2P && rxon.filter == 0);
        for (uint8_t b : rxon.bssid) assert(b == 0);
        for (uint8_t b : rxon.wlap) assert(b == 0);
        if (sc->rxonError) return sc->rxonError;
        sc->commands.push_back(code);
        return 0;
    }
    static int iwn_cmd_with_doorbell_hook(
        iwn_softc *sc, int code, const void *buf, int size, int async,
        bool (*pre)(iwn_softc *, void *), void (*post)(iwn_softc *, void *), void *context) {
        assert(async == 1 && code == IWN_CMD_TXFIFO_FLUSH && post == nullptr);
        assert(size == 8 && size == sizeof(iwn_txfifo_flush_cmd));
        const auto &flush = *static_cast<const iwn_txfifo_flush_cmd *>(buf);
        assert(flush.flush_control == IWN_TXFIFO_FLUSH_DROP_ALL && flush.reserved == 0);
        if (sc->commandError) return sc->commandError;
        assert(pre(sc, context));
        sc->flushedMask = flush.queue_control;
        sc->commands.push_back(code);
        sc->txq[sc->command_queue].cur = (sc->txq[sc->command_queue].cur + 1) & 255;
        return 0;
    }
};
#define IWN_WRITE(sc, reg, value) ItlIwn::write(sc, reg, value)
#include "production.inc"

static void seed(ItlIwn &hal, int client, int tid, int qid, int read, int count, bool transferred = false) {
    auto &peer = hal.apClients[client];
    peer.inUse = true;
    peer.txBaMask |= 1U << tid;
    peer.txBaQueue[tid] = qid;
    peer.txSequence[tid] = 92; // Deliberately different from physical write cursor.
    hal.com.agg_queue_mask |= 1U << qid;
    hal.com.qfullmsk |= 1U << qid;
    auto &ring = hal.com.txq[qid];
    ring.read = read; ring.cur = (read + count) & 255; ring.queued = count;
    for (int n = 0; n < count; ++n) {
        auto &data = ring.data[(read + n) & 255];
        data.ap_data = true;
        if (!transferred || n != 0) data.m = new int(n);
    }
}
static void accepted_flush(ItlIwn &hal) {
    hal.com.flushDone = true;
    hal.iwn_note_ap_stop_tx_flush(IWN_CMD_TXFIFO_FLUSH, hal.apStopTxFlushIndex, false);
}
static void check_scheduler(const iwn_softc &sc, uint32_t clearedQueues) {
    for (size_t word = 0; word < sizeof(sc.scheduler) / sizeof(uint32_t); ++word) {
        const bool status = word * 4 >= 0x6a0 && word * 4 < 0x7e0;
        const int qid = status ? (word * 4 - 0x6a0) / 16 : 0;
        const uint32_t expected = status && (clearedQueues & (1U << qid)) ?
            0 : 0x5ca00000U + word;
        assert(sc.scheduler[word] == expected);
    }
}
static void protocol_cases() {
    ItlIwn hal;
    seed(hal, 0, 0, 12, 38, 3);
    seed(hal, 1, 7, 19, 250, 8, true);
    auto &sc = hal.com;
    sc.sc_tx_ba[0].wn = &hal;
    sc.agg_queue_mask |= 1U << 10;
    sc.qfullmsk |= 1U << 10;
    sc.txq[10].queued = 7; sc.txq[10].read = 12; sc.txq[10].cur = 19;
    sc.txq[9].cur = 255;
    hal.apClients[2].txBaEnablePending = true;
    hal.apClients[2].txBaPendingQueue = 13;
    assert(hal.stopAPMode() == kIOReturnNotReady);
    assert(hal.apFirmwareStage == IWN_AP_STAGE_STOP_TX_FLUSH);
    assert(sc.commands.size() == 1 && sc.commands[0] == IWN_CMD_TXFIFO_FLUSH);
    assert(sc.flushedMask == (0x1f0U | (1U << 12) | (1U << 19)));
    assert(hal.apStopTxFlushIndex == 255 && sc.freed == 0);
    assert(hal.stopAPMode() == kIOReturnNotReady && sc.commands.size() == 1);
    hal.iwn_note_ap_stop_tx_flush(IWN_CMD_WIPAN_RXON, 255, false);
    hal.iwn_note_ap_stop_tx_flush(IWN_CMD_TXFIFO_FLUSH, 254, false);
    assert(sc.freed == 0 && sc.commands.size() == 1);
    sc.failLock = true;
    accepted_flush(hal);
    assert(hal.apFirmwareStage == IWN_AP_STAGE_STOP_TX_RETIRE);
    assert(sc.freed == 0 && hal.apClients[0].txBaMask == 1);
    check_scheduler(sc, 0);
    assert(hal.apClients[2].txBaEnablePending);
    assert(hal.stopAPMode() == kIOReturnNotReady && sc.freed == 0);
    sc.failLock = false;
    sc.rxonError = ENOBUFS;
    assert(hal.stopAPMode() == kIOReturnNotReady);
    assert(sc.freed == 10 && sc.resets == 11);
    assert(hal.apFirmwareStage == IWN_AP_STAGE_STOP_TX_RETIRE);
    assert(hal.apClients[0].txBaMask == 0 && hal.apClients[1].txBaMask == 0);
    assert(!hal.apClients[2].txBaEnablePending);
    sc.rxonError = 0;
    assert(hal.stopAPMode() == kIOReturnNotReady);
    assert(hal.apFirmwareStage == IWN_AP_STAGE_STOP_RXON);
    assert(sc.freed == 10 && sc.resets == 11 && sc.commands.size() == 2);
    assert(sc.txq[12].queued == 0 && sc.txq[19].queued == 0);
    assert(sc.agg_queue_mask == (1U << 10) && sc.qfullmsk == (1U << 10));
    assert(sc.txq[10].queued == 7 && sc.txq[10].read == 12 && sc.txq[10].cur == 19);
    assert(!sc.stopped[10] && sc.sc_tx_ba[0].wn == &hal);
    check_scheduler(sc, (1U << 12) | (1U << 19));
    assert(sc.statusClears == 2 && sc.pointerWrites == 0);
    accepted_flush(hal); // Late duplicate is not another retirement.
    assert(sc.freed == 10 && sc.commands.size() == 2);
    assert(hal.stopAPMode() == kIOReturnNotReady && hal.resets == 0);
}
static void failure_cases() {
    ItlIwn hal;
    seed(hal, 0, 0, 12, 29, 1);
    hal.com.commandError = EIO;
    assert(hal.stopAPMode() == kIOReturnError);
    assert(hal.apFirmwareStage == IWN_AP_STAGE_RUNNING && !hal.scanBlocked);
    assert(hal.com.freed == 0 && hal.apClients[0].txBaMask == 1);
    hal.com.commandError = 0;
    assert(hal.stopAPMode() == kIOReturnNotReady);
    hal.iwn_note_ap_stop_tx_flush(IWN_CMD_TXFIFO_FLUSH, hal.apStopTxFlushIndex, true);
    assert(hal.apFirmwareStage == IWN_AP_STAGE_RUNNING && hal.com.freed == 0);
    assert(hal.stopAPMode() == kIOReturnNotReady);
    // Fixed PAN queues must complete normally; flush ACK does not fake TX_DONE.
    hal.com.txq[8].queued = 1;
    accepted_flush(hal);
    assert(hal.apFirmwareStage == IWN_AP_STAGE_STOP_TX_RETIRE && hal.com.freed == 0);
    hal.com.txq[8].queued = 0; // External real-completion substitute.
    assert(hal.stopAPMode() == kIOReturnNotReady && hal.com.freed == 1);

    ItlIwn bad;
    seed(bad, 0, 0, 12, 0, 1);
    uint32_t mask = 0xdeadbeef;
    bad.com.sc_tx_ba[2].wn = &bad;
    assert(bad.iwn_ap_stop_tx_queue_mask(&mask) == EBUSY && mask == 0xdeadbeef);
    assert(bad.stopAPMode() == kIOReturnError && bad.com.commands.empty());
    bad.com.sc_tx_ba[2].wn = nullptr;
    bad.apClients[1].inUse = true;
    bad.apClients[1].txBaMask = 1;
    bad.apClients[1].txBaQueue[0] = 12;
    assert(bad.iwn_ap_stop_tx_queue_mask(&mask) == EINVAL);
    bad.apClients[1].txBaMask = 0;
    bad.apClients[0].txBaQueue[0] = 255;
    assert(bad.iwn_ap_stop_tx_queue_mask(&mask) == EINVAL);
    bad.apClients[0].txBaQueue[0] = 12;
    bad.apClients[0].inUse = false;
    assert(bad.iwn_ap_stop_tx_queue_mask(&mask) == EINVAL);
    bad.apClients[0].inUse = true;
    bad.com.txq[12].queued = 2;
    assert(bad.iwn_ap_stop_tx_queue_mask(&mask) == EINVAL);
    bad.com.txq[12].queued = 1;
    bad.com.txq[12].cur = bad.com.txq[12].read;
    assert(bad.iwn_ap_stop_tx_queue_mask(&mask) == EINVAL);
    assert(bad.iwn_retire_flushed_ap_tx() == EINVAL);
    ItlIwn idle;
    idle.apFirmwareTransitionActive = false;
    assert(idle.stopAPMode() == kIOReturnSuccess && idle.com.commands.empty());
    ItlIwn scan;
    scan.scanResult = kIOReturnBusy;
    assert(scan.stopAPMode() == kIOReturnBusy && scan.com.commands.empty());
}
static void backend_cases() {
    for (bool old : {false, true}) for (int read : {0, 38, 250, 255})
    for (int count : {0, 1, 3, 213}) for (uint8_t tid = 0; tid < 8; ++tid)
    for (int qid : {11, 12, 19}) {
        if (old && qid != 12) continue;
        ItlIwn hal;
        hal.com.hw_type = old ? IWN_HW_REV_TYPE_4965 : 12;
        seed(hal, 0, tid, qid, read, count, true);
        hal.com.flushDone = true;
        assert(ItlIwn::iwn_nic_lock(&hal.com) == 0);
        hal.iwn_ap_ampdu_tx_stop(qid, tid, 4095);
        ItlIwn::iwn_nic_unlock(&hal.com);
        assert(hal.com.freed == (count ? count - 1 : 0) && hal.com.resets == count);
        assert(hal.com.txq[qid].queued == 0);
        assert(hal.com.txq[qid].read == (old ? 255 : (read + count) % 256));
        assert(hal.com.txq[qid].read == hal.com.txq[qid].cur);
        check_scheduler(hal.com, old ? 0 : 1U << qid);
        assert(hal.com.pointerWrites == (old ? 1U : 0));
        assert(ItlIwn::iwn_nic_lock(&hal.com) == 0);
        hal.iwn_ap_ampdu_tx_stop(qid, tid, 4095);
        ItlIwn::iwn_nic_unlock(&hal.com);
        assert(hal.com.freed == (count ? count - 1 : 0) && hal.com.resets == count);
        check_scheduler(hal.com, old ? 0 : 1U << qid);
    }
}
int main(int argc, char **argv) {
    (void)&iwn_ap_stop_tx_prepare_doorbell;
    if (argc == 2 && std::string(argv[1]) == "--backend") backend_cases();
    else { protocol_cases(); failure_cases(); backend_cases(); }
    std::puts("PASS: actual AP stop waits exact FIFO flush, clears retired SCD status, drains AP-only DMA and preserves STA");
}
