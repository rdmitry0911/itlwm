/*
 * Host-owned APSTA owner.
 *
 * This surface intentionally separates role-7 owner lifetime from
 * functional Intel AP/GO firmware bring-up: role-7 create/delete can
 * allocate and tear down the owner, while AP-up remains false unless
 * a HAL backend explicitly advertises and starts AP mode.
 */
#ifndef AirportItlwmAPSTAOwner_hpp
#define AirportItlwmAPSTAOwner_hpp

#include <IOKit/IOLib.h>
#include <libkern/c++/OSObject.h>
#include <net80211/ieee80211_var.h>
#include "AirportItlwmAPSTAInterface.hpp"
#include "Airport/apple80211_ioctl.h"

class AirportItlwm;
struct ItlHalApConfig;
struct ItlHalApKey;
struct ItlHalApCSA;
struct ItlHalApRSNConfig;
struct ItlHalApStationCommand;
struct ieee80211_node;

extern "C" void AirportItlwmAPSTANet80211Event(
    struct ieee80211com *ic,
    struct ieee80211_node *ni,
    int event,
    void *arg);

enum AirportItlwmAPSTAOwnerLifecycleState {
    kAirportItlwmAPSTAOwnerUnallocated = 0,
    kAirportItlwmAPSTAOwnerAllocated,
    kAirportItlwmAPSTAOwnerCreated,
    kAirportItlwmAPSTAOwnerLowerBlocked,
    kAirportItlwmAPSTAOwnerRunning,
    kAirportItlwmAPSTAOwnerTerminal,
    kAirportItlwmAPSTAOwnerFreed,
};

class AirportItlwmAPSTAOwner : public OSObject {
    OSDeclareDefaultStructors(AirportItlwmAPSTAOwner)

public:
    bool initWithController(AirportItlwm *controller,
                            const struct apple80211_virt_if_create_data *create);
    void free() override;

    IOReturn startLowerIfReady();
    IOReturn stopLower();
    void teardown();

    bool isCreated() const { return lifecycle >= kAirportItlwmAPSTAOwnerCreated &&
                                    lifecycle < kAirportItlwmAPSTAOwnerTerminal; }
    bool isApRunning() const { return lifecycle == kAirportItlwmAPSTAOwnerRunning &&
                                      state.resetState26c != 0; }
    const char *bsdName() const { return bsdNameStorage; }
    bool matchesBSDName(const uint8_t *name) const;

    AirportItlwmAPSTAStateBlock *stateBlock() { return &state; }
    const AirportItlwmAPSTAStateBlock *stateBlock() const { return &state; }

    IOReturn getSSID(AirportItlwmAPSTASsidDataLayout *out) const;
    IOReturn getState(AirportItlwmAPSTAStateDataLayout *out) const;
    IOReturn getOpMode(AirportItlwmAPSTAOpModeDataLayout *out) const;
    IOReturn getPeerCacheMaximumSize(AirportItlwmAPSTAPeerCacheMaximumSizeLayout *out) const;
    IOReturn setSSID(const struct apple80211_ssid_data *in);
    IOReturn setChannel(const struct apple80211_channel_data *in);
    IOReturn setHostAPMode(const AirportItlwmAPSTAHostApModeNetworkDataLayout *in);
    IOReturn setCipherKey(const struct apple80211_key *key);
    IOReturn getHostAPModeHidden(AirportItlwmAPSTAHostApModeHiddenOutputLayout *out) const;
    IOReturn getSoftAPParams(AirportItlwmAPSTASoftAPParamsOutputLayout *out) const;
    IOReturn getSoftAPStats(AirportItlwmAPSTASoftAPStatsLayout *out) const;
    IOReturn getStationList(struct apple80211_sta_data *out);
    IOReturn getStaIEList(AirportItlwmAPSTAStaIEDataLayout *out);
    IOReturn getStaStats(AirportItlwmAPSTAStaStatsDataLayout *out);
    IOReturn getKeyRsc(AirportItlwmAPSTAKeyRscDataLayout *out);
    IOReturn setStationAuthorization(const AirportItlwmAPSTAStaAuthorizeInputLayout *in);
    IOReturn setStationDisassociation(const AirportItlwmAPSTAStaDisassocInputLayout *in, bool deauth);
    IOReturn setSoftAPExtCaps(const struct apple80211_softap_extended_capabilities_info *in);
    IOReturn setMisMaxSta(const struct apple80211_mis_max_sta *in);
    IOReturn setMaxAssoc(uint32_t value);
    IOReturn setPeerCacheControl(const AirportItlwmAPSTAPeerCacheControlLayout *in);
    IOReturn setHostAPModeHidden(const AirportItlwmAPSTAHostApModeHiddenLayout *in);
    IOReturn setSoftAPParams(const AirportItlwmAPSTASoftAPParamsInputLayout *in);
    IOReturn setRsnConf(const struct apple80211_rsn_conf_data *in);
    IOReturn setSoftAPTriggerCSA(const AirportItlwmAPSTACsaInputLayout *in);
    IOReturn setSoftAPWifiNetworkInfoIE(const AirportItlwmAPSTASoftAPWifiNetworkInfoCarrierLayout *in);

    IOReturn setBeaconTemplate(const void *templateBytes,
                               size_t templateLength,
                               uint16_t beaconInterval,
                               uint8_t dtimPeriod);
    IOReturn triggerCSA(uint16_t channel, uint8_t count);
    IOReturn publishStationEventFromNet80211(uint32_t eventType,
                                             const uint8_t *mac,
                                             uint32_t flags,
                                             const uint8_t *ies = nullptr,
                                             uint32_t iesLength = 0);

private:
    void initSoftAPParameters();
    void resetRuntimeState();
    void setSoftAPPowerSaveState(uint8_t newState, uint8_t reason);
    AirportItlwmAPSTAStationTableEntryLayout *findStation(const uint8_t *mac);
    AirportItlwmAPSTAStationTableEntryLayout *allocateStation(const uint8_t *mac);
    void removeStation(const uint8_t *mac);
    void clearStation(AirportItlwmAPSTAStationTableEntryLayout *entry);
    void recomputeStationCount();
    IOReturn postStationMessage(uint32_t messageId, const void *payload, size_t payloadLength);

    AirportItlwm *owner;
    AirportItlwmAPSTAOwnerLifecycleState lifecycle;
    AirportItlwmAPSTAStateBlock state;
    uint8_t role;
    uint8_t mac[IEEE80211_ADDR_LEN];
    char bsdNameStorage[IFNAMSIZ];
    uint16_t apChannel;
    uint32_t apChannelFlags;
};

#endif /* AirportItlwmAPSTAOwner_hpp */
