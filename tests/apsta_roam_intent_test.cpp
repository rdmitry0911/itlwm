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
constexpr unsigned IEEE80211_ADDR_LEN = 6;
#define IEEE80211_ADDR_COPY(dst, src) std::memcpy(dst, src, IEEE80211_ADDR_LEN)
#define IEEE80211_ADDR_EQ(a, b) (std::memcmp(a, b, IEEE80211_ADDR_LEN) == 0)
constexpr int IEEE80211_WCL_REASSOC_OWNER_LEAF_SCAN_STARTED = 1;
constexpr int IEEE80211_WCL_REASSOC_OWNER_LEAF_ROAM_STARTED = 2;
#define AIRPORT_ITLWM_REQUIRE_LIVE_OPERATION() do {} while (0)
#define XYLog(...) do {} while (0)
#define IC2IFP(ic) (ic)
#define IEEE80211_STA_ONLY
#define ISSET(value, flags) ((value) & (flags))
#define isclr(value, bit) (((value)[(bit) / 8] & (1U << ((bit) % 8))) == 0)
constexpr unsigned IEEE80211_F_AUTO_JOIN = 1, IEEE80211_F_BGSCAN = 2,
    IEEE80211_F_WEPON = 4, IEEE80211_F_RSNON = 8, IEEE80211_F_DESBSSID = 16,
    IEEE80211_F_PSK = 32, IEEE80211_F_MFPR = 64, IEEE80211_F_DONEGO = 128;
constexpr unsigned IEEE80211_NODE_ASSOCFAIL_CHAN = 1,
    IEEE80211_NODE_ASSOCFAIL_IBSS = 2, IEEE80211_NODE_ASSOCFAIL_PRIVACY = 4,
    IEEE80211_NODE_ASSOCFAIL_BASIC_RATE = 8, IEEE80211_NODE_ASSOCFAIL_ESSID = 16,
    IEEE80211_NODE_ASSOCFAIL_BSSID = 32, IEEE80211_NODE_ASSOCFAIL_WPA_PROTO = 64;
constexpr unsigned IEEE80211_CAPINFO_ESS = 1, IEEE80211_CAPINFO_PRIVACY = 16,
    IEEE80211_RATE_BASIC = 128, IEEE80211_AKM_PSK = 2,
    IEEE80211_AKM_SHA256_PSK = 32, TEST_AKM_SAE = 64,
    IEEE80211_CIPHER_WEP40 = 1, IEEE80211_CIPHER_TKIP = 2,
    IEEE80211_CIPHER_CCMP = 4, IEEE80211_CIPHER_WEP104 = 8,
    IEEE80211_CIPHER_BIP = 16, IEEE80211_RSNCAP_MFPC = 128,
    IEEE80211_RSNCAP_MFPR = 64, IEEE80211_C_MFP = 1;

// PRODUCTION_CARRIERS
// PRODUCTION_REQUEST

struct ieee80211_channel { uint8_t number = 13; };
static ieee80211_channel *const IEEE80211_CHAN_ANYC = nullptr;
struct ieee80211_node {
    uint8_t ni_bssid[6] = {2, 1, 2, 3, 4, 5};
    uint8_t ni_esslen = 3, ni_essid[32] = {'l', 'a', 'b'};
    unsigned ni_rssi = 40;
    ieee80211_channel *ni_chan = nullptr;
    unsigned ni_capinfo = IEEE80211_CAPINFO_ESS | IEEE80211_CAPINFO_PRIVACY;
    unsigned ni_rsnprotos = 1, ni_rsnakms = TEST_AKM_SAE;
    unsigned ni_rsngroupcipher = IEEE80211_CIPHER_CCMP;
    unsigned ni_rsnciphers = IEEE80211_CIPHER_CCMP;
    unsigned ni_rsncaps = IEEE80211_RSNCAP_MFPC | IEEE80211_RSNCAP_MFPR;
    unsigned ni_rsngroupmgmtcipher = IEEE80211_CIPHER_BIP;
    int ni_assoc_fail = 0;
};
struct ieee80211com {
    int ic_state = IEEE80211_S_RUN;
    ieee80211_node source;
    ieee80211_node *ic_bss = &source;
    bool ic_wcl_reassoc_owner_active = false;
    int ic_wcl_reassoc_owner_last_leaf = 0;
    uint8_t ic_wcl_reassoc_source_bssid[6]{};
    ieee80211_wcl_reassoc_request ic_wcl_reassoc_request{};
    unsigned ic_flags = IEEE80211_F_AUTO_JOIN | IEEE80211_F_BGSCAN |
        IEEE80211_F_RSNON | IEEE80211_F_DESBSSID | IEEE80211_F_MFPR;
    uint8_t ic_des_esslen = 0, ic_des_essid[32]{}, ic_des_bssid[6]{};
    uint8_t ic_chan_active[32]{};
    ieee80211_channel *ic_des_chan = IEEE80211_CHAN_ANYC;
    unsigned ic_rsnprotos = 1, ic_rsnakms = TEST_AKM_SAE;
    unsigned ic_rsnciphers = IEEE80211_CIPHER_CCMP, ic_caps = IEEE80211_C_MFP;
    bool ic_pae_mfp_requested = true;
};
static unsigned ieee80211_chan2ieee(ieee80211com *, ieee80211_channel *channel)
{ return channel->number; }
static int ieee80211_wnm_bss_transition_candidate_disposition(
    ieee80211com *, ieee80211_node *) { return 0; }
static uint8_t negotiatedRate = 12;
static uint8_t ieee80211_fix_rate(ieee80211com *, ieee80211_node *, unsigned)
{ return negotiatedRate; }
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
    const uint8_t target[] = {2, 3, 4, 5, 6, 153};
    IEEE80211_ADDR_COPY(input.candidates[0].bssid, target);
    input.feature_flags = 3;
    input.prune_rssi_dbm = -80;
    const auto reset = [&] { scans = pinDisarms = controller.reservations = 0; };
    const auto expectRoam = [&] {
        reset();
        assert(interface.setWCL_REASSOC(&input) == kIOReturnSuccess);
        assert(scans == 1 && pinDisarms == 1 && controller.reservations == 0);
        assert(admitted.channel_count == 2 && admitted.candidate_count == 1);
        assert(admitted.channel_spec[1] == 153);
        assert(std::memcmp(admitted.candidate[0].bssid, target, 6) == 0);
        assert(admitted.feature_flags == 3 && admitted.prune_rssi_dbm == -80);
        apple80211_reassoc snapshot{};
        std::memcpy(&snapshot, interface.cachedReassocRequest, sizeof(snapshot));
        assert(snapshot.candidate_count == 1);
    };
    const auto expectAPConstraint = [&] {
        reset();
        assert(interface.setWCL_REASSOC(&input) == kIOReturnSuccess);
        assert(scans == 1 && pinDisarms == 1 && controller.reservations == 0);
        assert(admitted.channel_count == 1 && admitted.channel_spec[0] == 13);
        assert(admitted.candidate_count == 1);
        assert(std::memcmp(admitted.candidate[0].bssid, target, 6) == 0);
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
    input.channel_spec_count = 0;
    reset();
    assert(interface.setWCL_REASSOC(&input) == kIOReturnSuccess);
    assert(scans == 1 && pinDisarms == 1 && controller.reservations == 0);
    assert(admitted.candidate_count == 1);
    assert(std::memcmp(admitted.candidate[0].bssid, target, 6) == 0);

    // Actual WCLNetManager::setROAMWithBssid carrier, with identifiers redacted:
    // no channels, one ff:ff:ff:ff:ff:ff BSSID. It must not become channel 255.
    input = {};
    input.candidate_count = 1;
    std::memset(input.candidates[0].bssid, 0xff, 6);
    ap.running = false;
    reset();
    assert(interface.setWCL_REASSOC(&input) == kIOReturnSuccess);
    assert(scans == 1 && admitted.channel_count == 0 && admitted.candidate_count == 1);
    assert(std::memcmp(admitted.candidate[0].bssid, input.candidates[0].bssid, 6) == 0);
    input.candidate_count = UINT32_MAX;
    input.channel_spec_count = UINT32_MAX;
    reset();
    assert(interface.setWCL_REASSOC(&input) == kIOReturnSuccess);
    assert(scans == 1 && admitted.candidate_count == 7 && admitted.channel_count == 50);
    assert(interface.setWCL_REASSOC(nullptr) == kIOReturnBadArgumentTahoe);
    controller.fHalService = nullptr;
    assert(controller.getAPSTAPrimaryRoamSharedChannel() == 0);

    // Exercise the real net80211 BSSID/channel/ESS matcher as well as ingress.
    auto &ic = hal.ic;
    auto &policy = ic.ic_wcl_reassoc_request;
    ieee80211_channel channel;
    ieee80211_node candidate;
    candidate.ni_chan = &channel;
    IEEE80211_ADDR_COPY(candidate.ni_bssid, target);
    IEEE80211_ADDR_COPY(ic.ic_wcl_reassoc_source_bssid, ic.source.ni_bssid);
    ic.ic_wcl_reassoc_owner_active = true;
    ic.ic_wcl_reassoc_owner_last_leaf = IEEE80211_WCL_REASSOC_OWNER_LEAF_SCAN_STARTED;
    policy = {};
    policy.candidate_count = 1;
    std::memset(policy.candidate[0].bssid, 0xff, 6);
    assert(ieee80211_wcl_reassoc_candidate_disposition(&ic, &candidate) == 1);
    channel.number = 153;
    assert(ieee80211_wcl_reassoc_candidate_disposition(&ic, &candidate) == 1);
    policy.channel_count = 1;
    policy.channel_spec[0] = 0xd00d; // The explicit channel list is independent.
    assert(ieee80211_wcl_reassoc_candidate_disposition(&ic, &candidate) == -1);
    channel.number = 13;
    IEEE80211_ADDR_COPY(policy.candidate[0].bssid, target);
    assert(ieee80211_wcl_reassoc_candidate_disposition(&ic, &candidate) == 1);
    candidate.ni_bssid[5] = 9; // Another AP on the same channel is not the target.
    assert(ieee80211_wcl_reassoc_candidate_disposition(&ic, &candidate) == -1);
    policy.candidate_count = 2;
    IEEE80211_ADDR_COPY(policy.candidate[1].bssid, candidate.ni_bssid);
    assert(ieee80211_wcl_reassoc_candidate_disposition(&ic, &candidate) == 1);
    policy.candidate_count = 1;
    std::memset(policy.candidate[0].bssid, 0, 6);
    assert(ieee80211_wcl_reassoc_candidate_disposition(&ic, &candidate) == 1);
    candidate.ni_essid[0] = 'x';
    assert(ieee80211_wcl_reassoc_candidate_disposition(&ic, &candidate) == -1);
    candidate.ni_essid[0] = 'l';
    policy.prune_rssi_dbm = -65;
    assert(ieee80211_wcl_reassoc_candidate_disposition(&ic, &candidate) == 1);
    candidate.ni_rssi = 30;
    assert(ieee80211_wcl_reassoc_candidate_disposition(&ic, &candidate) == -1);
    candidate.ni_rssi = 40;
    // Exercise actual BSS admission: discovery's AUTO_JOIN shortcut must not
    // bypass security/rates when the same node becomes a real WCL roam target.
    ieee80211_channel sourceChannel;
    sourceChannel.number = 9;
    ic.ic_des_chan = &sourceChannel;
    IEEE80211_ADDR_COPY(ic.ic_des_bssid, ic.source.ni_bssid);
    assert(ieee80211_match_bss(&ic, &candidate, 1) == 0);
    candidate.ni_capinfo &= ~IEEE80211_CAPINFO_PRIVACY;
    assert(ieee80211_match_bss(&ic, &candidate, 1) & IEEE80211_NODE_ASSOCFAIL_PRIVACY);
    candidate.ni_capinfo |= IEEE80211_CAPINFO_PRIVACY;
    candidate.ni_rsnakms = IEEE80211_AKM_PSK;
    assert(ieee80211_match_bss(&ic, &candidate, 1) & IEEE80211_NODE_ASSOCFAIL_WPA_PROTO);
    candidate.ni_rsnakms = TEST_AKM_SAE;
    candidate.ni_rsncaps = 0;
    assert(ieee80211_match_bss(&ic, &candidate, 1) & IEEE80211_NODE_ASSOCFAIL_WPA_PROTO);
    candidate.ni_rsncaps = IEEE80211_RSNCAP_MFPC | IEEE80211_RSNCAP_MFPR;
    candidate.ni_rsngroupmgmtcipher = IEEE80211_CIPHER_CCMP;
    assert(ieee80211_match_bss(&ic, &candidate, 1) & IEEE80211_NODE_ASSOCFAIL_WPA_PROTO);
    candidate.ni_rsngroupmgmtcipher = IEEE80211_CIPHER_BIP;
    negotiatedRate = IEEE80211_RATE_BASIC;
    assert(ieee80211_match_bss(&ic, &candidate, 1) & IEEE80211_NODE_ASSOCFAIL_BASIC_RATE);
    negotiatedRate = 12;
    assert(ieee80211_match_bss(&ic, &candidate, 1) == 0);
    assert(ieee80211_match_bss(&ic, &candidate, 0) == 0); // Scan export unchanged.
    IEEE80211_ADDR_COPY(candidate.ni_bssid, ic.source.ni_bssid);
    assert(ieee80211_wcl_reassoc_candidate_disposition(&ic, &candidate) == -1);
    ic.ic_wcl_reassoc_owner_active = false;
    assert(ieee80211_wcl_reassoc_candidate_disposition(&ic, &candidate) == 0);
    assert(ieee80211_wcl_reassoc_candidate_disposition(nullptr, &candidate) == 0);
    std::puts("PASS: production WCL BSSID decoding, wildcard/allowlist matching and AP-only channel constraints");
}
