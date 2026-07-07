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
static constexpr uint32_t kGuardIntervalShort = 400;
static constexpr uint32_t kGuardIntervalLong = 800;
static constexpr uint32_t kGuardIntervalHtWide = 1600;
static constexpr uint32_t kGuardIntervalHtUltraWide = 3200;
static constexpr uint32_t kHtGuardIntervalSelectorShift = 10;
static constexpr uint32_t kHtGuardIntervalSelectorMask = 0x3;

inline bool isAcceptedQueryStatus(uint32_t status)
{
    return status == 0 || status == kConfigNoValueStatus;
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
