// Hardware, IOKit queues, and readiness sleep are explicit doubles. Execute
// full production CSR check, HAL admission, enableAdapter, and POWER core.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <functional>
#include <HAL/ItlRadioPowerOnFailureV1.h>

using IOReturn = int32_t;
using UInt32 = uint32_t;
using IOInterruptState = unsigned;
struct IOSimpleLock { bool held = false; };
static IOInterruptState IOSimpleLockLockDisableInterrupt(IOSimpleLock *lock) {
    assert(!lock->held); lock->held = true; return 1;
}
static void IOSimpleLockUnlockEnableInterrupt(IOSimpleLock *lock, IOInterruptState irq) {
    assert(irq == 1 && lock->held); lock->held = false;
}
struct IONetworkInterface {};
constexpr IOReturn kIOReturnSuccess = 0;
constexpr IOReturn kIOReturnNotReady = static_cast<IOReturn>(0xe00002d8);
constexpr IOReturn kIOReturnTimeout = static_cast<IOReturn>(0xe00002d6);
constexpr IOReturn kIOReturnBadArgument = static_cast<IOReturn>(0xe00002c7);
constexpr IOReturn kIOReturnAborted = static_cast<IOReturn>(0xe00002eb);
constexpr uint8_t kWiFiPowerOff = 0, kWiFiPowerOn = 1, kWiFiPowerStandby = 4;
constexpr uint32_t kAirportItlwmPowerOnReadyTimeoutMs = 15000;
constexpr int kWatchDogTimerPeriod = 1000;
constexpr uint32_t MVM_FLAG_RFKILL = 2, MVM_FLAG_SHUTDOWN = 0x100;
constexpr uint32_t MVM_CSR_GP_CNTRL = 0x024;
constexpr uint32_t MVM_CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW = 1U << 27;
struct mvm_softc {
    struct ieee80211com {
        void (*ic_event_handler)(ieee80211com *, int, void *) = nullptr;
    } sc_ic;
    uint32_t sc_flags = 0;
    uint32_t csr = MVM_CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW;
    unsigned reads = 0;
    uint8_t init_retry_count = 0;
};
constexpr int IEEE80211_EVT_RADIO_POWER_ON_FAILED = 27;
static uint32_t read_csr(mvm_softc *sc, uint32_t offset) {
    assert(offset == MVM_CSR_GP_CNTRL);
    ++sc->reads;
    return sc->csr;
}
#define MVM_READ(sc, offset) read_csr(sc, offset)
static int splnet() { return 0; }
static void splx(int) {}
#define XYLog(...) do {} while (0)
#define RT_SET(value) do {} while (0)
#define __IO80211_TARGET 260000
#define __MAC_26_0 260000
struct RuntimeStats { unsigned enableCnt = 0; IOReturn lastEnableRet = 0; };
static RuntimeStats sRT;
struct Queue { unsigned enables = 0; void enable() { ++enables; } };
struct Watchdog {
    unsigned arms = 0, enables = 0;
    void setTimeoutMS(int) { ++arms; }
    void enable() { ++enables; }
};
class ItlMvm {
public:
    mvm_softc com;
    IOSimpleLock lowerLock;
    IOSimpleLock *wclScanLock = &lowerLock;
    unsigned enableCalls = 0;
    IOReturn enableResult = kIOReturnSuccess;
    uint64_t radioPowerOnEpoch = 0;
    uint64_t radioReadyReceiptSerial = 0, radioReadyRequestEpoch = 0;
    uint32_t radioReadyBackendGeneration = 0;
    int mvm_check_rfkill(mvm_softc *);
    IOReturn checkRadioPowerOnAdmission();
    IOReturn enableForRadioPowerOn(IONetworkInterface *, uint64_t);
    void cancelRadioPowerOnRequest(uint64_t);
    uint64_t radioPowerOnRequestEpoch() const;
    void reportRadioPowerOnFailure(uint64_t, IOReturn, uint32_t, int);
    IOReturn enable(IONetworkInterface *) { ++enableCalls; return enableResult; }
};
#include "hal.inc"
enum {
    kAirportItlwmDeferredPowerAvailabilityCancelEpoch = 4,
};
class AirportItlwm {
public:
    ItlMvm hal;
    ItlMvm *fHalService = &hal;
    Queue txComp, rx, tx;
    Queue *fTxCompQueue = &txComp, *fRxQueue = &rx, *fTxQueue = &tx;
    Watchdog timer;
    Watchdog *watchdogTimer = &timer;
    bool fWatchdogStopping = false, admitted = true;
    uint8_t power_state = kWiFiPowerOff;
    uint64_t nextEpoch = 0, pendingEpoch = 0;
    unsigned arms = 0, waits = 0, cancellations = 0, offCarriers = 0, disables = 0;
    IOReturn waitResult = kIOReturnSuccess;
    std::function<void()> waitHook;
    IOReturn enableAdapter(IONetworkInterface *, uint64_t = 0);
    bool retireFailedRadioPowerOn(uint64_t, IONetworkInterface *);
    int handlePowerStateChangeCore(uint32_t, IONetworkInterface *);
    uint64_t armDeferredPowerOnAvailability() {
        ++arms; pendingEpoch = ++nextEpoch; return pendingEpoch;
    }
    IOReturn waitForDeferredPowerOnAvailability(uint64_t epoch, uint32_t timeout) {
        assert(epoch == pendingEpoch && timeout == kAirportItlwmPowerOnReadyTimeoutMs);
        ++waits;
        if (waitHook) {
            auto hook = waitHook; waitHook = {}; hook();
            return kIOReturnAborted;
        }
        if (waitResult == kIOReturnSuccess) pendingEpoch = 0;
        return waitResult;
    }
    IOReturn publishDeferredPowerAvailabilityGated(AirportItlwm *target,
        void *action, void *epoch, void *, void *) {
        assert(target == this);
        assert(reinterpret_cast<uintptr_t>(action) == kAirportItlwmDeferredPowerAvailabilityCancelEpoch);
        if (reinterpret_cast<uintptr_t>(epoch) != pendingEpoch)
            return kIOReturnAborted;
        ++cancellations; pendingEpoch = 0; return kIOReturnSuccess;
    }
    void publishDeferredPowerOffAvailability() { ++offCarriers; pendingEpoch = 0; }
    void disableAdapterCore(IONetworkInterface *) {
        ++disables; hal.radioPowerOnEpoch = 0;
    }
};
struct AirportItlwmControllerLifecycleOperationGuard {
    AirportItlwm *self;
    AirportItlwmControllerLifecycleOperationGuard(AirportItlwm *that, bool) : self(that) {}
    bool admitted() const { return self->admitted; }
};
#include "controller.inc"

static void blocked_transitions() {
    for (uint8_t oldState : {kWiFiPowerOff, kWiFiPowerStandby}) {
        AirportItlwm driver;
        driver.power_state = oldState;
        driver.hal.com.csr = 0;
        for (unsigned repeat = 0; repeat != 4; ++repeat) {
            assert(static_cast<IOReturn>(driver.handlePowerStateChangeCore(kWiFiPowerOn, nullptr)) == kIOReturnNotReady);
            assert(driver.power_state == oldState);
            assert(driver.arms == 0 && driver.waits == 0 && driver.cancellations == 0);
            assert(driver.hal.enableCalls == 0 && driver.timer.arms == 0);
            assert(driver.tx.enables == 0 && driver.rx.enables == 0);
            assert(driver.hal.com.sc_flags & MVM_FLAG_RFKILL);
        }
        assert(driver.hal.com.reads == 4);
    }
    AirportItlwm standby;
    standby.hal.com.csr = 0;
    assert(static_cast<IOReturn>(standby.handlePowerStateChangeCore(kWiFiPowerStandby, nullptr)) == kIOReturnNotReady);
    assert(standby.power_state == kWiFiPowerOff && standby.waits == 0);
}
static void unblock_and_reblock() {
    AirportItlwm driver;
    driver.hal.com.csr = 0;
    assert(static_cast<IOReturn>(driver.handlePowerStateChangeCore(kWiFiPowerOn, nullptr)) == kIOReturnNotReady);
    driver.hal.com.csr = MVM_CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW;
    assert(driver.handlePowerStateChangeCore(kWiFiPowerOn, nullptr) == 0);
    assert(driver.power_state == kWiFiPowerOn && driver.arms == 1 && driver.waits == 1);
    assert((driver.hal.com.sc_flags & MVM_FLAG_RFKILL) == 0);
    assert(driver.handlePowerStateChangeCore(kWiFiPowerOff, nullptr) == 0);
    assert(driver.offCarriers == 1 && driver.disables == 1);
    driver.hal.com.csr = 0;
    assert(static_cast<IOReturn>(driver.handlePowerStateChangeCore(kWiFiPowerOn, nullptr)) == kIOReturnNotReady);
    assert(driver.power_state == kWiFiPowerOff && driver.waits == 1);
}
static void lower_result_and_timeout() {
    AirportItlwm failed;
    failed.hal.enableResult = kIOReturnAborted;
    assert(static_cast<IOReturn>(failed.handlePowerStateChangeCore(kWiFiPowerOn, nullptr)) == kIOReturnAborted);
    assert(failed.power_state == kWiFiPowerOff && failed.arms == 1 && failed.waits == 0);
    assert(failed.cancellations == 1 && failed.pendingEpoch == 0);
    assert(failed.tx.enables == 0 && failed.rx.enables == 0 && failed.txComp.enables == 0);
    assert(failed.timer.arms == 0 && sRT.lastEnableRet == kIOReturnAborted);

    AirportItlwm timeout;
    timeout.waitResult = kIOReturnTimeout;
    assert(static_cast<IOReturn>(timeout.handlePowerStateChangeCore(kWiFiPowerOn, nullptr)) == kIOReturnTimeout);
    assert(timeout.waits == 1 && timeout.cancellations == 1 && timeout.power_state == kWiFiPowerOff);
}
static void boot_and_nonstarting_transitions() {
    // Bootstrap is firmware discovery, not a public radio-on request. A
    // temporary RF_KILL must not turn its accepted enable into BootFailure.
    AirportItlwm boot;
    boot.hal.com.csr = 0;
    assert(boot.enableAdapter(nullptr) == kIOReturnSuccess);
    assert(boot.hal.enableCalls == 1 && boot.hal.com.reads == 0);
    assert(boot.tx.enables == 1 && boot.rx.enables == 1 && boot.txComp.enables == 1);
    for (const auto oldState : {kWiFiPowerOn, kWiFiPowerStandby}) {
        AirportItlwm off;
        off.power_state = oldState;
        off.hal.com.csr = 0;
        assert(off.handlePowerStateChangeCore(kWiFiPowerOff, nullptr) == 0);
        assert(off.hal.com.reads == 0 && off.offCarriers == 1 && off.disables == 1);
    }
    AirportItlwm nullHal;
    nullHal.fHalService = nullptr;
    assert(static_cast<IOReturn>(nullHal.handlePowerStateChangeCore(kWiFiPowerOn, nullptr)) == kIOReturnNotReady);
    assert(nullHal.arms == 0 && nullHal.power_state == kWiFiPowerOff);
    ItlMvm shutdown;
    shutdown.com.sc_flags = MVM_FLAG_SHUTDOWN;
    assert(shutdown.checkRadioPowerOnAdmission() == kIOReturnNotReady && shutdown.com.reads == 0);
    AirportItlwm invalid;
    invalid.hal.com.csr = 0;
    assert(static_cast<IOReturn>(invalid.handlePowerStateChangeCore(99, nullptr)) == kIOReturnBadArgument);
    assert(invalid.power_state == kWiFiPowerOff && invalid.hal.com.reads == 0);
    assert(invalid.handlePowerStateChangeCore(kWiFiPowerOff, nullptr) == 0);
    assert(invalid.hal.com.reads == 0 && invalid.offCarriers == 0);
}
static void superseded_wait_does_not_rollback_successor() {
    AirportItlwm driver;
    driver.waitHook = [&] {
        if (driver.hal.radioPowerOnRequestEpoch() != 0)
            assert(driver.hal.radioPowerOnRequestEpoch() == 1);
        assert(driver.handlePowerStateChangeCore(kWiFiPowerOff, nullptr) == 0);
        assert(driver.handlePowerStateChangeCore(kWiFiPowerOn, nullptr) == 0);
        assert(driver.power_state == kWiFiPowerOn);
    };
    assert(static_cast<IOReturn>(driver.handlePowerStateChangeCore(kWiFiPowerOn, nullptr)) == kIOReturnAborted);
    assert(driver.power_state == kWiFiPowerOn);
    assert(driver.hal.radioPowerOnRequestEpoch() == 2);
    assert(driver.disables == 1 && driver.offCarriers == 1 && driver.cancellations == 0);
    AirportItlwm failed;
    failed.waitResult = kIOReturnTimeout;
    assert(static_cast<IOReturn>(failed.handlePowerStateChangeCore(kWiFiPowerOn, nullptr)) == kIOReturnTimeout);
    assert(failed.disables == 1 && failed.hal.radioPowerOnRequestEpoch() == 0);
    failed.waitResult = kIOReturnSuccess;
    assert(failed.handlePowerStateChangeCore(kWiFiPowerOn, nullptr) == 0);
    assert(failed.power_state == kWiFiPowerOn && failed.arms == 2);
}
int main() {
    blocked_transitions();
    unblock_and_reblock();
    lower_result_and_timeout();
    boot_and_nonstarting_transitions();
    superseded_wait_does_not_rollback_successor();
    std::puts("MVM radio power admission: PASS (fresh CSR, repeated refusal, unblock/reblock, lower error, timeout retirement/retry, superseded rollback, bootstrap, Off, detach)");
}
