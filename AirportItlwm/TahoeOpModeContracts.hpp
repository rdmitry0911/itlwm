//
//  TahoeOpModeContracts.hpp
//  AirportItlwm
//

#ifndef TahoeOpModeContracts_hpp
#define TahoeOpModeContracts_hpp

#include <stdint.h>

namespace TahoeOpModeContracts {

static constexpr uint32_t kInvalidArgumentStatus = 0x16;
static constexpr uint32_t kPrimaryVersion = 1;
static constexpr uint32_t kPrimaryInitialMode = 0x00;
static constexpr uint32_t kAssociatedStaMode = 0x01;
static constexpr uint32_t kAssociatedIbssMode = 0x02;
static constexpr uint32_t kCurrentBssIbssCapabilityBit = 0x02;
static constexpr uint32_t kPrimaryMonitorBit = 0x10;
static constexpr uint32_t kPrimaryAssociatedModeMutationCount = 1;

template <typename OpModeData>
inline bool initializePrimaryCarrier(OpModeData *data)
{
    if (data == nullptr)
        return false;

    data->version = kPrimaryVersion;
    data->op_mode = kPrimaryInitialMode;
    return true;
}

inline uint32_t modeForAssociatedBss(uint32_t currentBssCapability)
{
    return (currentBssCapability & kCurrentBssIbssCapabilityBit) != 0
               ? kAssociatedIbssMode
               : kAssociatedStaMode;
}

template <typename OpModeData>
inline void publishAssociatedBssMode(OpModeData *data,
                                     uint32_t currentBssCapability)
{
    if (data == nullptr)
        return;

    data->op_mode |= modeForAssociatedBss(currentBssCapability);
}

} // namespace TahoeOpModeContracts

#endif /* TahoeOpModeContracts_hpp */
