// Complete extracted IWM/IWX init; no firmware or successful GUI claim.
#include <HAL/ItlRadioReadyV1.h>
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>

#define KASSERT(condition, message) assert(condition)
#define XYLog(...) ((void)0)
#define ARRAY_SIZE(array) int(sizeof(array) / sizeof((array)[0]))
#define SEC_TO_NSEC(seconds) (uint64_t(seconds) * 1000000000ULL)
#define container_of(pointer, type, member) (reinterpret_cast<type *>(pointer))

static constexpr unsigned IFF_UP=1, IFF_RUNNING=2;
static constexpr int IEEE80211_M_MONITOR=1;
static constexpr int IEEE80211_S_INIT=0, IEEE80211_S_SCAN=1;
static constexpr int IEEE80211_S_AUTH=2, IEEE80211_S_RUN=3;
[[maybe_unused]] static constexpr int MVM_INVALID_QUEUE=-1;
static constexpr int PCATCH=0;
static constexpr uint32_t MVM_FLAG_RFKILL=2, MVM_FLAG_HW_ERR=0x80,
                          MVM_FLAG_SHUTDOWN=0x100;
using IOInterruptState=unsigned;
struct IOSimpleLock { bool held=false; };
static std::function<void()> unlockHook;
static IOInterruptState IOSimpleLockLockDisableInterrupt(IOSimpleLock *lock) {
    assert(lock && !lock->held); lock->held=true; return 1;
}
static void IOSimpleLockUnlockEnableInterrupt(IOSimpleLock *lock, IOInterruptState irq) {
    assert(lock && lock->held && irq==1); lock->held=false;
    if (unlockHook) { auto hook=unlockHook; unlockHook={}; hook(); }
}

struct Queue {};
struct _ifnet {
    void *if_softc=nullptr;
    unsigned if_flags=IFF_UP;
    Queue if_snd;
};
struct Node { int ni_chan=0; };
struct ieee80211com {
    _ifnet ic_if;
    Node node;
    Node *ic_bss=&node;
    int ic_ibss_chan=1, ic_opmode=0, ic_state=IEEE80211_S_INIT;
    unsigned ic_initial_scan_census_only=0;
};
struct mvm_softc {
    ieee80211com sc_ic;
    int sc_generation=10;
    uint32_t sc_flags=0;
    unsigned agg_tid_disable=0, agg_queue_mask=0;
    unsigned sc_tx_ba[4]={};
    struct { unsigned refs=0; } task_refs;
    struct { bool sku_cap_11n_enable=true, sku_cap_11ac_enable=true;
             bool sku_cap_11ax_enable=true; } sc_nvm;
    struct { int qid=0; } sc_tid_data[4];
    uint8_t init_retry_count=4;
};

enum class Scenario { Normal, LostWake, ConsumerAdvances, SecurityBeforeReady,
                      HardwareFailure, ScanRejected, ResetReplacement, Monitor,
                      ReadyOnTimeout, EarlyConsumer, NoReceipt,
                      ShutdownReplacement, ReceiptValidity };
static Scenario scenario=Scenario::Normal;
class ItlMvm;
static ItlMvm *active=nullptr;
static void ieee80211_begin_scan(_ifnet *);
static void ieee80211_new_state(ieee80211com *, int, int);
static void ifq_clr_oactive(Queue *) {}
static void ifq_flush(Queue *) {}

class ItlMvm {
public:
    // The fixture softc is the first member for the port's container_of use.
    mvm_softc com;
    IOSimpleLock lowerLock;
    IOSimpleLock *wclScanLock=&lowerLock;
    uint64_t radioReadyReceiptSerial=0, radioReadyRequestEpoch=42;
    uint32_t radioReadyBackendGeneration=0;
    bool ready=false, mfpOpen=false, txOpen=false, engineOpen=false;
    bool epochLive=true, scanAdmission=true, resetReconnect=false;
    int sleeps=0, stops=0, hardwareStops=0, opens=0;
    bool readySawAllSecurity=false;
    ItlMvm() { com.sc_ic.ic_if.if_softc=&com; active=this; }
    int mvm_init(_ifnet *);
    int mvm_init_internal(_ifnet *, bool);
    bool isRadioScanReady(uint32_t);
    bool isRadioReadyCurrent(const ItlRadioReadyV1 *);
    uint64_t scanCommandResetEpoch() const { return 7; }
    bool reopenScanCommands(uint64_t epoch, uint32_t generation) {
        assert(epoch==7 && generation==unsigned(com.sc_generation));
        return scanAdmission;
    }
    int mvm_init_hw(mvm_softc *sc) {
        assert(sc==&com);
        return scenario==Scenario::HardwareFailure ? EIO : 0;
    }
    void mvm_stop_device(mvm_softc *sc) { assert(sc==&com); ++hardwareStops; }
    void mvm_setup_ht_rates(mvm_softc *) {}
    void mvm_setup_vht_rates(mvm_softc *) {}
    void mvm_setup_he_rates(mvm_softc *) {}
    void mvm_mfp_pae_reopen(mvm_softc *) { mfpOpen=true; ++opens; }
    void mvm_sae_tx_reopen(mvm_softc *) { txOpen=true; ++opens; }
    void mvm_sae_engine_reopen(mvm_softc *) { engineOpen=true; ++opens; }
    bool mvm_sae_driver_reset_recovery_pending(mvm_softc *, bool consume) {
        const bool pending=resetReconnect;
        if (consume) resetReconnect=false;
        return pending;
    }
    bool mvm_task_gate_begin_epoch(mvm_softc *, int *generation) {
        *generation=++com.sc_generation;
        return epochLive;
    }
    bool mvm_task_gate_epoch_live(mvm_softc *, int generation) {
        return epochLive && generation==com.sc_generation &&
            (com.sc_flags & MVM_FLAG_SHUTDOWN)==0;
    }
    bool mvm_task_gate_open(mvm_softc *, int generation) {
        return generation==com.sc_generation && epochLive;
    }
    void mvm_task_gate_end_epoch(mvm_softc *) {}
    bool mvm_cmdq_start(mvm_softc *, int generation) {
        return generation==com.sc_generation;
    }
    void mvm_cmdq_stop(mvm_softc *) {}
    void stop() {
        ++stops; ready=false; radioReadyReceiptSerial=0;
        mfpOpen=txOpen=engineOpen=false;
        com.sc_ic.ic_if.if_flags &= ~IFF_RUNNING;
    }
    void mvm_stop(_ifnet *) { stop(); }
    void mvm_stop_internal(_ifnet *, bool, bool) { stop(); }
    void mvm_stop_internal(_ifnet *, bool) { stop(); }
    bool mvm_radio_init_begin(mvm_softc *, int *generation) {
        *generation=++com.sc_generation;
        return epochLive;
    }
    bool mvm_radio_init_current(mvm_softc *, int generation) {
        return epochLive && generation==com.sc_generation &&
            (com.sc_flags & MVM_FLAG_SHUTDOWN)==0 &&
            (com.sc_ic.ic_if.if_flags & IFF_UP)!=0;
    }
    void mvm_radio_init_end(mvm_softc *) {}
    void commitScanReady() {
        assert(com.sc_ic.ic_if.if_flags & IFF_RUNNING);
        com.sc_ic.ic_state=IEEE80211_S_SCAN;
        ready=true;
        radioReadyReceiptSerial=51;
        radioReadyBackendGeneration=com.sc_generation;
        readySawAllSecurity=engineOpen && (!MVM_INIT_IWM || (mfpOpen && txOpen));
        if (scenario==Scenario::ConsumerAdvances || scenario==Scenario::EarlyConsumer)
            com.sc_ic.ic_state=IEEE80211_S_AUTH;
    }
    int tsleep_nsec(void *ident, int priority, const char *, uint64_t timeout) {
        assert(ident==&com.sc_ic.ic_state && priority==PCATCH);
        assert(timeout==SEC_TO_NSEC(1));
        ++sleeps;
        if (scenario==Scenario::ResetReplacement) {
            ++com.sc_generation; epochLive=false;
            ready=false; mfpOpen=txOpen=engineOpen=false;
            return 0;
        }
        if (scenario==Scenario::ShutdownReplacement) {
            commitScanReady(); com.sc_flags |= MVM_FLAG_SHUTDOWN;
            mfpOpen=txOpen=engineOpen=false;
            return 0;
        }
        if (scenario==Scenario::LostWake || scenario==Scenario::ScanRejected || sleeps>1)
            return EWOULDBLOCK;
        if (scenario==Scenario::NoReceipt)
            com.sc_ic.ic_state=IEEE80211_S_SCAN;
        else
            commitScanReady();
        return scenario==Scenario::ReadyOnTimeout ? EWOULDBLOCK : 0;
    }
    int run() {
#if MVM_INIT_IWM
        return mvm_init(&com.sc_ic.ic_if);
#else
        return mvm_init_internal(&com.sc_ic.ic_if, true);
#endif
    }
};

static void ieee80211_begin_scan(_ifnet *ifp) {
    assert(ifp==&active->com.sc_ic.ic_if);
    if (scenario==Scenario::LostWake || scenario==Scenario::EarlyConsumer)
        active->commitScanReady();
}
static void ieee80211_new_state(ieee80211com *ic, int state, int) {
    assert(ic==&active->com.sc_ic); ic->ic_state=state;
}

#include "init.inc"

int main(int argc, char **argv) {
    assert(argc==2);
    const std::string name=argv[1];
    if (name=="normal") scenario=Scenario::Normal;
    else if (name=="lost-wake") scenario=Scenario::LostWake;
    else if (name=="consumer-advances") scenario=Scenario::ConsumerAdvances;
    else if (name=="security-before-ready") scenario=Scenario::SecurityBeforeReady;
    else if (name=="hardware-failure") scenario=Scenario::HardwareFailure;
    else if (name=="scan-rejected") scenario=Scenario::ScanRejected;
    else if (name=="reset-replacement") scenario=Scenario::ResetReplacement;
    else if (name=="monitor") scenario=Scenario::Monitor;
    else if (name=="ready-on-timeout") scenario=Scenario::ReadyOnTimeout;
    else if (name=="early-consumer") scenario=Scenario::EarlyConsumer;
    else if (name=="no-receipt") scenario=Scenario::NoReceipt;
    else if (name=="shutdown-replacement") scenario=Scenario::ShutdownReplacement;
    else if (name=="receipt-validity") scenario=Scenario::ReceiptValidity;
    else return 2;
    ItlMvm driver;
    if (scenario==Scenario::ReceiptValidity) {
        const unsigned generation=driver.com.sc_generation;
        assert(!driver.isRadioScanReady(generation));
        driver.com.sc_ic.ic_if.if_flags |= IFF_RUNNING;
        driver.commitScanReady();
        driver.com.sc_ic.ic_state=IEEE80211_S_AUTH;
        assert(driver.isRadioScanReady(generation));
        assert(!driver.isRadioScanReady(0));
        assert(!driver.isRadioScanReady(generation+1));
        for (uint32_t flag : {MVM_FLAG_RFKILL,MVM_FLAG_HW_ERR,MVM_FLAG_SHUTDOWN}) {
            driver.com.sc_flags=flag;
            assert(!driver.isRadioScanReady(generation));
        }
        driver.com.sc_flags=0;
        for (unsigned flags : {0U,IFF_UP,IFF_RUNNING}) {
            driver.com.sc_ic.ic_if.if_flags=flags;
            assert(!driver.isRadioScanReady(generation));
        }
        driver.com.sc_ic.ic_if.if_flags=IFF_UP | IFF_RUNNING;
        driver.radioReadyRequestEpoch=0; // Genuine bootstrap is still init.
        assert(driver.isRadioScanReady(generation));
        unlockHook=[&] { driver.radioReadyReceiptSerial=0; };
        assert(!driver.isRadioScanReady(generation));
        driver.commitScanReady();
        unlockHook=[&] { ++driver.com.sc_generation; };
        assert(!driver.isRadioScanReady(generation));
        driver.wclScanLock=nullptr;
        assert(!driver.isRadioScanReady(generation));
        std::puts("MVM complete sticky ready getter and validator: PASS");
        return 0;
    }
    if (scenario==Scenario::Monitor)
        driver.com.sc_ic.ic_opmode=IEEE80211_M_MONITOR;
    const int result=driver.run();
    std::fprintf(stderr,
        "%s family=%s result=%d sleeps=%d stops=%d ready=%d securityAtReady=%d\n",
        argv[1], MVM_INIT_IWM ? "iwm" : "iwx", result, driver.sleeps,
        driver.stops, driver.ready, driver.readySawAllSecurity);
    if (scenario==Scenario::HardwareFailure) {
        assert(result==EIO && driver.hardwareStops==1 && driver.opens==0);
    } else if (scenario==Scenario::ScanRejected || scenario==Scenario::NoReceipt) {
        assert(result==EWOULDBLOCK && driver.stops==1);
    } else if (scenario==Scenario::ResetReplacement || scenario==Scenario::ShutdownReplacement) {
        assert(result==ENXIO && driver.stops==0 && !driver.engineOpen);
    } else {
        assert(result==0 && driver.stops==0 && driver.engineOpen);
        if (scenario!=Scenario::Monitor) assert(driver.ready);
        if (scenario==Scenario::SecurityBeforeReady) assert(driver.readySawAllSecurity);
        if (scenario==Scenario::LostWake || scenario==Scenario::EarlyConsumer)
            assert(driver.sleeps==0);
    }
    std::puts("MVM complete init/ready ordering: PASS");
}
