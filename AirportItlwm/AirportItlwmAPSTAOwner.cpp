/*
 * Host-owned APSTA owner: role-7 lifetime, APSTA state block,
 * station table, and AP-up gate. See AirportItlwmAPSTAOwner.hpp
 * for the contract boundary.
 */
#include "AirportItlwmV2.hpp"
#include "AirportItlwmRegDiag.hpp"
#include "AirportItlwmAPSTAOwner.hpp"
#include "HAL/ItlHalService.hpp"
#include <net80211/ieee80211_node.h>
#include <sys/errno.h>

OSDefineMetaClassAndStructors(AirportItlwmAPSTAOwner, OSObject)

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

bool AirportItlwmAPSTAOwner::initWithController(
    AirportItlwm *controller,
    const struct apple80211_virt_if_create_data *create)
{
    // Establish a terminal-safe state before any path that can return
    // false so a failed-init release through free()/teardown()/stopLower()
    // observes initialized members regardless of where init falls out.
    owner = nullptr;
    lifecycle = kAirportItlwmAPSTAOwnerTerminal;
    role = 0;
    bzero(&state, sizeof(state));
    bzero(mac, sizeof(mac));
    apChannel = 0;
    apChannelFlags = 0;
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
    lifecycle = kAirportItlwmAPSTAOwnerCreated;
    role = create->role;

    memcpy(mac, create->mac, IEEE80211_ADDR_LEN);
    if (create->bsd_name[0] != 0) {
        strlcpy(bsdNameStorage, reinterpret_cast<const char *>(create->bsd_name), sizeof(bsdNameStorage));
    } else {
        strlcpy(bsdNameStorage, "apsta0", sizeof(bsdNameStorage));
    }

    state.softapBeaconInterval14 = 100;
    state.ownerCoreOrInterface = owner;
    state.initState268 = 1;
    state.resetState26c = 0;
    state.hostApTransitionState270 = 0;
    state.numTxQueues = kAirportItlwmAPSTATxSubQueueCount;
    state.featureGate0d = 1;
    state.featureGate0c = 1;
    initSoftAPParameters();
    return true;
}

void AirportItlwmAPSTAOwner::free()
{
    teardown();
    lifecycle = kAirportItlwmAPSTAOwnerFreed;
    owner = nullptr;
    OSObject::free();
}

void AirportItlwmAPSTAOwner::initSoftAPParameters()
{
    bzero(state.softapStats, sizeof(state.softapStats));
    state.softapRuntime1a8 = 0;
    for (unsigned i = 0; i < kAirportItlwmAPSTAStationTableEntryCount; i++) {
        clearStation(&state.softapStaTableB8[i]);
    }
    state.softapAssociatedStaCount00 = 0;
    state.softapMaxAssoc04 = 1;
    state.softapMaxAssocLimit08 = IEEE80211_AID_DEF;
    state.softapDtimPeriod16 = kAirportItlwmAPSTAInitSoftAPDefaultDtimPeriod;
    state.softapParam18 = kAirportItlwmAPSTAInitSoftAPDefaultParam18;
    state.softapParam1c = kAirportItlwmAPSTAInitSoftAPDefaultParam1c;
    state.softapParam20 = kAirportItlwmAPSTAInitSoftAPDefaultParam20;
    state.softapParam24 = kAirportItlwmAPSTAInitSoftAPDefaultParam24;
    state.softapParam28 = kAirportItlwmAPSTAInitSoftAPDefaultParam28;
    state.softapAppliedBeaconInterval68 = state.softapBeaconInterval14;
    state.softapAppliedDtimPeriod6a = state.softapDtimPeriod16;
}

void AirportItlwmAPSTAOwner::resetRuntimeState()
{
    state.resetState26c = 0;
    state.resetFlag329 = 0;
    state.hostApTransitionState270 = 0;
    state.softapAssociatedStaCount00 = 0;
    state.softapRuntimeB0 = 0;
    state.softapPowerStateB4 = 0;
    for (unsigned i = 0; i < kAirportItlwmAPSTAStationTableEntryCount; i++) {
        clearStation(&state.softapStaTableB8[i]);
    }
    setSoftAPPowerSaveState(kAirportItlwmAPSTAResetPowerSaveState,
                            kAirportItlwmAPSTAResetPowerSaveReason);
    bzero(state.softapStats, sizeof(state.softapStats));
    state.softapRuntime90 = 0;
    state.softapRuntime98 = 0;
    state.softapRuntimeA0 = 0;
}

void AirportItlwmAPSTAOwner::setSoftAPPowerSaveState(uint8_t newState, uint8_t reason)
{
    if ((state.softapParam0e & 1) == 0) {
        return;
    }
    if (reason == kAirportItlwmAPSTAPowerStateReasonInfraScan ||
        newState > kAirportItlwmAPSTAPowerStateMaxKnown) {
        return;
    }

    if (state.softapMode10 != newState) {
        const uint32_t recordOffset =
            kAirportItlwmAPSTAPowerStateTransitionRecordBaseOffset -
            kAirportItlwmAPSTASoftAPStatsOffset +
            newState * kAirportItlwmAPSTAPowerStateTransitionRecordStride;
        uint32_t transitionCount = 0;
        memcpy(&transitionCount,
               &state.softapStats[recordOffset +
                                   kAirportItlwmAPSTAPowerStateTransitionCountOffset],
               sizeof(transitionCount));
        transitionCount++;
        memcpy(&state.softapStats[recordOffset +
                                  kAirportItlwmAPSTAPowerStateTransitionCountOffset],
               &transitionCount, sizeof(transitionCount));

        if (newState == kAirportItlwmAPSTAPowerStateOff) {
            state.lowTrafficCounter64 = 0;
            if (reason == kAirportItlwmAPSTAPowerStateReasonReset ||
                reason == kAirportItlwmAPSTAPowerStateReasonPowerOff) {
                state.powerAssertionFlag0c = 0;
            }
        } else if (newState == kAirportItlwmAPSTAPowerStateOn) {
            state.powerAssertionFlag0c = 0;
        } else if (newState == kAirportItlwmAPSTAPowerStateLowPower) {
            state.powerAssertionFlag0c =
                static_cast<uint8_t>(kAirportItlwmAPSTAHoldPowerAssertionStateValue);
            state.lowTrafficCounter64 = 0;
        }
    }

    state.softapMode10 = newState;
}

IOReturn AirportItlwmAPSTAOwner::startLowerIfReady()
{
    if (owner == nullptr || owner->fHalService == nullptr) {
        lifecycle = kAirportItlwmAPSTAOwnerLowerBlocked;
        state.resetState26c = 0;
        return kIOReturnNotReady;
    }

    if (!owner->fHalService->supportsAPMode()) {
        lifecycle = kAirportItlwmAPSTAOwnerLowerBlocked;
        state.resetState26c = 0;
        return kIOReturnUnsupported;
    }

    ItlHalApConfig cfg;
    bzero(&cfg, sizeof(cfg));
    memcpy(cfg.bssid, mac, IEEE80211_ADDR_LEN);
    cfg.channel = apChannel;
    cfg.maxStations = state.softapMaxAssoc04;
    cfg.beaconInterval = state.softapBeaconInterval14;
    cfg.dtimPeriod = static_cast<uint8_t>(state.softapDtimPeriod16);
    IOReturn ret = owner->fHalService->startAPMode(&cfg);
    if (ret == kIOReturnSuccess) {
        lifecycle = kAirportItlwmAPSTAOwnerRunning;
        state.resetState26c = 1;
        state.hostApTransitionState270 = 1;
    } else {
        lifecycle = kAirportItlwmAPSTAOwnerLowerBlocked;
        state.resetState26c = 0;
    }
    return ret;
}

IOReturn AirportItlwmAPSTAOwner::stopLower()
{
    if (owner != nullptr && owner->fHalService != nullptr && owner->fHalService->supportsAPMode()) {
        (void)owner->fHalService->stopAPMode();
    }
    resetRuntimeState();
    if (lifecycle != kAirportItlwmAPSTAOwnerFreed) {
        lifecycle = kAirportItlwmAPSTAOwnerTerminal;
    }
    return kIOReturnSuccess;
}

void AirportItlwmAPSTAOwner::teardown()
{
    if (lifecycle < kAirportItlwmAPSTAOwnerTerminal) {
        (void)stopLower();
    }
    owner = nullptr;
}

bool AirportItlwmAPSTAOwner::matchesBSDName(const uint8_t *name) const
{
    if (name == nullptr || name[0] == 0) {
        return false;
    }
    return strncmp(bsdNameStorage, reinterpret_cast<const char *>(name), sizeof(bsdNameStorage)) == 0;
}

IOReturn AirportItlwmAPSTAOwner::setSSID(const struct apple80211_ssid_data *in)
{
    (void)in;
    return static_cast<IOReturn>(kAirportItlwmAPSTASetSsidSuccessReturn);
}

IOReturn AirportItlwmAPSTAOwner::setChannel(const struct apple80211_channel_data *in)
{
    if (in == nullptr || in->channel.channel >= kAirportItlwmAPSTASetChannelTrapThreshold) {
        return static_cast<IOReturn>(kAirportItlwmAPSTASetChannelInvalidArgumentReturn);
    }
    apChannel = static_cast<uint16_t>(in->channel.channel);
    apChannelFlags = in->channel.flags;
    if (!isApRunning() || owner == nullptr || owner->fHalService == nullptr) {
        return static_cast<IOReturn>(kAirportItlwmAPSTASetChannelInvalidSoftAPInfoReturn);
    }
    return triggerCSA(apChannel, 0);
}

IOReturn AirportItlwmAPSTAOwner::setCipherKey(const struct apple80211_key *key)
{
    if (!isApRunning() || owner == nullptr || owner->fHalService == nullptr) {
        return static_cast<IOReturn>(kAirportItlwmAPSTASetCipherKeyNotUpReturn);
    }
    if (key == nullptr) {
        return kIOReturnBadArgument;
    }
    if (key->key_cipher_type == kAirportItlwmAPSTASetCipherKeyCipherNone) {
        return static_cast<IOReturn>(kAirportItlwmAPSTASetCipherKeyUnsupportedCipherReturn);
    }
    if (key->key_cipher_type != kAirportItlwmAPSTASetCipherKeyCipherNone &&
        key->key_cipher_type != kAirportItlwmAPSTASetCipherKeyCipherAccepted3 &&
        key->key_cipher_type != kAirportItlwmAPSTASetCipherKeyCipherAccepted5) {
        return static_cast<IOReturn>(kAirportItlwmAPSTASetCipherKeyUnsupportedCipherReturn);
    }
    ItlHalApKey halKey;
    bzero(&halKey, sizeof(halKey));
    halKey.station = key->key_ea.octet;
    halKey.keyIndex = static_cast<uint8_t>(key->key_index);
    halKey.cipher = static_cast<uint8_t>(key->key_cipher_type);
    halKey.keyData = key->key;
    halKey.keyLength = key->key_len;
    halKey.rsc = key->key_rsc;
    halKey.rscLength = key->key_rsc_len;
    return owner->fHalService->setAPKey(&halKey);
}

IOReturn AirportItlwmAPSTAOwner::getHostAPModeHidden(
    AirportItlwmAPSTAHostApModeHiddenOutputLayout *out) const
{
    if (out == nullptr) {
        return static_cast<IOReturn>(kAirportItlwmAPSTAGetHostApModeHiddenInvalidArgumentReturn);
    }
    out->hidden00 = kAirportItlwmAPSTAGetHostApModeHiddenValue;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmAPSTAOwner::getSoftAPParams(
    AirportItlwmAPSTASoftAPParamsOutputLayout *out) const
{
    if (out == nullptr) {
        return kIOReturnBadArgument;
    }
    out->param04 = state.softapParam18;
    out->param08 = state.softapParam1c;
    out->param0c = state.softapParam20;
    out->param10 = state.softapParam24;
    out->param14 = state.softapAppliedBeaconInterval68;
    out->mode16 = state.softapMode10;
    out->enabled17 = state.softapParam0e & 1;
    out->param18 = state.softapParam28;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmAPSTAOwner::getSoftAPStats(
    AirportItlwmAPSTASoftAPStatsLayout *out) const
{
    if (out == nullptr) {
        return kIOReturnBadArgument;
    }
    memcpy(out->stats, state.softapStats, kAirportItlwmAPSTAGetSoftAPStatsCopySize);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmAPSTAOwner::getStationList(struct apple80211_sta_data *out)
{
    if (out == nullptr) {
        return static_cast<IOReturn>(kAirportItlwmAPSTAGetStationListNullReturn);
    }
    if (!isApRunning()) {
        return static_cast<IOReturn>(kAirportItlwmAPSTAGetStationListNotUpReturn);
    }

    uint32_t count = 0;
    for (unsigned i = 0; i < kAirportItlwmAPSTAStationTableEntryCount &&
                         count < APPLE80211_MAX_STATIONS; i++) {
        AirportItlwmAPSTAStationTableEntryLayout *entry = &state.softapStaTableB8[i];
        if (!entry->active00) {
            continue;
        }
        out->station_list[count].version = APPLE80211_VERSION;
        memcpy(out->station_list[count].sta_mac.octet, entry->mac01,
               kAirportItlwmAPSTAStationTableMacSize);
        out->station_list[count].sta_rssi = 0;
        count++;
    }
    out->num_stations = count;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmAPSTAOwner::getStaIEList(AirportItlwmAPSTAStaIEDataLayout *out)
{
    if (out == nullptr) {
        return static_cast<IOReturn>(kAirportItlwmAPSTAGetStaIEListNullReturn);
    }
    AirportItlwmAPSTAStationTableEntryLayout *entry = findStation(out->mac04);
    if (entry == nullptr) {
        return static_cast<IOReturn>(kAirportItlwmAPSTAGetStaIEListNotFoundReturn);
    }
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmAPSTAOwner::getStaStats(AirportItlwmAPSTAStaStatsDataLayout *out)
{
    if (!isApRunning()) {
        return static_cast<IOReturn>(kAirportItlwmAPSTAGetStaStatsNotUpReturn);
    }
    if (out == nullptr) {
        return static_cast<IOReturn>(kAirportItlwmAPSTAGetStaStatsNullReturn);
    }
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmAPSTAOwner::getKeyRsc(AirportItlwmAPSTAKeyRscDataLayout *out)
{
    if (out == nullptr) {
        return kIOReturnBadArgument;
    }
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmAPSTAOwner::setSoftAPExtCaps(
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
    state.reserved0061 = 0;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmAPSTAOwner::setMaxAssoc(uint32_t value)
{
    if (state.softapMaxAssoc04 == value) {
        return kIOReturnSuccess;
    }
    const uint32_t payload = state.softapAssociatedStaCount00 + value;
    if (payload > state.softapMaxAssocLimit08) {
        return kIOReturnSuccess;
    }

    state.softapMaxAssoc04 = value;

    if (owner != nullptr && owner->fHalService != nullptr) {
        struct ieee80211com *ic = owner->fHalService->get80211Controller();
        if (ic != nullptr) {
            ic->ic_max_aid = static_cast<uint16_t>(payload);
        }
    }
    return kIOReturnSuccess;
}

IOReturn AirportItlwmAPSTAOwner::setMisMaxSta(const struct apple80211_mis_max_sta *in)
{
    if (!isApRunning()) {
        return static_cast<IOReturn>(kAirportItlwmAPSTASetMisMaxStaReturn);
    }
    if (in == nullptr) {
        return kIOReturnBadArgument;
    }
    (void)setMaxAssoc(in->value00);
    return static_cast<IOReturn>(kAirportItlwmAPSTASetMisMaxStaReturn);
}

IOReturn AirportItlwmAPSTAOwner::setPeerCacheControl(
    const AirportItlwmAPSTAPeerCacheControlLayout *in)
{
    (void)in;
    return static_cast<IOReturn>(kAirportItlwmAPSTASetPeerCacheControlReturn);
}

IOReturn AirportItlwmAPSTAOwner::setHostAPModeHidden(
    const AirportItlwmAPSTAHostApModeHiddenLayout *in)
{
    if (!isApRunning() || owner == nullptr || owner->fHalService == nullptr) {
        return static_cast<IOReturn>(kAirportItlwmAPSTAHiddenNotUpReturn);
    }
    if (in == nullptr) {
        return static_cast<IOReturn>(kAirportItlwmAPSTAHiddenInvalidArgumentReturn);
    }
    if (in->hidden04 > kAirportItlwmAPSTAHiddenMaxAcceptedValue) {
        return static_cast<IOReturn>(kAirportItlwmAPSTAHiddenInvalidArgumentReturn);
    }

    IOReturn ret = owner->fHalService->setAPHidden(in->hidden04 != 0);
    if (ret == kIOReturnSuccess) {
        state.hiddenNetworkFlag0d = static_cast<uint8_t>(in->hidden04 != 0);
        if (in->hidden04 == 0 && isApRunning()) {
            setSoftAPPowerSaveState(kAirportItlwmAPSTAHiddenClearPowerSaveState,
                                    kAirportItlwmAPSTAHiddenClearPowerSaveReason);
            state.softapParam0e = 0;
            state.powerAssertionFlag0c =
                static_cast<uint8_t>(kAirportItlwmAPSTAHoldPowerAssertionStateValue);
        }
    }
    return ret;
}

IOReturn AirportItlwmAPSTAOwner::setSoftAPParams(
    const AirportItlwmAPSTASoftAPParamsInputLayout *in)
{
    if (in == nullptr) {
        return kIOReturnBadArgument;
    }
    const bool wasEnabled = (state.softapParam0e & 1) != 0;
    const bool disableRequested = in->enabled17 == 0;
    if (wasEnabled && disableRequested && state.resetState26c != 0) {
        setSoftAPPowerSaveState(kAirportItlwmAPSTASetSoftAPParamsClearPowerState,
                                kAirportItlwmAPSTASetSoftAPParamsClearPowerReason);
        state.softapParam0e = 0;
    }
    if (in->param14 != kAirportItlwmAPSTASetSoftAPParamsBeaconSentinel &&
        in->param14 != state.softapAppliedBeaconInterval68) {
        state.softapAppliedBeaconInterval68 = in->param14;
        state.softapBeaconInterval14 = in->param14;
    }
    state.softapParam18 = in->param04;
    state.softapParam1c = in->param08;
    state.softapParam20 = in->param0c;
    state.softapParam24 = in->param10;
    state.softapParam28 = in->param18;
    if (!wasEnabled || !disableRequested) {
        setSoftAPPowerSaveState(kAirportItlwmAPSTASetSoftAPParamsHoldPowerState,
                                kAirportItlwmAPSTASetSoftAPParamsHoldPowerReason);
    }
    return static_cast<IOReturn>(kAirportItlwmAPSTASetSoftAPParamsReturn);
}

IOReturn AirportItlwmAPSTAOwner::setSoftAPWifiNetworkInfoIE(
    const AirportItlwmAPSTASoftAPWifiNetworkInfoCarrierLayout *in)
{
    if (kAirportItlwmAPSTAWifiNetworkInfoLocalFeatureGate46Enabled == 0) {
        return kIOReturnSuccess;
    }
    if (in == nullptr) {
        return kIOReturnBadArgument;
    }
    if (in->length03 > kAirportItlwmAPSTAWifiNetworkInfoMaxAcceptedLength) {
        return static_cast<IOReturn>(kAirportItlwmAPSTAInvalidSoftAPInfoReturn);
    }
    memcpy(state.softapWifiNetworkInfoIE, in,
           kAirportItlwmAPSTAWifiNetworkInfoIESize);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmAPSTAOwner::setBeaconTemplate(const void *templateBytes,
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

IOReturn AirportItlwmAPSTAOwner::triggerCSA(uint16_t channel, uint8_t count)
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

IOReturn AirportItlwmAPSTAOwner::setSoftAPTriggerCSA(
    const AirportItlwmAPSTACsaInputLayout *in)
{
    if (!isApRunning() || owner == nullptr || owner->fHalService == nullptr ||
        (state.resetFlag329 & kAirportItlwmAPSTACsaResetFlagBit) == 0) {
        return static_cast<IOReturn>(kAirportItlwmAPSTACsaNotUpReturn);
    }
    if (in == nullptr) {
        return static_cast<IOReturn>(kAirportItlwmAPSTACsaInvalidArgumentReturn);
    }
    if (in->channel04.channelNumber04 < kAirportItlwmAPSTACsaMinimumPrimaryChannel ||
        in->channel04.channelNumber04 >= kAirportItlwmAPSTACsaMaximumExcludedPrimaryChannel ||
        in->channel04.channelNumber04 >= kAirportItlwmAPSTACsaMaximumExcludedChannelSpec) {
        return static_cast<IOReturn>(kAirportItlwmAPSTACsaInvalidArgumentReturn);
    }

    ItlHalApCSA csa;
    bzero(&csa, sizeof(csa));
    csa.channel = static_cast<uint16_t>(in->channel04.channelNumber04);
    csa.count = in->mode10;
    return owner->fHalService->triggerAPCSA(&csa);
}

void AirportItlwmAPSTAOwner::clearStation(AirportItlwmAPSTAStationTableEntryLayout *entry)
{
    if (entry != nullptr) {
        bzero(entry, sizeof(*entry));
    }
}

AirportItlwmAPSTAStationTableEntryLayout *
AirportItlwmAPSTAOwner::findStation(const uint8_t *macAddr)
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
AirportItlwmAPSTAOwner::allocateStation(const uint8_t *macAddr)
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

void AirportItlwmAPSTAOwner::removeStation(const uint8_t *macAddr)
{
    AirportItlwmAPSTAStationTableEntryLayout *entry = findStation(macAddr);
    if (entry != nullptr) {
        clearStation(entry);
        recomputeStationCount();
    }
}

void AirportItlwmAPSTAOwner::recomputeStationCount()
{
    uint32_t count = 0;
    for (unsigned i = 0; i < kAirportItlwmAPSTAStationTableEntryCount; i++) {
        if (state.softapStaTableB8[i].active00) {
            count++;
        }
    }
    state.softapAssociatedStaCount00 = count;
}

IOReturn AirportItlwmAPSTAOwner::setStationAuthorization(
    const AirportItlwmAPSTAStaAuthorizeInputLayout *in)
{
    if (in == nullptr) {
        return static_cast<IOReturn>(kAirportItlwmAPSTAStaAuthorizeNullReturn);
    }
    if (!isApRunning() || owner == nullptr || owner->fHalService == nullptr) {
        return static_cast<IOReturn>(kAirportItlwmAPSTASoftAPNotReadyReturn);
    }
    ItlHalApStationCommand cmd;
    bzero(&cmd, sizeof(cmd));
    cmd.command = (in->authorizeFlag04 != 0)
        ? kAirportItlwmAPSTAStaAuthorizeSelectorIfAuthorized
        : kAirportItlwmAPSTAStaAuthorizeSelectorIfNotAuthorized;
    cmd.station = in->mac08;
    cmd.flags = in->authorizeFlag04;
    return owner->fHalService->sendAPStationCommand(&cmd);
}

IOReturn AirportItlwmAPSTAOwner::setStationDisassociation(
    const AirportItlwmAPSTAStaDisassocInputLayout *in,
    bool deauth)
{
    if (!isApRunning() || owner == nullptr || owner->fHalService == nullptr) {
        return static_cast<IOReturn>(kAirportItlwmAPSTASoftAPNotReadyReturn);
    }
    if (in == nullptr) {
        return kIOReturnBadArgument;
    }
    ItlHalApStationCommand cmd;
    bzero(&cmd, sizeof(cmd));
    (void)deauth;
    cmd.command = kAirportItlwmAPSTAStaDisassocVirtualIoctlSelector;
    cmd.flags = in->reason04;
    cmd.disassocReason = in->reason04;
    cmd.disassocCarrierValue08 = in->value08;
    cmd.disassocCarrierValue0c = in->value0c;
    cmd.disassocPayloadReason00 = in->reason04;
    cmd.disassocPayloadValue04 = in->value08;
    cmd.disassocPayloadValue08 = in->value0c;
    cmd.disassocPayloadSentinel0a = kAirportItlwmAPSTAStaDisassocPayloadSentinel0aValue;
    return owner->fHalService->sendAPStationCommand(&cmd);
}

IOReturn AirportItlwmAPSTAOwner::publishStationEventFromNet80211(
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
            if (isApRunning() && owner != nullptr && owner->fHalService != nullptr) {
                ItlHalApStationCommand cmd;
                bzero(&cmd, sizeof(cmd));
                cmd.command = eventType;
                cmd.station = macAddr;
                cmd.flags = flags;
                return owner->fHalService->sendAPStationCommand(&cmd);
            }
            return kIOReturnSuccess;
        }
        case kAirportItlwmAPSTAEventDeauth:
        case kAirportItlwmAPSTAEventDeauthInd:
        case kAirportItlwmAPSTAEventDisassoc:
        case kAirportItlwmAPSTAEventDisassocInd:
            removeStation(macAddr);
            if (isApRunning() && owner != nullptr && owner->fHalService != nullptr) {
                ItlHalApStationCommand cmd;
                bzero(&cmd, sizeof(cmd));
                cmd.command = eventType;
                cmd.station = macAddr;
                cmd.flags = flags;
                return owner->fHalService->sendAPStationCommand(&cmd);
            }
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

extern "C" void AirportItlwmAPSTANet80211Event(
    struct ieee80211com *ic,
    struct ieee80211_node *ni,
    int event,
    void *arg)
{
    (void)ic;
    AirportItlwmAPSTAOwner *owner = static_cast<AirportItlwmAPSTAOwner *>(arg);
    if (owner == nullptr || ni == nullptr) {
        return;
    }
    (void)owner->publishStationEventFromNet80211(static_cast<uint32_t>(event), ni->ni_macaddr, 0);
}
