#include "itlwm/hal_iwn/IwnScanDwellBudget.hpp"
#include <assert.h>
#include <stdio.h>

static void check(uint16_t cap, uint32_t away, uint16_t active,
                  uint16_t passive, uint16_t quiet)
{
    const bool result = iwn_bound_scan_dwell(cap, away, &active, &passive, &quiet);
    const uint32_t bound = away != 0 && (away - 1U) / 1024U < cap ?
        (away - 1U) / 1024U : cap;
    assert(result == (bound >= 2));
    if (!result)
        return;
    assert(active >= 1 && active < passive && passive <= cap);
    assert(quiet <= active);
    assert(away == 0 || static_cast<uint32_t>(passive) * 1024U < away);
}

int main()
{
    uint16_t active = 20, passive = 110, quiet = 10;
    assert(iwn_bound_scan_dwell(UINT16_MAX, 110U * 1024U,
                               &active, &passive, &quiet));
    assert(active == 20 && passive == 109 && quiet == 10);
    passive = 110;
    assert(iwn_bound_scan_dwell(85, 110U * 1024U,
                               &active, &passive, &quiet));
    assert(active == 20 && passive == 85);
    active = 60; passive = 130;
    assert(iwn_bound_scan_dwell(44, 200U * 1024U,
                               &active, &passive, &quiet));
    assert(active == 43 && passive == 44); // PAN budget cannot be raised.
    active = 24; passive = 130;
    assert(iwn_bound_scan_dwell(UINT16_MAX, 0, &active, &passive, &quiet));
    assert(active == 24 && passive == 130); // Unassociated scan unchanged.

    const uint16_t values[] = {0, 1, 2, 10, 20, 44, 85, 110, 130, 255, UINT16_MAX};
    const uint32_t awayValues[] = {0, 1, 1024, 2048, 2049, 110000,
                                  110U * 1024U, 200U * 1024U, UINT32_MAX};
    for (uint16_t cap : values)
        for (uint32_t away : awayValues)
            for (uint16_t a : values)
                for (uint16_t p : values)
                    check(cap, away, a, p, 10);
    assert(!iwn_bound_scan_dwell(85, 0, nullptr, &passive, &quiet));
    puts("PASS: production IWN dwell obeys STA/PAN and strict off-channel budgets");
}
