#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <utility>
#include <type_traits>
#include <vector>
#include <cerrno>
#include "include/HAL/ItlStateTransitionLease.hpp"
#include "include/HAL/ItlFirmwareContextLease.hpp"
#include "include/HAL/ItlScanCommandLease.hpp"
#include "itl80211/openbsd/net80211/ieee80211_join_attempt.h"

using u_int64_t = uint64_t;
using IOInterruptState = int;
using IOReturn = int;
enum { kIOReturnSuccess = 0, kIOReturnNotReady = 19 };
enum ieee80211_state { IEEE80211_S_INIT, IEEE80211_S_SCAN, IEEE80211_S_AUTH,
    IEEE80211_S_ASSOC, IEEE80211_S_RUN };
enum { IEEE80211_M_STA, IEEE80211_M_HOSTAP };
enum { IWM_FLAG_SHUTDOWN = 1, IWX_FLAG_SHUTDOWN = 1,
    IWM_FLAG_SCANNING = 2, IWX_FLAG_SCANNING = 2 };
enum { IFF_UP = 1, IFF_RUNNING = 2 };
#define IEEE80211_NEWSTATE_BACKEND_ARG(state, arg) ((arg) == -100 ? -1 : (arg))
#define IWX_AUTH_DIAG(...) ((void)0)
#define XYLog(...) ((void)0)
#define IC2IFP(ic) (&(ic)->ic_ac.ac_if)
#define container_of(ptr, type, field) static_cast<type *>((ptr)->owner)

static unsigned leafDepth;
static unsigned physicalContextCases;
static std::function<void()> unlocked;
struct IOSimpleLock { bool held = false; };
static int IOSimpleLockLockDisableInterrupt(IOSimpleLock *lock) {
    assert(lock && !lock->held);
    lock->held = true;
    ++leafDepth;
    return 0;
}
static void IOSimpleLockUnlockEnableInterrupt(IOSimpleLock *lock, int) {
    assert(lock && lock->held);
    lock->held = false;
    --leafDepth;
    if (leafDepth) return; // A yielding external boundary requires all leaves released.
    auto action = std::move(unlocked);
    unlocked = {};
    if (action) action();
}
struct OSObject { virtual ~OSObject() = default; };
struct IOService {};
struct IOInterruptEventSource;
static bool failAllocation;
static unsigned liveSources;
struct IOWorkLoop {
    bool gated = false, failAdd = false;
    bool inGate() const { return gated; }
    int addEventSource(IOInterruptEventSource *);
    void removeEventSource(IOInterruptEventSource *);
};
struct IOInterruptEventSource {
    using Action = void (*)(OSObject *, IOInterruptEventSource *, int);
    OSObject *owner;
    Action action;
    IOWorkLoop *workloop = nullptr;
    unsigned references = 1, signals = 0;
    bool enabled = false;
    static IOInterruptEventSource *interruptEventSource(OSObject *owner,
                                                       Action action) {
        assert(!leafDepth);
        if (failAllocation) return nullptr;
        ++liveSources;
        return new IOInterruptEventSource{owner, action};
    }
    void retain() { assert(references); ++references; }
    void release() {
        assert(!leafDepth && references);
        if (--references == 0) { --liveSources; delete this; }
    }
    void enable() { assert(!leafDepth); enabled = true; }
    void disable() { assert(!leafDepth); enabled = false; }
    void interruptOccurred(void *, IOService *, int) {
        assert(!leafDepth);
        ++signals;
    }
    void deliver() {
        assert(!leafDepth);
        if (!enabled || !workloop || !signals) return;
        signals = 0;
        bool prior = workloop->gated;
        workloop->gated = true;
        action(owner, this, 1);
        workloop->gated = prior;
    }
};
int IOWorkLoop::addEventSource(IOInterruptEventSource *source) {
    assert(!leafDepth);
    if (failAdd) return 1;
    source->workloop = this;
    return 0;
}
void IOWorkLoop::removeEventSource(IOInterruptEventSource *source) {
    assert(!leafDepth && source->workloop == this);
    source->workloop = nullptr;
}
struct _ifnet { void *if_softc = nullptr; unsigned if_flags = IFF_UP | IFF_RUNNING; };
#define ic_if ic_ac.ac_if
struct ieee80211_node {};
struct TestHal;
#include "scan_owner_test_fields.hpp"
struct ieee80211com {
    struct { _ifnet ac_if; } ic_ac;
    SCAN_OWNER_TEST_FIELDS;
    ieee80211_state ic_state = IEEE80211_S_SCAN;
    ieee80211_node *ic_bss = nullptr;
    TestHal *testOwner = nullptr;
};
extern "C" bool airportItlwmGetScanHomeAwayTime(uint32_t *value) {
    assert(!leafDepth); *value = 120; return true;
}
#include "include/HAL/ItlScanCommandPolicy.hpp"
struct task { unsigned enqueues = 0; };
struct taskq {};
static taskq systemQueue;
static taskq *systq = &systemQueue;
static void task_add(taskq *, task *task) { assert(!leafDepth); ++task->enqueues; }
struct TestSoft {
    OSObject *owner = nullptr;
    ieee80211com sc_ic;
    ieee80211_state ns_nstate = IEEE80211_S_INIT;
    int ns_arg = 0, sc_flags = 0;
    uint32_t sc_generation = 3;
    int (*sc_newstate)(ieee80211com *, ieee80211_state, int) = nullptr;
    task newstate_task, init_task, ba_task, mac_ctxt_task, chan_ctxt_task;
    taskq queue;
    taskq *sc_nswq = &queue;
    int sc_calib_to = 0;
    bool admission = true;
    bool closed = false;
    unsigned active = 0;
};
struct iwm_softc : TestSoft {};
struct iwx_softc : TestSoft {};
using iwx_node = ieee80211_node;
static int splnet() { return 0; }
static void splx(int) {}
static void timeout_del(int *) { assert(!leafDepth); }
static bool enter(TestSoft *sc, bool allowClosed) {
    assert(!leafDepth);
    if (!sc->admission || (sc->closed && !allowClosed)) return false;
    ++sc->active;
    return true;
}
static void leave(TestSoft *sc) { assert(!leafDepth && sc->active); --sc->active; }
static bool iwm_sae_tx_lifecycle_enter(iwm_softc *sc, bool allowClosed) { return enter(sc, allowClosed); }
static void iwm_sae_tx_lifecycle_leave(iwm_softc *sc) { leave(sc); }

struct TestCommandGate {
    int runAction(int (*)(OSObject *, void *, void *, void *, void *), void *) {
        assert(false && "state worker must not block on the main gate");
        return 0;
    }
};
using IOCommandGate = TestCommandGate;
[[maybe_unused]] static uint64_t ieee80211_pae_assoc_epoch_current(ieee80211com *ic) {
    return ic->ic_pae_assoc_epoch;
}
struct TestHal : OSObject {
    IOWorkLoop loop;
    IOSimpleLock scanLock, selectedLock;
    IOSimpleLock *wclScanLock = &scanLock;
    ItlScanCommandLease scanCommand{};
    ItlStateTransitionLease stateTransition{};
    ItlFirmwareContextLease primaryMacContext{}, primaryBindingContext{}, primaryStationContext{};
    ItlFirmwareStationUses primaryStationUses{};
    IOInterruptEventSource *stateTransitionSource = nullptr;
    ieee80211_node node;
    TestCommandGate gate;
    std::vector<int> lowerCalls;
    std::function<void()> lowerHook, commitHook, failureHook, ampduHook;
    std::function<void()> apFenceHook;
    bool apFence = false, deferAtScan = false;
    unsigned commits = 0, failures = 0, defers = 0;
    int lowerError = 0, genericError = 0, lastArgument = 0;
    IOWorkLoop *getMainWorkLoop() { return &loop; }
    TestCommandGate *getMainCommandGate() { return &gate; }
    bool isAPScanFenceActive() {
        assert(!leafDepth);
        const bool result = apFence || scanCommand.apSerial != 0;
        auto action = std::move(apFenceHook); apFenceHook = {};
        if (action) action();
        return result;
    }
    int lower(int operation) {
        assert(!leafDepth);
        lowerCalls.push_back(operation);
        auto action = std::move(lowerHook);
        lowerHook = {};
        if (action) action();
        return lowerError;
    }
    static int generic(ieee80211com *ic, ieee80211_state state, int argument) {
        TestHal *that = ic->testOwner;
        assert(!leafDepth && that->loop.inGate());
        ++that->commits;
        that->lastArgument = argument;
        ic->ic_state = state;
        auto action = std::move(that->commitHook);
        that->commitHook = {};
        if (action) action();
        return that->genericError;
    }
};
static void ieee80211_roam_link_failed(ieee80211com *ic, uint64_t epoch) {
    TestHal *that = ic->testOwner;
    assert(!leafDepth && that->loop.inGate());
    assert(epoch == ic->ic_pae_assoc_epoch);
    ++that->failures;
    auto action = std::move(that->failureHook);
    that->failureHook = {};
    if (action) action();
}
static void ieee80211_stop_ampdu_tx(ieee80211com *ic, ieee80211_node *, int) {
    auto action = std::move(ic->testOwner->ampduHook);
    ic->testOwner->ampduHook = {};
    if (action) action();
}
static void ieee80211_ba_del(ieee80211_node *) { assert(!leafDepth); }

#define STATE_DECLARATIONS \
    void reopenPrimaryStationUsers(const ItlStateTransitionRequest &); \
    bool deferPrimaryStationUsers(const ItlStateTransitionRequest &, bool = false); \
    void drainPrimaryRxBa(IOInterruptEventSource *) {} \
    void resumePrimaryStationUsers(); \
    bool initStateTransitions(); \
    void shutdownStateTransitions(); \
    int prepareStateTransition(int, int, ItlStateTransitionRequest *); \
    bool stateTransitionCurrent(const ItlStateTransitionRequest &); \
    bool primaryFirmwareContextsPresent(); \
    bool enqueueStateTransition(const ItlStateTransitionRequest &); \
    bool takeStateTransition(ItlStateTransitionRequest *); \
    bool noteStateTransitionProgress(ItlStateTransitionRequest *, uint8_t); \
    bool deferScanCommand(const ItlStateTransitionRequest &, bool = true); \
    void resumeScanCommand(); \
    bool scanCommandReplayPending(); \
    int postStateTransitionCommit(const ItlStateTransitionRequest &, int); \
    void recoverStateTransition(const ItlStateTransitionRequest &); \
    int drainStateTransitionCommit(IOInterruptEventSource *); \
    static void stateTransitionEvent(OSObject *, IOInterruptEventSource *, int)
#define FAMILY_OPERATIONS(prefix, soft) \
    void prefix##_add_task(soft *, taskq *q, task *t) { task_add(q, t); } \
    void prefix##_del_task(soft *, taskq *, task *) { assert(!leafDepth); } \
    int prefix##_run_stop(soft *) { return lower(1); } \
    int prefix##_deauth(soft *) { \
        const int result = lower(2); \
        if (result == 0) { primaryMacContext.clear(); primaryBindingContext.clear(); primaryStationContext.clear(); } \
        return result; \
    } \
    int prefix##_scan(soft *, const ItlStateTransitionRequest &request) { \
        const int error = lower(3); \
        if (!error && deferAtScan) { assert(deferScanCommand(request)); ++defers; } \
        return error; \
    } \
    int prefix##_auth(soft *) { return lower(4); } \
    int prefix##_run(soft *) { return lower(5); } \
    static void prefix##_newstate_task(void *); \
    static void prefix##_newstate_task_dispatch(void *); \
    static int prefix##_newstate(ieee80211com *, ieee80211_state, int); \
    static int _##prefix##_start_task(OSObject *, void *, void *, void *, void *) { return 0; } \
    int request(ieee80211_state state, int arg = -1) { \
        return prefix##_newstate(&com.sc_ic, state, arg); \
    } \
    void work() { assert(!loop.inGate()); prefix##_newstate_task_dispatch(&com); }
struct ItlIwm : TestHal {
    iwm_softc com;
    enum class Phase { Idle, InitialStarting };
    Phase wclScanPhase = Phase::Idle;
    uint64_t wclScanUpperGeneration = 0;
    bool wclScanPublicationInvalidated = false;
    STATE_DECLARATIONS;
    FAMILY_OPERATIONS(iwm, iwm_softc)
    void iwm_led_blink_stop(iwm_softc *) {}
};
struct ItlIwx : TestHal {
    iwx_softc com;
    using Phase = ItlIwm::Phase;
    Phase wclScanPhase = Phase::Idle;
    uint64_t wclScanUpperGeneration = 0;
    bool wclScanPublicationInvalidated = false;
    STATE_DECLARATIONS;
    FAMILY_OPERATIONS(iwx, iwx_softc)
    int iwx_rs_init(iwx_softc *, iwx_node *, bool) { return lower(6); }
    bool iwx_task_gate_enter(iwx_softc *sc, bool allowClosed) { return enter(sc, allowClosed); }
    void iwx_task_gate_leave(iwx_softc *sc) { leave(sc); }
};
using ItlIwmWclScanPhase = ItlIwm::Phase;
using ItlIwxWclScanPhase = ItlIwx::Phase;
#include "state-transition.inc"

template<class T> struct Fixture {
    T driver;
    Fixture() {
        auto &d = driver;
        d.com.owner = &d;
        d.com.sc_ic.testOwner = &d;
        d.com.sc_ic.ic_ac.ac_if.if_softc = &d.com;
        d.com.sc_ic.ic_bss = &d.node;
        d.com.sc_ic.ic_pae_selected_bss_lock = &d.selectedLock;
        d.com.sc_newstate = TestHal::generic;
        assert(d.scanCommand.reopen(0, d.com.sc_generation));
        assert(d.initStateTransitions());
    }
    ~Fixture() { driver.shutdownStateTransitions(); assert(!driver.com.active); }
};
template<class T> static void suite() {
    for (auto nextState : {IEEE80211_S_ASSOC,IEEE80211_S_RUN})
    for (bool superseded : {false,true}) {
        Fixture<T> f; auto &d = f.driver;
        d.com.sc_ic.ic_state = IEEE80211_S_RUN;
        assert(d.request(nextState,7) == 0);
        auto &station = d.primaryStationContext;
        station.owner = {19,d.com.sc_generation,{}};
        station.owner.identity.attempt = d.stateTransition.request.identity;
        station.stage = ItlFirmwareContextLease::Stage::Active;
        station.confirmed = true;
        assert(d.primaryStationUses.start(station.owner));
        ItlFirmwareContextReceipt use{};
        assert(d.primaryStationUses.acquire(station.owner,&use));
        d.work();
        assert(d.stateTransition.stage == ItlStateTransitionLease::Stage::Deferred);
        assert(d.primaryStationUses.closed && !d.primaryStationUses.retiring);
        assert(d.lowerCalls.empty());
        assert(d.primaryStationUses.release(&use));
        d.resumePrimaryStationUsers(); d.work();
        assert(d.primaryStationUses.closed);
        const auto oldRequest = d.stateTransition.request;
        if (superseded) {
            assert(d.request(IEEE80211_S_AUTH,9) == 0);
            d.reopenPrimaryStationUsers(oldRequest);
            assert(d.primaryStationUses.closed);
        } else {
            d.stateTransitionSource->deliver();
            assert(!d.primaryStationUses.closed);
            assert(d.primaryStationUses.acquire(station.owner,&use));
            assert(d.primaryStationUses.release(&use));
        }
        assert(!d.com.init_task.enqueues);
        ++physicalContextCases;
    }
    // Full production deferral/replay methods and workers. Reader storage is
    // a fixture here; packet construction itself is not simulated.
    for (auto nextState : {IEEE80211_S_INIT, IEEE80211_S_SCAN, IEEE80211_S_AUTH})
    for (int race = 0; race < 4; ++race) {
        Fixture<T> f; auto &d = f.driver;
        d.com.sc_ic.ic_state = IEEE80211_S_RUN;
        assert(d.request(nextState, 7) == 0);
        auto &station = d.primaryStationContext;
        station.owner = {19,d.com.sc_generation,{}};
        station.owner.identity.attempt = d.stateTransition.request.identity;
        station.stage = ItlFirmwareContextLease::Stage::Active;
        station.confirmed = true;
        assert(d.primaryStationUses.start(station.owner));
        ItlFirmwareContextReceipt use{};
        assert(d.primaryStationUses.acquire(station.owner,&use));
        const auto serial = d.stateTransition.request.serial;
        std::function<void()> exitAtDeferral;
        if (race == 1) {
            exitAtDeferral = [&] {
                if (d.stateTransition.stage != ItlStateTransitionLease::Stage::Deferred) {
                    unlocked = exitAtDeferral;
                    return;
                }
                assert(d.primaryStationUses.release(&use));
                // No exit notification: the post-deferral level check must wake it.
            };
            unlocked = exitAtDeferral;
        }
        d.work();
        assert(d.lowerCalls.empty() && !d.com.init_task.enqueues);
        assert(d.primaryStationUses.closed);
        if (race != 1) {
            assert(d.stateTransition.stage == ItlStateTransitionLease::Stage::Deferred);
            const auto enqueues = d.com.newstate_task.enqueues;
            d.resumeScanCommand(); // A scan terminal cannot resume station-reader wait.
            assert(d.com.newstate_task.enqueues == enqueues);
            if (race == 2)
                assert(d.request(IEEE80211_S_AUTH,9) == 0);
            if (race == 3) {
                ++d.com.sc_generation;
                d.scanCommand.open = false;
            }
            assert(d.primaryStationUses.release(&use));
            d.resumePrimaryStationUsers();
        }
        if (race == 3) {
            assert(d.stateTransition.stage == ItlStateTransitionLease::Stage::Empty);
            assert(d.lowerCalls.empty());
        } else {
            const auto expectedState = race == 2 ? IEEE80211_S_AUTH : nextState;
            assert(d.stateTransition.stage == ItlStateTransitionLease::Stage::Queued);
            assert((d.stateTransition.request.serial == serial) == (race != 2));
            const auto enqueues = d.com.newstate_task.enqueues;
            d.resumePrimaryStationUsers();
            assert(d.com.newstate_task.enqueues == enqueues);
            d.work();
            std::vector<int> expected{1,2};
            if (expectedState == IEEE80211_S_SCAN) expected.push_back(3);
            if (expectedState == IEEE80211_S_AUTH) expected.push_back(4);
            assert(d.lowerCalls == expected && !d.com.init_task.enqueues);
        }
        assert(!unlocked);
        ++physicalContextCases;
    }
    // RX firmware retirement can finish before its main-workloop publication.
    // EINPROGRESS is an exact continuation, never a failed join or INIT reset.
    // Exercise the complete state worker with the asynchronous RX boundary
    // represented by a real station reader, including both lost-wakeup edges.
    for (auto oldState : {IEEE80211_S_SCAN, IEEE80211_S_RUN})
    for (auto nextState : {IEEE80211_S_INIT, IEEE80211_S_SCAN, IEEE80211_S_AUTH})
    for (int race = 0; race < 4; ++race) {
        Fixture<T> f; auto &d = f.driver;
        d.com.sc_ic.ic_state = oldState;
        assert(d.request(nextState, 7) == 0);
        auto &station = d.primaryStationContext;
        station.owner = {19,d.com.sc_generation,{}};
        station.owner.identity.attempt = d.stateTransition.request.identity;
        station.stage = ItlFirmwareContextLease::Stage::Active;
        station.confirmed = true;
        assert(d.primaryStationUses.start(station.owner));
        const auto serial = d.stateTransition.request.serial;
        ItlFirmwareContextReceipt use{};
        std::function<void()> exitAtDeferral;
        d.lowerError = EINPROGRESS;
        d.lowerHook = [&] {
            assert(d.primaryStationUses.closed);
            assert(d.primaryStationUses.acquire(station.owner, &use, true));
            if (race == 1) {
                // Completion was already delivered before the worker returned.
                assert(d.primaryStationUses.release(&use));
            } else if (race == 2) {
                exitAtDeferral = [&] {
                    if (d.stateTransition.stage != ItlStateTransitionLease::Stage::Deferred) {
                        unlocked = exitAtDeferral;
                        return;
                    }
                    assert(d.primaryStationUses.release(&use));
                };
                unlocked = exitAtDeferral;
            } else if (race == 3) {
                assert(d.request(IEEE80211_S_AUTH, 9) == 0);
            }
        };
        d.work();
        assert(d.lowerCalls.size() == 1);
        assert(!d.commits && !d.failures && !d.com.init_task.enqueues);
        assert(!d.stateTransitionSource->signals);
        if (race == 0) {
            assert(d.stateTransition.stage == ItlStateTransitionLease::Stage::Deferred);
            assert(d.primaryStationUses.active == 1);
        }
        if (use.serial != 0) {
            assert(d.primaryStationUses.release(&use));
            d.resumePrimaryStationUsers();
        }
        assert(!unlocked && !d.primaryStationUses.active);
        assert(d.stateTransition.stage == ItlStateTransitionLease::Stage::Queued);
        assert((d.stateTransition.request.serial == serial) == (race != 3));
        d.lowerError = 0;
        d.work();
        const auto expectedState = race == 3 ? IEEE80211_S_AUTH : nextState;
        std::vector<int> expected;
        if (oldState == IEEE80211_S_RUN) expected = {1,1,2};
        else expected = {2,2};
        if (expectedState == IEEE80211_S_SCAN) expected.push_back(3);
        if (expectedState == IEEE80211_S_AUTH) expected.push_back(4);
        assert(d.lowerCalls == expected && !d.com.init_task.enqueues);
        d.stateTransitionSource->deliver();
        assert(!d.failures && !d.com.init_task.enqueues);
        if (expectedState != IEEE80211_S_SCAN) {
            assert(d.commits == 1 && d.com.sc_ic.ic_state == expectedState);
            assert(d.lastArgument == (race == 3 ? 9 : 7));
        }
        ++physicalContextCases;
    }
    for (auto oldState : {IEEE80211_S_INIT, IEEE80211_S_SCAN})
    for (auto nextState : {IEEE80211_S_INIT, IEEE80211_S_SCAN, IEEE80211_S_AUTH})
    for (int kind : {0,1,2})
    for (bool uncertain : {false,true}) {
        Fixture<T> f; auto &d = f.driver;
        d.com.sc_ic.ic_state = oldState;
        auto &context = kind == 2 ? d.primaryStationContext :
            kind == 1 ? d.primaryBindingContext : d.primaryMacContext;
        context.owner.serial = 19; context.owner.generation = d.com.sc_generation;
        context.stage = uncertain ? ItlFirmwareContextLease::Stage::Uncertain :
            ItlFirmwareContextLease::Stage::Active;
        assert(d.request(nextState) == 0);
        d.work();
        std::vector<int> expected{2};
        if (nextState == IEEE80211_S_SCAN) expected.push_back(3);
        if (nextState == IEEE80211_S_AUTH) expected.push_back(4);
        assert(d.lowerCalls == expected);
        assert(!d.primaryFirmwareContextsPresent());
        assert(d.stateTransition.request.lowerCompleted & ItlStateTransitionRequest::Deauthenticated);
        assert(!d.com.init_task.enqueues);
        ++physicalContextCases;
    }
    for (auto stage : {ItlFirmwareContextLease::Stage::Adding,
                       ItlFirmwareContextLease::Stage::Modifying,
                       ItlFirmwareContextLease::Stage::Removing}) {
        Fixture<T> f; auto &d = f.driver;
        d.primaryMacContext.stage = stage;
        assert(d.primaryFirmwareContextsPresent());
        d.primaryMacContext.clear();
        assert(!d.primaryFirmwareContextsPresent());
        ++physicalContextCases;
    }
    for (bool superseded : {false,true}) {
        Fixture<T> f; auto &d = f.driver;
        d.primaryMacContext.owner.serial = 20;
        d.primaryMacContext.owner.generation = d.com.sc_generation;
        d.primaryMacContext.stage = ItlFirmwareContextLease::Stage::Uncertain;
        assert(d.request(IEEE80211_S_AUTH,7) == 0);
        if (superseded)
            d.lowerHook = [&] { assert(d.request(IEEE80211_S_AUTH,8) == 0); };
        else
            d.lowerError = EIO;
        d.work(); d.stateTransitionSource->deliver();
        assert(d.lowerCalls == std::vector<int>{2});
        assert(!d.commits);
        if (superseded) {
            assert(!d.com.init_task.enqueues && !d.primaryFirmwareContextsPresent());
            d.work(); d.stateTransitionSource->deliver();
            assert(d.commits == 1 && d.lastArgument == 8);
            assert(d.lowerCalls == (std::vector<int>{2,4}));
        } else {
            assert(d.primaryFirmwareContextsPresent() && d.com.init_task.enqueues == 1);
            assert(!d.scanCommand.open);
        }
        ++physicalContextCases;
    }
    {
        Fixture<T> f; auto &d = f.driver;
        assert(d.request(IEEE80211_S_AUTH, 7) == 0);
        d.work();
        assert(d.commits == 0 && d.stateTransitionSource->signals == 1);
        d.stateTransitionSource->deliver();
        assert(d.commits == 1 && d.lastArgument == 7);
        auto copy = d.stateTransition.request;
        assert(d.postStateTransitionCommit(copy, 0) == ECANCELED);
        d.stateTransitionSource->deliver();
        d.work();
        assert(d.commits == 1 && d.lowerCalls.size() == 1);
    }
    for (unsigned boundary = 0; boundary < 5; ++boundary) {
        Fixture<T> f; auto &d = f.driver;
        assert(d.request(IEEE80211_S_AUTH, 7) == 0);
        auto prior = d.stateTransition.request;
        if (boundary == 0) assert(d.request(IEEE80211_S_AUTH, 8) == 0);
        if (boundary == 1) d.lowerHook = [&] { assert(d.request(IEEE80211_S_AUTH, 8) == 0); };
        d.work();
        if (boundary >= 2) {
            if (boundary == 2) assert(d.request(IEEE80211_S_AUTH, 8) == 0);
            if (boundary == 3) ++d.com.sc_ic.ic_pae_assoc_epoch;
            if (boundary == 4) ++d.com.sc_ic.ic_wcl_join_attempt.next_generation;
        }
        d.stateTransitionSource->deliver();
        assert(d.commits == (boundary == 0 ? 1U : 0U));
        assert(d.postStateTransitionCommit(prior, EIO) == ECANCELED);
        assert(!d.com.init_task.enqueues);
        if (boundary == 1 || boundary == 2) {
            d.work(); d.stateTransitionSource->deliver();
            assert(d.commits == 1 && d.lastArgument == 8);
        }
    }
    for (unsigned boundary = 0; boundary < 3; ++boundary) {
        Fixture<T> f; auto &d = f.driver;
        assert(d.request(IEEE80211_S_AUTH) == 0);
        if (boundary == 0) d.com.sc_flags |= IWM_FLAG_SHUTDOWN;
        if (boundary == 1) ++d.com.sc_generation;
        if (boundary == 2) d.stateTransition.invalidate();
        d.work(); d.stateTransitionSource->deliver();
        assert(!d.commits && d.lowerCalls.empty() && !d.com.init_task.enqueues);
    }
    for (unsigned boundary = 0; boundary < 4; ++boundary) {
        Fixture<T> f; auto &d = f.driver;
        assert(d.request(IEEE80211_S_AUTH) == 0);
        d.work();
        if (boundary == 0) d.com.sc_flags |= IWM_FLAG_SHUTDOWN;
        if (boundary == 1) ++d.com.sc_generation;
        if (boundary == 2) d.stateTransition.invalidate();
        if (boundary == 3) d.scanCommand.open = false;
        d.stateTransitionSource->deliver();
        assert(!d.commits && !d.com.init_task.enqueues);
    }
    {
        Fixture<T> f; auto &d = f.driver;
        assert(d.request(IEEE80211_S_AUTH) == 0);
        d.work();
        IOInterruptEventSource *old = d.stateTransitionSource;
        old->retain();
        d.shutdownStateTransitions();
        old->interruptOccurred(nullptr, nullptr, 0);
        old->deliver();
        assert(!d.commits && !old->workloop);
        assert(d.initStateTransitions());
        d.loop.gated = true;
        assert(d.drainStateTransitionCommit(old) == ECANCELED);
        d.loop.gated = false;
        old->release();
    }
    {
        Fixture<T> f; auto &d = f.driver;
        d.com.sc_ic.ic_state = IEEE80211_S_AUTH;
        assert(d.request(IEEE80211_S_AUTH, 22) == 0);
        d.loop.gated = true;
        assert(d.request(IEEE80211_S_ASSOC, 23) == 0);
        assert(d.commits == 1 && d.lastArgument == 23);
        d.loop.gated = false;
        d.work();
        assert(d.commits == 1);
        assert(d.request(IEEE80211_S_ASSOC, 23) == 0);
        assert(d.com.newstate_task.enqueues == 1);
        ++d.com.sc_ic.ic_wcl_join_attempt.next_generation;
        assert(d.request(IEEE80211_S_ASSOC, 23) == 0);
        assert(d.com.newstate_task.enqueues == 2);
        d.work(); d.stateTransitionSource->deliver();
        assert(d.commits == 2);
    }
    for (unsigned cause = 0; cause < 3; ++cause) {
        Fixture<T> f; auto &d = f.driver;
        d.lowerError = cause == 0 ? EIO : 0;
        d.genericError = cause != 0 ? EIO : 0;
        if (cause == 2) d.commitHook = [&] { assert(d.request(IEEE80211_S_AUTH, 5) == 0); };
        assert(d.request(IEEE80211_S_AUTH) == 0);
        d.work(); d.stateTransitionSource->deliver();
        assert(d.com.init_task.enqueues == (cause == 2 ? 0U : 1U));
        if (cause != 2) {
            assert(!d.scanCommand.open);
            assert(d.request(IEEE80211_S_AUTH) == ENXIO);
        }
    }
    {
        Fixture<T> f; auto &d = f.driver;
        d.lowerError = EIO;
        d.failureHook = [&] { assert(d.request(IEEE80211_S_AUTH, 5) == 0); };
        assert(d.request(IEEE80211_S_AUTH) == 0);
        d.work(); d.stateTransitionSource->deliver();
        assert(d.failures == 1 && !d.com.init_task.enqueues && d.scanCommand.open);
    }
    for (unsigned operation = 0; operation < 2; ++operation) {
        Fixture<T> f; auto &d = f.driver;
        d.com.sc_ic.ic_state = operation == 0 ? IEEE80211_S_RUN : IEEE80211_S_ASSOC;
        assert(d.request(IEEE80211_S_AUTH) == 0);
        d.lowerHook = [&] { assert(d.request(IEEE80211_S_AUTH, 8) == 0); };
        d.work();
        assert(d.lowerCalls.size() == 1 && !d.commits && !d.com.init_task.enqueues);
    }
    {
        Fixture<T> f; auto &d = f.driver;
        d.com.sc_ic.ic_state = IEEE80211_S_RUN;
        d.ampduHook = [&] { assert(d.request(IEEE80211_S_AUTH, 9) == 0); };
        assert(d.request(IEEE80211_S_SCAN) == ECANCELED);
        assert(d.com.newstate_task.enqueues == 1 && d.stateTransition.request.argument == 9);
    }
    {
        Fixture<T> f; auto &d = f.driver;
        assert(d.request(IEEE80211_S_SCAN, -100) == 0);
        assert(d.stateTransition.request.argument == -1);
        d.com.sc_flags |= IWM_FLAG_SCANNING;
        assert(d.scanCommand.reserve(d.com.sc_generation, 0, true, false, 0));
        d.work();
        assert(d.scanCommandReplayPending() && d.lowerCalls.empty() && !d.commits);
    }
    {
        Fixture<T> f; auto &d = f.driver;
        ItlStateTransitionRequest request{};
        assert(d.prepareStateTransition(IEEE80211_S_AUTH, 0, nullptr) == EINVAL);
        d.stateTransition.nextSerial = UINT64_MAX;
        assert(d.prepareStateTransition(IEEE80211_S_AUTH, 0, &request) == EOVERFLOW);
        assert(!request.serial);
        d.stateTransition.invalidate();
        assert(d.prepareStateTransition(IEEE80211_S_AUTH, 0, &request) == EOVERFLOW);
    }
    {
        Fixture<T> f; auto &d = f.driver;
        d.com.admission = false;
        assert(d.request(IEEE80211_S_AUTH) == ECANCELED);
        assert(!d.com.active && !d.com.newstate_task.enqueues);
    }
    {
        Fixture<T> f; auto &d = f.driver;
        ItlStateTransitionRequest request{}, taken{};
        assert(d.prepareStateTransition(IEEE80211_S_AUTH, 0, &request) == 0);
        assert(d.enqueueStateTransition(request));
        assert(!d.enqueueStateTransition(request));
        assert(d.takeStateTransition(&taken));
        assert(!d.takeStateTransition(&taken));
        assert(!d.enqueueStateTransition(request));
    }
    for (unsigned cause = 0; cause < 3; ++cause) {
        Fixture<T> f; auto &d = f.driver;
        d.shutdownStateTransitions();
        if (cause == 0) failAllocation = true;
        if (cause == 1) d.loop.failAdd = true;
        if (cause == 2) d.wclScanLock = nullptr;
        assert(!d.initStateTransitions() && !d.stateTransitionSource);
        failAllocation = false;
        d.wclScanLock = &d.scanLock;
        assert(!liveSources);
    }
    {
        Fixture<T> f; auto &d = f.driver;
        d.com.sc_ic.ic_state = IEEE80211_S_AUTH;
        assert(d.request(IEEE80211_S_ASSOC, 12) == 0);
        assert(!d.commits && d.lowerCalls.empty());
        d.work(); d.stateTransitionSource->deliver();
        assert(d.commits == 1 && d.lastArgument == 12);
        if (std::is_same<T, ItlIwx>::value)
            assert(d.lowerCalls == std::vector<int>{6});
        else
            assert(d.lowerCalls.empty());
    }
    {
        Fixture<T> f; auto &d = f.driver;
        assert(d.request(IEEE80211_S_AUTH) == 0);
        d.lowerError = EIO;
        d.work();
        d.lowerError = 0;
        d.com.sc_ic.ic_state = IEEE80211_S_AUTH;
        d.loop.gated = true;
        assert(d.request(IEEE80211_S_ASSOC) == 0);
        d.loop.gated = false;
        d.stateTransitionSource->deliver();
        assert(d.commits == 1 && !d.failures && !d.com.init_task.enqueues);
    }
    {
        Fixture<T> f; auto &d = f.driver;
        unlocked = [&] { ++d.com.sc_ic.ic_wcl_join_attempt.next_generation; };
        assert(d.request(IEEE80211_S_AUTH) == ECANCELED);
        assert(!d.com.newstate_task.enqueues && !d.commits);
    }
    {
        Fixture<T> f; auto &d = f.driver;
        d.com.sc_flags |= IWM_FLAG_SCANNING; // Stale flag, no physical owner.
        assert(d.request(IEEE80211_S_SCAN) == 0);
        d.work();
        assert(d.lowerCalls == std::vector<int>{3} && !d.scanCommandReplayPending());
    }
    {
        Fixture<T> f; auto &d = f.driver;
        d.com.sc_ic.ic_state = IEEE80211_S_RUN;
        d.com.sc_ic.ic_des_esslen = 4;
        memcpy(d.com.sc_ic.ic_des_essid, "join", 4);
        const auto background = d.scanCommand.reserve(d.com.sc_generation, 0, true, true, 0);
        assert(background && d.request(IEEE80211_S_SCAN) == 0);
        const auto accepted = d.stateTransition.request;
        d.deferAtScan = true;
        d.work();
        assert((d.lowerCalls == std::vector<int>{1, 2, 3}));
        assert(d.scanCommandReplayPending() && d.stateTransition.request.lowerCompleted == 3);
        assert(d.stateTransition.request.serial == accepted.serial);
        memcpy(d.com.sc_ic.ic_des_essid, "next", 4);
        assert(d.scanCommand.rejectUnsubmitted(background, d.com.sc_generation));
        d.resumeScanCommand();
        assert(d.stateTransition.stage == ItlStateTransitionLease::Stage::Queued);
        assert(d.com.newstate_task.enqueues == 2 && !d.com.active);
        assert(d.stateTransition.request.serial == accepted.serial);
        assert(d.stateTransition.request.scanHomeAwayMs == accepted.scanHomeAwayMs);
        assert(!memcmp(d.stateTransition.request.scanSsid, "join", 4));
        d.deferAtScan = false;
        d.work();
        assert((d.lowerCalls == std::vector<int>{1, 2, 3, 3}));
        d.resumeScanCommand();
        assert(d.com.newstate_task.enqueues == 2); // No duplicate resume.
    }
    {
        Fixture<T> f; auto &d = f.driver;
        const auto physical = d.scanCommand.reserve(d.com.sc_generation, 0, true, false, 0);
        assert(physical && d.request(IEEE80211_S_SCAN) == 0);
        d.work();
        auto old = d.stateTransition.request;
        assert(d.scanCommandReplayPending());
        assert(d.request(IEEE80211_S_AUTH, 27) == 0);
        assert(d.scanCommand.rejectUnsubmitted(physical, d.com.sc_generation));
        d.resumeScanCommand();
        assert(!d.noteStateTransitionProgress(&old, ItlStateTransitionRequest::RunStopped));
        assert(d.com.newstate_task.enqueues == 2);
        assert(d.stateTransition.request.lowerCompleted == 0);
        d.work(); d.stateTransitionSource->deliver();
        assert(d.commits == 1 && d.lastArgument == 27);
        assert(d.lowerCalls == std::vector<int>{4});
    }
    for (unsigned stale = 0; stale != 4; ++stale) {
        Fixture<T> f; auto &d = f.driver;
        const auto physical = d.scanCommand.reserve(d.com.sc_generation, 0, true, false, 0);
        assert(physical && d.request(IEEE80211_S_SCAN) == 0);
        d.work();
        assert(d.scanCommandReplayPending());
        assert(d.scanCommand.rejectUnsubmitted(physical, d.com.sc_generation));
        if (stale == 0) ++d.com.sc_ic.ic_pae_assoc_epoch;
        if (stale == 1) ++d.com.sc_ic.ic_wcl_join_attempt.next_generation;
        if (stale == 2) ++d.com.sc_generation;
        if (stale == 3) d.com.sc_ic.ic_if.if_flags = 0;
        d.resumeScanCommand();
        assert(!d.scanCommandReplayPending() && !d.stateTransition.request.serial);
        assert(d.com.newstate_task.enqueues == 1 && d.lowerCalls.empty());
    }
    {
        Fixture<T> f; auto &d = f.driver;
        d.apFence = true;
        assert(d.request(IEEE80211_S_SCAN) == 0);
        // AP release occurred after the observer's sample but before defer.
        d.apFenceHook = [&] { d.apFence = false; };
        d.work();
        assert(!d.scanCommandReplayPending() && d.com.newstate_task.enqueues == 2);
        assert(d.stateTransition.stage == ItlStateTransitionLease::Stage::Queued);
        d.work();
        assert(d.lowerCalls == std::vector<int>{3});
    }
    {
        Fixture<T> f; auto &d = f.driver;
        d.com.sc_ic.ic_state = IEEE80211_S_RUN;
        d.apFence = true;
        assert(d.request(IEEE80211_S_SCAN) == 0);
        d.work();
        assert(d.scanCommandReplayPending() && d.lowerCalls.empty());
        assert(d.stateTransition.request.lowerCompleted == 0);
        d.apFence = false; d.resumeScanCommand(); d.work();
        assert((d.lowerCalls == std::vector<int>{1, 2, 3}));
    }
    {
        Fixture<T> f; auto &d = f.driver;
        const auto physical = d.scanCommand.reserve(d.com.sc_generation, 0, true, false, 0);
        assert(physical && d.request(IEEE80211_S_SCAN) == 0);
        d.work();
        assert(d.scanCommand.rejectUnsubmitted(physical, d.com.sc_generation));
        d.com.admission = false; d.resumeScanCommand();
        assert(d.scanCommandReplayPending() && d.com.newstate_task.enqueues == 1);
        d.com.admission = true; d.resumeScanCommand(); d.work();
        assert(d.lowerCalls == std::vector<int>{3} && !d.com.active);
    }
    for (bool cancelled : {false, true}) {
        Fixture<T> f; auto &d = f.driver;
        const auto physical = d.scanCommand.reserve(d.com.sc_generation, 0, true, false, 0);
        d.wclScanPhase = T::Phase::InitialStarting;
        d.wclScanUpperGeneration = 77;
        d.com.sc_ic.ic_wcl_scan_plan.active = 1;
        d.com.sc_ic.ic_wcl_scan_plan.generation = 77;
        assert(physical && d.request(IEEE80211_S_SCAN) == 0);
        const auto serial = d.stateTransition.request.serial;
        d.work();
        assert(d.scanCommandReplayPending() && d.wclScanUpperGeneration == 77);
        assert(d.scanCommand.rejectUnsubmitted(physical, d.com.sc_generation));
        d.wclScanPublicationInvalidated = cancelled;
        d.resumeScanCommand();
        assert(!d.scanCommandReplayPending());
        if (cancelled) {
            assert(!d.stateTransition.request.serial && d.com.newstate_task.enqueues == 1);
        } else {
            assert(d.stateTransition.request.serial == serial);
            assert(d.stateTransition.request.scanGeneration == 77);
            d.work();
            assert(d.lowerCalls == std::vector<int>{3});
        }
    }
}
int main(int argc, char **argv) {
    if (argc > 1 && std::strcmp(argv[1], "iwm") == 0) {
        suite<ItlIwm>();
        assert(!liveSources && !leafDepth);
        return 0;
    }
    if (argc > 1 && std::strcmp(argv[1], "iwx") == 0) {
        suite<ItlIwx>();
        assert(!liveSources && !leafDepth);
        return 0;
    }
    suite<ItlIwm>();
    suite<ItlIwx>();
    {
        Fixture<ItlIwm> f; auto &d = f.driver;
        d.com.closed = true; // Initial SCAN must precede SAE TX admission.
        assert(d.request(IEEE80211_S_SCAN) == 0);
        d.work();
        assert(d.lowerCalls == std::vector<int>{3} && !d.com.active);
    }
    for (bool detaching : {false, true}) {
        Fixture<ItlIwm> f; auto &d = f.driver;
        assert(d.request(IEEE80211_S_AUTH) == 0);
        d.com.closed = true;
        d.com.admission = !detaching;
        d.scanCommand.open = false;
        d.work();
        assert(d.lowerCalls.empty() && !d.com.active);
    }
    {
        Fixture<ItlIwx> f; auto &d = f.driver;
        d.com.sc_ic.ic_state = IEEE80211_S_AUTH;
        d.loop.gated = true;
        d.lowerHook = [&] { assert(d.request(IEEE80211_S_AUTH, 8) == 0); };
        assert(d.request(IEEE80211_S_ASSOC) == ECANCELED);
        assert(!d.commits && !d.com.init_task.enqueues &&
            d.stateTransition.request.argument == 8);
    }
    assert(!liveSources && !leafDepth);
    std::printf("actual IWM/IWX queued-state identity, deferred replay and asynchronous commit: PASS (%u scenario groups)\n",
        92 + physicalContextCases);
}
