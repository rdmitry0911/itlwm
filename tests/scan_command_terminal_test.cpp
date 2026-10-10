#include "include/HAL/ItlScanCommandLease.hpp"
#include "itl80211/openbsd/net80211/ieee80211_join_attempt.h"
#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <functional>
#include <algorithm>

using IOInterruptState = unsigned;
struct IOSimpleLock { bool held = false; unsigned rank = 2; };
static IOSimpleLock *lockStack[2];
static unsigned held, generic, controlled, published, wakes, begins, requeues;
static unsigned joinCleanups, joinFailures;
static uint64_t cleanedJoin;
static std::function<void()> joinCleanupHook;
static uint64_t last_upper;
static unsigned last_status, last_mode;
static std::function<void()> unlock_hook, upper_hook;
static std::function<void()> sleep_hook;
static bool sleep_locked;
static unsigned waits, resets;
static unsigned abort_submissions;
static uint64_t aborted_serial;
static std::function<void()> abort_hook;
static int sleep_result;
static unsigned controller_gate_depth, blocked_scan_terminals;
static bool scan_terminal_irq_double, persistent_sleep_hook;
static uint64_t test_now;
static unsigned gate_sleeps,gate_wakes,gate_wake_attempts;
static bool drop_gate_wakes;
static std::function<void()> gate_sleep_hook,wait_unlock_hook;
using AbsoluteTime=uint64_t;
[[maybe_unused]] constexpr unsigned kSecondScale=1000000000,
    kMillisecondScale=1000000,THREAD_UNINT=0;
[[maybe_unused]] static void clock_interval_to_deadline(unsigned interval,unsigned scale,uint64_t *deadline)
{ *deadline=test_now+uint64_t(interval)*scale; }
[[maybe_unused]] static void clock_get_uptime(uint64_t *now) { *now=test_now; }
[[maybe_unused]] static void absolutetime_to_nanoseconds(uint64_t value,uint64_t *ns) { *ns=value; }
struct WorkLoop {
    bool inGate() const { return controller_gate_depth!=0; }
protected:
    int sleepGate(void *,AbsoluteTime deadline,unsigned) {
        assert(controller_gate_depth && !held && !sleep_locked);
        const unsigned saved=controller_gate_depth; controller_gate_depth=0;
        ++gate_sleeps;
        if(gate_sleep_hook) gate_sleep_hook();
        else if(sleep_hook) { auto hook=sleep_hook; if(!persistent_sleep_hook) sleep_hook={}; hook(); }
        test_now=std::max(test_now,deadline); controller_gate_depth=saved;
        return 0;
    }
    void wakeupGate(void *,bool) {
        assert(!held); ++gate_wake_attempts; if(!drop_gate_wakes) ++gate_wakes;
    }
    friend struct CommandGate;
};
struct CommandGate {
    WorkLoop loop;
    int commandSleep(void *event,AbsoluteTime deadline,unsigned flags)
    { return loop.sleepGate(event,deadline,flags); }
    void commandWakeup(void *event,bool one) { loop.wakeupGate(event,one); }
};
static IOInterruptState IOSimpleLockLockDisableInterrupt(IOSimpleLock *lock)
{
    assert(lock && !lock->held && held < 2);
    assert(!held || lockStack[held - 1]->rank < lock->rank);
    lockStack[held] = lock;
    lock->held = true; ++held; return 1;
}
static void IOSimpleLockUnlockEnableInterrupt(IOSimpleLock *lock, IOInterruptState irq)
{
    assert(lock && lock->held && held && irq == 1 && lockStack[held - 1] == lock);
    lock->held = false; --held;
    if (!held && unlock_hook) { auto hook = unlock_hook; unlock_hook = {}; hook(); }
}
constexpr unsigned IWM_FLAG_SCANNING = 1, IWM_FLAG_BGSCAN = 2;
constexpr unsigned IWX_FLAG_SCANNING = 1, IWX_FLAG_BGSCAN = 2;
constexpr unsigned IWM_UCODE_TLV_CAPA_UMAC_SCAN = 1;
constexpr unsigned IWM_UMAC_SCAN_ABORT_STATUS_SUCCESS = 0;
constexpr unsigned IWM_UMAC_SCAN_ABORT_STATUS_IN_PROGRESS = 1;
constexpr unsigned IWM_UMAC_SCAN_ABORT_STATUS_NOT_FOUND = 2;
constexpr unsigned IWX_UMAC_SCAN_ABORT_STATUS_SUCCESS = 0;
constexpr unsigned IWX_UMAC_SCAN_ABORT_STATUS_IN_PROGRESS = 1;
constexpr unsigned IWX_UMAC_SCAN_ABORT_STATUS_NOT_FOUND = 2;
static bool isset(unsigned flags, unsigned bit) { return (flags & bit) != 0; }
constexpr unsigned IFF_UP = 1, IFF_RUNNING = 2;
constexpr unsigned IEEE80211_F_BGSCAN = 1, IEEE80211_F_DISABLE_BG_AUTO_CONNECT = 2;
constexpr unsigned IEEE80211_SCAN_COMPLETION_WCL_HANDOFF = 1;
constexpr unsigned IEEE80211_SCAN_COMPLETION_GENERIC = 0;
constexpr unsigned IEEE80211_SCAN_COMPLETION_WCL_FOREGROUND = 2;
constexpr unsigned IEEE80211_WCL_SCAN_TERMINAL_STATUS_ABORTED = 1;
constexpr unsigned IEEE80211_WCL_SCAN_TERMINAL_STATUS_COMPLETE = 2;
constexpr unsigned IEEE80211_EVT_STA_JOIN_FAILED = 3;
constexpr unsigned IEEE80211_EVT_WCL_SCAN_INVALIDATED=4;
struct ieee80211_wcl_scan_invalidation { uint64_t generation; uint32_t backend_generation; };
struct Ifnet { unsigned if_flags = IFF_UP | IFF_RUNNING; };
enum { IEEE80211_M_STA, IEEE80211_M_HOSTAP, IEEE80211_S_SCAN };
constexpr unsigned IWM_FLAG_SHUTDOWN = 4, IWX_FLAG_SHUTDOWN = 4;
#include "scan_owner_test_fields.hpp"
struct ieee80211com {
    SCAN_OWNER_TEST_FIELDS;
    void *ic_softc = nullptr;
    Ifnet ic_if;
    unsigned ic_flags = 0;
    unsigned ic_wcl_scan_active = 0, ic_wcl_scan_suppress_scan_done_once = 0;
    void (*ic_event_handler)(ieee80211com *, unsigned, void *) = nullptr;
};
struct task { unsigned enqueues = 0; };
struct taskq {};
struct Softc {
    task newstate_task;
    taskq queue;
    taskq *sc_nswq = &queue;
    unsigned active = 0;
    bool admission = true;
    ieee80211com sc_ic;
    int sc_generation = 7;
    unsigned sc_flags = 0, sc_scan_abort_pending = 0;
    unsigned sc_enabled_capa = 1;
};
extern "C" bool airportItlwmGetScanHomeAwayTime(uint32_t *value) {
    assert(!held); *value = 120; return true;
}
#include "include/HAL/ItlScanCommandPolicy.hpp"
static bool iwm_sae_tx_lifecycle_enter(Softc *sc, bool) {
    assert(!held); if (!sc->admission) return false; ++sc->active; return true;
}
static void iwm_sae_tx_lifecycle_leave(Softc *sc) {
    assert(!held && sc->active); --sc->active;
}
static void run_upper()
{
    assert(!held);
    if (upper_hook) { auto hook = upper_hook; upper_hook = {}; hook(); }
}
static void ieee80211_end_scan(Ifnet *) { ++generic; run_upper(); }
static uint64_t completedReassocSerial, completedJoinGeneration;
static void ieee80211_end_scan_owned(Ifnet *ifp, unsigned mode,
    uint64_t joinGeneration, uint64_t reassocSerial) {
    assert(mode == 0);
    completedJoinGeneration = joinGeneration;
    completedReassocSerial = reassocSerial;
    ieee80211_end_scan(ifp);
}
static void ieee80211_end_scan_controlled(Ifnet *, unsigned mode)
{ ++controlled; last_mode = mode; run_upper(); }
static void ieee80211_begin_scan(Ifnet *) { assert(!held); ++begins; }
static void fixture_bzero(void *p, size_t n) { std::memset(p, 0, n); }
enum class Phase { Idle, InitialQueued, InitialStarting, InitialActive,
                   BackgroundStarting, BackgroundActive };
enum class Kind { None, ReplayInitial, Foreground, Background };
struct Terminal {
    uint64_t upperGeneration = 0;
    uint32_t backendGeneration = 0;
    bool publish = false;
};
using ItlIwmWclScanPhase = Phase;
using ItlIwxWclScanPhase = Phase;
using ItlIwmWclScanTerminalKind = Kind;
using ItlIwxWclScanTerminalKind = Kind;
using ItlIwmWclScanTerminal = Terminal;
using ItlIwxWclScanTerminal = Terminal;
#define DECLARE(family, lower) \
struct Itl##family { \
    Softc com; \
    Itl##family() { com.sc_ic.ic_softc = &com; com.sc_ic.ic_pae_selected_bss_lock = &ownerLeaf; } \
    IOSimpleLock leaf; \
    IOSimpleLock ownerLeaf = {false, 1}; \
    IOSimpleLock *wclScanLock = &leaf; \
    ItlScanCommandLease scanCommand = {}; \
    ItlScanCommandPolicy scanCommandPolicy = {}; \
    ItlStateTransitionLease stateTransition = {}; \
    void *stateTransitionSource = this; \
    uint64_t scanCommandAbortSerial = 0; \
    CommandGate gate; \
    CommandGate *getMainCommandGate() { return &gate; } \
    WorkLoop *getMainWorkLoop() { return &gate.loop; } \
    bool wclScanNeedsReopen=false,wclSaeAdmissionReserved=false; \
    uint64_t radioReadyReceiptSerial=0,radioReadyRequestEpoch=0; \
    uint32_t radioReadyBackendGeneration=0; \
    void invalidateWclScanForReset(); \
    Phase wclScanPhase = Phase::Idle; \
    uint64_t wclScanUpperGeneration = 0; \
    uint32_t wclScanBackendGeneration = 0; \
    bool wclScanPublicationInvalidated = false, apFence = false, apTerminal = false; \
    bool claimScanCommandTerminal(uint64_t, ItlScanCommandTerminal *, Terminal *, Kind *); \
    Kind claimWclScanTerminal(Terminal *, bool = false); \
    bool activateScanCommand(uint64_t, bool); \
    bool scanCommandCurrent(uint64_t); \
    bool scanCommandBackgroundPending(); \
    bool readyScanCommand(uint64_t); \
    void noteScanCommandTerminal(bool, uint32_t, bool); \
    bool deferScanCommand(const ItlStateTransitionRequest &, bool = true); \
    ItlStateTransitionRequest request() { \
        ItlStateTransitionRequest out; \
        auto irq = IOSimpleLockLockDisableInterrupt(&ownerLeaf); \
        const auto identity = ItlScanCommandPolicy::identityLocked(&com.sc_ic); \
        assert(stateTransition.prepare(com.sc_generation, IEEE80211_S_SCAN, -1, identity, &out)); \
        assert(ItlScanCommandPolicy::captureIngressLocked(&com.sc_ic, 0, &out)); \
        stateTransition.request = out; \
        assert(stateTransition.enqueue(out, com.sc_generation)); \
        assert(stateTransition.take(com.sc_generation, &out)); \
        IOSimpleLockUnlockEnableInterrupt(&ownerLeaf, irq); return out; \
    } \
    bool scanCommandReplayPending(); \
    void resumeScanCommand(); \
    void lower##_add_task(Softc *sc, taskq *, task *t) { \
        assert(!held && sc->active); ++t->enqueues; ++requeues; \
    } \
    bool iwx_task_gate_enter(Softc *sc, bool allow) { return iwm_sae_tx_lifecycle_enter(sc, allow); } \
    void iwx_task_gate_leave(Softc *sc) { iwm_sae_tx_lifecycle_leave(sc); } \
    int reserveScanCommandAbort(bool, uint64_t *, bool = false, uint64_t = 0); \
    int waitScanCommandAbort(uint64_t, uint32_t); \
    int lower##_scan_abort(Softc *, bool = false, uint64_t = 0); \
    static int lower##_bgscan_abort(ieee80211com *, uint64_t = 0); \
    int abort_background(uint64_t serial = 0) { return lower##_bgscan_abort(&com.sc_ic, serial); } \
    int lower##_umac_scan_abort_status(Softc *, uint32_t *status, uint64_t serial) { \
        assert(!held); \
        *status = 2; \
        if (!scanCommand.current(serial, com.sc_generation)) return ENXIO; \
        if (scanCommand.terminalSeen) return 0; \
        auto irq = IOSimpleLockLockDisableInterrupt(wclScanLock); \
        assert(scanCommand.submitAbort(serial, com.sc_generation)); \
        ++abort_submissions; aborted_serial = serial; *status = 0; \
        IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq); \
        if (abort_hook) { auto hook = abort_hook; abort_hook = {}; hook(); } \
        return 0; \
    } \
    int lower##_lmac_scan_abort(Softc *sc, uint64_t serial) { \
        uint32_t status; return lower##_umac_scan_abort_status(sc, &status, serial); \
    } \
    void rejectScanCommand(uint64_t serial) { \
        assert(!held && !sleep_locked); \
        if (scanCommand.quarantine(serial, com.sc_generation)) ++resets; \
    } \
    void lockTsleep() { assert(!held && !sleep_locked); sleep_locked = true; } \
    void unlockTsleep() { \
        assert(!held && sleep_locked); sleep_locked = false; \
        if(wait_unlock_hook) { auto hook=wait_unlock_hook; wait_unlock_hook={}; hook(); } \
    } \
    int tsleep_nsec_locked(void *, int, const char *, uint64_t remaining) { \
        assert(!held && sleep_locked); ++waits; sleep_locked = false; \
        const uint64_t sleep_started=test_now; \
        if (scan_terminal_irq_double && controller_gate_depth) { \
            ++blocked_scan_terminals; test_now+=remaining; sleep_locked = true; return ETIMEDOUT; \
        } \
        const bool had_hook=bool(sleep_hook); \
        if (sleep_hook) { auto hook = sleep_hook; if (!persistent_sleep_hook) sleep_hook = {}; hook(); } \
        if(sleep_result!=0 || !had_hook) test_now=std::max(test_now,sleep_started+remaining); \
        assert(!held && !sleep_locked); sleep_locked = true; return sleep_result; \
    } \
    bool isAPScanFenceActive() { assert(!held); return apFence || scanCommand.apSerial != 0; } \
    bool completePrimaryStaRecoveryScanAPHandoff() { assert(!held); return apTerminal; } \
    void publishWclScanTerminal(const Terminal *terminal, uint32_t status) { \
        assert(!held); if (!terminal->publish) return; \
        ++published; last_upper = terminal->upperGeneration; last_status = status; \
    } \
    void wakeupOn(void *) { assert(!held); ++wakes; } \
    void lower##_endscan(Softc *, uint64_t); \
    void finish(uint64_t serial) { lower##_endscan(&com, serial); } \
    static void lower##_wcl_join_failure_scan(ieee80211com *ic, uint64_t generation) { \
        auto *sc = static_cast<Softc *>(ic->ic_softc); \
        auto *that = reinterpret_cast<Itl##family *>( \
            reinterpret_cast<char *>(sc) - offsetof(Itl##family, com)); \
        assert(!held && !that->scanCommand.live()); \
        ++joinCleanups; cleanedJoin = generation; \
        if (joinCleanupHook) { auto hook = joinCleanupHook; joinCleanupHook = {}; hook(); } \
    } \
};
DECLARE(Iwm, iwm)
DECLARE(Iwx, iwx)
template<class D> static void reset_ticket(D *driver)
{
    assert(held == 1);
    driver->wclScanPhase = Phase::Idle;
    driver->wclScanUpperGeneration = 0;
    driver->wclScanBackendGeneration = 0;
    driver->wclScanPublicationInvalidated = false;
}
static void iwm_wcl_scan_ticket_reset_locked(ItlIwm *d) { reset_ticket(d); }
static void iwx_wcl_scan_ticket_reset_locked(ItlIwx *d) { reset_ticket(d); }
template<class D> static void start_rejected(D *,uint64_t,uint32_t)
{ assert(!held && !sleep_locked); }
static void iwm_wcl_scan_publish_start_rejected(ItlIwm *d,uint64_t generation,uint32_t backend)
{ start_rejected(d,generation,backend); }
static void iwx_wcl_scan_publish_start_rejected(ItlIwx *d,uint64_t generation,uint32_t backend)
{ start_rejected(d,generation,backend); }
#define explicit_bzero fixture_bzero
#define SEC_TO_NSEC(x) (uint64_t(x) * 1000000000ULL)
#define iwm_softc Softc
#define iwx_softc Softc
#define container_of(p, type, member) \
    reinterpret_cast<type *>(reinterpret_cast<char *>(p) - offsetof(type, member))
#include "scan-terminal.inc"
#undef iwm_softc
#undef iwx_softc
#undef explicit_bzero

static void reset_observers()
{
    assert(!held && !sleep_locked);
    generic = controlled = published = wakes = begins = requeues = 0;
    last_upper = last_status = last_mode = 0;
    unlock_hook = upper_hook = {};
    sleep_hook = {};
    waits = resets = 0;
    abort_submissions = 0; aborted_serial = 0; abort_hook = {};
    sleep_result = 0;
    controller_gate_depth=blocked_scan_terminals=0;
    scan_terminal_irq_double=persistent_sleep_hook=false;
    test_now=0; gate_sleeps=gate_wakes=gate_wake_attempts=0;
    drop_gate_wakes=false; gate_sleep_hook=wait_unlock_hook={};
    joinCleanups = joinFailures = 0; cleanedJoin = 0; joinCleanupHook = {};
}
template<class D> static uint64_t prepare(D &d, uint64_t join = 91, bool bg = false,
    uint64_t reassocSerial = 0,bool umac=true,uint32_t uid=0)
{
    if (!d.scanCommand.open)
        assert(d.scanCommand.reopen(d.scanCommand.resetEpoch, d.com.sc_generation));
    const auto serial = d.scanCommand.reserve(d.com.sc_generation, bg ? 0 : join,
                                              umac, bg, uid, reassocSerial);
    assert(serial && d.scanCommand.submit(serial, d.com.sc_generation));
    assert(d.activateScanCommand(serial, bg));
    return serial;
}
template<class D> static void wcl(D &d, uint64_t generation, bool bg = false)
{
    d.wclScanPhase = bg ? Phase::BackgroundActive : Phase::InitialActive;
    d.wclScanUpperGeneration = generation;
    d.wclScanBackendGeneration = unsigned(generation + 10);
    if (bg) d.com.sc_ic.ic_wcl_scan_active = 1;
}
template<class D> static unsigned exercise()
{
    unsigned cases = 0;
    for (unsigned mutation = 0; mutation != 3; ++mutation) {
        reset_observers(); D d;
        constexpr uint64_t join = UINT64_C(0x10000005b);
        const uint8_t bssid[6] = {2, 3, 4, 5, 6, 7};
        const uint8_t ssid[] = {'l', 'a', 'b'};
        auto &attempt = d.com.sc_ic.ic_wcl_join_attempt;
        attempt.next_generation = join - 1;
        assert(ieee80211_join_attempt_begin(&attempt, bssid, ssid, sizeof(ssid)) == join);
        const auto serial = prepare(d, join);
        assert(d.readyScanCommand(serial));
        d.com.sc_ic.ic_event_handler = [](ieee80211com *, unsigned event, void *payload) {
            assert(!held && event == IEEE80211_EVT_STA_JOIN_FAILED);
            auto *failure = static_cast<ieee80211_join_failure *>(payload);
            assert(failure->terminal.cause == IEEE80211_JOIN_FAILURE_NO_NETWORKS);
            ++joinFailures;
        };
        upper_hook = [&] {
            assert(!d.scanCommand.live());
            assert(ieee80211_join_attempt_fail(&attempt, join, 0,
                IEEE80211_JOIN_DISCOVERY, IEEE80211_JOIN_FAILURE_NO_NETWORKS,
                0, 0, 0, IEEE80211_JOIN_CLEANUP_ALL));
            if (mutation == 1)
                assert(ieee80211_join_attempt_begin(&attempt, bssid, ssid, sizeof(ssid)) == join + 1);
        };
        if (mutation == 2) joinCleanupHook = [&] {
            assert(ieee80211_join_attempt_begin(&attempt, bssid, ssid, sizeof(ssid)) == join + 1);
        };
        d.noteScanCommandTerminal(true, 0, false);
        assert(joinCleanups == unsigned(mutation != 1) && !joinFailures && !begins);
        if (mutation == 0) {
            assert(cleanedJoin == join);
            assert(attempt.cleanup_pending == (IEEE80211_JOIN_CLEANUP_LOWER | IEEE80211_JOIN_CLEANUP_SAE));
            ieee80211_wcl_join_cleanup_done(&d.com.sc_ic, join, IEEE80211_JOIN_CLEANUP_LOWER);
            assert(!joinFailures);
            ieee80211_wcl_join_cleanup_done(&d.com.sc_ic, join, IEEE80211_JOIN_CLEANUP_SAE);
            assert(joinFailures == 1);
            d.finish(serial); assert(joinCleanups == 1 && joinFailures == 1);
        } else {
            assert(attempt.result.generation == join + 1 && attempt.phase == IEEE80211_JOIN_DISCOVERY);
            assert(!attempt.cleanup_pending && !joinFailures);
        }
        ++cases;
    }
    reset_observers();
    {
        D d; const auto physical=prepare(d,0,true,UINT64_C(0x100000032));
        assert(d.readyScanCommand(physical));
        assert(d.abort_background(UINT64_C(0x100000031))==ECANCELED);
        assert(!d.scanCommand.command.stopping && !abort_submissions && !waits && !resets);
        assert(d.scanCommand.command.serial==physical);
        assert(d.scanCommand.command.reassocSerial==UINT64_C(0x100000032));
        sleep_hook=[&] { d.noteScanCommandTerminal(true,0,true); };
        assert(d.abort_background(UINT64_C(0x100000032))==0);
        assert(abort_submissions==1 && !d.scanCommand.live());
        ++cases;
    }
    reset_observers();
    {
        D d;
        const auto serial=prepare(d,0,true,UINT64_C(0x100000031));
        assert(d.readyScanCommand(serial));
        uint64_t replacement=0;
        upper_hook=[&] { replacement=prepare(d,0,true,UINT64_C(0x100000032)); };
        d.noteScanCommandTerminal(true,0,false);
        assert(completedReassocSerial==UINT64_C(0x100000031));
        assert(completedJoinGeneration==0 && generic==1);
        assert(d.scanCommand.command.serial==replacement);
        assert(d.scanCommand.command.reassocSerial==UINT64_C(0x100000032));
        d.finish(serial);
        assert(d.scanCommand.command.serial==replacement && generic==1);
        ++cases;
    }
    reset_observers();
    { D d; auto serial = prepare(d); wcl(d, 5);
      d.noteScanCommandTerminal(true, 1, false);
      assert(!d.scanCommand.terminalSeen);
      d.noteScanCommandTerminal(true, 0, false);
      assert(d.scanCommand.live() && !controlled);
      assert(d.readyScanCommand(serial));
      assert(controlled == 1 && !generic && published == 1 && last_upper == 5);
      assert(!d.scanCommand.live() && !d.com.sc_flags);
      d.noteScanCommandTerminal(true, 0, false); d.finish(serial);
      assert(published == 1); ++cases; }
    reset_observers();
    { D d; auto old = prepare(d); wcl(d, 5);
      assert(d.readyScanCommand(old)); uint64_t replacement = 0;
      upper_hook = [&] { replacement = prepare(d, 92); wcl(d, 6);
                         assert(d.readyScanCommand(replacement)); };
      d.noteScanCommandTerminal(true, 0, false);
      assert(published == 1 && last_upper == 5);
      assert(d.scanCommand.command.serial == replacement && d.com.sc_flags == 1);
      assert(d.wclScanUpperGeneration == 6);
      assert(d.scanCommand.noteTerminal(7, true, 0, false));
      d.finish(old);
      assert(published == 1 && d.scanCommand.live());
      d.finish(replacement);
      assert(published == 2 && last_upper == 6); ++cases; }
    for (bool fromReady : {false, true}) {
      reset_observers(); D d; auto old = prepare(d); wcl(d, 5);
      if (fromReady) assert(d.scanCommand.noteTerminal(7, true, 0, false));
      else assert(d.readyScanCommand(old));
      uint64_t replacement = 0;
      unlock_hook = [&] {
          d.scanCommand.invalidate(); ++d.com.sc_generation; d.com.sc_flags = 0;
          replacement = prepare(d, 92); wcl(d, 6);
          assert(d.scanCommand.ready(replacement, 8));
          assert(d.scanCommand.noteTerminal(8, true, 0, false));
      };
      if (fromReady) assert(d.readyScanCommand(old));
      else d.noteScanCommandTerminal(true, 0, false);
      assert(!published && !controlled && d.scanCommand.command.serial == replacement);
      d.finish(replacement); assert(published == 1 && last_upper == 6); ++cases;
    }
    reset_observers();
    { D d; auto serial = prepare(d, 0, true); wcl(d, 5, true);
      assert(d.readyScanCommand(serial));
      upper_hook = [&] { assert(d.com.sc_ic.ic_wcl_scan_active == 0);
          prepare(d, 0, true); wcl(d, 6, true); };
      d.noteScanCommandTerminal(true, 0, false);
      assert(generic == 1 && published == 1 && last_upper == 5);
      assert(d.com.sc_ic.ic_wcl_scan_active == 1 && d.com.sc_flags == 2); ++cases; }
    reset_observers();
    { D d; auto serial = prepare(d); wcl(d, 5);
      assert(d.readyScanCommand(serial));
      uint64_t abort = 0; assert(d.reserveScanCommandAbort(true, &abort) == 0);
      assert(abort == serial);
      d.noteScanCommandTerminal(true, 0, true);
      assert(wakes == 1 && !generic && !controlled && !published);
      assert(!d.com.sc_scan_abort_pending && !d.com.sc_flags);
      assert(d.wclScanUpperGeneration == 5); ++cases; }
    reset_observers();
    { D d; auto serial = prepare(d); assert(d.readyScanCommand(serial));
      d.noteScanCommandTerminal(true, 0, true);
      assert(!generic && controlled == 1 && last_mode == 1); ++cases; }
    reset_observers();
    { D d; auto serial = prepare(d, 0, true); wcl(d, 5, true);
      d.com.sc_ic.ic_flags = 7; assert(d.readyScanCommand(serial));
      upper_hook = [&] { assert(d.com.sc_ic.ic_flags == 4); };
      d.noteScanCommandTerminal(true, 0, true);
      assert(!generic && controlled == 1 && published == 1 && last_status == 1);
      assert(d.com.sc_ic.ic_flags == 4); ++cases; }
    reset_observers();
    { D d; auto serial = prepare(d); assert(d.readyScanCommand(serial));
      d.com.sc_ic.ic_wcl_join_attempt.result.generation = 92;
      d.com.sc_ic.ic_wcl_join_attempt.phase = IEEE80211_JOIN_DISCOVERY;
      assert(d.deferScanCommand(d.request()) && d.scanCommandReplayPending() && d.stateTransition.request.identity.joinGeneration == 92);
      d.noteScanCommandTerminal(true, 0, false);
      assert(!generic && controlled == 1 && !begins && requeues == 1);
      assert(!d.scanCommandReplayPending()); ++cases; }
    reset_observers();
    { D d; auto serial = prepare(d); assert(d.readyScanCommand(serial));
      d.com.sc_ic.ic_wcl_join_attempt.result.generation = 92;
      d.com.sc_ic.ic_wcl_join_attempt.phase = IEEE80211_JOIN_DISCOVERY;
      assert(d.deferScanCommand(d.request()));
      upper_hook = [&] { d.com.sc_ic.ic_wcl_join_attempt.result.generation = 93; };
      d.noteScanCommandTerminal(true, 0, false);
      assert(!generic && !begins && !d.scanCommandReplayPending()); ++cases; }
    reset_observers();
    { D d; auto serial = prepare(d); d.wclScanPhase = Phase::InitialQueued;
      assert(d.readyScanCommand(serial)); d.noteScanCommandTerminal(true, 0, false);
      assert(!generic && controlled == 1 && begins == 1 && !published);
      assert(d.wclScanPhase == Phase::InitialStarting); ++cases; }
    reset_observers();
    { D d; wcl(d, 5); Terminal terminal;
      assert(d.claimWclScanTerminal(&terminal) == Kind::Foreground);
      assert(terminal.upperGeneration == 5 && terminal.publish && !held); ++cases; }
    reset_observers();
    { D d; auto serial = prepare(d); assert(d.readyScanCommand(serial));
      uint64_t abort = 0; assert(d.reserveScanCommandAbort(true, &abort) == 0);
      sleep_hook = [&] { d.noteScanCommandTerminal(true, 0, true); };
      assert(d.waitScanCommandAbort(serial, 7) == 0);
      assert(waits == 1 && wakes == 1 && !resets && !d.scanCommand.live()); ++cases; }
    reset_observers();
    { D d; auto serial = prepare(d); assert(d.readyScanCommand(serial));
      uint64_t abort = 0; assert(d.reserveScanCommandAbort(true, &abort) == 0);
      uint64_t next = 0;
      sleep_hook = [&] {
          d.noteScanCommandTerminal(true, 0, true);
          next = prepare(d, 92); assert(d.readyScanCommand(next));
          assert(d.reserveScanCommandAbort(true, &abort) == 0 && abort == next);
      };
      assert(d.waitScanCommandAbort(serial, 7) == 0);
      assert(d.scanCommandAbortSerial == next && d.com.sc_scan_abort_pending == 1);
      assert(!resets && d.scanCommand.live()); ++cases; }
    reset_observers();
    { D d; auto serial = prepare(d); assert(d.readyScanCommand(serial));
      uint64_t abort = 0; assert(d.reserveScanCommandAbort(true, &abort) == 0);
      sleep_result = ETIMEDOUT;
      assert(d.waitScanCommandAbort(serial, 7) == ETIMEDOUT);
      assert(resets == 1 && d.scanCommand.live() && !d.scanCommand.open);
      assert(d.scanCommandAbortSerial == serial); ++cases; }
    reset_observers();
    { D d; auto serial = prepare(d); assert(d.readyScanCommand(serial));
      assert(d.scanCommand.noteTerminal(7, true, 0, false));
      uint64_t abort = 0; assert(d.reserveScanCommandAbort(true, &abort) == 0);
      assert(abort == serial && !d.scanCommand.live() && wakes == 1);
      assert(d.waitScanCommandAbort(serial, 7) == 0 && !waits); ++cases; }
    for (bool submitted : {false, true}) {
      reset_observers(); D d;
      assert(d.scanCommand.reopen(0, 7));
      auto serial = d.scanCommand.reserve(7, 0, true, true, 0);
      if (submitted) assert(d.scanCommand.submit(serial, 7));
      assert(d.com.sc_flags == 0 && d.scanCommandBackgroundPending());
      assert(d.abort_background() == EBUSY);
      assert(!d.scanCommand.command.stopping && !abort_submissions && !waits);
      assert(d.scanCommand.command.serial == serial); ++cases;
    }
    reset_observers();
    { D d; auto serial = prepare(d); assert(d.readyScanCommand(serial));
      d.com.sc_flags |= IWM_FLAG_BGSCAN; // Stale legacy flag must not select it.
      assert(!d.scanCommandBackgroundPending());
      assert(d.abort_background() == 0 && !abort_submissions && !waits);
      assert(d.scanCommand.command.serial == serial && !d.scanCommand.command.stopping);
      ++cases; }
    for (bool earlyTerminal : {false, true}) {
      reset_observers(); D d; auto serial = prepare(d, 0, true);
      assert(d.readyScanCommand(serial));
      auto finish = [&] { d.noteScanCommandTerminal(true, 0, true); };
      if (earlyTerminal) abort_hook = finish; else sleep_hook = finish;
      assert(d.abort_background() == 0);
      assert(abort_submissions == 1 && aborted_serial == serial && wakes == 1);
      assert(waits == unsigned(!earlyTerminal) && !generic && !published && !resets);
      ++cases;
    }
    reset_observers();
    { D d; auto serial = prepare(d, 0, true); assert(d.readyScanCommand(serial));
      sleep_result = ETIMEDOUT;
      assert(d.abort_background() == ETIMEDOUT);
      assert(abort_submissions == 1 && resets == 1 && !d.scanCommand.open);
      assert(d.scanCommand.command.serial == serial && d.scanCommandBackgroundPending());
      assert(d.abort_background() == ENXIO && abort_submissions == 1); ++cases; }
    reset_observers();
    { D d; auto serial = prepare(d, 0, true); assert(d.readyScanCommand(serial));
      sleep_hook = [&] { d.scanCommand.invalidate(); ++d.com.sc_generation;
                        d.scanCommandAbortSerial = 0; };
      assert(d.abort_background() == ENXIO && !resets && abort_submissions == 1);
      ++cases; }
    for (bool reserved : {false, true}) {
      reset_observers(); D d; assert(d.scanCommand.reopen(0, 7));
      uint64_t ap = 0;
      if (reserved) { ap = d.scanCommand.reserveAP(7); assert(ap); }
      else d.apFence = true; // IWX has queued AP start before lower admission.
      d.com.sc_ic.ic_wcl_join_attempt.result.generation = 92;
      d.com.sc_ic.ic_wcl_join_attempt.phase = IEEE80211_JOIN_DISCOVERY;
      assert(d.deferScanCommand(d.request()) && d.scanCommandReplayPending() && d.stateTransition.request.identity.joinGeneration == 92);
      d.resumeScanCommand(); assert(!begins && d.scanCommandReplayPending() && d.stateTransition.request.identity.joinGeneration == 92);
      if (reserved) assert(d.scanCommand.releaseAP(ap, 7));
      d.apFence = false; d.resumeScanCommand();
      assert(!begins && requeues == 1 && !d.scanCommandReplayPending()); ++cases;
    }
    reset_observers();
    for (unsigned mutation = 0; mutation != 4; ++mutation) {
        D d; prepare(d);
        d.com.sc_ic.ic_wcl_join_attempt.result.generation = 92;
        d.com.sc_ic.ic_wcl_join_attempt.phase = IEEE80211_JOIN_DISCOVERY;
        auto request = d.request();
        if (mutation == 0) (void)d.request();
        if (mutation == 1) ++d.com.sc_ic.ic_wcl_join_attempt.next_generation;
        if (mutation == 2) ++d.com.sc_ic.ic_pae_assoc_epoch;
        if (mutation == 3) d.com.sc_flags |= IWM_FLAG_SHUTDOWN;
        assert(!d.deferScanCommand(request));
        assert(!d.scanCommandReplayPending());
        ++cases;
    }
    return cases;
}
template<class D> static void abortWaitControl(bool gated,bool spurious)
{
    reset_observers(); D d;
    const auto serial=prepare(d);
    assert(d.readyScanCommand(serial));
    uint64_t abort=0;
    assert(d.reserveScanCommandAbort(true,&abort)==0 && abort==serial);
    auto irq=IOSimpleLockLockDisableInterrupt(d.wclScanLock);
    // Explicit command publication boundary, not a fabricated firmware status.
    assert(d.scanCommand.submitAbort(serial,d.com.sc_generation));
    IOSimpleLockUnlockEnableInterrupt(d.wclScanLock,irq);
    controller_gate_depth=gated ? 2 : 0;
    scan_terminal_irq_double=true; persistent_sleep_hook=spurious;
    sleep_hook=[&] {
        if(spurious && waits==1) return;
        d.noteScanCommandTerminal(true,0,true);
    };
    const int result=d.waitScanCommandAbort(serial,d.com.sc_generation);
    std::fprintf(stderr,"actual scan-abort wait gated=%u spurious=%u result=%d "
        "waits=%u blockedTerminals=%u wakes=%u resets=%u live=%u\n",
        gated,spurious,result,waits,blocked_scan_terminals,wakes,resets,d.scanCommand.live());
    assert(result==0 && !blocked_scan_terminals && wakes==1 && !resets);
    assert(!d.scanCommand.live() && !d.scanCommandAbortSerial);
    assert(controller_gate_depth==(gated ? 2U : 0U));
    std::puts("actual scan-abort terminal control: PASS");
}
template<class D> static unsigned abortWaitMatrix()
{
    unsigned cases=0;
    for(bool gated : {false,true}) for(bool umac : {false,true}) {
        for(unsigned edge=0;edge<10;++edge) {
            reset_observers(); D d;
            const auto serial=prepare(d,91,false,0,umac,17);
            assert(d.readyScanCommand(serial));
            uint64_t abort=0,next=0;
            assert(d.reserveScanCommandAbort(true,&abort)==0 && abort==serial);
            auto irq=IOSimpleLockLockDisableInterrupt(d.wclScanLock);
            assert(d.scanCommand.submitAbort(serial,d.com.sc_generation));
            IOSimpleLockUnlockEnableInterrupt(d.wclScanLock,irq);
            controller_gate_depth=gated ? 3 : 0;
            unsigned pulses=0,callbacks=0;
            auto terminal=[&] { d.noteScanCommandTerminal(umac,17,true); };
            if(edge==0) terminal(); // Actual terminal retained before registration.
            if(edge==2) drop_gate_wakes=true;
            auto pulse=[&] {
                ++pulses;
                if(edge==1 && pulses<3) { test_now+=100000000; return; }
                if(edge==3) { if(!gated) test_now+=100000000; return; }
                if(edge==4) { test_now=1000000000; terminal(); return; }
                if(edge==5 && pulses==1) {
                    d.noteScanCommandTerminal(!umac,17,true);
                    if(umac) d.noteScanCommandTerminal(umac,18,true);
                    assert(!wakes && d.scanCommand.live()); test_now+=100000000; return;
                }
                if(edge==6) { d.invalidateWclScanForReset(); return; }
                if(edge==7) { d.com.sc_flags|=IWM_FLAG_SHUTDOWN; return; }
                if(edge==8) { ++d.com.sc_generation; return; }
                terminal();
                if(edge==9) {
                    next=prepare(d,92,false,0,umac,18); assert(d.readyScanCommand(next));
                    assert(d.reserveScanCommandAbort(true,&abort)==0 && abort==next);
                }
            };
            if(edge==6) {
                wcl(d,5); d.com.sc_ic.ic_wcl_scan_active=1;
                d.radioReadyReceiptSerial=d.radioReadyRequestEpoch=91;
                d.radioReadyBackendGeneration=9;
                upper_hook=[&] { assert(!held && !sleep_locked); ++callbacks; };
                d.com.sc_ic.ic_event_handler=[](ieee80211com *,unsigned event,void *payload) {
                    assert(event==IEEE80211_EVT_WCL_SCAN_INVALIDATED && !held && !sleep_locked);
                    const auto *invalidation=static_cast<ieee80211_wcl_scan_invalidation *>(payload);
                    assert(invalidation->generation==5 && invalidation->backend_generation==15);
                    run_upper();
                };
            }
            if(gated) gate_sleep_hook=pulse;
            else { sleep_hook=pulse; persistent_sleep_hook=true; }
            if(edge==4 && !gated) sleep_result=ETIMEDOUT;
            const int result=d.waitScanCommandAbort(serial,7);
            const int expected=edge==3 ? ETIMEDOUT : edge>=6 && edge<=8 ? ENXIO : 0;
            assert(result==expected && controller_gate_depth==(gated ? 3U : 0U));
            assert(!held && !sleep_locked && !blocked_scan_terminals);
            if(edge==3) {
                assert(test_now==1000000000 && resets==1 && d.scanCommand.live());
                assert(!d.scanCommand.open && d.scanCommandAbortSerial==serial && d.com.sc_scan_abort_pending==1);
                assert((gated ? gate_sleeps : waits)==(gated ? 100U : 10U));
            } else if(edge==6) {
                assert(!resets && wakes==1 && gate_wake_attempts==1 && callbacks==1);
                assert(!d.scanCommand.open && !d.scanCommand.live() && !d.scanCommandAbortSerial);
                assert(!d.com.sc_scan_abort_pending && !d.com.sc_ic.ic_wcl_scan_active);
                assert(!d.radioReadyReceiptSerial && !d.radioReadyRequestEpoch && !d.radioReadyBackendGeneration);
                assert(d.wclScanNeedsReopen);
            } else if(edge==7 || edge==8) {
                assert(!resets && !wakes && d.scanCommand.live());
            } else {
                assert(!resets && wakes==1 && gate_wake_attempts==1);
                if(edge==9) assert(d.scanCommand.live() && d.scanCommandAbortSerial==next && d.com.sc_scan_abort_pending==1);
                else assert(!d.scanCommand.live() && !d.scanCommandAbortSerial && !d.com.sc_scan_abort_pending);
                if(edge==0) assert(!gate_sleeps && !waits);
                if(edge==1) assert(pulses==3);
                if(edge==2) assert(!gate_wakes);
                if(edge==4) assert(test_now==1000000000);
                if(edge==5) assert(pulses==2);
            }
            ++cases;
        }
    }
    { reset_observers(); D d;
      assert(d.waitScanCommandAbort(0,7)==EINVAL && !waits && !gate_sleeps); ++cases; }
    { reset_observers(); D d; d.wclScanLock=nullptr;
      assert(d.waitScanCommandAbort(1,7)==ENXIO && !waits && !gate_sleeps); ++cases; }
    { reset_observers(); D d; const auto serial=prepare(d); assert(d.readyScanCommand(serial));
      uint64_t abort=0; assert(d.reserveScanCommandAbort(true,&abort)==0);
      d.wclScanPhase=Phase::InitialQueued; d.invalidateWclScanForReset();
      assert(d.waitScanCommandAbort(serial,7)==ENXIO && wakes==1 && !waits && !gate_sleeps);
      assert(!d.com.sc_scan_abort_pending && !d.scanCommandAbortSerial); ++cases; }
    return cases;
}
int main(int argc,char **argv)
{
    if(argc==2) {
        if(std::strcmp(argv[1],"abort-matrix-iwm")==0) {
            std::printf("IWM actual scan-abort deadline and reset matrix: %u scenarios passed\n",abortWaitMatrix<ItlIwm>()); return 0;
        }
        if(std::strcmp(argv[1],"abort-matrix-iwx")==0) {
            std::printf("IWX actual scan-abort deadline and reset matrix: %u scenarios passed\n",abortWaitMatrix<ItlIwx>()); return 0;
        }
        const bool gated=std::strncmp(argv[1],"gated-abort-",12)==0;
        const bool spurious=std::strncmp(argv[1],"spurious-abort-",15)==0;
        const bool ordinary=std::strncmp(argv[1],"offgate-abort-",14)==0;
        if(!gated && !spurious && !ordinary) return 2;
        const char *family=argv[1]+(gated ? 12 : spurious ? 15 : 14);
        if(std::strcmp(family,"iwm")==0) abortWaitControl<ItlIwm>(gated,spurious);
        else if(std::strcmp(family,"iwx")==0) abortWaitControl<ItlIwx>(gated,spurious);
        else return 2;
        return 0;
    }
    if(argc!=1) return 2;
    std::printf("actual IWM/IWX scan terminal and replay: %u scenario groups passed\n",
                exercise<ItlIwm>() + exercise<ItlIwx>());
    std::printf("actual IWM/IWX scan-abort deadline and reset: %u scenarios passed\n",
                abortWaitMatrix<ItlIwm>()+abortWaitMatrix<ItlIwx>());
}
