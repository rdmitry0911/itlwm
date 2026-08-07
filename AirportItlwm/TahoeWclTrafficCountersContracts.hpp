#ifndef TahoeWclTrafficCountersContracts_hpp
#define TahoeWclTrafficCountersContracts_hpp

#include <stdint.h>

namespace TahoeWclTrafficCountersContracts {

/*
 * Tahoe's WCL selector 0x1b2 consumes exactly seven cumulative qwords.
 * WCL snapshots this carrier and computes deltas itself, so every populated
 * field must remain monotonic for the lifetime of the controller instance.
 */
struct Carrier {
    uint64_t primaryTxPackets;
    uint64_t trafficClassRxPackets;
    uint64_t totalTxPackets;
    uint64_t totalRxPackets;
    uint64_t awdlTxPackets;
    uint64_t nanTxPackets;
    uint64_t continuousNanoseconds;
};

static_assert(sizeof(Carrier) == 0x38,
              "Tahoe WCL traffic counter carrier must be seven qwords");

inline Carrier build(uint64_t txPackets, uint64_t rxPackets,
                     uint64_t continuousNanoseconds)
{
    Carrier carrier{};
    carrier.primaryTxPackets = txPackets;
    carrier.trafficClassRxPackets = rxPackets;
    carrier.totalTxPackets = txPackets;
    carrier.totalRxPackets = rxPackets;
    /* AirportItlwm does not publish AWDL or NAN data paths. */
    carrier.awdlTxPackets = 0;
    carrier.nanTxPackets = 0;
    carrier.continuousNanoseconds = continuousNanoseconds;
    return carrier;
}

} // namespace TahoeWclTrafficCountersContracts

#endif /* TahoeWclTrafficCountersContracts_hpp */
