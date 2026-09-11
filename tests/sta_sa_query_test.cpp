/* Actual shared policy and kernel-binding bodies; no duplicated FSM. */
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <sys/time.h>
#include "itl80211/openbsd/net80211/ieee80211_sta_sa_query.h"

using u_int8_t = uint8_t;
using u_int16_t = uint16_t;
using u_int32_t = uint32_t;
using u_int64_t = uint64_t;
using mbuf_tag_id_t = uint32_t;
using IOInterruptState = int;
struct IOSimpleLock {};
struct CTimeout {};
static uint64_t clock_us;
static int lock_depth, sends, events, arms;
static uint16_t random_value;
static bool tag_failure;
static bool timer_failure;
static const uint8_t ap[6] = { 0x82, 0xc3, 0x97, 0x84, 0x51, 0xca };
static const uint8_t sta[6] = { 0x4e, 0xbc, 0x8d, 0xff, 0x50, 0x23 };

#define IEEE80211_NODE_MFP 0x80
#define IEEE80211_NODE_TXMGMTPROT 0x40
#define IEEE80211_NODE_RXMGMTPROT 0x20
#define IEEE80211_M_STA 1
#define IEEE80211_S_RUN 4
#define IFF_UP 1
#define IFF_RUNNING 2
#define IEEE80211_FC0_TYPE_MGT 0
#define IEEE80211_FC0_SUBTYPE_MASK 0xf0
#define IEEE80211_FC0_SUBTYPE_ACTION 0xd0
#define IEEE80211_FC0_SUBTYPE_DEAUTH 0xc0
#define IEEE80211_FC0_SUBTYPE_DISASSOC 0xa0
#define IEEE80211_FC1_PROTECTED 0x40
#define IEEE80211_FC1_DIR_MASK 3
#define IEEE80211_REASON_NOT_AUTHED 6
#define IEEE80211_REASON_NOT_ASSOCED 7
#define IEEE80211_CATEG_SA_QUERY 8
#define IEEE80211_ACTION_SA_QUERY_REQ 0
#define IEEE80211_ACTION_SA_QUERY_RESP 1
#define IEEE80211_EVT_STA_SA_QUERY_TIMEOUT 25
#define MBUF_DONTWAIT 1
#define IEEE80211_ADDR_EQ(a, b) (std::memcmp(a, b, 6) == 0)
#define IEEE80211_IS_MULTICAST(a) ((a)[0] & 1)
#define LE_READ_2(p) ((uint16_t)((p)[0] | ((uint16_t)(p)[1] << 8)))
struct ieee80211_frame {
    uint8_t i_fc[2], i_dur[2], i_addr1[6], i_addr2[6], i_addr3[6], i_seq[2];
};
static_assert(sizeof(ieee80211_frame) == 24, "wire header");
struct Packet {
    ieee80211_frame wh{};
    uint8_t body[4]{};
    size_t len = 28, total = 28, tag_len = 0;
    ieee80211_sta_sa_query_token tag{};
};
using mbuf_t = Packet *;
#define mtod(m, type) reinterpret_cast<type>(&(m)->wh)
static size_t mbuf_len(mbuf_t m) { return m->len; }
static size_t mbuf_pkthdr_len(mbuf_t m) { return m->total; }
struct ieee80211_node { uint8_t ni_bssid[6], ni_macaddr[6]; int ni_port_valid; uint32_t ni_flags; };
struct ieee80211com {
    IOSimpleLock *ic_pae_selected_bss_lock;
    ieee80211_sta_sa_query ic_sta_sa_query{};
    CTimeout *ic_sta_sa_query_timeout = nullptr;
    mbuf_tag_id_t ic_sta_sa_query_tag_id = 0;
    uint8_t ic_sta_sa_query_tag_valid = 0, ic_sta_sa_query_enabled = 1;
    int ic_opmode = IEEE80211_M_STA, ic_state = IEEE80211_S_RUN;
    struct { int if_flags = IFF_UP | IFF_RUNNING; } ic_if;
    uint64_t epoch = 3, ic_pae_assoc_replace_epoch = 0;
    struct { uint64_t epoch; uint8_t bssid[6]; } ic_pae_selected_bss{};
    ieee80211_node *ic_bss = nullptr;
    uint8_t ic_myaddr[6]{};
    void (*ic_event_handler)(ieee80211com *, int, void *) = nullptr;
};
static Packet last_packet;
static IOInterruptState IOSimpleLockLockDisableInterrupt(IOSimpleLock *lock) {
    assert(lock && lock_depth == 0); ++lock_depth; return 0;
}
static void IOSimpleLockUnlockEnableInterrupt(IOSimpleLock *, IOInterruptState) {
    assert(lock_depth == 1); --lock_depth;
}
static uint64_t ieee80211_pae_assoc_epoch_current(const ieee80211com *ic) { return ic->epoch; }
static void microuptime(timeval *v) { v->tv_sec = clock_us / 1000000; v->tv_usec = clock_us % 1000000; }
static void arc4random_buf(void *p, size_t n) { assert(!lock_depth && n == 2); std::memcpy(p, &random_value, n); }
static int mbuf_tag_id_find(const char *, mbuf_tag_id_t *id) { assert(!lock_depth); *id = 123; return 0; }
static int mbuf_tag_allocate(mbuf_t m, mbuf_tag_id_t id, int type, size_t len, int how, void **p) {
    assert(!lock_depth && id == 123 && type == 1 && how == MBUF_DONTWAIT);
    if (tag_failure) return ENOBUFS;
    assert(len == sizeof(m->tag)); m->tag_len = len; *p = &m->tag; return 0;
}
static int mbuf_tag_find(mbuf_t m, mbuf_tag_id_t id, int type, size_t *len, void **p) {
    assert(!lock_depth && id == 123 && type == 1);
    if (!m->tag_len) return ENOENT;
    *len = m->tag_len; *p = &m->tag; return 0;
}
static CTimeout timer;
static void timeout_set(CTimeout **p, void (*)(void *), void *) { assert(!lock_depth); *p = &timer; }
static int timeout_add_msec(CTimeout **p, int delay) {
    assert(!lock_depth && delay > 0);
    if (!*p || timer_failure) return 0;
    ++arms; return 1;
}
static void timeout_del(CTimeout **) { assert(!lock_depth); }
static void timeout_free(CTimeout **p) { assert(!lock_depth); *p = nullptr; }
int ieee80211_sta_sa_query_tag(ieee80211com *, mbuf_t, const ieee80211_sta_sa_query_token *);
int ieee80211_send_sta_sa_query(ieee80211com *ic, ieee80211_node *, const ieee80211_sta_sa_query_token *token) {
    assert(!lock_depth); ++sends; last_packet = Packet{};
    last_packet.wh.i_fc[0] = 0xd0; last_packet.wh.i_fc[1] = 0x40;
    std::memcpy(last_packet.wh.i_addr1, token->bssid, 6);
    std::memcpy(last_packet.wh.i_addr2, token->sta, 6);
    std::memcpy(last_packet.wh.i_addr3, token->bssid, 6);
    last_packet.body[0] = 8; last_packet.body[1] = 0;
    last_packet.body[2] = token->transaction; last_packet.body[3] = token->transaction >> 8;
    return ieee80211_sta_sa_query_tag(ic, &last_packet, token);
}
#include "itl80211/openbsd/net80211/ieee80211_sta_sa_query.inc"

static void event(ieee80211com *ic, int kind, void *arg) {
    assert(!lock_depth && kind == IEEE80211_EVT_STA_SA_QUERY_TIMEOUT);
    assert(ieee80211_sta_sa_query_claim_failure(ic,
        static_cast<ieee80211_sta_sa_query_token *>(arg)));
    ++events;
}
static Packet disconnect(unsigned reason = 7) {
    Packet m;
    m.wh.i_fc[0] = 0xc0;
    std::memcpy(m.wh.i_addr1, sta, 6); std::memcpy(m.wh.i_addr2, ap, 6);
    std::memcpy(m.wh.i_addr3, ap, 6); m.body[0] = reason;
    m.len = m.total = 26;
    return m;
}

static void policy_tests() {
    using Q = ieee80211_sta_sa_query;
    using T = ieee80211_sta_sa_query_token;
    for (unsigned seed = 0; seed <= UINT16_MAX; ++seed) {
        Q q{}; T tx[5]{}, pending{}; uint64_t delay = 0;
        assert(ieee80211_sta_sa_query_begin_value(&q, 0, 1, ap, sta));
        for (unsigned i = 0; i < 5; ++i) {
            const uint64_t now = i * IEEE80211_STA_SA_QUERY_RETRY_US;
            assert(ieee80211_sta_sa_query_poll_value(&q, now,
                static_cast<uint16_t>(seed), &tx[i], &delay) == 1);
            assert(tx[i].slot == i && delay > 0);
            assert(ieee80211_sta_sa_query_tx_value(&q, &tx[i], 0));
            assert(ieee80211_sta_sa_query_tx_value(&q, &tx[i], 1));
            assert(!ieee80211_sta_sa_query_tx_value(&q, &tx[i], 1));
            assert(!ieee80211_sta_sa_query_poll_value(&q, now, 0, &pending, &delay));
        }
        assert(!ieee80211_sta_sa_query_poll_value(&q,
            IEEE80211_STA_SA_QUERY_MAX_US - 1, 0, &pending, &delay));
        assert(delay == 1);
        for (unsigned i = 0; i < 5; ++i) {
            Q responded = q;
            assert(ieee80211_sta_sa_query_response_value(&responded, 1, ap, sta, tx[i].transaction));
            assert(responded.phase == IEEE80211_STA_SA_QUERY_IDLE);
            assert(!ieee80211_sta_sa_query_tx_value(&responded, &tx[i], 1));
            assert(!ieee80211_sta_sa_query_begin_value(&responded, 9999999, 2, ap, sta));
            assert(ieee80211_sta_sa_query_begin_value(&responded, 10000000, 2, ap, sta));
            assert(!ieee80211_sta_sa_query_tx_value(&responded, &tx[i], 1));
            assert(!ieee80211_sta_sa_query_failure_value(&responded, &tx[i]));
        }
        assert(ieee80211_sta_sa_query_poll_value(&q, IEEE80211_STA_SA_QUERY_MAX_US,
            0, &pending, &delay) == -1);
        assert(!delay && ieee80211_sta_sa_query_failure_value(&q, &pending));
        assert(!ieee80211_sta_sa_query_failure_value(&q, &pending));
        assert(!ieee80211_sta_sa_query_response_value(&q, 1, ap, sta, tx[0].transaction));
    }
    Q q{}; T tx{}; uint64_t delay;
    assert(!ieee80211_sta_sa_query_begin_value(&q, 0, 0, ap, sta));
    q.next_generation = UINT64_MAX;
    assert(!ieee80211_sta_sa_query_begin_value(&q, 0, 1, ap, sta));
    q = Q{};
    assert(ieee80211_sta_sa_query_begin_value(&q, 100, 1, ap, sta));
    assert(ieee80211_sta_sa_query_poll_value(&q, 100, 1, &tx, &delay) == 1);
    assert(!ieee80211_sta_sa_query_response_value(&q, 1, ap, sta, tx.transaction));
    assert(!ieee80211_sta_sa_query_poll_value(&q, 100 + IEEE80211_STA_SA_QUERY_MAX_US,
        0, &tx, &delay)); // zero actual TX: no invented radio failure
    assert(q.phase == IEEE80211_STA_SA_QUERY_IDLE);
    assert(ieee80211_sta_sa_query_begin_value(&q, 10000100, 2, ap, sta));
    assert(ieee80211_sta_sa_query_poll_value(&q, 10000100, 0, &tx, &delay) == 1);
    assert(!ieee80211_sta_sa_query_poll_value(&q, 10000099, 0, &tx, &delay));
    assert(q.phase == IEEE80211_STA_SA_QUERY_IDLE); // backwards clock fail-closed
}

static void binding_tests() {
    IOSimpleLock lock;
    ieee80211_node ni{};
    std::memcpy(ni.ni_bssid, ap, 6); std::memcpy(ni.ni_macaddr, ap, 6);
    ni.ni_flags = 0xe0; ni.ni_port_valid = 1;
    ieee80211com ic{};
    ic.ic_pae_selected_bss_lock = &lock; ic.ic_bss = &ni;
    ic.ic_pae_selected_bss.epoch = ic.epoch;
    std::memcpy(ic.ic_pae_selected_bss.bssid, ap, 6);
    std::memcpy(ic.ic_myaddr, sta, 6); ic.ic_event_handler = event;
    ieee80211_sta_sa_query_attach(&ic);
    const auto baseline = ic;
    for (unsigned bad = 0; bad < 17; ++bad) {
        ic = baseline; Packet m = disconnect();
        switch (bad) {
        case 0: m.len = 25; break;
        case 1: m.body[0] = 2; break;
        case 2: m.wh.i_addr1[0] |= 1; break;
        case 3: m.wh.i_addr2[5] ^= 1; break;
        case 4: m.wh.i_addr3[5] ^= 1; break;
        case 5: m.wh.i_fc[1] = 0x40; break;
        case 6: m.wh.i_fc[1] = 1; break;
        case 7: m.wh.i_fc[0] = 0xd0; break;
        case 8: ic.ic_state = 3; break;
        case 9: ic.ic_opmode = 6; break;
        case 10: ic.ic_pae_assoc_replace_epoch = 1; break;
        case 11: ic.ic_if.if_flags = 0; break;
        case 12: ic.ic_sta_sa_query_enabled = 0; break;
        case 13: ic.ic_sta_sa_query_tag_valid = 0; break;
        case 14: ++ic.epoch; break;
        case 15: ic.ic_pae_selected_bss.bssid[5] ^= 1; break;
        case 16: m.wh.i_addr1[5] ^= 1; break;
        }
        ieee80211_sta_sa_query_unprotected(&ic, &ni, &m.wh, m.len);
        assert(ic.ic_sta_sa_query.phase == IEEE80211_STA_SA_QUERY_IDLE);
    }
    ic = baseline; Packet m = disconnect(); clock_us = 100;
    ieee80211_sta_sa_query_unprotected(&ic, &ni, &m.wh, m.len);
    assert(ic.ic_sta_sa_query.phase == IEEE80211_STA_SA_QUERY_WAIT);
    const auto started = ic;
    ieee80211_sta_sa_query_timeout(&ic);
    assert(sends == 1);
    ieee80211_sta_sa_query_token token{};
    assert(ieee80211_sta_sa_query_tx_snapshot(&ic, &ni, &last_packet, &token) == 1);
    auto sent = last_packet;
    auto irq = IOSimpleLockLockDisableInterrupt(&lock);
    assert(ieee80211_sta_sa_query_tx_commit_locked(&ic, &ni, &token));
    IOSimpleLockUnlockEnableInterrupt(&lock, irq);
    const auto submitted = ic;
    for (unsigned bad = 0; bad < 7; ++bad) {
        ic = submitted; Packet response = sent;
        std::memcpy(response.wh.i_addr1, sta, 6);
        std::memcpy(response.wh.i_addr2, ap, 6);
        response.body[1] = 1;
        switch (bad) {
        case 0: response.len = 27; break;
        case 1: response.wh.i_addr1[0] |= 1; break;
        case 2: response.wh.i_addr2[5] ^= 1; break;
        case 3: response.body[2] ^= 1; break;
        case 4: ++ic.epoch; break;
        case 5: response.body[0] = 7; break;
        case 6: response.wh.i_addr3[5] ^= 1; break;
        }
        ieee80211_sta_sa_query_response(&ic, &ni, &response.wh, response.len);
        assert(ic.ic_sta_sa_query.phase == IEEE80211_STA_SA_QUERY_WAIT);
    }
    ic = submitted; Packet response = sent;
    std::memcpy(response.wh.i_addr1, sta, 6); std::memcpy(response.wh.i_addr2, ap, 6);
    response.body[1] = 1;
    ieee80211_sta_sa_query_response(&ic, &ni, &response.wh, response.len);
    assert(ic.ic_sta_sa_query.phase == IEEE80211_STA_SA_QUERY_IDLE);
    ieee80211_sta_sa_query_timeout(&ic); assert(events == 0);
    assert(ieee80211_sta_sa_query_tx_snapshot(&ic, &ni, &sent, &token) == -1);
    ic = submitted; clock_us += IEEE80211_STA_SA_QUERY_MAX_US;
    ieee80211_sta_sa_query_timeout(&ic); assert(events == 1);
    ieee80211_sta_sa_query_timeout(&ic); assert(events == 1);
    ic = started; tag_failure = true; clock_us = 100;
    ieee80211_sta_sa_query_timeout(&ic);
    clock_us += IEEE80211_STA_SA_QUERY_MAX_US;
    ieee80211_sta_sa_query_timeout(&ic); assert(events == 1); tag_failure = false;
    ic = submitted; ++ic.epoch;
    ieee80211_sta_sa_query_timeout(&ic); assert(events == 1);
    ic = baseline; timer_failure = true;
    ieee80211_sta_sa_query_unprotected(&ic, &ni, &m.wh, m.len);
    assert(ic.ic_sta_sa_query.phase == IEEE80211_STA_SA_QUERY_IDLE);
    ic = started; ieee80211_sta_sa_query_timeout(&ic);
    assert(ic.ic_sta_sa_query.phase == IEEE80211_STA_SA_QUERY_IDLE);
    timer_failure = false;
    ic = submitted; ieee80211_sta_sa_query_detach(&ic);
    ieee80211_sta_sa_query_timeout(&ic);
    assert(!ic.ic_sta_sa_query_timeout && !ic.ic_sta_sa_query_enabled && events == 1);
    assert(!lock_depth && arms > 0);
}

int main() {
    policy_tests(); binding_tests();
    std::puts("PASS: 65536 ID seeds, all pending responses, deadlines, rate limit, stale TX/epoch/timer, RX identity, detach, local failures; actual policy+binding bodies");
}
