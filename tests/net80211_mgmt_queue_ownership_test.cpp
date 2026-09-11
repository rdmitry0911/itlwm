// Complete production mq_enqueue/send_mgmt/mgmt_output/ref/release paths.
// Allocation, frame construction and physical TX delivery are explicit boundaries.
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <initializer_list>
#ifdef __APPLE__
#include <libkern/OSByteOrder.h>
#define htole16(value) OSSwapHostToLittleInt16(value)
#else
#include <endian.h>
#endif
#include "kernel_memory_test_support.hpp"
using u_int = unsigned;
using u_int8_t = uint8_t;
using u_int16_t = uint16_t;
#define IEEE80211_STA_ONLY 1
constexpr int MBUF_DONTWAIT = 0, MT_DATA = 0;
constexpr int IEEE80211_ADDR_LEN = 6, IEEE80211_S_RUN = 4;
constexpr int IEEE80211_WNM_BSS_TM_ACCEPT = 0;
constexpr int IEEE80211_CATEG_WNM = 10, IEEE80211_ACTION_WNM_BSS_TRANS_RESP = 8;
constexpr int IEEE80211_FC0_VERSION_0 = 0, IEEE80211_FC0_TYPE_MGT = 0;
constexpr int IEEE80211_FC0_SUBTYPE_MASK = 0xf0;
constexpr int IEEE80211_FC0_SUBTYPE_ASSOC_REQ = 0, IEEE80211_FC0_SUBTYPE_REASSOC_REQ = 0x20;
constexpr int IEEE80211_FC0_SUBTYPE_PROBE_REQ = 0x40, IEEE80211_FC0_SUBTYPE_DISASSOC = 0xa0;
constexpr int IEEE80211_FC0_SUBTYPE_AUTH = 0xb0, IEEE80211_FC0_SUBTYPE_DEAUTH = 0xc0;
constexpr int IEEE80211_FC0_SUBTYPE_ACTION = 0xd0;
constexpr int IEEE80211_FC1_DIR_NODS = 0, IEEE80211_FC1_PROTECTED = 0x40;
constexpr int IEEE80211_SEQ_SEQ_SHIFT = 4, IEEE80211_M_STA = 0;
constexpr int IEEE80211_C_MFP = 1, IEEE80211_C_WNM_BSS_TRANSITION = 2;
constexpr int IEEE80211_NODE_MFP = 1, IEEE80211_NODE_TXMGMTPROT = 2;
constexpr int IEEE80211_TRANS_WAIT = 5, IEEE80211_STA_COLLECT = 3;
constexpr int kAirportItlwmPostPltiTraceEventAuthEnqueued = 1;
constexpr int kAirportItlwmPostPltiTraceEventAssocEnqueued = 2;
#define IEEE80211_ADDR_COPY(a,b) std::memcpy((a),(b),6)
#define IEEE80211_IS_MULTICAST(a) (((a)[0] & 1) != 0)
static void fixture_log(const char *, ...) {}
#define IWX_AUTH_DIAG fixture_log
#define DPRINTF(x) fixture_log x
struct ieee80211com;
struct ieee80211_node {
    unsigned ni_refcnt = 0, ni_inact = 0, ni_txseq = 0, ni_flags = 0;
    int ni_state = 0;
    uint8_t ni_macaddr[6] = {2}, ni_bssid[6] = {2};
    void (*ni_unref_cb)(ieee80211com *, ieee80211_node *, void *) = nullptr;
    void *ni_unref_arg = nullptr;
    size_t ni_unref_arg_size = 0;
};
struct ieee80211_frame {
    uint8_t i_fc[2], i_dur[2], i_addr1[6], i_addr2[6], i_addr3[6], i_seq[2];
};
struct Packet {
    alignas(16) uint8_t bytes[256]{};
    size_t length = 8;
    void *peer = nullptr;
    Packet *next = nullptr;
};
using mbuf_t = Packet *;
using ifnet_t = void *;
struct IORecursiveLock { unsigned depth = 0; };
static void IORecursiveLockLock(IORecursiveLock *lock) { assert(lock); ++lock->depth; }
static void IORecursiveLockUnlock(IORecursiveLock *lock) { assert(lock && lock->depth); --lock->depth; }
struct mbuf_list { mbuf_t ml_head = nullptr, ml_tail = nullptr; unsigned ml_len = 0; };
struct mbuf_queue { IORecursiveLock *mq_mtx; mbuf_list mq_list; unsigned mq_maxlen, mq_drops = 0; };
static unsigned mq_len(mbuf_queue *queue) { return queue->mq_list.ml_len; }
static void mbuf_setnextpkt(mbuf_t packet, mbuf_t next) { packet->next = next; }
static mbuf_t mbuf_nextpkt(mbuf_t packet) { return packet->next; }
static unsigned packetLive, packetFreed, traces, starts, callbacks, nodeFrees;
static bool prependFail, builderFail, completeInline;
static mbuf_t allocate() { if (builderFail) return nullptr; ++packetLive; return new Packet; }
static void mbuf_freem(mbuf_t packet) { assert(packet && packetLive); --packetLive; ++packetFreed; delete packet; }
static void mbuf_prepend(mbuf_t *packet, size_t count, int) {
    if (prependFail) { mbuf_freem(*packet); *packet = nullptr; return; }
    assert((*packet)->length + count <= sizeof((*packet)->bytes));
    std::memmove((*packet)->bytes + count, (*packet)->bytes, (*packet)->length);
    std::memset((*packet)->bytes, 0, count);
    (*packet)->length += count;
}
static void mbuf_pkthdr_setrcvif(mbuf_t packet, ifnet_t peer) { packet->peer = peer; }
static size_t mbuf_len(mbuf_t packet) { return packet->length; }
static size_t mbuf_pkthdr_len(mbuf_t packet) { return packet->length; }
static void mbuf_pkthdr_setlen(mbuf_t packet, size_t length) { packet->length = length; }
static void mbuf_setlen(mbuf_t packet, size_t length) { packet->length = length; }
#define mtod(m,t) reinterpret_cast<t>((m)->bytes)
struct _ifnet { int if_timer = 0; void (*if_start)(_ifnet *) = nullptr; };
struct ieee80211com {
    _ifnet ic_if;
    mbuf_queue ic_mgtq;
    unsigned ic_caps = IEEE80211_C_MFP | IEEE80211_C_WNM_BSS_TRANSITION;
    uint8_t ic_myaddr[6] = {4};
    int ic_opmode = IEEE80211_M_STA, ic_mgt_timer = 0;
    int ic_state = IEEE80211_S_RUN;
    ieee80211_node *ic_bss = nullptr;
    struct { unsigned is_tx_nombuf = 0, is_tx_unknownmgt = 0; } ic_stats;
};
static void panic(const char *) { assert(false); }
static int splnet() { return 1; }
static void splx(int) {}
static void ieee80211_free_node(ieee80211com *, ieee80211_node *) { ++nodeFrees; }
static void AirportItlwmPostPltiTraceRecord(ieee80211com *, int) { ++traces; }
#include "queue.inc"
#include "ref.inc"
#include "release.inc"
static mbuf_t ieee80211_get_probe_req(ieee80211com *, ieee80211_node *) { return allocate(); }
static mbuf_t ieee80211_get_auth(ieee80211com *, ieee80211_node *, int, int) { return allocate(); }
static mbuf_t ieee80211_get_deauth(ieee80211com *, ieee80211_node *, int) { return allocate(); }
static mbuf_t ieee80211_get_assoc_req(ieee80211com *, ieee80211_node *, int) { return allocate(); }
static mbuf_t ieee80211_get_disassoc(ieee80211com *, ieee80211_node *, int) { return allocate(); }
static mbuf_t ieee80211_get_action(ieee80211com *, ieee80211_node *, int, int, int) { return allocate(); }
static mbuf_t ieee80211_getmgmt(int, int, unsigned length) {
    mbuf_t packet = allocate(); if (packet) packet->length = length; return packet;
}
static mbuf_t ieee80211_get_compressed_bar(ieee80211com *, ieee80211_node *node, int, uint16_t) {
    mbuf_t packet = allocate(); if (packet) packet->peer = node; return packet;
}
#include "output.inc"
static void terminal(ieee80211com *ic) {
    mbuf_t packet = ml_dequeue(&ic->ic_mgtq.mq_list);
    assert(packet && packet->peer);
    auto *node = static_cast<ieee80211_node *>(packet->peer);
    packet->peer = nullptr;
    mbuf_freem(packet);
    ieee80211_release_node(ic, node);
}
static void start(_ifnet *ifp) {
    ++starts;
    if (completeInline) terminal(reinterpret_cast<ieee80211com *>(ifp));
}
static void callback(ieee80211com *ic, ieee80211_node *, void *) {
    assert(ic->ic_mgtq.mq_mtx->depth == 0);
    ++callbacks;
}
int main(int argc, char **argv) {
    assert(argc == 2);
    const int scenario = std::atoi(argv[1]);
    IORecursiveLock lock;
    ieee80211com ic{};
    ic.ic_if.if_start = start;
    ic.ic_mgtq.mq_mtx = &lock;
    ic.ic_mgtq.mq_maxlen = 1;
    ml_init(&ic.ic_mgtq.mq_list);
    ieee80211_node node, queuedNode;
    ic.ic_bss = &node;
    node.ni_flags = IEEE80211_NODE_MFP | IEEE80211_NODE_TXMGMTPROT;
    node.ni_unref_cb = callback;
    const bool full = (scenario >= 1 && scenario <= 7) || scenario == 11 || scenario == 12 ||
        scenario == 15 || scenario == 16;
    if (full) {
        mbuf_t existing = allocate();
        existing->peer = ieee80211_ref_node(&queuedNode);
        assert(mq_enqueue(&ic.ic_mgtq, existing) == 0);
    }
    if (scenario == 11) node.ni_refcnt = 2;
    const unsigned originalRefs = node.ni_refcnt;
    prependFail = scenario == 8 || scenario == 21;
    builderFail = scenario == 9 || scenario == 20;
    completeInline = scenario == 10;
    const int types[] = {IEEE80211_FC0_SUBTYPE_AUTH, IEEE80211_FC0_SUBTYPE_AUTH,
        IEEE80211_FC0_SUBTYPE_DEAUTH, IEEE80211_FC0_SUBTYPE_ASSOC_REQ,
        IEEE80211_FC0_SUBTYPE_REASSOC_REQ, IEEE80211_FC0_SUBTYPE_ACTION,
        IEEE80211_FC0_SUBTYPE_PROBE_REQ, IEEE80211_FC0_SUBTYPE_DISASSOC};
    const int type = scenario < 8 ? types[scenario] : IEEE80211_FC0_SUBTYPE_AUTH;
    if (scenario >= 15) {
        if (scenario == 19) ic.ic_state = 0;
        uint8_t target[6] = {6};
        const uint8_t status = scenario == 16 || scenario == 18 ? 1 : IEEE80211_WNM_BSS_TM_ACCEPT;
        const int result = ieee80211_send_bss_transition_response(&ic, &node, 7, status,
            status == IEEE80211_WNM_BSS_TM_ACCEPT ? target : nullptr);
        const int expected = full ? ENOBUFS : scenario == 19 ? EINVAL :
            scenario == 20 || scenario == 21 ? ENOMEM : 0;
        assert(result == expected);
        if (result != 0) {
            assert(node.ni_refcnt == 0 && starts == 0 && traces == 0 && ic.ic_mgt_timer == 0);
            assert(mq_len(&ic.ic_mgtq) == (full ? 1U : 0U));
        } else {
            assert(node.ni_refcnt == 1 && starts == 1 && ic.ic_mgt_timer == 0);
            auto *frame = mtod(ic.ic_mgtq.mq_list.ml_head, ieee80211_frame *);
            assert(frame->i_fc[0] == IEEE80211_FC0_SUBTYPE_ACTION &&
                (frame->i_fc[1] & IEEE80211_FC1_PROTECTED) != 0);
            terminal(&ic);
        }
    } else if (scenario == 12 || scenario == 13) {
        ieee80211_tx_compressed_bar(&ic, &node, 0, 5);
        assert(node.ni_refcnt == (full ? 0U : 1U));
        assert(starts == (full ? 0U : 1U));
        if (!full) terminal(&ic);
    } else {
        if (scenario == 14) ic.ic_mgt_timer = 17;
        const int selectedType = scenario == 14 ? IEEE80211_FC0_SUBTYPE_DEAUTH : type;
        const int result = ieee80211_send_mgmt(&ic, &node, selectedType, 1, 0);
        std::fprintf(stderr, "scenario=%d result=%d refs=%u starts=%u queue=%u timer=%d\n",
            scenario, result, node.ni_refcnt, starts, mq_len(&ic.ic_mgtq), ic.ic_mgt_timer);
        if (full) {
            assert(result == ENOBUFS && "discarded management must not report submission success");
            assert(node.ni_refcnt == originalRefs && "discarded frame must return its exact reference");
            assert(traces == 0 && starts == 0 && ic.ic_mgt_timer == 0 && ic.ic_if.if_timer == 0);
            assert(mq_len(&ic.ic_mgtq) == 1 && queuedNode.ni_refcnt == 1);
        } else if (prependFail || builderFail) {
            assert(result == ENOMEM && node.ni_refcnt == originalRefs && starts == 0 && traces == 0);
        } else {
            assert(result == 0 && starts == 1);
            assert(node.ni_refcnt == (completeInline ? 0U : 1U));
            assert(ic.ic_mgt_timer == (scenario == 14 ? 17 : IEEE80211_TRANS_WAIT));
            if (!completeInline) terminal(&ic);
        }
    }
    if (full) terminal(&ic);
    assert(packetLive == 0 && mq_len(&ic.ic_mgtq) == 0 && lock.depth == 0);
    assert(queuedNode.ni_refcnt == 0 && node.ni_refcnt == originalRefs);
    assert(callbacks == (scenario == 11 || scenario == 19 || scenario == 20 ? 0U : 1U));
    assert(nodeFrees == 0);
    std::printf("management queue ownership scenario %d PASS\n", scenario);
}
