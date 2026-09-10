#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <strings.h>
#include "AirportItlwm/AirportItlwmAPSTAInterface.hpp"

#include "tests/kernel_memory_test_support.hpp"

using IOReturn = uint32_t;
constexpr IOReturn kIOReturnSuccess = 0;
constexpr IOReturn kIOReturnNotReady = 0xe00002d8;
constexpr IOReturn kIOReturnUnsupported = 0xe00002c7;
constexpr IOReturn kIOReturnBadArgument = 0xe00002c2;
constexpr IOReturn kIOReturnBusy = 0xe00002d5;
constexpr IOReturn kIOReturnAborted = 0xe00002eb;
constexpr IOReturn kIOReturnTimeout = 0xe00002d6;
enum { IEEE80211_ADDR_LEN = 6, IEEE80211_S_SCAN = 1, IEEE80211_S_RUN = 4, IEEE80211_M_STA = 1,
       IEEE80211_F_RSNON = 1, IEEE80211_HTCAP_SMPS_MASK = 0xc,
       IEEE80211_HTCAP_SGI20 = 0x20, IEEE80211_MCS_RX_RATE_HIGH = 0x3ff };
template <typename... Args> static void testLog(const char *, Args...) {}
#define XYLog(...) testLog(__VA_ARGS__)
#define MIN(a,b) ((a) < (b) ? (a) : (b))

// PRODUCTION_TYPES

struct ieee80211_channel { unsigned number = 9; };
struct apple80211_channel_data {
    uint32_t version;
    struct { uint32_t version, channel, flags; } channel;
};
struct ieee80211_node {
    bool ni_port_valid = true;
    ieee80211_channel *ni_chan = nullptr;
};
struct ieee80211com {
    int ic_state = IEEE80211_S_RUN;
    int ic_opmode = IEEE80211_M_STA;
    ieee80211_node *ic_bss = nullptr;
    unsigned ic_flags = 0;
    uint64_t ic_pae_assoc_epoch = 1;
    uint8_t ic_sup_mcs[16]{};
    uint16_t ic_htcaps = 0, ic_max_rxrate = 0;
    uint8_t ic_ampdu_params = 0, ic_tx_mcs_set = 0;
};
struct TahoeOwnerRegistry {
    struct AssociationOwner {
        bool hasCarrier = false, publicCarrier = false;
        bool selectedFromCandidate = false, authAssocCompletionArmed = false;
        bool joinTerminalObserved = false;
    } association;
};
struct TestHal {
    ieee80211com ic;
    unsigned starts = 0, stops = 0;
    bool running = false;
    std::function<IOReturn(const ItlHalApConfig *)> onStart = [](const ItlHalApConfig *) { return kIOReturnSuccess; };
    std::function<IOReturn()> onStop = [] { return kIOReturnSuccess; };
    bool supportsAPMode() const { return true; }
    bool requiresAPSTASharedChannel() const { return false; }
    uint16_t getAPSTARequiredSharedChannel() const { return 0; }
    ieee80211com *get80211Controller() { return &ic; }
    IOReturn startAPMode(const ItlHalApConfig *cfg) {
        ++starts;
        IOReturn result = onStart(cfg);
        if (!result) running = true;
        return result;
    }
    IOReturn stopAPMode() {
        ++stops;
        IOReturn result = onStop();
        if (!result) running = false;
        return result;
    }
    IOReturn setAPHidden(bool) { return 0; }
    bool isPrimaryStaRecoveryScanPending() const { return false; }
    uint16_t getAPCurrentChannel() const { return running ? 9 : 0; }
};
static int ieee80211_chan2ieee(ieee80211com *, ieee80211_channel *channel) {
    return channel->number;
}
static IOReturn airportItlwmHandoffPrimaryStaRecoveryScanToAP(TestHal *) { return 0; }
struct AirportItlwm {
    TestHal *fHalService;
    TahoeOwnerRegistry registry;
    bool datapath = false;
    std::function<void(bool)> onDatapath;
    void setAPSTADatapathEnabled(bool enabled) {
        datapath = enabled;
        if (onDatapath) onDatapath(enabled);
    }
    TahoeOwnerRegistry &getTahoeOwnerRegistry() { return registry; }
};
class AirportItlwmAPSTAOwner {
public:
    AirportItlwm *owner;
    AirportItlwmAPSTAOwnerLifecycleState lifecycle = kAirportItlwmAPSTAOwnerCreated;
    AirportItlwmAPSTAStateBlock state{};
    uint8_t mac[6]{};
    uint16_t apChannel = 9;
    uint32_t apAuthUpper = 0;
    uint8_t apCredential[64]{};
    uint32_t apCredentialLength = 0;
    bool lowerStopPending = false;
    bool lowerAPCallInFlight = false;
    uint64_t hostAPRequestGeneration = 1;
    bool primaryStaCarrierHoldPending = false;
    uint64_t primaryStaHandoffAssociationEpoch = 0;
    bool primaryStaPostStopWclAssociationPending = false;
    uint64_t primaryStaPostStopAssociationEpoch = 0;
    bool radioResetResumePending = false;
    bool radioResetWaitForPrimaryStaRun = false;
    bool radioResetPrimaryStaScanHandoff = false;
    bool initialHostAPAdmissionPending = false;
    bool confirmedHostAPStartPending = false;
    bool interfaceDrivenHostAPConfirmationPending = false;
    bool primaryStaHandoffScanArmed = false;
    uint16_t radioResetResumeWaitTicks = 0;
    IOReturn startLowerIfReady();
    void advanceHostAPRequestGeneration();
    bool isApRunning() const {
        return lifecycle == kAirportItlwmAPSTAOwnerRunning && state.resetState26c != 0;
    }
    IOReturn setHostAPMode(const AirportItlwmAPSTAHostApModeNetworkDataLayout *);
    IOReturn resumeAfterRadioReset();
    void prepareRetainedLowerReset(uint16_t);
    void prepareEmptyAPForRadioReset();
    IOReturn setChannel(const apple80211_channel_data *data) {
        if (!data || !data->channel.channel) return kIOReturnBadArgument;
        apChannel = data->channel.channel;
        return 0;
    }
    IOReturn stopLower();
    IOReturn driveLowerStopToTerminal();
    void resetRuntimeState();
    void restoreRetainedPrimaryStaLinkAfterStop() {}
    void clearLowerAssociatedStations() {}
    void clearStation(AirportItlwmAPSTAStationTableEntryLayout *station) {
        bzero(station, sizeof(*station));
    }
    void setSoftAPPowerSaveState(uint8_t value, uint8_t) {
        state.softapMode10 = value;
    }
};

// Packet construction and external I/O are not the subject of this test.
// The actual complete start/stop/terminal/reset control methods follow.
static size_t apsta_build_wpa2_psk_rsn_ie(uint8_t *, size_t) { return 0; }
static size_t apsta_build_wpa3_sae_rsn_ie(uint8_t *, size_t) { return 0; }
static size_t apsta_build_beacon(uint8_t *, size_t, const uint8_t *,
    const uint8_t *, size_t, uint16_t, uint16_t, uint8_t, const uint8_t *,
    size_t, const ItlHalApConfig *) { return 8; }

// PRODUCTION_FUNCTIONS

int main() {
    TestHal hal;
    AirportItlwm controller{&hal, {}, false, {}};
    AirportItlwmAPSTAOwner ap{&controller};
    ap.state.softapSsidLength274 = 4;
    memcpy(ap.state.softapSsid278, "test", 4);

    assert(ap.startLowerIfReady() == 0);
    assert(ap.state.resetState26c == 1 && controller.datapath);
    assert(ap.stopLower() == 0);
    assert(ap.lifecycle == kAirportItlwmAPSTAOwnerTerminal);
    assert(ap.state.resetState26c == 0 && !controller.datapath);

    // A HAL start may yield its command gate while waiting for SCAN_ABORT.
    // An admitted public stop then resets the upper state and leaves the
    // lower removal pending. The old start must not republish AP-up.
    hal.onStop = [] { return kIOReturnNotReady; };
    hal.onStart = [&](const ItlHalApConfig *) {
        assert(ap.stopLower() == kIOReturnNotReady);
        assert(ap.lowerStopPending && ap.state.resetState26c == 0);
        return kIOReturnSuccess;
    };
    const IOReturn startResult = ap.startLowerIfReady();
    std::printf("after cancelled start: result=0x%x lifecycle=%u up=%u stop=%u datapath=%u\n",
        startResult, ap.lifecycle, ap.state.resetState26c,
        ap.lowerStopPending, controller.datapath);
    hal.onStop = [] { return kIOReturnSuccess; };
    assert(ap.driveLowerStopToTerminal() == 0);
    std::printf("after stop terminal: lifecycle=%u up=%u stop=%u datapath=%u\n",
        ap.lifecycle, ap.state.resetState26c, ap.lowerStopPending,
        controller.datapath);
    if (ap.state.resetState26c != 0 || controller.datapath) {
        std::fputs("FAIL: the completed stop retains AP-up/data publication from the obsolete start\n", stderr);
        return 1;
    }
    auto profile = [](const char *name, uint32_t auth, const char *key) {
        AirportItlwmAPSTAHostApModeNetworkDataLayout value{};
        value.version00 = value.channelVersion10 = 1;
        value.channelNumber14 = 9;
        value.ssidLength1c = std::strlen(name);
        std::memcpy(value.ssid20, name, value.ssidLength1c);
        value.authUpper0c = auth;
        value.credentialLength44 = std::strlen(key);
        std::memcpy(value.credential50, key, value.credentialLength44);
        return value;
    };
    const auto oldProfile = profile("old", 8, "old-test-key");
    const auto newProfile = profile("next", 0x1000, "new-test-key");
    const auto openProfile = profile("open", 0, "");

    // Cancellation must reach the owner even before the first AP-up edge.
    hal.onStart = [&](const ItlHalApConfig *) {
        assert(ap.setHostAPMode(nullptr) == 0);
        return kIOReturnSuccess;
    };
    unsigned beforeStarts = hal.starts;
    assert(ap.setHostAPMode(&openProfile) == kIOReturnAborted);
    assert(ap.lowerStopPending && !ap.radioResetResumePending);
    assert(ap.resumeAfterRadioReset() == 0);
    assert(ap.resumeAfterRadioReset() == 0);
    assert(hal.starts == beforeStarts + 1 && !ap.isApRunning());
    assert(!controller.datapath && !ap.lowerAPCallInFlight);

    // Preserve a real successor received inside the old start's gate sleep.
    hal.onStart = [&](const ItlHalApConfig *cfg) {
        assert(cfg->authUpper == 8 && cfg->ssidLength == 3);
        assert(ap.setHostAPMode(nullptr) == 0);
        assert(ap.setHostAPMode(&newProfile) == 0);
        assert(ap.confirmedHostAPStartPending && ap.lowerStopPending);
        assert(!ap.isApRunning());
        assert(std::memcmp(cfg->ssid, "old", 3) == 0);
        assert(std::memcmp(cfg->credential, "old-test-key", 12) == 0);
        return kIOReturnSuccess;
    };
    beforeStarts = hal.starts;
    assert(ap.setHostAPMode(&oldProfile) == kIOReturnAborted);
    assert(hal.starts == beforeStarts + 1);
    assert(ap.confirmedHostAPStartPending && ap.radioResetResumePending);
    hal.onStart = [&](const ItlHalApConfig *cfg) {
        assert(cfg->authUpper == 0x1000 && cfg->ssidLength == 4);
        assert(std::memcmp(cfg->ssid, "next", 4) == 0);
        assert(std::memcmp(cfg->credential, "new-test-key", 12) == 0);
        return kIOReturnSuccess;
    };
    hal.onStop = [] { return kIOReturnNotReady; };
    assert(ap.resumeAfterRadioReset() == kIOReturnNotReady);
    assert(ap.confirmedHostAPStartPending && hal.starts == beforeStarts + 1);
    hal.onStop = [] { return kIOReturnSuccess; };
    assert(ap.resumeAfterRadioReset() == 0);
    assert(ap.isApRunning() && controller.datapath);
    assert(!ap.confirmedHostAPStartPending && !ap.radioResetResumePending);
    assert(!ap.lowerStopPending && !ap.lowerAPCallInFlight);
    assert(hal.starts == beforeStarts + 2);

    // A replacement inside stop must not invoke a nested HAL start/stop.
    hal.onStop = [&] {
        const unsigned starts = hal.starts, stops = hal.stops;
        assert(ap.setHostAPMode(&openProfile) == 0);
        assert(ap.driveLowerStopToTerminal() == kIOReturnNotReady);
        assert(hal.starts == starts && hal.stops == stops);
        return kIOReturnSuccess;
    };
    assert(ap.setHostAPMode(nullptr) == 0);
    assert(ap.confirmedHostAPStartPending && !ap.isApRunning());
    hal.onStop = [] { return kIOReturnSuccess; };
    hal.onStart = [](const ItlHalApConfig *cfg) {
        assert(cfg->authUpper == 0 && cfg->credentialLength == 0);
        assert(std::memcmp(cfg->ssid, "open", 4) == 0);
        return kIOReturnSuccess;
    };
    assert(ap.resumeAfterRadioReset() == 0);
    assert(ap.isApRunning() && !ap.radioResetResumePending);
    assert(ap.setHostAPMode(nullptr) == 0);

    // Cancellation from an actual datapath publication callback wins too.
    controller.onDatapath = [&](bool enabled) {
        if (enabled) assert(ap.setHostAPMode(nullptr) == 0);
    };
    assert(ap.setHostAPMode(&openProfile) == kIOReturnAborted);
    assert(!controller.datapath && !ap.isApRunning());
    assert(ap.resumeAfterRadioReset() == 0);
    controller.onDatapath = {};

    // The watchdog's deferred start has the same supersession boundary as
    // a synchronous public start. Its stale tail must not clear the successor.
    hal.onStart = [](const ItlHalApConfig *) { return kIOReturnNotReady; };
    assert(ap.setHostAPMode(&oldProfile) == 0);
    assert(ap.initialHostAPAdmissionPending && ap.radioResetResumePending);
    hal.onStart = [&](const ItlHalApConfig *) {
        assert(ap.setHostAPMode(nullptr) == 0);
        assert(ap.setHostAPMode(&openProfile) == 0);
        return kIOReturnSuccess;
    };
    assert(ap.resumeAfterRadioReset() == kIOReturnAborted);
    assert(ap.confirmedHostAPStartPending && ap.radioResetResumePending);
    hal.onStart = [](const ItlHalApConfig *cfg) {
        assert(cfg->authUpper == 0 && cfg->credentialLength == 0);
        return kIOReturnSuccess;
    };
    for (IOReturn pending : {kIOReturnNotReady, kIOReturnBusy,
                            kIOReturnTimeout, kIOReturnAborted}) {
        hal.onStop = [pending] { return pending; };
        assert(ap.resumeAfterRadioReset() == pending);
        assert(ap.confirmedHostAPStartPending && !ap.isApRunning());
    }
    hal.onStop = [] { return kIOReturnSuccess; };
    assert(ap.resumeAfterRadioReset() == 0 && ap.isApRunning());
    assert(!ap.radioResetResumePending && !ap.confirmedHostAPStartPending);

    // An unexpected physical loss creates a new replay generation inside
    // the watchdog itself; that owned transition must still finish normally.
    beforeStarts = hal.starts;
    hal.running = false;
    assert(ap.resumeAfterRadioReset() == 0);
    assert(hal.starts == beforeStarts + 1 && ap.isApRunning());
    assert(!ap.radioResetResumePending);
    assert(ap.setHostAPMode(nullptr) == 0);

    // Rejected public input does not invalidate an in-flight valid start.
    auto invalid = openProfile;
    invalid.ssidLength1c = 33;
    hal.onStart = [&](const ItlHalApConfig *) {
        const auto generation = ap.hostAPRequestGeneration;
        assert(ap.setHostAPMode(&invalid) != 0);
        assert(ap.hostAPRequestGeneration == generation);
        return kIOReturnSuccess;
    };
    assert(ap.setHostAPMode(&openProfile) == 0 && ap.isApRunning());
    assert(ap.setHostAPMode(nullptr) == 0);

    ap.hostAPRequestGeneration = UINT64_MAX;
    ap.advanceHostAPRequestGeneration();
    assert(ap.hostAPRequestGeneration == 1);
    std::puts("PASS: production AP start/stop rejects superseded completions and replays the accepted successor");
}
