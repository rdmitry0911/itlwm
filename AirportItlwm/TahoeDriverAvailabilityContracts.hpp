//
//  TahoeDriverAvailabilityContracts.hpp
//  AirportItlwm
//

#ifndef TahoeDriverAvailabilityContracts_hpp
#define TahoeDriverAvailabilityContracts_hpp

#include "../include/Airport/apple80211_ioctl.h"

namespace TahoeDriverAvailabilityContracts {

enum class Transition : uint8_t {
    BootReady,
    PowerOff,
    PowerOn,
};

static constexpr uint32_t kVersion = 3;
static constexpr uint32_t kBootReadyFlags = 0x20;
static constexpr uint32_t kBootReadyReason = 0xe0822803;
static constexpr uint32_t kPowerOffReason = 0xe0821804;
static constexpr uint32_t kPowerOnReason = 0xe0821803;

inline apple80211_driver_available_data build(Transition transition)
{
    apple80211_driver_available_data payload = {};
    payload.version = kVersion;

    switch (transition) {
    case Transition::BootReady:
        payload.flags = kBootReadyFlags;
        payload.available = 1;
        payload.reason = kBootReadyReason;
        break;
    case Transition::PowerOff:
        payload.reason = kPowerOffReason;
        break;
    case Transition::PowerOn:
        payload.available = 1;
        payload.reason = kPowerOnReason;
        break;
    }

    return payload;
}

} // namespace TahoeDriverAvailabilityContracts

#endif /* TahoeDriverAvailabilityContracts_hpp */
