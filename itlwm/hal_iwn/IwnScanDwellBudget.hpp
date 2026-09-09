#ifndef IWN_SCAN_DWELL_BUDGET_HPP
#define IWN_SCAN_DWELL_BUDGET_HPP

#include <stdint.h>

/* DVM's channel dwell fields are TU, while max_out is microseconds.
 * commands.h requires active <= passive < max_out and quiet <= active.
 * The existing net80211 scan owner additionally uses active < passive.
 * Apply these constraints after all caller policy overrides, without
 * changing channel admission or authorizing active scans on passive NVM. */
static inline bool
iwn_bound_scan_dwell(uint16_t beaconLimitTu, uint32_t maxOutUs,
                     uint16_t *activeTu, uint16_t *passiveTu,
                     uint16_t *quietMs)
{
    if (activeTu == nullptr || passiveTu == nullptr || quietMs == nullptr)
        return false;

    uint32_t ceiling = beaconLimitTu;
    if (maxOutUs != 0) {
        const uint32_t awayCeiling = (maxOutUs - 1U) / 1024U;
        if (awayCeiling < ceiling)
            ceiling = awayCeiling;
    }
    if (ceiling < 2)
        return false;

    uint32_t passive = *passiveTu;
    if (passive > ceiling)
        passive = ceiling;
    if (passive < 2)
        passive = 2;
    uint32_t active = *activeTu;
    if (active >= passive)
        active = passive - 1;
    if (active == 0)
        active = 1;

    *activeTu = static_cast<uint16_t>(active);
    *passiveTu = static_cast<uint16_t>(passive);
    /* Numerically bounding milliseconds by TU is conservative (1 TU is
     * 1.024 ms), and preserves the existing integral quiet-time policy. */
    if (*quietMs > active)
        *quietMs = static_cast<uint16_t>(active);
    return true;
}

#endif
