//
//  TahoeLqmContracts.hpp
//  AirportItlwm
//

#ifndef TahoeLqmContracts_hpp
#define TahoeLqmContracts_hpp

#include <stddef.h>
#include <stdint.h>

namespace TahoeLqmContracts {

static constexpr uint32_t kInvalidArgumentRaw = 0x16;
static constexpr uint32_t kFeatureDisabledStatus = 0x2d;
static constexpr uint32_t kCarrierSize = 0x24;
static constexpr uint32_t kVersion = 1;
static constexpr uint32_t kMinimumIntervalMs = 1000;
static constexpr uint32_t kLowIntervalMs = 1000;
static constexpr uint32_t kHighIntervalMs = 5000;
static constexpr uint32_t kDefaultStatsIntervalMs = kHighIntervalMs;
static constexpr uint32_t kEventMessage = 0x27;
static constexpr uint32_t kEventSize = 0x1dc;
static constexpr uint32_t kThresholdOffset = 0x11;
static constexpr uint32_t kThresholdLength = 7;
static constexpr uint32_t kThresholdInvalidLow = 0x0b;
static constexpr uint32_t kThresholdInvalidHigh = 0x9b;
static constexpr uint32_t kTailOffset = 0x19;
static constexpr uint32_t kTailLength = 8;
static constexpr uint32_t kTailMaximumAcceptedValue = 99;
static constexpr uint32_t kLinkChangedSnrOffset = 0x08;
static constexpr uint32_t kLinkChangedNfOffset = 0x0a;
static constexpr int32_t kInvalidNoiseZero = 0;
static constexpr int32_t kInvalidNoiseSentinel = -127;
static constexpr int32_t kMaximumSnr = 127;

struct CounterSnapshot {
    uint32_t txErrors;
    uint32_t rxErrors;
    uint32_t txFrames;
    uint32_t rxFrames;
    uint32_t beaconFrames;
};

// AppleBCMWLANLQM::updateLQM builds this fixed carrier before posting message
// 0x27. Only fields recovered from current 26.3 code are named; the untouched
// tail remains opaque so new semantics are not invented for unknown offsets.
struct EventData {
    uint8_t hasRssi;                 // 0x000
    uint8_t reserved001[3];
    int32_t rssi;                    // 0x004
    uint8_t hasPerAntennaRssi;       // 0x008
    int8_t perAntennaRssi[2];        // 0x009
    uint8_t hasSnr;                  // 0x00b
    int16_t snr;                     // 0x00c
    uint8_t hasNoise;                // 0x00e
    uint8_t reserved00f;
    int16_t noise;                   // 0x010
    uint8_t hasCurrentBssRssi;       // 0x012
    int8_t currentBssRssi;           // 0x013
    uint32_t txErrors;               // 0x014
    uint32_t rxErrors;               // 0x018
    uint32_t txFrames;               // 0x01c
    uint32_t reserved020;
    uint32_t rxFrames;               // 0x024
    uint32_t beaconFrames;           // 0x028
    uint32_t reserved02c;
    uint8_t countersValid;           // 0x030
    uint8_t reserved031;
    uint8_t opaque032[0x1d8 - 0x032];
    uint8_t eventValid;               // 0x1d8
    uint8_t counterSnapshotChanged;   // 0x1d9
    uint8_t opaque1da[2];
} __attribute__((packed));

static_assert(sizeof(EventData) == kEventSize,
              "Tahoe LQM event must preserve the recovered 0x1dc ABI");
static_assert(offsetof(EventData, hasRssi) == 0x00,
              "Tahoe LQM RSSI validity offset mismatch");
static_assert(offsetof(EventData, rssi) == 0x04,
              "Tahoe LQM RSSI offset mismatch");
static_assert(offsetof(EventData, hasSnr) == 0x0b,
              "Tahoe LQM SNR validity offset mismatch");
static_assert(offsetof(EventData, snr) == 0x0c,
              "Tahoe LQM SNR offset mismatch");
static_assert(offsetof(EventData, hasNoise) == 0x0e,
              "Tahoe LQM noise validity offset mismatch");
static_assert(offsetof(EventData, noise) == 0x10,
              "Tahoe LQM noise offset mismatch");
static_assert(offsetof(EventData, countersValid) == 0x30,
              "Tahoe LQM counter-valid offset mismatch");
static_assert(offsetof(EventData, eventValid) == 0x1d8,
              "Tahoe LQM event-valid offset mismatch");
static_assert(offsetof(EventData, counterSnapshotChanged) == 0x1d9,
              "Tahoe LQM snapshot-generation offset mismatch");

inline bool isInvalidThresholdByte(uint8_t value)
{
    return static_cast<uint8_t>(value - kThresholdInvalidLow) <=
           static_cast<uint8_t>(kThresholdInvalidHigh - kThresholdInvalidLow);
}

inline bool hasInvalidInterval(uint32_t samplePeriodMs, uint32_t txPerIntervalMs,
                               uint32_t rxLossIntervalMs)
{
    return samplePeriodMs < kMinimumIntervalMs ||
           txPerIntervalMs < kMinimumIntervalMs ||
           rxLossIntervalMs < kMinimumIntervalMs;
}

inline bool hasInvalidThresholdBytes(const uint8_t *carrier)
{
    if (carrier == nullptr)
        return true;

    for (uint32_t i = 0; i < kThresholdLength; i++) {
        if (isInvalidThresholdByte(carrier[kThresholdOffset + i]))
            return true;
    }
    return false;
}

inline bool hasInvalidTailBytes(const uint8_t *carrier)
{
    if (carrier == nullptr)
        return true;

    for (uint32_t i = 0; i < kTailLength; i++) {
        if (carrier[kTailOffset + i] > kTailMaximumAcceptedValue)
            return true;
    }
    return false;
}

inline bool buildLinkChangedSignalMetrics(int32_t rssiDbm, int32_t noiseDbm,
                                          uint16_t *snrOut, uint16_t *nfOut)
{
    if (noiseDbm == kInvalidNoiseZero || noiseDbm == kInvalidNoiseSentinel ||
        snrOut == nullptr || nfOut == nullptr)
        return false;

    int32_t snr = rssiDbm - noiseDbm;
    if (snr < 0)
        snr = 0;
    if (snr > kMaximumSnr)
        snr = kMaximumSnr;

    *snrOut = static_cast<uint16_t>(snr);
    *nfOut = static_cast<uint16_t>(static_cast<int16_t>(noiseDbm));
    return true;
}

inline bool counterSnapshotsEqual(const CounterSnapshot &lhs,
                                  const CounterSnapshot &rhs)
{
    return lhs.txErrors == rhs.txErrors &&
           lhs.rxErrors == rhs.rxErrors &&
           lhs.txFrames == rhs.txFrames &&
           lhs.rxFrames == rhs.rxFrames &&
           lhs.beaconFrames == rhs.beaconFrames;
}

inline bool buildEventData(int32_t rssiDbm, int32_t noiseDbm,
                           const CounterSnapshot &current,
                           const CounterSnapshot *previous,
                           EventData *event)
{
    if (event == nullptr || rssiDbm < -100 || rssiDbm > 0)
        return false;

    *event = EventData{};
    event->hasRssi = 1;
    event->rssi = rssiDbm;
    event->hasCurrentBssRssi = 1;
    event->currentBssRssi = static_cast<int8_t>(rssiDbm);

    if (noiseDbm != kInvalidNoiseZero &&
        noiseDbm != kInvalidNoiseSentinel) {
        int32_t snrValue = rssiDbm - noiseDbm;
        if (snrValue < 0)
            snrValue = 0;
        if (snrValue > kMaximumSnr)
            snrValue = kMaximumSnr;
        event->hasSnr = 1;
        event->snr = static_cast<int16_t>(snrValue);
        event->hasNoise = 1;
        event->noise = static_cast<int16_t>(noiseDbm);
    }

    const bool snapshotChanged =
        previous == nullptr || !counterSnapshotsEqual(current, *previous);
    if (snapshotChanged) {
        event->txErrors = current.txErrors;
        event->rxErrors = current.rxErrors;
        event->txFrames = current.txFrames;
        event->rxFrames = current.rxFrames;
        event->beaconFrames = current.beaconFrames;
        event->countersValid = 1;
        event->counterSnapshotChanged = 1;
    }
    event->eventValid = 1;
    return true;
}

} // namespace TahoeLqmContracts

#endif /* TahoeLqmContracts_hpp */
