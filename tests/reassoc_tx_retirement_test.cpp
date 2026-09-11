// Execute complete IWM/IWX aggregate, descriptor, reset and free bodies plus
// the real common release/ref helpers. Firmware delivery, DMA primitives and
// scheduler writes are explicit boundaries; this is not hardware qualification.
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <functional>
#if defined(__APPLE__)
#include <libkern/OSByteOrder.h>
#define le16toh(x) OSSwapLittleToHostInt16(x)
#else
#include <endian.h>
#endif
using u_int = unsigned;
constexpr int IEEE80211_STA_CACHE = 1, IEEE80211_STA_COLLECT = 3;
constexpr int IWM_TX_RING_COUNT = 2, IWM_FIRST_AGG_TX_QUEUE = 2;
constexpr int IWM_DEVICE_FAMILY_7000 = 7000, IWX_INVALID_QUEUE = -1;
#ifndef NBBY
#define NBBY 8
#endif
#define KASSERT(condition, message) assert((condition) && (message))
#define DPRINTF(args) ((void)0)
#define IWX_AGG_SSN_TO_TXQ_IDX(ssn, count) ((ssn) % (count))
#define container_of(ptr, type, member) static_cast<type *>((ptr)->owner)
struct ieee80211com {};
struct ieee80211_node {
    unsigned ni_refcnt = 0;
    int ni_state = IEEE80211_STA_CACHE;
    void (*ni_unref_cb)(ieee80211com *, ieee80211_node *, void *) = nullptr;
    void *ni_unref_arg = nullptr;
    size_t ni_unref_arg_size = 0;
};
static int splnet() { return 0; }
static void splx(int) {}
static void ieee80211_free_node(ieee80211com *, ieee80211_node *) { assert(false); }
#include "node-ref.inc"
#include "node-release.inc"

struct Packet {};
struct HwNode { ieee80211_node in_ni; };
struct ieee80211_tx_info { unsigned flags = 0; };
struct TxFields {
    Packet *m = nullptr;
    HwNode *in = nullptr;
    void *map = nullptr;
    bool sae_active = false, ap_frame = false;
    unsigned totlen = 0, txmcs = 0, txrate = 0, fc = 0, sta_id = 0;
    uint8_t diag_peer[6]{};
    ieee80211_tx_info info;
};
struct iwm_tx_data : TxFields {};
struct iwx_tx_data : TxFields {};
struct Dma { void *vaddr = nullptr; size_t size = 0; };
struct iwx_tfh_tb { uint64_t value = 0; };
struct iwx_tfh_tfd { uint16_t num_tbs = 0; iwx_tfh_tb tbs[3]; };
struct iwm_tx_ring {
    int qid = 3, queued = 0, cur = 0, tail = 0;
    iwm_tx_data data[IWM_TX_RING_COUNT];
    unsigned *desc = nullptr;
    Dma desc_dma, cmd_dma;
};
struct iwx_tx_ring {
    int qid = 3, queued = 0, cur = 0, tail = 0, ring_count = 2;
    int hi_mark = 1, low_mark = 1;
    bool ap_queue_full = false;
    iwx_tx_data data[2];
    iwx_tfh_tfd *desc = nullptr;
    Dma desc_dma, cmd_dma, bc_tbl;
};
struct iwm_softc {
    ieee80211com sc_ic;
    unsigned qfullmsk = 8;
    int cmdqid = 0, sc_device_family = 0;
    void *sc_dmat = nullptr;
    iwm_tx_ring *ring = nullptr;
};
struct iwx_softc {
    ieee80211com sc_ic;
    unsigned qfullmsk = 8;
    void *sc_dmat = nullptr, *owner = nullptr;
};
static unsigned packetFrees, mapFrees, dmaFrees, saeFailures, callbacks;
static unsigned callbackQueued, callbackTail, callbackNodeLive, callbackLength;
static unsigned callbackDescriptorLive, callbackLockDepth;
static std::function<void()> observeCallback;
struct IOSimpleLock { unsigned depth = 0; };
static IOSimpleLock txLock;
static void IOSimpleLockLock(IOSimpleLock *lock) { assert(lock->depth++ == 0); }
static void IOSimpleLockUnlock(IOSimpleLock *lock) { assert(lock->depth-- == 1); }
static IOSimpleLock *iwx_txq_lock_for_ring(iwx_softc *, iwx_tx_ring *) {
    return &txLock;
}
static void mbuf_freem(Packet *m) { assert(m); ++packetFrees; }
static void bus_dmamap_destroy(void *, void *) { ++mapFrees; }
static void iwm_dma_contig_free(Dma *dma) {
    ++dmaFrees; dma->vaddr = nullptr; dma->size = 0;
}
static void iwx_dma_contig_free(Dma *dma) { iwm_dma_contig_free(dma); }
static void iwm_nic_unlock(iwm_softc *) { assert(false); }
static void iwm_reset_sched(iwm_softc *sc, int, int index, unsigned) {
    // Scheduler-MMIO effects are a boundary, not copied firmware behavior.
    sc->ring->desc[index] = 0;
}
struct ItlIwm {
    iwm_softc com;
    void iwm_txd_done(iwm_softc *, iwm_tx_data *);
    void iwm_ampdu_txq_advance(iwm_softc *, iwm_tx_ring *, int);
    void iwm_reset_tx_ring(iwm_softc *, iwm_tx_ring *);
    void iwm_free_tx_ring(iwm_softc *, iwm_tx_ring *);
    void iwm_sae_tx_report_terminal(iwm_softc *, iwm_tx_data *data, int error) {
        assert(error == EIO); ++saeFailures; data->sae_active = false;
    }
};
struct ItlIwx {
    iwx_softc com;
    ItlIwx() { com.owner = this; }
    void iwx_txd_done(iwx_softc *, iwx_tx_data *);
    void iwx_ampdu_txq_advance(iwx_softc *, iwx_tx_ring *, int);
    void iwx_reset_tx_ring(iwx_softc *, iwx_tx_ring *);
    void iwx_free_tx_ring(iwx_softc *, iwx_tx_ring *);
    void iwx_sae_tx_report_terminal(iwx_softc *, iwx_tx_data *data, int error) {
        assert(error == EIO); ++saeFailures; data->sae_active = false;
    }
};
void iwx_clear_tx_desc(iwx_softc *, iwx_tx_ring *, int);
#include "iwm-retirement.inc"
#pragma clang diagnostic push
// The unchanged DMA-sync-disabled production clear helper does not use sc.
#pragma clang diagnostic ignored "-Wunused-parameter"
#include "iwx-retirement.inc"
#pragma clang diagnostic pop

static void callback(ieee80211com *, ieee80211_node *, void *) {
    ++callbacks;
    assert(observeCallback);
    observeCallback();
}
template<class Ring> static void populate(Ring &ring, HwNode &node,
                                         Packet (&packets)[2], bool sae) {
    ring.queued = 2;
    ring.cur = 0;
    for (unsigned i = 0; i < 2; ++i) {
        ring.data[i].m = &packets[i];
        ring.data[i].in = &node;
        ring.data[i].totlen = 1400;
        ring.data[i].sae_active = sae;
        ring.data[i].map = &packets[i];
        ieee80211_ref_node(&node.in_ni);
    }
}
static void printObservation(const char *family, unsigned refs) {
    printf("%s: refs=%u callbacks=%u callback queued=%u tail=%u node_live=%u "
           "len=%u descriptor_live=%u lock_depth=%u packet_frees=%u\n",
           family, refs, callbacks, callbackQueued, callbackTail,
           callbackNodeLive, callbackLength, callbackDescriptorLive,
           callbackLockDepth, packetFrees);
}
int main(int argc, char **argv) {
    assert(argc == 2);
    setvbuf(stdout, nullptr, _IONBF, 0);
    const int scenario = std::atoi(argv[1]);
    assert(scenario >= 0 && scenario <= 9);
    HwNode node;
    Packet packets[2];
    const bool iwx = scenario == 2 || scenario == 4 || scenario == 6 ||
        scenario == 8 || scenario == 9;
    const bool reset = scenario == 3 || scenario == 4 || scenario == 7 || scenario == 8;
    const bool freeRing = scenario == 5 || scenario == 6;
    const bool sae = scenario == 7 || scenario == 8;
    const bool pendingCallback = scenario == 1 || scenario == 2 || sae;
    if (pendingCallback)
        node.in_ni.ni_unref_cb = callback;
    if (!iwx) {
        ItlIwm driver;
        iwm_tx_ring ring;
        unsigned descriptors[2] = {1, 1};
        ring.desc = descriptors;
        ring.desc_dma = {descriptors, sizeof(descriptors)};
        driver.com.ring = &ring;
        populate(ring, node, packets, sae);
        observeCallback = [&] {
            callbackQueued = ring.queued; callbackTail = ring.tail;
            callbackNodeLive = ring.data[1].in != nullptr;
            callbackLength = ring.data[1].totlen;
            callbackDescriptorLive = descriptors[1] != 0;
        };
        if (reset) driver.iwm_reset_tx_ring(&driver.com, &ring);
        else if (freeRing) driver.iwm_free_tx_ring(&driver.com, &ring);
        else {
            // Two distinct real reclaims, one descriptor each; this avoids
            // treating a wrapped SSN equal to tail as a full-ring terminal.
            driver.iwm_ampdu_txq_advance(&driver.com, &ring, 1);
            assert(node.in_ni.ni_refcnt == 1 && callbacks == 0);
            driver.iwm_ampdu_txq_advance(&driver.com, &ring, 0);
        }
        printObservation("IWM", node.in_ni.ni_refcnt);
        assert(packetFrees == 2);
        if (freeRing) assert(mapFrees == 2 && dmaFrees == 2);
        assert(node.in_ni.ni_refcnt == 0 && ring.data[0].in == nullptr &&
               ring.data[1].in == nullptr && "normal STA refs must retire too");
    } else {
        ItlIwx driver;
        iwx_tx_ring ring;
        iwx_tfh_tfd descriptors[2];
        unsigned byteCounts[2] = {1400, 1400};
        for (auto &descriptor : descriptors) {
            descriptor.num_tbs = 2; descriptor.tbs[1].value = 1;
        }
        ring.desc = descriptors;
        ring.desc_dma = {descriptors, sizeof(descriptors)};
        ring.bc_tbl = {byteCounts, sizeof(byteCounts)};
        populate(ring, node, packets, sae);
        observeCallback = [&] {
            callbackQueued = ring.queued; callbackTail = ring.tail;
            callbackNodeLive = ring.data[1].in != nullptr;
            callbackLength = ring.data[1].totlen;
            callbackDescriptorLive = descriptors[1].num_tbs != 0;
            callbackLockDepth = txLock.depth;
        };
        if (reset) driver.iwx_reset_tx_ring(&driver.com, &ring);
        else if (freeRing) driver.iwx_free_tx_ring(&driver.com, &ring);
        else {
            driver.iwx_ampdu_txq_advance(&driver.com, &ring, 1);
            assert(node.in_ni.ni_refcnt == 1 && callbacks == 0);
            driver.iwx_ampdu_txq_advance(&driver.com, &ring, 0);
        }
        printObservation("IWX", node.in_ni.ni_refcnt);
        assert(packetFrees == 2 && txLock.depth == 0);
        if (freeRing) assert(mapFrees == 2 && dmaFrees == 3);
        assert(node.in_ni.ni_refcnt == 0 && ring.data[0].in == nullptr &&
               ring.data[1].in == nullptr && "normal STA refs must retire too");
    }
    if (sae) assert(saeFailures == 2);
    if (pendingCallback) {
        assert(callbacks == 1);
        assert(callbackQueued == 0 && callbackNodeLive == 0 &&
               callbackDescriptorLive == 0 && callbackLockDepth == 0 &&
               "BSS continuation must run after physical retirement and outside leaf locks");
    }
    return 0;
}
