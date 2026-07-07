//
//  TahoeNrateContracts.hpp
//  AirportItlwm
//

#ifndef TahoeNrateContracts_hpp
#define TahoeNrateContracts_hpp

#include <stdint.h>

namespace TahoeNrateContracts {

static constexpr uint32_t kConfigNoValueStatus = 0xe00002e3;
static constexpr uint32_t kFamilyMask = 0x07000000;
static constexpr uint32_t kFamilyLegacy = 0x01000000;
static constexpr uint32_t kFamilyVht = 0x02000000;
static constexpr uint32_t kFamilyHt = 0x03000000;
static constexpr uint32_t kShortGuardIntervalBit = 1U << 23;
static constexpr uint32_t kBandwidthMask = 0x00070000;
static constexpr uint32_t kBandwidth20 = 0x00010000;
static constexpr uint32_t kBandwidth40 = 0x00020000;
static constexpr uint32_t kBandwidth80 = 0x00030000;
static constexpr uint32_t kBandwidth160 = 0x00040000;
static constexpr uint32_t kGuardIntervalShort = 400;
static constexpr uint32_t kGuardIntervalLong = 800;
static constexpr uint32_t kGuardIntervalHtWide = 1600;
static constexpr uint32_t kGuardIntervalHtUltraWide = 3200;
static constexpr uint32_t kHtGuardIntervalSelectorShift = 10;
static constexpr uint32_t kHtGuardIntervalSelectorMask = 0x3;

static constexpr uint32_t kIwmRateMcsHtMask = 1U << 8;
static constexpr uint32_t kIwmRateMcsCckMask = 1U << 9;
static constexpr uint32_t kIwmRateMcsVhtMask = 1U << 26;
static constexpr uint32_t kIwmRateMcsHeMask = 1U << 10;
static constexpr uint32_t kIwmRateMcsWidthMask = 3U << 11;
static constexpr uint32_t kIwmRateMcsSgiMask = 1U << 13;
static constexpr uint32_t kIwmRateHtMcsIndexMask = 0x3f;
static constexpr uint32_t kIwmRateVhtMcsMask = 0x0f;
static constexpr uint32_t kIwmRateVhtNssMask = 3U << 4;
static constexpr uint32_t kIwmRateVhtNssShift = 4;
static constexpr uint32_t kIwmRateLegacyMask = 0xff;

static constexpr uint32_t kIwxRateModMask = 0x7U << 8;
static constexpr uint32_t kIwxRateModCck = 0U << 8;
static constexpr uint32_t kIwxRateModLegacyOfdm = 1U << 8;
static constexpr uint32_t kIwxRateModHt = 2U << 8;
static constexpr uint32_t kIwxRateModVht = 3U << 8;
static constexpr uint32_t kIwxRateModHe = 4U << 8;
static constexpr uint32_t kIwxRateModEht = 5U << 8;
static constexpr uint32_t kIwxRateMcsWidthMask = 0x7U << 11;
static constexpr uint32_t kIwxRateMcsSgiMask = 1U << 20;
static constexpr uint32_t kIwxRateLegacyMask = 0x7;
static constexpr uint32_t kIwxRateHtMcsMask = 0x7;
static constexpr uint32_t kIwxRateMcsNssMask = 1U << 4;
static constexpr uint32_t kIwxRateMcsCodeMask = 0x0f;

inline bool isAcceptedQueryStatus(uint32_t status)
{
    return status == 0 || status == kConfigNoValueStatus;
}

inline bool legacyHalfMbpsFromIntelPlcp(uint32_t plcp, uint32_t *halfMbps)
{
    if (halfMbps == nullptr)
        return false;

    switch (plcp & 0xff) {
        case 10:
            *halfMbps = 2;
            return true;
        case 20:
            *halfMbps = 4;
            return true;
        case 55:
            *halfMbps = 11;
            return true;
        case 110:
            *halfMbps = 22;
            return true;
        case 13:
            *halfMbps = 12;
            return true;
        case 15:
            *halfMbps = 18;
            return true;
        case 5:
            *halfMbps = 24;
            return true;
        case 7:
            *halfMbps = 36;
            return true;
        case 9:
            *halfMbps = 48;
            return true;
        case 11:
            *halfMbps = 72;
            return true;
        case 1:
            *halfMbps = 96;
            return true;
        case 3:
            *halfMbps = 108;
            return true;
        default:
            return false;
    }
}

inline bool legacyHalfMbpsFromIwxV2(uint32_t raw, uint32_t *halfMbps)
{
    if (halfMbps == nullptr)
        return false;

    static constexpr uint8_t kCckHalfMbps[] = {2, 4, 11, 22};
    static constexpr uint8_t kOfdmHalfMbps[] = {12, 18, 24, 36, 48, 72, 96, 108};
    uint32_t index = raw & kIwxRateLegacyMask;
    switch (raw & kIwxRateModMask) {
        case kIwxRateModCck:
            if (index >= sizeof(kCckHalfMbps))
                return false;
            *halfMbps = kCckHalfMbps[index];
            return true;
        case kIwxRateModLegacyOfdm:
            if (index >= sizeof(kOfdmHalfMbps))
                return false;
            *halfMbps = kOfdmHalfMbps[index];
            return true;
        default:
            return false;
    }
}

inline uint32_t appleBandwidthFromIntelSelector(uint32_t selector)
{
    switch (selector) {
        case 0:
            return kBandwidth20;
        case 1:
            return kBandwidth40;
        case 2:
            return kBandwidth80;
        case 3:
            return kBandwidth160;
        default:
            return 0;
    }
}

inline bool buildLegacyNrateFromHalfMbps(uint32_t halfMbps, uint32_t *nrate)
{
    if (nrate == nullptr || halfMbps == 0 || halfMbps > 0xff)
        return false;

    *nrate = kFamilyLegacy | halfMbps;
    return true;
}

inline bool buildHtNrateFromMcs(uint32_t mcs, bool ht40, uint32_t *nrate)
{
    if (nrate == nullptr || mcs > kIwmRateHtMcsIndexMask)
        return false;

    *nrate = kFamilyHt | (ht40 ? kBandwidth40 : kBandwidth20) |
        (mcs & kIwmRateHtMcsIndexMask);
    return true;
}

inline bool normalizeIwmRateNFlagsToAppleNrate(uint32_t raw, uint32_t *nrate)
{
    if (nrate == nullptr)
        return false;

    if ((raw & kIwmRateMcsVhtMask) != 0) {
        uint32_t apple = kFamilyVht;
        apple |= raw & kIwmRateVhtMcsMask;
        apple |= ((((raw & kIwmRateVhtNssMask) >> kIwmRateVhtNssShift) + 1) & 0xf) << 4;
        apple |= appleBandwidthFromIntelSelector((raw & kIwmRateMcsWidthMask) >> 11);
        if ((raw & kIwmRateMcsSgiMask) != 0)
            apple |= kShortGuardIntervalBit;
        *nrate = apple;
        return true;
    }

    if ((raw & kIwmRateMcsHtMask) != 0) {
        uint32_t apple = kFamilyHt;
        apple |= raw & kIwmRateHtMcsIndexMask;
        apple |= appleBandwidthFromIntelSelector((raw & kIwmRateMcsWidthMask) >> 11);
        *nrate = apple;
        return true;
    }

    if ((raw & kIwmRateMcsHeMask) != 0)
        return false;

    uint32_t halfMbps = 0;
    if (!legacyHalfMbpsFromIntelPlcp(raw & kIwmRateLegacyMask, &halfMbps))
        return false;
    return buildLegacyNrateFromHalfMbps(halfMbps, nrate);
}

inline bool normalizeIwxRateNFlagsToAppleNrate(uint32_t raw, uint32_t *nrate)
{
    if (nrate == nullptr)
        return false;

    switch (raw & kIwxRateModMask) {
        case kIwxRateModVht: {
            uint32_t apple = kFamilyVht;
            apple |= raw & kIwxRateMcsCodeMask;
            apple |= ((((raw & kIwxRateMcsNssMask) >> 4) + 1) & 0xf) << 4;
            apple |= appleBandwidthFromIntelSelector((raw & kIwxRateMcsWidthMask) >> 11);
            if ((raw & kIwxRateMcsSgiMask) != 0)
                apple |= kShortGuardIntervalBit;
            *nrate = apple;
            return true;
        }
        case kIwxRateModHt: {
            uint32_t apple = kFamilyHt;
            apple |= (((raw & kIwxRateMcsNssMask) >> 1) |
                      (raw & kIwxRateHtMcsMask));
            apple |= appleBandwidthFromIntelSelector((raw & kIwxRateMcsWidthMask) >> 11);
            *nrate = apple;
            return true;
        }
        case kIwxRateModHe:
        case kIwxRateModEht:
            return false;
        case kIwxRateModCck:
        case kIwxRateModLegacyOfdm:
        default: {
            uint32_t halfMbps = 0;
            if (!legacyHalfMbpsFromIwxV2(raw, &halfMbps))
                return false;
            return buildLegacyNrateFromHalfMbps(halfMbps, nrate);
        }
    }
}

inline bool decodeMcsIndexFromNrate(uint32_t rate, uint32_t *index)
{
    if (index == nullptr)
        return false;

    switch (rate & kFamilyMask) {
        case kFamilyLegacy:
            *index = rate & 0xff;
            return true;
        case kFamilyVht:
        case kFamilyHt:
            *index = rate & 0xf;
            return true;
        default:
            return false;
    }
}

inline bool decodeGuardIntervalFromNrate(uint32_t rate, uint32_t *interval)
{
    if (interval == nullptr)
        return false;

    switch (rate & kFamilyMask) {
        case kFamilyVht:
            *interval = (rate & kShortGuardIntervalBit) ? kGuardIntervalShort
                                                        : kGuardIntervalLong;
            return true;
        case kFamilyHt:
            switch ((rate >> kHtGuardIntervalSelectorShift) &
                    kHtGuardIntervalSelectorMask) {
                case 2:
                    *interval = kGuardIntervalHtWide;
                    break;
                case 3:
                    *interval = kGuardIntervalHtUltraWide;
                    break;
                case 0:
                case 1:
                default:
                    *interval = kGuardIntervalLong;
                    break;
            }
            return true;
        default:
            *interval = kGuardIntervalLong;
            return true;
    }
}

inline bool decodeRateHalfMbpsFromNrate(uint32_t rate, uint32_t *halfMbps)
{
    if (halfMbps == nullptr)
        return false;

    static constexpr uint16_t kHt20Long[] = {13, 26, 39, 52, 78, 104, 117, 130};
    static constexpr uint16_t kHt40Long[] = {27, 54, 81, 108, 162, 216, 243, 270};
    static constexpr uint16_t kVht20Long[] = {13, 26, 39, 52, 78, 104, 117, 130, 156};
    static constexpr uint16_t kVht20Short[] = {14, 29, 43, 58, 87, 116, 130, 144, 174};
    static constexpr uint16_t kVht40Long[] = {27, 54, 81, 108, 162, 216, 243, 270, 324, 360};
    static constexpr uint16_t kVht40Short[] = {30, 60, 90, 120, 180, 240, 270, 300, 360, 400};
    static constexpr uint16_t kVht80Long[] = {59, 117, 176, 234, 351, 468, 527, 585, 702, 780};
    static constexpr uint16_t kVht80Short[] = {65, 130, 195, 260, 390, 520, 585, 650, 780, 867};
    static constexpr uint16_t kVht160Long[] = {117, 234, 351, 468, 702, 936, 1053, 1170, 1404, 1560};
    static constexpr uint16_t kVht160Short[] = {130, 260, 390, 520, 780, 1040, 1170, 1300, 1560, 1734};

    switch (rate & kFamilyMask) {
        case kFamilyLegacy:
            *halfMbps = rate & 0xff;
            return *halfMbps != 0;
        case kFamilyHt: {
            uint32_t mcs = rate & kIwmRateHtMcsIndexMask;
            const uint16_t *table = nullptr;
            switch (rate & kBandwidthMask) {
                case kBandwidth20:
                default:
                    table = kHt20Long;
                    break;
                case kBandwidth40:
                    table = kHt40Long;
                    break;
            }
            uint32_t streams = (mcs / 8) + 1;
            *halfMbps = table[mcs % 8] * streams;
            return true;
        }
        case kFamilyVht: {
            uint32_t mcs = rate & 0xf;
            uint32_t nss = (rate >> 4) & 0xf;
            bool shortGi = (rate & kShortGuardIntervalBit) != 0;
            const uint16_t *table = nullptr;
            uint32_t entries = 10;
            switch (rate & kBandwidthMask) {
                case kBandwidth20:
                default:
                    table = shortGi ? kVht20Short : kVht20Long;
                    entries = 9;
                    break;
                case kBandwidth40:
                    table = shortGi ? kVht40Short : kVht40Long;
                    break;
                case kBandwidth80:
                    table = shortGi ? kVht80Short : kVht80Long;
                    break;
                case kBandwidth160:
                    table = shortGi ? kVht160Short : kVht160Long;
                    break;
            }
            if (nss == 0 || mcs >= entries)
                return false;
            *halfMbps = table[mcs] * nss;
            return true;
        }
        default:
            return false;
    }
}

inline bool decodeRateMbpsFromNrate(uint32_t rate, uint32_t *mbps)
{
    if (mbps == nullptr)
        return false;

    uint32_t halfMbps = 0;
    if (!decodeRateHalfMbpsFromNrate(rate, &halfMbps))
        return false;
    *mbps = halfMbps / 2;
    return true;
}

template <typename McsVhtData>
inline bool fillMcsVhtFromNrate(uint32_t rate, McsVhtData *data)
{
    if (data == nullptr)
        return false;
    if ((rate & kFamilyMask) != kFamilyVht)
        return false;

    data->index = rate & 0xf;
    data->nss = (rate >> 4) & 0xf;
    decodeGuardIntervalFromNrate(rate, &data->guard_interval);
    switch (rate & kBandwidthMask) {
        case 0x00010000:
            data->bw = 20;
            break;
        case 0x00020000:
            data->bw = 40;
            break;
        case 0x00030000:
            data->bw = 80;
            break;
        case 0x00040000:
            data->bw = 160;
            break;
        default:
            break;
    }
    return true;
}

} // namespace TahoeNrateContracts

#endif /* TahoeNrateContracts_hpp */
