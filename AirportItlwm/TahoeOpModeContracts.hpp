//
//  TahoeOpModeContracts.hpp
//  AirportItlwm
//

#ifndef TahoeOpModeContracts_hpp
#define TahoeOpModeContracts_hpp

#include <stdint.h>

namespace TahoeOpModeContracts {

static constexpr uint32_t kInvalidArgumentStatus = 0x16;
static constexpr uint32_t kPrimaryVersion = 1;
static constexpr uint32_t kPrimaryInitialMode = 0x00;
static constexpr uint32_t kPrimaryMonitorBit = 0x10;
static constexpr uint32_t kPrimaryStaBitMutationCount = 0;

template <typename OpModeData>
inline bool initializePrimaryCarrier(OpModeData *data)
{
    if (data == nullptr)
        return false;

    data->version = kPrimaryVersion;
    data->op_mode = kPrimaryInitialMode;
    return true;
}

} // namespace TahoeOpModeContracts

#endif /* TahoeOpModeContracts_hpp */
