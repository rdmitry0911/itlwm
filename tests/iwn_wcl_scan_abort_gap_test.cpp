// Replays the RF-observed abort-to-terminal cut. Complete production WCL
// background ingress, lease reserve/abort/retirement bodies are extracted.
// Channel planning and command construction/submission are explicit doubles;
// this is not an execution of the complete scan worker or firmware.
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "kernel_memory_test_support.hpp"
using u_int8_t = uint8_t;
using u_int16_t = uint16_t;
using u_int32_t = uint32_t;
using u_int64_t = uint64_t;
using IOReturn = uint32_t;
constexpr IOReturn kIOReturnSuccess = 0;
constexpr IOReturn kIOReturnBadArgument = 0xe00002c2;
constexpr IOReturn kIOReturnBusy = 0xe00002d5;
constexpr IOReturn kIOReturnAborted = 0xe00002eb;
constexpr unsigned IWN_FLAG_SCANNING = 1;
struct IOSimpleLock { bool held = false; };
static void IOSimpleLockLock(IOSimpleLock *lock) {
    assert(lock != nullptr && !lock->held);
    lock->held = true;
}
static void IOSimpleLockUnlock(IOSimpleLock *lock) {
    assert(lock != nullptr && lock->held);
    lock->held = false;
}
#include "types.inc"
struct ieee80211com {};
struct iwn_softc {
    ieee80211com sc_ic;
    IOSimpleLock lock;
    IOSimpleLock *sc_scan_lease_lock = &lock;
    iwn_scan_lease sc_scan_lease{};
    iwn_wcl_initial_scan_pending sc_wcl_initial_scan_pending{};
    uint64_t sc_scan_lease_next_serial = 471;
    unsigned sc_flags = 0;
    bool sc_ap_transition_scan_blocked = false;
    uint64_t sc_sae_join_scan_block_generation = 0;
    uint64_t sc_sae_bss_loss_join_handoff_generation = 0;
    bool sc_sae_wcl_admission_reserved = false;
    bool sc_sae_wcl_admission_requires_fresh_scan = false;
    bool sc_scan_lease_replay_pending = false;
    uint64_t sc_wcl_join_cleanup_generation = 0;
};
struct ItlIwn {
    iwn_softc com;
    unsigned mock_submissions = 0;
    uint16_t last_band = 0;
    IOReturn beginWclBackgroundScan(uint64_t, uint32_t *);
    int iwn_scan_start(iwn_softc *, uint16_t, int, iwn_scan_lease_owner,
                       uint64_t, uint64_t, uint32_t *, uint64_t);
};
static uint64_t ieee80211_wcl_join_scan_generation(ieee80211com *) {
    assert(false); return 0; // No foreground join in these controls.
}
static bool ieee80211_wcl_reassoc_current(ieee80211com *, uint64_t) {
    assert(false); return false; // New WCL census, not another roam owner.
}
static int iwn_wcl_scan_initial_band(iwn_softc *, uint16_t *band) {
    *band = 0x80; // Exact requested 2.4GHz plan in the captured failure.
    return 0;
}
#include "production.inc"

// Isolate the recorded cut: all earlier lower admission gates passed on RF.
// Keep the real reservation function; only command construction and firmware
// visibility after successful reservation are simulated here.
int ItlIwn::iwn_scan_start(iwn_softc *sc, uint16_t band, int background,
    iwn_scan_lease_owner owner, uint64_t upper, uint64_t handoff,
    uint32_t *backend, uint64_t direct_sae)
{
    assert(background == 1 && owner == IWN_SCAN_LEASE_WCL_BACKGROUND);
    assert(handoff == 0 && direct_sae == 0);
    uint64_t serial = 0;
    if (!iwn_scan_lease_reserve(sc, owner, upper, handoff, backend, &serial,
                              direct_sae, 0))
        return EBUSY;
    ++mock_submissions;
    last_band = band;
    sc->sc_scan_lease.phase = IWN_SCAN_LEASE_ACTIVE;
    sc->sc_scan_lease.command_submitted = true;
    sc->sc_flags |= IWN_FLAG_SCANNING;
    return 0;
}

static void active_roam(ItlIwn &driver) {
    auto &sc = driver.com;
    sc.sc_scan_lease.serial = 471;
    sc.sc_scan_lease.reassoc_serial = 31;
    sc.sc_scan_lease.owner = IWN_SCAN_LEASE_GENERIC_BACKGROUND;
    sc.sc_scan_lease.phase = IWN_SCAN_LEASE_ACTIVE;
    sc.sc_scan_lease.command_submitted = true;
    sc.sc_flags = IWN_FLAG_SCANNING;
}
static void abort_roam(ItlIwn &driver) {
    active_roam(driver);
    uint64_t serial = 0;
    bool submit = false;
    assert(iwn_scan_lease_mark_abort(&driver.com, IWN_SCAN_LEASE_NONE,
                                   0, &serial, &submit, 31));
    assert(serial == 471 && submit);
    // Radio control independently observed the real abort sender returning0.
    // This test injects that boundary receipt, not an invented completion.
    driver.com.sc_scan_lease.abort_submitted = true;
}
static void old_terminal(ItlIwn &driver) {
    auto &sc = driver.com;
    sc.sc_scan_lease.terminal_claimed = true;
    sc.sc_scan_lease.phase = IWN_SCAN_LEASE_DRAINING;
    sc.sc_flags &= ~IWN_FLAG_SCANNING;
    bool retired = false;
    (void)iwn_scan_lease_finish_terminal(&sc, 471, &retired);
    assert(retired && !iwn_scan_lease_live_locked(&sc));
}

int main(int argc, char **argv) {
    const bool require_handoff = argc == 2 &&
        std::strcmp(argv[1], "require-handoff") == 0;
    assert(argc == 1 || require_handoff);
    {
        ItlIwn driver;
        uint32_t backend = 0;
        assert(driver.beginWclBackgroundScan(408, &backend) == 0);
        assert(backend != 0 && driver.mock_submissions == 1);
        assert(driver.last_band == 0x80);
    }
    {
        ItlIwn driver;
        active_roam(driver);
        uint32_t backend = 99;
        assert(driver.beginWclBackgroundScan(408, &backend) == kIOReturnBusy);
        assert(backend == 0 && driver.mock_submissions == 0);
        assert(driver.com.sc_scan_lease.serial == 471);
    }
    {
        ItlIwn driver;
        abort_roam(driver);
        uint32_t backend = 99;
        const auto result = driver.beginWclBackgroundScan(408, &backend);
        assert(driver.mock_submissions == 0);
        assert(driver.com.sc_scan_lease.serial == 471);
        assert(iwn_scan_lease_live_locked(&driver.com));
        std::printf("ABORT_GAP result=0x%x backend=%u old_serial=%llu live=1 submissions=%u\n",
            result, backend,
            static_cast<unsigned long long>(driver.com.sc_scan_lease.serial),
            driver.mock_submissions);
        if (require_handoff && (result != kIOReturnSuccess || backend != 0)) {
            std::fputs("FAIL: valid WCL census is lost before its cancelled roam terminal; require an owned deferred start, not hardware overlap\n", stderr);
            return 1;
        }
        if (!require_handoff) {
            assert(result == kIOReturnBusy && backend == 0);
            old_terminal(driver);
            // This is a different later request. Its success does not repair
            // the first request, just as the later5GHz carrier on RF did not.
            assert(driver.beginWclBackgroundScan(409, &backend) == 0);
            assert(backend != 0 && driver.mock_submissions == 1);
        }
    }
    std::puts(require_handoff ? "PASS: deferred-ingress requirement (worker/RF still separate)" :
        "PASS: three baseline replay controls; abort-gap requirement remains FAIL");
}
