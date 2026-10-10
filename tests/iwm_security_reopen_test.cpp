// Complete production owner/reopen/close/callback/publication bodies.
// Kernel locks, cancellation payload, runtime capability, callback addresses
// and task scheduling are explicit doubles; no firmware or callbacks execute.
#include <sys/types.h>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>

#if defined(__APPLE__)
static void explicit_bzero(void *memory, size_t length) {
    volatile uint8_t *bytes=static_cast<volatile uint8_t *>(memory);
    while (length--) *bytes++=0;
}
#endif
#define KASSERT(condition, message) assert(condition)
constexpr unsigned IFF_UP=1, IFF_RUNNING=2;
constexpr uint32_t IWM_FLAG_SHUTDOWN=0x100;
constexpr uint32_t IWM_SAE_ENGINE_CALLBACK_CLOSED=0x80000000U;
constexpr uint32_t IWM_SAE_ENGINE_CALLBACK_COUNT_MASK=0x7fffffffU;
struct Lock { bool held=false; };
using IOLock=Lock;
using IOSimpleLock=Lock;
using IOInterruptState=unsigned;
static unsigned lockDepth=0, leafDepth=0;
static Lock *unlockTarget=nullptr, *simpleTarget=nullptr;
static std::function<void()> unlockHook, sleepHook, beforeSimpleHook;
static std::function<bool()> unlockPredicate;
static void afterUnlock(Lock *lock) {
    if (lock==unlockTarget && unlockHook &&
        (!unlockPredicate || unlockPredicate())) {
        auto hook=unlockHook; unlockHook={}; hook();
    }
}
static void IOLockLock(Lock *lock) {
    assert(leafDepth==0 && lock && !lock->held);
    lock->held=true; ++lockDepth;
}
static void IOLockUnlock(Lock *lock) {
    assert(leafDepth==0 && lock && lock->held && lockDepth);
    lock->held=false; --lockDepth; afterUnlock(lock);
}
static void IOSimpleLockLock(Lock *lock) {
    if (lock==simpleTarget && beforeSimpleHook) {
        auto hook=beforeSimpleHook; beforeSimpleHook={}; hook();
    }
    assert(lock && !lock->held); lock->held=true; ++lockDepth; ++leafDepth;
}
static void IOSimpleLockUnlock(Lock *lock) {
    assert(lock && lock->held && lockDepth && leafDepth);
    lock->held=false; --lockDepth; --leafDepth; afterUnlock(lock);
}
static IOInterruptState IOSimpleLockLockDisableInterrupt(Lock *lock) {
    IOSimpleLockLock(lock); return 1;
}
static void IOSimpleLockUnlockEnableInterrupt(Lock *lock, IOInterruptState irq) {
    assert(irq==1); IOSimpleLockUnlock(lock);
}
static void IOLockWakeup(Lock *lock, void *, bool) {
    assert(lock && lock->held);
}
static void IOSleep(unsigned delay) {
    assert(delay==1 && lockDepth==0 && sleepHook);
    auto hook=sleepHook; sleepHook={}; hook();
}
struct iwm_sae_engine_owner {
    bool active=false, cancelled=false, suppress_scan=true;
};
struct IwmSaeEngineCancellation { unsigned unused=0; };
struct _ifnet { unsigned if_flags=IFF_UP; };
struct ieee80211com {
    _ifnet ic_if;
    IOSimpleLock *ic_pae_selected_bss_lock=nullptr;
    void (*ic_sae_auth_hold)()=nullptr;
    void (*ic_sae_auth_owned)()=nullptr;
    void (*ic_sae_engine_peer_event)()=nullptr;
    void (*ic_sae_wcl_request_revoke)()=nullptr;
    void (*ic_sae_roam_port_valid)()=nullptr;
    void (*ic_sae_wnm_roam_start)()=nullptr;
    void (*ic_sae_wcl_roam_start)()=nullptr;
    void (*ic_sae_bss_loss_recover)()=nullptr;
};
struct iwm_softc {
    ieee80211com sc_ic;
    Lock mfpLock, txLock, txLifeLock, engineLock, hookWriterLock;
    IOSimpleLock *sc_mfp_pae_lock=&mfpLock;
    IOSimpleLock *sc_sae_tx_lock=&txLock;
    IOLock *sc_sae_tx_lifecycle_lock=&txLifeLock;
    IOSimpleLock *sc_sae_engine_lock=&engineLock;
    uint32_t sc_flags=0;
    int sc_generation=10;
    uint32_t sc_radio_init_refs=0, sc_radio_stop_refs=0;
    uint32_t sc_sae_tx_lifecycle_active=0;
    bool sc_mfp_pae_detaching=false, sc_mfp_pae_stopping=true;
    uint32_t sc_mfp_pae_lifecycle_generation=1;
    bool sc_sae_tx_detaching=false, sc_sae_tx_stopping=true;
    bool sc_sae_tx_lifecycle_closed=true;
    uint32_t sc_sae_tx_generation=1;
    bool sc_sae_engine_detaching=false, sc_sae_engine_stopping=true;
    bool sc_sae_engine_task_ready=true, runtimeEnabled=true;
    uint32_t sc_sae_engine_lifecycle_generation=1;
    uint32_t sc_sae_engine_join_failure_generation=0;
    uint32_t sc_sae_engine_callback_state=IWM_SAE_ENGINE_CALLBACK_CLOSED;
    iwm_sae_engine_owner sc_sae_engine_owner;
    void *sc_sae_engine=nullptr;
    unsigned schedules=0;
    iwm_softc() { sc_ic.ic_pae_selected_bss_lock=&hookWriterLock; }
};
static bool iwm_sae_engine_runtime_enabled(const iwm_softc *sc) {
    return sc && sc->runtimeEnabled && sc->sc_sae_engine_lock;
}
static bool iwm_sae_engine_mark_cancelled_locked(iwm_softc *sc,
    uint64_t, bool suppress, IwmSaeEngineCancellation *) {
    assert(sc->engineLock.held);
    if (!sc->sc_sae_engine_owner.active) return false;
    sc->sc_sae_engine_owner.cancelled=true;
    sc->sc_sae_engine_owner.suppress_scan=suppress;
    return true;
}
class ItlIwm {
public:
    struct CommandGate { void commandWakeup(void *, bool) {} };
    CommandGate *getMainCommandGate() { return nullptr; }
    bool iwm_radio_init_begin(iwm_softc *, int *);
    bool iwm_radio_init_current(iwm_softc *, int);
    bool iwm_radio_init_current_locked(iwm_softc *, int);
    void iwm_radio_init_end(iwm_softc *);
    bool iwm_radio_stop_begin(iwm_softc *, int *);
    void iwm_radio_stop_end(iwm_softc *, int);
#if IWM_SECURITY_REOPEN_HISTORICAL
    // Historical complete bodies had no expected-radio-generation argument.
    // Adapters deliberately discard it, rather than retrofit the old guard.
    void iwm_mfp_pae_reopen(iwm_softc *);
    void iwm_sae_tx_reopen(iwm_softc *);
    void iwm_sae_engine_reopen(iwm_softc *);
    void iwm_mfp_pae_reopen(iwm_softc *sc, int) { iwm_mfp_pae_reopen(sc); }
    void iwm_sae_tx_reopen(iwm_softc *sc, int) { iwm_sae_tx_reopen(sc); }
    void iwm_sae_engine_reopen(iwm_softc *sc, int) { iwm_sae_engine_reopen(sc); }
#else
    void iwm_mfp_pae_reopen(iwm_softc *, int);
    void iwm_sae_tx_reopen(iwm_softc *, int);
    void iwm_sae_engine_reopen(iwm_softc *, int);
#endif
    void iwm_sae_engine_stop_begin(iwm_softc *);
    void iwm_sae_engine_schedule_task(iwm_softc *sc) {
        assert(lockDepth==0); ++sc->schedules;
    }
    // Storage-only addresses: the real publisher installs all eight, but
    // this fixture neither invokes nor claims to qualify their callbacks.
    static void iwm_sae_auth_hold() {}
    static void iwm_sae_auth_owned() {}
    static void iwm_sae_engine_peer_event() {}
    static void iwm_sae_wcl_request_revoke() {}
    static void iwm_sae_roam_port_valid() {}
    static void iwm_sae_wnm_roam_start() {}
    static void iwm_sae_wcl_roam_start() {}
    static void iwm_sae_bss_loss_recover() {}
};

#include "security.inc"

static bool hooksLive(const iwm_softc &sc) {
    const auto &ic=sc.sc_ic;
    return ic.ic_sae_auth_hold && ic.ic_sae_auth_owned &&
        ic.ic_sae_engine_peer_event && ic.ic_sae_wcl_request_revoke &&
        ic.ic_sae_roam_port_valid && ic.ic_sae_wnm_roam_start &&
        ic.ic_sae_wcl_roam_start && ic.ic_sae_bss_loss_recover;
}

int main(int argc, char **argv) {
    assert(argc==2);
    const std::string scenario=argv[1];
    ItlIwm driver;
    iwm_softc sc;
    int generation=0, stopGeneration=0;
    assert(driver.iwm_radio_init_begin(&sc, &generation) && generation==11);
    auto stopOwner=[&] {
        assert(driver.iwm_radio_stop_begin(&sc, &stopGeneration));
    };
    auto stop=[&] {
        stopOwner(); driver.iwm_sae_engine_stop_begin(&sc);
    };
    auto reopenOne=[&](int expected) {
        if (scenario.find("-mfp")!=std::string::npos)
            driver.iwm_mfp_pae_reopen(&sc, expected);
        else if (scenario.find("-tx")!=std::string::npos)
            driver.iwm_sae_tx_reopen(&sc, expected);
        else driver.iwm_sae_engine_reopen(&sc, expected);
    };
    auto assertClosed=[&] {
        assert(sc.sc_mfp_pae_stopping && sc.sc_mfp_pae_lifecycle_generation==1);
        assert(sc.sc_sae_tx_stopping && sc.sc_sae_tx_lifecycle_closed);
        assert(sc.sc_sae_engine_stopping && !hooksLive(sc) &&
            sc.sc_sae_engine_callback_state==IWM_SAE_ENGINE_CALLBACK_CLOSED);
    };
    if (scenario=="shutdown-mfp" || scenario=="shutdown-tx" ||
        scenario=="shutdown-engine" || scenario.rfind("stale-",0)==0 ||
        scenario.rfind("no-init-",0)==0) {
        // An engine-only stale reopen still sees an open TX task lifecycle.
        // Keep the other cases closed so their own transition is observable.
        if (scenario.find("-engine")!=std::string::npos)
            sc.sc_sae_tx_lifecycle_closed=false;
        if (scenario.rfind("shutdown-",0)==0) stopOwner();
        if (scenario.rfind("no-init-",0)==0) driver.iwm_radio_init_end(&sc);
        reopenOne(scenario.rfind("stale-",0)==0 ? generation-1 : generation);
        if (scenario.find("-engine")!=std::string::npos) {
            assert(sc.sc_sae_engine_stopping && !hooksLive(sc) &&
                sc.sc_sae_engine_callback_state==IWM_SAE_ENGINE_CALLBACK_CLOSED);
        } else assertClosed();
    } else if (scenario=="shutdown-publication" ||
        scenario=="shutdown-after-hook-snapshot") {
        sc.sc_sae_engine_stopping=false;
        sc.sc_sae_engine_callback_state=0;
        if (scenario=="shutdown-publication") stopOwner();
        else {
            simpleTarget=&sc.hookWriterLock; beforeSimpleHook=stopOwner;
        }
        assert(!iwm_sae_engine_publish_hooks(&sc, true, false, 1));
        assert(!hooksLive(sc) && !sc.sc_ic.ic_sae_auth_hold);
    } else if (scenario=="detach") {
        sc.sc_mfp_pae_detaching=sc.sc_sae_tx_detaching=sc.sc_sae_engine_detaching=true;
        driver.iwm_mfp_pae_reopen(&sc, generation);
        driver.iwm_sae_tx_reopen(&sc, generation);
        driver.iwm_sae_engine_reopen(&sc, generation);
        assertClosed();
    } else {
        driver.iwm_mfp_pae_reopen(&sc, generation);
        driver.iwm_sae_tx_reopen(&sc, generation);
        if (scenario=="pending-retirement") {
            sc.sc_sae_engine_owner.active=sc.sc_sae_engine_owner.cancelled=true;
            sc.sc_ic.ic_sae_auth_owned=ItlIwm::iwm_sae_auth_owned;
            driver.iwm_sae_engine_reopen(&sc, generation);
            assert(!sc.sc_sae_engine_stopping && sc.schedules==1 &&
                !hooksLive(sc) && sc.sc_ic.ic_sae_auth_owned &&
                !sc.sc_sae_engine_owner.suppress_scan &&
                sc.sc_sae_engine_callback_state==0);
        } else if (scenario=="stop-before-claim" ||
            scenario=="stop-during-drain" || scenario=="stop-after-claim") {
            if (scenario=="stop-during-drain") {
                sc.sc_sae_engine_callback_state=IWM_SAE_ENGINE_CALLBACK_CLOSED | 1U;
                sleepHook=[&] { stop(); iwm_sae_engine_callback_leave(&sc); };
            } else {
                unlockTarget=sc.sc_sae_tx_lifecycle_lock; unlockHook=stop;
                if (scenario=="stop-after-claim") {
                    unlockPredicate=[&] { return !sc.sc_sae_engine_stopping; };
#if IWM_SECURITY_REOPEN_HISTORICAL
                    unlockTarget=sc.sc_sae_engine_lock;
#endif
                }
            }
            driver.iwm_sae_engine_reopen(&sc, generation);
            assert(!unlockHook && !sleepHook);
            assert(sc.sc_sae_engine_stopping && !hooksLive(sc) &&
                sc.sc_sae_engine_lifecycle_generation==2 &&
                sc.sc_sae_engine_callback_state==IWM_SAE_ENGINE_CALLBACK_CLOSED);
        } else if (scenario=="runtime-disabled") {
            sc.runtimeEnabled=false;
            driver.iwm_sae_engine_reopen(&sc, generation);
            assert(sc.sc_sae_engine_stopping && !hooksLive(sc));
        } else if (scenario=="normal" || scenario=="publication-owner-lock") {
            if (scenario=="publication-owner-lock") {
                simpleTarget=&sc.hookWriterLock;
                beforeSimpleHook=[&] {
                    assert(sc.txLifeLock.held && sc.sc_radio_init_refs==1 &&
                        sc.sc_radio_stop_refs==0 && sc.sc_generation==generation);
                };
            }
            driver.iwm_sae_engine_reopen(&sc, generation);
            assert(!beforeSimpleHook);
            assert(!sc.sc_mfp_pae_stopping && !sc.sc_sae_tx_stopping &&
                !sc.sc_sae_tx_lifecycle_closed && !sc.sc_sae_engine_stopping &&
                hooksLive(sc) && sc.sc_sae_engine_callback_state==0);
        } else return 2;
    }
    if (sc.sc_radio_init_refs) driver.iwm_radio_init_end(&sc);
    if (sc.sc_radio_stop_refs) driver.iwm_radio_stop_end(&sc, stopGeneration);
    assert(lockDepth==0 && leafDepth==0 && sc.sc_sae_tx_lifecycle_active==0);
    std::printf("IWM full security reopen/close %s: PASS\n", argv[1]);
}
