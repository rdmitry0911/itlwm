//
//  TahoePhyModeContracts.hpp
//  AirportItlwm
//

#ifndef TahoePhyModeContracts_hpp
#define TahoePhyModeContracts_hpp

#include <stdint.h>

namespace TahoePhyModeContracts {

static constexpr uint32_t kVersion = 1;
static constexpr uint32_t kModeUnknown = 0x000;
static constexpr uint32_t kModeAuto = 0x001;
static constexpr uint32_t kMode11A = 0x002;
static constexpr uint32_t kMode11B = 0x004;
static constexpr uint32_t kMode11G = 0x008;
static constexpr uint32_t kMode11N = 0x010;
static constexpr uint32_t kMode11AC = 0x080;
static constexpr uint32_t kMode11AX = 0x100;

inline uint32_t buildSupportedPhyMode(bool supports5GHz,
                                      bool supports2GHzCck,
                                      bool supports2GHzOfdm,
                                      bool supportsHt,
                                      bool supportsVht,
                                      bool supportsHe)
{
    uint32_t phyMode = kModeAuto;

    if (supports5GHz)
        phyMode |= kMode11A;
    if (supports2GHzCck)
        phyMode |= kMode11B;
    if (supports2GHzOfdm)
        phyMode |= kMode11G;
    if (supportsHt || supportsVht || supportsHe)
        phyMode |= kMode11N;
    if (supportsVht)
        phyMode |= kMode11AC;
    if (supportsHe)
        phyMode |= kMode11AX;

    return phyMode;
}

inline bool hasCompleteVhtCapability(uint32_t vhtCaps,
                                     bool hasVhtMcsCarrier)
{
    return vhtCaps != 0 && hasVhtMcsCarrier;
}

inline bool hasCompleteHeCapability(bool hasHeCapsCarrier,
                                    bool hasHeMcsCarrier)
{
    return hasHeCapsCarrier && hasHeMcsCarrier;
}

inline uint32_t activePhyModeForAssociatedBss(bool hasHe,
                                              bool hasVht,
                                              bool hasHt,
                                              bool is5GHz,
                                              bool has2GHzOfdmRate)
{
    if (hasHe)
        return kMode11AX;
    if (hasVht)
        return kMode11AC;
    if (hasHt)
        return kMode11N;
    if (is5GHz)
        return kMode11A;
    if (has2GHzOfdmRate)
        return kMode11G;
    return kMode11B;
}

template <typename PhyModeData>
inline bool initializePhyModeCarrier(PhyModeData *data, uint32_t supportedPhyMode)
{
    if (data == nullptr)
        return false;

    data->version = kVersion;
    data->phy_mode = supportedPhyMode;
    data->active_phy_mode = kModeUnknown;
    return true;
}

template <typename PhyModeData>
inline void publishAssociatedActiveMode(PhyModeData *data, uint32_t activePhyMode)
{
    if (data == nullptr)
        return;

    data->active_phy_mode = activePhyMode;
}

} // namespace TahoePhyModeContracts

#endif /* TahoePhyModeContracts_hpp */
