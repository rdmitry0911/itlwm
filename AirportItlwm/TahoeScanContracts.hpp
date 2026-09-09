//
//  TahoeScanContracts.hpp
//  AirportItlwm
//

#ifndef TahoeScanContracts_hpp
#define TahoeScanContracts_hpp

#include <stddef.h>
#include <stdint.h>

#include "TahoeLqmContracts.hpp"

namespace TahoeScanContracts {

static constexpr uint32_t kWclScanResultMetaFlags = 0x2;
static constexpr uint32_t kWclScanResultSsidPresentFlag = 0x4;
static constexpr uint32_t kWclScanResultNoisePresentFlag = 1U << 12;
static constexpr uint32_t kWclScanResultSnrPresentFlag = 1U << 13;
static constexpr uint32_t kWclScanResultRssiPresentFlag = 1U << 14;
static constexpr uint32_t kWclScanResultRssiOnChannelFlag = 1U << 6;
static constexpr uint32_t kWclScanResultSignalPresentFlags =
    kWclScanResultNoisePresentFlag | kWclScanResultSnrPresentFlag |
    kWclScanResultRssiPresentFlag;
static constexpr size_t kBssidLength = 6;

inline uint32_t buildWclScanResultMetaFlags(uint8_t ssidLength)
{
    return kWclScanResultMetaFlags |
        (ssidLength != 0 ? kWclScanResultSsidPresentFlag : 0);
}

inline bool hasMeasuredRssi(uint64_t sampleStamp, uint8_t normalizedRssi,
                            uint16_t measuredChannel, uint16_t bssChannel)
{
    return sampleStamp != 0 && normalizedRssi > 0 && normalizedRssi < 100 &&
        measuredChannel != 0 && measuredChannel == bssChannel;
}

/* Bit 14 gates the actual RSSI + timestamp write in Apple's BSS consumer.
 * Bit 6 says the sample was received on the advertised channel. A cached
 * replay keeps its provenance but must not refresh the RSSI timestamp. */
inline uint32_t buildMeasuredRssiFlags(uint64_t sampleStamp,
                                      uint64_t publishedStamp,
                                      uint8_t normalizedRssi,
                                      uint16_t measuredChannel,
                                      uint16_t bssChannel)
{
    if (!hasMeasuredRssi(sampleStamp, normalizedRssi,
                         measuredChannel, bssChannel))
        return 0;
    return kWclScanResultRssiOnChannelFlag |
        (sampleStamp > publishedStamp ? kWclScanResultRssiPresentFlag : 0);
}

inline bool hasRenderableBssid(const uint8_t *bssid)
{
    if (bssid == nullptr)
        return false;

    uint8_t any = 0;
    for (size_t i = 0; i < kBssidLength; i++)
        any |= bssid[i];
    return any != 0;
}

/*
 * A Tahoe 0xc9 WCL scan result carries a measured noise floor at
 * BeaconMetaData +0x34.  A zero or -127 dBm sample is not a measurement and
 * therefore leaves the field and its validity bit unset.  The Intel scan
 * node does not expose a separate firmware per-BSS SNR sample, so callers
 * deliberately leave +0x36 and its independent validity bit clear rather
 * than presenting a derived value as a measurement.
 */
inline bool buildWclScanResultNoiseMetric(int32_t noiseDbm,
                                          int16_t *noiseOut)
{
    if (noiseOut == nullptr)
        return false;

    if (noiseDbm == TahoeLqmContracts::kInvalidNoiseZero ||
        noiseDbm == TahoeLqmContracts::kInvalidNoiseSentinel ||
        noiseDbm < -32768 || noiseDbm > 32767)
        return false;

    *noiseOut = static_cast<int16_t>(noiseDbm);
    return true;
}

} // namespace TahoeScanContracts

#endif /* TahoeScanContracts_hpp */
