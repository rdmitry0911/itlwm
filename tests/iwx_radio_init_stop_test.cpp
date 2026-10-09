// Full real init/stop/disable/activate, task gate, q0 start/stop, ready getter.
// Hardware, task barriers, net80211 soft state and IOKit primitives are doubles.
#include <HAL/ItlRadioReadyV1.h>
#include <HAL/ItlScanCommandLease.hpp>
#include <HAL/ItlSaeAuthTransportV1.h>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#if defined(__APPLE__)
// Userland Darwin lacks the kernel explicit_bzero primitive used below.
static void explicit_bzero(void *memory, size_t length) {
    volatile uint8_t *bytes=static_cast<volatile uint8_t *>(memory);
    while (length--) *bytes++=0;
}
#endif

#define KASSERT(condition, message) assert(condition)
#define XYLog(...) ((void)0)
#define nitems(array) int(sizeof(array) / sizeof((array)[0]))
#define ARRAY_SIZE(array) nitems(array)
#define SEC_TO_NSEC(seconds) (uint64_t(seconds) * 1000000000ULL)
#define container_of(pointer, type, member) reinterpret_cast<type *>(pointer)
#define IEEE80211_ADDR_COPY(dst, src) std::memcpy(dst, src, 6)
constexpr unsigned IFF_UP=1, IFF_RUNNING=2;
constexpr uint32_t IWX_FLAG_RFKILL=2, IWX_FLAG_HW_ERR=0x80,
    IWX_FLAG_SHUTDOWN=0x100, IWX_FLAG_SCANNING=0x200, IWX_FLAG_BGSCAN=0x400,
    IWX_FLAG_MAC_ACTIVE=0x800, IWX_FLAG_BINDING_ACTIVE=0x1000,
    IWX_FLAG_STA_ACTIVE=0x2000, IWX_FLAG_TE_ACTIVE=0x4000, IWX_FLAG_TXFLUSH=0x8000;
constexpr int IEEE80211_M_MONITOR=1, IEEE80211_S_INIT=0, IEEE80211_S_SCAN=1,
    IEEE80211_S_RUN=3, IEEE80211_CHAN_WIDTH_20_NOHT=0;
constexpr int DVACT_QUIESCE=1, DVACT_RESUME=2, DVACT_WAKEUP=3,
    PCATCH=0, THREAD_INTERRUPTIBLE=1;
constexpr int IWX_INVALID_QUEUE=-1, IWX_DQA_CMD_QUEUE=0;
constexpr int IWX_CMD_SLOT_FREE=0, IWX_CMD_SLOT_SUBMITTED=1, IWX_CMD_SLOT_ABORTED=2,
    IWX_CMD_ASYNC_OWNER_NONE=0, IWX_CMD_ASYNC_ACK_NONE=0;
using IOReturn=int;
[[maybe_unused]] constexpr int kIOReturnSuccess=0, kIOReturnNotReady=-1;
struct IONetworkInterface {};
using IOInterruptState=unsigned;
struct IOSimpleLock { std::mutex mutex; };
struct IOLock { std::mutex mutex; std::condition_variable cv; };
static std::mutex stageMutex;
static std::condition_variable stageCv;
static bool hardwareEntered=false, releaseHardware=false, offFinished=false,
    initFinished=false, resetEntered=false, releaseReset=false;
static unsigned drainWaits=0;
static void IOLockLock(IOLock *lock) { lock->mutex.lock(); }
static void IOLockUnlock(IOLock *lock) { lock->mutex.unlock(); }
static void IOLockWakeup(IOLock *lock, void *, bool) { lock->cv.notify_all(); }
static void IOLockSleep(IOLock *lock, void *, int) {
    { std::lock_guard<std::mutex> guard(stageMutex); ++drainWaits; stageCv.notify_all(); }
    std::unique_lock<std::mutex> guard(lock->mutex, std::adopt_lock);
    lock->cv.wait(guard); guard.release();
}
static void IOSimpleLockLock(IOSimpleLock *lock) { lock->mutex.lock(); }
static void IOSimpleLockUnlock(IOSimpleLock *lock) { lock->mutex.unlock(); }
static IOInterruptState IOSimpleLockLockDisableInterrupt(IOSimpleLock *lock) {
    IOSimpleLockLock(lock); return 1;
}
static void IOSimpleLockUnlockEnableInterrupt(IOSimpleLock *lock, IOInterruptState irq) {
    assert(irq==1); IOSimpleLockUnlock(lock);
}
static void IOSleep(unsigned delay) { std::this_thread::sleep_for(std::chrono::milliseconds(delay)); }
struct Queue {};
struct _ifnet { void *if_softc=nullptr; unsigned if_flags=IFF_UP; Queue if_snd; int if_timer=0; };
struct ieee80211_node { int ni_chan=0, ni_chw=0; };
struct iwx_node { ieee80211_node in_ni; void *in_phyctxt=nullptr; uint8_t in_macaddr[6]={}; };
struct ieee80211com {
    union { _ifnet ic_if; struct { _ifnet ac_if; } ic_ac; };
    iwx_node node; ieee80211_node *ic_bss=&node.in_ni;
    int ic_state=IEEE80211_S_INIT, ic_opmode=0, ic_ibss_chan=1;
    unsigned ic_initial_scan_census_only=0;
    ieee80211com() : ic_if{} {}
};
struct task {};
struct taskq {};
static taskq sysQueue, stateQueue;
static taskq *systq=&sysQueue;
static int task_del(taskq *, task *) { return 1; }
static int task_add(taskq *, task *) { return 1; }
static void taskq_barrier(taskq *q) {
    if (q==systq && hardwareEntered) {
        std::unique_lock<std::mutex> guard(stageMutex);
        stageCv.wait(guard, [] { return initFinished; });
    }
}
struct iwx_rxba_data {};
struct iwx_tfh_tfd {};
struct iwx_tx_ring { iwx_tfh_tfd *desc=nullptr; unsigned ring_count=4; };
struct Slot { int state=IWX_CMD_SLOT_FREE, async_owner=0, async_ack_kind=0; uint64_t async_cookie=0; };
struct iwx_softc {
    ieee80211com sc_ic;
    int sc_generation=10;
    uint32_t sc_flags=0;
    struct { unsigned refs=1; } task_refs;
    struct { bool sku_cap_11n_enable=true, sku_cap_11ac_enable=true,
                  sku_cap_11ax_enable=true; } sc_nvm;
    struct { int qid=0; } sc_tid_data[4];
    uint8_t init_retry_count=4;
    uint8_t *sc_cmd_resp_pkt[4]={}; size_t sc_cmd_resp_len[4]={};
    uint32_t sc_scan_abort_pending=0;
    int sc_rx_ba_sessions=0;
    struct { unsigned start_tidmask=0, stop_tidmask=0; } ba_rx, ba_tx;
    int ns_nstate=IEEE80211_S_INIT, sc_tx_timer=0;
    iwx_rxba_data sc_rxba_data[4];
    task init_task, newstate_task, security_rx_task, sae_tx_task, mfp_pae_task,
        assoc_comeback_task, ap_start_task, ap_stop_task, ap_client_task,
        ba_task, mac_ctxt_task, chan_ctxt_task;
    taskq *sc_nswq=&stateQueue;
    bool sc_taskq_initialized=true, sc_task_callbacks_ready=true;
    IOLock gateLock; IOLock *sc_task_gate_lock=&gateLock;
    unsigned sc_task_gate_active=0, sc_task_gate_init_refs=0, sc_task_gate_stop_refs=0;
    bool sc_task_gate_closed=true, sc_task_gate_detaching=false,
        sc_task_gate_bootstrap_init=true;
    bool sc_assoc_comeback_queued=false;
    int sc_assoc_comeback_retry=0, sc_assoc_comeback_generation=0;
    IOSimpleLock mfpLock; IOSimpleLock *sc_mfp_pae_lock=&mfpLock;
    bool sc_mfp_pae_reset_pending=false;
    IOSimpleLock cmdLock; IOSimpleLock *sc_cmdq_lock=&cmdLock;
    uint32_t sc_cmdq_epoch=1, sc_cmdq_senders=0;
    bool sc_cmdq_stopping=true, sc_cmdq_detaching=false;
    Slot sc_cmdq_slots[4];
    iwx_tfh_tfd descriptors[4]; iwx_tx_ring txq[1];
    iwx_softc() { txq[0].desc=descriptors; }
    static int newstate(ieee80211com *ic, int state, int) { ic->ic_state=state; return 0; }
    int (*sc_newstate)(ieee80211com *, int, int)=newstate;
};
struct Resettable { void clear() {} void close() {} };
struct ItlFirmwareStationRetirement {};
struct ItlRxBaSessionCount { static void reset(int *) {} };
static const uint8_t etheranyaddr[6]={};
static int splnet() { return 1; }
static void splx(int) {}
static void timeout_del(int *) {}
static void ifq_clr_oactive(Queue *) {}
static void ifq_flush(Queue *) {}
static void ieee80211_pae_assoc_epoch_begin(ieee80211com *) {}
class ItlIwx;
static ItlIwx *active=nullptr;
static void ieee80211_begin_scan(_ifnet *);
static void ieee80211_new_state(ieee80211com *ic, int state, int) { ic->ic_state=state; }
static void iwx_ap_lifecycle_reset(ItlIwx *, bool) {}
class ItlIwx {
public:
    iwx_softc com;
    IOSimpleLock scanLock; IOSimpleLock *wclScanLock=&scanLock;
    ItlScanCommandLease scanCommand{};
    uint64_t radioPowerOnEpoch=42, radioReadyReceiptSerial=0, radioReadyRequestEpoch=0;
    uint32_t radioReadyBackendGeneration=0;
    bool apCsaTimerInitialized=false;
    int apCsaTimeout=0;
    Resettable primaryMacContext, primaryBindingContext, primaryStationContext, primaryStationUses;
    ItlFirmwareStationRetirement primaryStationRetirement;
    int primaryStationCommand=0, primaryMacCommand=0;
    bool hardwareLive=false, pauseHardware=false, failHardware=false,
        emitReady=true, pauseReset=false;
    int hardwareStarts=0, hardwareStops=0, scans=0, readyEvents=0,
        resumeCalls=0, prepareCalls=0, wakeups=0;
    std::mutex waitMutex; std::condition_variable waitCv;
    ItlIwx() { com.sc_ic.ic_if.if_softc=&com; active=this; }
    IOReturn disable(IONetworkInterface *);
    IOReturn enable(IONetworkInterface *);
    int iwx_activate(iwx_softc *, int);
    int iwx_init_internal(_ifnet *, bool);
    void iwx_stop(_ifnet *);
    void iwx_stop_internal(_ifnet *, bool, bool);
    bool iwx_task_gate_close(iwx_softc *, bool, int *);
    bool iwx_task_gate_begin_epoch(iwx_softc *, int *);
    bool iwx_task_gate_epoch_live(iwx_softc *, int);
    bool iwx_task_gate_open(iwx_softc *, int);
    void iwx_task_gate_rearm(iwx_softc *, int);
    bool iwx_task_gate_enter(iwx_softc *, bool);
    void iwx_task_gate_leave(iwx_softc *);
    void iwx_task_gate_end_epoch(iwx_softc *);
    void iwx_task_gate_drain(iwx_softc *, uint32_t, uint32_t, uint32_t);
    void iwx_bootstrap_init_task(iwx_softc *);
    bool iwx_cmdq_start(iwx_softc *, int);
    bool iwx_cmdq_enter(iwx_softc *);
    void iwx_cmdq_leave(iwx_softc *);
    void iwx_cmdq_stop(iwx_softc *);
    uint64_t scanCommandResetEpoch();
    bool reopenScanCommands(uint64_t, uint32_t);
    bool isRadioScanReady(uint32_t);
    bool isRadioReadyCurrent(const ItlRadioReadyV1 *);
    void lockTsleep() { waitMutex.lock(); }
    void unlockTsleep() { waitMutex.unlock(); }
    void wakeupOn(void *) { ++wakeups; waitCv.notify_all(); }
    int iwx_init_hw(iwx_softc *sc) {
        ++hardwareStarts; hardwareLive=true;
        if (pauseHardware) {
            assert(iwx_cmdq_enter(sc));
            lockTsleep(); IOSimpleLockLock(sc->sc_cmdq_lock);
            sc->sc_cmdq_slots[0].state=IWX_CMD_SLOT_SUBMITTED;
            sc->sc_cmdq_slots[0].async_owner=1;
            sc->sc_cmdq_slots[0].async_ack_kind=1;
            sc->sc_cmdq_slots[0].async_cookie=999;
            sc->sc_cmd_resp_pkt[0]=static_cast<uint8_t *>(std::calloc(1, 64));
            assert(sc->sc_cmd_resp_pkt[0]); sc->sc_cmd_resp_len[0]=64;
            IOSimpleLockUnlock(sc->sc_cmdq_lock);
            { std::lock_guard<std::mutex> guard(stageMutex); hardwareEntered=true; stageCv.notify_all(); }
            std::unique_lock<std::mutex> guard(waitMutex, std::adopt_lock);
            waitCv.wait(guard, [&] {
                IOSimpleLockLock(sc->sc_cmdq_lock);
                const bool aborted=sc->sc_cmdq_slots[0].state==IWX_CMD_SLOT_ABORTED;
                IOSimpleLockUnlock(sc->sc_cmdq_lock);
                return aborted || releaseHardware;
            });
            IOSimpleLockLock(sc->sc_cmdq_lock);
            const bool aborted=sc->sc_cmdq_slots[0].state==IWX_CMD_SLOT_ABORTED;
            IOSimpleLockUnlock(sc->sc_cmdq_lock);
            guard.unlock(); iwx_cmdq_leave(sc);
            if (aborted) return ENXIO;
        }
        return failHardware ? EIO : 0;
    }
    void iwx_stop_device(iwx_softc *sc) {
        iwx_cmdq_stop(sc); ++hardwareStops;
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
    }
    int tsleep_nsec(void *, int, const char *, uint64_t) { return EWOULDBLOCK; }
    void iwx_setup_ht_rates(iwx_softc *) {}
    void iwx_setup_vht_rates(iwx_softc *) {}
    void iwx_setup_he_rates(iwx_softc *) {}
    void iwx_sae_engine_reopen(iwx_softc *) {}
    void iwx_sae_engine_stop_begin(iwx_softc *) {}
    void iwx_sae_wcl_stop_begin(iwx_softc *) {}
    void iwx_mfp_pae_abort_all(iwx_softc *, bool) {}
    void iwx_security_rx_purge(iwx_softc *) {}
    bool iwx_sae_tx_snapshot_reset(iwx_softc *, ItlSaeAuthTransportEventV1 *) { return false; }
    void iwx_sae_tx_cancel_all(iwx_softc *) {}
    void iwx_sae_tx_purge(iwx_softc *) {}
    void iwx_sae_tx_emit_reset_event(iwx_softc *, ItlSaeAuthTransportEventV1 *) {}
    bool iwx_sae_driver_reset_recovery_pending(iwx_softc *, bool) { return false; }
    int iwx_resume(iwx_softc *) { ++resumeCalls; return 0; }
    int iwx_prepare_card_hw(iwx_softc *) { ++prepareCalls; return 0; }
    void iwx_del_task(iwx_softc *, taskq *, task *) {}
    void resetPrimaryRxBaLocked() {}
    void iwx_clear_reorder_buffer(iwx_softc *, iwx_rxba_data *) {}
    int runWorker() {
        assert(iwx_task_gate_enter(&com, true));
        const int result=iwx_init_internal(&com.sc_ic.ic_if, true);
        iwx_task_gate_leave(&com);
        { std::lock_guard<std::mutex> guard(stageMutex); initFinished=true; stageCv.notify_all(); }
        return result;
    }
};
static void ieee80211_begin_scan(_ifnet *) {
    ++active->scans; active->com.sc_ic.ic_state=IEEE80211_S_SCAN;
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
    ItlIwx driver;
    if (scenario=="early-off" || scenario=="early-off-on" || scenario=="early-off-primary-down") {
        driver.pauseHardware=true;
        int initResult=-1; bool prematureOff=false;
        std::thread initializer([&] { initResult=driver.runWorker(); });
        { std::unique_lock<std::mutex> guard(stageMutex);
          assert(stageCv.wait_for(guard, std::chrono::seconds(3), [] { return hardwareEntered; })); }
        assert(!(driver.com.sc_ic.ic_if.if_flags & IFF_RUNNING));
        if (scenario=="early-off-primary-down") driver.com.sc_ic.ic_if.if_flags &= ~IFF_UP;
        std::thread off([&] {
            assert(driver.disable(nullptr)==0);
            std::lock_guard<std::mutex> guard(stageMutex);
            prematureOff=!initFinished; offFinished=true; stageCv.notify_all();
        });
        { std::unique_lock<std::mutex> guard(stageMutex);
          assert(stageCv.wait_for(guard, std::chrono::seconds(3), [] { return offFinished; })); }
        if (prematureOff && scenario=="early-off-on") {
            assert(driver.enable(nullptr)==0); driver.radioPowerOnEpoch=84;
        }
        { std::lock_guard<std::mutex> guard(driver.waitMutex); releaseHardware=true; driver.waitCv.notify_all(); }
        initializer.join(); off.join();
        std::fprintf(stderr, "IWX early Off: premature=%d init=%d live=%d stops=%d readyEpoch=%llu\n",
            prematureOff, initResult, driver.hardwareLive, driver.hardwareStops,
            static_cast<unsigned long long>(driver.radioReadyRequestEpoch));
        assert(!prematureOff && initResult==ENXIO && !driver.hardwareLive);
        assert(driver.hardwareStops==1 && driver.scans==0 && driver.readyEvents==0);
        assert(driver.com.sc_cmdq_stopping && driver.com.sc_cmdq_senders==0 &&
            driver.com.sc_cmdq_slots[0].state==IWX_CMD_SLOT_ABORTED &&
            driver.com.sc_cmdq_slots[0].async_cookie==0 && driver.com.sc_cmd_resp_pkt[0]==nullptr);
        assert(driver.com.sc_task_gate_init_refs==0 && driver.com.sc_task_gate_active==0 &&
            driver.com.sc_task_gate_stop_refs==0 && driver.com.sc_task_gate_closed);
        if (scenario=="early-off-on") {
            driver.pauseHardware=false;
            assert(driver.enable(nullptr)==0); driver.radioPowerOnEpoch=84;
            assert(driver.runWorker()==0 && driver.hardwareLive && driver.readyEvents==1);
            assert(driver.radioReadyRequestEpoch==84);
            assert(driver.disable(nullptr)==0 && !driver.hardwareLive);
        }
    } else if (scenario=="overlapping-off" || scenario=="on-during-stop") {
        assert(driver.runWorker()==0);
        driver.pauseReset=true; bool premature=false; int onResult=0;
        std::thread first([&] { assert(driver.disable(nullptr)==0); });
        { std::unique_lock<std::mutex> guard(stageMutex);
          assert(stageCv.wait_for(guard, std::chrono::seconds(3), [] { return resetEntered; })); }
        std::thread second;
        if (scenario=="on-during-stop") onResult=driver.enable(nullptr);
        else {
            second=std::thread([&] {
                assert(driver.disable(nullptr)==0);
                std::lock_guard<std::mutex> guard(stageMutex);
                offFinished=true; stageCv.notify_all();
            });
            std::unique_lock<std::mutex> guard(stageMutex);
            assert(stageCv.wait_for(guard, std::chrono::seconds(3), [] { return offFinished || drainWaits!=0; }));
            premature=offFinished;
        }
        { std::lock_guard<std::mutex> guard(stageMutex); releaseReset=true; stageCv.notify_all(); }
        first.join(); if (second.joinable()) second.join();
        std::fprintf(stderr, "IWX repeated control: premature=%d onResult=%d resume=%d prepare=%d\n",
            premature, onResult, driver.resumeCalls, driver.prepareCalls);
        assert(!driver.hardwareLive && driver.hardwareStops==1);
        if (scenario=="overlapping-off") assert(!premature);
        else assert(onResult==kIOReturnNotReady && driver.resumeCalls==0 && driver.prepareCalls==0 &&
            !(driver.com.sc_ic.ic_if.if_flags & IFF_UP));
    } else if (scenario=="self-task-stop-collision" || scenario=="self-epoch-stop-collision") {
        const bool selfTask=scenario=="self-task-stop-collision";
        int generation=0;
        if (selfTask) assert(driver.iwx_task_gate_enter(&driver.com, true));
        else assert(driver.iwx_task_gate_begin_epoch(&driver.com, &generation));
        driver.hardwareLive=true;
        std::thread first([&] { assert(driver.disable(nullptr)==0); });
        { std::unique_lock<std::mutex> guard(stageMutex);
          assert(stageCv.wait_for(guard, std::chrono::seconds(3), [] { return drainWaits!=0; })); }
        driver.iwx_stop_internal(&driver.com.sc_ic.ic_if, selfTask, !selfTask);
        if (selfTask) driver.iwx_task_gate_leave(&driver.com);
        else driver.iwx_task_gate_end_epoch(&driver.com);
        first.join();
        assert(!driver.hardwareLive && driver.hardwareStops==1 &&
            driver.com.sc_task_gate_active==0 && driver.com.sc_task_gate_init_refs==0 &&
            driver.com.sc_task_gate_stop_refs==0);
    } else if (scenario=="normal" || scenario=="timeout" || scenario=="hardware-failure" || scenario=="monitor") {
        driver.emitReady=scenario!="timeout"; driver.failHardware=scenario=="hardware-failure";
        if (scenario=="monitor") driver.com.sc_ic.ic_opmode=IEEE80211_M_MONITOR;
        const int result=driver.runWorker();
        assert(result==(driver.failHardware ? EIO : scenario=="timeout" ? EWOULDBLOCK : 0));
        assert(driver.com.sc_task_gate_active==0 && driver.com.sc_task_gate_init_refs==0);
        if (scenario=="timeout" || driver.failHardware) assert(!driver.hardwareLive && driver.hardwareStops==1);
        else assert(driver.disable(nullptr)==0 && !driver.hardwareLive);
    } else return 2;
    std::printf("IWX complete init/stop/task gate/q0 %s: PASS\n", argv[1]);
}
