// Complete lower lifecycle functions; real scan lease/ready validator.
// Firmware, task queues, net80211 soft-state and IOKit locks are doubles.
#include <HAL/ItlRadioReadyV1.h>
#include <HAL/ItlRadioPowerOnFailureV1.h>
#include <HAL/ItlScanCommandLease.hpp>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#define KASSERT(condition, message) assert(condition)
#define XYLog(...) ((void)0)
#define DEVNAME(sc) "iwm-lifecycle-test"
#define nitems(array) int(sizeof(array) / sizeof((array)[0]))
#define SEC_TO_NSEC(seconds) (uint64_t(seconds) * 1000000000ULL)
#define container_of(pointer, type, member) reinterpret_cast<type *>(pointer)
#define IEEE80211_ADDR_COPY(dst, src) std::memcpy(dst, src, 6)
constexpr unsigned IFF_UP=1, IFF_RUNNING=2;
constexpr uint32_t IWM_FLAG_RFKILL=2, IWM_FLAG_HW_ERR=0x80,
    IWM_FLAG_SHUTDOWN=0x100, IWM_FLAG_SCANNING=0x200, IWM_FLAG_BGSCAN=0x400,
    IWM_FLAG_MAC_ACTIVE=0x800, IWM_FLAG_BINDING_ACTIVE=0x1000,
    IWM_FLAG_STA_ACTIVE=0x2000, IWM_FLAG_TE_ACTIVE=0x4000;
constexpr int IEEE80211_M_MONITOR=1, IEEE80211_S_INIT=0,
    IEEE80211_S_SCAN=1, IEEE80211_S_RUN=3, IEEE80211_CHAN_WIDTH_20_NOHT=0;
[[maybe_unused]] constexpr int DVACT_QUIESCE=1, DVACT_RESUME=2, DVACT_WAKEUP=3,
    PCATCH=0, THREAD_UNINT=0, THREAD_INTERRUPTIBLE=1;
using IOReturn=int;
[[maybe_unused]] constexpr int kIOReturnSuccess=0, kIOReturnNotReady=-1,
    kIOReturnIOError=-2;
constexpr int IEEE80211_EVT_RADIO_POWER_ON_FAILED=27;
struct IONetworkInterface {};
using IOInterruptState=unsigned;
struct IOSimpleLock { std::mutex mutex; };
struct IOLock { std::mutex mutex; std::condition_variable cv; };
static std::mutex stageMutex;
static std::condition_variable stageCv;
static bool hardwareEntered=false, releaseHardware=false, offFinished=false,
    stopIntent=false, initFinished=false, resetEntered=false, releaseReset=false;
static unsigned drainWaits=0;
static std::function<void()> sleepHook;
static void IOLockLock(IOLock *lock) { lock->mutex.lock(); }
static void IOLockUnlock(IOLock *lock) { lock->mutex.unlock(); }
static void IOLockWakeup(IOLock *lock, void *, bool) { lock->cv.notify_all(); }
static void IOLockSleep(IOLock *lock, void *, int) {
    { std::lock_guard<std::mutex> guard(stageMutex); ++drainWaits; stageCv.notify_all(); }
    std::unique_lock<std::mutex> guard(lock->mutex, std::adopt_lock);
    lock->cv.wait(guard); guard.release();
}
static IOInterruptState IOSimpleLockLockDisableInterrupt(IOSimpleLock *lock) {
    lock->mutex.lock(); return 1;
}
static void IOSimpleLockUnlockEnableInterrupt(IOSimpleLock *lock, IOInterruptState irq) {
    assert(irq==1); lock->mutex.unlock();
}
struct Queue {};
struct _ifnet { void *if_softc=nullptr; unsigned if_flags=IFF_UP; Queue if_snd; int if_timer=0; };
struct ieee80211_node { int ni_chan=0, ni_chw=0; };
struct iwm_node { ieee80211_node in_ni; void *in_phyctxt=nullptr; uint8_t in_macaddr[6]={}; };
struct ieee80211com {
    union { _ifnet ic_if; struct { _ifnet ac_if; } ic_ac; };
    iwm_node node; ieee80211_node *ic_bss=&node.in_ni;
    int ic_state=IEEE80211_S_INIT, ic_opmode=0, ic_ibss_chan=1;
    unsigned ic_initial_scan_census_only=0;
    unsigned failureEvents=0;
    ItlRadioPowerOnFailureV1 lastFailure{};
    void (*ic_event_handler)(ieee80211com *, int, void *)=nullptr;
    ieee80211com() : ic_if{} {}
};
struct task {};
struct taskq {};
static taskq queue;
static taskq *systq=&queue;
struct iwm_rxba_data {};
struct iwm_softc {
    ieee80211com sc_ic;
    int sc_generation=10;
    uint32_t sc_flags=0;
    unsigned agg_tid_disable=0, agg_queue_mask=0;
    struct { void *wn=nullptr; } sc_tx_ba[4];
    struct { unsigned refs=0; } task_refs;
    struct { bool sku_cap_11n_enable=true, sku_cap_11ac_enable=true; } sc_nvm;
    uint8_t init_retry_count=4;
    uint8_t *sc_cmd_resp_pkt[4]={}; size_t sc_cmd_resp_len[4]={};
    uint32_t sc_scan_abort_pending=0;
    int sc_rx_ba_sessions=0;
    struct { unsigned start_tidmask=0, stop_tidmask=0; } ba_rx, ba_tx;
    int ns_nstate=IEEE80211_S_INIT, sc_calib_to=0, sc_tx_timer[4]={};
    iwm_rxba_data sc_rxba_data[4];
    task init_task, newstate_task, ap_client_task, assoc_comeback_task,
        ba_task, mac_ctxt_task, chan_ctxt_task;
    taskq *sc_nswq=&queue;
    IOLock lifeLock; IOLock *sc_sae_tx_lifecycle_lock=&lifeLock;
    unsigned sc_sae_tx_lifecycle_active=0, sc_radio_init_refs=0, sc_radio_stop_refs=0;
    bool sc_sae_tx_detaching=false;
    static int newstate(ieee80211com *ic, int state, int) { ic->ic_state=state; return 0; }
    int (*sc_newstate)(ieee80211com *, int, int)=newstate;
};
struct Resettable { void clear() {} void close() {} };
struct ItlFirmwareStationRetirement {};
struct ItlRxBaSessionCount { static void reset(int *) {} };
static const uint8_t etheranyaddr[6]={};
static int splnet() { return 1; }
static void splx(int) {}
static int task_del(taskq *, task *) { return 1; }
static unsigned taskAdds=0;
static int task_add(taskq *, task *) { ++taskAdds; return 1; }
static void timeout_del(int *) {}
static void ifq_clr_oactive(Queue *) {}
static void ifq_flush(Queue *) {}
static void ieee80211_pae_assoc_epoch_begin(ieee80211com *) {}
static void itl_ap_firmware_runtime_reset(Resettable *, bool) {}
constexpr int kItlApFirmwareResourceIdle=0;
class ItlIwm;
static ItlIwm *active=nullptr;
static void ieee80211_begin_scan(_ifnet *);
static void ieee80211_new_state(ieee80211com *ic, int state, int) { ic->ic_state=state; }
class ItlIwm {
public:
    iwm_softc com;
    IOSimpleLock scanLock; IOSimpleLock *wclScanLock=&scanLock;
    ItlScanCommandLease scanCommand{};
    uint64_t radioPowerOnEpoch=42, radioReadyReceiptSerial=0, radioReadyRequestEpoch=0;
    uint32_t radioReadyBackendGeneration=0;
    bool apCsaTimerInitialized=false;
    int apCsaTimeout=0;
    struct ApRuntime : Resettable { int stage=kItlApFirmwareResourceIdle; } apRuntime;
    Resettable primaryMacContext, primaryBindingContext, primaryStationContext, primaryStationUses;
    ItlFirmwareStationRetirement primaryStationRetirement;
    int primaryStationCommand=0, primaryMacCommand=0;
    bool apPrimaryStaRecoveryScanAbortPending=false, apPrimaryStaRecoveryScanYielded=false,
        apPrimaryStaRecoveryScanGeneric=false;
    unsigned apPrimaryStaRecoveryScanGeneration=0;
    bool hardwareLive=false, pauseHardware=true, failHardware=false,
        emitReady=true, pauseReset=false;
    int hardwareStarts=0, hardwareStops=0, scans=0, readyEvents=0;
    int hardwareError=0;
    int sc_rx_ba_sessions=0;
    ItlIwm() {
        com.sc_ic.ic_if.if_softc=&com; active=this;
        com.sc_ic.ic_event_handler=[](ieee80211com *ic, int code, void *payload) {
            assert(code==IEEE80211_EVT_RADIO_POWER_ON_FAILED);
            ++ic->failureEvents;
            ic->lastFailure=*static_cast<ItlRadioPowerOnFailureV1 *>(payload);
        };
    }
    IOReturn disable(IONetworkInterface *);
    IOReturn enable(IONetworkInterface *);
    int iwm_activate(iwm_softc *, int);
#if IWM_RADIO_INIT_STOP_HISTORICAL
    int iwm_init(_ifnet *);
#else
    int iwm_init(_ifnet *, bool *owner_admitted = nullptr);
#endif
    static void iwm_init_task(void *);
    void iwm_stop(_ifnet *);
    void iwm_stop_internal(_ifnet *, bool);
    bool iwm_radio_init_begin(iwm_softc *, int *);
    bool iwm_radio_init_current(iwm_softc *, int);
    bool iwm_radio_init_current_locked(iwm_softc *, int);
    void iwm_radio_init_end(iwm_softc *);
    bool iwm_radio_stop_begin(iwm_softc *, int *);
    void iwm_radio_stop_drain(iwm_softc *, unsigned, unsigned);
    void iwm_radio_stop_end(iwm_softc *, int);
    uint64_t scanCommandResetEpoch();
    bool reopenScanCommands(uint64_t, uint32_t);
    bool isRadioScanReady(uint32_t);
    bool isRadioReadyCurrent(const ItlRadioReadyV1 *);
    uint64_t radioPowerOnRequestEpoch() const;
    void cancelRadioPowerOnRequest(uint64_t);
    uint8_t claimRadioPowerOnRetry(uint64_t);
    void reportRadioPowerOnFailure(uint64_t, IOReturn, uint32_t, int);
    int iwm_init_hw(iwm_softc *) {
        ++hardwareStarts; hardwareLive=true;
        if (pauseHardware) {
            std::unique_lock<std::mutex> guard(stageMutex);
            hardwareEntered=true; stageCv.notify_all();
            stageCv.wait(guard, [] { return releaseHardware; });
        }
        return hardwareError ? hardwareError : failHardware ? EIO : 0;
    }
    void iwm_stop_device(iwm_softc *) {
        ++hardwareStops;
        if (pauseReset) {
            std::unique_lock<std::mutex> guard(stageMutex);
            resetEntered=true; stageCv.notify_all();
            stageCv.wait(guard, [] { return releaseReset; });
        }
        hardwareLive=false;
    }
    void invalidateWclScanForReset() {
        IOInterruptState irq=IOSimpleLockLockDisableInterrupt(wclScanLock);
        scanCommand.invalidate(); radioReadyReceiptSerial=0;
        radioReadyRequestEpoch=0; radioReadyBackendGeneration=0;
        IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
        std::lock_guard<std::mutex> guard(stageMutex);
        stopIntent=true; stageCv.notify_all();
    }
    void wakeupOn(void *) {}
    int tsleep_nsec(void *, int, const char *, uint64_t) {
        if (sleepHook) { auto hook=sleepHook; sleepHook={}; hook(); }
        return EWOULDBLOCK;
    }
    void iwm_setup_ht_rates(iwm_softc *) {}
    void iwm_setup_vht_rates(iwm_softc *) {}
    void iwm_mfp_pae_reopen(iwm_softc *, int = 0) {}
    void iwm_sae_tx_reopen(iwm_softc *, int = 0) {}
    void iwm_sae_engine_reopen(iwm_softc *, int = 0) {}
    bool iwm_sae_driver_reset_recovery_pending(iwm_softc *, bool) { return false; }
    void iwm_sae_driver_reset_recovery_prepare(iwm_softc *) {}
    int iwm_stop_ap_resources(iwm_softc *, ApRuntime *, bool) { return 0; }
    int iwm_resume(iwm_softc *) { return 0; }
    bool iwm_set_hw_ready(iwm_softc *) { return true; }
    void iwm_del_task(iwm_softc *, taskq *, task *) {}
    void iwm_assoc_comeback_cancel(iwm_softc *) {}
    void resetPrimaryRxBaLocked() {}
    void iwm_clear_reorder_buffer(iwm_softc *, iwm_rxba_data *) {}
    void iwm_led_blink_stop(iwm_softc *) {}
};
static void ieee80211_begin_scan(_ifnet *) {
    ++active->scans;
    active->com.sc_ic.ic_state=IEEE80211_S_SCAN;
    if (!active->emitReady) return;
    active->radioReadyReceiptSerial=51;
    active->radioReadyRequestEpoch=active->radioPowerOnEpoch;
    active->radioReadyBackendGeneration=active->com.sc_generation;
    ++active->readyEvents;
}

#include "lifecycle.inc"

int main(int argc, char **argv) {
    assert(argc==2);
    const std::string scenario=argv[1];
    ItlIwm driver;
    int initResult=-1;
    if (scenario=="early-off" || scenario=="early-off-on") {
        std::thread initializer([&] {
            initResult=driver.iwm_init(&driver.com.sc_ic.ic_if);
            std::lock_guard<std::mutex> guard(stageMutex);
            initFinished=true; stageCv.notify_all();
        });
        {
            std::unique_lock<std::mutex> guard(stageMutex);
            assert(stageCv.wait_for(guard, std::chrono::seconds(3), [] { return hardwareEntered; }));
        }
        assert(!(driver.com.sc_ic.ic_if.if_flags & IFF_RUNNING));
        std::thread off([&] {
            assert(driver.disable(nullptr)==kIOReturnSuccess);
            std::lock_guard<std::mutex> guard(stageMutex);
            offFinished=true; stageCv.notify_all();
        });
        bool prematureOff;
        {
            std::unique_lock<std::mutex> guard(stageMutex);
            assert(stageCv.wait_for(guard, std::chrono::seconds(3), [] { return offFinished || stopIntent; }));
            prematureOff=offFinished;
        }
        // Old code returns Off without an actual reset; then replacement On
        // can restore UP and the old firmware publishes that request's ready.
        if (prematureOff && scenario=="early-off-on") {
            assert(driver.enable(nullptr)==0); driver.radioPowerOnEpoch=84;
        }
        {
            std::lock_guard<std::mutex> guard(stageMutex);
            releaseHardware=true; stageCv.notify_all();
        }
        initializer.join(); off.join();
        std::fprintf(stderr, "early Off result=%d premature=%d hardwareLive=%d stops=%d scans=%d readyEpoch=%llu\n",
            initResult, prematureOff, driver.hardwareLive, driver.hardwareStops,
            driver.scans, static_cast<unsigned long long>(driver.radioReadyRequestEpoch));
        assert(!prematureOff);
        assert(initResult==ENXIO && !driver.hardwareLive && driver.hardwareStops==1);
        assert(driver.scans==0 && driver.readyEvents==0 && !driver.scanCommand.open);
        assert(!(driver.com.sc_ic.ic_if.if_flags & (IFF_UP | IFF_RUNNING)));
        assert(driver.com.sc_radio_init_refs==0 && driver.com.sc_radio_stop_refs==0 &&
            driver.com.sc_sae_tx_lifecycle_active==0);
        if (scenario=="early-off-on") {
            assert(driver.enable(nullptr)==0); driver.radioPowerOnEpoch=84;
            driver.pauseHardware=false;
            assert(driver.iwm_init(&driver.com.sc_ic.ic_if)==0);
            assert(driver.hardwareLive && driver.scans==1 && driver.readyEvents==1 &&
                driver.radioReadyRequestEpoch==84);
            assert(driver.disable(nullptr)==0 && !driver.hardwareLive);
        }
    } else if (scenario=="init-owner-retry") {
        driver.com.init_retry_count=0;
        std::thread initializer([&] {
            initResult=driver.iwm_init(&driver.com.sc_ic.ic_if);
        });
        {
            std::unique_lock<std::mutex> guard(stageMutex);
            assert(stageCv.wait_for(guard, std::chrono::seconds(3),
                [] { return hardwareEntered; }));
        }
        // The complete worker and real init admission run five times while
        // the original full init retains its owner inside the firmware double.
        // A refused second owner is not a completed firmware attempt.
        const unsigned addsBefore=taskAdds;
        for (unsigned collision=0; collision<5; ++collision)
            driver.iwm_init_task(&driver.com);
        const unsigned retries=driver.com.init_retry_count;
        const unsigned failures=driver.com.sc_ic.failureEvents;
        const unsigned adds=taskAdds-addsBefore;
        const uint64_t requestEpoch=driver.radioPowerOnRequestEpoch();
        const auto failure=driver.com.sc_ic.lastFailure;
        {
            std::lock_guard<std::mutex> guard(stageMutex);
            releaseHardware=true; stageCv.notify_all();
        }
        initializer.join();
        std::fprintf(stderr,
            "init-owner collision hardwareStarts=%d retries=%u failureEvents=%u "
            "requeues=%u requestEpoch=%llu failureReason=%u lowerError=%d "
            "originalResult=%d readyEpoch=%llu\n",
            driver.hardwareStarts, retries, failures, adds,
            static_cast<unsigned long long>(requestEpoch), failure.reason,
            failure.lowerError, initResult,
            static_cast<unsigned long long>(driver.radioReadyRequestEpoch));
        assert(driver.hardwareStarts==1 && retries==0 && failures==0 &&
            adds==0 && requestEpoch==42);
        assert(initResult==0 && driver.readyEvents==1 &&
            driver.radioReadyRequestEpoch==42 && driver.hardwareLive);
        assert(driver.com.sc_radio_init_refs==0 &&
            driver.com.sc_sae_tx_lifecycle_active==0);
        assert(driver.disable(nullptr)==0 && !driver.hardwareLive);
    } else if (scenario=="worker-five-failures" || scenario=="worker-five-enxio" ||
        scenario=="worker-eventual-success") {
        driver.pauseHardware=false; driver.com.init_retry_count=0;
        driver.hardwareError=scenario=="worker-five-enxio" ? ENXIO : EIO;
        const unsigned failures=scenario=="worker-eventual-success" ? 2 : 5;
        for (unsigned attempt=1; attempt<=failures; ++attempt) {
            driver.iwm_init_task(&driver.com);
            assert(driver.hardwareStarts==int(attempt) &&
                driver.com.init_retry_count==attempt && !driver.hardwareLive);
            assert(driver.com.sc_ic.failureEvents==(attempt==5 ? 1U : 0U));
        }
        if (scenario=="worker-eventual-success") {
            driver.hardwareError=0; driver.iwm_init_task(&driver.com);
            assert(driver.hardwareStarts==3 && driver.com.init_retry_count==0 &&
                driver.com.sc_ic.failureEvents==0 && driver.radioReadyRequestEpoch==42 &&
                driver.radioPowerOnRequestEpoch()==0 && driver.hardwareLive);
            assert(driver.disable(nullptr)==0 && !driver.hardwareLive);
        } else {
            assert(driver.com.sc_ic.lastFailure.reason==kItlRadioPowerOnFailureRecoveryExhausted &&
                driver.com.sc_ic.lastFailure.lowerError==driver.hardwareError &&
                driver.radioPowerOnRequestEpoch()==0 && taskAdds==4);
        }
    } else if (scenario=="overlapping-off") {
        driver.pauseHardware=false;
        assert(driver.iwm_init(&driver.com.sc_ic.ic_if)==0);
        driver.pauseReset=true;
        std::thread first([&] { assert(driver.disable(nullptr)==0); });
        {
            std::unique_lock<std::mutex> guard(stageMutex);
            assert(stageCv.wait_for(guard, std::chrono::seconds(3), [] { return resetEntered; }));
        }
        std::thread second([&] {
            assert(driver.disable(nullptr)==0);
            std::lock_guard<std::mutex> guard(stageMutex);
            offFinished=true; stageCv.notify_all();
        });
        {
            std::unique_lock<std::mutex> guard(stageMutex);
            assert(stageCv.wait_for(guard, std::chrono::seconds(3), [] { return drainWaits!=0; }));
            assert(!offFinished);
            releaseReset=true; stageCv.notify_all();
        }
        first.join(); second.join();
        assert(!driver.hardwareLive && driver.hardwareStops==1);
        assert(driver.com.sc_radio_init_refs==0 && driver.com.sc_radio_stop_refs==0);
    } else if (scenario=="admission") {
        int generation=0;
        driver.com.sc_ic.ic_if.if_flags=0;
        assert(!driver.iwm_radio_init_begin(&driver.com, &generation));
        driver.com.sc_ic.ic_if.if_flags=IFF_UP;
        driver.com.sc_sae_tx_detaching=true;
        assert(!driver.iwm_radio_init_begin(&driver.com, &generation));
        driver.com.sc_sae_tx_detaching=false;
        driver.com.sc_flags=IWM_FLAG_SHUTDOWN;
        assert(!driver.iwm_radio_init_begin(&driver.com, &generation));
        assert(driver.enable(nullptr)==kIOReturnNotReady);
        driver.com.sc_flags=0;
        assert(driver.iwm_radio_init_begin(&driver.com, &generation));
        int duplicate=0;
        assert(!driver.iwm_radio_init_begin(&driver.com, &duplicate));
        assert(driver.com.sc_generation==generation);
        assert(driver.iwm_radio_init_current(&driver.com, generation));
        driver.com.sc_sae_tx_detaching=true;
        assert(!driver.iwm_radio_init_current(&driver.com, generation));
        driver.iwm_radio_init_end(&driver.com);
        assert(driver.com.sc_sae_tx_lifecycle_active==0);
    } else if (scenario=="normal" || scenario=="timeout" || scenario=="hardware-failure") {
        driver.pauseHardware=false;
        driver.failHardware=scenario=="hardware-failure";
        driver.emitReady=scenario!="timeout";
        initResult=driver.iwm_init(&driver.com.sc_ic.ic_if);
        assert(initResult==(driver.failHardware ? EIO : scenario=="timeout" ? EWOULDBLOCK : 0));
        assert(driver.com.sc_radio_init_refs==0 && driver.com.sc_sae_tx_lifecycle_active==0);
        if (scenario=="timeout") assert(!driver.hardwareLive && driver.hardwareStops==1);
        if (scenario=="normal") assert(driver.disable(nullptr)==0 && !driver.hardwareLive);
    } else return 2;
    std::printf("IWM full init/stop %s: PASS\n", argv[1]);
}
