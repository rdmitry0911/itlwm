//
//  TahoeMimoContracts.hpp
//  AirportItlwm
//

#ifndef TahoeMimoContracts_hpp
#define TahoeMimoContracts_hpp

#include <stdint.h>
#include <stddef.h>

namespace TahoeMimoContracts {

static constexpr uint32_t kBadArgumentStatus = 0xe00002bc;
static constexpr uint32_t kStatusCarrierSize = 0x21;
static constexpr uint32_t kStatusVersion = 1;
static constexpr uint32_t kSetConfigStatusMutationCount = 0;

struct StatusCarrier
{
    uint32_t version;
    uint32_t ownerValue04;
    uint32_t coreValue08;
    uint32_t coreValue0c;
    uint8_t reserved10;
    uint32_t coreValue11;
    uint16_t coreValue15;
    uint8_t coreValue17;
    uint8_t coreValue18;
    uint64_t coreValue19;
} __attribute__((packed));
static_assert(sizeof(StatusCarrier) == kStatusCarrierSize,
              "Tahoe MIMO status carrier must match Apple writes through +0x20");
static_assert(offsetof(StatusCarrier, version) == 0x00,
              "Tahoe MIMO status version offset mismatch");
static_assert(offsetof(StatusCarrier, ownerValue04) == 0x04,
              "Tahoe MIMO status owner dword offset mismatch");
static_assert(offsetof(StatusCarrier, coreValue08) == 0x08,
              "Tahoe MIMO status core +0x4564 dword output mismatch");
static_assert(offsetof(StatusCarrier, coreValue0c) == 0x0c,
              "Tahoe MIMO status core +0x4568 dword output mismatch");
static_assert(offsetof(StatusCarrier, coreValue11) == 0x11,
              "Tahoe MIMO status unaligned +0x11 dword output mismatch");
static_assert(offsetof(StatusCarrier, coreValue15) == 0x15,
              "Tahoe MIMO status +0x15 word output mismatch");
static_assert(offsetof(StatusCarrier, coreValue17) == 0x17,
              "Tahoe MIMO status +0x17 byte output mismatch");
static_assert(offsetof(StatusCarrier, coreValue18) == 0x18,
              "Tahoe MIMO status +0x18 byte output mismatch");
static_assert(offsetof(StatusCarrier, coreValue19) == 0x19,
              "Tahoe MIMO status +0x19 qword output mismatch");

inline bool initializeStatusCarrier(StatusCarrier *status)
{
    if (status == nullptr)
        return false;

    uint8_t *raw = reinterpret_cast<uint8_t *>(status);
    for (uint32_t i = 0; i < kStatusCarrierSize; i++)
        raw[i] = 0;
    status->version = kStatusVersion;
    return true;
}

} // namespace TahoeMimoContracts

#endif /* TahoeMimoContracts_hpp */
