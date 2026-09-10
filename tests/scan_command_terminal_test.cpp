#include "include/HAL/ItlScanCommandLease.hpp"
#include "itl80211/openbsd/net80211/ieee80211_join_attempt.h"
#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <functional>

using IOInterruptState = unsigned;
struct IOSimpleLock { bool held = false; unsigned rank = 2; };
static IOSimpleLock *lockStack[2];
static unsigned held, generic, controlled, published, wakes, begins, requeues;
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
constexpr unsigned IEEE80211_SCAN_COMPLETION_WCL_FOREGROUND = 2;
constexpr unsigned IEEE80211_WCL_SCAN_TERMINAL_STATUS_ABORTED = 1;
constexpr unsigned IEEE80211_WCL_SCAN_TERMINAL_STATUS_COMPLETE = 2;
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
    int reserveScanCommandAbort(bool, uint64_t *, bool = false); \
    int waitScanCommandAbort(uint64_t, uint32_t); \
    int lower##_scan_abort(Softc *, bool = false); \
    static int lower##_bgscan_abort(ieee80211com *); \
    int abort_background() { return lower##_bgscan_abort(&com.sc_ic); } \
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
    void unlockTsleep() { assert(!held && sleep_locked); sleep_locked = false; } \
    int tsleep_nsec_locked(void *, int, const char *, uint64_t) { \
        assert(!held && sleep_locked); ++waits; sleep_locked = false; \
        if (sleep_hook) { auto hook = sleep_hook; sleep_hook = {}; hook(); } \
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
}
template<class D> static uint64_t prepare(D &d, uint64_t join = 91, bool bg = false)
{
    if (!d.scanCommand.open)
        assert(d.scanCommand.reopen(d.scanCommand.resetEpoch, d.com.sc_generation));
    const auto serial = d.scanCommand.reserve(d.com.sc_generation, bg ? 0 : join,
                                              true, bg, 0);
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
int main()
{
    std::printf("actual IWM/IWX scan terminal and replay: %u scenario groups passed\n",
                exercise<ItlIwm>() + exercise<ItlIwx>());
}
