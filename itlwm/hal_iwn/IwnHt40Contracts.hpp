//
//  IwnHt40Contracts.hpp
//  itlwm
//

#ifndef IwnHt40Contracts_hpp
#define IwnHt40Contracts_hpp

#include <stdint.h>

namespace IwnHt40Contracts {

static constexpr unsigned int kPrimarySecondaryChannelDelta = 4;
static constexpr unsigned int kMaxNvmPowerChannel = 254;
static constexpr uint8_t kSecondaryOffsetNone = 0;
static constexpr uint8_t kSecondaryOffsetAbove = 1;
static constexpr uint8_t kSecondaryOffsetBelow = 3;

inline bool isPrimaryWithSecondaryAbove(unsigned int primary,
                                        unsigned int secondary)
{
    return primary <= kMaxNvmPowerChannel &&
        secondary <= kMaxNvmPowerChannel &&
        secondary >= primary &&
        secondary - primary == kPrimarySecondaryChannelDelta;
}

inline bool nvmPowerChannel(unsigned int primary, bool secondaryBelow,
                            unsigned int *powerChannel)
{
    if (powerChannel == nullptr)
        return false;
    if (primary > kMaxNvmPowerChannel)
        return false;
    if (secondaryBelow) {
        if (primary < kPrimarySecondaryChannelDelta)
            return false;
        *powerChannel = primary - kPrimarySecondaryChannelDelta;
    } else {
        *powerChannel = primary;
    }
    return true;
}

inline bool allowsLocalDirection(bool peerSupportsHt40,
                                 uint8_t secondaryOffset,
                                 bool localHasSecondaryAbove,
                                 bool localHasSecondaryBelow)
{
    if (!peerSupportsHt40)
        return false;
    if (secondaryOffset == kSecondaryOffsetAbove)
        return localHasSecondaryAbove;
    if (secondaryOffset == kSecondaryOffsetBelow)
        return localHasSecondaryBelow;
    return false;
}

inline bool allowsSgiForEffectiveHtWidth(bool effectiveHt40,
                                         bool peerSupportsSgi20,
                                         bool peerSupportsSgi40)
{
    return effectiveHt40 ? peerSupportsSgi40 : peerSupportsSgi20;
}

} // namespace IwnHt40Contracts

#endif
