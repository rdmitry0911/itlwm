//
//  TahoeCommandRouter.hpp
//  AirportItlwm
//

#ifndef TahoeCommandRouter_hpp
#define TahoeCommandRouter_hpp

#include <stdint.h>

namespace TahoeCommandRouter {

enum OwnerTarget : uint32_t {
    kHiddenInterfaceOwner = 0x1510,
    kCommanderOwner = 0x1520,
    kJoinOwner = 0x1528,
    kNetAdapterOwner = 0x15e0,
    kNdpOwner = 0x2c20,
    kRangingOwner = 0x2c28,
};

inline uint32_t routeIE(bool customAssoc)
{
    return customAssoc ? kJoinOwner : kCommanderOwner;
}

inline uint32_t routeUSBHostNotification()
{
    return kHiddenInterfaceOwner;
}

inline uint32_t routeBtcoex()
{
    return kCommanderOwner;
}

inline uint32_t routeBypassTxPowerCap()
{
    return kCommanderOwner;
}

inline uint32_t routeActionFrame()
{
    return kNetAdapterOwner;
}

inline uint32_t routeOffloadNdp()
{
    return kNdpOwner;
}

inline uint32_t routeRanging()
{
    return kRangingOwner;
}

} // namespace TahoeCommandRouter

#endif /* TahoeCommandRouter_hpp */
