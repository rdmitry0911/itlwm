#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sys/types.h>

using IOReturn = int;
constexpr int kIOReturnSuccess = 0;
constexpr int kIOReturnBadArgumentTahoe = 1;
constexpr int kIOReturnBusy = 2;
constexpr int IEEE80211_S_RUN = 4;
#define AIRPORT_ITLWM_REQUIRE_LIVE_OPERATION() do {} while (0)
#define XYLog(...) do {} while (0)
#define IC2IFP(ic) (ic)

// PRODUCTION_CARRIERS
// PRODUCTION_REQUEST

struct ieee80211com { int ic_state = IEEE80211_S_RUN; void *ic_bss = this; };
struct AirportItlwmAPSTAOwner {
    bool created = true, running = false;
    bool radioResetResumePending = false, lowerStopPending = false;
    bool initialHostAPAdmissionPending = false, confirmedHostAPStartPending = false;
    bool primaryStaHandoffScanArmed = false, primaryStaCarrierHoldPending = false;
    bool isCreated() const { return created; }
    bool isApRunning() const { return running; }
    bool hasHostAPIntent() const;
};
struct FakeHal {
    ieee80211com ic;
    uint16_t apChannel = 0, requiredChannel = 13;
    ieee80211com *get80211Controller() { return &ic; }
    uint16_t getAPCurrentChannel() const { return apChannel; }
    uint16_t getAPSTARequiredSharedChannel() const { return requiredChannel; }
};
struct TahoeOwnerRegistry {
    struct AssociationOwner {};
    AssociationOwner association, publicAssociation;
};
struct AirportItlwm {
    FakeHal *fHalService = nullptr;
    AirportItlwmAPSTAOwner *fAPSTAOwner = nullptr;
    unsigned reservations = 0;
    TahoeOwnerRegistry registry{};
    uint16_t getAPSTAPrimaryRoamSharedChannel() const;
    TahoeOwnerRegistry &getTahoeOwnerRegistry() { return registry; }
    void noteAPSTASharedChannelFilteredWclReassoc(ieee80211com *) { ++reservations; }
};
struct AirportItlwmSkywalkInterface {
    FakeHal *fHalService = nullptr;
    AirportItlwm *instance = nullptr;
    uint8_t cachedReassocRequest[sizeof(apple80211_reassoc)]{};
    bool hasCachedReassocRequest = false;
    IOReturn setWCL_REASSOC(apple80211_reassoc *);
};
static unsigned scans = 0, pinDisarms = 0;
static ieee80211_wcl_reassoc_request admitted;
static void ieee80211_public_initial_bssid_pin_disarm(ieee80211com *) { ++pinDisarms; }
static int ieee80211_begin_wcl_reassoc_bgscan(
    ieee80211com *, const ieee80211_wcl_reassoc_request *request)
{ ++scans; admitted = *request; return 0; }

// PRODUCTION_FUNCTIONS

int main()
{
    FakeHal hal;
    AirportItlwmAPSTAOwner ap;
    AirportItlwm controller{&hal, &ap};
    AirportItlwmSkywalkInterface interface{&hal, &controller};
    apple80211_reassoc input{};
    input.channel_spec_count = 2;
    input.channel_specs[0] = 13;
    input.channel_specs[1] = 153;
    input.candidate_count = 1;
    input.candidates[0].channel_spec = 153;
    input.candidates[0].score = 77;
    input.feature_flags = 3;
    input.prune_rssi_dbm = -80;
    const auto reset = [&] { scans = pinDisarms = controller.reservations = 0; };
    const auto expectRoam = [&] {
        reset();
        assert(interface.setWCL_REASSOC(&input) == kIOReturnSuccess);
        assert(scans == 1 && pinDisarms == 1 && controller.reservations == 0);
        assert(admitted.channel_count == 2 && admitted.candidate_count == 1);
        assert(admitted.channel_spec[1] == 153);
        assert(admitted.candidate[0].channel_spec == 153 && admitted.candidate[0].score == 77);
        assert(admitted.feature_flags == 3 && admitted.prune_rssi_dbm == -80);
        apple80211_reassoc snapshot{};
        std::memcpy(&snapshot, interface.cachedReassocRequest, sizeof(snapshot));
        assert(snapshot.candidate_count == 1);
    };
    const auto expectAPConstraint = [&] {
        reset();
        assert(interface.setWCL_REASSOC(&input) == kIOReturnSuccess);
        assert(scans == 0 && pinDisarms == 0 && controller.reservations == 1);
    };

    expectRoam(); // A default, allocated role-7 interface is not AP intent.
    ap.primaryStaHandoffScanArmed = ap.primaryStaCarrierHoldPending = true;
    expectRoam(); // Old retention tokens must not create a new AP constraint.
    controller.fAPSTAOwner = nullptr;
    expectRoam();
    hal.apChannel = 13;
    expectAPConstraint(); // A real lower AP remains protected without an owner.
    hal.apChannel = 0;
    controller.fAPSTAOwner = &ap;

    bool *intentFlags[] = {&ap.running, &ap.radioResetResumePending,
        &ap.lowerStopPending, &ap.initialHostAPAdmissionPending,
        &ap.confirmedHostAPStartPending};
    for (bool *flag : intentFlags) {
        *flag = true;
        expectAPConstraint();
        ap.created = false;
        expectRoam(); // A terminal owner cannot constrain its successor.
        ap.created = true;
        *flag = false;
    }
    ap.running = true;
    hal.requiredChannel = 0;
    expectRoam(); // A backend without the single-channel restriction is unchanged.
    hal.requiredChannel = 13;
    input.channel_spec_count = 1;
    input.channel_specs[0] = 153;
    reset();
    assert(interface.setWCL_REASSOC(&input) == kIOReturnBusy);
    assert(scans == 0 && pinDisarms == 0 && controller.reservations == 0);

    input.candidate_count = 0;
    reset();
    assert(interface.setWCL_REASSOC(&input) == kIOReturnSuccess);
    assert(scans == 0 && pinDisarms == 0 && controller.reservations == 0);
    assert(interface.setWCL_REASSOC(nullptr) == kIOReturnBadArgumentTahoe);
    controller.fHalService = nullptr;
    assert(controller.getAPSTAPrimaryRoamSharedChannel() == 0);
    std::puts("PASS: production WCL roam targets are constrained only by an accepted AP lifecycle");
}
