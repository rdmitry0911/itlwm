/*
 * Host-owned APSTA owner.
 *
 * This surface intentionally separates role-7 owner lifetime from
 * functional Intel AP/GO firmware bring-up: role-7 create/delete can
 * allocate and tear down the owner, while AP-up remains false unless
 * a HAL backend explicitly advertises and starts AP mode.
 */
#ifndef AirportItlwmAPSTAStage1Owner_hpp
#define AirportItlwmAPSTAStage1Owner_hpp

#include <IOKit/IOLib.h>
#include <libkern/c++/OSObject.h>
#include <net80211/ieee80211_var.h>
#include "AirportItlwmAPSTAInterface.hpp"
#include "Airport/apple80211_ioctl.h"

class AirportItlwm;
struct ItlHalApConfig;
struct ItlHalApKey;
struct ItlHalApCSA;
struct ItlHalApStationCommand;
struct ieee80211_node;

extern "C" void AirportItlwmAPSTAStage1Net80211Event(
    struct ieee80211com *ic,
    void *arg,
    uint32_t eventType,
    const struct ieee80211_node *ni);

enum AirportItlwmAPSTAStage1LifecycleState {
    kAirportItlwmAPSTAStage1Unallocated = 0,
    kAirportItlwmAPSTAStage1Allocated,
    kAirportItlwmAPSTAStage1Created,
    kAirportItlwmAPSTAStage1LowerBlocked,
    kAirportItlwmAPSTAStage1Running,
    kAirportItlwmAPSTAStage1Terminal,
    kAirportItlwmAPSTAStage1Freed,
};

class AirportItlwmAPSTAStage1Owner : public OSObject {
    OSDeclareDefaultStructors(AirportItlwmAPSTAStage1Owner)

public:
    bool initWithController(AirportItlwm *controller,
                            const struct apple80211_virt_if_create_data *create);
    void free() override;

    IOReturn startLowerIfReady();
    IOReturn stopLower();
    void teardown();

    bool isCreated() const { return lifecycle >= kAirportItlwmAPSTAStage1Created &&
                                    lifecycle < kAirportItlwmAPSTAStage1Terminal; }
    bool isApRunning() const { return lifecycle == kAirportItlwmAPSTAStage1Running &&
                                      state.resetState26c != 0; }
    const char *bsdName() const { return bsdNameStorage; }
    bool matchesBSDName(const uint8_t *name) const;

    AirportItlwmAPSTAStateBlock *stateBlock() { return &state; }
    const AirportItlwmAPSTAStateBlock *stateBlock() const { return &state; }

    IOReturn setSoftAPExtCaps(const struct apple80211_softap_extended_capabilities_info *in);
    IOReturn setMisMaxSta(const struct apple80211_mis_max_sta *in);
    IOReturn setMaxAssoc(uint32_t value);

    IOReturn setBeaconTemplate(const void *templateBytes,
                               size_t templateLength,
                               uint16_t beaconInterval,
                               uint8_t dtimPeriod);
    IOReturn setCipherKey(const void *keyBytes, size_t keyLength);
    IOReturn triggerCSA(uint16_t channel, uint8_t count);
    IOReturn publishStationEventFromNet80211(uint32_t eventType,
                                             const uint8_t *mac,
                                             uint32_t flags);

private:
    void resetRuntimeState();
    AirportItlwmAPSTAStationTableEntryLayout *findStation(const uint8_t *mac);
    AirportItlwmAPSTAStationTableEntryLayout *allocateStation(const uint8_t *mac);
    void removeStation(const uint8_t *mac);
    void clearStation(AirportItlwmAPSTAStationTableEntryLayout *entry);
    void recomputeStationCount();

    AirportItlwm *owner;
    AirportItlwmAPSTAStage1LifecycleState lifecycle;
    AirportItlwmAPSTAStateBlock state;
    uint8_t role;
    uint8_t mac[IEEE80211_ADDR_LEN];
    char bsdNameStorage[IFNAMSIZ];
};

#endif /* AirportItlwmAPSTAStage1Owner_hpp */
