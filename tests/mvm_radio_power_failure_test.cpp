// Complete production init task, request CAS, upper mailbox, doorbell action
// and wait. Firmware init, hardware state, IOKit locks/sleep are doubles.
#include <HAL/ItlRadioPowerOnFailureV1.h>
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <initializer_list>

using IOReturn = uint32_t;
using UInt32 = uint32_t;
using u_int8_t = uint8_t;
using IOInterruptState = unsigned;
using AbsoluteTime = uint64_t;
constexpr IOReturn kIOReturnSuccess = 0, kIOReturnNotReady = 0xe00002d8,
    kIOReturnBadArgument = 0xe00002c7, kIOReturnIOError = 0xe00002ca,
    kIOReturnAborted = 0xe00002eb, kIOReturnTimeout = 0xe00002d6;
constexpr IOReturn THREAD_AWAKENED = 0, THREAD_TIMED_OUT = 1;
constexpr int THREAD_ABORTSAFE = 0, kMillisecondScale = 1;
constexpr uint8_t kWiFiPowerOff = 0, kWiFiPowerOn = 1;
constexpr UInt32 kAirportItlwmPmDriverAvailabilityPendingBit = 0x80;
constexpr uint32_t MVM_FLAG_HW_ERR = 0x80, MVM_FLAG_RFKILL = 2,
    MVM_FLAG_SHUTDOWN = 0x100;
constexpr int IFF_UP = 1, IFF_RUNNING = 2;
constexpr int IEEE80211_EVT_RADIO_POWER_ON_FAILED = 27;
static unsigned lockDepth;
struct IOSimpleLock { bool held = false; };
static IOSimpleLock *unlockTarget;
static std::function<void()> unlockHook;
static IOInterruptState IOSimpleLockLockDisableInterrupt(IOSimpleLock *lock) {
    assert(lock && !lock->held && lockDepth == 0); lock->held = true; ++lockDepth; return 1;
}
static void IOSimpleLockUnlockEnableInterrupt(IOSimpleLock *lock, IOInterruptState irq) {
    assert(irq == 1 && lock->held && lockDepth == 1); lock->held = false; --lockDepth;
    if (lock == unlockTarget && unlockHook) {
        auto hook = unlockHook; unlockHook = {}; hook();
    }
}
static void OSBitOrAtomic(UInt32 value, UInt32 *flags) { *flags |= value; }
static void clock_interval_to_deadline(uint32_t milliseconds, int, uint64_t *deadline) { *deadline = milliseconds; }
static int splnet() { return 0; }
static void splx(int) {}
#define XYLog(...) do {} while (0)
#define DEVNAME(sc) "mvm-test"
struct OSObject { virtual ~OSObject() = default; };
#define OSDynamicCast(type, object) dynamic_cast<type *>(object)
struct IONetworkInterface {};
struct IOInterruptEventSource {
    unsigned refs = 1, interrupts = 0;
    void retain() { ++refs; }
    void release() { assert(refs > 1); --refs; }
};
struct IOWorkLoop { bool gated = false; bool inGate() const { return gated; } };
struct IOCommandGate {
    IOWorkLoop *loop = nullptr;
    unsigned wakes = 0, sleeps = 0;
    std::function<IOReturn()> sleepHook;
    void commandWakeup(void *, bool) { assert(loop->gated && !lockDepth); ++wakes; }
    IOReturn commandSleep(void *, AbsoluteTime, int) {
        assert(loop->gated && !lockDepth); ++sleeps; loop->gated = false;
        IOReturn result = sleepHook ? sleepHook() : THREAD_TIMED_OUT;
        loop->gated = true; return result;
    }
};
namespace TahoeWclPhysicalScanContracts {
struct State { uint64_t activeGeneration = 0; uint32_t activeBackendGeneration = 0, terminalStatus = 0; };
static bool ownsCompletion(State *, uint64_t, uint32_t) { return false; }
}
struct AirportItlwmWclPhysicalScanLifecycle {
    IOSimpleLock *admissionLock = nullptr;
    IOInterruptEventSource *source = nullptr;
    unsigned users = 0;
    bool settingUp = false, stopping = false, tearingDown = false;
    bool snapshotReady = false, snapshotInProgress = false, terminalQueued = false, publishing = false;
    TahoeWclPhysicalScanContracts::State state;
    uint64_t availabilityEpoch = 0, pendingPowerOnEpoch = 0, readyPowerOnEpoch = 0, failedPowerOnEpoch = 0;
    IOReturn powerOnFailureStatus = 0;
    bool powerOnPublishQueued = false, powerOnFailureQueued = false;
    bool powerOnWakeBulletinPending = false, powerOnWakeScanTerminalObserved = false;
    bool powerOnWakeAvailabilityAckObserved = false, powerOnWakePublishQueued = false;
};
class AirportItlwm : public OSObject {
public:
    IOSimpleLock lock;
    IOInterruptEventSource source;
    IOWorkLoop loop;
    IOCommandGate gate;
    AirportItlwmWclPhysicalScanLifecycle fWclPhysicalScanLifecycle;
    UInt32 pmPowerStateFlags = 0;
    uint8_t power_state = kWiFiPowerOn;
    unsigned gateReads = 0, wakeBulletins = 0;
    AirportItlwm() {
        fWclPhysicalScanLifecycle.admissionLock = &lock;
        fWclPhysicalScanLifecycle.source = &source; gate.loop = &loop;
    }
    IOCommandGate *getCommandGate() { ++gateReads; return &gate; }
    IOWorkLoop *getWorkLoop() { return &loop; }
    uint64_t armDeferredPowerOnAvailability(bool = false);
    void noteRadioPowerOnFailure(const ItlRadioPowerOnFailureV1 *);
    void dispatchRadioPowerOnFailure(uint64_t);
    IOReturn waitForDeferredPowerOnAvailability(uint64_t, uint32_t);
    void dispatchDeferredWakePowerChanged(uint64_t) { ++wakeBulletins; }
};
struct AirportItlwmControllerLifecycleOperationGuard {
    AirportItlwmControllerLifecycleOperationGuard(AirportItlwm *, bool) {}
    bool admitted() const { return true; }
};
static void signalWclPhysicalScanTerminalDoorbell(AirportItlwmWclPhysicalScanLifecycle &state,
    IOSimpleLock *, IOInterruptEventSource *source) {
    assert(!lockDepth && state.users == 1 && source->refs == 2);
    ++source->interrupts; source->release(); --state.users;
}
static void dispatchWclPhysicalScanTerminal(AirportItlwm *, uint64_t, uint32_t, uint32_t) { assert(false); }
#include "controller.inc"
static void serviceInterrupt(AirportItlwm &upper) {
    assert(upper.source.interrupts != 0); --upper.source.interrupts;
    upper.loop.gated = true;
    wclPhysicalScanTerminalInterruptAction(&upper, &upper.source, 1);
    upper.loop.gated = false;
}

struct Task { bool queued = false; };
static int queueTag;
static int *systq = &queueTag;
static unsigned adds, dels;
static bool task_add(int *, Task *task) { ++adds; task->queued = true; return true; }
static bool task_del(int *, Task *task) { ++dels; task->queued = false; return true; }
struct _ifnet { int if_flags = 0; };
struct ieee80211com {
    _ifnet ic_if;
    AirportItlwm *upper = nullptr;
    unsigned callbacks = 0;
    ItlRadioPowerOnFailureV1 lastFailure{};
    void (*ic_event_handler)(ieee80211com *, int, void *) = nullptr;
};
struct mvm_softc {
    void *owner = nullptr;
    ieee80211com sc_ic;
    uint32_t sc_flags = 0;
    int sc_generation = 1;
    uint8_t init_retry_count = 0;
    Task init_task;
};
#define container_of(ptr, type, member) static_cast<type *>((ptr)->owner)
static void event(ieee80211com *ic, int code, void *data) {
    assert(code == IEEE80211_EVT_RADIO_POWER_ON_FAILED && !lockDepth);
    ++ic->callbacks; ic->lastFailure = *static_cast<ItlRadioPowerOnFailureV1 *>(data);
    const unsigned before = ic->upper->gateReads;
    ic->upper->noteRadioPowerOnFailure(&ic->lastFailure);
    assert(ic->upper->gateReads == before); // no gate access from init worker
}
class ItlMvm {
public:
    mvm_softc com;
    IOSimpleLock lowerLock;
    IOSimpleLock *wclScanLock = &lowerLock;
    uint64_t radioPowerOnEpoch = 0;
    unsigned initCalls = 0, stops = 0;
    int initResult = EIO;
    std::function<void()> initHook;
    explicit ItlMvm(AirportItlwm &upper) {
        com.owner = this; com.sc_ic.upper = &upper; com.sc_ic.ic_event_handler = event;
    }
    IOReturn enable(IONetworkInterface *) {
        com.sc_ic.ic_if.if_flags |= IFF_UP; task_add(systq, &com.init_task); return kIOReturnSuccess;
    }
    IOReturn enableForRadioPowerOn(IONetworkInterface *, uint64_t);
    void cancelRadioPowerOnRequest(uint64_t);
    uint64_t radioPowerOnRequestEpoch() const;
    uint8_t claimRadioPowerOnRetry(uint64_t);
    void reportRadioPowerOnFailure(uint64_t, IOReturn, uint32_t, int);
    static void mvm_init_task(void *);
    int mvm_init(_ifnet *) {
        ++initCalls; ++com.sc_generation;
        if (initHook) { auto hook = initHook; initHook = {}; hook(); }
        if (initResult == 0) com.sc_ic.ic_if.if_flags |= IFF_RUNNING;
        return initResult;
    }
    int mvm_init_internal(_ifnet *ifp, bool) { return mvm_init(ifp); }
    void mvm_stop(_ifnet *ifp) { ++stops; ++com.sc_generation; ifp->if_flags &= ~IFF_RUNNING; }
    void mvm_stop_internal(_ifnet *ifp, bool, bool) { mvm_stop(ifp); }
    void mvm_sae_driver_reset_recovery_prepare(mvm_softc *) {}
    void mvm_bootstrap_init_task(mvm_softc *sc) { task_add(systq, &sc->init_task); }
    void run() { com.init_task.queued = false; mvm_init_task(&com); }
};
#include "hal.inc"

static void fatal_and_late_rfkill() {
    for (uint32_t fatal : {MVM_FLAG_RFKILL, MVM_FLAG_HW_ERR}) {
        AirportItlwm upper;
        ItlMvm lower(upper);
        const uint64_t epoch = upper.armDeferredPowerOnAvailability(true);
        assert(lower.enableForRadioPowerOn(nullptr, epoch) == 0);
        lower.com.sc_flags |= fatal;
        lower.run();
        assert(lower.com.sc_ic.callbacks == 1 && lower.initCalls == 0);
        assert(lower.com.sc_ic.lastFailure.requestEpoch == epoch);
        assert(lower.com.sc_ic.lastFailure.status == (fatal == MVM_FLAG_RFKILL ? kIOReturnNotReady : kIOReturnIOError));
        assert(upper.gateReads == 0 && upper.gate.wakes == 0);
        serviceInterrupt(upper);
        upper.loop.gated = true;
        assert(upper.waitForDeferredPowerOnAvailability(epoch, 15000) == lower.com.sc_ic.lastFailure.status);
        assert(upper.gate.sleeps == 0 && upper.gate.wakes == 1 && upper.wakeBulletins == 0);
        assert(upper.pmPowerStateFlags & kAirportItlwmPmDriverAvailabilityPendingBit);
        assert(upper.fWclPhysicalScanLifecycle.pendingPowerOnEpoch == epoch);
        lower.reportRadioPowerOnFailure(epoch, kIOReturnIOError, kItlRadioPowerOnFailureHardware, EIO);
        assert(lower.com.sc_ic.callbacks == 1);
    }
    AirportItlwm upper;
    ItlMvm lower(upper);
    const uint64_t epoch = upper.armDeferredPowerOnAvailability();
    lower.enableForRadioPowerOn(nullptr, epoch);
    lower.initResult = EPERM;
    lower.initHook = [&] { lower.com.sc_flags |= MVM_FLAG_RFKILL; };
    lower.run();
    assert(lower.initCalls == 1 && lower.com.sc_ic.callbacks == 1);
    assert(lower.com.init_retry_count == 0 && !lower.com.init_task.queued);
    assert(lower.com.sc_ic.lastFailure.reason == kItlRadioPowerOnFailureRfKill);
}
static void retry_exhaustion_and_success() {
    AirportItlwm upper;
    ItlMvm lower(upper);
    const uint64_t epoch = upper.armDeferredPowerOnAvailability();
    lower.enableForRadioPowerOn(nullptr, epoch);
    for (unsigned attempt = 1; attempt <= 5; ++attempt) {
        lower.run();
        assert(lower.initCalls == attempt && lower.com.init_retry_count == attempt);
        assert(lower.com.sc_ic.callbacks == (attempt == 5 ? 1U : 0U));
        assert(lower.com.init_task.queued == (attempt < 5));
    }
    assert(lower.com.sc_ic.lastFailure.requestEpoch == epoch);
    assert(lower.com.sc_ic.lastFailure.reason == kItlRadioPowerOnFailureRecoveryExhausted);
    assert(lower.com.sc_ic.lastFailure.lowerError == EIO);
    assert(lower.radioPowerOnRequestEpoch() == 0);

    AirportItlwm recovered;
    ItlMvm good(recovered);
    good.enableForRadioPowerOn(nullptr, recovered.armDeferredPowerOnAvailability());
    good.run(); good.run(); good.initResult = 0; good.run();
    assert(good.initCalls == 3 && good.com.sc_ic.callbacks == 0 && good.radioPowerOnRequestEpoch() == 0);
}
static void replacement_and_stale_doorbell() {
    AirportItlwm upper;
    ItlMvm lower(upper);
    const uint64_t oldEpoch = upper.armDeferredPowerOnAvailability();
    lower.enableForRadioPowerOn(nullptr, oldEpoch);
    lower.initHook = [&] {
        const uint64_t next = upper.armDeferredPowerOnAvailability();
        assert(next != oldEpoch); lower.enableForRadioPowerOn(nullptr, next);
    };
    lower.run();
    assert(lower.com.sc_ic.callbacks == 0 && lower.com.init_retry_count == 0);
    assert(lower.radioPowerOnRequestEpoch() == 2 && lower.com.init_task.queued);
    lower.cancelRadioPowerOnRequest(oldEpoch);
    assert(lower.radioPowerOnRequestEpoch() == 2);
    lower.com.sc_flags |= MVM_FLAG_RFKILL;
    lower.run();
    assert(lower.com.sc_ic.lastFailure.requestEpoch == 2);
    assert(upper.source.interrupts == 1);
    upper.armDeferredPowerOnAvailability();
    serviceInterrupt(upper);
    assert(upper.gate.wakes == 0 && upper.fWclPhysicalScanLifecycle.failedPowerOnEpoch == 0);
    assert(upper.source.refs == 1 && upper.fWclPhysicalScanLifecycle.users == 0);
}
static void sleep_wakeup_and_invalid_values() {
    AirportItlwm upper;
    ItlMvm lower(upper);
    const uint64_t epoch = upper.armDeferredPowerOnAvailability();
    lower.enableForRadioPowerOn(nullptr, epoch);
    upper.gate.sleepHook = [&] {
        lower.com.sc_flags |= MVM_FLAG_RFKILL; lower.run();
        serviceInterrupt(upper); return THREAD_AWAKENED;
    };
    upper.loop.gated = true;
    assert(upper.waitForDeferredPowerOnAvailability(epoch, 15000) == kIOReturnNotReady);
    assert(upper.gate.sleeps == 1 && upper.gate.wakes == 1);
    auto failure = lower.com.sc_ic.lastFailure;
    const uint64_t next = upper.armDeferredPowerOnAvailability();
    upper.noteRadioPowerOnFailure(&failure); // old epoch
    assert(upper.fWclPhysicalScanLifecycle.failedPowerOnEpoch == 0);
    failure.requestEpoch = next;
    for (int malformed = 0; malformed != 7; ++malformed) {
        auto bad = failure;
        if (malformed == 0) bad.version = 0;
        if (malformed == 1) bad.size = 0;
        if (malformed == 2) bad.reserved = 1;
        if (malformed == 3) bad.status = 0;
        if (malformed == 4) bad.reason = 0;
        if (malformed == 5) bad.reason = kItlRadioPowerOnFailureRecoveryExhausted + 1;
        if (malformed == 6) bad.requestEpoch = 0;
        upper.noteRadioPowerOnFailure(&bad);
        assert(upper.fWclPhysicalScanLifecycle.failedPowerOnEpoch == 0);
    }
    upper.fWclPhysicalScanLifecycle.stopping = true;
    upper.noteRadioPowerOnFailure(&failure);
    assert(upper.fWclPhysicalScanLifecycle.failedPowerOnEpoch == 0);
    AirportItlwm boot;
    ItlMvm legacy(boot);
    legacy.enable(nullptr); legacy.com.sc_flags |= MVM_FLAG_RFKILL; legacy.run();
    assert(legacy.com.sc_ic.callbacks == 0 && boot.source.interrupts == 0);
}
static void exhausted_old_claim_does_not_delete_new_work() {
    AirportItlwm upper;
    ItlMvm lower(upper);
    const uint64_t oldEpoch = upper.armDeferredPowerOnAvailability();
    lower.enableForRadioPowerOn(nullptr, oldEpoch);
    lower.com.init_retry_count = 4;
    unlockTarget = &lower.lowerLock;
    unlockHook = [&] {
        lower.enableForRadioPowerOn(nullptr, upper.armDeferredPowerOnAvailability());
    };
    lower.run();
    unlockTarget = nullptr;
    assert(lower.radioPowerOnRequestEpoch() == 2 && lower.com.init_retry_count == 0);
    assert(lower.com.init_task.queued && lower.com.sc_ic.callbacks == 0);
}
int main() {
    fatal_and_late_rfkill(); retry_exhaustion_and_success();
    replacement_and_stale_doorbell(); sleep_wakeup_and_invalid_values();
    exhausted_old_claim_does_not_delete_new_work();
    assert(lockDepth == 0);
    std::puts("MVM activation failure: PASS (actual workers, five attempts, request CAS, nonblocking mailbox/doorbell, wait, stale/cancel/bootstrap fences)");
}
