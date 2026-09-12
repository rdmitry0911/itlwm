#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sys/types.h>
#include "kernel_memory_test_support.hpp"

constexpr int IEEE80211_CHAN_MAX = 255;
constexpr uint16_t IEEE80211_CHAN_2GHZ = 0x80;
constexpr uint16_t IEEE80211_CHAN_5GHZ = 0x100;
constexpr int IWN_FLAG_HAS_5GHZ = 1;
constexpr int IFF_UP = 1, IFF_RUNNING = 2;
constexpr int IWN_SCAN_LEASE_WCL_INITIAL = 4;
constexpr int IWN_SCAN_LEASE_WCL_BACKGROUND = 3;
constexpr int IEEE80211_EVT_WCL_SCAN_START_REJECTED = 9;
constexpr int IEEE80211_SAE_WCL_REQUEST_RESUME_STARTED = 1;
constexpr int IEEE80211_SAE_WCL_REQUEST_RESUME_DEFERRED = 2;
enum ieee80211_state { IEEE80211_S_INIT, IEEE80211_S_SCAN };
struct ieee80211_channel { uint16_t ic_flags; uint8_t number; };
struct ieee80211_wcl_scan_plan {
    uint8_t channel_filter;
    bool allowed[IEEE80211_CHAN_MAX + 1];
};
struct ieee80211_wcl_scan_start_rejected { uint64_t generation; };
struct ieee80211com {
    struct { int if_flags; } ic_if;
    ieee80211_channel ic_channels[IEEE80211_CHAN_MAX + 1];
    ieee80211_wcl_scan_plan plan;
    bool has_plan;
    void (*ic_event_handler)(ieee80211com *, int, void *);
};
struct iwn_softc {
    ieee80211com sc_ic;
    void *sc_scan_lease_lock;
    bool sc_scan_lease_replay_task_ready;
    struct {
        bool queued, terminal_handoff_ready, launching, command_started;
        uint64_t upper_generation, generic_serial;
        bool background;
    } sc_wcl_initial_scan_pending;
    bool sc_scan_lease_replay_pending;
    ieee80211_state sc_scan_lease_replay_nstate;
    int sc_scan_lease_replay_arg;
    uint64_t sc_scan_lease_replay_sae_generation;
    int sc_flags;
    bool live;
};
struct ItlIwn {
    iwn_softc com;
    static void iwn_scan_lease_replay_task(void *);
    int iwn_scan_start(iwn_softc *, uint16_t, int, int, uint64_t,
                       uint64_t, uint32_t *, bool);
    static int iwn_newstate_impl(ieee80211com *, ieee80211_state, int, uint64_t) {
        assert(false); return EINVAL;
    }
    void releaseSaeWclCredentialAdmission() {}
};
#define container_of(ptr, type, member) \
    reinterpret_cast<type *>(reinterpret_cast<char *>(ptr) - offsetof(type, member))
#define XYLog(...) ((void)0)
static void IOSimpleLockLock(void *) {}
static void IOSimpleLockUnlock(void *) {}
static bool iwn_scan_lease_live_locked(const iwn_softc *sc) { return sc->live; }
static void iwn_wcl_initial_scan_pending_clear_locked(iwn_softc *sc) {
    sc->sc_wcl_initial_scan_pending = {};
}
static int ieee80211_wcl_scan_plan_snapshot(ieee80211com *ic,
                                           ieee80211_wcl_scan_plan *plan) {
    *plan = ic->plan;
    return ic->has_plan;
}
static bool ieee80211_wcl_scan_plan_channel_allowed(
    ieee80211com *, const ieee80211_wcl_scan_plan *plan,
    const ieee80211_channel *channel) {
    return !plan->channel_filter || plan->allowed[channel->number];
}
static int ieee80211_sae_wcl_request_resume_scan(ieee80211com *, uint64_t) {
    return IEEE80211_SAE_WCL_REQUEST_RESUME_STARTED;
}
static void ieee80211_new_state(ieee80211com *, ieee80211_state, int) {}
// Join-failure cleanup was added to the production worker independently of
// these queued-band cases. They have no join-cleanup ticket; unexpected
// admission to that branch must fail the fixture, not silently emulate it.
static uint64_t iwn_scan_lease_take_join_cleanup(iwn_softc *) { return 0; }
static bool ieee80211_wcl_join_failure_pending(ieee80211com *, uint64_t) {
    assert(false); return false;
}
static void ieee80211_pae_assoc_epoch_note_newstate(ieee80211com *, ieee80211_state, int) {
    assert(false);
}
static void iwn_sae_engine_request_join_retirement(iwn_softc *, uint64_t) {
    assert(false);
}

// PRODUCTION_FUNCTIONS

static unsigned submitted, rejected;
static uint16_t submitted_band;
static int injected_error;
static bool cross_doorbell;
static void reject_event(ieee80211com *, int event, void *arg) {
    assert(event == IEEE80211_EVT_WCL_SCAN_START_REJECTED);
    assert(static_cast<ieee80211_wcl_scan_start_rejected *>(arg)->generation == 77);
    ++rejected;
}
int ItlIwn::iwn_scan_start(iwn_softc *sc, uint16_t flags, int bg, int owner,
                           uint64_t upper, uint64_t handoff,
                           uint32_t *backend, bool direct) {
    assert(bg == 0 && owner == IWN_SCAN_LEASE_WCL_INITIAL);
    assert(upper == 77 && handoff == 123 && !direct);
    assert(sc->sc_wcl_initial_scan_pending.launching);
    ++submitted;
    submitted_band = flags;
    if (!iwn_wcl_scan_plan_has_eligible_band(sc, flags)) return EINVAL;
    if (cross_doorbell) {
        sc->sc_wcl_initial_scan_pending.command_started = true;
        *backend = 5;
    }
    return injected_error;
}
static ItlIwn fixture() {
    ItlIwn driver{};
    driver.com.sc_scan_lease_lock = reinterpret_cast<void *>(1);
    driver.com.sc_scan_lease_replay_task_ready = true;
    driver.com.sc_flags = IWN_FLAG_HAS_5GHZ;
    driver.com.sc_ic.ic_if.if_flags = IFF_UP | IFF_RUNNING;
    driver.com.sc_ic.ic_event_handler = reject_event;
    driver.com.sc_ic.has_plan = true;
    driver.com.sc_ic.plan.channel_filter = 1;
    driver.com.sc_ic.ic_channels[13] = {IEEE80211_CHAN_2GHZ, 13};
    driver.com.sc_ic.ic_channels[153] = {IEEE80211_CHAN_5GHZ, 153};
    driver.com.sc_ic.plan.allowed[153] = true;
    driver.com.sc_wcl_initial_scan_pending = {true, true, false, false, 77, 123, false};
    submitted = rejected = 0;
    submitted_band = 0;
    injected_error = 0;
    cross_doorbell = true;
    return driver;
}
int main() {
    auto driver = fixture();
    uint16_t expected_band = 0;
    assert(iwn_wcl_scan_initial_band(&driver.com, &expected_band) == 0);
    assert(expected_band == IEEE80211_CHAN_5GHZ);
    ItlIwn::iwn_scan_lease_replay_task(&driver.com);
    assert(submitted == 1 && submitted_band == IEEE80211_CHAN_5GHZ);
    assert(rejected == 0 && !driver.com.sc_wcl_initial_scan_pending.queued);

    driver = fixture();
    driver.com.sc_ic.plan.allowed[13] = true;
    ItlIwn::iwn_scan_lease_replay_task(&driver.com);
    assert(submitted == 1 && submitted_band == IEEE80211_CHAN_2GHZ);
    assert(rejected == 0);

    driver = fixture();
    driver.com.sc_flags = 0;
    ItlIwn::iwn_scan_lease_replay_task(&driver.com);
    assert(submitted == 0 && rejected == 1);
    assert(!driver.com.sc_wcl_initial_scan_pending.queued);

    driver = fixture();
    driver.com.sc_ic.plan.allowed[153] = false;
    ItlIwn::iwn_scan_lease_replay_task(&driver.com);
    assert(submitted == 0 && rejected == 1);

    driver = fixture();
    driver.com.sc_ic.has_plan = false;
    ItlIwn::iwn_scan_lease_replay_task(&driver.com);
    assert(submitted == 1 && submitted_band == IEEE80211_CHAN_2GHZ);

    driver = fixture();
    driver.com.sc_ic.plan.channel_filter = 0;
    ItlIwn::iwn_scan_lease_replay_task(&driver.com);
    assert(submitted == 1 && submitted_band == IEEE80211_CHAN_2GHZ);

    driver = fixture();
    driver.com.live = true;
    ItlIwn::iwn_scan_lease_replay_task(&driver.com);
    assert(submitted == 0 && rejected == 0);
    assert(driver.com.sc_wcl_initial_scan_pending.queued);

    driver = fixture();
    driver.com.sc_wcl_initial_scan_pending.terminal_handoff_ready = false;
    ItlIwn::iwn_scan_lease_replay_task(&driver.com);
    assert(submitted == 0 && rejected == 0);

    driver = fixture();
    driver.com.sc_wcl_initial_scan_pending.queued = false;
    ItlIwn::iwn_scan_lease_replay_task(&driver.com);
    assert(submitted == 0 && rejected == 0);

    driver = fixture();
    injected_error = ECANCELED;
    cross_doorbell = false;
    ItlIwn::iwn_scan_lease_replay_task(&driver.com);
    assert(submitted == 1 && rejected == 1);

    driver = fixture();
    injected_error = EIO;
    ItlIwn::iwn_scan_lease_replay_task(&driver.com);
    assert(submitted == 1 && rejected == 0);
    puts("IWN queued scan band: PASS");
}
