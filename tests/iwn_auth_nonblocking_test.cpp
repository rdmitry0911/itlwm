/* Execute the complete production iwn_auth, not a second AUTH model.
 * Firmware commands, channel configuration and DELAY are explicit boundary
 * doubles. These small structures/constants are fixture interfaces, not an
 * assertion about the Intel wire ABI. No radio or asynchronous completion is
 * simulated. A no-wait PASS alone will not qualify a future beacon owner. */
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

static constexpr unsigned IEEE80211_DUR_TU = 1024;
static constexpr unsigned IEEE80211_F_USEPROT = 1;
static constexpr unsigned IEEE80211_F_SHSLOT = 2;
static constexpr unsigned IEEE80211_F_SHPREAMBLE = 4;
static constexpr unsigned IWN_RXON_TSF = 1;
static constexpr unsigned IWN_RXON_CTS_TO_SELF = 2;
static constexpr unsigned IWN_RXON_AUTO = 4;
static constexpr unsigned IWN_RXON_24GHZ = 8;
static constexpr unsigned IWN_RXON_TGG_PROT = 16;
static constexpr unsigned IWN_RXON_SHSLOT = 32;
static constexpr unsigned IWN_RXON_SHPREAMBLE = 64;
enum { IEEE80211_MODE_11A, IEEE80211_MODE_11B, IEEE80211_MODE_11G };
enum { IWN_CMD_RXON = 1, IWN_RIDX_OFDM = 2, IWN_RIDX_CCK = 3 };

struct ieee80211_channel { unsigned number; bool five; };
struct ieee80211_node {
    uint8_t ni_macaddr[6], ni_bssid[6];
    ieee80211_channel *ni_chan;
    uint16_t ni_intval;
};
struct ieee80211com {
    ieee80211_node *ic_bss;
    unsigned ic_flags, ic_curmode, ic_mgt_timer;
    uint8_t ic_myaddr[6];
};
struct iwn_softc;
struct iwn_ops { int (*set_txpower)(iwn_softc *, int); };
struct iwn_rxon {
    uint8_t bssid[6], myaddr[6], wlap[6];
    unsigned chan, flags, cck_mask, ofdm_mask;
};
struct iwn_softc {
    iwn_ops ops;
    ieee80211com sc_ic;
    iwn_rxon rxon;
    int rxonsz;
    uint8_t bss_node_addr[6];
    struct { const char *dv_xname; } sc_dev;
};
static const uint8_t etheranyaddr[6] = {};
#define IEEE80211_ADDR_EQ(a, b) (memcmp((a), (b), 6) == 0)
#define IEEE80211_ADDR_COPY(a, b) memcpy((a), (b), 6)
#define IEEE80211_IS_CHAN_2GHZ(c) (!(c)->five)
#define IEEE80211_IS_CHAN_5GHZ(c) ((c)->five)
#ifndef htole32
#define htole32(value) (value)
#endif
#define XYLog(...) ((void)0)

static uint64_t busyMicroseconds;
static unsigned commandStep;
static int failStep;
static void countedDelay(uint64_t usec) { busyMicroseconds += usec; }
#define DELAY(usec) countedDelay(usec)
static unsigned ieee80211_chan2ieee(ieee80211com *, ieee80211_channel *c) {
    return c->number;
}
static int step(unsigned expected) {
    assert(++commandStep == expected);
    return failStep == static_cast<int>(expected) ? 5 : 0;
}
static int txpower(iwn_softc *, int async) {
    assert(async == 1);
    return step(3);
}
class ItlIwn {
public:
    bool apStaBssAssociated = true;
    int iwn_auth(iwn_softc *, int);
    void iwn_rxon_configure_ht40(ieee80211com *, ieee80211_node *) {}
    int iwn_cmd(iwn_softc *sc, int code, const void *payload, int size, int async) {
        assert(code == IWN_CMD_RXON && payload == &sc->rxon);
        assert(size == sc->rxonsz && async == 1);
        assert(!apStaBssAssociated);
        return step(1);
    }
    int iwn_set_ap_sta_auth_priority(bool enabled) {
        assert(enabled);
        return step(2);
    }
    int iwn_add_broadcast_node(iwn_softc *, int async, int ridx) {
        assert(async == 1 && ridx == IWN_RIDX_CCK);
        return step(4);
    }
};
#include "auth.inc"

int main(int argc, char **argv) {
    assert(argc == 2);
    const char *mode = argv[1];
    const uint8_t source[6] = {0x82,0xc3,0x97,0x84,0x51,0xca};
    const uint8_t target[6] = {0x9a,0xfb,0x5d,0x97,0xa9,0x02};
    const uint8_t station[6] = {0x02,0,0,0,0,1};
    ieee80211_channel channel = {13, false};
    ieee80211_node node = {};
    memcpy(node.ni_bssid, target, 6);
    memcpy(node.ni_macaddr, target, 6);
    node.ni_chan = &channel;
    node.ni_intval = 100;
    iwn_softc sc = {};
    sc.ops.set_txpower = txpower;
    sc.sc_ic.ic_bss = &node;
    sc.sc_ic.ic_curmode = IEEE80211_MODE_11G;
    memcpy(sc.sc_ic.ic_myaddr, station, 6);
    sc.rxonsz = sizeof(sc.rxon);
    sc.sc_dev.dv_xname = "iwn-test";
    int argument = 192; // Actual q1 fake DEAUTH reason at the BSS switch.
    if (strcmp(mode, "roam") == 0)
        memcpy(sc.bss_node_addr, source, 6);
    else if (strcmp(mode, "cold") == 0)
        argument = -1;
    else if (strcmp(mode, "incoming") == 0)
        memcpy(sc.bss_node_addr, target, 6);
    else if (strcmp(mode, "retry") == 0) {
        argument = -1;
        sc.sc_ic.ic_mgt_timer = 1;
    } else if (strcmp(mode, "command-failure") == 0) {
        argument = -1;
        failStep = 1;
    } else
        return 2;
    ItlIwn driver;
    const int result = driver.iwn_auth(&sc, argument);
    std::printf("mode=%s result=%d command_steps=%u busy_us=%llu\n", mode,
        result, commandStep, static_cast<unsigned long long>(busyMicroseconds));
    std::fflush(stdout);
    if (failStep != 0) {
        assert(result == 5 && commandStep == 1 && busyMicroseconds == 0);
        return 0;
    }
    assert(result == 0 && commandStep == 4);
    assert(IEEE80211_ADDR_EQ(sc.rxon.bssid, target));
    assert(IEEE80211_ADDR_EQ(sc.rxon.myaddr, station));
    assert(IEEE80211_ADDR_EQ(sc.rxon.wlap, station));
    assert(sc.rxon.chan == 13);
    // Required behavior, deliberately fails on the currently loaded source.
    assert(busyMicroseconds == 0 && "AUTH must not busy-wait on the workloop");
}
