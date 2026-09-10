#include "include/HAL/ItlScanCommandLease.hpp"
#include "itl80211/openbsd/net80211/ieee80211_join_attempt.h"
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <initializer_list>

using IOInterruptState = unsigned;
struct IOSimpleLock { bool held = false; unsigned rank = 2; };
static IOSimpleLock *lockStack[2];
static unsigned held, reset_tasks;
static IOInterruptState IOSimpleLockLockDisableInterrupt(IOSimpleLock *lock)
{
    assert(lock && !lock->held && held < 2);
    assert(!held || lockStack[held - 1]->rank < lock->rank);
    lockStack[held] = lock;
    lock->held = true;
    ++held;
    return 1;
}
static void IOSimpleLockUnlockEnableInterrupt(IOSimpleLock *lock,
                                             IOInterruptState irq)
{
    assert(lock && lock->held && irq == 1 && held && lockStack[held - 1] == lock);
    lock->held = false;
    --held;
}
enum ieee80211_state { IEEE80211_S_INIT, IEEE80211_S_SCAN, IEEE80211_S_AUTH };
enum { IEEE80211_M_STA, IEEE80211_M_HOSTAP };
#include "scan_owner_test_fields.hpp"
struct ieee80211com { SCAN_OWNER_TEST_FIELDS; };
static uint32_t homeAway = 121;
extern "C" bool airportItlwmGetScanHomeAwayTime(uint32_t *value) {
    assert(!held); *value = homeAway; return true;
}
#include "include/HAL/ItlScanCommandPolicy.hpp"
struct task {};
static void *systq;
static void task_add(void *, task *) { assert(held == 0); ++reset_tasks; }
constexpr unsigned IWM_FLAG_SHUTDOWN = 1, IWX_FLAG_SHUTDOWN = 1;
constexpr unsigned IWM_FLAG_SCANNING = 2, IWX_FLAG_SCANNING = 2;
constexpr unsigned IWM_FLAG_BGSCAN = 4, IWX_FLAG_BGSCAN = 4;
struct Softc {
    int sc_generation = 7;
    ieee80211_state ns_nstate = IEEE80211_S_INIT;
    int ns_arg = 0;
    unsigned sc_flags = 0;
    ieee80211com sc_ic;
    task init_task;
};
enum class ItlIwmWclScanPhase { Idle, InitialStarting, BackgroundStarting };
enum class ItlIwxWclScanPhase { Idle, InitialStarting, BackgroundStarting };
#define DECLARE_BRIDGE(family) \
struct Itl##family { \
    Softc com; \
    IOSimpleLock leaf; \
    IOSimpleLock ownerLeaf = {false, 1}; \
    Itl##family() { com.sc_ic.ic_pae_selected_bss_lock = &ownerLeaf; } \
    IOSimpleLock *wclScanLock = &leaf; \
    ItlScanCommandLease scanCommand = {}; \
    ItlScanCommandPolicy scanCommandPolicy = {}; \
    ItlStateTransitionLease stateTransition = {}; \
    void *stateTransitionSource = this; \
    uint64_t wclScanUpperGeneration = 0; \
    bool wclScanPublicationInvalidated = false; \
    Itl##family##WclScanPhase wclScanPhase = Itl##family##WclScanPhase::Idle; \
    uint64_t scanCommandResetEpoch(); \
    bool reopenScanCommands(uint64_t, uint32_t); \
    int prepareStateTransition(int, int, ItlStateTransitionRequest *); \
    int reserveScanCommand(bool, bool, uint64_t *, const ItlStateTransitionRequest * = nullptr); \
    bool scanCommandOwnerCurrentLocked(uint64_t, uint32_t) const; \
    bool copyScanCommandPolicy(uint64_t, ItlScanCommandPolicy *); \
    void rejectScanCommand(uint64_t); \
    uint64_t reserveAPScanCommand(); \
    uint64_t currentAPScanCommand() const; \
    void finishAPScanCommand(uint64_t, bool); \
};
DECLARE_BRIDGE(Iwm)
DECLARE_BRIDGE(Iwx)
#include "scan-admission-bridge.inc"

// Convenience only at caller ingress: both preparation and reservation bodies
// come from production. Stale-request cases below retain an explicit old copy.
template<class D> static int reserve(D &d, bool background, bool umac, uint64_t *serial) {
    ItlStateTransitionRequest request = {};
    request.state = IEEE80211_S_SCAN;
    if (!background && d.scanCommand.open && d.wclScanLock)
        (void)d.prepareStateTransition(IEEE80211_S_SCAN, -1, &request);
    return d.reserveScanCommand(background, umac, serial, background ? nullptr : &request);
}

template<class Driver, class Phase> static unsigned exercise()
{
    unsigned cases = 0;
    Driver driver;
    driver.com.sc_ic.ic_wcl_join_attempt.result.generation = 91;
    driver.com.sc_ic.ic_wcl_join_attempt.phase = IEEE80211_JOIN_DISCOVERY;
    uint64_t serial = 0;
    assert(reserve(driver, false, true, nullptr) == EINVAL);
    assert(reserve(driver, false, true, &serial) == ENXIO);
    const uint64_t initial = driver.scanCommandResetEpoch();
    assert(initial == 0);
    assert(!driver.reopenScanCommands(initial, 8));
    driver.com.sc_flags = IWM_FLAG_SHUTDOWN;
    assert(!driver.reopenScanCommands(initial, 7));
    driver.com.sc_flags = 0;
    assert(driver.reopenScanCommands(initial, 7));
    ++cases;

    assert(reserve(driver, false, true, &serial) == 0);
    assert(serial && driver.scanCommand.command.joinGeneration == 91);
    const uint64_t first = serial;
    driver.com.sc_ic.ic_wcl_join_attempt.result.generation = 92;
    assert(reserve(driver, false, true, &serial) == EBUSY && !serial);
    assert(driver.scanCommand.command.joinGeneration == 91);
    driver.rejectScanCommand(first + 1);
    assert(driver.scanCommand.live() && reset_tasks == 0);
    driver.rejectScanCommand(first);
    assert(!driver.scanCommand.live() && reset_tasks == 0);
    assert(reserve(driver, false, true, &serial) == 0);
    assert(serial > first && driver.scanCommand.command.joinGeneration == 92);
    driver.rejectScanCommand(first);
    assert(driver.scanCommand.live() && reset_tasks == 0);
    driver.rejectScanCommand(serial);
    ++cases;

    for (unsigned excluded = 0; excluded != 6; ++excluded) {
        driver.com.sc_ic.ic_initial_scan_census_only = excluded == 1;
        driver.com.sc_ic.ic_wcl_join_attempt.phase = excluded == 0 ? IEEE80211_JOIN_IDLE :
            (excluded == 2 ? IEEE80211_JOIN_AUTH : IEEE80211_JOIN_DISCOVERY);
        driver.com.sc_ic.ic_wcl_join_attempt.result.association_epoch = excluded == 3 ? 4 : 0;
        driver.wclScanPhase = excluded == 4 ? Phase::InitialStarting : Phase::Idle;
        driver.wclScanUpperGeneration = excluded == 4 ? 8 : 0;
        driver.com.sc_ic.ic_wcl_scan_plan.active = excluded == 4;
        driver.com.sc_ic.ic_wcl_scan_plan.generation = 8;
        assert(reserve(driver, excluded == 5, true, &serial) == 0);
        assert(driver.scanCommand.command.joinGeneration == 0);
        driver.rejectScanCommand(serial);
        ++cases;
    }
    driver.com.sc_ic.ic_initial_scan_census_only = 0;
    assert(reserve(driver, false, false, &serial) == 0);
    assert(driver.scanCommand.command.joinGeneration == 92);
    assert(driver.scanCommand.submit(serial, 7));
    driver.rejectScanCommand(serial);
    assert(reset_tasks == 1 && driver.scanCommand.live() && !driver.scanCommand.open);
    driver.rejectScanCommand(serial);
    assert(reset_tasks == 1);
    assert(!driver.reopenScanCommands(initial, 7));
    driver.scanCommand.invalidate();
    assert(!driver.reopenScanCommands(initial, 7));
    ++driver.com.sc_generation;
    assert(driver.reopenScanCommands(driver.scanCommandResetEpoch(), 8));
    const uint64_t old = serial;
    assert(reserve(driver, false, true, &serial) == 0 && serial > old);
    driver.rejectScanCommand(old);
    assert(driver.scanCommand.live());
    driver.rejectScanCommand(serial);
    ++cases;

    assert(reserve(driver, false, true, &serial) == 0);
    assert(!driver.reserveAPScanCommand()); // Reserved, but not doorbelled.
    driver.rejectScanCommand(serial);
    auto ap = driver.reserveAPScanCommand();
    assert(ap && ap == driver.currentAPScanCommand() && !driver.reserveAPScanCommand());
    assert(reserve(driver, false, true, &serial) == EBUSY);
    driver.finishAPScanCommand(ap + 1, true);
    assert(driver.currentAPScanCommand() == ap);
    driver.finishAPScanCommand(ap, true);
    assert(!driver.currentAPScanCommand());
    assert(reserve(driver, false, true, &serial) == 0);
    driver.rejectScanCommand(serial);
    ++cases;

    for (unsigned flag : {IWM_FLAG_SHUTDOWN, IWM_FLAG_SCANNING, IWM_FLAG_BGSCAN}) {
        driver.com.sc_flags = flag;
        assert(!driver.reserveAPScanCommand());
    }
    driver.com.sc_flags = 0;
    driver.wclScanPhase = Phase::InitialStarting;
    assert(!driver.reserveAPScanCommand());
    driver.wclScanPhase = Phase::Idle;
    ++cases;

    ap = driver.reserveAPScanCommand();
    assert(ap); const unsigned priorResets = reset_tasks;
    driver.finishAPScanCommand(ap, false);
    assert(reset_tasks == priorResets + 1 && !driver.scanCommand.open);
    assert(driver.currentAPScanCommand() == ap);
    driver.finishAPScanCommand(ap, true);
    driver.finishAPScanCommand(ap, false);
    assert(driver.currentAPScanCommand() == ap && reset_tasks == priorResets + 1);
    assert(!driver.reopenScanCommands(driver.scanCommand.resetEpoch, 8));
    driver.scanCommand.invalidate(); ++driver.com.sc_generation;
    assert(driver.reopenScanCommands(driver.scanCommand.resetEpoch, 9));
    const auto nextAP = driver.reserveAPScanCommand();
    assert(nextAP > ap);
    driver.finishAPScanCommand(ap, true);
    driver.finishAPScanCommand(ap, false);
    assert(driver.currentAPScanCommand() == nextAP && driver.scanCommand.open);
    driver.finishAPScanCommand(nextAP, true);
    ++cases;

    // Exact queued/common ownership at both reservation and builder entry.
    for (unsigned boundary = 0; boundary != 8; ++boundary) {
        ItlStateTransitionRequest request;
        auto &ic = driver.com.sc_ic;
        ic.ic_wcl_join_attempt.phase = IEEE80211_JOIN_DISCOVERY;
        ic.ic_wcl_join_attempt.result.association_epoch = 0;
        ic.ic_initial_scan_census_only = 0;
        assert(driver.prepareStateTransition(IEEE80211_S_SCAN, -1, &request) == 0);
        if (boundary == 0) ++driver.stateTransition.request.serial;
        if (boundary == 1) ++ic.ic_wcl_join_attempt.next_generation;
        if (boundary == 2) ++ic.ic_wcl_join_attempt.result.generation;
        if (boundary == 3) ++ic.ic_pae_assoc_epoch;
        if (boundary == 4) ic.ic_wcl_join_attempt.phase = IEEE80211_JOIN_AUTH;
        if (boundary == 5) ic.ic_wcl_join_attempt.result.association_epoch = 123;
        if (boundary == 6) ic.ic_initial_scan_census_only = 1;
        if (boundary == 7) request.scanSsidLength = 33;
        assert(driver.reserveScanCommand(false, true, &serial, &request) ==
            (boundary == 7 ? EINVAL : ECANCELED));
        assert(!serial && !driver.scanCommand.live());
        ++cases;
    }
    auto &ic = driver.com.sc_ic;
    ic.ic_initial_scan_census_only = 0;
    ic.ic_wcl_join_attempt.phase = IEEE80211_JOIN_DISCOVERY;
    ic.ic_wcl_join_attempt.result.association_epoch = 0;
    ic.ic_wcl_join_attempt.result.ssid_len = 4;
    memcpy(ic.ic_wcl_join_attempt.result.ssid, "join", 4);
    ic.ic_des_esslen = 3; memcpy(ic.ic_des_essid, "old", 3);
    ItlStateTransitionRequest request;
    assert(driver.prepareStateTransition(IEEE80211_S_SCAN, -1, &request) == 0);
    const auto originalHome = homeAway;
    ++homeAway; memcpy(ic.ic_des_essid, "new", 3);
    assert(driver.reserveScanCommand(false, true, &serial, &request) == 0);
    ItlScanCommandPolicy policy = {};
    assert(driver.copyScanCommandPolicy(serial, &policy));
    assert(policy.plan.ssid_len == 4 && !memcmp(policy.plan.ssid, "join", 4));
    assert(policy.homeAwayMs == originalHome && policy.stateSerial == request.serial);
    assert(policy.joinGeneration == request.scanJoinGeneration);
    assert(driver.prepareStateTransition(IEEE80211_S_SCAN, -1, &request) == 0);
    policy.homeAwayMs = 999;
    assert(!driver.copyScanCommandPolicy(serial, &policy) && !policy.homeAwayMs);
    assert(driver.scanCommandPolicy.homeAwayMs == originalHome);
    driver.rejectScanCommand(serial + 1);
    assert(driver.scanCommandPolicy.homeAwayMs == originalHome);
    driver.rejectScanCommand(serial);
    assert(!driver.scanCommandPolicy.stateSerial);
    ++cases;

    for (unsigned boundary = 0; boundary != 6; ++boundary) {
        driver.wclScanPhase = Phase::InitialStarting;
        driver.wclScanUpperGeneration = 500;
        driver.wclScanPublicationInvalidated = false;
        ic.ic_wcl_scan_plan.active = 1; ic.ic_wcl_scan_plan.generation = 500;
        assert(driver.prepareStateTransition(IEEE80211_S_SCAN, -1, &request) == 0);
        assert(request.scanGeneration == 500);
        assert(driver.reserveScanCommand(false, true, &serial, &request) == 0);
        if (boundary == 0) ic.ic_wcl_scan_plan.active = 0;
        if (boundary == 1) ++ic.ic_wcl_scan_plan.generation;
        if (boundary == 2) driver.wclScanPublicationInvalidated = true;
        if (boundary == 3) ++driver.wclScanUpperGeneration;
        if (boundary == 4) ++ic.ic_pae_assoc_epoch;
        if (boundary == 5) ++driver.stateTransition.request.serial;
        assert(!driver.copyScanCommandPolicy(serial, &policy));
        driver.rejectScanCommand(serial);
        ++cases;
    }
    driver.wclScanPhase = Phase::Idle;
    ic.ic_wcl_join_attempt.phase = IEEE80211_JOIN_IDLE;
    for (int invalidLength : {-1, 33, 256, 260}) {
        ic.ic_des_esslen = invalidLength;
        assert(driver.prepareStateTransition(IEEE80211_S_SCAN, -1, &request) == EINVAL);
        assert(driver.reserveScanCommand(true, true, &serial) == EINVAL);
        assert(!driver.scanCommand.live());
        ++cases;
    }
    driver.wclScanLock = nullptr;
    assert(driver.scanCommandResetEpoch() == UINT64_MAX);
    assert(!driver.reopenScanCommands(0, 8));
    assert(reserve(driver, false, true, &serial) == ENXIO);
    driver.rejectScanCommand(old);
    assert(held == 0);
    reset_tasks = 0;
    ++cases;
    return cases;
}

int main()
{
    const unsigned count = exercise<ItlIwm, ItlIwmWclScanPhase>() +
        exercise<ItlIwx, ItlIwxWclScanPhase>();
    std::printf("actual IWM/IWX scan admission bridge: %u scenario groups passed\n", count);
}
