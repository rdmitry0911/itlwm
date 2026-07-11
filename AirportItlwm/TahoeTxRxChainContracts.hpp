//
//  TahoeTxRxChainContracts.hpp
//  AirportItlwm
//

#ifndef TahoeTxRxChainContracts_hpp
#define TahoeTxRxChainContracts_hpp

#include <stdint.h>

namespace TahoeTxRxChainContracts {

struct Carrier {
    uint8_t hardwareRx;
    uint8_t hardwareTx;
    uint8_t activeTx;
    uint8_t activeRx;
} __attribute__((packed));

static_assert(sizeof(Carrier) == 0x04,
              "TXRX_CHAIN_INFO must remain four one-byte masks");

inline Carrier build(uint8_t hardwareRx,
                     uint8_t hardwareTx,
                     uint8_t activeTx,
                     uint8_t activeRx)
{
    return {hardwareRx, hardwareTx, activeTx, activeRx};
}

} // namespace TahoeTxRxChainContracts

#endif /* TahoeTxRxChainContracts_hpp */
