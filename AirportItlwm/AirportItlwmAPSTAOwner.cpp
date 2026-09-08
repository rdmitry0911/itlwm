/*
 * Host-owned APSTA owner: role-7 lifetime, APSTA state block,
 * station table, and AP-up gate. See AirportItlwmAPSTAOwner.hpp
 * for the contract boundary.
 */
#include "AirportItlwmV2.hpp"
#include "AirportItlwmRegDiag.hpp"
#include "AirportItlwmAPSTAOwner.hpp"
#include "AirportItlwmAPSTAEventContracts.hpp"
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

static bool apsta_lower_start_retryable(IOReturn result)
{
    return result == kIOReturnBusy ||
        result == kIOReturnNotReady ||
        result == kIOReturnTimeout ||
        result == kIOReturnAborted;
}

static bool apsta_lower_stop_pending(IOReturn result)
{
    return result == kIOReturnBusy ||
        result == kIOReturnNotReady ||
        result == kIOReturnTimeout ||
        result == kIOReturnAborted;
}

enum {
    kAirportItlwmAPSTAAuthUpperOpen = 0,
    kAirportItlwmAPSTAAuthUpperWPA2PSK = 0x8,
    kAirportItlwmAPSTAAuthUpperWPA3SAE = 0x1000,
    kAirportItlwmAPSTAWPA2CredentialLengthMin = 8,
    kAirportItlwmAPSTAWPA2CredentialLengthMax = 63,
    /*
     * The controller watchdog retries once per second.  A retained AP must
     * normally follow the primary BSS RXON replay, but an AP-only machine
     * must not remain unavailable forever if the pre-sleep STA cannot
     * reassociate.
     */
    kAirportItlwmAPSTARadioResetPrimaryStaWaitTicks = 30
};

static_assert(kAirportItlwmAPSTAAuthUpperWPA3SAE ==
                  APPLE80211_AUTHTYPE_WPA3_SAE,
              "HostAP WPA3 carrier must match the Apple80211 SAE bit");

static size_t apsta_build_wpa2_psk_rsn_ie(
    uint8_t *output,
    size_t outputCapacity)
{
    /*
     * RSN v1, group CCMP, one pairwise cipher CCMP, one AKM PSK,
     * capabilities 0. This is the WPA2-Personal carrier produced by
     * airportd's private security type 0x80 / HostAP auth_upper 0x8.
     */
    static const uint8_t rsn[] = {
        IEEE80211_ELEMID_RSN, 20,
        0x01, 0x00,
        0x00, 0x0f, 0xac, 0x04,
        0x01, 0x00, 0x00, 0x0f, 0xac, 0x04,
        0x01, 0x00, 0x00, 0x0f, 0xac, 0x02,
        0x00, 0x00
    };
    if (output == nullptr || outputCapacity < sizeof(rsn))
        return 0;
    memcpy(output, rsn, sizeof(rsn));
    return sizeof(rsn);
}

static size_t apsta_build_wpa3_sae_rsn_ie(
    uint8_t *output,
    size_t outputCapacity)
{
    /*
     * RSN v1, group/pairwise CCMP, SAE AKM, management-frame protection
     * capable and required, no PMKID, and BIP-CMAC-128 as the group
     * management cipher.  CoreWLAN maps private HostAP security 0x1000 to
     * Apple80211 auth_upper WPA3_SAE (0x1000).
     */
    static const uint8_t rsn[] = {
        IEEE80211_ELEMID_RSN, 26,
        0x01, 0x00,
        0x00, 0x0f, 0xac, 0x04,
        0x01, 0x00, 0x00, 0x0f, 0xac, 0x04,
        0x01, 0x00, 0x00, 0x0f, 0xac, 0x08,
        0xc0, 0x00,
        0x00, 0x00,
        0x00, 0x0f, 0xac, 0x06
    };
    if (output == nullptr || outputCapacity < sizeof(rsn))
        return 0;
    memcpy(output, rsn, sizeof(rsn));
    return sizeof(rsn);
}

static size_t apsta_build_beacon(
    uint8_t *output,
    size_t outputCapacity,
    const uint8_t *bssid,
    const uint8_t *ssid,
    size_t ssidLength,
    uint16_t channel,
    uint16_t beaconInterval,
    uint8_t dtimPeriod,
    const uint8_t *rsnIE,
    size_t rsnIELength,
    const struct ItlHalApConfig *config)
{
    const size_t fixedLength = sizeof(struct ieee80211_frame) + 12;
    const bool is2GHz = channel <= 14;
    const uint8_t rates2GHz[] = {
        0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24
    };
    const uint8_t rates5GHz[] = {
        0x8c, 0x12, 0x98, 0x24, 0xb0, 0x48, 0x60, 0x6c
    };
    const uint8_t extendedRates2GHz[] = { 0x30, 0x48, 0x60, 0x6c };
    const bool ht20 = itl_hal_ap_ht_enabled(config);
    const size_t required =
        fixedLength +
        2 + ssidLength +
        2 + sizeof(rates2GHz) +
        3 +
        6 +
        rsnIELength +
        sizeof(kItlHalApWmmParameterIE) +
        (ht20 ? kItlHalApHtCapabilityIELength +
                kItlHalApHtOperationIELength : 0) +
        (is2GHz ? 2 + sizeof(extendedRates2GHz) : 0);
    if (output == nullptr || bssid == nullptr || ssid == nullptr ||
        ssidLength == 0 ||
        ssidLength > kAirportItlwmAPSTAGetSsidMaxLength ||
        channel == 0 || channel > UINT8_MAX ||
        config == nullptr ||
        (rsnIELength != 0 && rsnIE == nullptr) ||
        outputCapacity < required) {
        return 0;
    }

    bzero(output, required);
    struct ieee80211_frame *frame =
        reinterpret_cast<struct ieee80211_frame *>(output);
    frame->i_fc[0] = IEEE80211_FC0_VERSION_0 |
        IEEE80211_FC0_TYPE_MGT | IEEE80211_FC0_SUBTYPE_BEACON;
    frame->i_fc[1] = IEEE80211_FC1_DIR_NODS;
    IEEE80211_ADDR_COPY(frame->i_addr1, etherbroadcastaddr);
    IEEE80211_ADDR_COPY(frame->i_addr2, bssid);
    IEEE80211_ADDR_COPY(frame->i_addr3, bssid);

    uint8_t *cursor = output + sizeof(*frame) + 8;
    const uint16_t interval = htole16(
        beaconInterval != 0 ? beaconInterval : 100);
    const uint16_t capability = htole16(
        IEEE80211_CAPINFO_ESS |
        (rsnIELength != 0 ? IEEE80211_CAPINFO_PRIVACY : 0) |
        (is2GHz ? IEEE80211_CAPINFO_SHORT_SLOTTIME : 0));
    memcpy(cursor, &interval, sizeof(interval));
    cursor += sizeof(interval);
    memcpy(cursor, &capability, sizeof(capability));
    cursor += sizeof(capability);

    *cursor++ = IEEE80211_ELEMID_SSID;
    *cursor++ = static_cast<uint8_t>(ssidLength);
    memcpy(cursor, ssid, ssidLength);
    cursor += ssidLength;

    *cursor++ = IEEE80211_ELEMID_RATES;
    *cursor++ = static_cast<uint8_t>(sizeof(rates2GHz));
    if (is2GHz)
        memcpy(cursor, rates2GHz, sizeof(rates2GHz));
    else
        memcpy(cursor, rates5GHz, sizeof(rates5GHz));
    cursor += sizeof(rates2GHz);

    *cursor++ = IEEE80211_ELEMID_DSPARMS;
    *cursor++ = 1;
    *cursor++ = static_cast<uint8_t>(channel);

    *cursor++ = IEEE80211_ELEMID_TIM;
    *cursor++ = 4;
    *cursor++ = 0;
    *cursor++ = dtimPeriod != 0 ? dtimPeriod : 1;
    *cursor++ = 0;
    *cursor++ = 0;

    if (is2GHz) {
        *cursor++ = IEEE80211_ELEMID_XRATES;
        *cursor++ = static_cast<uint8_t>(sizeof(extendedRates2GHz));
        memcpy(cursor, extendedRates2GHz, sizeof(extendedRates2GHz));
        cursor += sizeof(extendedRates2GHz);
    }
    if (rsnIELength != 0) {
        memcpy(cursor, rsnIE, rsnIELength);
        cursor += rsnIELength;
    }
    if (ht20) {
        cursor += itl_hal_ap_build_ht_capability_ie(
            cursor, static_cast<size_t>(output + required - cursor), config);
        cursor += itl_hal_ap_build_ht_operation_ie(
            cursor, static_cast<size_t>(output + required - cursor), config);
    }
    memcpy(cursor, kItlHalApWmmParameterIE,
           sizeof(kItlHalApWmmParameterIE));
    cursor += sizeof(kItlHalApWmmParameterIE);
    return static_cast<size_t>(cursor - output);
}

static void apsta_copy_mac_prefix(
    uint32_t *dwordOut,
    uint16_t *tailOut,
    const uint8_t *mac)
{
    memcpy(dwordOut, mac, sizeof(*dwordOut));
    memcpy(tailOut, mac + sizeof(*dwordOut), sizeof(*tailOut));
}

static bool apsta_ie_has_apple_oui(const uint8_t *ie)
{
    const uint8_t *oui = ie + kAirportItlwmAPSTAAppleIEOuiOffset;
    return AirportItlwmAPSTAEventContracts::isRecognizedAppleOUI(oui);
}

static bool apsta_check_for_apple_ie(const uint8_t *ies, uint32_t iesLength)
{
    bool foundAppleIE = false;

    while (ies != nullptr && iesLength >= kAirportItlwmAPSTAAppleIEMinScanRemaining) {
        const uint32_t elementLength =
            static_cast<uint32_t>(ies[kAirportItlwmAPSTAAppleIELengthOffset]);
        const uint32_t totalLength =
            elementLength + kAirportItlwmAPSTAVendorIEListHeaderSize;
        if (totalLength > iesLength) {
            break;
        }

        if (ies[kAirportItlwmAPSTAVendorIEListElementIdOffset] ==
                kAirportItlwmAPSTAAppleIEVendorElementId &&
            apsta_ie_has_apple_oui(ies)) {
            foundAppleIE = true;
            break;
        }

        ies += totalLength;
        iesLength -= totalLength;
    }

    return foundAppleIE;
}

static uint32_t apsta_extract_instant_hotspot_flags(const uint8_t *ies,
                                                    uint32_t iesLength)
{
    uint32_t flags = 0;
    while (ies != nullptr && iesLength >= kAirportItlwmAPSTAAppleIEMinScanRemaining) {
        const uint32_t elementLength =
            static_cast<uint32_t>(ies[kAirportItlwmAPSTAAppleIELengthOffset]);
        const uint32_t totalLength =
            elementLength + kAirportItlwmAPSTAVendorIEListHeaderSize;
        if (totalLength > iesLength) {
            break;
        }

        if (ies[kAirportItlwmAPSTAVendorIEListElementIdOffset] ==
                kAirportItlwmAPSTAAppleIEVendorElementId &&
            AirportItlwmAPSTAEventContracts::isInstantHotspotOUI(
                ies + kAirportItlwmAPSTAAppleIEOuiOffset) &&
            totalLength > kAirportItlwmAPSTAAppleIEInstantHotspotFlagsOffset &&
            ies[kAirportItlwmAPSTAAppleIEInstantHotspotSubtypeOffset] ==
                kAirportItlwmAPSTAAppleIEInstantHotspotSubtype) {
            const uint8_t appleFlags =
                ies[kAirportItlwmAPSTAAppleIEInstantHotspotFlagsOffset];
            if ((appleFlags &
                 (1U << kAirportItlwmAPSTAAppleIEAihsFlagBit)) != 0) {
                flags |= kAirportItlwmAPSTAEventAssocFlagAihs;
            }
            if ((appleFlags &
                 (1U << kAirportItlwmAPSTAAppleIESharingFlagBit)) != 0) {
                flags |= kAirportItlwmAPSTAEventAssocFlagSharing;
            }
            break;
        }

        ies += totalLength;
        iesLength -= totalLength;
    }
    return flags;
}

static void apsta_copy_rsnxe(const uint8_t *ies,
                             uint32_t iesLength,
                             uint8_t *output,
                             size_t outputCapacity)
{
    while (ies != nullptr && iesLength >= kAirportItlwmAPSTARSNXEMinScanRemaining) {
        const uint32_t elementLength = static_cast<uint32_t>(ies[1]);
        const uint32_t totalLength =
            elementLength + kAirportItlwmAPSTAVendorIEListHeaderSize;
        if (totalLength > iesLength) {
            break;
        }
        if (ies[0] == kAirportItlwmAPSTARSNXEElementId &&
            totalLength <= outputCapacity) {
            memcpy(output, ies, totalLength);
            return;
        }
        ies += totalLength;
        iesLength -= totalLength;
    }
}

static uint16_t apsta_load_le16(const uint8_t *input)
{
    uint16_t value = 0;
    memcpy(&value, input, sizeof(value));
    return value;
}

static uint32_t apsta_auth_ind_status_from_reason(uint32_t reason)
{
    if (reason == 0) {
        return kAirportItlwmAPSTAEventAuthIndSuccessStatus;
    }
    if (reason < kAirportItlwmAPSTAEventAuthIndReasonTrapThreshold) {
        return reason | kAirportItlwmAPSTAEventAuthIndReasonAppleBase;
    }
    return kAirportItlwmAPSTAEventAuthIndReasonFallback;
}

static uint32_t apsta_auth_ind_aligned_chunk_length(uint16_t length)
{
    if (length == 0) {
        return kAirportItlwmAPSTAEventAuthIndChunkAlignment;
    }
    return ((static_cast<uint32_t>(length) - 1) &
            ~(kAirportItlwmAPSTAEventAuthIndChunkAlignment - 1)) +
           kAirportItlwmAPSTAEventAuthIndChunkAlignment;
}

static void apsta_copy_auth_ind_chunks(
    const uint8_t *data,
    uint32_t dataLength,
    AirportItlwmAPSTAAuthIndMessageLayout *message)
{
    if (data == nullptr ||
        dataLength < kAirportItlwmAPSTAEventAuthIndDataMinimumLength) {
        return;
    }

    const uint16_t headerType =
        apsta_load_le16(data + kAirportItlwmAPSTAEventAuthIndDataHeaderTypeOffset);
    const uint16_t headerLength =
        apsta_load_le16(data + kAirportItlwmAPSTAEventAuthIndDataHeaderLengthOffset);
    if (headerType != kAirportItlwmAPSTAEventAuthIndDataHeaderTypeValue ||
        headerLength > dataLength ||
        headerLength < kAirportItlwmAPSTAEventAuthIndDataMinimumLength) {
        return;
    }

    uint32_t remaining = headerLength -
        kAirportItlwmAPSTAEventAuthIndDataMinimumLength;
    if (remaining < kAirportItlwmAPSTAEventAuthIndChunkHeaderSize) {
        return;
    }
    const uint8_t *chunk = data + kAirportItlwmAPSTAEventAuthIndDataChunkListOffset;
    uint8_t *rawMessage = reinterpret_cast<uint8_t *>(message);

    while (remaining >= kAirportItlwmAPSTAEventAuthIndChunkHeaderSize) {
        const uint16_t chunkType =
            apsta_load_le16(chunk + kAirportItlwmAPSTAEventAuthIndChunkTypeOffset);
        const uint16_t chunkLength =
            apsta_load_le16(chunk + kAirportItlwmAPSTAEventAuthIndChunkLengthOffset);
        const uint32_t alignedLength =
            apsta_auth_ind_aligned_chunk_length(chunkLength);
        if (alignedLength > kAirportItlwmAPSTAEventAuthIndChunkAlignedLengthMax ||
            alignedLength > remaining -
                kAirportItlwmAPSTAEventAuthIndChunkHeaderSize) {
            return;
        }

        const uint8_t *chunkData =
            chunk + kAirportItlwmAPSTAEventAuthIndChunkDataOffset;
        if (chunkType == kAirportItlwmAPSTAEventAuthIndChunkType1) {
            if (chunkLength >= kAirportItlwmAPSTAEventAuthIndChunkType1MinSize &&
                chunkLength <= kAirportItlwmAPSTAEventAuthIndChunkType1MaxSize) {
                memcpy(rawMessage +
                           kAirportItlwmAPSTAEventAuthIndChunkType1OutputOffset,
                       chunkData, chunkLength);
            }
        } else if (chunkType == kAirportItlwmAPSTAEventAuthIndChunkType2) {
            if (chunkLength == kAirportItlwmAPSTAEventAuthIndChunkType2Size) {
                memcpy(rawMessage +
                           kAirportItlwmAPSTAEventAuthIndChunkType2OutputOffset,
                       chunkData, chunkLength);
            }
        }

        const uint32_t advance =
            alignedLength + kAirportItlwmAPSTAEventAuthIndChunkHeaderSize;
        if (advance > remaining) {
            return;
        }
        remaining -= advance;
        chunk += advance;
    }
}

static uint32_t apsta_rsn_clamp_count(uint32_t count)
{
    return count < kAirportItlwmAPSTARSNConfListMaxCount
        ? count
        : kAirportItlwmAPSTARSNConfListMaxCount;
}

static uint32_t apsta_rsn_version_list_count(uint32_t value)
{
    if (value == 0) {
        return 0;
    }
    uint32_t count = value - 1;
    if (count > kAirportItlwmAPSTARSNConfVersionClampLimit) {
        count = kAirportItlwmAPSTARSNConfVersionClampLimit;
    }
    return count + 1;
}

static uint32_t apsta_rsn_pairwise_cipher_mask(
    const struct apple80211_rsn_conf_data *in)
{
    uint32_t mask = 0;
    const uint32_t count = apsta_rsn_clamp_count(in->pairwiseCipherCount2c);
    for (uint32_t i = 0; i < count; i++) {
        switch (in->pairwiseCipherList30[i]) {
            case kAirportItlwmAPSTARSNConfPairwiseCipherValue1:
                mask |= kAirportItlwmAPSTARSNConfPairwiseCipherValue1Mask;
                break;
            case kAirportItlwmAPSTARSNConfPairwiseCipherValue2:
                mask |= kAirportItlwmAPSTARSNConfPairwiseCipherValue2Mask;
                break;
            default:
                break;
        }
    }
    return mask;
}

static uint32_t apsta_rsn_group_cipher_mask(
    const struct apple80211_rsn_conf_data *in)
{
    uint32_t mask = 0;
    const uint32_t count = apsta_rsn_clamp_count(in->groupCipherCount7c);
    for (uint32_t i = 0; i < count; i++) {
        switch (in->groupCipherList80[i]) {
            case kAirportItlwmAPSTARSNConfGroupCipherValue4:
                mask |= kAirportItlwmAPSTARSNConfGroupCipherValue4Mask;
                break;
            case kAirportItlwmAPSTARSNConfGroupCipherValue8:
                mask |= kAirportItlwmAPSTARSNConfGroupCipherValue8Mask;
                break;
            case kAirportItlwmAPSTARSNConfGroupCipherValue1000:
                mask |= kAirportItlwmAPSTARSNConfGroupCipherValue1000Mask;
                break;
            default:
                break;
        }
    }
    return mask;
}

static uint32_t apsta_rsn_auth_mask(const uint32_t *values, uint32_t count)
{
    uint32_t mask = 0;
    for (uint32_t i = 0; i < count; i++) {
        const uint32_t value = values[i];
        if (value <= kAirportItlwmAPSTARSNConfMapMaxIndex) {
            mask |= kAirportItlwmAPSTARSNConfAppleCipherMap[value];
        }
    }
    return mask;
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
    apAuthUpper = kAirportItlwmAPSTAAuthUpperOpen;
    bzero(apCredential, sizeof(apCredential));
    apCredentialLength = 0;
    lowerStopPending = false;
    radioResetResumePending = false;
    radioResetWaitForPrimaryStaRun = false;
    radioResetPrimaryStaScanHandoff = false;
    initialHostAPAdmissionPending = false;
    confirmedHostAPStartPending = false;
    interfaceDrivenHostAPConfirmationPending = false;
    primaryStaHandoffScanArmed = false;
    radioResetResumeWaitTicks = 0;
    lowerAssociatedStaCount = 0;
    bzero(lowerAssociatedStaMacs, sizeof(lowerAssociatedStaMacs));
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
    if (apsta_mac_is_zero(mac) &&
        controller->copyPermanentHardwareAddress(mac)) {
        /*
         * Reference passes the role MAC from VIRTUAL_IF_CREATE unchanged into
         * APSTA init and later copies it into RegistrationInfo+0x108.  The
         * public carrier normally supplies that distinct address.  Preserve
         * it above; only the zero-carrier compatibility path derives one.
         *
         * The primary Skywalk ifnet keeps the permanent address as its
         * immutable six-byte unique-id even after macOS installs a private
         * link address.  Merely setting the local bit is insufficient when
         * the permanent address is already local (as it is in the VM).  Flip
         * an additional unicast-safe bit unconditionally so late APSTA
         * publication cannot reuse the primary registration identity.
         */
        mac[0] = static_cast<uint8_t>((mac[0] | 0x02U) ^ 0x04U);
    }
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
    bzero(apCredential, sizeof(apCredential));
    apCredentialLength = 0;
    apAuthUpper = kAirportItlwmAPSTAAuthUpperOpen;
    lowerStopPending = false;
    radioResetResumePending = false;
    radioResetWaitForPrimaryStaRun = false;
    radioResetPrimaryStaScanHandoff = false;
    initialHostAPAdmissionPending = false;
    confirmedHostAPStartPending = false;
    interfaceDrivenHostAPConfirmationPending = false;
    primaryStaHandoffScanArmed = false;
    radioResetResumeWaitTicks = 0;
    clearLowerAssociatedStations();
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
    primaryStaHandoffScanArmed = false;
    state.resetState26c = 0;
    state.resetFlag329 = 0;
    state.hostApTransitionState270 = 0;
    /* A public HostAP stop ends the closednet profile.  Radio-reset replay
     * with an associated station deliberately bypasses resetRuntimeState(),
     * so sleep still retains selector 336 while a later explicit AP start
     * cannot inherit the previous network's hidden state. */
    state.hiddenNetworkFlag0d = 0;
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
    clearLowerAssociatedStations();
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

    struct ieee80211com *ic = owner->fHalService->get80211Controller();
    /*
     * AppleBCMWLAN can hand the requested chanspec to its FullMAC AP
     * context.  Intel DVM instead publishes STA+AP with exactly one
     * different channel.  The public Tahoe sequence can issue POWER and run
     * a WCL join between SET_CHANNEL and HOST_AP_MODE, so the candidate that
     * was current when userspace selected the AP channel is not necessarily
     * the BSS committed at this final lower admission boundary.  Make that
     * live primary BSS authoritative before building the beacon/RXON pair;
     * otherwise both BSD interfaces report active while AP management TX is
     * attempted through the wrong radio context.
     */
    if (owner->fHalService->requiresAPSTASharedChannel()) {
        const uint16_t primaryChannel =
            owner->fHalService->getAPSTARequiredSharedChannel();
        if (primaryChannel != 0 && primaryChannel != apChannel) {
            XYLog("APSTA public start shared channel follows primary "
                  "profile=%u primary=%u\n",
                  static_cast<unsigned>(apChannel),
                  static_cast<unsigned>(primaryChannel));
            apChannel = primaryChannel;
        }
    }

    ItlHalApConfig cfg;
    bzero(&cfg, sizeof(cfg));
    memcpy(cfg.bssid, mac, IEEE80211_ADDR_LEN);
    cfg.channel = apChannel;
    cfg.maxStations = state.softapMaxAssoc04;
    cfg.beaconInterval = state.softapBeaconInterval14;
    cfg.dtimPeriod = static_cast<uint8_t>(state.softapDtimPeriod16);
    cfg.authUpper = apAuthUpper;
    cfg.ssid = state.softapSsid278;
    cfg.ssidLength = state.softapSsidLength274;
    cfg.credential = apCredentialLength != 0 ? apCredential : nullptr;
    cfg.credentialLength = apCredentialLength;
    uint8_t rsnIE[32];
    size_t rsnIELength = 0;
    if (apAuthUpper == kAirportItlwmAPSTAAuthUpperWPA2PSK) {
        rsnIELength = apsta_build_wpa2_psk_rsn_ie(
            rsnIE, sizeof(rsnIE));
    } else if (apAuthUpper == kAirportItlwmAPSTAAuthUpperWPA3SAE) {
        rsnIELength = apsta_build_wpa3_sae_rsn_ie(
            rsnIE, sizeof(rsnIE));
    } else if (apAuthUpper != kAirportItlwmAPSTAAuthUpperOpen) {
        lifecycle = kAirportItlwmAPSTAOwnerLowerBlocked;
        state.resetState26c = 0;
        return kIOReturnUnsupported;
    }
    cfg.rsnIE = rsnIELength != 0 ? rsnIE : nullptr;
    cfg.rsnIELength = rsnIELength;
    if (ic != nullptr && ic->ic_sup_mcs[0] != 0) {
        /* Begin with HT20 only. Width-dependent rates and coexistence are a
         * separate layer; SGI20 and the physical stream count remain the
         * capabilities of the actual IWM/IWX device. */
        cfg.htCapabilities = static_cast<uint16_t>(ic->ic_htcaps &
            (IEEE80211_HTCAP_SMPS_MASK | IEEE80211_HTCAP_SGI20));
        cfg.htAmpduParams = ic->ic_ampdu_params;
        memcpy(cfg.htMcsSet, ic->ic_sup_mcs,
               MIN(sizeof(ic->ic_sup_mcs), sizeof(cfg.htMcsSet)));
        const uint16_t maxRxRate =
            ic->ic_max_rxrate & IEEE80211_MCS_RX_RATE_HIGH;
        cfg.htMcsSet[10] = static_cast<uint8_t>(maxRxRate);
        cfg.htMcsSet[11] = static_cast<uint8_t>(maxRxRate >> 8);
        cfg.htMcsSet[12] = ic->ic_tx_mcs_set;
    }
    uint8_t beaconTemplate[256];
    cfg.beaconTemplateLength = apsta_build_beacon(
        beaconTemplate, sizeof(beaconTemplate), mac,
        cfg.ssid, cfg.ssidLength, cfg.channel,
        cfg.beaconInterval, cfg.dtimPeriod,
        cfg.rsnIE, cfg.rsnIELength, &cfg);
    if (cfg.beaconTemplateLength == 0) {
        lifecycle = kAirportItlwmAPSTAOwnerLowerBlocked;
        state.resetState26c = 0;
        return kIOReturnBadArgument;
    }
    cfg.beaconTemplate = beaconTemplate;
    IOReturn ret = owner->fHalService->startAPMode(&cfg);
    if (ret == kIOReturnSuccess && state.hiddenNetworkFlag0d != 0) {
        /*
         * A radio reset reconstructs the lower AP from the retained public
         * profile.  closednet is a separate Apple selector, so replay it
         * before publishing the AP as running; otherwise wake exposes one or
         * more visible beacons until userspace happens to resend selector
         * 336.  IWN accepts this while its first beacon is still queued,
         * while IWM/IWX update their already materialized template.
         */
        XYLog("AirportItlwm: APSTA replaying retained hidden AP profile "
              "after radio reset\n");
        ret = owner->fHalService->setAPHidden(true);
        if (ret != kIOReturnSuccess)
            (void)owner->fHalService->stopAPMode();
    }
    if (ret == kIOReturnSuccess) {
        lifecycle = kAirportItlwmAPSTAOwnerRunning;
        state.resetState26c = 1;
        state.resetFlag329 |= kAirportItlwmAPSTACsaResetFlagBit;
        state.hostApTransitionState270 = 1;
        /*
         * AppleBCMWLAN's recovered setHostApModeInternal success tail
         * enables the AP interface; APSTAInterface::enable then calls
         * enableDatapath, which starts TXC/RX and arms the first RX
         * request. The local role-7 BSD interface is already up when the
         * HostAP command reaches this owner, so this is the equivalent
         * successful lower-mode edge.
         */
        owner->setAPSTADatapathEnabled(true);
    } else {
        lifecycle = kAirportItlwmAPSTAOwnerLowerBlocked;
        state.resetState26c = 0;
    }
    return ret;
}

IOReturn AirportItlwmAPSTAOwner::stopLower()
{
    radioResetResumePending = false;
    radioResetWaitForPrimaryStaRun = false;
    radioResetPrimaryStaScanHandoff = false;
    initialHostAPAdmissionPending = false;
    confirmedHostAPStartPending = false;
    interfaceDrivenHostAPConfirmationPending = false;
    radioResetResumeWaitTicks = 0;
    if (owner != nullptr)
        owner->setAPSTADatapathEnabled(false);

    /*
     * Apple setHostApModeInternal(NULL) returns the lower stop result and
     * does not turn a failed teardown into a completed public transition.
     * IWX must submit its firmware removals outside the upper command gate,
     * so its first answer is deliberately NotReady.  Publish the upper AP as
     * down immediately, but retain a private stop owner until the lower task
     * has reached its terminal.  Otherwise a rapid stop/start can overwrite
     * the only teardown intent while the old MAC context is still beaconing.
     */
    resetRuntimeState();
    lowerStopPending = true;
    state.hostApTransitionState270 = 1;
    lifecycle = kAirportItlwmAPSTAOwnerLowerBlocked;
    return driveLowerStopToTerminal();
}

IOReturn AirportItlwmAPSTAOwner::driveLowerStopToTerminal()
{
    if (!lowerStopPending)
        return kIOReturnSuccess;

    const IOReturn result =
        owner != nullptr && owner->fHalService != nullptr
            ? owner->fHalService->stopAPMode() : kIOReturnSuccess;
    if (result != kIOReturnSuccess) {
        XYLog("APSTA lower stop pending result=0x%x\n",
              static_cast<unsigned>(result));
        return result;
    }

    lowerStopPending = false;
    state.hostApTransitionState270 = 0;
    if (lifecycle != kAirportItlwmAPSTAOwnerFreed)
        lifecycle = kAirportItlwmAPSTAOwnerTerminal;
    XYLog("APSTA lower stop reached terminal\n");
    return kIOReturnSuccess;
}

void AirportItlwmAPSTAOwner::prepareEmptyAPForRadioReset()
{
    /*
     * This path is immediately followed by HAL disable and a destructive
     * Intel firmware reset.  Apple hostAPPowerOff still makes the no-client
     * upper HostAP profile terminal, but submitting an asynchronous IWX AP
     * removal here would race that reset from the power-command workloop.
     * The generic device stop is already the authoritative lower erasure
     * edge; retire only the upper owner here.  Explicit HostAP NULL remains
     * owned by stopLower() and its independently observed firmware terminal.
     */
    setSoftAPPowerSaveState(
        kAirportItlwmAPSTAHostApPowerOffSetPowerSaveState,
        kAirportItlwmAPSTAHostApPowerOffPowerSaveReason);
    state.softapParam0e = 0;
    radioResetResumePending = false;
    radioResetWaitForPrimaryStaRun = false;
    radioResetPrimaryStaScanHandoff = false;
    initialHostAPAdmissionPending = false;
    confirmedHostAPStartPending = false;
    interfaceDrivenHostAPConfirmationPending = false;
    radioResetResumeWaitTicks = 0;
    lowerStopPending = false;
    if (owner != nullptr)
        owner->setAPSTADatapathEnabled(false);
    resetRuntimeState();
    if (lifecycle != kAirportItlwmAPSTAOwnerFreed)
        lifecycle = kAirportItlwmAPSTAOwnerTerminal;
    XYLog("APSTA empty HostAP terminal delegated to imminent radio reset\n");
}

void AirportItlwmAPSTAOwner::prepareRetainedLowerReset(
    uint16_t lowerChannel)
{
    /*
     * The upper HostAP profile is the durable owner across a destructive
     * Intel firmware epoch.  The lower channel is authoritative after an
     * asynchronous CSA, but can already be zero when a firmware fatal or TX
     * watchdog reaches this census.  In that case apChannel is the most
     * recent healthy watchdog snapshot.
     */
    if (lowerChannel != 0 && lowerChannel != apChannel) {
        XYLog("APSTA radio-reset channel snapshot profile=%u committed=%u\n",
              static_cast<unsigned>(apChannel),
              static_cast<unsigned>(lowerChannel));
        apChannel = lowerChannel;
    }

    setSoftAPPowerSaveState(
        kAirportItlwmAPSTAHostApPowerOffConcurrencyFallbackState,
        kAirportItlwmAPSTAHostApPowerOffConcurrencyFallbackReason);
    struct ieee80211com *ic =
        owner != nullptr && owner->fHalService != nullptr
            ? owner->fHalService->get80211Controller() : nullptr;
    const bool primaryRecoveryScanPending =
        owner != nullptr && owner->fHalService != nullptr &&
        owner->fHalService->isPrimaryStaRecoveryScanPending();
    /*
     * The PM callback can arrive after the primary state left RUN.  The
     * steady watchdog sample is therefore intentionally sticky until this
     * retained profile has crossed the replacement firmware epoch.  The
     * reference FullMAC retains both BSS contexts across hostAPPowerOff;
     * after a destructive Intel epoch the equivalent order is to let an
     * exact retained-ESS recovery scan restore the primary BSS before PAN
     * replay.  Starting PAN first aborts that physical scan.
     */
    radioResetWaitForPrimaryStaRun =
        radioResetWaitForPrimaryStaRun ||
        (ic != nullptr && ic->ic_state == IEEE80211_S_RUN) ||
        primaryRecoveryScanPending;
    if (primaryRecoveryScanPending)
        XYLog("APSTA radio-reset primary recovery scan owns radio\n");
    radioResetResumeWaitTicks = 0;
    radioResetPrimaryStaScanHandoff = false;
    initialHostAPAdmissionPending = false;
    confirmedHostAPStartPending = false;
    interfaceDrivenHostAPConfirmationPending = false;
    if (owner != nullptr)
        owner->setAPSTADatapathEnabled(false);
    for (unsigned i = 0; i < kAirportItlwmAPSTAStationTableEntryCount; i++)
        clearStation(&state.softapStaTableB8[i]);
    state.softapAssociatedStaCount00 = 0;
    clearLowerAssociatedStations();
    state.resetState26c = 0;
    state.hostApTransitionState270 = 0;
    lifecycle = kAirportItlwmAPSTAOwnerLowerBlocked;
    radioResetResumePending = true;
}

void AirportItlwmAPSTAOwner::prepareForRadioReset()
{
    if (!isApRunning())
        return;

    /*
     * AppleBCMWLANCore::powerOff() calls APSTA::hostAPPowerOff().
     * With no associated station the reference tears HostAP down. With a
     * live station it leaves the AP owner in power-save state 3, and
     * powerOn() later restores state 1. DVM cannot retain its PAN context
     * across iwn_hw_stop(), so preserve the same upper intent while closing
     * the Skywalk datapath before the lower rings and firmware disappear.
     * The post-reset census terminal replays the retained profile into a new
     * PAN context before reopening this datapath.
     */
    if (state.softapAssociatedStaCount00 == 0 &&
        lowerAssociatedStaCount == 0) {
        prepareEmptyAPForRadioReset();
        return;
    }

    /*
     * Broadcom's reference FullMAC context survives hostAPPowerOff(), so a
     * completed CSA remains the active channel without rebuilding the AP
     * from its original profile.  Intel DVM/MVM firmware loses that context
     * during the destructive radio reset.  The lower HAL is authoritative
     * for asynchronous CSA completion (and rollback), therefore snapshot
     * its committed channel immediately before teardown and replay that
     * channel rather than the stale initial SET_CHANNEL value.
     */
    const uint16_t lowerChannel =
        owner != nullptr && owner->fHalService != nullptr
            ? owner->fHalService->getAPCurrentChannel() : 0;
    /*
     * Apple retains both FullMAC contexts through hostAPPowerOff().  DVM
     * loses both in iwn_hw_stop(), and its later primary IWN_CMD_RXON
     * invalidates PAN station/data state if PAN is replayed first.  Remember
     * whether the primary STA was live before sleep so wake can restore the
     * BSS RXON first and only then reconstruct the retained PAN owner.
     */
    /*
     * The PM stack may already have moved net80211 out of RUN by the time
     * disableAdapterCore() reaches this callback.  resumeAfterRadioReset()
     * also samples the primary state from the one-second runtime watchdog,
     * so retain that last pre-transition RUN observation across this late
     * callback instead of replacing it with a transient INIT/SCAN state.
     */
    prepareRetainedLowerReset(lowerChannel);
}

IOReturn AirportItlwmAPSTAOwner::resumeAfterRadioReset()
{
    /* An explicit HostAP NULL outranks every retained-profile replay.  Drive
     * its asynchronous IWX teardown from the same once-per-second command-
     * gated census until the HAL reports the actual lower terminal.  Tahoe
     * can submit the replacement non-NULL carrier immediately after that
     * accepted stop.  In that case the profile is already confirmed, so
     * cross the stop terminal and continue into the ordinary lower-start
     * arbitration instead of dropping the replacement request. */
    if (lowerStopPending) {
        const IOReturn stopResult = driveLowerStopToTerminal();
        if (stopResult != kIOReturnSuccess)
            return stopResult;
        if (!confirmedHostAPStartPending)
            return kIOReturnSuccess;
        lifecycle = kAirportItlwmAPSTAOwnerLowerBlocked;
        state.hostApTransitionState270 = 1;
        radioResetResumePending = true;
        XYLog("APSTA confirmed HostAP replacement crossed lower stop "
              "terminal\n");
    }

    if (!radioResetResumePending) {
        /*
         * Keep a near-boundary snapshot while the AP is running.  macOS can
         * lower the primary net80211 state before prepareForRadioReset(), but
         * the watchdog observes the ordinary steady RUN state every second.
         * The late sleep callback ORs its own sample with this one.
         */
        struct ieee80211com *ic =
            owner != nullptr && owner->fHalService != nullptr
                ? owner->fHalService->get80211Controller() : nullptr;
        const bool upperRunning = isApRunning();
        const uint16_t lowerChannel =
            upperRunning && owner != nullptr &&
                    owner->fHalService != nullptr
                ? owner->fHalService->getAPCurrentChannel() : 0;

        /*
         * IWN/IWM/IWX reset destroys the firmware AP context independently
         * of the Apple role-7 owner.  A zero lower channel while the upper
         * owner is still Running is therefore an unexpected lower epoch,
         * not a public HostAP stop.  Retain the configured profile, close
         * the stale datapath and let the existing bounded replay path build
         * a fresh lower context.  This also covers firmware fatal recovery
         * when there was no associated station; hostAPPowerOff's deliberate
         * no-client shutdown remains confined to prepareForRadioReset().
         */
        if (upperRunning && lowerChannel == 0) {
            XYLog("APSTA unexpected lower reset detected; retaining "
                  "HostAP profile\n");
            prepareRetainedLowerReset(0);
        } else {
            /* Keep the durable profile aligned with a completed lower CSA so
             * an unexpected reset between PM callbacks replays the actual
             * on-air channel rather than the original SET_CHANNEL value. */
            if (upperRunning && lowerChannel != apChannel) {
                XYLog("APSTA live channel snapshot profile=%u committed=%u\n",
                      static_cast<unsigned>(apChannel),
                      static_cast<unsigned>(lowerChannel));
                apChannel = lowerChannel;
            }
            radioResetWaitForPrimaryStaRun =
                upperRunning && ic != nullptr &&
                ic->ic_state == IEEE80211_S_RUN;
            radioResetResumeWaitTicks = 0;
            return kIOReturnSuccess;
        }
    }

    /*
     * An unexpected lower-loss census can run before the replacement
     * firmware enters its foreground scan.  The first replay attempt then
     * returns NotReady, and a later watchdog would otherwise submit PAN over
     * the now-live scan forever.  Give STA a bounded interval, then ask the
     * lower owner to abort only an untagged foreground lease at its native
     * terminal.  Tagged WCL/background owners remain fail-closed below.
     */
    if (!radioResetWaitForPrimaryStaRun &&
        !radioResetPrimaryStaScanHandoff &&
        owner != nullptr && owner->fHalService != nullptr) {
        struct ieee80211com *ic =
            owner->fHalService->get80211Controller();
        if (ic != nullptr && ic->ic_state == IEEE80211_S_SCAN) {
            const bool retainedRecovery =
                owner->fHalService->isPrimaryStaRecoveryScanPending();
            radioResetWaitForPrimaryStaRun = true;
            radioResetResumeWaitTicks = 0;
            XYLog("APSTA radio-reset late primary foreground scan owns "
                  "radio retained_recovery=%u public_hostap=%u\n",
                  retainedRecovery ? 1U : 0U,
                  (initialHostAPAdmissionPending ||
                   confirmedHostAPStartPending) ? 1U : 0U);
        }
    }

    if (radioResetWaitForPrimaryStaRun) {
        struct ieee80211com *ic =
            owner != nullptr && owner->fHalService != nullptr
                ? owner->fHalService->get80211Controller() : nullptr;
        if (ic == nullptr)
            return kIOReturnNotReady;
        /*
         * A public HostAP request is an explicit radio-ownership decision.
         * Tahoe's recovered FullMAC path disables its foreground/background
         * scan policy before configuring HostAP, so do not spend the
         * retained-STA recovery budget on the initial admission.  The lower
         * IWM/IWX owner still performs a native scan abort and waits for its
         * terminal before submitting AP firmware commands.  A destructive
         * radio-reset replay keeps the full bounded STA-first interval.
         */
        const bool publicHostAPStartPending =
            initialHostAPAdmissionPending ||
            confirmedHostAPStartPending;
        const bool waitForRetainedPrimary =
            !publicHostAPStartPending;
        if (ic->ic_state != IEEE80211_S_RUN &&
            waitForRetainedPrimary &&
            radioResetResumeWaitTicks <
                kAirportItlwmAPSTARadioResetPrimaryStaWaitTicks) {
            radioResetResumeWaitTicks++;
            return kIOReturnNotReady;
        }
        const bool foregroundScanShouldYield =
            ic->ic_state == IEEE80211_S_SCAN &&
            (publicHostAPStartPending ||
             radioResetResumeWaitTicks >=
                 kAirportItlwmAPSTARadioResetPrimaryStaWaitTicks);
        if (foregroundScanShouldYield) {
            const IOReturn handoffResult =
                airportItlwmHandoffPrimaryStaRecoveryScanToAP(
                    owner->fHalService);
            XYLog("APSTA bounded primary foreground scan handoff "
                  "wait_ticks=%u public_hostap=%u result=0x%x\n",
                  static_cast<unsigned>(radioResetResumeWaitTicks),
                  publicHostAPStartPending ? 1U : 0U,
                  static_cast<unsigned>(handoffResult));
            if (handoffResult != kIOReturnSuccess &&
                handoffResult != kIOReturnUnsupported)
                return handoffResult;
            radioResetPrimaryStaScanHandoff =
                handoffResult == kIOReturnSuccess;
        }
        XYLog("APSTA radio-reset primary STA boundary state=%u wait_ticks=%u\n",
              static_cast<unsigned>(ic->ic_state),
              static_cast<unsigned>(radioResetResumeWaitTicks));
        radioResetWaitForPrimaryStaRun = false;
    }

    /*
     * The reference FullMAC retains both channel contexts across
     * hostAPPowerOff().  Intel DVM loses both contexts and advertises only
     * one different channel for STA+AP.  Once the primary STA has won the
     * wake/reset ordering above, rebuild the off-air retained PAN profile on
     * that actual BSS channel.  Replaying the stale pre-sleep AP channel
     * would create two apparently active interfaces with neither a usable
     * DVM schedule nor a working primary data path.
     *
     * startLowerIfReady() applies the same final-boundary rule to a public AP
     * start, covering a WCL join that races between SET_CHANNEL and
     * HOST_AP_MODE.  This earlier reset-specific update remains useful for
     * the retained-profile log and for building the replay intent before the
     * lower start call.
     */
    if (owner != nullptr && owner->fHalService != nullptr &&
        owner->fHalService->requiresAPSTASharedChannel()) {
        struct ieee80211com *ic =
            owner->fHalService->get80211Controller();
        if (ic != nullptr && ic->ic_state == IEEE80211_S_RUN &&
            ic->ic_bss != nullptr && ic->ic_bss->ni_chan != nullptr) {
            const int primaryChannel =
                ieee80211_chan2ieee(ic, ic->ic_bss->ni_chan);
            if (primaryChannel > 0 && primaryChannel <= UINT16_MAX &&
                static_cast<uint16_t>(primaryChannel) != apChannel) {
                XYLog("APSTA radio-reset shared channel follows primary "
                      "profile=%u primary=%u\n",
                      static_cast<unsigned>(apChannel),
                      static_cast<unsigned>(primaryChannel));
                apChannel = static_cast<uint16_t>(primaryChannel);
            }
        }
    }

    const IOReturn result = startLowerIfReady();
    if (result == kIOReturnSuccess) {
        radioResetResumePending = false;
        radioResetPrimaryStaScanHandoff = false;
        radioResetResumeWaitTicks = 0;
        const bool asynchronousPublicStart =
            initialHostAPAdmissionPending || confirmedHostAPStartPending;
        initialHostAPAdmissionPending = false;
        confirmedHostAPStartPending = false;
        if (asynchronousPublicStart)
            XYLog("APSTA asynchronous public HostAP start reached lower "
                  "running\n");
        setSoftAPPowerSaveState(
            kAirportItlwmAPSTAHostApPowerOnRestoreState,
            kAirportItlwmAPSTAHostApPowerOnRestoreReason);
    } else if (!apsta_lower_start_retryable(result)) {
        radioResetResumePending = false;
        radioResetWaitForPrimaryStaRun = false;
        radioResetPrimaryStaScanHandoff = false;
        initialHostAPAdmissionPending = false;
        confirmedHostAPStartPending = false;
        interfaceDrivenHostAPConfirmationPending = false;
        radioResetResumeWaitTicks = 0;
        state.hostApTransitionState270 = 0;
    } else if (radioResetPrimaryStaScanHandoff &&
               result != kIOReturnNotReady) {
        /* IWX reports NotReady while its lower worker is still in flight.
         * Any other retryable terminal has already resumed the primary scan;
         * allow a fresh bounded arbitration interval on the next census. */
        radioResetPrimaryStaScanHandoff = false;
        radioResetResumeWaitTicks = 0;
    }
    return result;
}

void AirportItlwmAPSTAOwner::teardown()
{
    if (lifecycle < kAirportItlwmAPSTAOwnerTerminal) {
        (void)stopLower();
    }
    owner = nullptr;
}

void AirportItlwmAPSTAOwner::noteInterfaceEnableDuringPendingHostAPStart()
{
    /*
     * Standard Tahoe Internet Sharing enables the role-7 BSD interface
     * after the first accepted HOST_AP_MODE request, then repeats the same
     * selector to complete its transaction.  Broadcom finishes the lower
     * start synchronously, so +0x26c cannot become observable between those
     * two consumer events.  IWX cannot wait synchronously without starving
     * the workloop which delivers its firmware completion.  Preserve the
     * same public ordering by remembering this exact interface event until
     * the repeated selector reaches setHostAPMode().
     *
     * A direct CoreWLAN HostAP start has no role-7 interface-enable event in
     * this interval.  It therefore keeps the immediate RUNNING -> SWAP edge
     * needed to route a direct stop back into the driver.
     */
    if (!initialHostAPAdmissionPending ||
        confirmedHostAPStartPending ||
        interfaceDrivenHostAPConfirmationPending)
        return;

    interfaceDrivenHostAPConfirmationPending = true;
    struct ieee80211com *ic = owner != nullptr && owner->fHalService != nullptr
        ? owner->fHalService->get80211Controller() : nullptr;
    /*
     * On the reference path setHostApModeInternal first asks BssManager
     * whether the primary is associated and leaves that BSS intact while it
     * configures the virtual interface.  Tahoe's public role-7 transaction
     * instead emits one generic RUN -> SCAN(-1) between this enable edge and
     * its repeated HOST_AP_MODE carrier.  Arm a one-shot only when there is
     * a fully live infrastructure BSS to preserve.  A later non-generic
     * state request, an unassociated primary, or an AP stop consumes nothing
     * and cannot turn this into a general scan veto.
     */
    (void)armPrimaryStaHandoffScan(ic);
    XYLog("APSTA interface-driven HostAP confirmation pending\n");
}

bool AirportItlwmAPSTAOwner::armPrimaryStaHandoffScan(
    struct ieee80211com *ic)
{
    primaryStaHandoffScanArmed = owner != nullptr &&
        owner->fHalService != nullptr &&
        owner->fHalService->get80211Controller() == ic &&
        !lowerStopPending && !isApRunning() &&
        ic != nullptr && ic->ic_state == IEEE80211_S_RUN &&
        ic->ic_opmode == IEEE80211_M_STA && ic->ic_bss != nullptr &&
        ic->ic_bss->ni_port_valid;
    return primaryStaHandoffScanArmed;
}

bool AirportItlwmAPSTAOwner::consumePrimaryStaHandoffScan(
    struct ieee80211com *ic, int arg)
{
    if (!primaryStaHandoffScanArmed)
        return false;

    /* This is intentionally a one-attempt reservation.  If Tahoe changes
     * the observed public transaction shape, fall back to ordinary net80211
     * behavior rather than retaining a stale scan veto. */
    primaryStaHandoffScanArmed = false;
    if (owner == nullptr || owner->fHalService == nullptr ||
        owner->fHalService->get80211Controller() != ic || arg != -1 ||
        lowerStopPending || isApRunning() || ic == nullptr ||
        ic->ic_state != IEEE80211_S_RUN ||
        ic->ic_opmode != IEEE80211_M_STA || ic->ic_bss == nullptr ||
        !ic->ic_bss->ni_port_valid)
        return false;

    XYLog("APSTA preserving associated primary BSS across public role-7 "
          "HostAP handoff scan\n");
    return true;
}

bool AirportItlwmAPSTAOwner::matchesBSDName(const uint8_t *name) const
{
    if (name == nullptr || name[0] == 0) {
        return false;
    }
    return strncmp(bsdNameStorage, reinterpret_cast<const char *>(name), sizeof(bsdNameStorage)) == 0;
}

void AirportItlwmAPSTAOwner::copyMacAddress(uint8_t *address) const
{
    if (address != nullptr)
        memcpy(address, mac, IEEE80211_ADDR_LEN);
}

IOReturn AirportItlwmAPSTAOwner::setMacAddress(const uint8_t *address)
{
    if (address == nullptr || apsta_mac_is_zero(address) ||
        IEEE80211_IS_MULTICAST(address))
        return kIOReturnBadArgument;

    /*
     * AppleBCMWLANIO80211APSTAInterface::setMacAddress keeps this update on
     * the APSTA firmware/BSS owner and rejects it after the AP-up state has
     * been entered.  Do not route a role-7 address through the shared
     * infrastructure ieee80211com.
     */
    if (state.resetState26c != 0)
        return kIOReturnError;

    memcpy(mac, address, IEEE80211_ADDR_LEN);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmAPSTAOwner::getSSID(AirportItlwmAPSTASsidDataLayout *out) const
{
    if (state.softapSsidLength274 > kAirportItlwmAPSTAGetSsidMaxLength) {
        return static_cast<IOReturn>(kAirportItlwmAPSTAGetSsidInvalidArgumentReturn);
    }
    out->length04 = state.softapSsidLength274;
    memcpy(out->ssid08, state.softapSsid278, state.softapSsidLength274);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmAPSTAOwner::getState(AirportItlwmAPSTAStateDataLayout *out) const
{
    out->state04 = kAirportItlwmAPSTAGetStateOutputValue;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmAPSTAOwner::getOpMode(AirportItlwmAPSTAOpModeDataLayout *out) const
{
    if (out == nullptr) {
        return static_cast<IOReturn>(kAirportItlwmAPSTAGetOpModeInvalidArgumentReturn);
    }
    out->type00 = kAirportItlwmAPSTAGetOpModeTypeValue;
    out->mode04 = state.resetState26c != 0
        ? kAirportItlwmAPSTAGetOpModeAPUpValue
        : kAirportItlwmAPSTAGetOpModeAPDownValue;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmAPSTAOwner::getPeerCacheMaximumSize(
    AirportItlwmAPSTAPeerCacheMaximumSizeLayout *out) const
{
    out->maximum04 = kAirportItlwmAPSTAGetPeerCacheMaximumSizeValue;
    return kIOReturnSuccess;
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
    if (owner == nullptr || owner->fHalService == nullptr || in->channel.channel == 0) {
        return static_cast<IOReturn>(kAirportItlwmAPSTASetChannelInvalidSoftAPInfoReturn);
    }
    struct ieee80211com *ic = owner->fHalService->get80211Controller();
    if (ic == nullptr) {
        return static_cast<IOReturn>(kAirportItlwmAPSTASetChannelInvalidSoftAPInfoReturn);
    }
    for (int i = 0; i <= IEEE80211_CHAN_MAX; i++) {
        if (ic->ic_channels[i].ic_freq == 0) {
            continue;
        }
        if (ieee80211_chan2ieee(ic, &ic->ic_channels[i]) == in->channel.channel) {
            apChannel = static_cast<uint16_t>(in->channel.channel);
            apChannelFlags = in->channel.flags;
            return kIOReturnSuccess;
        }
    }
    return static_cast<IOReturn>(kAirportItlwmAPSTASetChannelInvalidSoftAPInfoReturn);
}

IOReturn AirportItlwmAPSTAOwner::setHostAPMode(
    const AirportItlwmAPSTAHostApModeNetworkDataLayout *in)
{
    if (in != nullptr) {
        XYLog("AP HostAP carrier version=%u flags=0x%x auth_lower=0x%x "
              "auth_upper=0x%x channel=%u channel_flags=0x%x ssid_len=%u "
              "credential_len=%u vendor_ie_len=%u\n",
              in->version00, in->flags04, in->authLower08,
              in->authUpper0c, in->channelNumber14, in->channelFlags18,
              in->ssidLength1c,
              in->credentialLength44, in->vendorIELength2dc);
        if (in->vendorIELength2dc >
                kAirportItlwmAPSTAHostApModeVendorIELengthMaxAccepted ||
            in->ssidLength1c >
                kAirportItlwmAPSTAHostApModeSsidLengthMaxAccepted ||
            in->credentialLength44 > sizeof(apCredential)) {
            return static_cast<IOReturn>(
                kAirportItlwmAPSTASetHostApModeInvalidArgumentReturn);
        }
    }

    if (owner == nullptr || owner->fHalService == nullptr) {
        return static_cast<IOReturn>(kAirportItlwmAPSTASetHostApModeNotUpReturn);
    }

    if (in == nullptr || in->ssidLength1c == 0) {
        primaryStaHandoffScanArmed = false;
        /* A stop carrier is also terminal for an accepted initial HostAP
         * transition which has not crossed the replacement firmware epoch
         * yet.  Otherwise the watchdog could start a profile after
         * userspace had already withdrawn it.  Broadcom reaches its lower
         * terminal synchronously and returns success to selector 25.  IWX
         * must issue MAC/PHY removals on sc_nswq, so acknowledge ownership of
         * that accepted stop while lowerStopPending remains the authoritative
         * private terminal fence.  A later non-NULL carrier can then queue a
         * replacement without racing the old firmware context. */
        if (!isApRunning() && !radioResetResumePending &&
            !lowerStopPending)
            return kIOReturnSuccess;
        const IOReturn stopResult = stopLower();
        if (stopResult == kIOReturnSuccess)
            return kIOReturnSuccess;
        if (lowerStopPending && apsta_lower_stop_pending(stopResult)) {
            XYLog("APSTA accepted asynchronous HostAP stop pending lower "
                  "terminal result=0x%x\n",
                  static_cast<unsigned>(stopResult));
            return kIOReturnSuccess;
        }
        return stopResult;
    }

    if (in->authUpper0c != kAirportItlwmAPSTAAuthUpperOpen &&
        in->authUpper0c != kAirportItlwmAPSTAAuthUpperWPA2PSK &&
        in->authUpper0c != kAirportItlwmAPSTAAuthUpperWPA3SAE) {
        return kIOReturnUnsupported;
    }
    if ((in->authUpper0c == kAirportItlwmAPSTAAuthUpperWPA2PSK ||
         in->authUpper0c == kAirportItlwmAPSTAAuthUpperWPA3SAE) &&
        (in->credentialLength44 <
             kAirportItlwmAPSTAWPA2CredentialLengthMin ||
         in->credentialLength44 >
             kAirportItlwmAPSTAWPA2CredentialLengthMax)) {
        return static_cast<IOReturn>(
            kAirportItlwmAPSTASetHostApModeInvalidArgumentReturn);
    }
    if (in->authUpper0c == kAirportItlwmAPSTAAuthUpperOpen &&
        in->credentialLength44 != 0) {
        return static_cast<IOReturn>(
            kAirportItlwmAPSTASetHostApModeInvalidArgumentReturn);
    }

    /*
     * airportd's private HostAP command embeds its CWChannel directly in
     * apple80211_network_data. It does not issue a separate SET_CHANNEL.
     * Route that recovered +0x10/+0x14/+0x18 carrier through the same
     * channel admission used by the public Apple80211 lifecycle.
     */
    struct apple80211_channel_data channel;
    bzero(&channel, sizeof(channel));
    channel.version = in->channelVersion10;
    channel.channel.version = in->channelVersion10;
    channel.channel.channel = in->channelNumber14;
    channel.channel.flags = in->channelFlags18;
    const IOReturn channelResult = setChannel(&channel);
    if (channelResult != kIOReturnSuccess)
        return channelResult;

    state.softapSsidLength274 = in->ssidLength1c;
    bzero(state.softapSsid278, sizeof(state.softapSsid278));
    memcpy(state.softapSsid278, in->ssid20, in->ssidLength1c);
    apAuthUpper = in->authUpper0c;
    bzero(apCredential, sizeof(apCredential));
    apCredentialLength = in->credentialLength44;
    if (apCredentialLength != 0) {
        memcpy(apCredential, in->credential50, apCredentialLength);
    }

    if (isApRunning()) {
        /* CoreWLAN may repeat an identical start request.  A direct start
         * does not require it, but standard Internet Sharing marks its open
         * transaction with the role-7 interface-enable event above.  This
         * repeat is the exact event which closes that transaction. */
        if (interfaceDrivenHostAPConfirmationPending) {
            interfaceDrivenHostAPConfirmationPending = false;
            XYLog("APSTA interface-driven HostAP selector reached lower "
                  "running\n");
        }
        primaryStaHandoffScanArmed = false;
        return kIOReturnSuccess;
    }

    /* A repeated non-NULL carrier can arrive before the watchdog has
     * published an accepted initial lower start.  It is already the public
     * confirmation, so remember that fact instead of requiring a third
     * selector after the asynchronous worker reaches RUNNING. */
    if (initialHostAPAdmissionPending) {
        initialHostAPAdmissionPending = false;
        confirmedHostAPStartPending = true;
        interfaceDrivenHostAPConfirmationPending = false;
        primaryStaHandoffScanArmed = false;
        XYLog("APSTA repeated HostAP selector confirmed pending lower "
              "start\n");
    }

    /* Standard Tahoe Internet Sharing sends NULL followed immediately by
     * the replacement carrier.  Keep the new validated profile behind the
     * old lower terminal, acknowledge it to airportd, and let the command-
     * gated census perform the actual stop->start serialization. */
    if (lowerStopPending) {
        confirmedHostAPStartPending = true;
        lifecycle = kAirportItlwmAPSTAOwnerLowerBlocked;
        state.hostApTransitionState270 = 1;
        radioResetResumePending = true;
        radioResetWaitForPrimaryStaRun = false;
        radioResetPrimaryStaScanHandoff = false;
        radioResetResumeWaitTicks = 0;
        const IOReturn stopResult = driveLowerStopToTerminal();
        if (stopResult != kIOReturnSuccess) {
            if (apsta_lower_stop_pending(stopResult)) {
                XYLog("APSTA queued confirmed HostAP replacement behind "
                      "lower stop result=0x%x\n",
                      static_cast<unsigned>(stopResult));
                return kIOReturnSuccess;
            }
            confirmedHostAPStartPending = false;
            interfaceDrivenHostAPConfirmationPending = false;
            radioResetResumePending = false;
            return stopResult;
        }
        lifecycle = kAirportItlwmAPSTAOwnerLowerBlocked;
        state.hostApTransitionState270 = 1;
        XYLog("APSTA confirmed HostAP replacement observed immediate lower "
              "stop terminal\n");
    }

    const IOReturn result = startLowerIfReady();
    if (result == kIOReturnSuccess) {
        radioResetResumePending = false;
        radioResetWaitForPrimaryStaRun = false;
        radioResetPrimaryStaScanHandoff = false;
        radioResetResumeWaitTicks = 0;
        if (confirmedHostAPStartPending) {
            confirmedHostAPStartPending = false;
            interfaceDrivenHostAPConfirmationPending = false;
            XYLog("APSTA confirmed HostAP replacement reached lower "
                  "running synchronously\n");
        }
        return kIOReturnSuccess;
    }
    if (!apsta_lower_start_retryable(result)) {
        radioResetResumePending = false;
        radioResetWaitForPrimaryStaRun = false;
        radioResetPrimaryStaScanHandoff = false;
        initialHostAPAdmissionPending = false;
        confirmedHostAPStartPending = false;
        interfaceDrivenHostAPConfirmationPending = false;
        radioResetResumeWaitTicks = 0;
        state.hostApTransitionState270 = 0;
        return result;
    }

    /*
     * Tahoe's normal CoreWLAN HostAP sequence can retire the primary Intel
     * MAC/PHY immediately before selector 25 arrives.  Broadcom's recovered
     * setHostApModeInternal represents this exact interval with transition
     * state +0x270 while AP-up +0x26c remains clear.  Intel firmware startup
     * is asynchronous, and its lower-ready callback cannot submit the
     * synchronous AP command sequence from the same task which completes
     * initialization.  Retain the already validated profile and let the
     * controller watchdog retry it after that callback returns.
     *
     * Returning success here acknowledges ownership of the HostAP request;
     * isApRunning(), link state and the APSTA datapath remain fail-closed
     * until startLowerIfReady() really succeeds on a later tick.
     */
    lifecycle = kAirportItlwmAPSTAOwnerLowerBlocked;
    state.resetState26c = 0;
    state.hostApTransitionState270 = 1;
    radioResetResumePending = true;
    radioResetWaitForPrimaryStaRun = false;
    radioResetPrimaryStaScanHandoff = false;
    initialHostAPAdmissionPending = !confirmedHostAPStartPending;
    radioResetResumeWaitTicks = 0;
    XYLog("APSTA lower start deferred result=0x%x channel=%u confirmed=%u\n",
          result, static_cast<unsigned>(apChannel),
          confirmedHostAPStartPending ? 1U : 0U);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmAPSTAOwner::setCipherKey(const struct apple80211_key *key)
{
    if (!isApRunning() || owner == nullptr || owner->fHalService == nullptr) {
        return static_cast<IOReturn>(kAirportItlwmAPSTASetCipherKeyNotUpReturn);
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
    halKey.flags = key->key_flags;
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
    out->param04 = state.softapParam18;
    out->param08 = state.softapParam1c;
    out->param0c = state.softapParam20;
    out->param10 = state.softapParam24;
    out->param14 = state.softapAppliedBeaconInterval68;
    out->mode16 = state.softapMode10;
    out->enabled17 = state.softapParam0e & 1;
    out->param18 = static_cast<uint8_t>(state.softapParam28);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmAPSTAOwner::getSoftAPStats(
    AirportItlwmAPSTASoftAPStatsLayout *out) const
{
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

    memcpy(out->output10, entry->mac01, sizeof(out->output10));
    if (owner == nullptr || owner->fHalService == nullptr) {
        return kIOReturnNotReady;
    }

    ItlHalApStaIEQuery query;
    bzero(&query, sizeof(query));
    query.station = out->mac04;
    query.requestedLength =
        (out->length0c > kAirportItlwmAPSTAGetStaIEListWpaIeNameLength)
            ? (out->length0c - kAirportItlwmAPSTAGetStaIEListWpaIeNameLength)
            : 0;
    query.output = out->output10;
    query.outputCapacity = query.requestedLength;

    IOReturn ret = owner->fHalService->getAPStationIE(&query);
    if (ret != kIOReturnSuccess) {
        return ret;
    }

    out->length0c =
        out->output10[kAirportItlwmAPSTAGetStaIEListReturnedLengthSourceOffset -
                      kAirportItlwmAPSTAGetStaIEListOutputMacOffset] +
        kAirportItlwmAPSTAGetStaIEListReturnedLengthBias;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmAPSTAOwner::getStaStats(AirportItlwmAPSTAStaStatsDataLayout *out)
{
    if (!isApRunning()) {
        return static_cast<IOReturn>(kAirportItlwmAPSTAGetStaStatsNotUpReturn);
    }
    if (out == nullptr) {
        return static_cast<IOReturn>(kAirportItlwmAPSTAGetStaStatsNullReturn);
    }
    if (owner == nullptr || owner->fHalService == nullptr) {
        return kIOReturnNotReady;
    }

    ItlHalApStaStatsQuery query;
    bzero(&query, sizeof(query));
    query.station = out->mac04;

    IOReturn ret = owner->fHalService->getAPStationStats(&query);
    if (ret != kIOReturnSuccess) {
        return ret;
    }

    out->valid00 = kAirportItlwmAPSTAGetStaStatsOutputValidValue;
    out->field0c = query.field0c;
    out->field10 = query.field10;
    out->field14 = query.field14;
    out->field18 = query.field18;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmAPSTAOwner::getKeyRsc(AirportItlwmAPSTAKeyRscDataLayout *out)
{
    if (owner == nullptr || owner->fHalService == nullptr) {
        return kIOReturnNotReady;
    }

    ItlHalApKeyRscQuery query;
    bzero(&query, sizeof(query));
    query.keyIndex = out->keyIndex0e;
    query.rsc = out->rsc54;
    query.rscLength = sizeof(out->rsc54);

    IOReturn ret = owner->fHalService->getAPKeyRSC(&query);
    if (ret != kIOReturnSuccess) {
        return ret;
    }

    out->rscLength50 = kAirportItlwmAPSTAGetKeyRscOutputLengthValue;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmAPSTAOwner::setSoftAPExtCaps(
    const struct apple80211_softap_extended_capabilities_info *in)
{
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
        /* Apple changes maxassoc only after the AP-up gate.  Keep the live
         * firmware-neutral admission table in step with that control-plane
         * update instead of retaining startAPMode()'s initial value forever. */
        (void)owner->fHalService->setAPMaxStations(payload);
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
    state.softapParam28 = static_cast<uint32_t>(in->param18);
    if (!wasEnabled || !disableRequested) {
        setSoftAPPowerSaveState(kAirportItlwmAPSTASetSoftAPParamsHoldPowerState,
                                kAirportItlwmAPSTASetSoftAPParamsHoldPowerReason);
    }
    return static_cast<IOReturn>(kAirportItlwmAPSTASetSoftAPParamsReturn);
}

IOReturn AirportItlwmAPSTAOwner::setRsnConf(
    const struct apple80211_rsn_conf_data *in)
{
    if ((state.rsnConfGate29b & kAirportItlwmAPSTARSNConfGateBit) != 0) {
        return static_cast<IOReturn>(kAirportItlwmAPSTARSNConfRejectedReturn);
    }

    ItlHalApRSNConfig config;
    bzero(&config, sizeof(config));
    config.pairwiseCipherList = in->pairwiseCipherList30;
    config.pairwiseCipherCount =
        apsta_rsn_clamp_count(in->pairwiseCipherCount2c);
    config.groupCipherList = in->groupCipherList80;
    config.groupCipherCount =
        apsta_rsn_clamp_count(in->groupCipherCount7c);
    config.cipherMask =
        apsta_rsn_pairwise_cipher_mask(in) | apsta_rsn_group_cipher_mask(in);
    if (in->pairwiseCipherCount2c != 0 && in->pairwiseVersionCount08 != 0) {
        config.pairwiseVersionList = in->pairwiseVersionList0c;
        config.pairwiseVersionCount =
            apsta_rsn_version_list_count(in->pairwiseVersionCount08);
        config.authMask |= apsta_rsn_auth_mask(
            in->pairwiseVersionList0c, config.pairwiseVersionCount);
    }
    if (in->groupCipherCount7c != 0 && in->groupVersionCount58 != 0) {
        config.groupVersionList = in->groupVersionList5c;
        config.groupVersionCount =
            apsta_rsn_version_list_count(in->groupVersionCount58);
        config.authMask |= apsta_rsn_auth_mask(
            in->groupVersionList5c, config.groupVersionCount);
    }
    config.mfp = in->mfpA0;

    if (owner == nullptr || owner->fHalService == nullptr) {
        return kIOReturnUnsupported;
    }
    return owner->fHalService->setAPRSNConfig(&config);
}

IOReturn AirportItlwmAPSTAOwner::setSoftAPWifiNetworkInfoIE(
    const AirportItlwmAPSTASoftAPWifiNetworkInfoCarrierLayout *in)
{
    if (!owner->isAPSTACoreFeatureFlagSet(
            kAirportItlwmAPSTAWifiNetworkInfoFeatureGate46)) {
        return kIOReturnSuccess;
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
    csa.mode = 0;
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
    csa.mode = in->mode10;
    csa.count = kItlApCsaDefaultCount;
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
            state.softapAssociatedStaCount00++;
            return entry;
        }
    }
    return nullptr;
}

void AirportItlwmAPSTAOwner::removeStation(const uint8_t *macAddr)
{
    AirportItlwmAPSTAStationTableEntryLayout *entry = findStation(macAddr);
    if (entry != nullptr) {
        if (state.softapAssociatedStaCount00 != 0) {
            state.softapAssociatedStaCount00--;
        }
        clearStation(entry);
    }
}

void AirportItlwmAPSTAOwner::noteLowerAssociatedStation(
    const uint8_t *macAddr)
{
    if (macAddr == nullptr || apsta_mac_is_zero(macAddr))
        return;
    for (unsigned i = 0; i < kAirportItlwmAPSTAStationTableEntryCount; i++) {
        if (memcmp(lowerAssociatedStaMacs[i], macAddr,
                   IEEE80211_ADDR_LEN) == 0)
            return;
    }
    for (unsigned i = 0; i < kAirportItlwmAPSTAStationTableEntryCount; i++) {
        if (!apsta_mac_is_zero(lowerAssociatedStaMacs[i]))
            continue;
        memcpy(lowerAssociatedStaMacs[i], macAddr, IEEE80211_ADDR_LEN);
        lowerAssociatedStaCount++;
        return;
    }
}

void AirportItlwmAPSTAOwner::forgetLowerAssociatedStation(
    const uint8_t *macAddr)
{
    if (macAddr == nullptr || apsta_mac_is_zero(macAddr))
        return;
    for (unsigned i = 0; i < kAirportItlwmAPSTAStationTableEntryCount; i++) {
        if (memcmp(lowerAssociatedStaMacs[i], macAddr,
                   IEEE80211_ADDR_LEN) != 0)
            continue;
        bzero(lowerAssociatedStaMacs[i], IEEE80211_ADDR_LEN);
        if (lowerAssociatedStaCount != 0)
            lowerAssociatedStaCount--;
        return;
    }
}

void AirportItlwmAPSTAOwner::clearLowerAssociatedStations()
{
    lowerAssociatedStaCount = 0;
    bzero(lowerAssociatedStaMacs, sizeof(lowerAssociatedStaMacs));
}

bool AirportItlwmAPSTAOwner::areAllStationsInLowPowerMode() const
{
    for (unsigned i = 0; i < kAirportItlwmAPSTAStationTableEntryCount; i++) {
        const AirportItlwmAPSTAStationTableEntryLayout *entry =
            &state.softapStaTableB8[i];
        if ((entry->active00 & 1U) != 0 &&
            entry->sleepState10 == kAirportItlwmAPSTACheckAllStaBlockingSleepState) {
            return false;
        }
    }
    return true;
}

IOReturn AirportItlwmAPSTAOwner::postStationMessage(
    uint32_t messageId,
    const void *payload,
    size_t payloadLength)
{
    if (owner == nullptr || owner->fNetIf == nullptr) {
        return kIOReturnNotReady;
    }
    owner->postMessage(owner->fNetIf, messageId,
                       const_cast<void *>(payload),
                       static_cast<unsigned long>(payloadLength), true);
    return kIOReturnSuccess;
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
    const uint8_t *ies,
    uint32_t iesLength,
    uint32_t status,
    uint32_t reason,
    uint32_t authType,
    const uint8_t *eventData,
    uint32_t eventDataLength)
{
    if (macAddr == nullptr) {
        return kIOReturnBadArgument;
    }

    switch (eventType) {
        case kAirportItlwmAPSTAEventAuthInd: {
            if (status != kAirportItlwmAPSTAEventAuthIndRequiredStatus ||
                authType != kAirportItlwmAPSTAEventAuthIndRequiredAuthType) {
                return kIOReturnSuccess;
            }
            AirportItlwmAPSTAAuthIndMessageLayout message;
            bzero(&message, sizeof(message));
            message.type00 = kAirportItlwmAPSTAEventAuthIndTypeValue;
            message.status08 = apsta_auth_ind_status_from_reason(reason);
            apsta_copy_mac_prefix(&message.macDword0c, &message.macTail10, macAddr);
            apsta_copy_auth_ind_chunks(eventData, eventDataLength, &message);
            (void)postStationMessage(kAirportItlwmAPSTAEventAuthIndMessageId,
                                     &message, sizeof(message));
            return kIOReturnSuccess;
        }
        case kAirportItlwmAPSTAEventAssocInd:
        case kAirportItlwmAPSTAEventReassocInd: {
            if (!AirportItlwmAPSTAEventContracts::associationStatusIsSuccessful(
                    status, reason)) {
                return kIOReturnSuccess;
            }
            /* Firmware association is authoritative for radio power.  The
             * recovered public event path intentionally suppresses a hidden
             * non-Apple peer, but that policy must not make S3 tear down an
             * AP which still has a live Linux/Android station. */
            noteLowerAssociatedStation(macAddr);
            const bool foundAppleIE =
                apsta_check_for_apple_ie(ies, iesLength);
            if (!AirportItlwmAPSTAEventContracts::associationIsAdmitted(
                    status, reason, foundAppleIE, state.hiddenNetworkFlag0d)) {
                return kIOReturnSuccess;
            }

            apsta_copy_mac_prefix(&state.softapEvent80, &state.softapEvent84, macAddr);
            AirportItlwmAPSTAStationTableEntryLayout *entry = allocateStation(macAddr);
            if (entry != nullptr) {
                const uint32_t appleFlags =
                    apsta_extract_instant_hotspot_flags(ies, iesLength);
                entry->aihsFlag20 =
                    (appleFlags & kAirportItlwmAPSTAEventAssocFlagAihs) ? 1 : 0;
                entry->sharingFlag24 =
                    (appleFlags & kAirportItlwmAPSTAEventAssocFlagSharing) ? 1 : 0;
                if (foundAppleIE) {
                    entry->appleStationFlag28 = 1;
                }
            }

            AirportItlwmAPSTAStaAssocMessageLayout message;
            bzero(&message, sizeof(message));
            apsta_copy_mac_prefix(&message.macDword00, &message.macTail04, macAddr);
            message.associatedCount08 = state.softapAssociatedStaCount00;
            if (entry != nullptr) {
                message.assocFlags0c = static_cast<uint8_t>(
                    (entry->aihsFlag20 ? kAirportItlwmAPSTAEventAssocFlagAihs : 0) |
                    (entry->sharingFlag24 ?
                        kAirportItlwmAPSTAEventAssocFlagSharing : 0));
            }
            if (foundAppleIE) {
                message.assocFlags0c |=
                    kAirportItlwmAPSTAEventAssocFlagAppleStation;
            }
            apsta_copy_rsnxe(ies, iesLength, message.rsnxe10, sizeof(message.rsnxe10));
            (void)postStationMessage(kAirportItlwmAPSTAEventAssocMessageId,
                                     &message, sizeof(message));
            return kIOReturnSuccess;
        }
        case kAirportItlwmAPSTAEventDeauth:
        case kAirportItlwmAPSTAEventDeauthInd:
        case kAirportItlwmAPSTAEventDisassoc:
        case kAirportItlwmAPSTAEventDisassocInd: {
            apsta_copy_mac_prefix(&state.softapEvent80, &state.softapEvent84, macAddr);
            forgetLowerAssociatedStation(macAddr);
            removeStation(macAddr);
            AirportItlwmAPSTAStaRemoveMessageLayout message;
            bzero(&message, sizeof(message));
            apsta_copy_mac_prefix(&message.macDword00, &message.macTail04, macAddr);
            message.associatedCount08 = state.softapAssociatedStaCount00;
            (void)postStationMessage(kAirportItlwmAPSTAEventRemoveMessageId,
                                     &message, sizeof(message));
            return kIOReturnSuccess;
        }
        case kAirportItlwmAPSTAEventActionFrame: {
            AirportItlwmAPSTAStationTableEntryLayout *entry = findStation(macAddr);
            if (entry == nullptr) {
                return kIOReturnSuccess;
            }

            uint8_t category = kAirportItlwmAPSTAActionFrameUnknownCategoryAction;
            uint8_t action = kAirportItlwmAPSTAActionFrameUnknownCategoryAction;
            if (!AirportItlwmAPSTAEventContracts::parseActionFrame(
                    eventData, eventDataLength, &category, &action)) {
                return kIOReturnSuccess;
            }
            if (AirportItlwmAPSTAEventContracts::isLphsStateAction(
                    category, action)) {
                entry->sleepState10 = action;
            }

            const bool concurrencyEnabled = owner != nullptr &&
                owner->isAPSTASoftAPConcurrencyEnabled();
            if (areAllStationsInLowPowerMode() && !concurrencyEnabled) {
                setSoftAPPowerSaveState(
                    kAirportItlwmAPSTAActionFrameAllStaPowerSaveState,
                    kAirportItlwmAPSTAActionFrameAllStaPowerSaveReason);
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
    (void)owner->publishStationEventFromNet80211(
        static_cast<uint32_t>(event), ni->ni_macaddr,
        ni->ni_rsnie_tlv, ni->ni_rsnie_tlv_len);
}
