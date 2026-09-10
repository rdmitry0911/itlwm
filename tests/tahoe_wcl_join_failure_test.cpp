#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include "include/Airport/apple80211_ioctl.h"
#include "AirportItlwm/TahoeOwnerRegistry.hpp"
#include "AirportItlwm/TahoeWclJoinFailure.hpp"

using IOReturn = int;
enum { kIOReturnSuccess, kIOReturnNotReady, kIOReturnBadArgument };
struct OSObject { virtual ~OSObject() = default; };
#define OSDynamicCast(type, object) dynamic_cast<type *>(object)
#define IEEE80211_ADDR_EQ(a,b) (std::memcmp(a,b,6) == 0)
struct ieee80211com { uint64_t generation = 0; };
static bool ieee80211_wcl_join_generation_current(ieee80211com *ic, uint64_t generation) {
    return ic && generation != 0 && ic->generation == generation;
}
struct Hal {
    ieee80211com ic;
    ieee80211com *get80211Controller() { return &ic; }
};
struct Mail { unsigned int selector; std::vector<uint8_t> bytes; };
struct AirportItlwm : OSObject {
    Hal hal;
    Hal *fHalService = &hal;
    void *fNetIf = this;
    TahoeOwnerRegistry registry;
    std::vector<Mail> messages;
    TahoeOwnerRegistry &getTahoeOwnerRegistry() { return registry; }
    void postMessage(void *interface, unsigned int selector, void *bytes,
        unsigned int length, bool async) {
        assert(interface == fNetIf && async);
        const auto *data = static_cast<const uint8_t *>(bytes);
        messages.push_back({selector, std::vector<uint8_t>(data, data + length)});
    }
};
#include "join-controller.inc"

static const uint8_t bssid[] = {2, 4, 6, 8, 10, 12};
static const uint8_t ssid[] = {'L', 'a', 'b'};
static void put16(uint8_t *out, unsigned int offset, uint16_t value) {
    out[offset] = value & 0xff;
    out[offset + 1] = value >> 8;
}
static void put32(uint8_t *out, unsigned int offset, uint32_t value) {
    for (unsigned int i = 0; i < 4; i++)
        out[offset + i] = (value >> (8 * i)) & 0xff;
}
struct Case {
    ieee80211_join_phase phase;
    ieee80211_join_failure_cause cause;
    uint16_t peerCode;
    uint16_t overall;
    uint16_t secondary;
    uint32_t authStatus, authReason, assocStatus, assocReason, beaconStatus;
};
int main() {
    const Case cases[] = {
        {IEEE80211_JOIN_DISCOVERY, IEEE80211_JOIN_FAILURE_NO_NETWORKS,
         0, 1, 1000, 0, 0, 0, 0, 0xe0820403},
        {IEEE80211_JOIN_DISCOVERY, IEEE80211_JOIN_FAILURE_TIMEOUT,
         0, 16, 0xffff, 0, 0, 0, 0, 0xe0820402},
        {IEEE80211_JOIN_AUTH, IEEE80211_JOIN_FAILURE_TIMEOUT,
         0, 16, 1002, 0xe0820402, 0, 0, 0, 0xe0820401},
        {IEEE80211_JOIN_AUTH, IEEE80211_JOIN_FAILURE_NO_ACK,
         0, 1, 1001, 0xe0820405, 0, 0, 0, 0xe0820401},
        {IEEE80211_JOIN_AUTH, IEEE80211_JOIN_FAILURE_PEER_STATUS,
         15, 15, 0xffff, 0xe0820401, 0xe082100f, 0, 0, 0xe0820401},
        {IEEE80211_JOIN_ASSOC, IEEE80211_JOIN_FAILURE_TIMEOUT,
         0, 16, 1004, 0, 0, 0xe0820402, 0, 0xe0820401},
        {IEEE80211_JOIN_ASSOC, IEEE80211_JOIN_FAILURE_NO_ACK,
         0, 1, 1003, 0, 0, 0xe0820405, 0, 0xe0820401},
        {IEEE80211_JOIN_ASSOC, IEEE80211_JOIN_FAILURE_PEER_STATUS,
         17, 17, 0xffff, 0, 0, 0xe0820401, 0xe0821011, 0xe0820401},
        {IEEE80211_JOIN_ASSOC, IEEE80211_JOIN_FAILURE_PEER_STATUS,
         77, 77, 0xffff, 0, 0, 0xe0820401, 0xe3ff8100, 0xe0820401},
        {IEEE80211_JOIN_KEYS, IEEE80211_JOIN_FAILURE_TIMEOUT,
         0, 1014, 1005, 0, 0, 0, 0, 0xe0820401},
        {IEEE80211_JOIN_AUTH, IEEE80211_JOIN_FAILURE_PEER_REASON,
         15, 15, 0xffff, 0, 0, 0, 0, 0xe0820401},
        {IEEE80211_JOIN_ASSOC, IEEE80211_JOIN_FAILURE_PEER_REASON,
         0, 12, 0xffff, 0, 0, 0, 0, 0xe0820401},
        {IEEE80211_JOIN_KEYS, IEEE80211_JOIN_FAILURE_PEER_REASON,
         3, 3, 0xffff, 0, 0, 0, 0, 0xe0820401},
    };
    for (const auto &item : cases) {
        ieee80211_join_attempt attempt{};
        const uint64_t generation = ieee80211_join_attempt_begin(&attempt,
            bssid, ssid, sizeof(ssid));
        const uint64_t epoch = item.phase == IEEE80211_JOIN_DISCOVERY ? 0 : 71;
        if (epoch != 0)
            assert(ieee80211_join_attempt_bind(&attempt, generation, epoch,
                bssid, ssid, sizeof(ssid)));
        if (item.phase > IEEE80211_JOIN_AUTH)
            assert(ieee80211_join_attempt_note_success(&attempt, generation,
                epoch, IEEE80211_JOIN_AUTH));
        if (item.phase > IEEE80211_JOIN_ASSOC)
            assert(ieee80211_join_attempt_note_success(&attempt, generation,
                epoch, IEEE80211_JOIN_ASSOC));
        const bool peerLeft = item.cause == IEEE80211_JOIN_FAILURE_PEER_REASON;
        assert(ieee80211_join_attempt_fail(&attempt, generation, epoch,
            item.phase, item.cause, peerLeft ? 0 : item.peerCode,
            peerLeft ? item.peerCode : 0, 0, 1));
        assert(ieee80211_join_attempt_cleanup_done(&attempt, generation, 1));
        ieee80211_join_failure failure;
        assert(ieee80211_join_attempt_take_failure(&attempt, generation, &failure));
        TahoeWclJoinFailure::Payloads payload;
        std::memset(&payload, 0xa5, sizeof(payload));
        assert(TahoeWclJoinFailure::build(failure, &payload));
        assert(payload.publishAuthAssoc == (!peerLeft &&
            (item.phase == IEEE80211_JOIN_AUTH || item.phase == IEEE80211_JOIN_ASSOC)));
        assert(payload.publishFirstBeacon == (!peerLeft && item.phase != IEEE80211_JOIN_KEYS));
        uint8_t auth[28]{}, beacon[20]{}, connect[164]{};
        put16(auth, 0, item.overall);
        put16(auth, 2, item.secondary);
        auth[4] = item.phase > IEEE80211_JOIN_AUTH ||
            (item.phase == IEEE80211_JOIN_AUTH && !peerLeft);
        std::memcpy(auth + 5, bssid, sizeof(bssid));
        put32(auth, 12, item.authStatus);
        put32(auth, 16, item.authReason);
        put32(auth, 20, item.assocStatus);
        put32(auth, 24, item.assocReason);
        put16(beacon, 0, item.overall);
        put16(beacon, 2, item.secondary);
        std::memcpy(beacon + 4, bssid, sizeof(bssid));
        put32(beacon, 12, item.beaconStatus);
        put16(connect, 0, item.overall);
        put16(connect, 2, item.secondary);
        std::memcpy(connect + 4, bssid, sizeof(bssid));
        put32(connect, 12, item.assocStatus);
        put32(connect, 16, item.assocReason);
        assert(std::memcmp(&payload.authAssoc, auth, sizeof(auth)) == 0);
        assert(std::memcmp(&payload.firstBeacon, beacon, sizeof(beacon)) == 0);
        assert(std::memcmp(&payload.connect, connect, sizeof(connect)) == 0);

        for (unsigned int publicOwner = 0; publicOwner < 2; publicOwner++) {
            AirportItlwm controller;
            controller.hal.ic.generation = generation;
            auto &owner = publicOwner ? controller.registry.publicAssociation :
                controller.registry.association;
            owner.joinAttemptGeneration = generation;
            owner.hasCarrier = true;
            owner.authAssocCompletionArmed = true;
            owner.ssidLength = sizeof(ssid);
            std::memcpy(owner.ssid, ssid, sizeof(ssid));
            std::memcpy(owner.selectedBssid, bssid, sizeof(bssid));
            assert(postTahoeWclJoinFailureGated(&controller, &failure,
                nullptr, nullptr, nullptr) == kIOReturnSuccess);
            std::vector<unsigned int> expected;
            if (!peerLeft && item.phase == IEEE80211_JOIN_ASSOC)
                expected.push_back(78);
            if (payload.publishAuthAssoc)
                expected.push_back(211);
            if (payload.publishFirstBeacon)
                expected.push_back(212);
            expected.push_back(213);
            assert(controller.messages.size() == expected.size());
            for (size_t i = 0; i < expected.size(); i++)
                assert(controller.messages[i].selector == expected[i]);
            const auto &last = controller.messages.back().bytes;
            assert(last.size() == sizeof(connect));
            assert(std::memcmp(last.data(), connect, sizeof(connect)) == 0);
            assert(owner.joinTerminalObserved && owner.connectCompletionPublished);
            assert(postTahoeWclJoinFailureGated(&controller, &failure,
                nullptr, nullptr, nullptr) == kIOReturnNotReady);
            assert(controller.messages.size() == expected.size());
            /* A new request with the same network cannot consume an old
             * terminal, even when its upper owner has not been rearmed. */
            owner.joinTerminalObserved = false;
            owner.connectCompletionPublished = false;
            controller.hal.ic.generation++;
            assert(postTahoeWclJoinFailureGated(&controller, &failure,
                nullptr, nullptr, nullptr) == kIOReturnNotReady);
            assert(controller.messages.size() == expected.size());
            controller.hal.ic.generation = generation;
            owner.joinAttemptGeneration++;
            assert(postTahoeWclJoinFailureGated(&controller, &failure,
                nullptr, nullptr, nullptr) == kIOReturnNotReady);
            assert(controller.messages.size() == expected.size());
        }
        failure.generation = 0;
        assert(!TahoeWclJoinFailure::build(failure, &payload));
    }
    std::puts("PASS: 13 production WCL failure payloads, exact 28/20/164 bytes, stage-specific messages and ASSOC candidate records");
    std::puts("PASS: production controller failure dispatch, WCL/public owners, message order, duplicate and stale-generation rejection");
}
