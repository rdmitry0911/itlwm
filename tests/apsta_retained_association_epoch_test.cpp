#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

enum { IEEE80211_S_SCAN = 1, IEEE80211_S_RUN = 4 };
enum { IEEE80211_M_STA = 1, IEEE80211_M_HOSTAP = 6 };
enum { IEEE80211_ADDR_LEN = 6 };
#define XYLog(...) ((void)0)

struct ieee80211_node {
    bool ni_port_valid = true;
    uint8_t ni_bssid[IEEE80211_ADDR_LEN] = {2, 0, 0, 0, 0, 1};
};
struct ieee80211com {
    int ic_state = IEEE80211_S_RUN;
    int ic_opmode = IEEE80211_M_STA;
    ieee80211_node *ic_bss;
    volatile uint64_t ic_pae_assoc_epoch = 7;
};
struct TestHal {
    ieee80211com *ic;
    ieee80211com *get80211Controller() const { return ic; }
};
struct TestController { TestHal *fHalService; };

class AirportItlwmAPSTAOwner {
public:
    TestController *owner;
    bool lowerStopPending = false;
    bool apRunning = false;
    bool primaryStaCarrierHoldPending = false;
    uint64_t primaryStaHandoffAssociationEpoch = 0;
    bool primaryStaPostStopWclAssociationPending = false;
    uint64_t primaryStaPostStopAssociationEpoch = 0;
    bool primaryStaHandoffScanArmed = false;

    bool isApRunning() const { return apRunning; }
    bool armPrimaryStaHandoffScan(ieee80211com *);
    bool consumePrimaryStaHandoffScan(ieee80211com *, int);
    bool shouldRetainPrimaryStaCarrier() const;
    bool consumePrimaryStaCarrierHold();
    bool consumePrimaryStaPostStopWclAssociation(const uint8_t *);
};

// PRODUCTION_FUNCTIONS

int main()
{
    ieee80211_node node;
    ieee80211com ic{IEEE80211_S_RUN, IEEE80211_M_STA, &node, 7};
    TestHal hal{&ic};
    TestController controller{&hal};
    AirportItlwmAPSTAOwner owner{&controller};

    // An actual same-association AP handoff retains each separate edge.
    assert(owner.armPrimaryStaHandoffScan(&ic));
    assert(owner.primaryStaHandoffAssociationEpoch == 7);
    assert(owner.consumePrimaryStaHandoffScan(&ic, -1));
    assert(!owner.consumePrimaryStaHandoffScan(&ic, -1));
    assert(owner.shouldRetainPrimaryStaCarrier());
    assert(owner.consumePrimaryStaCarrierHold());
    assert(!owner.consumePrimaryStaCarrierHold());

    // Reproduced leave: credentials/epoch retire before RUN and port-valid.
    assert(owner.armPrimaryStaHandoffScan(&ic));
    __atomic_store_n(&ic.ic_pae_assoc_epoch, 8, __ATOMIC_RELEASE);
    assert(ic.ic_state == IEEE80211_S_RUN && node.ni_port_valid);
    assert(!owner.shouldRetainPrimaryStaCarrier());
    assert(!owner.consumePrimaryStaCarrierHold());
    assert(!owner.consumePrimaryStaHandoffScan(&ic, -1));
    assert(!owner.primaryStaHandoffScanArmed);

    // Even the same BSSID in a new association is not a post-stop replay.
    owner.primaryStaPostStopWclAssociationPending = true;
    owner.primaryStaPostStopAssociationEpoch = 7;
    assert(!owner.consumePrimaryStaPostStopWclAssociation(node.ni_bssid));
    assert(!owner.primaryStaPostStopWclAssociationPending);
    assert(owner.primaryStaPostStopAssociationEpoch == 0);
    owner.primaryStaPostStopWclAssociationPending = true;
    owner.primaryStaPostStopAssociationEpoch = 8;
    assert(owner.consumePrimaryStaPostStopWclAssociation(node.ni_bssid));
    assert(!owner.consumePrimaryStaPostStopWclAssociation(node.ni_bssid));

    // A different target retires rather than preserves a deferred replay.
    uint8_t other[IEEE80211_ADDR_LEN] = {2, 0, 0, 0, 0, 2};
    owner.primaryStaPostStopWclAssociationPending = true;
    owner.primaryStaPostStopAssociationEpoch = 8;
    assert(!owner.consumePrimaryStaPostStopWclAssociation(other));
    assert(!owner.consumePrimaryStaPostStopWclAssociation(node.ni_bssid));

    // DVM temporarily publishes HOSTAP during retained-primary PAN stop.
    assert(owner.armPrimaryStaHandoffScan(&ic));
    ic.ic_opmode = IEEE80211_M_HOSTAP;
    owner.lowerStopPending = true;
    assert(owner.shouldRetainPrimaryStaCarrier());
    assert(!owner.consumePrimaryStaHandoffScan(&ic, -1));
    assert(owner.consumePrimaryStaCarrierHold());
    ic.ic_opmode = IEEE80211_M_STA;
    owner.lowerStopPending = false;

    // No uninitialized epoch, closed port, missing node or wrong controller.
    ic.ic_pae_assoc_epoch = 0;
    assert(!owner.armPrimaryStaHandoffScan(&ic));
    ic.ic_pae_assoc_epoch = 9;
    node.ni_port_valid = false;
    assert(!owner.armPrimaryStaHandoffScan(&ic));
    node.ni_port_valid = true;
    ic.ic_bss = nullptr;
    assert(!owner.armPrimaryStaHandoffScan(&ic));
    ic.ic_bss = &node;
    ieee80211com otherIc = ic;
    assert(!owner.armPrimaryStaHandoffScan(&otherIc));
    assert(owner.armPrimaryStaHandoffScan(&ic));
    assert(!owner.consumePrimaryStaHandoffScan(&ic, 0));
    owner.owner = nullptr;
    assert(!owner.shouldRetainPrimaryStaCarrier());
    assert(!owner.armPrimaryStaHandoffScan(&ic));
    assert(apsta_primary_association_epoch(nullptr) == 0);

    puts("PASS: production APSTA reservations cannot cross association epochs");
}
