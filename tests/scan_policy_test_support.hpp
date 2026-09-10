#ifndef ScanPolicyTestSupport_hpp
#define ScanPolicyTestSupport_hpp

#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <mutex>
#include <sys/types.h>

#ifndef NBBY
#define NBBY 8
#endif
#ifndef __packed
#define __packed __attribute__((packed))
#endif
#ifndef howmany
#define howmany(x, y) (((x) + ((y) - 1)) / (y))
#endif
#define isset(a, b) ((a)[(b) / NBBY] & (1U << ((b) % NBBY)))
#define setbit(a, b) ((a)[(b) / NBBY] |= (1U << ((b) % NBBY)))
#include "scan-policy-declarations.inc"
#include "itl80211/openbsd/net80211/ieee80211_join_attempt.h"
enum { IEEE80211_M_STA, IEEE80211_M_HOSTAP };

#ifdef __APPLE__
// Userspace Darwin lacks the kernel helper. Keep the test boundary explicit.
static void explicit_bzero(void *buffer, size_t size)
{
    auto *bytes = static_cast<volatile unsigned char *>(buffer);
    while (size-- != 0)
        *bytes++ = 0;
}
#endif

using IOInterruptState = unsigned;
struct IOSimpleLock { std::mutex mutex; };
static thread_local IOSimpleLock *heldPlanLock;
[[maybe_unused]] static IOInterruptState IOSimpleLockLockDisableInterrupt(IOSimpleLock *lock)
{
    assert(lock && !heldPlanLock);
    lock->mutex.lock();
    heldPlanLock = lock;
    return 0;
}
[[maybe_unused]] static void IOSimpleLockUnlockEnableInterrupt(IOSimpleLock *lock,
                                             IOInterruptState)
{
    assert(heldPlanLock == lock);
    heldPlanLock = nullptr;
    lock->mutex.unlock();
}

// Only external kernel/softc fields used by the complete production bodies.
struct ieee80211com {
    int ic_opmode = IEEE80211_M_STA;
    uint64_t ic_pae_assoc_epoch = 41;
    ieee80211_join_attempt ic_wcl_join_attempt = {};
    unsigned ic_initial_scan_census_only = 0;
    IOSimpleLock *ic_pae_selected_bss_lock = nullptr;
    ieee80211_wcl_scan_plan ic_wcl_scan_plan = {};
    int ic_des_esslen = 0;
    uint8_t ic_des_essid[IEEE80211_NWID_LEN] = {};
    ieee80211_channel ic_channels[IEEE80211_CHAN_MAX + 1] = {};
};

static unsigned ieee80211_mhz2ieee(unsigned frequency, unsigned)
{
    return frequency < 3000 ? (frequency - 2407) / 5 :
                             (frequency - 5000) / 5;
}
static unsigned ieee80211_chan2ieee(ieee80211com *,
                                   const ieee80211_channel *channel)
{
    return ieee80211_mhz2ieee(channel->ic_freq, 0);
}

static void *checkedPlanCopy(void *destination, const void *source, size_t size)
{
    assert(heldPlanLock && "plain plan copy must exclude clear/restage");
    return std::memcpy(destination, source, size);
}
#define memcpy checkedPlanCopy
#include "scan-policy-common.inc"
#undef memcpy

static uint32_t configuredHomeAway = 121;
static unsigned homeAwayReads;
static bool homeAwayAvailable = true;
extern "C" bool airportItlwmGetScanHomeAwayTime(uint32_t *value)
{
    ++homeAwayReads;
    *value = configuredHomeAway;
    return homeAwayAvailable;
}
#include "include/HAL/ItlScanCommandPolicy.hpp"

// Value-only reservation boundary for byte-layout fixtures. The complete
// prepare/reserve/copy/owner methods are exercised by the admission suite.
static int capturePolicyAtFixtureReservation(ieee80211com *ic, bool exact,
                                             ItlScanCommandPolicy *policy)
{
    const auto home = ItlScanCommandPolicy::homeAwayTime();
    auto irq = IOSimpleLockLockDisableInterrupt(ic->ic_pae_selected_bss_lock);
    const uint64_t generation = exact ?
        (ic->ic_wcl_scan_plan.active ? ic->ic_wcl_scan_plan.generation : UINT64_MAX) : 0;
    const int error = ItlScanCommandPolicy::captureOwnedLocked(ic, generation, nullptr, policy);
    if (!error) policy->homeAwayMs = home;
    IOSimpleLockUnlockEnableInterrupt(ic->ic_pae_selected_bss_lock, irq);
    return error;
}

static ieee80211_wcl_scan_plan makePlan(uint64_t generation, uint8_t marker)
{
    ieee80211_wcl_scan_plan plan = {};
    plan.generation = generation;
    plan.ssid_len = IEEE80211_NWID_LEN;
    std::memset(plan.ssid, marker, sizeof(plan.ssid));
    plan.scan_type = 1;
    plan.active_dwell_ms = 17 + marker;
    plan.passive_dwell_ms = 117 + marker;
    plan.home_dwell_ms = 217 + marker;
    plan.channel_filter = 1;
    plan.requested_channel_count = 2;
    setbit(plan.channel_2ghz, 9);
    setbit(plan.channel_5ghz, 149);
    return plan;
}

static void stagePlan(ieee80211com &ic, const ieee80211_wcl_scan_plan &plan)
{
    ieee80211_wcl_scan_plan_clear(&ic, 0);
    assert(ieee80211_wcl_scan_plan_stage(&ic, &plan) == 0);
}

#endif
