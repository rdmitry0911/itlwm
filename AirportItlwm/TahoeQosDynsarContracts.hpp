//
//  TahoeQosDynsarContracts.hpp
//  AirportItlwm
//

#ifndef TahoeQosDynsarContracts_hpp
#define TahoeQosDynsarContracts_hpp

#include <stdint.h>

namespace TahoeQosDynsarContracts {

static constexpr uint32_t kDynSarFailSafeStartTicksOffset = 0x74e0;
static constexpr uint32_t kDynSarFailSafeShift = 0x0a;
static constexpr uint64_t kDynSarFailSafeWindow = 0x9502f9ULL;
static constexpr uint32_t kDynSarFailSafeLogLine = 0xdea9;

static constexpr uint32_t kCongestionControlFeatureOffset = 0x7584;
static constexpr uint8_t kCongestionControlFeatureBit = 0x01;
static constexpr uint32_t kUnsupportedStatus = 0xe00002c7;

static constexpr uint32_t kForceDisableAwdlAmpduOffset = 0x3764;
static constexpr uint32_t kForceAwdlAmpduOffset = 0x3768;
static constexpr uint32_t kHwFeatureFlagsOffset = 0x458c;
static constexpr uint32_t kSplitTxStatusOffset = 0x00dc;
static constexpr uint8_t kSplitTxStatusBit = 0x01;
static constexpr uint32_t kTxAddrResolveReqV4Offset = 0x2aa4;
static constexpr uint32_t kTxAddrResolveReqV6Offset = 0x2aa8;
static constexpr uint32_t kSlowWifiFeatureEnabledOffset = 0x7569;
static constexpr uint32_t kLowLatencyOwnerOffset = 0x2c28;
static constexpr uint32_t kTxBlankingStatusOffset = 0x4ce8;
static constexpr uint8_t kTxBlankingStatusBit = 0x01;

inline bool isDynSarFailSafeMode(uint64_t nowTicks, uint64_t startTicks)
{
    return ((nowTicks - startTicks) >> kDynSarFailSafeShift) <
           kDynSarFailSafeWindow;
}

inline bool congestionControlSupported(uint8_t featureByte)
{
    return (featureByte & kCongestionControlFeatureBit) != 0;
}

inline bool txBlankingStatusEnabled(uint8_t statusByte)
{
    return (statusByte & kTxBlankingStatusBit) != 0;
}

} // namespace TahoeQosDynsarContracts

#endif /* TahoeQosDynsarContracts_hpp */
