// Real scan lease, full production ready producer/receipt validator and
// upper mailbox/action/availability bodies. Firmware state, IOKit and public
// carrier delivery are explicit doubles; no on-air qualification is implied.
#include <HAL/ItlRadioReadyV1.h>
#include <HAL/ItlScanCommandLease.hpp>
#include <AirportItlwm/TahoeWclPhysicalScanContracts.hpp>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <initializer_list>

using IOReturn = int32_t;
using UInt32 = uint32_t;
using IOInterruptState = unsigned;
constexpr IOReturn kIOReturnSuccess = 0,
    kIOReturnNotReady = static_cast<IOReturn>(0xe00002d8),
    kIOReturnBadArgument = static_cast<IOReturn>(0xe00002c7),
    kIOReturnAborted = static_cast<IOReturn>(0xe00002eb);
constexpr uint8_t kWiFiPowerOff = 0, kWiFiPowerOn = 1;
constexpr UInt32 kAirportItlwmPmDriverAvailabilityPendingBit = 0x80;
constexpr uintptr_t kAirportItlwmDeferredPowerAvailabilityCancel = 1,
    kAirportItlwmDeferredPowerAvailabilityCancelEpoch = 2,
    kAirportItlwmDeferredPowerAvailabilityPublishOff = 3,
    kAirportItlwmDeferredPowerAvailabilityPublishWakePowerChanged = 4,
    kAirportItlwmDeferredPowerAvailabilityPublishOn = 5;
constexpr int IEEE80211_EVT_WCL_SCAN_REOPENED = 13;
constexpr int IFF_UP = 1, IFF_RUNNING = 2, IEEE80211_F_AUTO_JOIN = 4;
constexpr uint32_t MVM_FLAG_RFKILL = 2, MVM_FLAG_HW_ERR = 0x80,
    MVM_FLAG_SHUTDOWN = 0x100;
#define __IO80211_TARGET 260000
#define __MAC_26_0 260000
constexpr int APPLE80211_M_POWER_CHANGED = 7;
static bool lowerContext;
static unsigned lockDepth;
struct IOSimpleLock { bool held = false; };
static IOSimpleLock *unlockTarget;
static std::function<void()> unlockHook;
static IOInterruptState IOSimpleLockLockDisableInterrupt(IOSimpleLock *lock) {
    assert(lock && !lock->held && !lockDepth); lock->held = true; ++lockDepth; return 1;
}
static void IOSimpleLockUnlockEnableInterrupt(IOSimpleLock *lock, IOInterruptState irq) {
    assert(lock && lock->held && irq == 1 && lockDepth == 1);
    lock->held = false; --lockDepth;
    if (lock == unlockTarget && unlockHook) {
        auto hook = unlockHook; unlockHook = {}; hook();
    }
}
static void OSBitOrAtomic(UInt32 value, UInt32 *flags) { *flags |= value; }
static void OSBitAndAtomic(UInt32 value, UInt32 *flags) { *flags &= value; }
#define XYLog(...) do {} while (0)
struct OSObject { virtual ~OSObject() = default; };
#define OSDynamicCast(type, object) dynamic_cast<type *>(object)
struct IONetworkInterface {};
struct _ifnet { int if_flags = 0; };
class AirportItlwm;
struct ieee80211com {
    _ifnet ic_if;
    struct { bool empty = true; } ic_ess;
    int ic_flags = 0;
    unsigned deselections = 0, callbacks = 0;
    AirportItlwm *upper = nullptr;
    void (*ic_event_handler)(ieee80211com *, int, void *) = nullptr;
    ItlRadioReadyV1 lastReady{};
};
#define TAILQ_EMPTY(head) ((head)->empty)
static void ieee80211_deselect_ess(ieee80211com *ic) {
    assert(!lowerContext && !lockDepth); ++ic->deselections;
}
static void ieee80211_wcl_join_cancel(ieee80211com *, int) {}
static void ieee80211_roam_link_cancel(ieee80211com *) {}
struct mvm_softc {
    ieee80211com sc_ic;
    uint32_t sc_flags = 0;
    int sc_generation = 1;
    uint8_t init_retry_count = 0;
};
constexpr int MVM_LONG_GROUP = 0, MVM_ADD_STA = 1, MVM_TX_CMD = 2,
    MVM_BEACON_TEMPLATE_CMD = 3;
static uint8_t mvm_ap_go_command_version(mvm_softc *, int, int) { return 0; }
static void event(ieee80211com *, int, void *);
class ItlMvm {
public:
    mvm_softc com;
    IOSimpleLock lowerLock;
    IOSimpleLock *wclScanLock = &lowerLock;
    ItlScanCommandLease scanCommand{};
    bool wclScanNeedsReopen = true;
    uint64_t radioPowerOnEpoch = 0, radioReadyReceiptSerial = 0,
        radioReadyRequestEpoch = 0;
    uint32_t radioReadyBackendGeneration = 0;
    explicit ItlMvm(AirportItlwm &upper) {
        com.sc_ic.upper = &upper; com.sc_ic.ic_event_handler = event;
    }
    IOReturn enable(IONetworkInterface *) { com.sc_ic.ic_if.if_flags |= IFF_UP; return 0; }
    IOReturn enableForRadioPowerOn(IONetworkInterface *, uint64_t);
    void cancelRadioPowerOnRequest(uint64_t);
    uint64_t radioPowerOnRequestEpoch() const;
    bool isRadioReadyCurrent(const ItlRadioReadyV1 *);
    void noteWclScanRadioReady(uint64_t);
    ieee80211com *get80211Controller() { return &com.sc_ic; }
    uint64_t startRealLease() {
        scanCommand.invalidate();
        assert(scanCommand.reopen(scanCommand.resetEpoch, com.sc_generation));
        com.sc_ic.ic_if.if_flags = IFF_UP | IFF_RUNNING;
        wclScanNeedsReopen = true;
        const uint64_t serial = scanCommand.reserve(com.sc_generation, 0, true, false, 1);
        assert(serial != 0); return serial;
    }
};
#include "hal.inc"

struct IOInterruptEventSource {
    unsigned refs = 1, interrupts = 0;
    void retain() { ++refs; }
    void release() { assert(refs > 1); --refs; }
};
struct IOWorkLoop { bool gated = false; bool inGate() const { return gated; } };
using Action = IOReturn (*)(OSObject *, void *, void *, void *, void *);
struct IOCommandGate {
    OSObject *owner = nullptr;
    IOWorkLoop *loop = nullptr;
    unsigned actions = 0, wakes = 0;
    IOReturn runAction(Action action, void *a = nullptr, void *b = nullptr,
                       void *c = nullptr, void *d = nullptr) {
        // A lower callback trying this while Off drains it is a gate inversion.
        assert(!lowerContext && !lockDepth); ++actions;
        bool wasGated = loop->gated; loop->gated = true;
        IOReturn result = action(owner, a, b, c, d);
        loop->gated = wasGated; return result;
    }
    void commandWakeup(void *, bool) { assert(loop->gated && !lowerContext); ++wakes; }
};
struct AirportItlwmWclPhysicalScanLifecycle {
    IOSimpleLock *admissionLock = nullptr;
    IOInterruptEventSource *source = nullptr;
    unsigned users = 0;
    bool settingUp = false, stopping = false, tearingDown = false;
    bool snapshotReady = false, snapshotInProgress = false, terminalQueued = false, publishing = false;
    TahoeWclPhysicalScanContracts::State state{};
    uint64_t availabilityEpoch = 0, pendingPowerOnEpoch = 0, readyPowerOnEpoch = 0, failedPowerOnEpoch = 0;
    IOReturn powerOnFailureStatus = 0;
    bool powerOnPublishQueued = false, powerOnFailureQueued = false;
    ItlRadioReadyV1 radioReady{};
    uint64_t radioReadyAvailabilityEpoch = 0;
    bool radioReadyQueued = false;
    bool powerOnWakeBulletinPending = false, powerOnWakeScanTerminalObserved = false;
    bool powerOnWakeAvailabilityAckObserved = false, powerOnWakePublishQueued = false;
};
struct TahoeOwnerRegistry {
    struct AssociationOwner {};
    AssociationOwner association, publicAssociation;
};
class AirportItlwm : public OSObject {
public:
    IOSimpleLock lock;
    IOInterruptEventSource source;
    IOWorkLoop loop;
    IOCommandGate gate;
    AirportItlwmWclPhysicalScanLifecycle fWclPhysicalScanLifecycle;
    ItlMvm *fHalService = nullptr;
    void *fNetIf = this;
    UInt32 pmPowerStateFlags = 0;
    uint8_t power_state = kWiFiPowerOn;
    unsigned gateReads = 0, reopenWcl = 0, reopenStandard = 0, apPublications = 0,
        onCarriers = 0, offCarriers = 0, wakeCarriers = 0, scanTerminals = 0;
    TahoeOwnerRegistry registry;
    std::function<void()> apHook;
    AirportItlwm() {
        fWclPhysicalScanLifecycle.admissionLock = &lock;
        fWclPhysicalScanLifecycle.source = &source;
        gate.loop = &loop; gate.owner = this;
    }
    IOCommandGate *getCommandGate() { ++gateReads; return &gate; }
    IOWorkLoop *getWorkLoop() { assert(!lowerContext); return &loop; }
    uint64_t armDeferredPowerOnAvailability(bool = false);
    void cancelDeferredPowerOnAvailabilityRaw();
    bool cancelDeferredPowerOnAvailabilityEpochRaw(uint64_t);
    static IOReturn publishDeferredPowerAvailabilityGated(OSObject *, void *, void *, void *, void *);
    void noteRadioReady(const ItlRadioReadyV1 *);
    void dispatchRadioReady(uint64_t, uint64_t);
    bool noteRadioScanReadyAndQueuePowerOnAvailability();
    void dispatchRadioPowerOnFailure(uint64_t) { assert(false); }
    void dispatchDeferredWakePowerChanged(uint64_t) { assert(false); }
    void reopenWclPhysicalScanAfterRadioReset() { assert(loop.gated && !lowerContext); ++reopenWcl; }
    void reopenStandardPhysicalScanAfterRadioReset() { assert(loop.gated && !lowerContext); ++reopenStandard; }
    TahoeOwnerRegistry &getTahoeOwnerRegistry() { return registry; }
    void postTahoeWclInternalLinkDownInd() {}
    void postMessage(void *, int code, void *, int, bool) {
        assert(loop.gated && !lowerContext && code == APPLE80211_M_POWER_CHANGED); ++wakeCarriers;
    }
};
struct AirportItlwmControllerLifecycleOperationGuard {
    AirportItlwmControllerLifecycleOperationGuard(AirportItlwm *, bool) {}
    bool admitted() const { return true; }
};
namespace TahoeDriverAvailabilityContracts { enum class Transition { PowerOff, PowerOn }; }
static void postTahoeDriverAvailabilityTransition(AirportItlwm *upper,
    TahoeDriverAvailabilityContracts::Transition transition) {
    assert(!lowerContext && !lockDepth && upper->loop.gated);
    if (transition == TahoeDriverAvailabilityContracts::Transition::PowerOn) ++upper->onCarriers;
    else ++upper->offCarriers;
}
static IOReturn publishDefaultAPSTAInterfaceGated(OSObject *owner, void *, void *, void *, void *) {
    auto *upper = static_cast<AirportItlwm *>(owner);
    assert(!lowerContext && !lockDepth && upper->loop.gated); ++upper->apPublications;
    if (upper->apHook) upper->apHook(); return 0;
}
static void signalWclPhysicalScanTerminalDoorbell(AirportItlwmWclPhysicalScanLifecycle &state,
    IOSimpleLock *, IOInterruptEventSource *source) {
    assert(!lockDepth && state.users == 1 && source->refs == 2);
    ++source->interrupts; source->release(); --state.users;
}
static void dispatchWclPhysicalScanTerminal(AirportItlwm *upper, uint64_t generation,
    uint32_t backend, uint32_t status) {
    // Scan result delivery is a separate tested callout. Assert ordering at
    // this actual interrupt-action boundary, with the real completion lease.
    assert(upper->loop.gated && !lowerContext && upper->onCarriers == 1);
    assert(generation == 11 && backend == 22 && status == 0);
    ++upper->scanTerminals;
    TahoeWclPhysicalScanContracts::finishCompletion(
        &upper->fWclPhysicalScanLifecycle.state, generation, backend);
}
#include "controller.inc"
static void event(ieee80211com *ic, int code, void *data) {
    assert(lowerContext && !lockDepth && code == IEEE80211_EVT_WCL_SCAN_REOPENED);
    ++ic->callbacks;
#if MVM_RADIO_READY_LEGACY_NEGATIVE
    (void)data;
    ic->upper->noteRadioScanReadyAndQueuePowerOnAvailability();
#else
    assert(data != nullptr); ic->lastReady = *static_cast<ItlRadioReadyV1 *>(data);
    const auto gateReads = ic->upper->gateReads;
    ic->upper->noteRadioReady(&ic->lastReady);
    assert(ic->upper->gateReads == gateReads);
#endif
}
static void produce(ItlMvm &lower, uint64_t serial) {
    lowerContext = true; lower.noteWclScanRadioReady(serial); lowerContext = false;
}
static void service(AirportItlwm &upper) {
    assert(upper.source.interrupts != 0); --upper.source.interrupts;
    upper.loop.gated = true;
    wclPhysicalScanTerminalInterruptAction(&upper, &upper.source, 1);
    upper.loop.gated = false;
}
static void tagged_ready_and_completed_scan() {
    AirportItlwm upper; ItlMvm lower(upper); upper.fHalService = &lower;
    const uint64_t epoch = upper.armDeferredPowerOnAvailability();
    assert(lower.enableForRadioPowerOn(nullptr, epoch) == 0);
    const uint64_t serial = lower.startRealLease();
    upper.loop.gated = true; // concurrent Off may own the upper gate
    produce(lower, serial); upper.loop.gated = false;
    assert(upper.reopenWcl == 0 && upper.apPublications == 0 && upper.onCarriers == 0);
    assert(lower.com.sc_ic.lastReady.requestEpoch == epoch);
    lower.scanCommand.clearCommand(); // real lease completion precedes delivery
    lower.cancelRadioPowerOnRequest(epoch); // successful init no longer owns failure
    assert(lower.radioPowerOnRequestEpoch() == 0);
    assert(lower.isRadioReadyCurrent(&lower.com.sc_ic.lastReady));
    service(upper);
    assert(upper.onCarriers == 1 && upper.reopenWcl == 1 && upper.reopenStandard == 1);
    assert(upper.apPublications == 1 && lower.com.sc_ic.deselections == 1);
    assert(upper.fWclPhysicalScanLifecycle.pendingPowerOnEpoch == 0);
    assert((upper.pmPowerStateFlags & kAirportItlwmPmDriverAvailabilityPendingBit) == 0);
    upper.noteRadioReady(&lower.com.sc_ic.lastReady); // duplicate cannot republish
    assert(upper.source.interrupts == 0 && upper.onCarriers == 1);
    assert(upper.source.refs == 1 && upper.fWclPhysicalScanLifecycle.users == 0);
}
static void replacement_captured_at_unlock() {
    AirportItlwm upper; ItlMvm lower(upper); upper.fHalService = &lower;
    const auto oldEpoch = upper.armDeferredPowerOnAvailability();
    lower.enableForRadioPowerOn(nullptr, oldEpoch);
    const auto serial = lower.startRealLease();
    unlockTarget = &lower.lowerLock;
    unlockHook = [&] {
        upper.cancelDeferredPowerOnAvailabilityRaw();
        const auto newEpoch = upper.armDeferredPowerOnAvailability();
        assert(newEpoch != oldEpoch); lower.enableForRadioPowerOn(nullptr, newEpoch);
    };
    produce(lower, serial); unlockTarget = nullptr;
    assert(lower.com.sc_ic.lastReady.requestEpoch == oldEpoch);
    assert(!lower.isRadioReadyCurrent(&lower.com.sc_ic.lastReady));
    assert(!upper.fWclPhysicalScanLifecycle.radioReadyQueued && !upper.onCarriers);
}
static void queued_old_ready_cannot_pop_new() {
    AirportItlwm upper; ItlMvm lower(upper); upper.fHalService = &lower;
    auto oldEpoch = upper.armDeferredPowerOnAvailability();
    lower.enableForRadioPowerOn(nullptr, oldEpoch);
    auto oldSerial = lower.startRealLease(); produce(lower, oldSerial);
    upper.cancelDeferredPowerOnAvailabilityRaw();
    auto next = upper.armDeferredPowerOnAvailability();
    lower.enableForRadioPowerOn(nullptr, next);
    ++lower.com.sc_generation;
    auto fresh = lower.startRealLease(); produce(lower, fresh);
    upper.loop.gated = true; upper.dispatchRadioReady(oldSerial, oldEpoch); upper.loop.gated = false;
    assert(upper.fWclPhysicalScanLifecycle.radioReadyQueued && !upper.onCarriers);
    service(upper); // any doorbell can service a genuinely queued current value
    assert(upper.onCarriers == 1 && upper.apPublications == 1);
    service(upper); assert(upper.onCarriers == 1);
}
static void bootstrap_and_failures() {
    AirportItlwm boot; ItlMvm lower(boot); boot.fHalService = &lower;
    auto serial = lower.startRealLease(); produce(lower, serial); service(boot);
    assert(boot.reopenWcl == 1 && boot.apPublications == 1 && boot.onCarriers == 0);
    boot.noteRadioReady(&lower.com.sc_ic.lastReady);
    assert(boot.source.interrupts == 0); // bootstrap duplicates must not deselect again
    boot.armDeferredPowerOnAvailability();
    boot.noteRadioReady(&lower.com.sc_ic.lastReady); // old epoch-zero bootstrap
    assert(!boot.fWclPhysicalScanLifecycle.radioReadyQueued);
    for (int failure = 0; failure != 7; ++failure) {
        AirportItlwm upper; ItlMvm radio(upper); upper.fHalService = &radio;
        auto epoch = upper.armDeferredPowerOnAvailability();
        radio.enableForRadioPowerOn(nullptr, epoch);
        produce(radio, radio.startRealLease());
        if (failure == 0) radio.com.sc_flags |= MVM_FLAG_RFKILL;
        if (failure == 1) radio.com.sc_flags |= MVM_FLAG_HW_ERR;
        if (failure == 2) radio.com.sc_flags |= MVM_FLAG_SHUTDOWN;
        if (failure == 3) ++radio.com.sc_generation;
        if (failure == 4) radio.com.sc_ic.ic_if.if_flags &= ~IFF_RUNNING;
        if (failure == 5) upper.fWclPhysicalScanLifecycle.failedPowerOnEpoch = epoch;
        if (failure == 6) upper.power_state = kWiFiPowerOff;
        service(upper);
        assert(!upper.onCarriers && !upper.reopenWcl && !upper.apPublications);
    }
    AirportItlwm upper; ItlMvm radio(upper); upper.fHalService = &radio;
    auto epoch = upper.armDeferredPowerOnAvailability(); radio.enableForRadioPowerOn(nullptr, epoch);
    produce(radio, radio.startRealLease());
    upper.apHook = [&] { radio.com.sc_flags |= MVM_FLAG_RFKILL; };
    service(upper); assert(upper.reopenWcl == 1 && upper.onCarriers == 0);
}
static void newer_receipt_replaces_queued_reset() {
    AirportItlwm upper; ItlMvm lower(upper); upper.fHalService = &lower;
    auto epoch = upper.armDeferredPowerOnAvailability();
    lower.enableForRadioPowerOn(nullptr, epoch);
    produce(lower, lower.startRealLease());
    auto old = lower.com.sc_ic.lastReady;
    ++lower.com.sc_generation;
    produce(lower, lower.startRealLease());
    auto fresh = lower.com.sc_ic.lastReady;
    assert(fresh.receiptSerial > old.receiptSerial);
    upper.noteRadioReady(&old); // delayed older receipt cannot replace the fresh value
    assert(upper.fWclPhysicalScanLifecycle.radioReady.receiptSerial == fresh.receiptSerial);
    service(upper); assert(upper.onCarriers == 1 && upper.apPublications == 1);
    service(upper); assert(upper.onCarriers == 1);
}
static void coalesced_census_follows_availability() {
    AirportItlwm upper; ItlMvm lower(upper); upper.fHalService = &lower;
    auto epoch = upper.armDeferredPowerOnAvailability();
    lower.enableForRadioPowerOn(nullptr, epoch);
    produce(lower, lower.startRealLease());
    auto &state = upper.fWclPhysicalScanLifecycle;
    state.snapshotReady = true; state.terminalQueued = true;
    state.state.activeGeneration = 11; state.state.activeBackendGeneration = 22;
    state.state.terminalBackendGeneration = 22;
    state.state.phase = TahoeWclPhysicalScanContracts::Phase::Completing;
    service(upper);
    assert(upper.scanTerminals == 1 && upper.onCarriers == 1);
}
static void reentrant_role_publication_preserves_successor() {
    AirportItlwm upper; ItlMvm lower(upper); upper.fHalService = &lower;
    const auto oldEpoch = upper.armDeferredPowerOnAvailability();
    lower.enableForRadioPowerOn(nullptr, oldEpoch);
    produce(lower, lower.startRealLease());
    uint64_t successor = 0;
    upper.apHook = [&] {
        upper.cancelDeferredPowerOnAvailabilityRaw();
        successor = upper.armDeferredPowerOnAvailability();
        lower.enableForRadioPowerOn(nullptr, successor);
        ++lower.com.sc_generation;
        produce(lower, lower.startRealLease());
    };
    service(upper); upper.apHook = {};
    assert(successor != oldEpoch && upper.onCarriers == 0);
    assert(upper.fWclPhysicalScanLifecycle.pendingPowerOnEpoch == successor);
    assert(upper.fWclPhysicalScanLifecycle.radioReadyQueued);
    service(upper);
    assert(upper.onCarriers == 1 && upper.fWclPhysicalScanLifecycle.pendingPowerOnEpoch == 0);
}
static void malformed_and_lifecycle() {
    AirportItlwm upper; ItlMvm lower(upper); upper.fHalService = &lower;
    const auto epoch = upper.armDeferredPowerOnAvailability();
    ItlRadioReadyV1 valid{1, sizeof(ItlRadioReadyV1), epoch, 1, 1, 0};
    for (int malformed = 0; malformed != 6; ++malformed) {
        auto bad = valid;
        if (malformed == 0) bad.version = 0;
        if (malformed == 1) bad.size = 0;
        if (malformed == 2) bad.reserved = 1;
        if (malformed == 3) bad.receiptSerial = 0;
        if (malformed == 4) bad.backendGeneration = 0;
        if (malformed == 5) bad.requestEpoch = epoch + 1;
        upper.noteRadioReady(&bad); assert(!upper.fWclPhysicalScanLifecycle.radioReadyQueued);
    }
    upper.fWclPhysicalScanLifecycle.tearingDown = true;
    upper.noteRadioReady(&valid); assert(!upper.fWclPhysicalScanLifecycle.radioReadyQueued);
    AirportItlwm legacy;
    legacy.armDeferredPowerOnAvailability();
    assert(legacy.noteRadioScanReadyAndQueuePowerOnAvailability());
    assert(legacy.onCarriers == 1); // explicitly retained legacy IWN consumer
}
int main() {
    static_assert(sizeof(ItlRadioReadyV1) == 32, "ready value ABI");
    tagged_ready_and_completed_scan(); replacement_captured_at_unlock();
    queued_old_ready_cannot_pop_new(); bootstrap_and_failures(); malformed_and_lifecycle();
    newer_receipt_replaces_queued_reset();
    coalesced_census_follows_availability();
    reentrant_role_publication_preserves_successor();
    assert(!lockDepth && !lowerContext);
    std::puts("MVM radio ready: PASS (real lease, immutable receipt, complete producer/validator/mailbox/action/carrier, completed scan, replacement, bootstrap, failure and gate/drain fences)");
}
