//
//  TahoeLeScanContracts.hpp
//  AirportItlwm
//

#ifndef TahoeLeScanContracts_hpp
#define TahoeLeScanContracts_hpp

#include <stdint.h>
#include <stddef.h>

namespace TahoeLeScanContracts {

static constexpr uint32_t kBadArgumentStatus = 0xe00002bc;
static constexpr uint32_t kCarrierSize = 0x1c;
static constexpr uint32_t kOwnerStateSize = 0x18;
static constexpr uint32_t kFirstCopiedDwordOffset = 0x04;
static constexpr uint32_t kLastCopiedDwordOffset = 0x18;
static constexpr uint32_t kCopiedDwordCount = 6;

struct Carrier
{
    uint32_t ignored00;
    uint32_t value04;
    uint32_t value08;
    uint32_t value0c;
    uint32_t value10;
    uint32_t value14;
    uint32_t value18;
} __attribute__((packed));
static_assert(sizeof(Carrier) == kCarrierSize,
              "Tahoe LE-scan carrier must preserve input offsets +0x0..+0x18");
static_assert(offsetof(Carrier, value04) == kFirstCopiedDwordOffset,
              "Tahoe LE-scan carrier first copied dword is at +0x04");
static_assert(offsetof(Carrier, value18) == kLastCopiedDwordOffset,
              "Tahoe LE-scan carrier last copied dword is at +0x18");

struct OwnerState
{
    uint32_t value04;
    uint32_t value08;
    uint32_t value0c;
    uint32_t value10;
    uint32_t value14;
    uint32_t value18;
} __attribute__((packed));
static_assert(sizeof(OwnerState) == kOwnerStateSize,
              "Tahoe LE-scan owner state must match six copied dwords");

inline bool copyOwnerStateFromCarrier(const Carrier *carrier, OwnerState *state)
{
    if (carrier == nullptr || state == nullptr)
        return false;

    state->value04 = carrier->value04;
    state->value08 = carrier->value08;
    state->value0c = carrier->value0c;
    state->value10 = carrier->value10;
    state->value14 = carrier->value14;
    state->value18 = carrier->value18;
    return true;
}

} // namespace TahoeLeScanContracts

#endif /* TahoeLeScanContracts_hpp */
