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
static constexpr uint8_t kCardCapabilityByte8 = 0x01;
static constexpr uint8_t kCardCapabilityByte9 = 0x02;

static constexpr uint8_t kAppleImpossibleCap2Mask = 0x80;
static constexpr uint8_t kAppleImpossibleCap3Mask = 0x08;
static constexpr uint8_t kAppleImpossibleCap6Mask = 0x80;
static constexpr size_t kRequiredCardCapabilityBytes = 10;

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
    capabilities[8] = kCardCapabilityByte8;
    capabilities[9] = kCardCapabilityByte9;
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
