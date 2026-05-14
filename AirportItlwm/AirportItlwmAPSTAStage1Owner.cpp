/*
 * Host-owned APSTA owner: role-7 lifetime, APSTA state block,
 * station table, and AP-up gate.  See AirportItlwmAPSTAStage1Owner.hpp
 * for the contract boundary.
 */
#include "AirportItlwmV2.hpp"
#include "AirportItlwmRegDiag.hpp"
#include "AirportItlwmAPSTAStage1Owner.hpp"
#include "HAL/ItlHalService.hpp"
#include <net80211/ieee80211_node.h>
#include <sys/errno.h>

OSDefineMetaClassAndStructors(AirportItlwmAPSTAStage1Owner, OSObject)

static bool apsta_mac_is_zero(const uint8_t *mac)
{
    if (mac == nullptr) {
        return true;
    }
    for (unsigned i = 0; i < IEEE80211_ADDR_LEN; i++) {
        if (mac[i] != 0) {
            return false;
        }
    }
    return true;
}

bool AirportItlwmAPSTAStage1Owner::initWithController(
    AirportItlwm *controller,
    const struct apple80211_virt_if_create_data *create)
{
    // Establish a terminal-safe state before any path that can return
    // false so a failed-init release through free()/teardown()/stopLower()
    // observes initialized members regardless of where init falls out.
    owner = nullptr;
    lifecycle = kAirportItlwmAPSTAStage1Terminal;
    role = 0;
    bzero(&state, sizeof(state));
    bzero(mac, sizeof(mac));
    bzero(bsdNameStorage, sizeof(bsdNameStorage));

    if (!OSObject::init()) {
        return false;
    }
    if (controller == nullptr || create == nullptr) {
        return false;
    }
    if (create->role != APPLE80211_VIF_SOFT_AP) {
        return false;
    }

    owner = controller;
    lifecycle = kAirportItlwmAPSTAStage1Created;
    role = create->role;

    memcpy(mac, create->mac, IEEE80211_ADDR_LEN);
    if (create->bsd_name[0] != 0) {
        strlcpy(bsdNameStorage, reinterpret_cast<const char *>(create->bsd_name), sizeof(bsdNameStorage));
    } else {
        strlcpy(bsdNameStorage, "apsta0", sizeof(bsdNameStorage));
    }

    state.softapMaxAssoc04 = 1;
    state.softapMaxAssocLimit08 = IEEE80211_AID_DEF;
    state.softapBeaconInterval14 = 100;
    state.softapDtimPeriod16 = 1;
    state.softapAppliedBeaconInterval68 = state.softapBeaconInterval14;
    state.softapAppliedDtimPeriod6a = state.softapDtimPeriod16;
    state.ownerCoreOrInterface = owner;
    state.initState268 = 1;
    state.resetState26c = 0;
    state.hostApTransitionState270 = 0;
    state.numTxQueues = kAirportItlwmAPSTATxSubQueueCount;
    state.featureGate0d = 1;
    state.featureGate0c = 1;
    return true;
}

void AirportItlwmAPSTAStage1Owner::free()
{
    teardown();
    lifecycle = kAirportItlwmAPSTAStage1Freed;
    owner = nullptr;
    OSObject::free();
}

void AirportItlwmAPSTAStage1Owner::resetRuntimeState()
{
    state.softapAssociatedStaCount00 = 0;
    state.resetState26c = 0;
    state.hostApTransitionState270 = 0;
    for (unsigned i = 0; i < kAirportItlwmAPSTAStationTableEntryCount; i++) {
        clearStation(&state.softapStaTableB8[i]);
    }
}

IOReturn AirportItlwmAPSTAStage1Owner::startLowerIfReady()
{
    if (owner == nullptr || owner->fHalService == nullptr) {
        lifecycle = kAirportItlwmAPSTAStage1LowerBlocked;
        state.resetState26c = 0;
        return kIOReturnNotReady;
    }

    if (!owner->fHalService->supportsAPMode()) {
        lifecycle = kAirportItlwmAPSTAStage1LowerBlocked;
        state.resetState26c = 0;
        return kIOReturnUnsupported;
    }

    ItlHalApConfig cfg;
    bzero(&cfg, sizeof(cfg));
    memcpy(cfg.bssid, mac, IEEE80211_ADDR_LEN);
    cfg.maxStations = state.softapMaxAssoc04;
    cfg.beaconInterval = state.softapBeaconInterval14;
    cfg.dtimPeriod = static_cast<uint8_t>(state.softapDtimPeriod16);
    IOReturn ret = owner->fHalService->startAPMode(&cfg);
    if (ret == kIOReturnSuccess) {
        lifecycle = kAirportItlwmAPSTAStage1Running;
        state.resetState26c = 1;
        state.hostApTransitionState270 = 1;
    } else {
        lifecycle = kAirportItlwmAPSTAStage1LowerBlocked;
        state.resetState26c = 0;
    }
    return ret;
}

IOReturn AirportItlwmAPSTAStage1Owner::stopLower()
{
    if (owner != nullptr && owner->fHalService != nullptr && owner->fHalService->supportsAPMode()) {
        (void)owner->fHalService->stopAPMode();
    }
    resetRuntimeState();
    if (lifecycle != kAirportItlwmAPSTAStage1Freed) {
        lifecycle = kAirportItlwmAPSTAStage1Terminal;
    }
    return kIOReturnSuccess;
}

void AirportItlwmAPSTAStage1Owner::teardown()
{
    if (lifecycle < kAirportItlwmAPSTAStage1Terminal) {
        (void)stopLower();
    }
    owner = nullptr;
}

bool AirportItlwmAPSTAStage1Owner::matchesBSDName(const uint8_t *name) const
{
    if (name == nullptr || name[0] == 0) {
        return true;
    }
    return strncmp(bsdNameStorage, reinterpret_cast<const char *>(name), sizeof(bsdNameStorage)) == 0;
}

IOReturn AirportItlwmAPSTAStage1Owner::setSoftAPExtCaps(
    const struct apple80211_softap_extended_capabilities_info *in)
{
    if (in == nullptr) {
        return kIOReturnBadArgument;
    }
    state.softapAppleVendorIEExtra50 = in->flag00;
    memcpy(state.softapAppleVendorIETail51, &in->value01,
           sizeof(state.softapAppleVendorIETail51));
    memcpy(state.softapAppleVendorIETail59, &in->value09,
           sizeof(state.softapAppleVendorIETail59));
    return kIOReturnSuccess;
}

IOReturn AirportItlwmAPSTAStage1Owner::setMaxAssoc(uint32_t value)
{
    if (value < 1) {
        value = 1;
    }
    if (value > IEEE80211_AID_DEF) {
        value = IEEE80211_AID_DEF;
    }
    state.softapMaxAssoc04 = value;
    state.softapMaxAssocLimit08 = IEEE80211_AID_DEF;

    if (owner != nullptr && owner->fHalService != nullptr) {
        struct ieee80211com *ic = owner->fHalService->get80211Controller();
        if (ic != nullptr) {
            ic->ic_max_aid = static_cast<uint16_t>(value);
        }
    }
    return kIOReturnSuccess;
}

IOReturn AirportItlwmAPSTAStage1Owner::setMisMaxSta(const struct apple80211_mis_max_sta *in)
{
    if (in == nullptr) {
        return kIOReturnBadArgument;
    }
    if (isApRunning()) {
        (void)setMaxAssoc(in->value00);
    }
    return kIOReturnSuccess;
}

IOReturn AirportItlwmAPSTAStage1Owner::setBeaconTemplate(const void *templateBytes,
                                                         size_t templateLength,
                                                         uint16_t beaconInterval,
                                                         uint8_t dtimPeriod)
{
    if (templateBytes == nullptr || templateLength == 0) {
        return kIOReturnBadArgument;
    }
    state.softapBeaconInterval14 = beaconInterval != 0 ? beaconInterval : 100;
    state.softapDtimPeriod16 = dtimPeriod != 0 ? dtimPeriod : 1;
    state.softapAppliedBeaconInterval68 = state.softapBeaconInterval14;
    state.softapAppliedDtimPeriod6a = state.softapDtimPeriod16;

    if (!isApRunning() || owner == nullptr || owner->fHalService == nullptr) {
        return kIOReturnNotReady;
    }
    return owner->fHalService->updateAPBeacon(templateBytes, templateLength,
                                              state.softapBeaconInterval14,
                                              static_cast<uint8_t>(state.softapDtimPeriod16));
}

IOReturn AirportItlwmAPSTAStage1Owner::setCipherKey(const void *keyBytes, size_t keyLength)
{
    if (keyBytes == nullptr || keyLength == 0) {
        return kIOReturnBadArgument;
    }
    if (!isApRunning() || owner == nullptr || owner->fHalService == nullptr) {
        return kIOReturnNotReady;
    }
    ItlHalApKey key;
    bzero(&key, sizeof(key));
    key.keyData = keyBytes;
    key.keyLength = keyLength;
    return owner->fHalService->setAPKey(&key);
}

IOReturn AirportItlwmAPSTAStage1Owner::triggerCSA(uint16_t channel, uint8_t count)
{
    if (!isApRunning() || owner == nullptr || owner->fHalService == nullptr) {
        return kIOReturnNotReady;
    }
    ItlHalApCSA csa;
    bzero(&csa, sizeof(csa));
    csa.channel = channel;
    csa.count = count;
    return owner->fHalService->triggerAPCSA(&csa);
}

void AirportItlwmAPSTAStage1Owner::clearStation(AirportItlwmAPSTAStationTableEntryLayout *entry)
{
    if (entry != nullptr) {
        bzero(entry, sizeof(*entry));
    }
}

AirportItlwmAPSTAStationTableEntryLayout *
AirportItlwmAPSTAStage1Owner::findStation(const uint8_t *macAddr)
{
    if (apsta_mac_is_zero(macAddr)) {
        return nullptr;
    }
    for (unsigned i = 0; i < kAirportItlwmAPSTAStationTableEntryCount; i++) {
        AirportItlwmAPSTAStationTableEntryLayout *entry = &state.softapStaTableB8[i];
        if (entry->active00 && memcmp(entry->mac01, macAddr, IEEE80211_ADDR_LEN) == 0) {
            return entry;
        }
    }
    return nullptr;
}

AirportItlwmAPSTAStationTableEntryLayout *
AirportItlwmAPSTAStage1Owner::allocateStation(const uint8_t *macAddr)
{
    AirportItlwmAPSTAStationTableEntryLayout *entry = findStation(macAddr);
    if (entry != nullptr) {
        return entry;
    }
    for (unsigned i = 0; i < kAirportItlwmAPSTAStationTableEntryCount; i++) {
        entry = &state.softapStaTableB8[i];
        if (!entry->active00) {
            bzero(entry, sizeof(*entry));
            entry->active00 = 1;
            memcpy(entry->mac01, macAddr, IEEE80211_ADDR_LEN);
            entry->sleepState10 = kAirportItlwmAPSTAStationTableDefaultSleepState;
            recomputeStationCount();
            return entry;
        }
    }
    return nullptr;
}

void AirportItlwmAPSTAStage1Owner::removeStation(const uint8_t *macAddr)
{
    AirportItlwmAPSTAStationTableEntryLayout *entry = findStation(macAddr);
    if (entry != nullptr) {
        clearStation(entry);
        recomputeStationCount();
    }
}

void AirportItlwmAPSTAStage1Owner::recomputeStationCount()
{
    uint32_t count = 0;
    for (unsigned i = 0; i < kAirportItlwmAPSTAStationTableEntryCount; i++) {
        if (state.softapStaTableB8[i].active00) {
            count++;
        }
    }
    state.softapAssociatedStaCount00 = count;
}

IOReturn AirportItlwmAPSTAStage1Owner::publishStationEventFromNet80211(
    uint32_t eventType,
    const uint8_t *macAddr,
    uint32_t flags)
{
    if (apsta_mac_is_zero(macAddr)) {
        return kIOReturnBadArgument;
    }

    switch (eventType) {
        case kAirportItlwmAPSTAEventAuthInd:
            return kIOReturnSuccess;
        case kAirportItlwmAPSTAEventAssocInd:
        case kAirportItlwmAPSTAEventReassocInd: {
            AirportItlwmAPSTAStationTableEntryLayout *entry = allocateStation(macAddr);
            if (entry == nullptr) {
                return static_cast<IOReturn>(0xe00002bc);
            }
            entry->aihsFlag20 = (flags & kAirportItlwmAPSTAEventAssocFlagAihs) ? 1 : 0;
            entry->sharingFlag24 = (flags & kAirportItlwmAPSTAEventAssocFlagSharing) ? 1 : 0;
            entry->appleStationFlag28 = (flags & kAirportItlwmAPSTAEventAssocFlagAppleStation) ? 1 : 0;
            recomputeStationCount();
            return kIOReturnSuccess;
        }
        case kAirportItlwmAPSTAEventDeauth:
        case kAirportItlwmAPSTAEventDeauthInd:
        case kAirportItlwmAPSTAEventDisassoc:
        case kAirportItlwmAPSTAEventDisassocInd:
            removeStation(macAddr);
            return kIOReturnSuccess;
        case kAirportItlwmAPSTAEventActionFrame: {
            AirportItlwmAPSTAStationTableEntryLayout *entry = findStation(macAddr);
            if (entry != nullptr) {
                entry->sleepState10 = (flags != 0)
                    ? kAirportItlwmAPSTAStationTableLowPowerSleepState
                    : kAirportItlwmAPSTAStationTableAwakeSleepState;
            }
            return kIOReturnSuccess;
        }
        default:
            return kIOReturnUnsupported;
    }
}

extern "C" void AirportItlwmAPSTAStage1Net80211Event(
    struct ieee80211com *ic,
    struct ieee80211_node *ni,
    int event,
    void *arg)
{
    (void)ic;
    AirportItlwmAPSTAStage1Owner *owner = static_cast<AirportItlwmAPSTAStage1Owner *>(arg);
    if (owner == nullptr || ni == nullptr) {
        return;
    }
    (void)owner->publishStationEventFromNet80211(static_cast<uint32_t>(event), ni->ni_macaddr, 0);
}
