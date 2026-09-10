#ifndef ScanOwnerTestFields_hpp
#define ScanOwnerTestFields_hpp
#include <sys/types.h>
#ifndef NBBY
#define NBBY 8
#endif
#ifndef howmany
#define howmany(x, y) (((x) + ((y) - 1)) / (y))
#endif
#include "scan-owner-plan.inc"
#include "include/HAL/ItlStateTransitionLease.hpp"
#include "itl80211/openbsd/net80211/ieee80211_join_attempt.h"

// Embed in each fixture's external com; no fake policy/admission implementation.
#define SCAN_OWNER_TEST_FIELDS \
    IOSimpleLock *ic_pae_selected_bss_lock = nullptr; \
    int ic_opmode = IEEE80211_M_STA; \
    uint64_t ic_pae_assoc_epoch = 41; \
    ieee80211_join_attempt ic_wcl_join_attempt = {}; \
    unsigned ic_initial_scan_census_only = 0; \
    ieee80211_wcl_scan_plan ic_wcl_scan_plan = {}; \
    int ic_des_esslen = 0; \
    uint8_t ic_des_essid[IEEE80211_NWID_LEN] = {}
#endif
