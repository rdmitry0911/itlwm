//
//  TahoeScanContracts.hpp
//  AirportItlwm
//

#ifndef TahoeScanContracts_hpp
#define TahoeScanContracts_hpp

#include <stddef.h>
#include <stdint.h>

namespace TahoeScanContracts {

static constexpr uint32_t kWclScanResultMetaFlags = 0x2;
static constexpr uint32_t kWclScanResultSsidPresentLegacyMask = 0x4;
static constexpr size_t kBssidLength = 6;

inline bool hasRenderableBssid(const uint8_t *bssid)
{
    if (bssid == nullptr)
        return false;

    uint8_t any = 0;
    for (size_t i = 0; i < kBssidLength; i++)
        any |= bssid[i];
    return any != 0;
}

} // namespace TahoeScanContracts

#endif /* TahoeScanContracts_hpp */
