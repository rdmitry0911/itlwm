//
//  AirportItlwmAPSTAEventContracts.hpp
//  AirportItlwm
//

#ifndef AirportItlwmAPSTAEventContracts_hpp
#define AirportItlwmAPSTAEventContracts_hpp

#include "AirportItlwmAPSTAInterface.hpp"

namespace AirportItlwmAPSTAEventContracts {

inline bool associationStatusIsSuccessful(uint32_t status, uint32_t reason)
{
    return status == kAirportItlwmAPSTAEventSuccessStatus &&
           reason == kAirportItlwmAPSTAEventSuccessReason;
}

inline bool associationIsAdmitted(uint32_t status,
                                  uint32_t reason,
                                  bool foundAppleIE,
                                  uint8_t hiddenNetworkFlag)
{
    return associationStatusIsSuccessful(status, reason) &&
           (foundAppleIE || (hiddenNetworkFlag & 1U) == 0);
}

inline bool ouiMatches(const uint8_t *oui, const uint8_t *expected)
{
    if (oui == nullptr || expected == nullptr) {
        return false;
    }
    for (uint32_t i = 0; i < kAirportItlwmAPSTAAppleIEOuiSize; i++) {
        if (oui[i] != expected[i]) {
            return false;
        }
    }
    return true;
}

inline bool isRecognizedAppleOUI(const uint8_t *oui)
{
    return ouiMatches(oui, kAirportItlwmAPSTAAppleIEOui) ||
           ouiMatches(oui, kAirportItlwmAPSTAAppleIEBsOui) ||
           ouiMatches(oui, kAirportItlwmAPSTAAppleIEDeviceInfoOui);
}

inline bool isInstantHotspotOUI(const uint8_t *oui)
{
    return ouiMatches(oui, kAirportItlwmAPSTAAppleIEOui);
}

inline bool parseActionFrame(const uint8_t *data,
                             uint32_t dataLength,
                             uint8_t *category,
                             uint8_t *action)
{
    if (data == nullptr || category == nullptr || action == nullptr ||
        dataLength < kAirportItlwmAPSTAActionFrameMinimumLength) {
        return false;
    }

    *category = kAirportItlwmAPSTAActionFrameUnknownCategoryAction;
    *action = kAirportItlwmAPSTAActionFrameUnknownCategoryAction;

    const uint16_t rawVersion = static_cast<uint16_t>(data[0]) |
        static_cast<uint16_t>(static_cast<uint16_t>(data[1]) << 8);
    const uint16_t swappedVersion = static_cast<uint16_t>(
        static_cast<uint16_t>(rawVersion << 8) |
        static_cast<uint16_t>(rawVersion >> 8));
    if (swappedVersion >= kAirportItlwmAPSTAActionFrameVersionSwapRejectThreshold) {
        return false;
    }

    if (rawVersion == kAirportItlwmAPSTAActionFrameVersion1) {
        *category = data[kAirportItlwmAPSTAActionFrameVersion1CategoryOffset];
        *action = data[kAirportItlwmAPSTAActionFrameVersion1ActionOffset];
    } else if (rawVersion == kAirportItlwmAPSTAActionFrameVersion2) {
        if (dataLength < kAirportItlwmAPSTAActionFrameVersion2MinimumLength) {
            return false;
        }
        *category = data[kAirportItlwmAPSTAActionFrameVersion2CategoryOffset];
        *action = data[kAirportItlwmAPSTAActionFrameVersion2ActionOffset];
    }

    return true;
}

inline bool isLphsStateAction(uint8_t category, uint8_t action)
{
    return category == kAirportItlwmAPSTAActionFrameLphsCategory &&
           (action == kAirportItlwmAPSTAActionFrameLphsActionSleep ||
            action == kAirportItlwmAPSTAActionFrameLphsActionAwake);
}

inline bool softAPConcurrencyIsEnabled(bool feature46,
                                       uint8_t corePrivateFeatureByte)
{
    return feature46 &&
           (corePrivateFeatureByte &
            kAirportItlwmAPSTAConcurrencyCorePrivateMask) != 0;
}

} // namespace AirportItlwmAPSTAEventContracts

#endif /* AirportItlwmAPSTAEventContracts_hpp */
