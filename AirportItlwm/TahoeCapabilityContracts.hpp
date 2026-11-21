//
//  TahoeCapabilityContracts.hpp
//  AirportItlwm
//

#ifndef TahoeCapabilityContracts_hpp
#define TahoeCapabilityContracts_hpp

#include <stddef.h>
#include <stdint.h>

namespace TahoeCapabilityContracts {

static constexpr uint8_t kCardCapabilityByte0AppleBase = 0x6f;
static constexpr uint8_t kCardCapabilityShouldSupportTetheringMask = 0x80;
static constexpr uint8_t kCardCapabilityByte0 =
    kCardCapabilityByte0AppleBase | kCardCapabilityShouldSupportTetheringMask;
static constexpr uint8_t kCardCapabilityByte1 = 0xe6;
static constexpr uint8_t kCardCapabilityByte2 = 0x6f;
static constexpr uint8_t kCardCapabilityByte3 = 0x27;
static constexpr uint8_t kCardCapabilityByte5 = 0x40;
static constexpr uint8_t kCardCapabilityByte6 = 0x0c;
static constexpr uint8_t kCardCapabilityCurrentScanResultMask = 0x02;
static constexpr uint8_t kCardCapabilityCurrentNetworkProfileMask = 0x04;
static constexpr uint8_t kCardCapabilityByte7 =
    kCardCapabilityCurrentScanResultMask |
    kCardCapabilityCurrentNetworkProfileMask;
static constexpr uint8_t kCardCapabilityByte8 = 0x01;
static constexpr uint8_t kCardCapabilityByte9 = 0x02;
/*
 * AppleBCMWLANCore::getCARD_CAPABILITIES maps feature flag 0x41 (WPA3 SAE)
 * to capability byte 9, bit 0x08.  IO80211's Apple80211Associate2 separately
 * requires an MFP admission bit in capability byte 6 before it will accept an
 * SAE selector.  Keep both out of the common baseline: only a build that also
 * exposes the complete SAE/software-PMF WCL ingress may opt in.
 */
static constexpr size_t kCardCapabilityMfpByte = 6;
static constexpr uint8_t kCardCapabilityMfpMask = 0x10;
static constexpr size_t kCardCapabilitySaeByte = 9;
static constexpr uint8_t kCardCapabilitySaeMask = 0x08;

/*
 * IO80211Family answers PHY_MODE for entitled clients in WCL without asking
 * the driver: WCLConfigManager::getPHY_MODE starts from a/b/g/n/ac and adds
 * 11ax, to the supported and to the active mode, only when
 * WCLDeviceConfiguration::isHeSupported() is true, which is capability byte
 * 17, bit 0x80 of the CARD_CAPABILITIES it loaded at start.
 */
static constexpr size_t kCardCapabilityHeByte = 17;
static constexpr uint8_t kCardCapabilityHeMask = 0x80;

/*
 * WPA3 for the access point.  AppleBCMWLANCore::getCARD_CAPABILITIES
 * (0xffffff80015e4c66) sets capability byte 10, bit 0x01 (CoreWiFi
 * capability 80) from feature flag 0x4b, which IssueFwCapExt takes from the
 * firmware and which is cleared together with the SAE flags 0x41/0x42/0x6d
 * when the firmware has no SAE.  -[CWWiFiClient platformCapabilities] turns
 * capability 80 into 0x200000, and the Internet Sharing dialog
 * (-[CWHostAPDialog_SL populateSecurityTypes]) offers WPA3 Personal and the
 * WPA2/WPA3 transition, and enables its Security popup, only with it.
 */
static constexpr size_t kCardCapabilityHostApSaeByte = 10;
static constexpr uint8_t kCardCapabilityHostApSaeMask = 0x01;

/*
 * 6 GHz and SAE hash-to-element.  AppleBCMWLANCore::getCARD_CAPABILITIES
 * (25C56 0xffffff80015c4a9e) sets byte 0xf of the carrier (capability 11),
 * bit 0x40, when feature 0x5d (is6GSupported, firmware cap_ext) is set
 * (0xffffff80015c50ac), and byte 0x10 (capability 12), bit 0x08, from
 * feature 0x64 (isSaeH2eEnabled, 0xffffff80015c4e71).  WCL caches the
 * carrier and its WCLDeviceConfiguration::is6GSupported (0xffffff80021ce8cc)
 * is that 0x40: it gates the 6 GHz follow-up probe and co-located AP flags
 * of a scan request, RNR results, set6G_MODE and 6 GHz roaming.
 */
static constexpr size_t kCardCapabilitySixGhzByte = 11;
static constexpr uint8_t kCardCapabilitySixGhzMask = 0x40;
static constexpr size_t kCardCapabilitySaeH2eByte = 12;
static constexpr uint8_t kCardCapabilitySaeH2eMask = 0x08;

static constexpr uint8_t kAppleImpossibleCap2Mask = 0x80;
static constexpr uint8_t kAppleImpossibleCap3Mask = 0x08;
static constexpr uint8_t kAppleImpossibleCap6Mask = 0x80;
static constexpr size_t kRequiredCardCapabilityBytes = 10;
static constexpr size_t kApple80211BindCardCapabilitiesLength = 0x15;

template <size_t N>
inline void applyAppleConsistentCardCapabilityCluster(uint8_t (&capabilities)[N])
{
    static_assert(N >= kRequiredCardCapabilityBytes,
                  "CARD_CAPABILITIES carrier must expose bytes through cap[9]");

    capabilities[0] = kCardCapabilityByte0;
    capabilities[1] = kCardCapabilityByte1;
    capabilities[2] = kCardCapabilityByte2;
    capabilities[3] = kCardCapabilityByte3;
    capabilities[5] = kCardCapabilityByte5;
    capabilities[6] = kCardCapabilityByte6;
    capabilities[7] = kCardCapabilityByte7;
    capabilities[8] = kCardCapabilityByte8;
    capabilities[9] = kCardCapabilityByte9;
}

template <size_t N>
inline void applySaeCardCapability(uint8_t (&capabilities)[N])
{
    static_assert(N > kCardCapabilitySaeByte,
                  "CARD_CAPABILITIES carrier must expose the SAE byte");

    capabilities[kCardCapabilityMfpByte] |= kCardCapabilityMfpMask;
    capabilities[kCardCapabilitySaeByte] |= kCardCapabilitySaeMask;
}

template <size_t N>
inline void applyHostApSaeCardCapability(uint8_t (&capabilities)[N])
{
    static_assert(N > kCardCapabilityHostApSaeByte,
                  "CARD_CAPABILITIES carrier must expose the HostAP SAE byte");

    capabilities[kCardCapabilityHostApSaeByte] |= kCardCapabilityHostApSaeMask;
}

template <size_t N>
inline void applySixGhzCardCapability(uint8_t (&capabilities)[N])
{
    static_assert(N > kCardCapabilitySixGhzByte,
                  "CARD_CAPABILITIES carrier must expose the 6 GHz byte");

    capabilities[kCardCapabilitySixGhzByte] |= kCardCapabilitySixGhzMask;
}

template <size_t N>
inline void applySaeH2eCardCapability(uint8_t (&capabilities)[N])
{
    static_assert(N > kCardCapabilitySaeH2eByte,
                  "CARD_CAPABILITIES carrier must expose the SAE H2E byte");

    capabilities[kCardCapabilitySaeH2eByte] |= kCardCapabilitySaeH2eMask;
}

template <size_t N>
inline void applyHeCardCapability(uint8_t (&capabilities)[N])
{
    static_assert(N > kCardCapabilityHeByte,
                  "CARD_CAPABILITIES carrier must expose the HE byte");

    capabilities[kCardCapabilityHeByte] |= kCardCapabilityHeMask;
}

inline bool hasAppleImpossibleAdvancedAkmBits(uint8_t cap2,
                                              uint8_t cap3,
                                              uint8_t cap6)
{
    return ((cap2 & kAppleImpossibleCap2Mask) != 0) ||
           ((cap3 & kAppleImpossibleCap3Mask) != 0) ||
           ((cap6 & kAppleImpossibleCap6Mask) != 0);
}

} // namespace TahoeCapabilityContracts

#endif /* TahoeCapabilityContracts_hpp */
