//
//  TahoeSkywalkIoctlRoutes.hpp
//  AirportItlwm
//

#ifndef TahoeSkywalkIoctlRoutes_hpp
#define TahoeSkywalkIoctlRoutes_hpp

#include <stdint.h>

namespace TahoeSkywalkIoctlRoutes {

enum : uint32_t {
    kIocSsid = 1,
    kIocAuthType = 2,
    kIocCipherKey = 3,
    kIocChannel = 4,
    kIocBssid = 9,
    kIocAssociate = 20,
    kIocDisassociate = 22,
    kIocRsnIe = 46,
    kIocBtcoexMode = 87,
    kIocCurrentNetwork = 103,
    kIocTxChainPower = 108,
    kIocPeerCacheMaximumSize = 130,
    kIocPeerCacheControl = 143,
    kIocChainAck = 174,
    kIocDesense = 175,
    kIocDesenseLevel = 194,
    kIocBgscanCacheResults = 215,
    kIocRoamProfile = 216,
    kIocBtcoexProfiles = 221,
    kIocBtcoexConfig = 222,
    kIocChipCounterStats = 227,
    kIocDbgGuardTimeParams = 228,
    kIocBtcoexOptions = 235,
    kIocAwdlRsdbCaps = 246,
    kIocTkoParams = 251,
    kIocTkoDump = 252,
    kIocBtcoexProfile = 255,
    kIocBtcoexProfileActive = 256,
    kIocMaxNssForAp = 259,
    kIocBtcoex2GChainDisable = 260,
    kIocCurPmk = 360,
    kIocSetMacAddress = 368,
    kIocSoftapExtendedCapabilitiesIe = 403,
    kIocMisMaxSta = 508,
};

inline bool shouldRoute(uint32_t reqType, bool isSet)
{
    switch (reqType) {
        case kIocSsid:
        case kIocBssid:
        case kIocChannel:
        case kIocCurrentNetwork:
        case kIocBgscanCacheResults:
        case kIocAwdlRsdbCaps:
        case kIocChipCounterStats:
        case kIocTkoDump:
        case kIocMaxNssForAp:
        case kIocPeerCacheMaximumSize:
            return !isSet;

        case kIocRoamProfile:
        case kIocBtcoexProfiles:
        case kIocBtcoexConfig:
        case kIocBtcoexOptions:
        case kIocBtcoexMode:
        case kIocTxChainPower:
        case kIocChainAck:
        case kIocDesense:
        case kIocDesenseLevel:
        case kIocCurPmk:
        case kIocDbgGuardTimeParams:
        case kIocTkoParams:
        case kIocBtcoexProfile:
        case kIocBtcoexProfileActive:
        case kIocBtcoex2GChainDisable:
            return true;

        case kIocAssociate:
        case kIocDisassociate:
        case kIocAuthType:
        case kIocRsnIe:
        case kIocSetMacAddress:
        case kIocCipherKey:
        case kIocPeerCacheControl:
        case kIocSoftapExtendedCapabilitiesIe:
        case kIocMisMaxSta:
            return isSet;

        default:
            return false;
    }
}

} // namespace TahoeSkywalkIoctlRoutes

#endif /* TahoeSkywalkIoctlRoutes_hpp */
