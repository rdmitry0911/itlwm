//
//  AirportSTAIOCTL.cpp
//  AirportItlwm
//
//  Created by qcwap on 2020/9/4.
//  Copyright © 2020 钟先耀. All rights reserved.
//

#include "AirportItlwm.hpp"
#include "AirportItlwmAPSTAInterface.hpp"
#include "AirportItlwmAPSTAOwner.hpp"
#include "AirportItlwmCountryCode.hpp"
#include "TahoeAssociationContracts.hpp"
#include "TahoeCapabilityContracts.hpp"
#include "TahoeLqmContracts.hpp"
#include "TahoeNrateContracts.hpp"
#include "TahoeOpModeContracts.hpp"
#include "TahoePhyModeContracts.hpp"

extern IOCommandGate *_fCommandGate;

static constexpr IOReturn kApple80211ErrDriverNotAvailable = 0xe0822403;
static constexpr IOReturn kApple80211ErrConfigNoValue =
    static_cast<IOReturn>(TahoeNrateContracts::kConfigNoValueStatus);
static constexpr IOReturn kApple80211ErrNoCachedValue = 0xe00002f0;
static constexpr IOReturn kApple80211ErrInvalidArgumentRaw = 0x16;

static void publishResolvedCountryCodeProperty(AirportItlwm *controller)
{
    if (controller == nullptr || controller->fNetIf == nullptr ||
        controller->fHalService == nullptr)
        return;

    char userOverrideCc[APPLE80211_MAX_CC_LEN];
    uint8_t resolvedCc[APPLE80211_MAX_CC_LEN];
    memset(userOverrideCc, 0, sizeof(userOverrideCc));
    PE_parse_boot_argn("itlwm_cc", userOverrideCc, sizeof(userOverrideCc));

    AirportItlwmCountryCode::selectCountryCode(
        controller->fHalService, userOverrideCc,
        controller->fHalService->getDriverInfo()->getFirmwareCountryCode(),
        controller->geo_location_cc, resolvedCc);

    OSString *value =
        OSString::withCString(reinterpret_cast<const char *>(resolvedCc));
    if (value != nullptr) {
        controller->fNetIf->setProperty(APPLE80211_REGKEY_COUNTRY_CODE, value);
        value->release();
    }
    controller->fNetIf->setProperty(
        APPLE80211_REGKEY_LOCALE,
        AirportItlwmCountryCode::localePropertyString(APPLE80211_LOCALE_FCC));
}

static bool tahoeComHasVhtMcsCarrier(const ieee80211com *ic)
{
    if (ic == nullptr)
        return false;

    if (ic->ic_vht_rx_mcs_map != 0 || ic->ic_vht_tx_mcs_map != 0)
        return true;

    for (size_t i = 0; i < sizeof(ic->ic_vht_sup_mcs) /
        sizeof(ic->ic_vht_sup_mcs[0]); i++) {
        if (ic->ic_vht_sup_mcs[i] != 0)
            return true;
    }

    return false;
}

static bool tahoeComHasVhtCapability(const ieee80211com *ic)
{
    return ic != nullptr &&
        TahoePhyModeContracts::hasCompleteVhtCapability(
            ic->ic_vhtcaps, tahoeComHasVhtMcsCarrier(ic));
}

static bool tahoeComHasHeCapsCarrier(const ieee80211com *ic)
{
    if (ic == nullptr)
        return false;
    if (ic->ic_hecaps != 0)
        return true;

    for (size_t i = 0; i < sizeof(ic->ic_he_cap_elem.mac_cap_info); i++) {
        if (ic->ic_he_cap_elem.mac_cap_info[i] != 0)
            return true;
    }
    for (size_t i = 0; i < sizeof(ic->ic_he_cap_elem.phy_cap_info); i++) {
        if (ic->ic_he_cap_elem.phy_cap_info[i] != 0)
            return true;
    }

    return false;
}

static bool tahoeComHasHeMcsCarrier(const ieee80211com *ic)
{
    if (ic == nullptr)
        return false;

    const uint8_t *mcs =
        reinterpret_cast<const uint8_t *>(&ic->ic_he_mcs_nss_supp);
    for (size_t i = 0; i < sizeof(ic->ic_he_mcs_nss_supp); i++) {
        if (mcs[i] != 0)
            return true;
    }

    return false;
}

static bool tahoeComHasHeCapability(const ieee80211com *ic)
{
    if (ic == nullptr)
        return false;

    return TahoePhyModeContracts::hasCompleteHeCapability(
        tahoeComHasHeCapsCarrier(ic), tahoeComHasHeMcsCarrier(ic));
}

static uint32_t tahoeSupportedPhyModeForController(const ieee80211com *ic)
{
    bool supports5GHz = false;
    bool supports2GHzCck = false;
    bool supports2GHzOfdm = false;

    if (ic != nullptr) {
        for (uint32_t i = 0; i <= IEEE80211_CHAN_MAX; i++) {
            const ieee80211_channel *channel = &ic->ic_channels[i];
            if (channel->ic_flags == 0)
                continue;

            if (IEEE80211_IS_CHAN_5GHZ(channel))
                supports5GHz = true;
            if (IEEE80211_IS_CHAN_B(channel))
                supports2GHzCck = true;
            if (IEEE80211_IS_CHAN_PUREG(channel) ||
                IEEE80211_IS_CHAN_G(channel))
                supports2GHzOfdm = true;
        }
    }

    return TahoePhyModeContracts::buildSupportedPhyMode(
        supports5GHz, supports2GHzCck, supports2GHzOfdm,
        ic != nullptr && ic->ic_htcaps != 0,
        tahoeComHasVhtCapability(ic),
        tahoeComHasHeCapability(ic));
}

static bool tahoeNodeHas2GHzOfdmRate(const ieee80211_node *ni)
{
    if (ni == nullptr)
        return false;

    for (uint8_t i = 0; i < ni->ni_rates.rs_nrates; i++) {
        if ((ni->ni_rates.rs_rates[i] & IEEE80211_RATE_VAL) >= 12)
            return true;
    }

    return false;
}

static uint32_t tahoeActivePhyModeForBss(const ieee80211com *ic,
                                         const ieee80211_node *ni)
{
    const bool hasHe = ic != nullptr && ni != nullptr &&
        tahoeComHasHeCapability(ic) &&
        (ni->ni_flags & IEEE80211_NODE_HE) != 0;
    const bool hasVht = ic != nullptr && ni != nullptr &&
        tahoeComHasVhtCapability(ic) &&
        ieee80211_node_supports_vht(const_cast<ieee80211_node *>(ni));
    const bool hasHt = ic != nullptr && ni != nullptr &&
        ic->ic_htcaps != 0 &&
        ieee80211_node_supports_ht(const_cast<ieee80211_node *>(ni));
    const bool is5GHz = ni != nullptr && ni->ni_chan != nullptr &&
        ni->ni_chan != IEEE80211_CHAN_ANYC &&
        IEEE80211_IS_CHAN_5GHZ(ni->ni_chan);

    return TahoePhyModeContracts::activePhyModeForAssociatedBss(
        hasHe, hasVht, hasHt, is5GHz, tahoeNodeHas2GHzOfdmRate(ni));
}

static constexpr uint16_t kAppleTahoeQTxpowerTable[40] = {
    0x1a1b, 0x1ba7, 0x1d4b, 0x1f07, 0x20de, 0x22d1, 0x24e1, 0x2710,
    0x2961, 0x2bd4, 0x2e6d, 0x312d, 0x3417, 0x372d, 0x3a72, 0x3de9,
    0x4194, 0x4577, 0x4994, 0x4df1, 0x528f, 0x5773, 0x5ca2, 0x621f,
    0x67ef, 0x6e18, 0x749e, 0x7b87, 0x82d9, 0x8a99, 0x92d0, 0x9b83,
    0xa4ba, 0xae7c, 0xb8d3, 0xc3c7, 0xcf60, 0xdbaa, 0xe8ae, 0xf678,
};

static uint32_t decodeAppleTahoeQTxpowerRaw(uint8_t raw)
{
    uint32_t scale = 0xffff;
    if (raw < 0xc1) {
        uint32_t tableIndex;
        if (raw < 0x99) {
            int current = static_cast<int>(raw) - 0xc1;
            int last = current;
            scale = 1;
            do {
                last = current;
                scale *= 10;
                current = last + 0x28;
            } while (current < -0x28);
            tableIndex = static_cast<uint32_t>(last + 0x50);
        } else {
            tableIndex = static_cast<uint32_t>(raw - 0x99);
            scale = 1;
        }
        scale = (((scale >> 1) + kAppleTahoeQTxpowerTable[tableIndex]) / scale) & 0xffff;
    }
    return scale;
}

static uint8_t encodeAppleTahoeQTxpowerBootstrap(uint8_t maxHalfDbm)
{
    int raw = static_cast<int>(maxHalfDbm) * 2 - 136;
    raw = MAX(-128, MIN(127, raw));
    return static_cast<uint8_t>(static_cast<int8_t>(raw));
}

static bool getTahoeCachedQTxpowerRaw(ItlHalService *hal, uint8_t *raw)
{
    if (hal == nullptr || raw == nullptr)
        return false;

    if (auto *iwm = OSDynamicCast(ItlIwm, hal)) {
        struct iwm_softc *sc = &iwm->com;
        if (sc->sc_has_last_qtxpower_raw) {
            *raw = sc->sc_last_qtxpower_raw;
            return true;
        }
        *raw = encodeAppleTahoeQTxpowerBootstrap(sc->sc_nvm.max_tx_pwr_half_dbm);
        return true;
    }

    if (auto *iwx = OSDynamicCast(ItlIwx, hal)) {
        struct iwx_softc *sc = &iwx->com;
        if (sc->sc_has_last_qtxpower_raw) {
            *raw = sc->sc_last_qtxpower_raw;
            return true;
        }
        *raw = 0xaa;
        return true;
    }

    return false;
}

static IOReturn getTahoeCachedNrate(ItlHalService *hal, uint32_t *rate)
{
    if (hal == nullptr || rate == nullptr)
        return kIOReturnError;

    *rate = 0;

    if (auto *iwm = OSDynamicCast(ItlIwm, hal)) {
        struct iwm_softc *sc = &iwm->com;
        if (!TahoeNrateContracts::normalizeIwmRateNFlagsToAppleNrate(
                sc->lq_sta.rs_drv.last_rate_n_flags, rate))
            return kApple80211ErrConfigNoValue;
        return kIOReturnSuccess;
    }

    if (auto *iwx = OSDynamicCast(ItlIwx, hal)) {
        struct iwx_softc *sc = &iwx->com;
        if (!sc->sc_has_last_rate_n_flags)
            return kApple80211ErrConfigNoValue;
        if (!TahoeNrateContracts::normalizeIwxRateNFlagsToAppleNrate(
                sc->sc_last_rate_n_flags, rate))
            return kApple80211ErrConfigNoValue;
        return kIOReturnSuccess;
    }

    if (auto *iwn = OSDynamicCast(ItlIwn, hal)) {
        struct iwn_softc *sc = &iwn->com;
        if (!sc->sc_has_last_apple_nrate)
            return kApple80211ErrConfigNoValue;
        *rate = sc->sc_last_apple_nrate;
        return kIOReturnSuccess;
    }

    return kApple80211ErrConfigNoValue;
}

static IOReturn getTahoeCurrentRateMbps(ItlHalService *hal, uint32_t *rateMbps)
{
    if (rateMbps == nullptr)
        return kIOReturnBadArgument;

    uint32_t nrate = 0;
    IOReturn ret = getTahoeCachedNrate(hal, &nrate);
    if (ret != kIOReturnSuccess)
        return ret;

    if (!TahoeNrateContracts::decodeRateMbpsFromNrate(nrate, rateMbps))
        return kApple80211ErrConfigNoValue;
    return kIOReturnSuccess;
}

static IOReturn storeAssocRsnIeOverride(struct ieee80211com *ic,
                                        const uint8_t *ie,
                                        uint16_t length)
{
#ifdef USE_APPLE_SUPPLICANT
    if (ic == nullptr || (length > 0 && ie == nullptr))
        return kIOReturnError;

    static_assert(sizeof(ic->ic_rsn_ie_override) == APPLE80211_MAX_RSN_IE_LEN,
                  "Max RSN IE length mismatch");
    const uint16_t copyLen = TahoeAssociationContracts::boundedRsnIeLength(
        length, APPLE80211_MAX_RSN_IE_LEN);
    memset(ic->ic_rsn_ie_override, 0, sizeof(ic->ic_rsn_ie_override));
    if (copyLen > 0)
        memcpy(ic->ic_rsn_ie_override, ie, copyLen);
    if (ic->ic_state == IEEE80211_S_RUN && ic->ic_bss != nullptr &&
        copyLen >= 2 && ic->ic_rsn_ie_override[1] > 0)
        ieee80211_save_ie(ic->ic_rsn_ie_override, &ic->ic_bss->ni_rsnie);
    return kIOReturnSuccess;
#else
    (void)ic;
    (void)ie;
    (void)length;
    return kIOReturnUnsupported;
#endif
}

static bool decodeTahoeMcsIndexFromCachedNrate(uint32_t rate, uint32_t *index)
{
    return TahoeNrateContracts::decodeMcsIndexFromNrate(rate, index);
}

static IOReturn fillTahoeMcsVhtFromCachedNrate(ItlHalService *hal, apple80211_mcs_vht_data *data)
{
    if (data == nullptr)
        return kIOReturnBadArgument;

    uint32_t rate = 0;
    IOReturn ret = getTahoeCachedNrate(hal, &rate);
    if (TahoeNrateContracts::isAcceptedQueryStatus(static_cast<uint32_t>(ret)))
        TahoeNrateContracts::fillMcsVhtFromNrate(rate, data);
    return ret;
}

SInt32 AirportItlwm::apple80211Request(unsigned int request_type,
                                       int request_number,
                                       IO80211Interface *interface,
                                       void *data)
{
    if (request_type != SIOCGA80211 && request_type != SIOCSA80211)
        return kIOReturnError;
    IOReturn ret = kIOReturnError;

    switch (request_number) {
        case APPLE80211_IOC_SSID:  // 1
            IOCTL(request_type, SSID, apple80211_ssid_data);
            break;
        case APPLE80211_IOC_AUTH_TYPE:  // 2
            IOCTL(request_type, AUTH_TYPE, apple80211_authtype_data);
            break;
        case APPLE80211_IOC_CHANNEL:  // 4
            IOCTL(request_type, CHANNEL, apple80211_channel_data);
            break;
        case APPLE80211_IOC_PROTMODE:
            IOCTL(request_type, PROTMODE, apple80211_protmode_data);
            break;
        case APPLE80211_IOC_TXPOWER:  // 7
            IOCTL_GET(request_type, TXPOWER, apple80211_txpower_data);
            break;
        case APPLE80211_IOC_RATE:  // 8
            IOCTL_GET(request_type, RATE, apple80211_rate_data);
            break;
        case APPLE80211_IOC_BSSID:  // 9
            IOCTL(request_type, BSSID, apple80211_bssid_data);
            break;
        case APPLE80211_IOC_SCAN_REQ:  // 10
            IOCTL_SET(request_type, SCAN_REQ, apple80211_scan_data);
            break;
        case APPLE80211_IOC_SCAN_REQ_MULTIPLE:
            IOCTL_SET(request_type, SCAN_REQ_MULTIPLE, apple80211_scan_multiple_data);
            break;
        case APPLE80211_IOC_SCAN_RESULT:  // 11
            IOCTL_GET(request_type, SCAN_RESULT, apple80211_scan_result*);
            break;
        case APPLE80211_IOC_CARD_CAPABILITIES:  // 12
            IOCTL_GET(request_type, CARD_CAPABILITIES, apple80211_capability_data);
            break;
        case APPLE80211_IOC_STATE:  // 13
            IOCTL_GET(request_type, STATE, apple80211_state_data);
            break;
        case APPLE80211_IOC_PHY_MODE:  // 14
            IOCTL_GET(request_type, PHY_MODE, apple80211_phymode_data);
            break;
        case APPLE80211_IOC_OP_MODE:  // 15
            IOCTL_GET(request_type, OP_MODE, apple80211_opmode_data);
            break;
        case APPLE80211_IOC_RSSI:  // 16
            IOCTL_GET(request_type, RSSI, apple80211_rssi_data);
            break;
        case APPLE80211_IOC_NOISE:  // 17
            IOCTL_GET(request_type, NOISE, apple80211_noise_data);
            break;
        case APPLE80211_IOC_INT_MIT:  // 18
            IOCTL_GET(request_type, INT_MIT, apple80211_intmit_data);
            break;
        case APPLE80211_IOC_POWER:  // 19
            IOCTL(request_type, POWER, apple80211_power_data);
            break;
        case APPLE80211_IOC_ASSOCIATE:  // 20
            IOCTL_SET(request_type, ASSOCIATE, apple80211_assoc_data);
            break;
        case APPLE80211_IOC_ASSOCIATE_RESULT: // 21
            IOCTL_GET(request_type, ASSOCIATE_RESULT, apple80211_assoc_result_data);
            break;
        case APPLE80211_IOC_DISASSOCIATE: // 22
            if (request_type == SIOCSA80211)
                setDISASSOCIATE(interface);
            break;
        case APPLE80211_IOC_RATE_SET:
            IOCTL_GET(request_type, RATE_SET, apple80211_rate_set_data);
            break;
        case APPLE80211_IOC_MCS_INDEX_SET:
            IOCTL_GET(request_type, MCS_INDEX_SET, apple80211_mcs_index_set_data);
            break;
        case APPLE80211_IOC_VHT_MCS_INDEX_SET:
            IOCTL_GET(request_type, VHT_MCS_INDEX_SET, apple80211_vht_mcs_index_set_data);
            break;
        case APPLE80211_IOC_MCS_VHT:
            IOCTL(request_type, MCS_VHT, apple80211_mcs_vht_data);
            break;
        case APPLE80211_IOC_SUPPORTED_CHANNELS:  // 27
        case APPLE80211_IOC_HW_SUPPORTED_CHANNELS:
            IOCTL_GET(request_type, SUPPORTED_CHANNELS, apple80211_sup_channel_data);
            break;
        case APPLE80211_IOC_LOCALE:  // 28
            IOCTL_GET(request_type, LOCALE, apple80211_locale_data);
            break;
        case APPLE80211_IOC_DEAUTH:
            IOCTL(request_type, DEAUTH, apple80211_deauth_data);
            break;
        case APPLE80211_IOC_TX_ANTENNA:  // 37
            IOCTL_GET(request_type, TX_ANTENNA, apple80211_antenna_data);
            break;
        case APPLE80211_IOC_ANTENNA_DIVERSITY:  // 39
            IOCTL_GET(request_type, ANTENNA_DIVERSITY, apple80211_antenna_data);
            break;
        case APPLE80211_IOC_DRIVER_VERSION:  // 43
            IOCTL_GET(request_type, DRIVER_VERSION, apple80211_version_data);
            break;
        case APPLE80211_IOC_HARDWARE_VERSION:  // 44
            IOCTL_GET(request_type, HARDWARE_VERSION, apple80211_version_data);
            break;
        case APPLE80211_IOC_RSN_IE: // 46
            IOCTL(request_type, RSN_IE, apple80211_rsn_ie_data);
            break;
        case APPLE80211_IOC_RSN_CONF:
            IOCTL_SET(request_type, RSN_CONF, apple80211_rsn_conf_data);
            break;
        case APPLE80211_IOC_AP_IE_LIST: // 48
            if (request_type != SIOCGA80211)
                return kIOReturnError;
            IOCTL_GET(request_type, AP_IE_LIST, apple80211_ap_ie_data);
            break;
        case APPLE80211_IOC_ASSOCIATION_STATUS:  // 50
            IOCTL_GET(request_type, ASSOCIATION_STATUS, apple80211_assoc_status_data);
            break;
        case APPLE80211_IOC_COUNTRY_CODE:  // 51
            IOCTL(request_type, COUNTRY_CODE, apple80211_country_code_data);
            break;
        case APPLE80211_IOC_RADIO_INFO:
            IOCTL_GET(request_type, RADIO_INFO, apple80211_radio_info_data);
            break;
        case APPLE80211_IOC_MCS:  // 57
            IOCTL_GET(request_type, MCS, apple80211_mcs_data);
            break;
        case APPLE80211_IOC_VIRTUAL_IF_CREATE: // 94
            IOCTL_SET(request_type, VIRTUAL_IF_CREATE, apple80211_virt_if_create_data);
            break;
        case APPLE80211_IOC_VIRTUAL_IF_DELETE:
            IOCTL_SET(request_type, VIRTUAL_IF_DELETE, apple80211_virt_if_delete_data);
            break;
        case APPLE80211_IOC_VIRTUAL_IF_ROLE:
        case APPLE80211_IOC_VIRTUAL_IF_PARENT:
            // Tahoe airportd/CoreWiFi _initInterface issues these payload-less
            // selectors on the controller-owned EXTERNAL Apple80211 request
            // path.  Live build 5cb2a53 proved the earlier interface-side fix
            // in AirportItlwmSkywalkInterface::processApple80211Ioctl() was
            // not enough: IO80211Family still logged raw FAIL:6 because this
            // dispatcher had no cases for 96/97 at all, so the request fell
            // into the generic unhandled path before the Tahoe bridge could
            // matter.
            //
            // The recovered Apple contract for a normal infrastructure
            // interface is not ENXIO/6 here.  Both selectors must return the
            // Apple-specific "not a virtual interface" code:
            //   APPLE80211_IOC_VIRTUAL_IF_ROLE   -> 0xe082280e
            //   APPLE80211_IOC_VIRTUAL_IF_PARENT -> 0xe082280e
            ret = static_cast<IOReturn>(0xe082280e);
            break;
        case APPLE80211_IOC_ROAM_THRESH:
            IOCTL_GET(request_type, ROAM_THRESH, apple80211_roam_threshold_data);
            break;
        case APPLE80211_IOC_LINK_CHANGED_EVENT_DATA:
            IOCTL_GET(request_type, LINK_CHANGED_EVENT_DATA, apple80211_link_changed_event_data);
            break;
        case APPLE80211_IOC_POWERSAVE:
            IOCTL_GET(request_type, POWERSAVE, apple80211_powersave_data);
            break;
        case APPLE80211_IOC_CIPHER_KEY:
            IOCTL_SET(request_type, CIPHER_KEY, apple80211_key);
            break;
        case APPLE80211_IOC_SCANCACHE_CLEAR:
            IOCTL_SET(request_type, SCANCACHE_CLEAR, apple80211req);
            break;
        case APPLE80211_IOC_TX_NSS:
            IOCTL(request_type, TX_NSS, apple80211_tx_nss_data);
            break;
        case APPLE80211_IOC_NSS:
            IOCTL_GET(request_type, NSS, apple80211_nss_data);
            break;
        case APPLE80211_IOC_ROAM:
            IOCTL_SET(request_type, ROAM, apple80211_sta_roam_data);
            break;
        case APPLE80211_IOC_ROAM_PROFILE:
            IOCTL(request_type, ROAM_PROFILE, apple80211_roam_profile_band_data);
            break;
        case APPLE80211_IOC_WOW_PARAMETERS:
            IOCTL(request_type, WOW_PARAMETERS, apple80211_wow_parameter_data);
            break;
        case APPLE80211_IOC_IE:
            IOCTL(request_type, IE, apple80211_ie_data);
            break;
        case APPLE80211_IOC_P2P_LISTEN:
            IOCTL_SET(request_type, P2P_LISTEN, apple80211_p2p_listen_data);
            break;
        case APPLE80211_IOC_P2P_SCAN:
            IOCTL_SET(request_type, P2P_SCAN, apple80211_scan_data);
            break;
        case APPLE80211_IOC_P2P_GO_CONF:
            IOCTL_SET(request_type, P2P_GO_CONF, apple80211_p2p_go_conf_data);
            break;
        case APPLE80211_IOC_BTCOEX_PROFILES:
            IOCTL(request_type, BTCOEX_PROFILES, apple80211_btc_profiles_data);
            break;
        case APPLE80211_IOC_BTCOEX_CONFIG:
            IOCTL(request_type, BTCOEX_CONFIG, apple80211_btc_config_data);
            break;
        case APPLE80211_IOC_BTCOEX_OPTIONS:
            IOCTL(request_type, BTCOEX_OPTIONS, apple80211_btc_options_data);
            break;
        case APPLE80211_IOC_BTCOEX_MODE:
            IOCTL(request_type, BTCOEX_MODE, apple80211_btc_mode_data);
            break;
        case APPLE80211_IOC_SOFTAP_EXTENDED_CAPABILITIES_IE:
            IOCTL_SET(request_type, SOFTAP_EXTENDED_CAPABILITIES_IE,
                      apple80211_softap_extended_capabilities_info);
            break;
        case APPLE80211_IOC_MIS_MAX_STA:
            IOCTL_SET(request_type, MIS_MAX_STA, apple80211_mis_max_sta);
            break;
        default:
        unhandled:
            if (!ml_at_interrupt_context()) {
                XYLog("%s Unhandled IOCTL %s (%d) %s\n", __FUNCTION__, IOCTL_NAMES[request_number >= ARRAY_SIZE(IOCTL_NAMES) ? 0: request_number],
                      request_number, request_type == SIOCGA80211 ? "get" : (request_type == SIOCSA80211 ? "set" : "other"));
            }
            break;
    }

    return ret;
}

IOReturn AirportItlwm::
getSSID(OSObject *object,
                        struct apple80211_ssid_data *sd)
{
    struct ieee80211com * ic = fHalService->get80211Controller();
    // Apple's IO80211Controller::getSSIDData always pre-zeroes and returns success.
    // When not associated, ssid_len=0. Returning error causes airportd "driver not available".
    memset(sd, 0, sizeof(*sd));
    sd->version = APPLE80211_VERSION;
    if (ic->ic_state == IEEE80211_S_RUN && ic->ic_bss != NULL &&
        ic->ic_bss->ni_esslen <= APPLE80211_MAX_SSID_LEN) {
        sd->ssid_len = ic->ic_bss->ni_esslen;
        if (sd->ssid_len != 0)
            memcpy(sd->ssid_bytes, ic->ic_bss->ni_essid, sd->ssid_len);
    }
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
setSSID(OSObject *object, struct apple80211_ssid_data *sd)
{
    RT2_SET(5);
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
getAUTH_TYPE(OSObject *object, struct apple80211_authtype_data *ad)
{
    ad->version = APPLE80211_VERSION;
    ad->authtype_lower = current_authtype_lower;
    ad->authtype_upper = current_authtype_upper;
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
setAUTH_TYPE(OSObject *object, struct apple80211_authtype_data *ad)
{
    current_authtype_lower = ad->authtype_lower;
    current_authtype_upper = ad->authtype_upper;
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
setCIPHER_KEY(OSObject *object, struct apple80211_key *key)
{
    static_assert(__offsetof(struct apple80211_key, key_ea) == 92, "struct corrupted");
    static_assert(__offsetof(struct apple80211_key, key_rsc_len) == 80, "struct corrupted");
    static_assert(__offsetof(struct apple80211_key, wowl_kck_len) == 100, "struct corrupted");
    static_assert(__offsetof(struct apple80211_key, wowl_kek_len) == 120, "struct corrupted");
    static_assert(__offsetof(struct apple80211_key, wowl_kck_key) == 104, "struct corrupted");
    
    switch (key->key_cipher_type) {
        case APPLE80211_CIPHER_NONE:
            // clear existing key
//            XYLog("Setting NONE key is not supported\n");
            break;
        case APPLE80211_CIPHER_WEP_40:
        case APPLE80211_CIPHER_WEP_104:
            XYLog("Setting WEP key %d is not supported\n", key->key_index);
            break;
        case APPLE80211_CIPHER_TKIP:
        case APPLE80211_CIPHER_AES_OCB:
        case APPLE80211_CIPHER_AES_CCM:
            switch (key->key_flags) {
                case 4: // PTK
                    setPTK(key->key, key->key_len);
                    getNetworkInterface()->postMessage(APPLE80211_M_RSN_HANDSHAKE_DONE);
                    break;
                case 0: // GTK
                    setGTK(key->key, key->key_len, key->key_index, key->key_rsc);
                    getNetworkInterface()->postMessage(APPLE80211_M_RSN_HANDSHAKE_DONE);
                    break;
            }
            break;
        case APPLE80211_CIPHER_PMK:
            XYLog("Setting WPA PMK is not supported\n");
            break;
        case APPLE80211_CIPHER_PMKSA:
            XYLog("Setting WPA PMKSA is not supported\n");
            break;
    }
    //fInterface->postMessage(APPLE80211_M_CIPHER_KEY_CHANGED);
    return kIOReturnSuccess;
}

// From Ventura, airport/wifiagent seems that they don't like to accept extra channel flags in the scan result list,
// if not, the exact behavior is that the wifi list on the control center/menu bar will not refresh after system boot.
#if __IO80211_TARGET >= __MAC_13_0
static int ieeeChanFlag2appleScanFlagVentura(int flags)
{
    int ret = 0;
    if (flags & IEEE80211_CHAN_2GHZ)
        ret |= APPLE80211_C_FLAG_2GHZ;
    if (flags & IEEE80211_CHAN_5GHZ)
        ret |= APPLE80211_C_FLAG_5GHZ;
    ret |= (APPLE80211_C_FLAG_ACTIVE | APPLE80211_C_FLAG_20MHZ);
    return ret;
}
#endif

static int ieeeChanFlag2apple(int flags, int bw)
{
    int ret = 0;
    if (flags & IEEE80211_CHAN_2GHZ)
        ret |= APPLE80211_C_FLAG_2GHZ;
    if (flags & IEEE80211_CHAN_5GHZ)
        ret |= APPLE80211_C_FLAG_5GHZ;
    if (!(flags & IEEE80211_CHAN_PASSIVE))
        ret |= APPLE80211_C_FLAG_ACTIVE;
    if (flags & IEEE80211_CHAN_DFS)
        ret |= APPLE80211_C_FLAG_DFS;
    if (bw == -1) {
        if (flags & IEEE80211_CHAN_VHT) {
            if ((flags & IEEE80211_CHAN_VHT160) || (flags & IEEE80211_CHAN_VHT80_80))
                ret |= APPLE80211_C_FLAG_160MHZ;
            if (flags & IEEE80211_CHAN_VHT80)
                ret |= APPLE80211_C_FLAG_80MHZ;
        } else if ((flags & IEEE80211_CHAN_HT40) && (flags & IEEE80211_CHAN_HT)) {
            ret |= APPLE80211_C_FLAG_40MHZ;
            if (flags & IEEE80211_CHAN_HT40U)
                ret |= APPLE80211_C_FLAG_EXT_ABV;
        } else if (flags & IEEE80211_CHAN_HT20)
            ret |= APPLE80211_C_FLAG_20MHZ;
        else if ((flags & IEEE80211_CHAN_CCK) || (flags & IEEE80211_CHAN_OFDM))
            ret |= APPLE80211_C_FLAG_10MHZ;
    } else {
        switch (bw) {
            case IEEE80211_CHAN_WIDTH_80P80:
            case IEEE80211_CHAN_WIDTH_160:
                ret |= APPLE80211_C_FLAG_160MHZ;
                break;
            case IEEE80211_CHAN_WIDTH_80:
                ret |= APPLE80211_C_FLAG_80MHZ;
                break;
            case IEEE80211_CHAN_WIDTH_40:
                ret |= APPLE80211_C_FLAG_40MHZ;
                if (flags & IEEE80211_CHAN_HT40U)
                    ret |= APPLE80211_C_FLAG_EXT_ABV;
                break;
            case IEEE80211_CHAN_WIDTH_20:
                ret |= APPLE80211_C_FLAG_20MHZ;
                break;
            default:
                if (flags & IEEE80211_CHAN_HT20)
                    ret |= APPLE80211_C_FLAG_20MHZ;
                else if ((flags & IEEE80211_CHAN_CCK) || (flags & IEEE80211_CHAN_OFDM))
                    ret |= APPLE80211_C_FLAG_10MHZ;
                break;
        }
    }
    return ret;
}

IOReturn AirportItlwm::
getCHANNEL(OSObject *object,
                           struct apple80211_channel_data *cd)
{
    struct ieee80211com * ic = fHalService->get80211Controller();
    // Apple's IO80211 framework pre-zeroes channel data and returns success.
    // When not associated, channel=0 / flags=0.
    memset(cd, 0, sizeof(apple80211_channel_data));
    cd->version = APPLE80211_VERSION;
    cd->channel.version = APPLE80211_VERSION;
    if (ic->ic_state == IEEE80211_S_RUN) {
        cd->channel.channel = ieee80211_chan2ieee(ic, ic->ic_bss->ni_chan);
        cd->channel.flags = ieeeChanFlag2apple(ic->ic_bss->ni_chan->ic_flags, ic->ic_bss->ni_chw);
    }
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
setCHANNEL(OSObject *object, struct apple80211_channel_data *data)
{
    return kIOReturnError;
}

IOReturn AirportItlwm::
getPROTMODE(OSObject *object, struct apple80211_protmode_data *pd)
{
    (void)object;
    (void)pd;
    return static_cast<IOReturn>(
        TahoeAssociationContracts::kPublicProtmodeUnsupportedStatus);
}

IOReturn AirportItlwm::
setPROTMODE(OSObject *object, struct apple80211_protmode_data *pd)
{
    return kIOReturnError;
}

IOReturn AirportItlwm::
getTXPOWER(OSObject *object,
                           struct apple80211_txpower_data *txd)
{
    if (txd == NULL)
        return kIOReturnBadArgument;
    memset(txd, 0, sizeof(*txd));
    txd->version = APPLE80211_VERSION;
    txd->txpower_unit = APPLE80211_UNIT_MW;
    uint8_t raw = 0;
    if (getTahoeCachedQTxpowerRaw(fHalService, &raw))
        txd->txpower = static_cast<int32_t>(decodeAppleTahoeQTxpowerRaw(raw));
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
getTX_NSS(OSObject *object, struct apple80211_tx_nss_data *data)
{
    memset(data, 0, sizeof(*data));
    data->version = APPLE80211_VERSION;
    data->nss = fHalService->getDriverInfo()->getTxNSS();
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
getNSS(OSObject *object, struct apple80211_nss_data *data)
{
    memset(data, 0, sizeof(*data));
    data->version = APPLE80211_VERSION;
    data->nss = fHalService->getDriverInfo()->getTxNSS();
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
setTX_NSS(OSObject *object, struct apple80211_tx_nss_data *data)
{
    return kIOReturnError;
}

IOReturn AirportItlwm::
setROAM(OSObject *object, struct apple80211_sta_roam_data *data)
{
    return kIOReturnError;
}

IOReturn AirportItlwm::
getRATE(OSObject *object, struct apple80211_rate_data *rd)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    if (rd == nullptr)
        return kIOReturnBadArgument;
    // Legacy STA dispatch must match the same Apple not-associated contract as
    // the Tahoe Skywalk path: AppleBCMWLANCore::getRATE() returns 0xe0822403
    // until the current BSS is associated, not raw POSIX 6.
    if (ic->ic_bss == NULL || ic->ic_state != IEEE80211_S_RUN)
        return kApple80211ErrDriverNotAvailable;
    return getTahoeCurrentRateMbps(fHalService, &rd->rate[0]);
}

IOReturn AirportItlwm::
getROAM_PROFILE(OSObject *object, struct apple80211_roam_profile_band_data *data)
{
    if (roamProfile == NULL) {
        XYLog("%s no roam profile, return error\n", __FUNCTION__);
        return kIOReturnError;
    }
    memcpy(data, roamProfile, sizeof(struct apple80211_roam_profile_band_data));
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
setROAM_PROFILE(OSObject *object, struct apple80211_roam_profile_band_data *data)
{
#if 0
    for (int i = 0; i < data->profile_cnt; i++) {
        struct apple80211_roam_profile *bd = &data->profiles[i];
        XYLog("%s %d ROAM_PROF_BACKOFF_MULTIPLIER: %d, ROAM_PROF_FULLSCAN_PERIOD: %d, ROAM_PROF_INIT_SCAN_PERIOD: %d, ROAM_PROF_MAX_SCAN_PERIOD: %d, ROAM_PROF_NFSCAN: %d, ROAM_PROF_ROAM_DELTA: %d, ROAM_PROF_ROAM_FLAGS:%d, ROAM_PROF_ROAM_TRIGGER: %d, ROAM_PROF_RSSI_BOOST_DELTA: %d, ROAM_PROF_RSSI_BOOST_THRESH: %d, ROAM_PROF_RSSI_LOWER: %d\n", __FUNCTION__, i, bd->backoff_multiplier, bd->full_scan_period, bd->init_scan_period, bd->max_scan_period, bd->nfscan, bd->delta, bd->flags, bd->trigger, bd->rssi_boost_delta, bd->rssi_boost_thresh, bd->rssi_lower);
    }
#endif
    if (roamProfile != NULL)
        IOFree(roamProfile, sizeof(struct apple80211_roam_profile_band_data));
    roamProfile = (uint8_t *)IOMalloc(sizeof(struct apple80211_roam_profile_band_data));
    memcpy(roamProfile, data, sizeof(struct apple80211_roam_profile_band_data));
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
getBTCOEX_CONFIG(OSObject *object, struct apple80211_btc_config_data *data)
{
    if (!data)
        return kIOReturnError;
    memcpy(data, &btcConfig, sizeof(struct apple80211_btc_config_data));
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
setBTCOEX_CONFIG(OSObject *object, struct apple80211_btc_config_data *data)
{
    if (!data)
        return kIOReturnError;
    memcpy(&btcConfig, data, sizeof(struct apple80211_btc_config_data));
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
getBTCOEX_MODE(OSObject *object, struct apple80211_btc_mode_data *data)
{
    if (!data)
        return kIOReturnError;
    data->version = APPLE80211_VERSION;
    data->btc_mode = btcMode;
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
setBTCOEX_MODE(OSObject *object, struct apple80211_btc_mode_data *data)
{
    if (!data)
        return kIOReturnError;
    btcMode = data->btc_mode;
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
getBTCOEX_OPTIONS(OSObject *object, struct apple80211_btc_options_data *data)
{
    if (!data)
        return kIOReturnError;
    data->version = APPLE80211_VERSION;
    data->btc_options = btcOptions;
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
setBTCOEX_OPTIONS(OSObject *object, struct apple80211_btc_options_data *data)
{
    if (!data)
        return kIOReturnError;
    btcOptions = data->btc_options;
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
getBTCOEX_PROFILES(OSObject *object, struct apple80211_btc_profiles_data *data)
{
    if (!data || !btcProfile)
        return kIOReturnError;
    memcpy(data, btcProfile, sizeof(struct apple80211_btc_profiles_data));
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
setBTCOEX_PROFILES(OSObject *object, struct apple80211_btc_profiles_data *data)
{
    if (!data)
        return kIOReturnError;
    if (btcProfile)
        IOFree(btcProfile, sizeof(struct apple80211_btc_profiles_data));
    btcProfile = (struct apple80211_btc_profiles_data *)IOMalloc(sizeof(struct apple80211_btc_profiles_data));
    memcpy(btcProfile, data, sizeof(struct apple80211_btc_profiles_data));
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
getWOW_PARAMETERS(OSObject *object, struct apple80211_wow_parameter_data *data)
{
    return kIOReturnError;
}

IOReturn AirportItlwm::
setWOW_PARAMETERS(OSObject *object, struct apple80211_wow_parameter_data *data)
{
    return kIOReturnError;
}

IOReturn AirportItlwm::
getBSSID(OSObject *object,
                         struct apple80211_bssid_data *bd)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    // Apple's IO80211 framework pre-zeroes BSSID and returns success.
    // When not associated, BSSID is all-zero.
    memset(bd, 0, sizeof(*bd));
    bd->version = APPLE80211_VERSION;
    if (ic->ic_state == IEEE80211_S_RUN) {
        memcpy(bd->bssid.octet, ic->ic_bss->ni_bssid, APPLE80211_ADDR_LEN);
    }
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
setBSSID(OSObject *object, struct apple80211_bssid_data *data)
{
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
getCARD_CAPABILITIES(OSObject *object,
                                     struct apple80211_capability_data *cd)
{
#if __IO80211_TARGET >= __MAC_26_0
    static_assert(sizeof(struct apple80211_capability_data) == 0x1c,
                  "Tahoe apple80211_capability_data must be 0x1c bytes");
#endif
    memset(cd, 0, sizeof(struct apple80211_capability_data));

    cd->version = APPLE80211_VERSION;
    // Keep the legacy dispatcher shadow on the same Apple-consistent bitmap
    // as the Tahoe controller path, including the fixed cap[0..1] request
    // capability bytes that CoreWiFi consults before issuing current-link
    // property requests.
    TahoeCapabilityContracts::applyAppleConsistentCardCapabilityCluster(
        cd->capabilities);
    //cd->capabilities[8] = 0x40;
    //cd->capabilities[8] |= 8;//dfs white list
    //cd->capabilities[9] = 0x28;
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
getSTATE(OSObject *object,
                         struct apple80211_state_data *sd)
{
    memset(sd, 0, sizeof(*sd));
    sd->version = APPLE80211_VERSION;
    sd->state = fHalService->get80211Controller()->ic_state;
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
getMCS_INDEX_SET(OSObject *object, struct apple80211_mcs_index_set_data *ad)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    if (ad == NULL)
        return kIOReturnBadArgument;
    if (ic->ic_bss == NULL)
        return static_cast<IOReturn>(0xe0822403);

    memset(ad, 0, sizeof(*ad));
    ad->version = APPLE80211_VERSION;
    size_t size = min(ARRAY_SIZE(ic->ic_bss->ni_rxmcs), ARRAY_SIZE(ad->mcs_set_map));
    bool hasAnyMcsBit = false;
    for (size_t i = 0; i < size; i++) {
        ad->mcs_set_map[i] = ic->ic_bss->ni_rxmcs[i];
        hasAnyMcsBit |= ad->mcs_set_map[i] != 0;
    }
    return hasAnyMcsBit ? kIOReturnSuccess : static_cast<IOReturn>(0xe00002f0);
}

IOReturn AirportItlwm::
getVHT_MCS_INDEX_SET(OSObject *object, struct apple80211_vht_mcs_index_set_data *data)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    if (ic->ic_bss == NULL || ic->ic_curmode < IEEE80211_MODE_11AC) {
        return kIOReturnError;
    }
    memset(data, 0, sizeof(struct apple80211_vht_mcs_index_set_data));
    data->version = APPLE80211_VERSION;
    data->mcs_map = ic->ic_bss->ni_vht_mcsinfo.tx_mcs_map;
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
getMCS_VHT(OSObject *object, struct apple80211_mcs_vht_data *data)
{
    if (data == nullptr)
        return kIOReturnBadArgument;
    memset(data, 0, sizeof(struct apple80211_mcs_vht_data));
    data->version = APPLE80211_VERSION;
    return fillTahoeMcsVhtFromCachedNrate(fHalService, data);
}

IOReturn AirportItlwm::
setMCS_VHT(OSObject *object, struct apple80211_mcs_vht_data *data)
{
    return kIOReturnError;
}

IOReturn AirportItlwm::
getRATE_SET(OSObject *object, struct apple80211_rate_set_data *ad)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    // Apple delegates RATE_SET through IO80211BssManager::getCurrentRateSet():
    // no current BSS -> 0xe0822403, empty cached set -> 0xe00002f0. The
    // legacy dispatcher must keep the same split as the Tahoe Skywalk path.
    if (ic->ic_bss == NULL)
        return kApple80211ErrDriverNotAvailable;
    if (ic->ic_bss->ni_rates.rs_nrates == 0)
        return kApple80211ErrNoCachedValue;
    memset(ad, 0, sizeof(*ad));
    ad->version = APPLE80211_VERSION;
    ad->num_rates = ic->ic_bss->ni_rates.rs_nrates;
    size_t size = min(ic->ic_bss->ni_rates.rs_nrates, ARRAY_SIZE(ad->rates));
    for (size_t i = 0; i < size; i++) {
        ad->rates[i].version = APPLE80211_VERSION;
        ad->rates[i].rate = ic->ic_bss->ni_rates.rs_rates[i];
        ad->rates[i].flags = 0;
    }
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
getPHY_MODE(OSObject *object,
                            struct apple80211_phymode_data *pd)
{
    if (pd == nullptr)
        return kApple80211ErrInvalidArgumentRaw;

    struct ieee80211com *ic = fHalService->get80211Controller();

    if (!TahoePhyModeContracts::initializePhyModeCarrier(
            pd, tahoeSupportedPhyModeForController(ic)))
        return kApple80211ErrInvalidArgumentRaw;

    if (ic != nullptr && ic->ic_state == IEEE80211_S_RUN &&
        ic->ic_bss != nullptr) {
        TahoePhyModeContracts::publishAssociatedActiveMode(
            pd, tahoeActivePhyModeForBss(ic, ic->ic_bss));
    }
    
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
getOP_MODE(OSObject *object,
                           struct apple80211_opmode_data *od)
{
    if (!TahoeOpModeContracts::initializePrimaryCarrier(od))
        return static_cast<IOReturn>(TahoeOpModeContracts::kInvalidArgumentStatus);
    struct ieee80211com *ic = fHalService->get80211Controller();
    if (ic->ic_state == IEEE80211_S_RUN && ic->ic_bss != NULL)
        TahoeOpModeContracts::publishAssociatedBssMode(od, ic->ic_bss->ni_capinfo);
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
getRSSI(OSObject *object,
                        struct apple80211_rssi_data *rd)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    // Apple's BSS-manager-backed RSSI getter returns 0xe0822403 when there is
    // no current BSS. Keep the legacy STA path aligned with the same
    // producer/consumer contract rather than leaking raw 6.
    if (ic->ic_bss == NULL)
        return kApple80211ErrDriverNotAvailable;
    memset(rd, 0, sizeof(*rd));
    rd->version = APPLE80211_VERSION;
    rd->num_radios = 1;
    rd->rssi_unit = APPLE80211_UNIT_DBM;
    rd->rssi[0] = rd->aggregate_rssi
    = rd->rssi_ext[0]
    = rd->aggregate_rssi_ext
    = -(0 - IWM_MIN_DBM - ic->ic_bss->ni_rssi);
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
getRSN_IE(OSObject *object, struct apple80211_rsn_ie_data *data)
{
#ifdef USE_APPLE_SUPPLICANT
    struct ieee80211com *ic = fHalService->get80211Controller();
    if (ic->ic_bss == NULL || ic->ic_bss->ni_rsnie == NULL) {
        return kIOReturnError;
    }
    data->version = APPLE80211_VERSION;
    if (ic->ic_rsn_ie_override[1] > 0) {
        data->len = 2 + ic->ic_rsn_ie_override[1];
        memcpy(data->ie, ic->ic_rsn_ie_override, data->len);
    }
    else {
        data->len = 2 + ic->ic_bss->ni_rsnie[1];
        memcpy(data->ie, ic->ic_bss->ni_rsnie, data->len);
    }
    return kIOReturnSuccess;
#else
    return kIOReturnUnsupported;
#endif
}

IOReturn AirportItlwm::
setRSN_IE(OSObject *object, struct apple80211_rsn_ie_data *data)
{
    (void)object;
    (void)data;
    return static_cast<IOReturn>(
        TahoeAssociationContracts::kPublicSetRsnIeReturn);
}

IOReturn AirportItlwm::
getAP_IE_LIST(OSObject *object, struct apple80211_ap_ie_data *data)
{
#ifdef USE_APPLE_SUPPLICANT
    struct ieee80211com *ic = fHalService->get80211Controller();
    if (!data)
        return kIOReturnError;

    data->version = APPLE80211_VERSION;
    data->len = 0;

    if (ic->ic_bss == NULL || ic->ic_bss->ni_rsnie_tlv == NULL || ic->ic_bss->ni_rsnie_tlv_len == 0)
        return kIOReturnSuccess;
    if (ic->ic_bss->ni_rsnie_tlv_len > sizeof(data->ie_data))
        return kIOReturnError;

    data->len = ic->ic_bss->ni_rsnie_tlv_len;
    memcpy(data->ie_data, ic->ic_bss->ni_rsnie_tlv, data->len);
    return kIOReturnSuccess;
#else
    return kIOReturnUnsupported;
#endif
}

IOReturn AirportItlwm::
getNOISE(OSObject *object,
                         struct apple80211_noise_data *nd)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    if (nd == NULL)
        return kIOReturnBadArgument;
    if (ic->ic_bss == NULL)
        return static_cast<IOReturn>(0xe0822403);

    int32_t noise = fHalService->getDriverInfo()->getBSSNoise();
    if (noise == 0 || noise == -127)
        return 0x66;

    memset(nd, 0, sizeof(*nd));
    nd->version = APPLE80211_VERSION;
    nd->num_radios = 1;
    nd->noise[0] = nd->aggregate_noise = noise;
    nd->noise_unit = APPLE80211_UNIT_DBM;
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
getINT_MIT(OSObject *object, struct apple80211_intmit_data *imd)
{
    if (!imd)
        return kIOReturnError;
    imd->version = APPLE80211_VERSION;
    imd->int_mit = APPLE80211_INT_MIT_AUTO;
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
getPOWER(OSObject *object,
                         struct apple80211_power_data *pd)
{
    if (!pd)
        return kIOReturnError;
    pd->version = APPLE80211_VERSION;
    pd->num_radios = 4;
    pd->power_state[0] = power_state;
    pd->power_state[1] = power_state;
    pd->power_state[2] = power_state;
    pd->power_state[3] = power_state;
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
getPOWERSAVE(OSObject *object, struct apple80211_powersave_data *pd)
{
    pd->version = APPLE80211_VERSION;
    pd->powersave_level = APPLE80211_POWERSAVE_MODE_DISABLED;
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
setPOWER(OSObject *object,
                         struct apple80211_power_data *pd)
{
    if (!pd)
        return kIOReturnError;
    struct _ifnet *ifp = &fHalService->get80211Controller()->ic_ac.ac_if;
    bool isUp = (ifp->if_flags & IFF_UP) != 0;
    bool isRunning = (ifp->if_flags & IFF_RUNNING) != 0;
    if (pd->num_radios > 0) {
        uint32_t reqState = pd->power_state[0];
        // Guard: don't kill an adapter that is still initializing (IFF_UP but not yet IFF_RUNNING)
        if (reqState == kWiFiPowerOff && isUp && !isRunning) {
            return kIOReturnSuccess;
        }
        handlePowerStateChange(reqState, fNetIf);
    }

    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
setASSOCIATE(OSObject *object,
                             struct apple80211_assoc_data *ad)
{
    struct apple80211_authtype_data auth_type_data;
    struct ieee80211com *ic = fHalService->get80211Controller();

    if (!ad)
        return kIOReturnError;
    
    if (ic->ic_state < IEEE80211_S_SCAN)
        return kIOReturnSuccess;
    
    if (ic->ic_state == IEEE80211_S_ASSOC || ic->ic_state == IEEE80211_S_AUTH)
        return kIOReturnSuccess;

    if (ad->ad_mode != APPLE80211_AP_MODE_IBSS) {
        disassocIsVoluntary = false;
        auth_type_data.version = APPLE80211_VERSION;
        auth_type_data.authtype_upper = ad->ad_auth_upper;
        auth_type_data.authtype_lower = ad->ad_auth_lower;
        setAUTH_TYPE(object, &auth_type_data);
        const uint16_t rsnIeLen = static_cast<uint16_t>(ad->ad_rsn_ie[1] + 2);
        storeAssocRsnIeOverride(ic, ad->ad_rsn_ie, rsnIeLen);

        associateSSID(ad->ad_ssid, ad->ad_ssid_len, ad->ad_bssid, ad->ad_auth_lower, ad->ad_auth_upper, ad->ad_key.key, ad->ad_key.key_len, ad->ad_key.key_index);
    }
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
getASSOCIATE_RESULT(OSObject *object, struct apple80211_assoc_result_data *ad)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    if (ad && ic->ic_state == IEEE80211_S_RUN) {
        memset(ad, 0, sizeof(struct apple80211_assoc_result_data));
        ad->version = APPLE80211_VERSION;
        ad->result = APPLE80211_RESULT_SUCCESS;
        return kIOReturnSuccess;
    }
    return kIOReturnError;
}

IOReturn AirportItlwm::setDISASSOCIATE(OSObject *object)
{
    struct ieee80211com *ic = fHalService->get80211Controller();

    if (ic->ic_state < IEEE80211_S_SCAN)
        return kIOReturnSuccess;
    
    if (ic->ic_state > IEEE80211_S_AUTH && ic->ic_bss != NULL)
        IEEE80211_SEND_MGMT(ic, ic->ic_bss, IEEE80211_FC0_SUBTYPE_DEAUTH, IEEE80211_REASON_AUTH_LEAVE);
    
    if (ic->ic_state == IEEE80211_S_ASSOC || ic->ic_state == IEEE80211_S_AUTH)
        return kIOReturnSuccess;
    
    disassocIsVoluntary = true;

    ieee80211_del_ess(ic, nullptr, 0, 1);
    ieee80211_deselect_ess(ic);
#ifdef USE_APPLE_SUPPLICANT
    ic->ic_rsn_ie_override[1] = 0;
#endif
    ic->ic_assoc_status = APPLE80211_STATUS_UNAVAILABLE;
    ic->ic_deauth_reason = APPLE80211_REASON_ASSOC_LEAVING;
    ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
getSUPPORTED_CHANNELS(OSObject *object, struct apple80211_sup_channel_data *ad)
{
    if (!ad)
        return kIOReturnError;
    ad->version = APPLE80211_VERSION;
    ad->num_channels = 0;
    struct ieee80211com *ic = fHalService->get80211Controller();
    for (int i = 0; i < IEEE80211_CHAN_MAX; i++) {
        if (ic->ic_channels[i].ic_freq != 0) {
            ad->supported_channels[ad->num_channels].channel = ieee80211_chan2ieee(ic, &ic->ic_channels[i]);
#if __IO80211_TARGET < __MAC_13_0
            ad->supported_channels[ad->num_channels].flags = ieeeChanFlag2apple(ic->ic_channels[i].ic_flags, -1);
#else
            ad->supported_channels[ad->num_channels].flags = ieeeChanFlag2appleScanFlagVentura(ic->ic_channels[i].ic_flags);
#endif
            
            ad->num_channels++;
        }
    }
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
getLOCALE(OSObject *object,
                          struct apple80211_locale_data *ld)
{
    if (!ld)
        return kIOReturnError;
    ld->version = APPLE80211_VERSION;
    ld->locale  = APPLE80211_LOCALE_FCC;
    
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
getDEAUTH(OSObject *object,
                          struct apple80211_deauth_data *da)
{
    if (!da)
        return kIOReturnError;
    da->version = APPLE80211_VERSION;
    struct ieee80211com *ic = fHalService->get80211Controller();
    da->deauth_reason = ic->ic_deauth_reason;
//    XYLog("%s, %d\n", __FUNCTION__, da->deauth_reason);
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
getASSOCIATION_STATUS(OSObject *object, struct apple80211_assoc_status_data *hv)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    
    if (!hv)
        return kIOReturnError;
    memset(hv, 0, sizeof(*hv));
    hv->version = APPLE80211_VERSION;
    if (ic->ic_state == IEEE80211_S_RUN)
        hv->status = APPLE80211_STATUS_SUCCESS;
    else
        hv->status = APPLE80211_STATUS_UNAVAILABLE;
//    XYLog("%s, %d\n", __FUNCTION__, hv->status);
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
setSCANCACHE_CLEAR(OSObject *object, struct apple80211req *req)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    //if doing background or active scan, don't free nodes.
    if ((ic->ic_flags & IEEE80211_F_BGSCAN) || (ic->ic_flags & IEEE80211_F_ASCAN))
        return kIOReturnSuccess;
    ieee80211_free_allnodes(ic, 0);
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
setDEAUTH(OSObject *object,
                          struct apple80211_deauth_data *da)
{
    return kIOReturnSuccess;
}

void AirportItlwm::
eventHandler(struct ieee80211com *ic, int msgCode, void *data)
{
    IO80211Interface *interface = OSDynamicCast(IO80211Interface, ic->ic_ac.ac_if.iface);
    if (!interface)
        return;
    switch (msgCode) {
        case IEEE80211_EVT_COUNTRY_CODE_UPDATE:
            publishResolvedCountryCodeProperty(
                OSDynamicCast(AirportItlwm, ic->ic_ac.ac_if.controller));
            interface->postMessage(APPLE80211_M_COUNTRY_CODE_CHANGED);
            break;
        case IEEE80211_EVT_STA_ASSOC_DONE:
            publishResolvedCountryCodeProperty(
                OSDynamicCast(AirportItlwm, ic->ic_ac.ac_if.controller));
            interface->postMessage(APPLE80211_M_ASSOC_DONE);
            break;
        case IEEE80211_EVT_STA_DEAUTH:
            interface->postMessage(APPLE80211_M_DEAUTH_RECEIVED);
            break;
#if 0
        case IEEE80211_EVT_SCAN_DONE:
            interface->postMessage(APPLE80211_M_SCAN_DONE);
            break;
#endif
        default:
            break;
    }
}

IOReturn AirportItlwm::
getTX_ANTENNA(OSObject *object,
                              apple80211_antenna_data *ad)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    if (ic->ic_state != IEEE80211_S_RUN ||  ic->ic_bss == NULL || !ad)
        return kIOReturnError;
    ad->version = APPLE80211_VERSION;
    ad->num_radios = 1;
    ad->antenna_index[0] = 1;
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
getANTENNA_DIVERSITY(OSObject *object,
                                     apple80211_antenna_data *ad)
{
    if (!ad)
        return kIOReturnError;
    ad->version = APPLE80211_VERSION;
    ad->num_radios = 1;
    ad->antenna_index[0] = 1;
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
getDRIVER_VERSION(OSObject *object,
                                  struct apple80211_version_data *hv)
{
    if (!hv)
        return kIOReturnError;
    hv->version = APPLE80211_VERSION;
    snprintf(hv->string, sizeof(hv->string), "itlwm: %s%s fw: %s", ITLWM_VERSION, GIT_COMMIT, fHalService->getDriverInfo()->getFirmwareVersion());
    hv->string_len = strlen(hv->string);
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
getHARDWARE_VERSION(OSObject *object,
                                    struct apple80211_version_data *hv)
{
    if (!hv)
        return kIOReturnError;
    hv->version = APPLE80211_VERSION;
    strncpy(hv->string, fHalService->getDriverInfo()->getFirmwareVersion(), sizeof(hv->string));
    hv->string_len = strlen(fHalService->getDriverInfo()->getFirmwareVersion());
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
getCOUNTRY_CODE(OSObject *object,
                                struct apple80211_country_code_data *cd)
{
    char user_override_cc[3];
    const char *cc_fw = fHalService->getDriverInfo()->getFirmwareCountryCode();
    
    if (!cd)
        return kIOReturnError;
    cd->version = APPLE80211_VERSION;
    memset(user_override_cc, 0, sizeof(user_override_cc));
    PE_parse_boot_argn("itlwm_cc", user_override_cc, 3);
    /*
     * Apple keeps a real current-country state and airportd also derives
     * 802.11d country codes from scan cache. When firmware only exposes the
     * local "ZZ" fallback, prefer the associated BSS' 802.11d alpha2 carrier
     * before falling back to a geolocation value or the firmware placeholder.
     */
    AirportItlwmCountryCode::selectCountryCode(
        fHalService, user_override_cc, cc_fw, geo_location_cc, cd->cc);
    if (fNetIf != nullptr) {
        fNetIf->setProperty(APPLE80211_REGKEY_COUNTRY_CODE,
                            reinterpret_cast<const char *>(cd->cc));
        fNetIf->setProperty(
            APPLE80211_REGKEY_LOCALE,
            AirportItlwmCountryCode::localePropertyString(APPLE80211_LOCALE_FCC));
    }
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
setCOUNTRY_CODE(OSObject *object, struct apple80211_country_code_data *data)
{
    if (data && data->cc[0] != 120 && data->cc[0] != 88) {
        uint8_t normalizedCc[APPLE80211_MAX_CC_LEN];
        AirportItlwmCountryCode::copyAlpha2(
            normalizedCc, reinterpret_cast<const char *>(data->cc));
        memcpy(geo_location_cc, normalizedCc, sizeof(geo_location_cc));
        if (fNetIf != nullptr) {
            fNetIf->setProperty(APPLE80211_REGKEY_COUNTRY_CODE,
                                reinterpret_cast<const char *>(normalizedCc));
            fNetIf->setProperty(
                APPLE80211_REGKEY_LOCALE,
                AirportItlwmCountryCode::localePropertyString(APPLE80211_LOCALE_FCC));
        }
        fNetIf->postMessage(APPLE80211_M_COUNTRY_CODE_CHANGED);
    }
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
getMCS(OSObject *object, struct apple80211_mcs_data* md)
{
    if (md == NULL)
        return kIOReturnBadArgument;
    md->version = APPLE80211_VERSION;
    md->index = 0;

    uint32_t rate = 0;
    IOReturn ret = getTahoeCachedNrate(fHalService, &rate);
    if (ret == kIOReturnSuccess || ret == kApple80211ErrConfigNoValue) {
        uint32_t index = 0;
        if (decodeTahoeMcsIndexFromCachedNrate(rate, &index))
            md->index = index;
    }

    return ret;
}

IOReturn AirportItlwm::
getROAM_THRESH(OSObject *object, struct apple80211_roam_threshold_data* md)
{
    if (!md)
        return kIOReturnError;
    md->threshold = 100;
    md->count = 0;
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
getRADIO_INFO(OSObject *object, struct apple80211_radio_info_data* md)
{
    if (!md)
        return kIOReturnError;
    md->version = APPLE80211_VERSION;
    md->count = 1;
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
setSCAN_REQ(OSObject *object,
                            struct apple80211_scan_data *sd)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
#if 0
    XYLog("%s Type: %u BSS Type: %u PHY Mode: %u Dwell time: %u Rest time: %u Num channels: %u SSID: %s BSSID: %s\n",
          __FUNCTION__,
          sd->scan_type,
          sd->bss_type,
          sd->phy_mode,
          sd->dwell_time,
          sd->rest_time,
          sd->num_channels,
          sd->ssid,
          ether_sprintf(sd->bssid.octet));
#endif
    if (fScanResultWrapping)
        return 22;
    if (ic->ic_state <= IEEE80211_S_INIT)
        return 22;
    /* Reset iterator — see AirportItlwmSkywalkInterface::setSCAN_REQ */
    fNextNodeToSend = NULL;
    fScanResultWrapping = false;
    if (sd->scan_type == APPLE80211_SCAN_TYPE_FAST || sd->scan_type == APPLE80211_SCAN_TYPE_PASSIVE) {
        if (scanSource) {
            scanSource->setTimeoutMS(100);
            scanSource->enable();
        }
        return kIOReturnSuccess;
    }
    ieee80211_begin_cache_bgscan(&ic->ic_ac.ac_if);
    if (scanSource) {
        scanSource->setTimeoutMS(100);
        scanSource->enable();
    }
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
setSCAN_REQ_MULTIPLE(OSObject *object, struct apple80211_scan_multiple_data *sd)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
#if 0
    int i;
    XYLog("%s Type: %u SSID Count: %u BSSID Count: %u PHY Mode: %u Dwell time: %u Rest time: %u Num channels: %u Unk: %u\n",
          __FUNCTION__,
          sd->scan_type,
          sd->ssid_count,
          sd->bssid_count,
          sd->phy_mode,
          sd->dwell_time,
          sd->rest_time,
          sd->num_channels,
          sd->unk_2);
    for (i = 0; i < sd->ssid_count; i++)
        XYLog("%s index=%d ssid=%s ssid_len=%d\n", __FUNCTION__, i, sd->ssids[i].ssid_bytes, sd->ssids[i].ssid_len);
#endif
    if (fScanResultWrapping)
        return 22;
    if (ic->ic_state <= IEEE80211_S_INIT)
        return 22;
    ieee80211_begin_cache_bgscan(&ic->ic_ac.ac_if);
    if (scanSource) {
        scanSource->setTimeoutMS(100);
        scanSource->enable();
    }
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
getSCAN_RESULT(OSObject *object, struct apple80211_scan_result **sr)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    
    if (fNextNodeToSend == NULL) {
        if (fScanResultWrapping) {
            fScanResultWrapping = false;
            return 5;
        } else {
            fNextNodeToSend = RB_MIN(ieee80211_tree, &ic->ic_tree);
            if (fNextNodeToSend == NULL)
                return 12;
        }
    }
//    XYLog("%s ni_bssid=%s ni_essid=%s channel=%d flags=%d asr_cap=%d asr_nrates=%d asr_ssid_len=%d asr_ie_len=%d asr_rssi=%d\n", __FUNCTION__, ether_sprintf(fNextNodeToSend->ni_bssid), fNextNodeToSend->ni_essid, ieee80211_chan2ieee(ic, fNextNodeToSend->ni_chan), ieeeChanFlag2apple(fNextNodeToSend->ni_chan->ic_flags), fNextNodeToSend->ni_capinfo, fNextNodeToSend->ni_rates.rs_nrates, fNextNodeToSend->ni_esslen, fNextNodeToSend->ni_rsnie_tlv == NULL ? 0 : fNextNodeToSend->ni_rsnie_tlv_len, fNextNodeToSend->ni_rssi);
    apple80211_scan_result* result = (apple80211_scan_result* )fNextNodeToSend->verb;
    bzero(result, sizeof(*result));
    result->version = APPLE80211_VERSION;
    if (fNextNodeToSend->ni_rsnie_tlv && fNextNodeToSend->ni_rsnie_tlv_len > 0) {
#if __IO80211_TARGET < __MAC_12_0
        result->asr_ie_len = fNextNodeToSend->ni_rsnie_tlv_len;
        result->asr_ie_data = fNextNodeToSend->ni_rsnie_tlv;
#else
        result->asr_ie_len = MIN(fNextNodeToSend->ni_rsnie_tlv_len, sizeof(result->asr_ie_data));
        memcpy(result->asr_ie_data, fNextNodeToSend->ni_rsnie_tlv, result->asr_ie_len);
#endif
    } else {
        result->asr_ie_len = 0;
#if __IO80211_TARGET < __MAC_12_0
        result->asr_ie_data = NULL;
#endif
    }
    result->asr_beacon_int = fNextNodeToSend->ni_intval;
    result->asr_nrates = MIN((uint8_t)fNextNodeToSend->ni_rates.rs_nrates,
                             (uint8_t)APPLE80211_SCAN_RESULT_MAX_RATES);
    for (uint8_t i = 0; i < result->asr_nrates; i++)
        result->asr_rates[i] = fNextNodeToSend->ni_rates.rs_rates[i];
    result->asr_age = (uint32_t)(airport_up_time() - fNextNodeToSend->ni_age_ts);
#if __IO80211_TARGET >= __MAC_26_0
    memcpy(&result->asr_timestamp, fNextNodeToSend->ni_tstamp, sizeof(result->asr_timestamp));
#endif
    result->asr_cap = fNextNodeToSend->ni_capinfo;
    result->asr_channel.version = APPLE80211_VERSION;
    result->asr_channel.channel = ieee80211_chan2ieee(ic, fNextNodeToSend->ni_chan);
#if __IO80211_TARGET < __MAC_13_0
    result->asr_channel.flags = ieeeChanFlag2apple(fNextNodeToSend->ni_chan->ic_flags, -1);
#else
    result->asr_channel.flags = ieeeChanFlag2appleScanFlagVentura(fNextNodeToSend->ni_chan->ic_flags);
#endif
    result->asr_noise = fHalService->getDriverInfo()->getBSSNoise();
    result->asr_rssi = -(0 - IWM_MIN_DBM - fNextNodeToSend->ni_rssi);
    memcpy(result->asr_bssid, fNextNodeToSend->ni_bssid, IEEE80211_ADDR_LEN);
    result->asr_ssid_len = MIN(static_cast<uint8_t>(APPLE80211_MAX_SSID_LEN),
                               fNextNodeToSend->ni_esslen);
    if (result->asr_ssid_len != 0)
        memcpy(&result->asr_ssid, fNextNodeToSend->ni_essid, result->asr_ssid_len);

    *sr = result;
    
    fNextNodeToSend = RB_NEXT(ieee80211_tree, &ic->ic_tree, fNextNodeToSend);
    if (fNextNodeToSend == NULL)
        fScanResultWrapping = true;
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
setVIRTUAL_IF_CREATE(OSObject *object, struct apple80211_virt_if_create_data* data)
{
    // From Ventura, the virtual interface sequence has channged, now temporary disabled the virtual interface creation because it is no functionality. This fix the issue of delaying start time of associating to AP.
#if __IO80211_TARGET >= __MAC_13_0
    if (data == nullptr)
        return kIOReturnBadArgument;

    switch (data->role) {
        case 8:
        case 9:
        case 10:
            return static_cast<IOReturn>(0xe00002c7);
        case APPLE80211_VIF_AWDL:
            return static_cast<IOReturn>(0xe00002bd);
        case APPLE80211_VIF_SOFT_AP: {
            /*
             * V1 parity with the Tahoe Skywalk role-7 owner path:
             * allocate/validate the controller-owned APSTA owner,
             * attempt the lower AP/GO start gate, and tear the owner
             * back down on any lower failure. Role-7 success remains
             * impossible until a HAL backend advertises and starts
             * AP/GO mode.
             */
            AirportItlwmAPSTAOwner *owner = ensureAPSTAOwner(data);
            if (owner == nullptr) {
                return static_cast<IOReturn>(
                    kAirportItlwmAPSTARawInvalidArgumentReturn);
            }
            IOReturn lowerRet = owner->startLowerIfReady();
            if (lowerRet != kIOReturnSuccess) {
                deleteAPSTAOwner();
                return lowerRet;
            }
            return kIOReturnSuccess;
        }
        default:
            return static_cast<IOReturn>(0xe0000001);
    }
#else
    struct ether_addr addr;
    struct apple80211_channel chann;
    XYLog("%s role=%d, bsd_name=%s, mac=%s, unk1=%d\n", __FUNCTION__, data->role, data->bsd_name,
          ether_sprintf(data->mac), data->unk1);
    if (data->role == APPLE80211_VIF_P2P_DEVICE) {
        IO80211P2PInterface *inf = new IO80211P2PInterface;
        if (inf == NULL)
            return kIOReturnError;
        memcpy(addr.octet, data->mac, 6);
        inf->init(this, &addr, data->role, "p2p");
        fP2PDISCInterface = inf;
    } else if (data->role == APPLE80211_VIF_P2P_GO) {
        IO80211P2PInterface *inf = new IO80211P2PInterface;
        if (inf == NULL)
            return kIOReturnError;
        memcpy(addr.octet, data->mac, 6);
        inf->init(this, &addr, data->role, "p2p");
        fP2PGOInterface = inf;
    } else if (data->role == APPLE80211_VIF_AWDL) {
        if (fAWDLInterface != NULL && strncmp((const char *)data->bsd_name, "awdl", 4) == 0) {
            XYLog("%s awdl interface already exists!\n", __FUNCTION__);
            return kIOReturnSuccess;
        }
        IO80211P2PInterface *inf = new IO80211P2PInterface;
        if (inf == NULL)
            return kIOReturnError;
        memcpy(addr.octet, data->mac, 6);
        inf->init(this, &addr, data->role, "awdl");
        chann.channel = 149;
        chann.version = 1;
        chann.flags = APPLE80211_C_FLAG_5GHZ | APPLE80211_C_FLAG_ACTIVE | APPLE80211_C_FLAG_80MHZ;
        setInfraChannel(&chann);
        fAWDLInterface = inf;
    } else {
        XYLog("%s unhandled virtual interface role type: %d\n", __FUNCTION__, data->role);
        return kIOReturnError;
    }
    return kIOReturnSuccess;
#endif
}

IOReturn AirportItlwm::
setVIRTUAL_IF_DELETE(OSObject *object, struct apple80211_virt_if_delete_data *data)
{
#if __IO80211_TARGET >= __MAC_13_0
    /*
     * The Tahoe delete carrier has only a BSD name. Match it against
     * the controller-owned role-7 APSTA owner and otherwise fail
     * closed without allocating or publishing AP state.
     */
    if (data == nullptr)
        return kIOReturnBadArgument;
    return deleteAPSTAOwnerForBSDName(data->bsd_name);
#else
    //TODO find vif according to the bsd_name
    IO80211VirtualInterface *vif = OSDynamicCast(IO80211VirtualInterface, object);
    if (vif == NULL)
        return kIOReturnError;
    detachVirtualInterface(vif, false);
    vif->release();
    return kIOReturnSuccess;
#endif
}

/*
 * V1 controller mirror of the Tahoe-shape link-changed-event-data
 * publisher. The V1 IOCTL path must produce the same 32-byte
 * response shape as the V2 / Skywalk interface so the upper layer
 * reads voluntary, reason, SNR, and NF at the same offsets regardless
 * of which controller class is bound.
 */
IOReturn AirportItlwm::
getLINK_CHANGED_EVENT_DATA(OSObject *object, struct apple80211_link_changed_event_data *ed)
{
    if (ed == nullptr)
        return 16;

    struct ieee80211com *ic = fHalService->get80211Controller();

    bzero(ed, sizeof(apple80211_link_changed_event_data));
    ed->isLinkDown = !(currentStatus & kIONetworkLinkActive);
    if (ed->isLinkDown) {
        ed->voluntary_down = disassocIsVoluntary ? 1 : 0;
        ed->reason         = APPLE80211_LINK_DOWN_REASON_DEAUTH;
        if (ic != nullptr && ic->ic_bss != nullptr) {
            memcpy(ed->last_assoc, ic->ic_bss->ni_bssid, IEEE80211_ADDR_LEN);
        }
    } else {
        ed->voluntary_up = 1;
        if (ic != nullptr && ic->ic_bss != nullptr) {
            int32_t rssi = IWM_MIN_DBM + ic->ic_bss->ni_rssi;
            ed->rssi = (uint32_t)rssi;
            uint16_t snr = 0;
            uint16_t nf = 0;
            if (TahoeLqmContracts::buildLinkChangedSignalMetrics(
                    rssi, fHalService->getDriverInfo()->getBSSNoise(), &snr, &nf)) {
                ed->snr = snr;
                ed->nf = nf;
            }
        }
    }
    return kIOReturnSuccess;
}

/*
 * AP-mode HostAP selector wiring (Tahoe IO80211Family parity).
 *
 * The Apple BCM HostAP path stores
 * setSOFTAP_EXTENDED_CAPABILITIES_IE input bytes into a private
 * APSTA state region. The recovered body first clears qword +0x50,
 * qword +0x58 and word +0x60 (bytes 0x50..0x61), then writes the
 * input fields back at state offsets +0x50 (1 byte from input
 * +0x00), +0x51 (qword from input +0x01) and +0x59 (qword from
 * input +0x09). The qword writes at +0x51 and +0x59 are unaligned,
 * which is why the local mirror is a tightly packed 17-byte
 * struct. The selector returns success without firmware
 * interaction. setMIS_MAX_STA gates on the APSTA AP-up flag; when
 * AP is up it forwards the input dword +0x00 to a maxassoc
 * backend, ignores the helper result, and returns success. When AP
 * is down the body silently returns success.
 *
 * The local backend for the maxassoc admission limit is the
 * OpenBSD net80211 ic->ic_max_aid field, consumed by the existing
 * AID allocation loop in ieee80211_node_join() (rejects beyond
 * limit with IEEE80211_REASON_ASSOC_TOOMANY = 17). The AID/TIM
 * bitmap allocated at attach time covers IEEE80211_AID_DEF
 * entries; raising ic_max_aid above that capacity would overrun
 * ic_aid_bitmap and ic_tim_bitmap, so writes are clamped to
 * [1, IEEE80211_AID_DEF].
 *
 * Functional AP-mode operation requires separate iwx/iwm HAL work
 * (both currently panic on IEEE80211_M_HOSTAP). This wiring stops
 * at selector dispatch + APSTA state mirror + admission-limit
 * plumbing; AP firmware enablement is residual scope.
 */
bool AirportItlwm::isHostApRunning() const
{
    /*
     * HostAP mode is structurally unreachable on this driver:
     *   - The build defines IEEE80211_STA_ONLY, which hides
     *     IEEE80211_M_HOSTAP from the ieee80211_opmode enumeration
     *     (see itl80211/openbsd/net80211/ieee80211_var.h:259-268).
     *   - The iwx and iwm firmware MAC-context command paths
     *     panic on any opmode that is not STA or MONITOR
     *     (itlwm/hal_iwx/ItlIwx.cpp:8428 and
     *     itlwm/hal_iwm/mac80211.cpp:2019).
     * The recovered Apple contract for setMIS_MAX_STA reads
     * "if AP-up state is nonzero, ...; ignore helper result;
     * return success", so a structurally-false AP-up gate yields
     * the truthful no-backend-call return-success branch.
     */
    return false;
}

IOReturn AirportItlwm::setMaxAssoc(uint32_t value)
{
    if (fHalService == nullptr) {
        return kIOReturnNotReady;
    }
    struct ieee80211com *ic = fHalService->get80211Controller();
    if (ic == nullptr) {
        return kIOReturnNotReady;
    }
    /*
     * Clamp invariant: ic_aid_bitmap and ic_tim_bitmap are sized at
     * attach against ic_max_aid using IEEE80211_AID_DEF when the
     * field is unset (see ieee80211_node_attach in
     * itl80211/openbsd/net80211/ieee80211_node.c). Reducing the
     * limit below the previously allocated capacity is always safe
     * because the ieee80211_node_join admission loop only inspects
     * bitmap indices < ic_max_aid; raising the limit above the
     * allocated capacity is rejected here to preserve the
     * bitmap-size invariant.
     */
    if (value < 1) {
        value = 1;
    }
    if (value > IEEE80211_AID_DEF) {
        value = IEEE80211_AID_DEF;
    }
    ic->ic_max_aid = (uint16_t)value;
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
setSOFTAP_EXTENDED_CAPABILITIES_IE(OSObject *object,
    struct apple80211_softap_extended_capabilities_info *in)
{
    /*
     * Recovered Apple body clears state qword +0x50, qword +0x58
     * and word +0x60 (covering bytes 0x50..0x61), and then writes
     * input byte +0x00 to state +0x50, input qword +0x01 to state
     * +0x51 and input qword +0x09 to state +0x59. The qword writes
     * at +0x51 and +0x59 are unaligned inside the cleared region.
     *
     * The local mirror reuses the same packed wire-carrier type
     * (apple80211_softap_extended_capabilities_info), so its three
     * fields physically land at mirror offsets +0x00, +0x01 and
     * +0x09 — corresponding to state +0x50, +0x51 and +0x59 — and
     * the qword fields are unaligned inside the mirror exactly as
     * they are inside the cleared APSTA region. The compile-time
     * static_asserts on the carrier type enforce this layout.
     * Each scalar assignment below fully overwrites the prior value
     * at its mirror offset, which subsumes the recovered clear+write
     * sequence for that field. The single byte at state +0x61 is
     * cleared by the recovered body but is not written by either
     * the recovered body or this mirror; it lies past the end of the
     * 17-byte mirror, so the local representation does not need to
     * track it.
     */
    fAPSTASoftApExtCapsState.flag00  = in->flag00;
    fAPSTASoftApExtCapsState.value01 = in->value01;
    fAPSTASoftApExtCapsState.value09 = in->value09;
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
setMIS_MAX_STA(OSObject *object, struct apple80211_mis_max_sta *in)
{
    /*
     * Recovered Apple body: AP-up gate; when AP is operational
     * forward input dword +0x00 to setMaxAssoc and ignore its
     * result, otherwise the body is silently a no-op. The
     * dispatcher returns success unconditionally per the recovered
     * contract.
     */
    if (isHostApRunning()) {
        (void)setMaxAssoc(in->value00);
    }
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::setRSN_CONF(OSObject *object,
    struct apple80211_rsn_conf_data *in)
{
    (void)object;
    if (fAPSTAOwner == NULL) {
        return kIOReturnUnsupported;
    }
    return fAPSTAOwner->setRsnConf(in);
}
