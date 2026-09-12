// Complete production ingress, worker, scan_start, lease, pre/post-doorbell,
// terminal/reset/abort and real upper reducer. Command construction, register
// write, scheduler and unrelated net80211 operations are explicit doubles.
// The complete command sender is separately tested by test_iwn_scan_abort_owner.
// These source executions do not replace the controlled radio reproduction.
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <utility>
#include <sys/time.h>
#include "kernel_memory_test_support.hpp"
#include "AirportItlwm/TahoeWclPhysicalScanContracts.hpp"
namespace upper = TahoeWclPhysicalScanContracts;
using u_int8_t = uint8_t;
using u_int16_t = uint16_t;
using u_int32_t = uint32_t;
using u_int64_t = uint64_t;
using IOReturn = uint32_t;
using IOInterruptState = int;
constexpr IOReturn kIOReturnSuccess = 0, kIOReturnBadArgument = 0xe00002c2,
    kIOReturnBusy = 0xe00002d5, kIOReturnAborted = 0xe00002eb,
    kIOReturnNotReady = 0xe00002d8, kIOReturnError = 0xe00002bc;
constexpr unsigned IWN_FLAG_SCANNING = 1, IWN_FLAG_BGSCAN = 2,
    IWN_FLAG_HAS_5GHZ = 4, IWN_FLAG_FATAL_RECOVERY = 8;
constexpr unsigned IFF_UP = 1, IFF_RUNNING = 2;
constexpr unsigned IEEE80211_F_RSNON = 1, IEEE80211_F_BGSCAN = 2,
    IEEE80211_F_DISABLE_BG_AUTO_CONNECT = 4, IEEE80211_F_AUTO_JOIN = 8;
constexpr int IEEE80211_M_STA = 1, IEEE80211_CHAN_MAX = 255;
constexpr uint16_t IEEE80211_CHAN_2GHZ = 0x80, IEEE80211_CHAN_5GHZ = 0x100;
constexpr int IEEE80211_EVT_WCL_SCAN_STARTED = 1, IEEE80211_EVT_WCL_SCAN_START_REJECTED = 2;
constexpr int IEEE80211_SAE_WCL_REQUEST_RESUME_STARTED = 1, IEEE80211_SAE_WCL_REQUEST_RESUME_DEFERRED = 2;
enum ieee80211_state { IEEE80211_S_INIT, IEEE80211_S_SCAN, IEEE80211_S_RUN };
struct IOSimpleLock { bool held = false; unsigned rank = 2; };
static IOSimpleLock *lock_stack[2];
static unsigned locks_held;
static void IOSimpleLockLock(IOSimpleLock *lock) {
    assert(lock && !lock->held && locks_held < 2);
    assert(!locks_held || lock_stack[locks_held - 1]->rank < lock->rank);
    lock_stack[locks_held++] = lock; lock->held = true;
}
static void IOSimpleLockUnlock(IOSimpleLock *lock) {
    assert(lock && lock->held && locks_held && lock_stack[locks_held - 1] == lock);
    --locks_held; lock->held = false;
}
static IOInterruptState IOSimpleLockLockDisableInterrupt(IOSimpleLock *lock) {
    IOSimpleLockLock(lock); return 0;
}
static void IOSimpleLockUnlockEnableInterrupt(IOSimpleLock *lock, IOInterruptState) { IOSimpleLockUnlock(lock); }
#include "types.inc"
struct ieee80211_node { bool ni_port_valid = true; };
struct ieee80211_channel { uint16_t ic_flags = 0; uint8_t number = 0; };
struct ieee80211_wcl_scan_plan {
    uint64_t generation = 408;
    bool channel_filter = true;
    bool allowed[IEEE80211_CHAN_MAX + 1]{};
};
struct ieee80211_wcl_scan_started { uint64_t generation; uint32_t backend_generation; };
struct ieee80211_wcl_scan_start_rejected { uint64_t generation; uint32_t backend_generation; };
struct ieee80211_wcl_scan_invalidation { uint64_t generation; uint32_t backend_generation; };
struct ieee80211_standard_scan_invalidation { uint64_t generation; uint32_t backend_generation; };
struct ieee80211com {
    struct { unsigned if_flags = IFF_UP | IFF_RUNNING; } ic_if;
    ieee80211_state ic_state = IEEE80211_S_RUN;
    unsigned ic_opmode = IEEE80211_M_STA, ic_mgt_timer = 0;
    unsigned ic_flags = IEEE80211_F_RSNON, ic_wcl_scan_active = 0, ic_des_esslen = 0;
    time_t ic_last_cache_scan_ts = 100;
    uint64_t ic_pae_assoc_epoch = 7, ic_wcl_reassoc_owner_serial = 31, ic_wcl_reassoc_source_epoch = 7;
    bool ic_wcl_reassoc_owner_active = false;
    IOSimpleLock *ic_pae_selected_bss_lock = nullptr;
    ieee80211_node node;
    ieee80211_node *ic_bss = &node;
    ieee80211_channel ic_channels[IEEE80211_CHAN_MAX + 1];
    ieee80211_wcl_scan_plan plan;
    void (*ic_event_handler)(ieee80211com *, int, void *) = nullptr;
    void *driver = nullptr;
};
struct task {};
struct iwn_softc {
    ieee80211com sc_ic;
    IOSimpleLock lock, selected_lock{false, 1};
    IOSimpleLock *sc_scan_lease_lock = &lock;
    iwn_scan_lease sc_scan_lease{};
    iwn_wcl_initial_scan_pending sc_wcl_initial_scan_pending{};
    uint64_t sc_scan_lease_next_serial = 471;
    unsigned sc_flags = IWN_FLAG_HAS_5GHZ;
    bool sc_ap_transition_scan_blocked = false;
    uint64_t sc_sae_join_scan_block_generation = 0, sc_sae_bss_loss_join_handoff_generation = 0;
    bool sc_sae_wcl_admission_reserved = false, sc_sae_wcl_admission_requires_fresh_scan = false;
    bool sc_scan_lease_replay_pending = false, sc_scan_lease_replay_task_ready = true;
    ieee80211_state sc_scan_lease_replay_nstate = IEEE80211_S_INIT;
    int sc_scan_lease_replay_arg = -1;
    uint64_t sc_scan_lease_replay_sae_generation = 0, sc_wcl_join_cleanup_generation = 0;
    task init_task;
    void *driver = nullptr;
};
struct ItlIwn {
    iwn_softc com;
    upper::State state{};
    unsigned writes = 0, starts = 0, rejects = 0, terminals = 0, aborts = 0;
    uint16_t last_band = 0;
    bool auth_pending = false;
    std::function<void(ItlIwn &)> during_build, after_doorbell;
    int build_error = 0, post_error = 0;
    ItlIwn();
    IOReturn beginWclBackgroundScan(uint64_t, uint32_t *);
    IOReturn beginWclBackgroundScanAfterRoam(uint64_t, uint64_t, uint64_t, uint32_t *);
    IOReturn abortWclBackgroundScan(uint64_t);
    void invalidateWclBackgroundScan();
    static void iwn_scan_lease_replay_task(void *);
    bool iwn_auth_beacon_pending() { return auth_pending; }
    int iwn_scan_start(iwn_softc *, uint16_t, int, iwn_scan_lease_owner,
                       uint64_t, uint64_t, uint32_t *, uint64_t, uint64_t = 0);
    int iwn_scan_submit(iwn_softc *, uint16_t, int, uint64_t, bool, bool, bool,
                       uint64_t, uint32_t, bool *, bool *);
    int iwn_scan_abort_command(iwn_softc *sc, uint64_t serial) {
        assert(sc->sc_scan_lease.serial == serial);
        sc->sc_scan_lease.abort_submitted = true; ++aborts; return 0;
    }
    static int iwn_newstate_impl(ieee80211com *, ieee80211_state, int, uint64_t) { assert(false); return EINVAL; }
    void releaseSaeWclCredentialAdmission() { assert(false); }
};
#define container_of(ptr, type, member) static_cast<type *>((ptr)->driver)
#define XYLog(...) ((void)0)
static unsigned replay_wakes, resets;
static void *systq;
static void task_add(void *, task *) { assert(locks_held == 0); ++resets; }
static void iwn_scan_lease_schedule_replay_task(iwn_softc *) { assert(locks_held == 0); ++replay_wakes; }
static bool iwn_rsn_join_scan_blocked(ieee80211com *) { return false; }
static int ieee80211_wnm_bss_transition_scan_owns_admission(ieee80211com *) { return 0; }
static bool btm_pending;
static bool ieee80211_wnm_bss_transition_fresh_scan_pending(ieee80211com *) { return btm_pending; }
static int ieee80211_wnm_bss_transition_target_channel(ieee80211com *, uint8_t *) { return 0; }
static bool ieee80211_sae_wcl_request_scan_selection_held(ieee80211com *) { return false; }
static bool ieee80211_sae_wcl_request_scan_selection_owned(ieee80211com *) { return false; }
static uint64_t ieee80211_wcl_join_scan_generation(ieee80211com *) { return 0; }
static bool ieee80211_wcl_reassoc_current(ieee80211com *, uint64_t) { return true; }
static bool ieee80211_wcl_join_failure_pending(ieee80211com *, uint64_t) { assert(false); return false; }
static void ieee80211_pae_assoc_epoch_note_newstate(ieee80211com *, ieee80211_state, int) { assert(false); }
static void iwn_sae_engine_request_join_retirement(iwn_softc *, uint64_t) { assert(false); }
static int ieee80211_sae_wcl_request_resume_scan(ieee80211com *, uint64_t) { assert(false); return 0; }
static void ieee80211_new_state(ieee80211com *, ieee80211_state, int) { assert(false); }
static void microtime(timeval *tv) { tv->tv_sec = 200; tv->tv_usec = 0; }
static void ieee80211_free_allnodes(ieee80211com *, int) { assert(locks_held == 0); }
static void AirportItlwmPostPltiTraceRecordWclPhysicalScan(ieee80211com *, int) {}
constexpr int kAirportItlwmPostPltiTraceEventWclPhysicalScanLowerLeaseReserved = 1;
static int ieee80211_wcl_scan_plan_snapshot(ieee80211com *ic, ieee80211_wcl_scan_plan *plan) { *plan = ic->plan; return 1; }
static bool ieee80211_wcl_scan_plan_channel_allowed(ieee80211com *,
    const ieee80211_wcl_scan_plan *plan, const ieee80211_channel *channel) {
    return !plan->channel_filter || plan->allowed[channel->number];
}
static int iwn_wcl_scan_initial_band(iwn_softc *, uint16_t *);
static bool iwn_scan_lease_mark_abort(iwn_softc *, iwn_scan_lease_owner, uint64_t, uint64_t *, bool *, uint64_t = 0);
static void iwn_scan_lease_abort_submission_failed(iwn_softc *, uint64_t) { assert(false); }
#include "production.inc"

static void event(ieee80211com *ic, int code, void *arg) {
    auto &d = *static_cast<ItlIwn *>(ic->driver);
    if (code == IEEE80211_EVT_WCL_SCAN_STARTED) {
        auto *value = static_cast<ieee80211_wcl_scan_started *>(arg);
        assert(d.writes > 0 && d.com.lock.held);
        assert(value->generation == 408 && value->backend_generation != 0);
        assert(upper::activate(&d.state, value->generation, value->backend_generation) != upper::StartDisposition::Lost);
        ++d.starts;
    } else {
        assert(code == IEEE80211_EVT_WCL_SCAN_START_REJECTED && locks_held == 0);
        auto *value = static_cast<ieee80211_wcl_scan_start_rejected *>(arg);
        assert(value->generation == 408 && value->backend_generation == 0);
        assert(upper::rejectInitialStart(&d.state, value->generation, 0)); ++d.rejects;
    }
}
ItlIwn::ItlIwn() {
    assert(locks_held == 0);
    com.driver = this; com.sc_ic.driver = this;
    com.sc_ic.ic_pae_selected_bss_lock = &com.selected_lock;
    com.sc_ic.ic_event_handler = event;
    com.sc_ic.ic_channels[9] = {IEEE80211_CHAN_2GHZ, 9};
    com.sc_ic.ic_channels[153] = {IEEE80211_CHAN_5GHZ, 153};
    com.sc_ic.plan.allowed[9] = true;
    state.nextGeneration = 407; uint64_t generation = 0;
    assert(upper::reserve(&state, &generation) && generation == 408);
    replay_wakes = resets = 0; btm_pending = false;
}
int ItlIwn::iwn_scan_submit(iwn_softc *sc, uint16_t band, int bg,
    uint64_t serial, bool foreground, bool publish_started, bool wcl,
    uint64_t generation, uint32_t backend, bool *attempted, bool *prepared)
{
    assert(!foreground && bg == 1 && wcl && locks_held == 0);
    assert(generation == sc->sc_ic.plan.generation);
    assert(iwn_wcl_scan_plan_has_eligible_band(sc, band));
    *attempted = *prepared = false;
    auto action = std::move(during_build); during_build = {};
    if (action) action(*this);
    if (build_error) return build_error;
    iwn_scan_doorbell_context context{};
    context.serial = serial; context.background = true;
    context.upper_generation = generation; context.backend_generation = backend;
    context.publish_wcl_initial_started = publish_started;
    if (!iwn_scan_lease_prepare_doorbell(sc, &context)) return ECANCELED;
    assert(sc->lock.held && sc->sc_scan_lease.command_submitted);
    if (publish_started) assert(sc->selected_lock.held);
    ++writes; last_band = band; // Register write is the hardware boundary double.
    iwn_scan_lease_finish_doorbell(sc, &context);
    *attempted = context.committed; assert(locks_held == 0);
    action = std::move(after_doorbell); after_doorbell = {};
    if (action) action(*this);
    return post_error;
}
static void active_roam(ItlIwn &d, bool aborted = true) {
    auto &sc = d.com;
    sc.sc_scan_lease.serial = 471; sc.sc_scan_lease.reassoc_serial = 31;
    sc.sc_scan_lease.owner = IWN_SCAN_LEASE_GENERIC_BACKGROUND;
    sc.sc_scan_lease.phase = IWN_SCAN_LEASE_ACTIVE;
    sc.sc_scan_lease.command_submitted = true; sc.sc_flags |= IWN_FLAG_SCANNING;
    if (aborted) {
        uint64_t serial = 0; bool submit = false;
        assert(iwn_scan_lease_mark_abort(&sc, IWN_SCAN_LEASE_NONE, 0, &serial, &submit, 31));
        assert(serial == 471 && submit); sc.sc_scan_lease.abort_submitted = true;
    }
}
static void queue(ItlIwn &d, bool setter_return = true) {
    uint32_t backend = 99;
    assert(d.beginWclBackgroundScanAfterRoam(408, 31, 7, &backend) == 0);
    assert(backend == 0 && d.writes == 0);
    const auto &p = d.com.sc_wcl_initial_scan_pending;
    assert(p.queued && p.background && !p.launching && !p.command_started);
    assert(p.upper_generation == 408 && p.generic_serial == 471);
    assert(p.source_epoch == 7 && p.superseded_reassoc_serial == 31);
    if (setter_return) assert(upper::queueInitialStart(&d.state, 408) == upper::StartDisposition::Active);
}
static bool finish(ItlIwn &d, bool old) {
    auto &sc = d.com; iwn_scan_lease_terminal terminal{};
    assert(iwn_scan_lease_claim_terminal(&sc, &terminal));
    assert(terminal.wcl != old);
    if (old) (void)iwn_wcl_initial_scan_claim_generic_terminal(&sc, &terminal);
    else {
        assert(terminal.upper_generation == 408 && d.starts == 1);
        assert(upper::claimCompletion(&d.state, terminal.upper_generation, terminal.backend_generation,
            terminal.aborted ? 1 : 0) == upper::CompletionDisposition::Publish); ++d.terminals;
    }
    sc.sc_flags &= ~(IWN_FLAG_SCANNING | IWN_FLAG_BGSCAN);
    sc.sc_ic.ic_flags &= ~(IEEE80211_F_BGSCAN | IEEE80211_F_DISABLE_BG_AUTO_CONNECT);
    sc.sc_ic.ic_wcl_scan_active = 0;
    bool retired = false;
    const bool replay = iwn_scan_lease_finish_terminal(&sc, terminal.serial, &retired);
    assert(retired && !iwn_scan_lease_live_locked(&sc)); return replay;
}
static void worker(ItlIwn &d) { ItlIwn::iwn_scan_lease_replay_task(&d.com); assert(locks_held == 0); }
static void reset_radio(ItlIwn &d) {
    ieee80211_wcl_scan_invalidation wcl{}; ieee80211_standard_scan_invalidation standard{};
    uint64_t rejected = 0;
    const auto owner = iwn_scan_lease_begin_hardware_invalidation(&d.com, &wcl, &standard, &rejected);
    if (rejected) {
        ieee80211_wcl_scan_start_rejected e{rejected, 0}; event(&d.com.sc_ic, IEEE80211_EVT_WCL_SCAN_START_REJECTED, &e);
    }
    if (owner != IWN_SCAN_LEASE_NONE) {
        assert(owner == IWN_SCAN_LEASE_WCL_BACKGROUND && d.starts == 1);
        assert(wcl.generation == 408 && wcl.backend_generation != 0);
    }
    iwn_scan_lease_retire_after_hardware_stop(&d.com);
    d.com.sc_flags &= ~(IWN_FLAG_SCANNING | IWN_FLAG_BGSCAN);
}
int main(int argc, char **argv) {
    assert(argc == 1 || (argc == 2 && std::strcmp(argv[1], "require-handoff") == 0));
    unsigned cases = 0;
    {
        ItlIwn d; uint32_t backend = 0;
        assert(d.beginWclBackgroundScan(408, &backend) == 0);
        assert(backend && d.writes == 1 && d.starts == 0); ++cases;
    }
    for (unsigned shape = 0; shape < 12; ++shape) {
        ItlIwn d; active_roam(d, shape != 0); uint32_t backend = 99;
        if (shape == 1) d.com.sc_scan_lease.reassoc_serial++;
        if (shape == 2) d.com.sc_scan_lease.abort_submitted = false;
        if (shape == 3) d.com.sc_scan_lease.owner = IWN_SCAN_LEASE_GENERIC_FOREGROUND;
        if (shape == 4) d.com.sc_ic.ic_pae_assoc_epoch++;
        if (shape == 5) d.com.sc_ic.ic_bss->ni_port_valid = false;
        if (shape == 6) d.com.sc_ap_transition_scan_blocked = true;
        if (shape == 7) d.com.sc_sae_wcl_admission_reserved = true;
        if (shape == 8) d.com.sc_scan_lease_replay_task_ready = false;
        if (shape == 9) btm_pending = true;
        if (shape == 10) d.auth_pending = true;
        if (shape == 11) d.com.sc_ic.ic_wcl_reassoc_owner_active = true;
        assert(d.beginWclBackgroundScanAfterRoam(408, 31, 7, &backend) == kIOReturnBusy);
        assert(backend == 0 && d.writes == 0 && !d.com.sc_wcl_initial_scan_pending.queued); ++cases;
    }
    for (unsigned band = 0; band < 2; ++band) {
        ItlIwn d; active_roam(d);
        d.com.sc_ic.plan.allowed[9] = band == 0; d.com.sc_ic.plan.allowed[153] = band != 0;
        queue(d); worker(d); assert(d.writes == 0);
        assert(finish(d, true)); worker(d);
        assert(d.writes == 1 && d.starts == 1 && d.rejects == 0);
        assert(d.last_band == (band ? IEEE80211_CHAN_5GHZ : IEEE80211_CHAN_2GHZ));
        assert(d.com.sc_scan_lease.serial == 472 && d.com.sc_scan_lease.upper_generation == 408);
        assert(!d.com.sc_wcl_initial_scan_pending.queued);
        assert(!finish(d, false)); worker(d); assert(d.terminals == 1 && d.writes == 1); ++cases;
    }
    { // Predecessor terminal before admission: normal immediate reservation.
        ItlIwn d; active_roam(d); assert(!finish(d, true)); uint32_t backend = 0;
        assert(d.beginWclBackgroundScanAfterRoam(408, 31, 7, &backend) == 0);
        assert(backend && d.writes == 1); ++cases;
    }
    { // Queue between the early STOP_SCAN handoff check and physical release.
        ItlIwn d; active_roam(d); iwn_scan_lease_terminal terminal{};
        assert(iwn_scan_lease_claim_terminal(&d.com, &terminal));
        assert(!iwn_wcl_initial_scan_claim_generic_terminal(&d.com, &terminal));
        queue(d); d.com.sc_flags &= ~IWN_FLAG_SCANNING;
        assert(iwn_scan_lease_finish_terminal(&d.com, 471, nullptr));
        worker(d); assert(d.writes == 1 && d.starts == 1); ++cases;
    }
    { // Never-doorbelled predecessor uses rollback, not a made-up terminal.
        ItlIwn d; active_roam(d); d.com.sc_scan_lease.phase = IWN_SCAN_LEASE_ARMING;
        d.com.sc_scan_lease.command_submitted = d.com.sc_scan_lease.abort_submitted = false;
        d.com.sc_flags &= ~IWN_FLAG_SCANNING;
        queue(d); assert(iwn_scan_lease_rollback(&d.com, 471)); worker(d); assert(d.starts == 1); ++cases;
    }
    { // Worker and new terminal both precede original setWCL_SCAN_REQ return.
        ItlIwn d; active_roam(d); queue(d, false); assert(finish(d, true));
        d.after_doorbell = [](ItlIwn &v) { (void)finish(v, false); }; worker(d);
        assert(upper::queueInitialStart(&d.state, 408) == upper::StartDisposition::TerminalPending);
        assert(d.starts == 1 && d.terminals == 1 && d.rejects == 0); ++cases;
    }
    for (unsigned shape = 0; shape < 8; ++shape) {
        ItlIwn d; active_roam(d); queue(d); assert(finish(d, true));
        if (shape == 0) d.com.sc_ic.ic_pae_assoc_epoch++;
        if (shape == 1) d.com.sc_ic.ic_bss->ni_port_valid = false;
        if (shape == 2) d.com.sc_ic.ic_mgt_timer = 1;
        if (shape == 3) d.com.sc_ic.ic_wcl_reassoc_owner_active = true;
        if (shape == 4) d.com.sc_ap_transition_scan_blocked = true;
        if (shape == 5) btm_pending = true;
        if (shape == 6) d.auth_pending = true;
        if (shape == 7) d.com.sc_ic.plan.allowed[9] = false;
        worker(d); assert(d.writes == 0 && d.rejects == 1 && d.state.phase == upper::Phase::Idle);
        assert(!d.com.sc_wcl_initial_scan_pending.queued); ++cases;
    }
    for (unsigned shape = 0; shape < 11; ++shape) {
        ItlIwn d; active_roam(d); queue(d); assert(finish(d, true));
        d.during_build = [shape](ItlIwn &v) {
            if (shape == 0) { ++v.com.sc_ic.ic_pae_assoc_epoch; v.com.sc_ic.ic_flags = 0x100; }
            if (shape == 1) v.com.sc_ic.ic_bss->ni_port_valid = false;
            if (shape == 2) v.com.sc_ic.ic_wcl_reassoc_owner_active = true;
            if (shape == 3) v.com.sc_ic.ic_mgt_timer = 1;
            if (shape == 4) v.build_error = ENOMEM;
            if (shape == 5) { v.invalidateWclBackgroundScan(); upper::reset(&v.state); }
            if (shape == 6) reset_radio(v);
            if (shape == 7) v.com.sc_ap_transition_scan_blocked = true;
            if (shape == 8) v.com.sc_sae_join_scan_block_generation = 90;
            if (shape == 9) v.com.sc_scan_lease_replay_task_ready = false;
            if (shape == 10) v.com.sc_ic.ic_if.if_flags = 0;
        };
        worker(d); assert(d.writes == 0 && d.starts == 0 && d.rejects == (shape == 5 ? 0U : 1U));
        assert(!d.com.sc_wcl_initial_scan_pending.queued);
        if (shape == 0) assert(d.com.sc_ic.ic_flags == 0x100); ++cases;
        if (shape == 6) assert(resets == 0); // Reset already retired this never-submitted worker.
    }
    for (unsigned timing = 0; timing < 3; ++timing) {
        ItlIwn d; active_roam(d); queue(d);
        auto cancel = [](ItlIwn &v) {
            uint64_t generation = 0; assert(upper::markAborting(&v.state, &generation) && generation == 408);
            assert(v.abortWclBackgroundScan(generation) == 0);
        };
        if (timing == 0) cancel(d);
        assert(finish(d, true) == (timing != 0));
        if (timing == 1) d.during_build = cancel;
        if (timing == 2) d.after_doorbell = cancel;
        worker(d);
        if (timing < 2) assert(d.writes == 0 && d.starts == 0 && d.rejects == 1);
        else { assert(d.writes == 1 && d.starts == 1 && d.aborts == 1); (void)finish(d, false); }
        ++cases;
    }
    { // Upper queued abort races with STARTED, before lower abort invocation.
        ItlIwn d; active_roam(d); queue(d); assert(finish(d, true));
        uint64_t generation = 0; assert(upper::markAborting(&d.state, &generation));
        worker(d); assert(d.state.phase == upper::Phase::Aborting && d.starts == 1);
        assert(d.abortWclBackgroundScan(generation) == 0); (void)finish(d, false); ++cases;
    }
    for (unsigned timing = 0; timing < 2; ++timing) {
        ItlIwn d; active_roam(d); queue(d);
        if (timing == 0) { reset_radio(d); worker(d); assert(d.rejects == 1 && d.writes == 0); }
        else { assert(finish(d, true)); d.after_doorbell = reset_radio; worker(d); assert(d.starts == 1 && d.rejects == 0); }
        ++cases;
    }
    { // Post-WRPTR error remains owned by physical terminal/reset.
        ItlIwn d; active_roam(d); queue(d); assert(finish(d, true)); d.post_error = EIO;
        worker(d); assert(d.starts == 1 && d.rejects == 0 && resets == 1);
        assert(iwn_scan_lease_live_locked(&d.com)); ++cases;
    }
    { // Obsolete no-doorbell worker must not request a reset of its successor.
        ItlIwn d; active_roam(d); queue(d); assert(finish(d, true));
        d.during_build = [](ItlIwn &v) {
            reset_radio(v);
            active_roam(v, false);
            v.com.sc_scan_lease.serial = 900;
            v.com.sc_ic.ic_pae_assoc_epoch = 9;
            v.com.sc_ic.ic_flags = 0x100;
        };
        worker(d);
        assert(d.writes == 0 && d.rejects == 1 && resets == 0);
        assert(d.com.sc_scan_lease.serial == 900 && d.com.sc_scan_lease.command_submitted);
        assert(d.com.sc_ic.ic_flags == 0x100); ++cases;
    }
    assert(locks_held == 0);
    std::printf("PASS: %u actual deferred WCL ingress/worker/scan-start/doorbell/upper/reset scenarios; RF still required\n", cases);
}
