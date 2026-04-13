//
//  TahoeErrorMap.hpp
//  AirportItlwm
//

#ifndef TahoeErrorMap_hpp
#define TahoeErrorMap_hpp

#include <IOKit/IOTypes.h>

namespace TahoeErrorMap {

static constexpr IOReturn kAppleInvalidArgumentRaw = static_cast<IOReturn>(0x16);
static constexpr IOReturn kAppleBadArgumentTahoe = static_cast<IOReturn>(0xe00002bc);
static constexpr IOReturn kAppleInvalidArgument = static_cast<IOReturn>(0xe00002c2);
static constexpr IOReturn kAppleUnsupported = static_cast<IOReturn>(0xe00002c7);
static constexpr IOReturn kAppleNoMemory = static_cast<IOReturn>(0xe00002bd);
static constexpr IOReturn kAppleRangingInvalid = static_cast<IOReturn>(0xe0000001);
static constexpr IOReturn kAppleTvpmAlreadySet = static_cast<IOReturn>(0xe3ff8117);

inline IOReturn passthrough(uint32_t status)
{
    return static_cast<IOReturn>(status);
}

inline IOReturn normalizeSuccessOrTvpm(uint32_t status)
{
    if (status == 0 || status == static_cast<uint32_t>(kAppleTvpmAlreadySet))
        return kIOReturnSuccess;
    return static_cast<IOReturn>(status);
}

} // namespace TahoeErrorMap

#endif /* TahoeErrorMap_hpp */
