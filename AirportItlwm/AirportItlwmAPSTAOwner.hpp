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
    void prepareForRadioReset();
    IOReturn resumeAfterRadioReset();
    void teardown();

    bool isCreated() const { return lifecycle >= kAirportItlwmAPSTAOwnerCreated &&
                                    lifecycle < kAirportItlwmAPSTAOwnerTerminal; }
    bool isApRunning() const { return lifecycle == kAirportItlwmAPSTAOwnerRunning &&
                                      state.resetState26c != 0; }
    bool shouldPublishPrimaryOpMode() const {
        /* AppleBCMWLANCore::getOP_MODE normally gates the APSTA vtable call
         * only on the owner's AP-up word at state +0x26c.  IWX has to finish
         * its lower start asynchronously, however, while standard Internet
         * Sharing identifies its still-open public transaction by enabling
         * the APSTA interface and repeating HOST_AP_MODE.  Do not expose
         * SWAP in that one event-delimited interval: airportd treats SWAP as
         * a completed transaction and rejects its required repeat before it
         * can reach this owner.  Direct CoreWLAN starts never drive that
         * interface-enable edge and therefore still publish immediately at
         * the real lower RUNNING boundary. */
        return isApRunning() &&
            !interfaceDrivenHostAPConfirmationPending;
    }
    void noteInterfaceEnableDuringPendingHostAPStart();
    /* Consume exactly the public role-7 handoff scan observed between its
     * interface-enable edge and the repeated HOST_AP_MODE carrier.  This
     * protects the already-associated primary BSS from a synthetic generic
     * RUN -> SCAN transition; it is not a general scan suppression gate. */
    bool armPrimaryStaHandoffScan(struct ieee80211com *ic);
    bool consumePrimaryStaHandoffScan(struct ieee80211com *ic, int arg);
    /* The lower IWN PAN transition has a short, AP-owned carrier withdrawal
     * on the primary interface. Preserve that carrier only while the
     * original STA BSS is still the authoritative, authorized RUN context;
     * ordinary deauthentication has already moved it out of that state. */
    bool shouldRetainPrimaryStaCarrier() const;
    const char *bsdName() const { return bsdNameStorage; }
    bool matchesBSDName(const uint8_t *name) const;
    void copyMacAddress(uint8_t *address) const;
    IOReturn setMacAddress(const uint8_t *address);

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
                                             const uint8_t *ies = nullptr,
                                             uint32_t iesLength = 0,
                                             uint32_t status = 0,
                                             uint32_t reason = 0,
                                             uint32_t authType = 0,
                                             const uint8_t *eventData = nullptr,
                                             uint32_t eventDataLength = 0);

private:
    void initSoftAPParameters();
    void resetRuntimeState();
    IOReturn driveLowerStopToTerminal();
    void restoreRetainedPrimaryStaLinkAfterStop();
    void prepareEmptyAPForRadioReset();
    void prepareRetainedLowerReset(uint16_t lowerChannel);
    void setSoftAPPowerSaveState(uint8_t newState, uint8_t reason);
    bool areAllStationsInLowPowerMode() const;
    AirportItlwmAPSTAStationTableEntryLayout *findStation(const uint8_t *mac);
    AirportItlwmAPSTAStationTableEntryLayout *allocateStation(const uint8_t *mac);
    void removeStation(const uint8_t *mac);
    void clearStation(AirportItlwmAPSTAStationTableEntryLayout *entry);
    void noteLowerAssociatedStation(const uint8_t *mac);
    void forgetLowerAssociatedStation(const uint8_t *mac);
    void clearLowerAssociatedStations();
    IOReturn postStationMessage(uint32_t messageId, const void *payload, size_t payloadLength);

    AirportItlwm *owner;
    AirportItlwmAPSTAOwnerLifecycleState lifecycle;
    AirportItlwmAPSTAStateBlock state;
    uint8_t role;
    uint8_t mac[IEEE80211_ADDR_LEN];
    char bsdNameStorage[IFNAMSIZ];
    uint16_t apChannel;
    uint32_t apChannelFlags;
    uint32_t apAuthUpper;
    uint8_t apCredential[0x40];
    uint32_t apCredentialLength;
    bool lowerStopPending;
    bool radioResetResumePending;
    bool radioResetWaitForPrimaryStaRun;
    bool radioResetPrimaryStaScanHandoff;
    bool initialHostAPAdmissionPending;
    bool confirmedHostAPStartPending;
    bool interfaceDrivenHostAPConfirmationPending;
    bool primaryStaHandoffScanArmed;
    uint16_t radioResetResumeWaitTicks;
    uint8_t lowerAssociatedStaCount;
    uint8_t lowerAssociatedStaMacs[kAirportItlwmAPSTAStationTableEntryCount]
                                   [IEEE80211_ADDR_LEN];
};

#endif /* AirportItlwmAPSTAOwner_hpp */
