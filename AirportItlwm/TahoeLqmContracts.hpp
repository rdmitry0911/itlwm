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
static constexpr uint32_t kThresholdOffset = 0x11;
static constexpr uint32_t kThresholdLength = 7;
static constexpr uint32_t kThresholdInvalidLow = 0x0b;
static constexpr uint32_t kThresholdInvalidHigh = 0x9b;
static constexpr uint32_t kTailOffset = 0x19;
static constexpr uint32_t kTailLength = 8;
static constexpr uint32_t kTailMaximumAcceptedValue = 99;
static constexpr uint32_t kWclLqmEventSnrFlagOffset = 0x0b;
static constexpr uint32_t kWclLqmEventSnrValueOffset = 0x0c;
static constexpr uint32_t kWclLqmEventNfFlagOffset = 0x0e;
static constexpr uint32_t kWclLqmEventNfValueOffset = 0x10;
static constexpr uint32_t kLinkChangedSnrOffset = 0x08;
static constexpr uint32_t kLinkChangedNfOffset = 0x0a;
static constexpr int32_t kInvalidNoiseZero = 0;
static constexpr int32_t kInvalidNoiseSentinel = -127;
static constexpr int32_t kMaximumSnr = 127;

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

} // namespace TahoeLqmContracts

#endif /* TahoeLqmContracts_hpp */
