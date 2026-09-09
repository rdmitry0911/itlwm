#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <initializer_list>
#include <sys/types.h>
#include <sys/time.h>
#include "AirportItlwm/TahoeScanContracts.hpp"
#include "AirportItlwm/TahoeBssManagerContracts.hpp"
#include "AirportItlwm/TahoeBeaconIeBuilder.hpp"

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define IEEE80211_ADDR_EQ(a, b) (memcmp(a, b, 6) == 0)
constexpr unsigned IEEE80211_ADDR_LEN = 6;
constexpr unsigned IEEE80211_CHAN_5GHZ = 1;
constexpr int IWM_MIN_DBM = -100;
enum { IEEE80211_S_SCAN, IEEE80211_S_RUN };
enum { IEEE80211_M_STA, IEEE80211_M_HOSTAP };
struct ieee80211_channel { uint16_t number = 0; unsigned ic_flags = 0; };
#define IEEE80211_CHAN_ANYC reinterpret_cast<ieee80211_channel *>(-1)
struct ieee80211_node {
    ieee80211_channel *ni_chan = nullptr;
    uint8_t ni_macaddr[6]{}, ni_bssid[6]{}, ni_essid[32]{};
    uint8_t ni_esslen = 0, ni_rssi = 0, ni_dtimcount = 0, ni_dtimperiod = 1;
    const uint8_t *ni_rsnie_tlv = nullptr;
    uint32_t ni_rsnie_tlv_len = 0;
    uint16_t ni_intval = 100, ni_capinfo = 0;
    uint64_t ni_scan_rssi_stamp = 0, ni_scan_rssi_published_stamp = 0;
    uint8_t ni_scan_rssi = 0, ni_scan_rssi_chan = 0;
};
struct ieee80211com {
    int ic_state = IEEE80211_S_RUN, ic_opmode = IEEE80211_M_STA;
    ieee80211_node *ic_bss = nullptr, *cache = nullptr;
};
struct ieee80211_rxinfo { int rxi_rssi = 0; uint8_t rxi_chan = 0; };
static uint64_t clockUs = 1000001;
static void microuptime(timeval *tv)
{
    tv->tv_sec = clockUs / 1000000;
    tv->tv_usec = clockUs % 1000000;
}
static unsigned lockDepth;
static int splnet() { return lockDepth++; }
static void splx(int previous)
{
    assert(lockDepth == static_cast<unsigned>(previous + 1));
    lockDepth = previous;
}
static ieee80211_node *ieee80211_find_node(ieee80211com *ic, const uint8_t *mac)
{
    assert(lockDepth != 0);
    return ic->cache && IEEE80211_ADDR_EQ(ic->cache->ni_macaddr, mac)
        ? ic->cache : nullptr;
}
static unsigned ieee80211_chan2ieee(ieee80211com *, const ieee80211_channel *ch)
{ return ch->number; }
struct ItlHalService {
    ieee80211com *ic;
    ieee80211com *get80211Controller() { return ic; }
};
using TahoeWclScanResultPayload = TahoeBssManagerContracts::BeaconPayload;
// Use the actual ABI but retain the producer's local field spelling.
#define ieLen ieLength
#define chanSpec channelSpec
#define ssidLen ssidLength
constexpr uint32_t kTahoeWclScanResultHeaderLen = 0x44;
struct TahoeWclSignalSnapshot { int16_t noiseDbm; bool hasNoise; };
static uint16_t buildTahoeWclCurrentBssChanSpec(ieee80211com *,
    const ieee80211_channel *ch)
{
    return ch && ch != IEEE80211_CHAN_ANYC
        ? ch->number | (ch->ic_flags ? 0xc000 : 0) : 0;
}
static uint32_t buildTahoeCurrentBssIeStream(const ieee80211_node *ni,
    uint8_t *dst, uint32_t capacity)
{
    return TahoeBeaconIeBuilder::buildCurrentBssIeStream(ni->ni_essid,
        ni->ni_esslen, ni->ni_dtimcount, ni->ni_dtimperiod, ni->ni_rsnie_tlv,
        ni->ni_rsnie_tlv_len, dst, capacity);
}

// PRODUCTION_FUNCTIONS

static bool measured(const TahoeWclScanResultSnapshot &entry)
{ return (entry.payload.meta.flags & 0x4000) != 0; }
static TahoeWclScanResultSnapshot snapshot(ieee80211com &ic)
{
    TahoeWclScanResultSnapshot result{};
    TahoeWclScanSnapshotCollector collector{{-95, true}, &ic, &result, 1, 0, false};
    const int level = splnet();
    collectTahoeWclScanResultSnapshot(&collector, ic.cache);
    splx(level);
    assert(collector.count == 1 && !collector.overflow);
    return result;
}

int main()
{
    ieee80211_channel ch{9, 0}, other{153, IEEE80211_CHAN_5GHZ};
    ieee80211_node node;
    node.ni_chan = &ch;
    node.ni_macaddr[0] = node.ni_bssid[0] = 2;
    node.ni_esslen = 1; node.ni_essid[0] = 'x';
    node.ni_rssi = 80; // Older selection peak must not become a fresh sample.
    ieee80211_node current = node;
    ieee80211com ic{IEEE80211_S_RUN, IEEE80211_M_STA, &current, &node};
    ItlHalService hal{&ic};
    auto empty = snapshot(ic);
    assert(!measured(empty));
    ieee80211_rxinfo rx{56, 9};
    ieee80211_record_scan_rssi(&ic, &node, &rx, 9);
    assert(node.ni_scan_rssi_stamp == clockUs && node.ni_scan_rssi == 56);
    assert(current.ni_scan_rssi_stamp == clockUs && current.ni_scan_rssi == 56);
    assert(node.ni_rssi == 80 && current.ni_rssi == 80);
    auto first = snapshot(ic);
    assert(measured(first)); // Old metadata producers fail this assertion.
    assert(empty.payload.meta.rssi == 0);
    assert(first.payload.meta.rssi == -44);
    assert((first.payload.meta.flags & 0x4040) == 0x4040);
    assert(first.payload.meta.noise == -95 && (first.payload.meta.flags & 0x1000));
    assert(!(first.payload.meta.flags & 0x2000));
    assert(node.ni_scan_rssi_published_stamp == 0); // Snapshot is not delivery.
    auto cancelled = snapshot(ic);
    (void)cancelled; // A cancelled terminal never calls the receipt helper.
    auto delivered = snapshot(ic);
    prepareTahoeWclScanRssiPublication(&ic, delivered);
    assert(measured(delivered) && node.ni_scan_rssi_published_stamp == 0);
    recordTahoeWclScanRssiPublication(&ic, delivered);
    assert(node.ni_scan_rssi_published_stamp == clockUs);
    auto replay = snapshot(ic);
    assert(!measured(replay) && replay.payload.meta.rssi == -44);
    assert(replay.payload.meta.flags & 0x40);
    prepareTahoeWclScanRssiPublication(&ic, first); // Earlier duplicate snapshot.
    assert(!measured(first));
    recordTahoeWclScanRssiPublication(&ic, first);
    assert(node.ni_scan_rssi_published_stamp == clockUs);

    TahoeBssManagerContracts::BeaconPayload bss{};
    assert(buildTahoeCurrentBssPayload(&hal, &bss));
    assert(bss.meta.rssi == -44 && (bss.meta.flags & 0x4040) == 0x4040);
    assert(node.ni_scan_rssi_published_stamp == clockUs);

    clockUs += 100000;
    rx.rxi_rssi = 50;
    ieee80211_record_scan_rssi(&ic, &node, &rx, 9);
    auto older = snapshot(ic);
    const uint64_t olderStamp = clockUs;
    clockUs += 100000;
    rx.rxi_rssi = 49;
    ieee80211_record_scan_rssi(&ic, &node, &rx, 9);
    // A new RX between snapshot and publication cannot be consumed by old data.
    recordTahoeWclScanRssiPublication(&ic, older);
    assert(node.ni_scan_rssi_published_stamp < olderStamp);
    prepareTahoeWclScanRssiPublication(&ic, older);
    assert(!measured(older));
    auto newer = snapshot(ic);
    assert(measured(newer) && newer.payload.meta.rssi == -51);
    auto staleIdentity = newer;
    staleIdentity.payload.meta.bssid[1] = 1;
    prepareTahoeWclScanRssiPublication(&ic, staleIdentity);
    assert(!measured(staleIdentity));
    staleIdentity = newer;
    staleIdentity.nodeMac[1] = 1;
    recordTahoeWclScanRssiPublication(&ic, staleIdentity);
    assert(node.ni_scan_rssi_published_stamp < olderStamp);
    ic.cache = nullptr;
    prepareTahoeWclScanRssiPublication(&ic, newer);
    assert(!measured(newer));
    ic.cache = &node;
    assert(measured(snapshot(ic)));

    for (int invalid : {-100, 0, 100, 255}) {
        rx.rxi_rssi = invalid;
        ieee80211_record_scan_rssi(&ic, &node, &rx, 9);
        assert(!measured(snapshot(ic)) && node.ni_scan_rssi_stamp == 0);
        assert(buildTahoeCurrentBssPayload(&hal, &bss));
        assert(bss.meta.rssi == 0 && !(bss.meta.flags & 0x4040));
    }
    rx.rxi_rssi = 1;
    ieee80211_record_scan_rssi(&ic, &node, &rx, 9);
    assert(snapshot(ic).payload.meta.rssi == -99);
    rx.rxi_rssi = 99;
    ieee80211_record_scan_rssi(&ic, &node, &rx, 9);
    assert(snapshot(ic).payload.meta.rssi == -1);
    for (uint8_t channel : {uint8_t(0), uint8_t(13)}) {
        rx.rxi_chan = channel;
        ieee80211_record_scan_rssi(&ic, &node, &rx, 9);
        assert(!measured(snapshot(ic)));
    }
    node.ni_chan = &other;
    rx = {22, 153}; clockUs += 100000;
    const uint64_t currentStamp = current.ni_scan_rssi_stamp;
    ieee80211_record_scan_rssi(&ic, &node, &rx, 153);
    auto five = snapshot(ic);
    assert(measured(five) && five.payload.meta.rssi == -78);
    assert(five.payload.meta.channelSpec == (0xc000 | 153));
    assert(current.ni_scan_rssi_stamp == currentStamp); // Different channel owner.
    current.ni_chan = &other; current.ni_bssid[1] = 1;
    ieee80211_record_scan_rssi(&ic, &node, &rx, 153);
    assert(current.ni_scan_rssi_stamp == currentStamp); // Different BSSID owner.
    current.ni_bssid[1] = 0; ic.ic_opmode = IEEE80211_M_HOSTAP;
    ieee80211_record_scan_rssi(&ic, &node, &rx, 153);
    assert(current.ni_scan_rssi_stamp == currentStamp); // Local AP is not RX RSSI.
    ic.ic_opmode = IEEE80211_M_STA; ic.ic_state = IEEE80211_S_SCAN;
    assert(!buildTahoeCurrentBssPayload(&hal, &bss));
    assert(!buildTahoeCurrentBssPayload(nullptr, &bss));
    assert(!ieee80211_scan_rssi_publication(nullptr, node.ni_macaddr,
        node.ni_bssid, clockUs, 22, 153, 1));
    assert(lockDepth == 0);
    std::puts("PASS: production measured RSSI, Apple metadata, current-BSS isolation, cancelled/stale/duplicate publication and channel validity");
}
