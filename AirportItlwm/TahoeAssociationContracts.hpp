//
//  TahoeAssociationContracts.hpp
//  AirportItlwm
//

#ifndef TahoeAssociationContracts_hpp
#define TahoeAssociationContracts_hpp

#include <stdint.h>

namespace TahoeAssociationContracts {

static constexpr uint32_t kHiddenAssociateSelector = 0x45;
static constexpr uint32_t kHiddenAssociateCompleteSelector = 0x46;
static constexpr uint32_t kAssocCandidatesPayloadLength = 0x3ad8;

static constexpr uint32_t kUnassocDwellOffset = 0x04;
static constexpr uint32_t kApModeOffset = 0x0c;
static constexpr uint32_t kAuthLowerOffset = 0x10;
static constexpr uint32_t kAuthUpperOffset = 0x14;
static constexpr uint32_t kAuthFlagsOffset = 0x18;
static constexpr uint32_t kAssociatedAuthTypeVersionOffset = 0x00;
static constexpr uint32_t kAssociatedAuthTypeLowerOffset = 0x04;
static constexpr uint32_t kAssociatedAuthTypeUpperOffset = 0x08;
static constexpr uint32_t kAssociatedAuthTypePayloadLength = 0x0c;
static constexpr uint32_t kSsidLengthOffset = 0x1c;
static constexpr uint32_t kSsidOffset = 0x20;
static constexpr uint32_t kKeyOffset = 0x40;
static constexpr uint32_t kKeyLengthOffset = 0x48;
static constexpr uint32_t kRsnIeLengthOffset = 0xd4;
static constexpr uint32_t kRsnIeOffset = 0xd6;
static constexpr uint32_t kInstantHotspotFlagsOffset = 0x1e0;
static constexpr uint32_t kInstantHotspotCompanionOffset = 0x1e1;
static constexpr uint32_t kWsecExtFlagsOffset = 0x1e8;
static constexpr uint32_t kWsecModeFlagsOffset = 0x1ec;
static constexpr uint32_t kContextBssidOffset = 0x1f4;
static constexpr uint32_t kPmfCapabilityOffset = 0x217;
static constexpr uint32_t kBssInfoFlagsOffset = 0x214;
static constexpr uint32_t kCandidateCountOffset = 0x218;
static constexpr uint32_t kFirstCandidateBssidOffset = 0x220;
static constexpr uint32_t kFirstCandidatePairedMacOffset = 0x226;
static constexpr uint32_t kFirstCandidateChannelOffset = 0x22c;
static constexpr uint32_t kCandidateStride = 0x12;

static constexpr uint32_t kJoinIoctlBssTypeSelector = 0x14;
static constexpr uint32_t kJoinIoctlAuthTypeSelector = 0x16;
static constexpr uint32_t kJoinIoctlAssocSelector = 0x1a;
static constexpr uint32_t kSetAssocWsecInfoSelector = 0x107;
static constexpr uint32_t kFinalAssocWsecInfoSelector = 0x10c;
static constexpr uint32_t kInvalidCarrierStatus = 0xe00002c2;
static constexpr uint32_t kPublicProtmodeUnsupportedStatus = 0xe00002c7;
static constexpr uint32_t kPublicRsnIeGateSelector = 0x29;
static constexpr uint32_t kPublicRsnIeVendorVirtualOffset = 0x398;
static constexpr uint32_t kPublicRsnIeNoOwnerStatus = 0xe082280e;
static constexpr uint32_t kPublicSetRsnIeReturn = 0;
static constexpr uint32_t kPublicSetRsnIeMutationCount = 0;

static constexpr uint8_t kPmfCapableBit = 0x40;
static constexpr uint16_t kInstantHotspotFlagMask = 0x0006;

inline bool isHiddenAssocCommand(int command)
{
    return command == static_cast<int>(kHiddenAssociateSelector) ||
           command == static_cast<int>(kHiddenAssociateCompleteSelector);
}

inline bool isAssocCandidatesPayloadLength(uint32_t length)
{
    return length == kAssocCandidatesPayloadLength;
}

inline uint16_t boundedRsnIeLength(uint16_t length, uint16_t capacity)
{
    return length <= capacity ? length : capacity;
}

inline uint8_t instantHotspotAppleDeviceFlags(uint16_t flags)
{
    if ((flags & kInstantHotspotFlagMask) == 0)
        return 0;
    return static_cast<uint8_t>((flags & 0x0002U) | ((flags >> 2) & 0x0001U));
}

inline bool pmfCapable(uint8_t field)
{
    return (field & kPmfCapableBit) != 0;
}

static_assert(kFirstCandidatePairedMacOffset - kFirstCandidateBssidOffset == 0x06,
              "Tahoe WCL candidate stores BSSID then peer MAC");
static_assert(kFirstCandidateChannelOffset - kFirstCandidateBssidOffset == 0x0c,
              "Tahoe WCL candidate channel offset mismatch");
static_assert(kCandidateCountOffset + 0x08 == kFirstCandidateBssidOffset,
              "Tahoe WCL candidate list follows count plus reserved dword");

} // namespace TahoeAssociationContracts

#endif /* TahoeAssociationContracts_hpp */
