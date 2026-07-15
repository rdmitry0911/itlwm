//
//  AirportItlwmSkywalkInterface.cpp
//  AirportItlwm-Sonoma
//
//  Created by qcwap on 2023/6/27.
//  Copyright © 2023 钟先耀. All rights reserved.
//
#include "AirportItlwmV2.hpp"
#include "AirportItlwmRegDiag.hpp"
#include "AirportItlwmSkywalkInterface.hpp"
#include "AirportItlwmAPSTAInterface.hpp"
#include "AirportItlwmAPSTAOwner.hpp"
#include "TahoeAssociationAuthContracts.hpp"
#include "TahoeBeaconIeBuilder.hpp"
#include "TahoeBssBlacklistContracts.hpp"
#include "TahoeBssManagerContracts.hpp"
#include "TahoeCapabilityContracts.hpp"
#include "TahoeLqmContracts.hpp"
#include "TahoeNrateContracts.hpp"
#include "TahoeOpModeContracts.hpp"
#include "TahoePhyModeContracts.hpp"
#include "TahoeQosDynsarContracts.hpp"
#include "TahoeScanContracts.hpp"
#include "TahoeSkywalkIoctlRoutes.hpp"
#include "TahoeTxRxChainContracts.hpp"
#include "Airport/IO80211BssManager.h"
#include <sys/CTimeout.hpp>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSMetaClass.h>
#include <crypto/sha1.h>
#include <net80211/ieee80211_node.h>
#include <net80211/ieee80211_ioctl.h>
#include <net80211/ieee80211_priv.h>

#define super IO80211InfraProtocol
OSDefineMetaClassAndStructors(AirportItlwmSkywalkInterface, IO80211InfraProtocol);

static_assert(TahoeAssociationAuthContracts::kAuthWpa2Psk ==
                  APPLE80211_AUTHTYPE_WPA2_PSK,
              "association auth contract must match Apple80211 WPA2 PSK bit");
static_assert(TahoeAssociationAuthContracts::kAuthWpa3Sae ==
                  APPLE80211_AUTHTYPE_WPA3_SAE,
              "association auth contract must match Apple80211 WPA3 SAE bit");

static constexpr UInt kApple80211LegacyGetIoctl = 3223873993U; // 0xc02869c9
static constexpr UInt kApple80211LegacySetIoctl = 2150132168U; // 0x802869c8

static bool isApple80211GetIoctl(UInt cmd)
{
    return cmd == SIOCGA80211 || cmd == kApple80211LegacyGetIoctl;
}

static bool isApple80211SetIoctl(UInt cmd)
{
    return cmd == SIOCSA80211 || cmd == kApple80211LegacySetIoctl;
}

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

static constexpr IOReturn kApple80211ErrDriverNotAvailable = 0xe0822403;
static constexpr IOReturn kApple80211ErrConfigNoValue =
    static_cast<IOReturn>(TahoeNrateContracts::kConfigNoValueStatus);
static constexpr IOReturn kApple80211ErrNoCachedValue = 0xe00002f0;
static constexpr IOReturn kApple80211ErrInvalidArgumentRaw = 0x16;
static constexpr uint32_t kIo80211InputProbeLimit = 64;
static constexpr int32_t kIo80211InputStageEntry = 1000;
static constexpr int32_t kIo80211InputStageReturn = 2000;
static constexpr uint32_t kExternalPmkWaitTimeoutMs = 3000;
static constexpr uint32_t kAppleBssManagerAssocRsnIeMaxLen = 0x101;
static constexpr uint16_t kAppleBssManagerAssociatedAuthTypeLen =
    static_cast<uint16_t>(sizeof(apple80211_authtype_data));
static constexpr uint32_t kAppleBssManagerBandInfoBitmapTable[4] = {
    0x1, 0x8, 0x2, 0x9
};
static_assert(sizeof(apple80211_authtype_data) ==
                  TahoeAssociationContracts::kAssociatedAuthTypePayloadLength,
              "associated auth type carrier length must match Apple ABI");
static_assert(__offsetof(apple80211_authtype_data, version) ==
                  TahoeAssociationContracts::kAssociatedAuthTypeVersionOffset &&
                  __offsetof(apple80211_authtype_data, authtype_lower) ==
                      TahoeAssociationContracts::kAssociatedAuthTypeLowerOffset &&
                  __offsetof(apple80211_authtype_data, authtype_upper) ==
                      TahoeAssociationContracts::kAssociatedAuthTypeUpperOffset,
              "associated auth type carrier offsets must match Apple ABI");

// Counter of successful APPLE80211_CIPHER_PMK installs through
// setCIPHER_KEY into ieee80211com::ic_psk. Atomic-relaxed so
// concurrent IOCTL paths do not lose increments.
extern "C" volatile uint64_t setCipherKey_pmk_install_count = 0;
// Counter of successful calls through the retained local setCUR_PMK helper.
// Current 25C56 public CUR_PMK SET returns before this helper, so this is not
// a public-current-wrapper counter; it remains for ABI/private callers.
extern "C" volatile uint64_t setCUR_PMK_pmk_install_count = 0;
// Counter of credential-safe external-PMK eligibility clears
// performed on reset/leave/disassoc/PMKSA-clear edges. Each clear
// zeroes ic_psk, drops IEEE80211_F_PSK, and logs only the reason
// tag so an observer can reconstruct the lifecycle without raw
// key material appearing in kernel logs.
extern "C" volatile uint64_t external_pmk_eligibility_clear_count = 0;

static volatile uint32_t sIo80211InputProbeCount = 0;

static uint16_t airportItlwmHostEtherType(const ether_header *eh)
{
    if (eh == nullptr)
        return 0;

    const uint16_t raw = eh->ether_type;
    return static_cast<uint16_t>((raw << 8) | (raw >> 8));
}

static bool isTahoeHiddenAssocCommand(int command)
{
    return TahoeAssociationContracts::isHiddenAssocCommand(command);
}

static uint32_t tahoeBssManagerBandInfoBitmap(uint32_t band)
{
    if (band == 0 ||
        band > sizeof(kAppleBssManagerBandInfoBitmapTable) /
            sizeof(kAppleBssManagerBandInfoBitmapTable[0]))
        return 0;

    return kAppleBssManagerBandInfoBitmapTable[band - 1];
}

static IO80211AuthContext
tahoeBssManagerAuthContext(
    const TahoeOwnerRegistry::AssociationOwner &associationOwner)
{
    IO80211AuthContext context{};
    context.authLower = associationOwner.authLower;
    context.authUpper = associationOwner.authUpper;
    context.authFlags = associationOwner.authFlags;
    context.bssInfoFlags = associationOwner.bssInfoFlags;
    return context;
}

static void
tahoeSeedBssManagerAuthContext(
    IO80211BssManager *bssManager,
    const TahoeOwnerRegistry::AssociationOwner &associationOwner)
{
    if (bssManager == nullptr || !associationOwner.hasCarrier)
        return;

    IO80211AuthContext context =
        tahoeBssManagerAuthContext(associationOwner);
    bssManager->setAuthContext(context);
}

static void
tahoeSeedBssManagerAssociatedAuthType(
    IO80211BssManager *bssManager,
    uint32_t authLower,
    uint32_t authUpper)
{
    if (bssManager == nullptr || (authLower == 0 && authUpper == 0))
        return;

    apple80211_authtype_data authType;
    memset(&authType, 0, sizeof(authType));
    authType.version = APPLE80211_VERSION;
    authType.authtype_lower = authLower;
    authType.authtype_upper = authUpper;
    bssManager->setAssociatedAuthType(
        reinterpret_cast<unsigned char *>(&authType),
        kAppleBssManagerAssociatedAuthTypeLen);
}

namespace {

static_assert(TahoeBssManagerContracts::kBeaconMetaDataSize ==
                  APPLE80211_WCL_BSS_INFO_HEADER_LEN,
              "BSS manager metadata must match the WCL 0x44 header");
static_assert(TahoeBssManagerContracts::kBeaconIeCapacity ==
                  APPLE80211_WCL_BSS_INFO_MAX_IE_LEN,
              "BSS manager IE capacity must match the WCL carrier");
static_assert(TahoeBssManagerContracts::kBeaconPayloadSize ==
                  APPLE80211_WCL_BSS_INFO_LEN,
              "BSS manager payload must match the WCL 0x844 output");

static uint16_t buildTahoeWclCurrentBssChanSpec(struct ieee80211com *ic,
                                                const struct ieee80211_channel *chan)
{
    if (ic == nullptr || chan == nullptr || chan == IEEE80211_CHAN_ANYC)
        return 0;

    const uint16_t primary = static_cast<uint16_t>(ieee80211_chan2ieee(ic, chan) & 0xff);
    if (primary == 0)
        return 0;

    if ((chan->ic_flags & IEEE80211_CHAN_5GHZ) != 0)
        return static_cast<uint16_t>(0xc000 | primary);
    return primary;
}

static bool buildTahoeCurrentBssPayload(
    ItlHalService *hal,
    TahoeBssManagerContracts::BeaconPayload *payload);

}

static constexpr uint64_t kAppleTahoeChipPowerDutyCycleFallback[6] = {
    0x07c808e50a010b1eULL,
    0x03560472058f06acULL,
    0x00000000011d0239ULL,
    0x00460050005a0064ULL,
    0x001e00280032003cULL,
    0x00000000000a0014ULL,
};

static IORegistryEntry *getTahoeChipPowerRegistryObject(AirportItlwm *controller)
{
    if (controller == nullptr)
        return nullptr;

    if (controller->fNetIf != nullptr)
        return controller->fNetIf;
    return controller;
}

static IORegistryEntry *getTahoeChipPowerRegistryProvider(AirportItlwm *controller)
{
    IORegistryEntry *entry = getTahoeChipPowerRegistryObject(controller);
    if (IOService *service = OSDynamicCast(IOService, entry)) {
        IORegistryEntry *provider = service->getProvider();
        if (provider != nullptr)
            return provider;
    }
    if (controller != nullptr)
        return controller->getProvider();
    return nullptr;
}

static bool copyTahoeChipPowerDutyCycle(IORegistryEntry *entry, uint64_t *table)
{
    if (entry == nullptr || table == nullptr)
        return false;

    OSObject *object = entry->copyProperty("wlan.chip.power.dutycycle");
    if (object == nullptr)
        return false;

    bool found = false;
    if (OSData *bytes = OSDynamicCast(OSData, object)) {
        if (bytes->getLength() == sizeof(kAppleTahoeChipPowerDutyCycleFallback)) {
            memcpy(table, bytes->getBytesNoCopy(),
                   sizeof(kAppleTahoeChipPowerDutyCycleFallback));
            found = true;
        }
    }

    object->release();
    return found;
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
    // Apple qtxpower stores a signed quarter-dBm byte. Intel exposes the
    // hardware-owned ceiling in half-dBm before BA notifications start feeding
    // actual reduced_txp values, so bootstrap from NVM instead of ic_txpower.
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
        // The iwx port does not surface the NVM txpower ceiling that exists on
        // the iwm branch. Apple seeds the query scratch byte before consulting
        // the qtxpower transport; keep the same deterministic bootstrap byte
        // until the first BA actual-txpower notification populates the cache.
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

static IOReturn fillTahoeGuardIntervalFromCachedNrate(ItlHalService *hal, apple80211_guard_interval_data *data)
{
    if (data == nullptr)
        return static_cast<IOReturn>(0xe00002c2);

    uint32_t rate = 0;
    IOReturn ret = getTahoeCachedNrate(hal, &rate);
    if (TahoeNrateContracts::isAcceptedQueryStatus(static_cast<uint32_t>(ret)))
        TahoeNrateContracts::decodeGuardIntervalFromNrate(rate, &data->interval);
    return ret;
}

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

static bool fillTahoeBssidChangedChannelFromCurrentBss(
    struct ieee80211com *ic, const uint8_t *bssid,
    apple80211_bssid_changed_event_data *eventData)
{
    if (ic == nullptr || bssid == nullptr || eventData == nullptr ||
        ic->ic_state != IEEE80211_S_RUN || ic->ic_bss == nullptr ||
        ic->ic_bss->ni_chan == nullptr ||
        ic->ic_bss->ni_chan == IEEE80211_CHAN_ANYC)
        return false;

    if (memcmp(bssid, ic->ic_bss->ni_bssid, IEEE80211_ADDR_LEN) != 0)
        return false;

    eventData->channel.version = APPLE80211_VERSION;
    eventData->channel.channel =
        ieee80211_chan2ieee(ic, ic->ic_bss->ni_chan);
    eventData->channel.flags =
        ieeeChanFlag2apple(ic->ic_bss->ni_chan->ic_flags,
                           ic->ic_bss->ni_chw);
    return eventData->channel.channel != 0;
}

static void postTahoeBssidChangedThroughInfraHelper(
    AirportItlwmSkywalkInterface *iface, AirportItlwm *controller,
    apple80211_bssid_changed_event_data *eventData, const char *source)
{
    (void)source;
    if (iface == nullptr || controller == nullptr ||
        controller->fNetIf == nullptr || eventData == nullptr)
        return;

    /*
     * IO80211InfraInterface::postMessage(msg=3) reaches bssidChange(data,len)
     * before the normal PostOffice delivery path. The local controller-level
     * postMessage transport intentionally bypasses that virtual infra helper,
     * so run the recovered side-effect hook explicitly for the same 24-byte
     * BSSID carrier before queuing the event.
     */
    iface->bssidChange(eventData, sizeof(*eventData));
    controller->postMessage(controller->fNetIf, APPLE80211_M_BSSID_CHANGED,
                            eventData, sizeof(*eventData), true);
}

#if __IO80211_TARGET >= __MAC_26_0
bool AirportItlwmSkywalkInterface::
publishTahoeBssidChangedFromCurrentBss(const char *source)
{
    struct ieee80211com *ic =
        fHalService ? fHalService->get80211Controller() : nullptr;
    if (instance == nullptr || instance->fNetIf == nullptr ||
        ic == nullptr || ic->ic_state != IEEE80211_S_RUN ||
        ic->ic_bss == nullptr)
        return false;

    uint8_t proposedBssid[IEEE80211_ADDR_LEN];
    memcpy(proposedBssid, ic->ic_bss->ni_bssid, IEEE80211_ADDR_LEN);
    const bool zeroBssidRejected =
        (proposedBssid[0] | proposedBssid[1] | proposedBssid[2] |
         proposedBssid[3] | proposedBssid[4] | proposedBssid[5]) == 0;
    if (zeroBssidRejected)
        return false;

    const bool sameBssAsLastPublished =
        fLastPublishedBssidValid &&
        memcmp(proposedBssid, fLastPublishedBssid, IEEE80211_ADDR_LEN) == 0;
    const uint32_t classifiedReason = sameBssAsLastPublished
        ? APPLE80211_BSSID_CHANGE_REASON_SAME_BSS
        : APPLE80211_BSSID_CHANGE_REASON_INITIAL;
    if (classifiedReason == APPLE80211_BSSID_CHANGE_REASON_SAME_BSS &&
        sameBssAsLastPublished)
        return false;

    apple80211_bssid_changed_event_data bd;
    bzero(&bd, sizeof(bd));
    memcpy(bd.bssid, proposedBssid, IEEE80211_ADDR_LEN);
    fillTahoeBssidChangedChannelFromCurrentBss(ic, proposedBssid, &bd);
    bd.reason = classifiedReason;
    postTahoeBssidChangedThroughInfraHelper(this, instance, &bd, source);
    memcpy(fLastPublishedBssid, proposedBssid, IEEE80211_ADDR_LEN);
    fLastPublishedBssidValid = true;
    return true;
}
#endif

struct apple80211_offload_arp_data
{
    uint32_t version;
    uint32_t has_ipv4_address;
    uint32_t ipv4_address;
    uint32_t keepalive_enabled;
    uint32_t gateway;
    uint16_t gateway_tail;
    uint16_t reserved;
} __attribute__((packed));
static_assert(sizeof(apple80211_offload_arp_data) == 0x18,
              "apple80211_offload_arp_data must preserve the recovered 0x18 Apple offsets");

struct tahoeWclRealTimeMode
{
    uint8_t enabled;
} __attribute__((packed));

struct tahoeWclQosParams
{
    uint32_t long_retry_limit;
    uint32_t rts_threshold;
    uint32_t lifetime_ac3;
    uint32_t lifetime_ac2;
    uint32_t powersave_mode;
    uint8_t  low_latency_policy;
    uint8_t  realtime_mlo;
    uint8_t  reserved16;
    uint8_t  flags;
} __attribute__((packed));
static_assert(sizeof(tahoeWclQosParams) == 0x18,
              "tahoeWclQosParams must match Apple dword fields + tail bytes");

struct tahoeDualPowerModeParams
{
    int32_t primary;
    int32_t secondary;
} __attribute__((packed));
static_assert(sizeof(tahoeDualPowerModeParams) == 0x8,
              "tahoeDualPowerModeParams must match two Apple mode dwords");

struct tahoeDynsarDetailRequest
{
    uint32_t version;
    uint32_t bank_selector;
    uint32_t entry_index;
    uint32_t header0;
    uint32_t header1;
    uint32_t reserved14;
    uint8_t payload[0x2d00];
} __attribute__((packed));
static_assert(sizeof(tahoeDynsarDetailRequest) == 0x2d18,
              "tahoeDynsarDetailRequest must retain the observed caller offsets");

struct tahoeSlowWifiFeatureEnabled
{
    uint32_t version;
    uint32_t enabled;
} __attribute__((packed));
static_assert(sizeof(tahoeSlowWifiFeatureEnabled) == 0x8,
              "tahoeSlowWifiFeatureEnabled must match the recovered Apple carrier");

struct tahoeLowLatencyInfo
{
    uint8_t enabled;
    uint8_t power_save;
    uint16_t window;
} __attribute__((packed));
static_assert(sizeof(tahoeLowLatencyInfo) == 0x4,
              "tahoeLowLatencyInfo must match the recovered Apple carrier");

struct tahoeUsbHostNotification
{
    uint32_t version;
    uint32_t sequence_number;
    uint32_t change;
    uint32_t present;
} __attribute__((packed));
static_assert(sizeof(tahoeUsbHostNotification) == 0x10,
              "tahoeUsbHostNotification must match the recovered Apple dword offsets");

struct apple80211_reassoc
{
    uint8_t reserved00[0x68];
    uint16_t channels[7];
    uint32_t channel_scores[7];
    uint32_t channel_count;
    uint32_t score_count;
    uint8_t feature_flags;
    int8_t roam_reason;
} __attribute__((packed));
static_assert(sizeof(apple80211_reassoc) == 0x9c,
              "apple80211_reassoc must cover the Apple offsets used by sendReassocCommand");

struct apple80211_pm_mode
{
    uint32_t version;
    uint32_t mode;
} __attribute__((packed));
static_assert(sizeof(apple80211_pm_mode) == 0x8,
              "apple80211_pm_mode must preserve the recovered version+mode layout");

struct scanHomeAndAwayTime
{
    uint32_t milliseconds;
} __attribute__((packed));
static_assert(sizeof(scanHomeAndAwayTime) == 0x4,
              "scanHomeAndAwayTime must match the single dword consumed by Apple");

struct apple80211_timesync_info
{
    char text[0x100];
} __attribute__((packed));
static_assert(sizeof(apple80211_timesync_info) == 0x100,
              "apple80211_timesync_info must preserve the recovered 0x100 text buffer");

struct apple80211_wcl_wnm_config_t
{
    uint8_t raw[0x338];
} __attribute__((packed));
static_assert(sizeof(apple80211_wcl_wnm_config_t) == 0x338,
              "apple80211_wcl_wnm_config_t must cover the recovered WnmAgent field range");

struct apple80211_wcl_wnm_offload_t
{
    uint8_t raw[0x30];
} __attribute__((packed));
static_assert(sizeof(apple80211_wcl_wnm_offload_t) == 0x30,
              "apple80211_wcl_wnm_offload_t must cover the recovered DMS offload field range");

static constexpr IOReturn kIOReturnBadArgumentTahoe = static_cast<IOReturn>(0xe00002bc);

void AirportItlwmSkywalkInterface::associateSSID(uint8_t *ssid, uint32_t ssid_len, const struct ether_addr &bssid, uint32_t authtype_lower, uint32_t authtype_upper, uint8_t *key, uint32_t key_len, int key_index, bool importLocalPmk, bool externalPmkOwner)
{
    struct ieee80211com *ic = fHalService->get80211Controller();

    // ieee80211_disable_rsn() zeroes ic->ic_psk along with the
    // RSN config fields. On the WCL/Skywalk externally-owned-PSK
    // path, Apple userspace delivers the PSK-derived PMK
    // separately via setCIPHER_KEY with
    // key_cipher_type=APPLE80211_CIPHER_PMK, and the delivery may
    // arrive either before or after setWCL_ASSOCIATE invokes this
    // function. The RSN-policy fields rebuilt below by
    // ieee80211_ioctl_setwpaparms do not require the disable_rsn
    // reset on that PSK path, so the reset is skipped only when
    // both (a) the caller signals that an external owner controls
    // the PMK and (b) the requested authentication type is a
    // PSK-AKM (WPA-PSK, WPA2-PSK, or SHA256-PSK). Open, WEP,
    // WPA-Enterprise, 8021X WCL associations, and the legacy
    // local-key path continue to call disable_rsn so their RSN
    // lifecycle is unchanged.
    const uint32_t external_psk_mask =
        APPLE80211_AUTHTYPE_WPA_PSK |
        APPLE80211_AUTHTYPE_WPA2_PSK |
        APPLE80211_AUTHTYPE_SHA256_PSK;
    bool external_psk_path = externalPmkOwner &&
        (authtype_upper & external_psk_mask) != 0;
    if (!external_psk_path) {
        // The local RSN-disable edge zeroes ic_psk and resets RSN
        // configuration. The recovered Apple contract treats this as
        // the lifecycle reset for the external-PMK eligibility, so
        // drop IEEE80211_F_PSK and emit a non-secret reset marker
        // before disable_rsn. Combining the helper-driven clear with
        // the disable_rsn implicit zeroing keeps the structural
        // clear count and the lifecycle reset observable on every
        // non-PSK association edge that reaches this branch.
        clearExternalPmkEligibilityLocked("associateSSID_disable_rsn");
        ieee80211_disable_rsn(ic);
    }
    ieee80211_disable_wep(ic);

    struct ieee80211_wpaparams     wpa;
    struct ieee80211_nwkey         nwkey;
    bzero(&wpa, sizeof(wpa));
    bzero(&nwkey, sizeof(nwkey));
    
    memset(ic->ic_des_essid, 0, IEEE80211_NWID_LEN);
    memcpy(ic->ic_des_essid, ssid, ssid_len);
    ic->ic_des_esslen = ssid_len;
    
    bool is_zero = true;
    for (int i = 0; i < IEEE80211_ADDR_LEN; i++)
    is_zero &= bssid.octet[i] == 0;
    
    if (!is_zero) {
        IEEE80211_ADDR_COPY(ic->ic_des_bssid, bssid.octet);
        ic->ic_flags |= IEEE80211_F_DESBSSID;
    }
    else {
        memset(ic->ic_des_bssid, 0, IEEE80211_ADDR_LEN);
        ic->ic_flags &= ~IEEE80211_F_DESBSSID;
    }

    const uint32_t localAuthUpper =
        TahoeAssociationAuthContracts::localAuthMaskWithoutFallbackRewrite(
            authtype_upper);
    if (TahoeAssociationAuthContracts::usesLocalWpaProtocol(localAuthUpper)) {
        wpa.i_protos = IEEE80211_WPA_PROTO_WPA1 | IEEE80211_WPA_PROTO_WPA2;
    }
    
    if (TahoeAssociationAuthContracts::usesLocalPskAkm(localAuthUpper)) {
        wpa.i_akms |= IEEE80211_WPA_AKM_PSK | IEEE80211_WPA_AKM_SHA256_PSK;
        wpa.i_enabled = 1;
        // The Tahoe Skywalk WCL_ASSOCIATE / IOC_ASSOCIATE carrier
        // may legitimately arrive with key == nullptr or key_len
        // smaller than the local PMK store (IEEE80211_PMK_LEN = 32)
        // because Apple delivers the PSK-derived PMK separately
        // through APPLE80211_IOC_CIPHER_KEY case APPLE80211_CIPHER_PMK,
        // APPLE80211_IOC_CIPHER_KEY case APPLE80211_CIPHER_MSK, or
        // APPLE80211_IOC_CUR_PMK, all of which converge on
        // installExternalPmkLocked(...) and on ieee80211com::ic_psk.
        // Treating the absent candidate key bytes as a local PMK
        // import and memcpy-ing zeroes into ic_psk would destroy any
        // PMK already installed by those carriers before the host
        // supplicant consumes its first 4-way M1, breaking the
        // invariant that the PMK byte store must be present in
        // ic_psk before the first 4-way M1 is consumed. Treat any
        // caller that requested a local import but supplied no
        // usable key bytes as the external-owner case so the prior
        // PMK install survives into the M1 consumer.
        // Active-owner selection for the PSK 4-way M1 consumer. The
        // public ASSOCIATE / ad_key path delivers a usable PMK byte
        // window through the caller key, so the local PAE owns the
        // handshake. The hidden WCL_ASSOCIATE path arrives with no
        // PMK bytes; in that case an Apple/user external supplicant
        // owns the first 4-way M1 and the local PAE must not derive
        // a PTK from a zero PMK.
        bool localImportHasKey =
            importLocalPmk && key != nullptr &&
            key_len >= sizeof(ic->ic_psk);
        if (localImportHasKey) {
#if __IO80211_TARGET >= __MAC_26_0
            // BEFORE installing the caller-supplied PMK on this
            // edge, invalidate any pending PLTI target so a
            // concurrent gated helper DeliverPMK cannot land
            // AFTER the caller install and overwrite ic_psk with a
            // System.keychain-derived PMK. The non-destructive
            // invalidate zeros only the pending generation under
            // the controller command gate; it does NOT touch
            // ic_psk / IEEE80211_F_PSK / ic_external_pmk_owner, so
            // the caller PMK installed immediately after survives
            // this lockout. Any gated DeliverPMK that managed to
            // complete BEFORE this invalidate wrote a helper PMK
            // into ic_psk; the immediately-following memcpy
            // overwrites that with the caller PMK, which is the
            // correct precedence for the public ad_key path.
            if (instance != nullptr) {
                instance->invalidatePendingAssocTargetOnly(
                    "associateSSID_owner_local");
            }
#endif
            memcpy(ic->ic_psk, key, sizeof(ic->ic_psk));
            ic->ic_flags |= IEEE80211_F_PSK;
            ic->ic_external_pmk_owner = 0;
        } else if (externalPmkOwner || importLocalPmk) {
            ic->ic_flags |= IEEE80211_F_PSK;
            ic->ic_external_pmk_owner = 1;
#if __IO80211_TARGET >= __MAC_26_0
            // External-supplicant PSK association edge: no caller PMK
            // bytes arrived (WCL_ASSOCIATE delivers the PMK out of
            // band, or the public path requested a local import but
            // supplied no usable key). This is the ONLY PSK branch
            // that needs the project-owned helper to derive and
            // deliver a PMK before first M1. Publish the
            // (ssid, bssid, authtype) target on the PLTI boundary
            // under the controller command gate; the helper's
            // WaitAssociationTarget call returns with the assigned
            // generation, derives the WPA2 PMK from the System
            // keychain entry, and calls DeliverPMK with the
            // generation echoed back. The kext sink validates the
            // echo under the same command gate as the install, so a
            // concurrent lifecycle reset cannot interleave between
            // the check and the ic_psk write.
            if (instance != nullptr) {
                uint64_t assocGeneration = instance->publishPendingAssocTarget(
                    ssid, ssid_len, bssid.octet,
                    authtype_lower, authtype_upper);
                if (assocGeneration != 0) {
                    uint32_t waitedMs = 0;
                    unsigned int pmkNonZero = 0;
                    uint32_t externalOwner = 0;
                    uint32_t sleepResult = 0;
                    bool inCommandGate = false;
                    bool pmkReady = instance->waitForExternalPmkReady(
                        assocGeneration, kExternalPmkWaitTimeoutMs,
                        &waitedMs, &pmkNonZero, &externalOwner,
                        &sleepResult, &inCommandGate);

                    if (!pmkReady) {
                        XYLog("external_pmk_wait TIMEOUT generation=%llu "
                              "waited_ms=%u ic_psk_nonzero_bytes=%u/%zu "
                              "external_owner=%u sleep_result=0x%x in_gate=%u\n",
                              (unsigned long long)assocGeneration,
                              waitedMs, pmkNonZero, sizeof(ic->ic_psk),
                              externalOwner, sleepResult,
                              inCommandGate ? 1U : 0U);
                    }
                }
            }
#endif
        } else {
#if __IO80211_TARGET >= __MAC_26_0
            // PSK auth selected but no PMK carrier of any kind was
            // identified on this edge. The earlier
            // ieee80211_disable_rsn (run at the top of associateSSID
            // when external_psk_path is false) already zeroed
            // ic_psk; we just need to lock the helper out of this
            // edge so a stale pending PLTI target from a prior
            // external-owner association cannot accept a
            // DeliverPMK. Non-destructive invalidate: leaves the
            // already-empty PMK state alone but rejects future
            // gated DeliverPMK calls.
            if (instance != nullptr) {
                instance->invalidatePendingAssocTargetOnly(
                    "associateSSID_owner_none");
            }
#endif
            ic->ic_external_pmk_owner = 0;
        }
        ieee80211_ioctl_setwpaparms(ic, &wpa);
    }
    if (TahoeAssociationAuthContracts::usesLocalEnterpriseAkm(localAuthUpper)) {
        wpa.i_akms |= IEEE80211_WPA_AKM_8021X | IEEE80211_WPA_AKM_SHA256_8021X;
        wpa.i_enabled = 1;
        ieee80211_ioctl_setwpaparms(ic, &wpa);
    }
    
    if (authtype_lower == APPLE80211_AUTHTYPE_SHARED) {
        XYLog("shared key authentication is not supported!\n");
        return;
    }
    
    if (authtype_upper == APPLE80211_AUTHTYPE_NONE && authtype_lower == APPLE80211_AUTHTYPE_OPEN) { // Open or WEP Open System
        if (key_len > 0) {
            nwkey.i_wepon = IEEE80211_NWKEY_WEP;
            nwkey.i_defkid = key_index + 1;
            nwkey.i_key[key_index].i_keylen = (int)key_len;
            nwkey.i_key[key_index].i_keydat = key;
            ieee80211_ioctl_setnwkeys(ic, &nwkey);
        }
    }
}

void AirportItlwmSkywalkInterface::setPTK(const u_int8_t *key, size_t key_len) {
    struct ieee80211com *ic = fHalService->get80211Controller();
    struct ieee80211_node    * ni = ic->ic_bss;
    struct ieee80211_key *k;
    int keylen;

    ni->ni_rsn_supp_state = RNSA_SUPP_PTKDONE;
    
    if (ni->ni_rsncipher != IEEE80211_CIPHER_USEGROUP) {
        u_int64_t prsc;
        
        /* check that key length matches that of pairwise cipher */
        keylen = ieee80211_cipher_keylen(ni->ni_rsncipher);
        if (key_len != keylen) {
            XYLog("PTK length mismatch. expected %d, got %zu\n", keylen, key_len);
            return;
        }
        prsc = /*(gtk == NULL) ? LE_READ_6(key->rsc) :*/ 0;
        
        /* map PTK to 802.11 key */
        k = &ni->ni_pairwise_key;
        memset(k, 0, sizeof(*k));
        k->k_cipher = ni->ni_rsncipher;
        k->k_rsc[0] = prsc;
        k->k_len = keylen;
        memcpy(k->k_key, key, k->k_len);
        /* install the PTK */
        if ((*ic->ic_set_key)(ic, ni, k) != 0) {
            XYLog("setting PTK failed\n");
            return;
        }
        ni->ni_flags &= ~IEEE80211_NODE_RSN_NEW_PTK;
        ni->ni_flags &= ~IEEE80211_NODE_TXRXPROT;
        ni->ni_flags |= IEEE80211_NODE_RXPROT;
    } else if (ni->ni_rsncipher != IEEE80211_CIPHER_USEGROUP)
        XYLog("%s: unexpected pairwise key update received from %s\n",
              ic->ic_if.if_xname, ether_sprintf(ni->ni_macaddr));
}

void AirportItlwmSkywalkInterface::setGTK(const u_int8_t *gtk, size_t key_len, u_int8_t kid, u_int8_t *rsc) {
    struct ieee80211com *ic = fHalService->get80211Controller();
    struct ieee80211_node    * ni = ic->ic_bss;
    struct ieee80211_key *k;
    int keylen;
    
    if (gtk != NULL) {
        /* check that key length matches that of group cipher */
        keylen = ieee80211_cipher_keylen(ni->ni_rsngroupcipher);
        if (key_len != keylen) {
            XYLog("GTK length mismatch. expected %d, got %zu\n", keylen, key_len);
            return;
        }
        /* map GTK to 802.11 key */
        k = &ic->ic_nw_keys[kid];
        if (k->k_cipher == IEEE80211_CIPHER_NONE || k->k_len != keylen || memcmp(k->k_key, gtk, keylen) != 0) {
            memset(k, 0, sizeof(*k));
            k->k_id = kid;    /* 0-3 */
            k->k_cipher = ni->ni_rsngroupcipher;
            k->k_flags = IEEE80211_KEY_GROUP;
            //if (gtk[6] & (1 << 2))
            //  k->k_flags |= IEEE80211_KEY_TX;
            k->k_rsc[0] = LE_READ_6(rsc);
            k->k_len = keylen;
            memcpy(k->k_key, gtk, k->k_len);
            /* install the GTK */
            if ((*ic->ic_set_key)(ic, ni, k) != 0) {
                XYLog("setting GTK failed\n");
                return;
            }
        }
    }
    
    if (true) {
        ni->ni_flags |= IEEE80211_NODE_TXRXPROT;
#ifndef IEEE80211_STA_ONLY
        if (ic->ic_opmode != IEEE80211_M_IBSS ||
            ++ni->ni_key_count == 2)
#endif
        {
            ni->ni_port_valid = 1;
            ieee80211_set_link_state(ic, LINK_STATE_UP);
            ni->ni_assoc_fail = 0;
            if (ic->ic_opmode == IEEE80211_M_STA)
                ic->ic_rsngroupcipher = ni->ni_rsngroupcipher;
        }
    }
}

void AirportItlwmSkywalkInterface::
free()
{
    RT_SET(22);
    sRT.skFreeStep = 1;

    // Panic timer — the previous crash (panic8) was in the destructor chain
    // triggered by super::free() → IO80211SkywalkInterface::free() → D0 → D1.
    // If it hangs again, dump full lifecycle state.
    thread_call_t skFreeTimer = thread_call_allocate(
        [](thread_call_param_t, thread_call_param_t) {
            if (!(sRT.rtMask & 0x800000))
                panic("SkywalkInterface::free hung  "
                      "skFreeStep=%u rtMask=0x%07x rt2=0x%04x rt3=0x%04x | "
                      "stopStep=%u freeStep=%u ss=%u | "
                      "ic=%d fl=0x%x pwr=%u link=0x%x | "
                      "evt=%u pm=%u(req=%u) wd=%u ioctl=%u(last=%d) "
                      "scanDone=%u ifType=0x%x "
                      "ls=%d lsCnt=%u scan=%u pmCnt=%u | "
                      "scanReq=%u assoc=%u scanRes=%u "
                      "icfl=0x%x esslen=%u nodes=%u mfail=0x%x | "
                      "fVars=%p bsdIf=%p enCnt=%u disCnt=%u enRet=0x%x | "
                      "bsdFl=0x%x bsdMtu=%u",
                      sRT.skFreeStep, sRT.rtMask, sRT.rtMask2, sRT.rtMask3,
                      sRT.stopStep, sRT.freeStep, sRT.startStep,
                      sRT.ic_state, sRT.if_flags, sRT.power_state,
                      sRT.linkStatus,
                      sRT.evtCount, sRT.postMsgCount, sRT.lastPmReq,
                      sRT.wdCount,
                      sRT.ioctlCount, sRT.lastIoctl,
                      sRT.scanDoneCount, sRT.ifType,
                      sRT.lastLinkState, sRT.linkSetCount,
                      sRT.scanCount, sRT.pmCount,
                      sRT.scanReqCount, sRT.assocCount, sRT.scanResCount,
                      sRT.ic_flags, sRT.ic_des_esslen,
                      sRT.nodeCount, sRT.matchFail,
                      (void *)(uintptr_t)sRT.fVarsPtr,
                      (void *)(uintptr_t)sRT.bsdIfPtr,
                      sRT.enableCnt, sRT.disableCnt, sRT.lastEnableRet,
                      sRT.bsdIfFlags, sRT.bsdIfMtu);
        }, NULL);
    uint64_t skFreeDeadline;
    clock_interval_to_deadline(30, kSecondScale, &skFreeDeadline);
    thread_call_enter_delayed(skFreeTimer, skFreeDeadline);

    // mExpansionData, mExpansionData2, and fVars->registrationInfo are
    // managed by the framework (allocated in initRegistrationInfo, freed
    // in IOSkywalkNetworkInterface::free / IOSkywalkEthernetInterface::free).
    // Manual IOFree was removed — it was freeing at wrong offsets when our
    // class size was 0x10 too small.  super::free() handles cleanup.
    sRT.skFreeStep = 2;
    instance = NULL;
    fHalService = NULL;
    scanSource = NULL;
    fNextNodeToSend = NULL;
    sRT.skFreeStep = 5;
    super::free();
    RT_SET(23);
    thread_call_cancel(skFreeTimer);
    thread_call_free(skFreeTimer);
}

#if __IO80211_TARGET >= __MAC_26_0
IOReturn AirportItlwmSkywalkInterface::
inputPacket(IO80211NetworkPacket *packet, packet_info_tag *tag,
            ether_header *eh, bool *accepted, bool arg)
{
    const uint32_t count = ++sIo80211InputProbeCount;
    const uint16_t etherType = airportItlwmHostEtherType(eh);
    const bool isEapol = etherType == ETHERTYPE_PAE;
    const bool shouldTrace = count <= kIo80211InputProbeLimit || isEapol;
    const uint64_t traceArg =
        (static_cast<uint64_t>(etherType) << 32) | static_cast<uint64_t>(count);

    if (shouldTrace) {
        airportItlwmRegDiagTrace(kAirportItlwmRegDiagTraceRx,
                                 kAirportItlwmRegDiagPathRx,
                                 kIOReturnSuccess,
                                 kIo80211InputStageEntry +
                                     (isEapol ? 1 : 0),
                                 traceArg,
                                 reinterpret_cast<uint64_t>(packet));
    }

    IOReturn ret = IO80211InfraInterface::inputPacket(packet, tag, eh,
                                                      accepted, arg);

    if (shouldTrace) {
        airportItlwmRegDiagTrace(kAirportItlwmRegDiagTraceRx,
                                 kAirportItlwmRegDiagPathRx,
                                 ret,
                                 kIo80211InputStageReturn +
                                     (isEapol ? 1 : 0),
                                 traceArg,
                                 reinterpret_cast<uint64_t>(packet));
    }

    return ret;
}

static IOSkywalkTxSubmissionQueue *
airportItlwmTxQueueForIndex(AirportItlwm *driver, unsigned char queueId)
{
    (void)queueId;
    if (driver == nullptr || driver->fTxQueue == nullptr)
        return nullptr;
    return driver->fTxQueue;
}

SInt64 AirportItlwmSkywalkInterface::
pendingPackets(unsigned char queueId)
{
    IOSkywalkTxSubmissionQueue *queue =
        airportItlwmTxQueueForIndex(instance, queueId);
    return queue != nullptr ? queue->getPacketCount() : 0;
}

SInt64 AirportItlwmSkywalkInterface::
packetSpace(unsigned char queueId)
{
    IOSkywalkTxSubmissionQueue *queue =
        airportItlwmTxQueueForIndex(instance, queueId);
    return queue != nullptr ? queue->getFreeSpace() : 0;
}

UInt64 AirportItlwmSkywalkInterface::
getTxQueueDepth(void)
{
    return instance != nullptr && instance->fTxQueue != nullptr
        ? instance->fSkywalkTxQueueDepth
        : 0;
}

UInt64 AirportItlwmSkywalkInterface::
getRxQueueCapacity(void)
{
    return instance != nullptr && instance->fRxQueue != nullptr
        ? instance->fSkywalkRxQueueCapacity
        : 0;
}

void *AirportItlwmSkywalkInterface::
getMultiCastQueue(void)
{
    return instance != nullptr ? instance->fMultiCastQueue : nullptr;
}

void *AirportItlwmSkywalkInterface::
getRxCompQueue(void)
{
    return instance != nullptr ? instance->fRxQueue : nullptr;
}

void *AirportItlwmSkywalkInterface::
getTxCompQueue(void)
{
    return instance != nullptr ? instance->fTxCompQueue : nullptr;
}

void *AirportItlwmSkywalkInterface::
getTxSubQueue(apple80211_wme_ac)
{
    return airportItlwmTxQueueForIndex(instance, 0);
}

void *AirportItlwmSkywalkInterface::
getTxPacketPool(void)
{
    return instance != nullptr ? instance->fTxPool : nullptr;
}

void *AirportItlwmSkywalkInterface::
getRxPacketPool(void)
{
    return instance != nullptr ? instance->fRxPool : nullptr;
}

int AirportItlwmSkywalkInterface::
getNumTxQueues(void)
{
    return instance != nullptr && instance->fTxQueue != nullptr ? 1 : 0;
}

void AirportItlwmSkywalkInterface::
enableDatapath(void)
{
    if (instance == nullptr)
        return;

    if (instance->fTxCompQueue)
        instance->fTxCompQueue->enable();
    if (instance->fRxQueue) {
        instance->fRxQueue->enable();
        instance->fRxQueue->requestEnqueue(nullptr, 0);
    }
    if (instance->fTxQueue)
        instance->fTxQueue->enable();
}

void AirportItlwmSkywalkInterface::
disableDatapath(void)
{
    if (instance == nullptr)
        return;

    if (instance->fTxQueue)
        instance->fTxQueue->disable();
    if (instance->fMultiCastQueue)
        instance->fMultiCastQueue->disable();
    if (instance->fRxQueue)
        instance->fRxQueue->disable();
    if (instance->fTxCompQueue)
        instance->fTxCompQueue->disable();
}
#endif

IOReturn AirportItlwmSkywalkInterface::
getCHANNELS_INFO(apple80211_channels_info *data)
{
    if (!data)
        return kIOReturnBadArgument;

    // Tahoe's UI path still queries legacy APPLE80211_IOC_CHANNELS_INFO.
    // Live 26.x IOC DEBUG traces showed slot [483] returning 0xe00002c7 from
    // this Skywalk vtable.  The older AirportVirtualIOCTL path already proved
    // that a simple channel inventory satisfies the family contract here, so
    // keep the Skywalk slot on that same ABI instead of shadowing it with
    // unsupported.
    struct ieee80211com *ic = fHalService ? fHalService->get80211Controller() : nullptr;
    if (!ic)
        return kIOReturnNotReady;

    memset(data, 0, sizeof(*data));
    data->version = APPLE80211_VERSION;
    for (int i = 0; i < IEEE80211_CHAN_MAX; i++) {
        struct ieee80211_channel *channel = &ic->ic_channels[i];
        if (channel->ic_freq == 0)
            continue;

        int slot = data->num_chan_specs;
        int chanNum = ieee80211_chan2ieee(ic, channel);
        data->chan_num[slot] = chanNum;
        data->support_80Mhz[slot] = IEEE80211_IS_CHAN_VHT80(channel);
        data->support_40Mhz[slot] =
            IEEE80211_IS_CHAN_HT40(channel) || IEEE80211_IS_CHAN_VHT40(channel);
        data->num_chan_specs++;
        if (data->num_chan_specs >= APPLE80211_MAX_CHANNELS)
            break;
    }
    return kIOReturnSuccess;
}

UInt64 AirportItlwmSkywalkInterface::createEventPipe(IO80211APIUserClient *client)
{
    return super::createEventPipe(client);
}

void *AirportItlwmSkywalkInterface::getController(void)
{
    // Apple Tahoe does not leave the inherited family slot as an opaque field
    // lookup. AppleBCMWLANSkywalkInterface::getController() returns the same
    // controller object that was bound earlier into interface-local state.
    // The local Tahoe bring-up also binds the controller during
    // bindController(...), but caches it only in `instance`, so expose that
    // same object through the family-visible slot instead of relying on an
    // unrelated inherited storage path.
    return instance;
}

bool AirportItlwmSkywalkInterface::isCommandProhibited(int command)
{
    // Only the hidden association carriers are proven owners for this gate.
    // Public current-link requests are already routed through the recovered
    // BSD Apple80211 dispatcher; admitting them here returns the family helper
    // boolean as a raw Apple80211 status for callers such as CoreWLAN GET CHANNEL.
    if (isTahoeHiddenAssocCommand(command))
        return true;

    return super::isCommandProhibited(command);
}

IOReturn AirportItlwmSkywalkInterface::
processBSDCommand(ifnet_t interface, UInt cmd, void *data)
{
    // Tahoe removed a large block of legacy Apple80211 GET/SET methods from the
    // IO80211InfraProtocol vtable. Our driver still implements those semantics
    // as helper methods below, but without an explicit BSD-command bridge they
    // never get called on 26.x.
    //
    // Evidence kept here so the reason for this bridge is not lost:
    // - airportd repeatedly queried APPLE80211_IOC_SSID and received
    //   0xe0822403 / "driver not available", aborting auto-join.
    // - docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/89_remaining_gaps_closed_checked.yaml
    //   shows Tahoe first tries WCL and then expects driver fallback behavior
    //   matching IO80211Controller cache semantics (success + zeroed data).
    // - Our Tahoe target does not build AirportSTAIOCTL.cpp, so the old ioctl
    //   dispatcher never runs here; the BSD path must forward these requests.
    if ((isApple80211GetIoctl(cmd) || isApple80211SetIoctl(cmd)) &&
        data != NULL) {
        apple80211req *req = static_cast<apple80211req *>(data);
        if (req->req_type == TahoeBssBlacklistContracts::kSelector) {
            const uint32_t routeStatus =
                TahoeBssBlacklistContracts::routePreflightStatus(
                    interface != NULL, req->req_len, req->req_data);
            if (routeStatus != TahoeBssBlacklistContracts::kSuccessStatus)
                return static_cast<IOReturn>(routeStatus);
        }
        UInt normalizedCmd =
            isApple80211GetIoctl(cmd) ? SIOCGA80211 : SIOCSA80211;
        IOReturn ret = processApple80211Ioctl(normalizedCmd, req);
        if (ret != kIOReturnUnsupported)
            return ret;
    }
    return super::processBSDCommand(interface, cmd, data);
}

IOReturn AirportItlwmSkywalkInterface::
processApple80211Ioctl(UInt cmd, apple80211req *req)
{
    static const IOReturn kApple80211NotVirtualInterface =
        static_cast<IOReturn>(0xe082280e);
    static const IOReturn kApple80211ClassOwnerAbsent =
        static_cast<IOReturn>(0xe082280e);
    static const IOReturn kApple80211RawEnxio =
        static_cast<IOReturn>(6);
    static const IOReturn kApple80211RawInvalidArgument =
        static_cast<IOReturn>(0x16);

    if (req == NULL)
        return kIOReturnUnsupported;

    switch (req->req_type) {
        case APPLE80211_IOC_VIRTUAL_IF_ROLE:
            /*
             * Current 25C56 public GET and SET ROLE/PARENT wrappers are
             * unread fixed 0xe082280e leaves. Keep the null and unknown
             * command fallbacks below, but do not synthesize a normal Tahoe
             * carrier result for a non-null public request.
             */
#if __IO80211_TARGET >= __MAC_26_0
            if (req->req_data != NULL &&
                (cmd == SIOCGA80211 || cmd == SIOCSA80211))
                return static_cast<IOReturn>(0xe082280e);
#endif // __IO80211_TARGET >= __MAC_26_0
            if (cmd != SIOCGA80211)
                return kIOReturnUnsupported;
            if (req->req_data == NULL)
                return kApple80211NotVirtualInterface;
            if (req->req_len != sizeof(uint32_t))
                return kApple80211RawInvalidArgument;
            *(uint32_t *)req->req_data =
                static_cast<uint32_t>(getInterfaceRole());
            return kIOReturnSuccess;
        case APPLE80211_IOC_VIRTUAL_IF_PARENT: {
#if __IO80211_TARGET >= __MAC_26_0
            if (req->req_data != NULL &&
                (cmd == SIOCGA80211 || cmd == SIOCSA80211))
                return static_cast<IOReturn>(0xe082280e);
#endif // __IO80211_TARGET >= __MAC_26_0
            if (cmd != SIOCGA80211)
                return kIOReturnUnsupported;
            if (req->req_data == NULL)
                return kApple80211NotVirtualInterface;

            IO80211SkywalkInterface *parent =
                (instance != NULL) ? instance->getPrimarySkywalkInterface()
                                   : NULL;
            if (parent == NULL)
                return kApple80211RawEnxio;

            const char *bsdName = parent->getBSDName();
            if (bsdName == NULL)
                return kApple80211RawEnxio;

            const size_t bsdNameLen = strlen(bsdName);
            if (req->req_len < bsdNameLen)
                return kApple80211RawInvalidArgument;

            memmove(req->req_data, bsdName, bsdNameLen);
            return kIOReturnSuccess;
        }
        case APPLE80211_IOC_STATE:
            if (cmd == SIOCGA80211) {
                struct ieee80211com *ic =
                    fHalService ? fHalService->get80211Controller() : NULL;
                uint32_t state =
                    (ic != NULL) ? ic->ic_state : IEEE80211_S_INIT;
                req->req_val = state;
                if (req->req_data == NULL)
                    return kIOReturnSuccess;
                if (req->req_len == sizeof(state)) {
                    *(uint32_t *)req->req_data = state;
                    return kIOReturnSuccess;
                }
            }
            break;
        default:
            break;
    }

    if (req->req_data == NULL)
        return kIOReturnUnsupported;

    // Tahoe architectural gap fixed here:
    // the Skywalk-only BSD bridge originally forwarded just a small hand-picked
    // subset of Apple80211 IOCTLs.  That diverged from the legacy STA IOCTL
    // plane, where APPLE80211_IOC_POWERSAVE and several other state-carrier /
    // bring-up requests were already wired to real handlers.
    //
    // Live proof on 4973c4d:
    // - WCL logged `setPOWERSAVE ... not supported` during early init even
    //   though this class already implemented setPOWERSAVE/getPOWERSAVE.
    // - The failure was not inside the handler.  The BSD bridge simply never
    //   routed APPLE80211_IOC_POWERSAVE to it, so Tahoe fell back to
    //   unsupported before the recovered handler could run.
    //
    // Restore those routes here instead of papering over each symptom higher
    // up.  This keeps the Tahoe Skywalk path architecturally aligned with the
    // legacy IOCTL plane: if a real handler exists in AirportItlwm /
    // AirportItlwmSkywalkInterface, the BSD bridge must make it reachable.
    switch (req->req_type) {
        case APPLE80211_IOC_SSID:
            // Live Tahoe runtime on build db546d2 disproves the earlier
            // "always preserve IOUC-first routing here" theory for the
            // bootstrap path. airportd reaches _initInterface only after the
            // interface is bound, then immediately issues external GET SSID.
            // The active IOUC/WCL route on our port returns 0xe0822403
            // ("not associated") instead of the framework cache semantics,
            // and _initInterface aborts right there:
            //   APPLE80211_IOC_SSID -> 0xe0822403
            //   _initInterface: Failed to query current SSID
            //
            // Reverse docs for 25D125 are explicit that third-party ports must
            // replicate IO80211Controller cache behavior here: pre-zero the
            // public SSID carrier and return success before association. Apple
            // can rely on its internal cache layer to absorb the low-level
            // 0xe0822403; our Tahoe bridge cannot leak that low-level status
            // to airportd during bootstrap.
            if (cmd == SIOCGA80211) {
                if (req->req_len == APPLE80211_MAX_SSID_LEN) {
                    uint32_t returnedLength = req->req_len;
                    IOReturn ret = getTahoeCompactSSID(
                        req->req_data, req->req_len, &returnedLength);
                    if (ret == kIOReturnSuccess)
                        req->req_len = returnedLength;
                    return ret;
                }
                if (req->req_len != 0 &&
                    req->req_len < sizeof(apple80211_ssid_data))
                    return kIOReturnBadArgument;
                if (instance != NULL && instance->fAPSTAOwner != NULL) {
                    return instance->getAPSTA_SSID(
                        this,
                        (AirportItlwmAPSTASsidDataLayout *)req->req_data);
                }
                return getSSID((apple80211_ssid_data *)req->req_data);
            }
            return (instance != NULL)
                       ? instance->setAPSTA_SSID(this, (apple80211_ssid_data *)req->req_data)
                       : kIOReturnNotReady;
        case APPLE80211_IOC_BSSID:
#if __IO80211_TARGET >= __MAC_26_0
            /*
             * The current public SET wrapper is an unread fixed 0xe082280e
             * leaf. Preserve the bootstrap-oriented GET producer below.
             */
            if (cmd == SIOCSA80211)
                return static_cast<IOReturn>(0xe082280e);
#endif // __IO80211_TARGET >= __MAC_26_0
            // Same bootstrap contract as SSID: airportd queries BSSID during
            // _initInterface and expects success with an all-zero BSSID before
            // association. Letting the active WCL path leak 0xe0822403 keeps
            // the interface in "driver not available" state even though the
            // interface itself is already attached as Wi-Fi en0.
            if (cmd == SIOCGA80211) {
                if (req->req_len == APPLE80211_ADDR_LEN)
                    return getTahoeCompactBSSID(req->req_data, req->req_len);
                if (req->req_len != 0 &&
                    req->req_len < sizeof(apple80211_bssid_data))
                    return kIOReturnBadArgument;
                return getBSSID((apple80211_bssid_data *)req->req_data);
            }
            return kIOReturnUnsupported;
        case APPLE80211_IOC_SCAN_RESULT:
            return (cmd == SIOCGA80211)
                       ? getSCAN_RESULT((struct apple80211_scan_result *)req->req_data)
                       : kIOReturnUnsupported;
        case APPLE80211_IOC_CHANNEL:
            if (cmd == SIOCGA80211) {
                // Same pre-association cache contract as SSID/BSSID. Reverse
                // docs show airportd queries CHANNEL during _initInterface and
                // expects success with zeroed channel data before association.
                // Returning the low-level -3903/driver-not-available shape
                // here aborts the bootstrap bind just as surely as SSID.
                return getCHANNEL((apple80211_channel_data *)req->req_data);
            }
            if (cmd == SIOCSA80211)
                return (instance != NULL)
                    ? instance->setAPSTA_CHANNEL(this, (apple80211_channel_data *)req->req_data)
                    : kIOReturnNotReady;
            return kIOReturnUnsupported;
        case APPLE80211_IOC_AUTH_TYPE:
            if (cmd == SIOCGA80211)
                return getAUTH_TYPE((apple80211_authtype_data *)req->req_data);
            if (cmd == SIOCSA80211) {
#if __IO80211_TARGET >= __MAC_26_0
                // Current 25C56 public SET wrapper is an unread fixed nonzero
                // stub. Keep the local helper below for association-internal
                // auth-context seeding; this public non-null carrier must not
                // acknowledge a state update the reference wrapper never makes.
                return static_cast<IOReturn>(0xe082280e);
#else
                return setAUTH_TYPE((apple80211_authtype_data *)req->req_data);
#endif // __IO80211_TARGET >= __MAC_26_0
            }
            return kIOReturnUnsupported;
        case APPLE80211_IOC_PROTMODE:
            /*
             * The current 25C56 public GET and SET wrappers are unread
             * fixed 0xe082280e leaves. Do not expose the historical
             * carrier helper on the normal Tahoe public BSD route.
             */
#if __IO80211_TARGET >= __MAC_26_0
            if (cmd == SIOCGA80211 || cmd == SIOCSA80211)
                return static_cast<IOReturn>(0xe082280e);
#endif // __IO80211_TARGET >= __MAC_26_0
            return kIOReturnUnsupported;
        case APPLE80211_IOC_HOST_AP_MODE:
#if __IO80211_TARGET >= __MAC_26_0
            /*
             * The current public GET wrapper is an unread fixed 0xe082280e
             * leaf. Keep the APSTA instance guard and SET producer below
             * for their separate directions and pre-26 behavior.
             */
            if (cmd == SIOCGA80211)
                return static_cast<IOReturn>(0xe082280e);
#endif // __IO80211_TARGET >= __MAC_26_0
            if (instance == NULL)
                return kIOReturnNotReady;
            return (cmd == SIOCSA80211)
                ? instance->setHOST_AP_MODE(
                    this,
                    (AirportItlwmAPSTAHostApModeNetworkDataLayout *)req->req_data)
                : kIOReturnUnsupported;
        case APPLE80211_IOC_AP_MODE:
#if __IO80211_TARGET >= __MAC_26_0
            /*
             * The current public GET wrapper is an unread fixed 0xe082280e
             * leaf. Preserve the separate AP-mode SET failure producer.
             */
            if (cmd == SIOCGA80211)
                return static_cast<IOReturn>(0xe082280e);
#endif // __IO80211_TARGET >= __MAC_26_0
            return (cmd == SIOCSA80211) ? setAP_MODE((apple80211_apmode_data *)req->req_data)
                                        : kIOReturnUnsupported;
#if __IO80211_TARGET >= __MAC_26_0
        case APPLE80211_IOC_COUNTERMEASURES:
            /*
             * The current public GET wrapper is an unread fixed 0xe082280e
             * leaf. No local COUNTERMEASURES carrier contract is inferred.
             */
            if (cmd == SIOCGA80211)
                return static_cast<IOReturn>(0xe082280e);
            return kIOReturnUnsupported;
        case APPLE80211_IOC_FRAG_THRESHOLD:
            /*
             * The current public GET wrapper is an unread fixed 0xe082280e
             * leaf. Do not activate the historical FRAG_THRESHOLD carrier
             * declaration without evidence of a public carrier contract.
             */
            if (cmd == SIOCGA80211)
                return static_cast<IOReturn>(0xe082280e);
            return kIOReturnUnsupported;
        case APPLE80211_IOC_SHORT_SLOT:
            /*
             * The current public GET wrapper is an unread fixed 0xe082280e
             * leaf. Do not activate the historical SHORT_SLOT carrier
             * declaration without evidence of a public carrier contract.
             */
            if (cmd == SIOCGA80211)
                return static_cast<IOReturn>(0xe082280e);
            return kIOReturnUnsupported;
        case APPLE80211_IOC_MULTICAST_RATE:
            /*
             * The current public GET wrapper is an unread fixed 0xe082280e
             * leaf. No local MULTICAST_RATE carrier contract is inferred.
             */
            if (cmd == SIOCGA80211)
                return static_cast<IOReturn>(0xe082280e);
            return kIOReturnUnsupported;
        case APPLE80211_IOC_SHORT_RETRY_LIMIT:
            /*
             * The current public GET wrapper is an unread fixed 0xe082280e
             * leaf. No local SHORT_RETRY_LIMIT carrier contract is inferred.
             */
            if (cmd == SIOCGA80211)
                return static_cast<IOReturn>(0xe082280e);
            return kIOReturnUnsupported;
        case APPLE80211_IOC_LONG_RETRY_LIMIT:
            /*
             * The current public GET wrapper is an unread fixed 0xe082280e
             * leaf. No local LONG_RETRY_LIMIT carrier contract is inferred.
             */
            if (cmd == SIOCGA80211)
                return static_cast<IOReturn>(0xe082280e);
            return kIOReturnUnsupported;
#endif // __IO80211_TARGET >= __MAC_26_0
        case APPLE80211_IOC_POWER:
            if (instance == NULL)
                return kIOReturnNotReady;
            if (cmd == SIOCGA80211)
                return instance->getPOWER(this, (apple80211_power_data *)req->req_data);
            if (cmd == SIOCSA80211)
                return instance->setPOWER(this, (apple80211_power_data *)req->req_data);
            return kIOReturnUnsupported;
        case APPLE80211_IOC_POWERSAVE:
            if (cmd == SIOCGA80211)
                return getPOWERSAVE((apple80211_powersave_data *)req->req_data);
            if (cmd == SIOCSA80211)
                return setPOWERSAVE((apple80211_powersave_data *)req->req_data);
            return kIOReturnUnsupported;
        case APPLE80211_IOC_CARD_CAPABILITIES:
            if (instance == NULL || cmd != SIOCGA80211)
                return (instance == NULL) ? kIOReturnNotReady : kIOReturnUnsupported;
            if (req->req_len != 0 &&
                req->req_len < TahoeCapabilityContracts::kApple80211BindCardCapabilitiesLength)
                return kIOReturnBadArgument;
            if (req->req_len != 0 &&
                req->req_len < sizeof(apple80211_capability_data)) {
                apple80211_capability_data cd;
                IOReturn ret = instance->getCARD_CAPABILITIES(this, &cd);
                if (ret != kIOReturnSuccess)
                    return ret;
                memcpy(req->req_data, &cd, req->req_len);
                return kIOReturnSuccess;
            }
            return instance->getCARD_CAPABILITIES(
                this,
                (apple80211_capability_data *)req->req_data);
        case APPLE80211_IOC_STATE:
            if (cmd == SIOCGA80211) {
                if (instance != NULL && instance->fAPSTAOwner != NULL) {
                    return instance->getAPSTA_STATE(
                        this,
                        (AirportItlwmAPSTAStateDataLayout *)req->req_data);
                }
                return getSTATE((apple80211_state_data *)req->req_data);
            }
            return kIOReturnUnsupported;
        case APPLE80211_IOC_PHY_MODE:
            return (cmd == SIOCGA80211) ? getPHY_MODE((apple80211_phymode_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_NOISE:
            return (cmd == SIOCGA80211) ? getNOISE((apple80211_noise_data *)req->req_data)
                                        : kIOReturnUnsupported;
#if __IO80211_TARGET >= __MAC_26_0
        case APPLE80211_IOC_INT_MIT:
            /*
             * The current public SET wrapper is an unread fixed 0xe082280e
             * leaf. Keep the already aligned GET leaf and the legacy helper
             * outside this Tahoe public carrier path.
             */
            if (cmd == SIOCSA80211)
                return static_cast<IOReturn>(0xe082280e);
            if (cmd == SIOCGA80211)
                return static_cast<IOReturn>(0xe082280e);
            return kIOReturnUnsupported;
#endif // __IO80211_TARGET >= __MAC_26_0
        case APPLE80211_IOC_RSSI:
            return (cmd == SIOCGA80211) ? getRSSI((apple80211_rssi_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_ASSOCIATE:
#if __IO80211_TARGET >= __MAC_26_0
            /*
             * The current public GET wrapper is an unread fixed 0xe082280e
             * leaf. Preserve the separate association SET producer below.
             */
            if (cmd == SIOCGA80211)
                return static_cast<IOReturn>(0xe082280e);
#endif // __IO80211_TARGET >= __MAC_26_0
            return (cmd == SIOCSA80211) ? setASSOCIATE((apple80211_assoc_data *)req->req_data)
                                        : kIOReturnUnsupported;
#if __IO80211_TARGET >= __MAC_26_0
        case APPLE80211_IOC_ASSOCIATE_RESULT:
            /*
             * The current public GET wrapper is an unread fixed 0xe082280e
             * leaf. Keep the legacy association-result producer separate.
             */
            if (cmd == SIOCGA80211)
                return static_cast<IOReturn>(0xe082280e);
            return kIOReturnUnsupported;
#endif // __IO80211_TARGET >= __MAC_26_0
        case APPLE80211_IOC_DISASSOCIATE:
#if __IO80211_TARGET >= __MAC_26_0
            /*
             * The current public GET wrapper is an unread fixed 0xe082280e
             * leaf. Preserve the separate disassociation SET producer below.
             */
            if (cmd == SIOCGA80211)
                return static_cast<IOReturn>(0xe082280e);
#endif // __IO80211_TARGET >= __MAC_26_0
            return (cmd == SIOCSA80211) ? setDISASSOCIATE(req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_STATUS_DEV_NAME:
#if __IO80211_TARGET >= __MAC_26_0
            /*
             * The current public GET wrapper is an unread fixed 0xe082280e
             * leaf. Match it before the generic carrier-null fallback;
             * no STATUS_DEV_NAME carrier access is needed for that leaf.
             */
            if (cmd == SIOCGA80211)
                return static_cast<IOReturn>(0xe082280e);
#endif // __IO80211_TARGET >= __MAC_26_0
            return kIOReturnUnsupported;
        case APPLE80211_IOC_RATE:
            if (cmd == SIOCGA80211)
                return getRATE((apple80211_rate_data *)req->req_data);
            if (cmd == SIOCSA80211)
                return setRATE((apple80211_rate_data *)req->req_data);
            return kIOReturnUnsupported;
        case APPLE80211_IOC_GUARD_INTERVAL:
            return (cmd == SIOCGA80211) ? getGUARD_INTERVAL((apple80211_guard_interval_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_POWER_DEBUG_INFO:
            return (cmd == SIOCGA80211) ? getPOWER_DEBUG_INFO((apple80211_power_debug_info *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_HT_CAPABILITY:
            if (cmd == SIOCGA80211)
                return getHT_CAPABILITY((apple80211_ht_capability *)req->req_data);
            if (cmd == SIOCSA80211)
                return setHT_CAPABILITY((apple80211_ht_capability *)req->req_data);
            return kIOReturnUnsupported;
        case APPLE80211_IOC_HE_CAPABILITY:
            return (cmd == SIOCGA80211) ? getHE_CAPABILITY((apple80211_he_capability *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_TXPOWER:
            if (cmd == SIOCGA80211)
                return getTXPOWER((apple80211_txpower_data *)req->req_data);
            if (cmd == SIOCSA80211)
                return setTXPOWER((apple80211_txpower_data *)req->req_data);
            return kIOReturnUnsupported;
        case APPLE80211_IOC_TX_CHAIN_POWER:
        case APPLE80211_IOC_CHAIN_ACK:
        case APPLE80211_IOC_DESENSE:
        case APPLE80211_IOC_DESENSE_LEVEL:
            /*
             * Recovered Tahoe wrappers:
             *   CHAIN_ACK       -> raw selector 0xae, 24-byte carrier
             *   TX_CHAIN_POWER  -> raw selector 0x6c, 88-byte carrier
             *   DESENSE         -> raw selector 0xaf, 16-byte carrier
             *   DESENSE_LEVEL   -> raw selector 0xc2, 8-byte carrier
             *
             * The first three jump into AppleBCMWLANCore-specific vtable
             * slots after the IO80211 gate; DESENSE_LEVEL get-side returns
             * the same class-owner-absent status directly. This port has no
             * lifted Broadcom RF-control owner, so preserve the recovered
             * Apple80211 failure code instead of manufacturing zero carriers.
             */
            return kApple80211ClassOwnerAbsent;
        case APPLE80211_IOC_THERMAL_INDEX:
            return (cmd == SIOCGA80211) ? getTHERMAL_INDEX((apple80211_thermal_index_t *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_POWER_BUDGET:
            if (cmd == SIOCGA80211)
                return getPOWER_BUDGET((apple80211_power_budget_t *)req->req_data);
            if (cmd == SIOCSA80211)
                return setPOWER_BUDGET((apple80211_power_budget_t *)req->req_data);
            return kIOReturnUnsupported;
        case APPLE80211_IOC_LQM_CONFIG:
            if (cmd == SIOCGA80211)
                return getLQM_CONFIG((apple80211_lqm_config_t *)req->req_data);
            if (cmd == SIOCSA80211)
                return setLQM_CONFIG((apple80211_lqm_config_t *)req->req_data);
            return kIOReturnUnsupported;
        case APPLE80211_IOC_PRIVATE_MAC:
            return (cmd == SIOCGA80211) ? getPRIVATE_MAC((apple80211_private_mac_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_SET_MAC_ADDRESS:
            return (cmd == SIOCSA80211)
                       ? setSET_MAC_ADDRESS((apple80211_set_mac_address_data *)req->req_data)
                       : kIOReturnUnsupported;
        case APPLE80211_IOC_OFFLOAD_TCPKA_ENABLE:
            if (cmd == SIOCGA80211)
                return getOFFLOAD_TCPKA_ENABLE((apple80211_offload_tcpka_enable_t *)req->req_data);
            if (cmd == SIOCSA80211)
                return setOFFLOAD_TCPKA_ENABLE((apple80211_offload_tcpka_enable_t *)req->req_data);
            return kIOReturnUnsupported;
        case APPLE80211_IOC_OP_MODE:
#if __IO80211_TARGET >= __MAC_26_0
            /*
             * The current public SET wrapper is an unread fixed 0xe082280e
             * leaf. Preserve the separate dynamic GET producer below.
             */
            if (cmd == SIOCSA80211)
                return static_cast<IOReturn>(0xe082280e);
#endif // __IO80211_TARGET >= __MAC_26_0
            if (cmd == SIOCGA80211) {
                if (instance != NULL && instance->fAPSTAOwner != NULL) {
                    return instance->getAPSTA_OP_MODE(
                        this,
                        (AirportItlwmAPSTAOpModeDataLayout *)req->req_data);
                }
                return getOP_MODE((apple80211_opmode_data *)req->req_data);
            }
            return kIOReturnUnsupported;
        case APPLE80211_IOC_SUPPORTED_CHANNELS:
        case APPLE80211_IOC_HW_SUPPORTED_CHANNELS:
            return (cmd == SIOCGA80211) ? getSUPPORTED_CHANNELS((apple80211_sup_channel_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_COUNTRY_CHANNELS:
            return (cmd == SIOCGA80211) ? getCOUNTRY_CHANNELS((apple80211_country_channel_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_LOCALE:
            return (cmd == SIOCGA80211) ? getLOCALE((apple80211_locale_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_DEAUTH:
            if (cmd == SIOCGA80211)
                return getDEAUTH((apple80211_deauth_data *)req->req_data);
            if (cmd == SIOCSA80211)
                return setDEAUTH((apple80211_deauth_data *)req->req_data);
            return kIOReturnUnsupported;
        case APPLE80211_IOC_RATE_SET:
            /*
             * The current 25C56 public SET wrapper is an unread fixed
             * 0xe082280e leaf. Preserve the separate dynamic GET producer.
             */
#if __IO80211_TARGET >= __MAC_26_0
            if (cmd == SIOCSA80211)
                return static_cast<IOReturn>(0xe082280e);
#endif // __IO80211_TARGET >= __MAC_26_0
            return (cmd == SIOCGA80211) ? getRATE_SET((apple80211_rate_set_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_ROAM_PROFILE:
            if (cmd == SIOCGA80211)
                return getROAM_PROFILE((apple80211_roam_profile_all_bands *)req->req_data);
            if (cmd == SIOCSA80211)
                return setROAM_PROFILE((apple80211_roam_profile_all_bands *)req->req_data);
            return kIOReturnUnsupported;
        case APPLE80211_IOC_RSN_IE:
            if (cmd == SIOCGA80211)
                return getRSN_IE((apple80211_rsn_ie_data *)req->req_data);
            if (cmd == SIOCSA80211)
                return setRSN_IE((apple80211_rsn_ie_data *)req->req_data);
            return kIOReturnUnsupported;
        case APPLE80211_IOC_AP_IE_LIST:
            return (cmd == SIOCGA80211) ? getAP_IE_LIST((apple80211_ap_ie_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_BT_COEX_FLAGS:
            /*
             * Recovered Tahoe wrapper setBT_COEX_FLAGS is a direct raw `6`
             * return. The getter remains on the inherited IO80211/WCL path
             * until its list-backed producer body is fully recovered.
             */
            return (cmd == SIOCSA80211) ? kApple80211RawEnxio
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_BT_POWER:
            /*
             * The current 25C56 public GET wrapper is an unread fixed
             * 0xe082280e leaf. Do not manufacture or inspect a BT-power
             * carrier on that Tahoe public route. The existing SET and
             * pre-26 paths remain their own contracts.
             */
#if __IO80211_TARGET >= __MAC_26_0
            if (cmd == SIOCGA80211)
                return static_cast<IOReturn>(0xe082280e);
#endif // __IO80211_TARGET >= __MAC_26_0
            return (cmd == SIOCSA80211) ? kApple80211ClassOwnerAbsent
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_BTCOEX_PROFILES:
        case APPLE80211_IOC_BTCOEX_CONFIG:
        case APPLE80211_IOC_BTCOEX_OPTIONS:
        case APPLE80211_IOC_BTCOEX_MODE:
            // IO80211Family 26.3 implements both directions as a direct
            // class-owner-absent leaf.  These legacy selectors must not
            // acknowledge or retain a synthetic BT coexistence state.
            return kApple80211ClassOwnerAbsent;
        case APPLE80211_IOC_BGSCAN_CACHE_RESULTS:
            return (cmd == SIOCGA80211) ? getWCL_BGSCAN_CACHE_RESULT((apple80211_bgscan_cached_network_data_list *)req->req_data)
                                        : kIOReturnUnsupported;
        case TahoeSkywalkIoctlRoutes::kIocWclBssInfo:
            /*
             * WCLNetManager::updateBss() requests selector 0x1b1 into a
             * 0x844 BeaconMetaData+IE buffer before constructing the
             * WCL-owned current WCLBSSBeacon. Route that raw get-side
             * selector to the recovered current-BSS producer.
             */
            return (cmd == SIOCGA80211)
                       ? getWCL_BSS_INFO((apple80211_beacon_msg *)req->req_data)
                       : kIOReturnUnsupported;
        case APPLE80211_IOC_CHIP_COUNTER_STATS:
            return (cmd == SIOCGA80211) ? getCHIP_COUNTER_STATS((apple80211_chip_stats *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_DBG_GUARD_TIME_PARAMS:
            if (cmd == SIOCGA80211)
                return getDBG_GUARD_TIME_PARAMS((apple80211_dbg_guard_time_params *)req->req_data);
            if (cmd == SIOCSA80211)
                return setDBG_GUARD_TIME_PARAMS((apple80211_dbg_guard_time_params *)req->req_data);
            return kIOReturnUnsupported;
        case APPLE80211_IOC_AWDL_RSDB_CAPS:
            return (cmd == SIOCGA80211) ? getAWDL_RSDB_CAPS((apple80211_rsdb_capability *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_TKO_DUMP:
            return (cmd == SIOCGA80211) ? getTKO_DUMP((apple80211_tko_dump *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_CIPHER_KEY:
            /*
             * The current 25C56 public GET wrapper is an unread fixed
             * 0xe082280e leaf. Keep the SET path below intact: it owns the
             * separate local key and PMK carrier handling.
             */
#if __IO80211_TARGET >= __MAC_26_0
            if (cmd == SIOCGA80211)
                return static_cast<IOReturn>(0xe082280e);
#endif // __IO80211_TARGET >= __MAC_26_0
            if (cmd != SIOCSA80211)
                return kIOReturnUnsupported;
            if (instance != NULL && instance->fAPSTAOwner != NULL)
                return instance->setAPSTA_CIPHER_KEY(
                    this, (apple80211_key *)req->req_data);
            return setCIPHER_KEY((apple80211_key *)req->req_data);
        case APPLE80211_IOC_STATION_LIST:
            if (instance == NULL)
                return kIOReturnNotReady;
            return (cmd == SIOCGA80211)
                ? instance->getAPSTA_STATION_LIST(
                    this, (apple80211_sta_data *)req->req_data)
                : kIOReturnUnsupported;
        case APPLE80211_IOC_PEER_CACHE_MAXIMUM_SIZE:
            if (instance == NULL)
                return kIOReturnNotReady;
            if (cmd == SIOCGA80211) {
                if (instance->fAPSTAOwner != NULL) {
                    return instance->getAPSTA_PEER_CACHE_MAXIMUM_SIZE(
                        this,
                        (AirportItlwmAPSTAPeerCacheMaximumSizeLayout *)req->req_data);
                }
                apple80211_peer_cache_maximum_size *data =
                    (apple80211_peer_cache_maximum_size *)req->req_data;
                data->version = APPLE80211_VERSION;
                data->max_peers = 255;
                return kIOReturnSuccess;
            }
            return kIOReturnUnsupported;
        case APPLE80211_IOC_CUR_PMK:
            // Current 25C56 public SET wrapper is an unread fixed nonzero
            // stub. Both public Tahoe ingress paths reach this dispatcher,
            // but the retained helper below is only for ABI/private contexts;
            // CIPHER_KEY and PLTI DeliverPMK stay separate PMK paths.
            if (cmd == SIOCSA80211) {
#if __IO80211_TARGET >= __MAC_26_0
                return static_cast<IOReturn>(0xe082280e);
#else
                return setCUR_PMK((struct apple80211_pmk *)req->req_data);
#endif // __IO80211_TARGET >= __MAC_26_0
            }
            return getCUR_PMK((struct apple80211_pmk *)req->req_data);
        case APPLE80211_IOC_ASSOCIATION_STATUS:
            return (cmd == SIOCGA80211) ? getASSOCIATION_STATUS((apple80211_assoc_status_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_COUNTRY_CODE:
            if (instance == NULL)
                return kIOReturnNotReady;
            if (cmd == SIOCGA80211) {
                if (req->req_len == APPLE80211_MAX_CC_LEN) {
                    apple80211_country_code_data country{};
                    IOReturn ret = instance->getCOUNTRY_CODE(this, &country);
                    if (ret != kIOReturnSuccess)
                        return ret;
                    memcpy(req->req_data, country.cc, APPLE80211_MAX_CC_LEN);
                    return kIOReturnSuccess;
                }
                return instance->getCOUNTRY_CODE(this, (apple80211_country_code_data *)req->req_data);
            }
            if (cmd == SIOCSA80211)
                return instance->setCOUNTRY_CODE(this, (apple80211_country_code_data *)req->req_data);
            return kIOReturnUnsupported;
        case APPLE80211_IOC_DRIVER_VERSION:
            if (instance == NULL || cmd != SIOCGA80211)
                return (instance == NULL) ? kIOReturnNotReady : kIOReturnUnsupported;
            return instance->getDRIVER_VERSION(this, (apple80211_version_data *)req->req_data);
        case APPLE80211_IOC_HARDWARE_VERSION:
            if (instance == NULL || cmd != SIOCGA80211)
                return (instance == NULL) ? kIOReturnNotReady : kIOReturnUnsupported;
            return instance->getHARDWARE_VERSION(this, (apple80211_version_data *)req->req_data);
        case APPLE80211_IOC_MCS:
            return (cmd == SIOCGA80211) ? getMCS((apple80211_mcs_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_MCS_INDEX_SET:
            return (cmd == SIOCGA80211) ? getMCS_INDEX_SET((apple80211_mcs_index_set_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_MCS_VHT:
            if (cmd == SIOCGA80211)
                return getMCS_VHT((apple80211_mcs_vht_data *)req->req_data);
            return kIOReturnUnsupported;
        case APPLE80211_IOC_VHT_CAPABILITY:
            if (cmd == SIOCGA80211)
                return getVHT_CAPABILITY((apple80211_vht_capability *)req->req_data);
            if (cmd == SIOCSA80211)
                return setVHT_CAPABILITY((apple80211_vht_capability *)req->req_data);
            return kIOReturnUnsupported;
        case APPLE80211_IOC_WOW_TEST:
            return (cmd == SIOCSA80211) ? setWOW_TEST((apple80211_wow_test_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_TRAP_CRASHTRACER_MINI_DUMP:
            return (cmd == SIOCGA80211) ? getTRAP_CRASHTRACER_MINI_DUMP((apple80211_trap_mini_dump_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_VIRTUAL_IF_CREATE:
            return (cmd == SIOCSA80211) ? setVIRTUAL_IF_CREATE((apple80211_virt_if_create_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_VIRTUAL_IF_DELETE:
            if (cmd == SIOCSA80211) {
#if __IO80211_TARGET >= __MAC_26_0
                // Current 25C56 public SET wrapper is an unread fixed nonzero
                // stub. The controller-owned APSTA cleanup below remains for
                // release/failure and pre-26 paths, but this public Tahoe
                // carrier must not report a state transition Apple does not make.
                return static_cast<IOReturn>(0xe082280e);
#else
                return setVIRTUAL_IF_DELETE(
                    (apple80211_virt_if_delete_data *)req->req_data);
#endif // __IO80211_TARGET >= __MAC_26_0
            }
            return kIOReturnUnsupported;
        case APPLE80211_IOC_SCAN_REQ:
#if __IO80211_TARGET >= __MAC_26_0
            /*
             * The current public GET wrapper is an unread fixed 0xe082280e
             * leaf. Keep the separate dynamic SET scan producer intact.
             */
            if (cmd == SIOCGA80211)
                return static_cast<IOReturn>(0xe082280e);
#endif // __IO80211_TARGET >= __MAC_26_0
            return (cmd == SIOCSA80211) ? setSCAN_REQ((apple80211_scan_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_IBSS_MODE:
#if __IO80211_TARGET >= __MAC_26_0
            /*
             * The current public GET wrapper is an unread fixed 0xe082280e
             * leaf. Keep the separate IBSS SET producer and the preceding
             * generic null-carrier fallback intact.
             */
            if (cmd == SIOCGA80211)
                return static_cast<IOReturn>(0xe082280e);
#endif // __IO80211_TARGET >= __MAC_26_0
            return (cmd == SIOCSA80211) ? setIBSS_MODE((apple80211_network_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_IE:
            return (cmd == SIOCSA80211) ? setIE((apple80211_ie_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_OFFLOAD_ARP:
            return (cmd == SIOCSA80211) ? setOFFLOAD_ARP((apple80211_offload_arp_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_OFFLOAD_NDP:
            return (cmd == SIOCSA80211) ? setOFFLOAD_NDP((apple80211_offload_ndp_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_GAS_REQ:
            return (cmd == SIOCSA80211) ? setGAS_REQ((apple80211_gas_query_t *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_RESET_CHIP:
            return (cmd == SIOCSA80211) ? setRESET_CHIP((apple80211_reset_command *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_CRASH:
            return (cmd == SIOCSA80211) ? setCRASH((apple80211_crash_command *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_RANGING_ENABLE:
            return (cmd == SIOCSA80211) ? setRANGING_ENABLE((apple80211_ranging_enable_request_t *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_RANGING_START:
            return (cmd == SIOCSA80211) ? setRANGING_START((apple80211_ranging_start_request_t *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_RANGING_AUTHENTICATE:
            return (cmd == SIOCSA80211) ? setRANGING_AUTHENTICATE((apple80211_ranging_authenticate_request_t *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_BTCOEX_PROFILE:
            if (cmd == SIOCGA80211)
                return getBTCOEX_PROFILE((apple80211_btcoex_profile *)req->req_data);
            if (cmd == SIOCSA80211)
                return setBTCOEX_PROFILE((apple80211_btcoex_profile *)req->req_data);
            return kIOReturnUnsupported;
        case APPLE80211_IOC_BTCOEX_PROFILE_ACTIVE:
            if (cmd == SIOCGA80211)
                return getBTCOEX_PROFILE_ACTIVE((apple80211_btcoex_profile_active_data *)req->req_data);
            if (cmd == SIOCSA80211)
                return setBTCOEX_PROFILE_ACTIVE((apple80211_btcoex_profile_active_data *)req->req_data);
            return kIOReturnUnsupported;
        case APPLE80211_IOC_MAX_NSS_FOR_AP:
            return (cmd == SIOCGA80211) ? getMAX_NSS_FOR_AP((apple80211_btcoex_max_nss_for_ap_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_BTCOEX_2G_CHAIN_DISABLE:
            if (cmd == SIOCGA80211)
                return getBTCOEX_2G_CHAIN_DISABLE((apple80211_btcoex_2g_chain_disable *)req->req_data);
            if (cmd == SIOCSA80211)
                return setBTCOEX_2G_CHAIN_DISABLE((apple80211_btcoex_2g_chain_disable *)req->req_data);
            return kIOReturnUnsupported;
        case APPLE80211_IOC_TKO_PARAMS:
            return (cmd == SIOCGA80211) ? getTKO_PARAMS((apple80211_tko_params *)req->req_data)
                                        : (cmd == SIOCSA80211 ? setTKO_PARAMS((apple80211_tko_params *)req->req_data)
                                                              : kIOReturnUnsupported);
        case APPLE80211_IOC_NSS:
            return (cmd == SIOCGA80211) ? getNSS((apple80211_nss_data *)req->req_data)
                                        : kIOReturnUnsupported;
#if __IO80211_TARGET >= __MAC_26_0
        case APPLE80211_IOC_BSS_BLACKLIST: {
            const uint32_t routeStatus =
                TahoeBssBlacklistContracts::routePreflightStatus(
                    true, req->req_len, req->req_data);
            if (routeStatus != TahoeBssBlacklistContracts::kSuccessStatus)
                return static_cast<IOReturn>(routeStatus);

            const uint32_t wrapperStatus =
                TahoeBssBlacklistContracts::wrapperStatus(
                    TahoeBssBlacklistContracts::localAdmissionStatus(
                        isCommandProhibited(
                            TahoeBssBlacklistContracts::kSelector)),
                    instance != nullptr);
            if (wrapperStatus != TahoeBssBlacklistContracts::kSuccessStatus)
                return static_cast<IOReturn>(wrapperStatus);

            if (cmd == SIOCGA80211)
                return getBSS_BLACKLIST((bss_blacklist *)req->req_data);
            if (cmd == SIOCSA80211)
                return setBSS_BLACKLIST((bss_blacklist *)req->req_data);
            return kIOReturnUnsupported;
        }
#endif
        case APPLE80211_IOC_CURRENT_NETWORK:
            return (cmd == SIOCGA80211) ? getCURRENT_NETWORK((apple80211_scan_result *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_LINK_CHANGED_EVENT_DATA:
            return (cmd == SIOCGA80211) ? getLINK_CHANGED_EVENT_DATA((apple80211_link_changed_event_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_STA_IE_LIST:
            if (instance == NULL)
                return kIOReturnNotReady;
            return (cmd == SIOCGA80211)
                ? instance->getAPSTA_STA_IE_LIST(
                    this, (AirportItlwmAPSTAStaIEDataLayout *)req->req_data)
                : kIOReturnUnsupported;
        case APPLE80211_IOC_KEY_RSC:
            if (instance == NULL)
                return kIOReturnNotReady;
            return (cmd == SIOCGA80211)
                ? instance->getAPSTA_KEY_RSC(
                    this, (AirportItlwmAPSTAKeyRscDataLayout *)req->req_data)
                : kIOReturnUnsupported;
        case APPLE80211_IOC_STA_STATS:
            if (instance == NULL)
                return kIOReturnNotReady;
            return (cmd == SIOCGA80211)
                ? instance->getAPSTA_STA_STATS(
                    this, (AirportItlwmAPSTAStaStatsDataLayout *)req->req_data)
                : kIOReturnUnsupported;
        case APPLE80211_IOC_STA_AUTHORIZE:
            if (instance == NULL)
                return kIOReturnNotReady;
            return (cmd == SIOCSA80211)
                ? instance->setSTA_AUTHORIZE(
                    this, (AirportItlwmAPSTAStaAuthorizeInputLayout *)req->req_data)
                : kIOReturnUnsupported;
        case APPLE80211_IOC_STA_DISASSOCIATE:
            if (instance == NULL)
                return kIOReturnNotReady;
            return (cmd == SIOCSA80211)
                ? instance->setSTA_DISASSOCIATE(
                    this, (AirportItlwmAPSTAStaDisassocInputLayout *)req->req_data, false)
                : kIOReturnUnsupported;
        case APPLE80211_IOC_STA_DEAUTH:
            if (instance == NULL)
                return kIOReturnNotReady;
            return (cmd == SIOCSA80211)
                ? instance->setSTA_DISASSOCIATE(
                    this, (AirportItlwmAPSTAStaDisassocInputLayout *)req->req_data, true)
                : kIOReturnUnsupported;
        case APPLE80211_IOC_RSN_CONF:
            if (instance == NULL)
                return kIOReturnNotReady;
            return (cmd == SIOCSA80211)
                ? instance->setRSN_CONF(
                    this, (apple80211_rsn_conf_data *)req->req_data)
                : kIOReturnUnsupported;
        case APPLE80211_IOC_VHT_MCS_INDEX_SET:
            return (cmd == SIOCGA80211) ? getVHT_MCS_INDEX_SET((apple80211_vht_mcs_index_set_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_HOST_AP_MODE_HIDDEN:
            if (instance == NULL)
                return kIOReturnNotReady;
            if (cmd == SIOCGA80211)
                return instance->getHOST_AP_MODE_HIDDEN(
                    this,
                    (AirportItlwmAPSTAHostApModeHiddenOutputLayout *)req->req_data);
            if (cmd == SIOCSA80211)
                return instance->setHOST_AP_MODE_HIDDEN(
                    this,
                    (AirportItlwmAPSTAHostApModeHiddenLayout *)req->req_data);
            return kIOReturnUnsupported;
        case APPLE80211_IOC_SOFTAP_PARAMS:
            if (instance == NULL)
                return kIOReturnNotReady;
            if (cmd == SIOCGA80211)
                return instance->getSOFTAP_PARAMS(
                    this,
                    (AirportItlwmAPSTASoftAPParamsOutputLayout *)req->req_data);
            if (cmd == SIOCSA80211)
                return instance->setSOFTAP_PARAMS(
                    this,
                    (AirportItlwmAPSTASoftAPParamsInputLayout *)req->req_data);
            return kIOReturnUnsupported;
        case APPLE80211_IOC_SOFTAP_STATS:
            if (instance == NULL)
                return kIOReturnNotReady;
            if (cmd == SIOCGA80211)
                return instance->getSOFTAP_STATS(
                    this,
                    (AirportItlwmAPSTASoftAPStatsLayout *)req->req_data);
            return kIOReturnUnsupported;
        case APPLE80211_IOC_PEER_CACHE_CONTROL:
            if (instance == NULL)
                return kIOReturnNotReady;
            return (cmd == SIOCSA80211)
                ? instance->setPEER_CACHE_CONTROL(
                    this, (AirportItlwmAPSTAPeerCacheControlLayout *)req->req_data)
                : kIOReturnUnsupported;
        case APPLE80211_IOC_SOFTAP_TRIGGER_CSA:
            if (instance == NULL)
                return kIOReturnNotReady;
            if (cmd == SIOCSA80211)
                return instance->setSOFTAP_TRIGGER_CSA(
                    this,
                    (AirportItlwmAPSTACsaInputLayout *)req->req_data);
            return kIOReturnUnsupported;
        case APPLE80211_IOC_SOFTAP_WIFI_NETWORK_INFO_IE:
            if (instance == NULL)
                return kIOReturnNotReady;
            if (cmd == SIOCSA80211)
                return instance->setSOFTAP_WIFI_NETWORK_INFO_IE(
                    this,
                    (AirportItlwmAPSTASoftAPWifiNetworkInfoCarrierLayout *)req->req_data);
            return kIOReturnUnsupported;
        case APPLE80211_IOC_SOFTAP_EXTENDED_CAPABILITIES_IE:
            if (instance == NULL)
                return kIOReturnNotReady;
            if (cmd == SIOCSA80211)
                return instance->setSOFTAP_EXTENDED_CAPABILITIES_IE(
                    this,
                    (apple80211_softap_extended_capabilities_info *)req->req_data);
            return kIOReturnUnsupported;
        case APPLE80211_IOC_MIS_MAX_STA:
            if (instance == NULL)
                return kIOReturnNotReady;
            if (cmd == SIOCSA80211)
                return instance->setMIS_MAX_STA(this,
                    (apple80211_mis_max_sta *)req->req_data);
            return kIOReturnUnsupported;
        default:
            return kIOReturnUnsupported;
    }
}

#if __IO80211_TARGET >= __MAC_26_0
bool AirportItlwmSkywalkInterface::
init()
{
    // Apple's Tahoe APSTA surface enters construction through subclass
    // no-arg init(), then performs provider/role/address binding on a
    // separate follow-up path. Advertising a local 2-arg init override here
    // was misleading: the port ignored those arguments anyway and the true
    // Apple-recovered crash-free constructor contract is still the no-arg
    // IO80211InfraInterface::init() path.
    if (!IO80211InfraInterface::init()) {
        XYLog("%s IO80211InfraInterface::init failed\n", __PRETTY_FUNCTION__);
        return false;
    }
    instance = NULL;
    fHalService = NULL;
    scanSource = NULL;
    cachedPowersaveLevel = APPLE80211_POWERSAVE_MODE_DISABLED;
    cachedTcpkaOffloadSupported = false;
    cachedTcpkaOffloadEnabled = false;
    cachedOSFeatureFlags = 0;
    cachedDhcpRenewalData = false;
    memset(&cachedHtCapability, 0, sizeof(cachedHtCapability));
    hasCachedHtCapability = false;
    leScanEnabledCount = 0;
    leScanDisabledCount = 0;
    leScanPeakSum = 0;
    leScanTotalSum = 0;
    memset(leScanDutyCount, 0, sizeof(leScanDutyCount));
    memset(&cachedVhtCapability, 0, sizeof(cachedVhtCapability));
    hasCachedVhtCapability = false;
    memset(cachedReassocRequest, 0, sizeof(cachedReassocRequest));
    hasCachedReassocRequest = false;
    memset(cachedLastActionFrame, 0, sizeof(cachedLastActionFrame));
    cachedLastActionFrameLen = 0;
    cachedLastActionFrameChannel = 0;
    cachedLastActionFrameCategory = 0;
    hasCachedLastActionFrame = false;
    memset(cachedBssBlacklist, 0, sizeof(cachedBssBlacklist));
    hasCachedBssBlacklist = false;
    memset(cachedTkoParams, 0, sizeof(cachedTkoParams));
    memset(fLastPublishedBssid, 0, sizeof(fLastPublishedBssid));
    fLastPublishedBssidValid = false;
    RT3_SET(12); // SkywalkInterface::init OK
    return true;
}

bool AirportItlwmSkywalkInterface::
init(IOService *provider, ether_addr *addr)
{
    // Tahoe still carries the secondary 2-arg vtable slot, but the kernel does
    // not export IO80211InfraInterface::init(provider, addr), so dropping the
    // local override makes the kext bind against a non-exported symbol and fail
    // BootKC verification. Keep this as a local shadow only; the real
    // constructor path is the no-arg init() + bindController() sequence above.
    return init();
}

void AirportItlwmSkywalkInterface::
setBSDName(char const *bsdName)
{
    IO80211InfraProtocol::setBSDName(bsdName);

    if (instance == nullptr || bsdName == nullptr || bsdName[0] == '\0')
        return;

    OSString *value = OSString::withCString(bsdName);
    if (value != nullptr) {
        instance->setProperty("BSD Name", value);
        value->release();
    }
}

bool AirportItlwmSkywalkInterface::
bindController(AirportItlwm *provider)
{
    // Recovered Apple APSTA construction uses a split contract: subclass
    // no-arg init first, then a separate parameter-binding path wires the
    // interface to controller-owned state before attach/start. Keep the local
    // port on the same shape instead of pretending the constructor itself is
    // the provider-binding entry.
    instance = provider;
    if (!instance) {
        XYLog("DEBUG %s FAIL: provider is NULL\n", __FUNCTION__);
        return false;
    }
    fHalService = instance->fHalService;
    scanSource = instance->scanSource;
    setInterfaceRole(1);
    setInterfaceId(1);
    return true;
}

int AirportItlwmSkywalkInterface::
getAssocState(void)
{
    return IO80211InfraInterface::getAssocState();
}

void AirportItlwmSkywalkInterface::
setDataPathState(bool state)
{
    IO80211InfraInterface::setDataPathState(state);
}

void AirportItlwmSkywalkInterface::
updateLinkStatus(void)
{
    IO80211InfraInterface::updateLinkStatus();
}

void AirportItlwmSkywalkInterface::
updateLinkStatusGated(void)
{
    IO80211InfraInterface::updateLinkStatusGated();
}

void AirportItlwmSkywalkInterface::
reportDetailedLinkStatus(if_link_status const *status)
{
    IOSkywalkNetworkInterface::reportDetailedLinkStatus(status);
}

void AirportItlwmSkywalkInterface::
updateDriverBssManagerRateAndMcs()
{
    struct ieee80211com *ic = fHalService ? fHalService->get80211Controller() : nullptr;
    if (ic == nullptr || ic->ic_state != IEEE80211_S_RUN || ic->ic_bss == nullptr)
        return;
    struct ieee80211_node *ni = ic->ic_bss;

    IO80211BssManager *bssManager =
        instance != nullptr ? instance->getBssManager() : nullptr;
    if (bssManager == nullptr)
        return;
    if (instance != nullptr) {
        const auto &association =
            instance->getTahoeOwnerRegistry().association;
        tahoeSeedBssManagerAuthContext(bssManager, association);
        if (association.hasCarrier) {
            tahoeSeedBssManagerAssociatedAuthType(
                bssManager, association.authLower, association.authUpper);
        } else {
            tahoeSeedBssManagerAssociatedAuthType(
                bssManager, current_authtype_lower, current_authtype_upper);
        }
    } else {
        tahoeSeedBssManagerAssociatedAuthType(
            bssManager, current_authtype_lower, current_authtype_upper);
    }
    Bands band = static_cast<Bands>(0);
    if (bssManager->getCurrentBand(band) == kIOReturnSuccess) {
        bssManager->setBandInfoBitmap(
            tahoeBssManagerBandInfoBitmap(static_cast<uint32_t>(band)));
    }
    bssManager->setLastBSSRssi();

    if (ni->ni_esslen <= IEEE80211_NWID_LEN)
        (void)bssManager->setAssocSSID(ni->ni_essid, ni->ni_esslen);

    if (ni->ni_rsnie_tlv != nullptr &&
        ni->ni_rsnie_tlv_len != 0 &&
        ni->ni_rsnie_tlv_len <= kAppleBssManagerAssocRsnIeMaxLen) {
        (void)bssManager->setAssocRSNIE(ni->ni_rsnie_tlv, ni->ni_rsnie_tlv_len);
    } else {
        (void)bssManager->setAssocRSNIE(nullptr, 0);
    }

    apple80211_rate_set_data rates;
    if (getRATE_SET(&rates) == kIOReturnSuccess)
        bssManager->setRateSet(rates);

    apple80211_mcs_index_set_data mcs;
    if (getMCS_INDEX_SET(&mcs) == kIOReturnSuccess)
        bssManager->setMCSIndexSet(mcs);

    apple80211_vht_mcs_index_set_data vht;
    memset(&vht, 0, sizeof(vht));
    vht.version = APPLE80211_VERSION;
    vht.mcs_map = 0xffff;
    if (getVHT_MCS_INDEX_SET(&vht) != kIOReturnSuccess) {
        vht.version = APPLE80211_VERSION;
        vht.mcs_map = 0xffff;
    }
    bssManager->setVHTMCSIndexSet(vht);

    apple80211_he_mcs_index_set_data he;
    memset(&he, 0, sizeof(he));
    he.version = APPLE80211_VERSION;
    he.mcs_map = 0xffff;
    if ((ic->ic_flags & IEEE80211_F_HEON) != 0 &&
        ic->ic_curmode >= IEEE80211_MODE_11AX) {
        he.mcs_map = ni->ni_he_mcs_nss_supp.tx_mcs_80;
    }
    bssManager->setHEMCSIndexSet(he);
}

bool AirportItlwmSkywalkInterface::
setLinkStateInternal(IO80211LinkState state, uint debounceTimeout, bool debounce,
                     uint code, uint connectionId)
{
    struct ieee80211com *ic = fHalService ? fHalService->get80211Controller() : nullptr;
    bool ret = IO80211InfraInterface::setLinkStateInternal(
        state, debounceTimeout, debounce, code, connectionId);
#if __IO80211_TARGET >= __MAC_26_0
    /*
     * Tahoe APPLE80211_M_LINK_CHANGED single-owner publisher on the
     * Skywalk link-state transition.
     *
     * The Tahoe userspace event handler
     * `__setupEventHandlersWithInterfaceName:_block_invoke` length-checks
     * every APPLE80211_M_LINK_CHANGED delivery against the 32-byte
     * apple80211_link_changed_event_data shape recovered from
     * WCLNetManager::getLinkChangeEventInternal at BootKC entry
     * 0xffffff8002111138. This override is the single owner of that
     * userspace carrier on Tahoe: both the Apple IO80211 framework path
     * that drives link state directly into
     * IO80211InfraInterface::setLinkStateInternal and the local BSD
     * newstate path through AirportItlwm::setLinkStateGated reach this
     * override (the latter via
     * `((IO80211InfraInterface *)fNetIf)->setLinkState(...)`), and the
     * Tahoe branch of setLinkStateGated no longer emits
     * APPLE80211_M_LINK_CHANGED itself, so the carrier is published
     * exactly once per accepted parent transition. Build the payload
     * inline from state already owned by the V1 and V2
     * APPLE80211_IOC_LINK_CHANGED_EVENT_DATA publishers: on link-up
     * `voluntary_up = 1`, `rssi` from the current node, and SNR/NF from a
     * valid HAL noise-floor sample; on link-down `voluntary_down` from the
     * locally tracked disassociation initiator, `reason =
     * APPLE80211_LINK_DOWN_REASON_DEAUTH`, and the current BSSID copied into
     * `last_assoc[0..5]`. Fields itlwm does not produce remain zero per the
     * bzero entry contract.
     *
     * Only the link-up and link-down terminal states publish the carrier;
     * the intermediate IO80211LinkState values are framework-internal
     * transitions and do not correspond to APPLE80211_M_LINK_CHANGED.
     *
     * Additionally gate publication on the parent bool return. Tahoe's
     * IO80211InfraInterface::setLinkStateInternal is a bool-return veneer:
     * true means the transition was accepted, false means it was rejected.
     */
    if (ret &&
        instance != nullptr && instance->fNetIf != nullptr &&
        (state == kIO80211NetworkLinkUp ||
         state == kIO80211NetworkLinkDown)) {
        apple80211_link_changed_event_data ed;
        bzero(&ed, sizeof(ed));
        const bool isLinkDown = (state != kIO80211NetworkLinkUp);
        if (!isLinkDown) {
            /*
             * Accepted SET_SSID-equivalent identity events must run after the
             * inherited parent link-up transition accepts, not on the earlier
             * RSN key-done edge. The accepted-join identity publisher is the
             * single local Tahoe owner for both populated BSSID_CHANGED and
             * SSID_CHANGED on this edge.
             */
            instance->publishTahoeAcceptedJoinIdentityEvents(
                "setLinkStateInternal");
        }
        ed.isLinkDown = isLinkDown ? 1 : 0;
        if (isLinkDown) {
            ed.voluntary_down = instance->disassocIsVoluntary ? 1 : 0;
            ed.reason = APPLE80211_LINK_DOWN_REASON_DEAUTH;
            if (ic != nullptr && ic->ic_bss != nullptr)
                memcpy(ed.last_assoc, ic->ic_bss->ni_bssid,
                       IEEE80211_ADDR_LEN);
        } else {
            ed.voluntary_up = 1;
            if (ic != nullptr && ic->ic_bss != nullptr) {
                int32_t rssi = IWM_MIN_DBM + ic->ic_bss->ni_rssi;
                ed.rssi = (uint32_t)rssi;
                uint16_t snr = 0;
                uint16_t nf = 0;
                if (TahoeLqmContracts::buildLinkChangedSignalMetrics(
                        rssi, fHalService->getDriverInfo()->getBSSNoise(), &snr, &nf)) {
                    ed.snr = snr;
                    ed.nf = nf;
                }
            }
        }
        instance->postMessage(instance->fNetIf, APPLE80211_M_LINK_CHANGED,
                              &ed, sizeof(ed), true);

        if (isLinkDown)
            this->fLastPublishedBssidValid = false;

        /*
         * The accepted identity publisher above owns the local SSID_CHANGED
         * carrier for this parent-success edge; keep this gate limited to the
         * link-changed carrier and the BSSID tracker reset on link-down.
         */
    }
#endif
    return ret;
}

void AirportItlwmSkywalkInterface::
setCurrentApAddress(ether_addr *addr)
{
    IO80211InfraInterface::setCurrentApAddress(addr);
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_LINK_STATE_UPDATE(apple80211_wcl_update_link_state *data)
{
    (void)IO80211InfraInterface::setWCL_LINK_STATE_UPDATE(data);
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    const uint8_t *payload = reinterpret_cast<const uint8_t *>(data);
    const bool linkUp = payload[6] != 0;
    const bool refreshCurrentBss = payload[8] != 0;
    if (instance == nullptr || instance->getBssManager() == nullptr)
        return kIOReturnNotReady;

    if (!linkUp) {
        instance->stopTahoeLqmStatsTimer();
        instance->clearTahoeCurrentBss();
        return kIOReturnSuccess;
    }

    if (!refreshCurrentBss) {
        instance->startTahoeLqmStatsTimer();
        return kIOReturnSuccess;
    }

    TahoeBssManagerContracts::BeaconPayload currentBss{};
    if (!buildTahoeCurrentBssPayload(fHalService, &currentBss))
        return kIOReturnError;
    if (!instance->setTahoeCurrentBss(currentBss.meta, currentBss.ie))
        return kIOReturnError;

    updateDriverBssManagerRateAndMcs();
    instance->startTahoeLqmStatsTimer();
    return kIOReturnSuccess;
}

SInt32 AirportItlwmSkywalkInterface::
setInterfaceEnable(bool enable)
{
    /*
     * Recovered AppleBCMWLANLowLatencyInterface::setInterfaceEnable(true)
     * continues past the base enable: it publishes the Skywalk carrier and
     * then raises the infra link state.  IOSkywalkLegacyEthernet reads its
     * visible IOLinkStatus from this provider context, so keep the lifted
     * body on the modeled low-latency object instead of faking registry
     * properties on the legacy child.
     */
    SInt32 ret = IO80211InfraInterface::setInterfaceEnable(enable);
    if (enable && ret == kIOReturnSuccess) {
        (void)reportLinkStatus(3, 0x80);
        (void)IO80211InfraInterface::setLinkState(
            kIO80211NetworkLinkUp, 1, false, 0, 0);
    }
    if (ret != kIOReturnSuccess)
        XYLog("DEBUG %s FAIL: enable=%d ret=0x%x instance=%p\n",
              __FUNCTION__, enable ? 1 : 0, ret, instance);
    return ret;
#else
bool AirportItlwmSkywalkInterface::
init(IOService *provider)
{
    bool ret = super::init(provider);
    if (!ret) {
        XYLog("%s super::init failed\n", __PRETTY_FUNCTION__);
        return false;
    }
    instance = OSDynamicCast(AirportItlwm, provider);
    if (!instance) {
        XYLog("DEBUG %s FAIL: provider is not AirportItlwm\n", __FUNCTION__);
        return false;
    }
    this->fHalService = instance->fHalService;
    this->scanSource = instance->scanSource;
    this->cachedPowersaveLevel = APPLE80211_POWERSAVE_MODE_DISABLED;
    this->cachedTcpkaOffloadSupported = false;
    this->cachedTcpkaOffloadEnabled = false;
    this->cachedOSFeatureFlags = 0;
    this->cachedDhcpRenewalData = false;
    memset(&this->cachedHtCapability, 0, sizeof(this->cachedHtCapability));
    this->hasCachedHtCapability = false;
    this->leScanEnabledCount = 0;
    this->leScanDisabledCount = 0;
    this->leScanPeakSum = 0;
    this->leScanTotalSum = 0;
    memset(this->leScanDutyCount, 0, sizeof(this->leScanDutyCount));
    memset(&this->cachedVhtCapability, 0, sizeof(this->cachedVhtCapability));
    this->hasCachedVhtCapability = false;
    memset(this->cachedReassocRequest, 0, sizeof(this->cachedReassocRequest));
    this->hasCachedReassocRequest = false;
    memset(this->cachedLastActionFrame, 0, sizeof(this->cachedLastActionFrame));
    this->cachedLastActionFrameLen = 0;
    this->cachedLastActionFrameChannel = 0;
    this->cachedLastActionFrameCategory = 0;
    this->hasCachedLastActionFrame = false;
    memset(this->cachedBssBlacklist, 0, sizeof(this->cachedBssBlacklist));
    this->hasCachedBssBlacklist = false;
    memset(this->cachedTkoParams, 0, sizeof(this->cachedTkoParams));
    RT3_SET(12); // SkywalkInterface::init OK
    return true;
#endif
}

//ifnet_t AirportItlwmSkywalkInterface::
//getBSDInterface()
//{
//    if (instance->bsdInterface)
//        return instance->bsdInterface->getIfnet();
//    return NULL;
//}

IOReturn AirportItlwmSkywalkInterface::
getSSID(struct apple80211_ssid_data *sd)
{
    struct ieee80211com * ic = fHalService->get80211Controller();
    // Apple's IO80211Controller::getSSIDData always pre-zeroes and returns success.
    // When not associated, the cached SSID data is zeroed (ssid_len=0).
    // Returning error here causes airportd to abort auto-join with "driver not available".
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

IOReturn AirportItlwmSkywalkInterface::
getTahoeCompactSSID(void *data, uint32_t length, uint32_t *returnedLength)
{
    if (data == nullptr || length != APPLE80211_MAX_SSID_LEN)
        return kIOReturnBadArgument;

    uint8_t *ssidBytes = static_cast<uint8_t *>(data);
    struct ieee80211com *ic = fHalService->get80211Controller();
    uint32_t ssidLength = 0;

    memset(ssidBytes, 0, APPLE80211_MAX_SSID_LEN);
    if (ic->ic_state == IEEE80211_S_RUN && ic->ic_bss != NULL &&
        ic->ic_bss->ni_esslen <= APPLE80211_MAX_SSID_LEN) {
        ssidLength = static_cast<uint32_t>(ic->ic_bss->ni_esslen);
        if (ssidLength != 0)
            memcpy(ssidBytes, ic->ic_bss->ni_essid, ssidLength);
    }
    if (returnedLength != nullptr)
        *returnedLength = ssidLength;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getAWDL_PEER_TRAFFIC_STATS(void *data, unsigned int length)
{
    // Tahoe visible APPLE80211_IOC_ASSOCIATE does not fall into the public
    // `setASSOCIATE(...)` path. Family `getSetHandler(20)` first emits the
    // hidden carrier `0x45` with the full `0x3ad8` assoc-candidates blob and,
    // when WCL does not absorb it, the fallback lands on this slot. Live
    // runtime proves that exact seam by logging `[470] getAWDL_PEER_TRAFFIC_STATS`
    // in the same cycle as `Exit-setASSOCIATE:153 ret:-536870201`.
    //
    // Reuse the already recovered public owner instead of leaking generic
    // unsupported from this hidden fallback. Non-association callers keep the
    // prior unsupported contract.
    if (data != nullptr &&
        TahoeAssociationContracts::isAssocCandidatesPayloadLength(length)) {
        if (airportItlwmRegDiagShouldBlock(kAirportItlwmRegDiagBlockHiddenAssoc)) {
            airportItlwmRegDiagRecordBlock(kAirportItlwmRegDiagBlockHiddenAssoc,
                                           kAirportItlwmRegDiagPathHiddenAssoc,
                                           length);
            airportItlwmRegDiagRecordAssoc(kAirportItlwmRegDiagPathHiddenAssoc,
                                           nullptr, 0, nullptr, 0, 0, 0,
                                           kIOReturnUnsupported);
            return kIOReturnUnsupported;
        }
        return setWCL_ASSOCIATE(reinterpret_cast<apple80211AssocCandidates *>(data));
    }

    if (data != nullptr && length == sizeof(apple80211_set_mac_address_data)) {
        return setSET_MAC_ADDRESS(
            reinterpret_cast<apple80211_set_mac_address_data *>(data));
    }

    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
getAUTH_TYPE(struct apple80211_authtype_data *ad)
{
    ad->version = APPLE80211_VERSION;
    ad->authtype_lower = current_authtype_lower;
    ad->authtype_upper = current_authtype_upper;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setAUTH_TYPE(struct apple80211_authtype_data *ad)
{
    current_authtype_lower = ad->authtype_lower;
    current_authtype_upper = ad->authtype_upper;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setCIPHER_KEY(struct apple80211_key *key)
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
                    break;
                case 0: // GTK
                    setGTK(key->key, key->key_len, key->key_index, key->key_rsc);
                    break;
            }
            break;
        case APPLE80211_CIPHER_PMK: {
            // APPLE80211_IOC_CIPHER_KEY (=3) with key_cipher_type =
            // APPLE80211_CIPHER_PMK (=6) remains an independent local PMK
            // ingestion path. The retained setCUR_PMK helper can share this
            // sink only in a distinct ABI/private context: current 25C56
            // public CUR_PMK SET does not reach it. installExternalPmkLocked
            // centralises the 32-byte validation, ic_psk copy,
            // IEEE80211_F_PSK flag, and WPA/RSN PSK auth-state refresh, and
            // emits only non-secret structural markers.
            return installExternalPmkLocked(key->key,
                                            key->key_len,
                                            "CIPHER_KEY");
        }
        case APPLE80211_CIPHER_MSK: {
            // Tahoe AppleBCMWLAN setKey case 9 shares the same PMK owner
            // helper as case 6 when the carrier delivers a 32-byte PMK
            // payload: the recovered AppleBCMWLANCore setter helper copies
            // the source bytes into the PMK store regardless of whether
            // the caller selected case 6 (PMK) or case 9 (MSK). The
            // matching local sink is ieee80211com::ic_psk, so a 32-byte
            // MSK delivered through this branch must populate the
            // host-supplicant PMK store the same way case 6 does. The
            // OpenBSD-derived net80211 supplicant only consumes a 32-byte
            // PMK; longer MSK material that case 9 may carry on 8021X
            // paths falls back to the existing PMKSA cache add so the
            // local AKM_8021X consumer is unchanged.
            if (key->key_len == IEEE80211_PMK_LEN) {
                return installExternalPmkLocked(key->key,
                                                key->key_len,
                                                "CIPHER_KEY_MSK");
            }
            ieee80211_pmksa_add(fHalService->get80211Controller(),
                                IEEE80211_AKM_8021X,
                                fHalService->get80211Controller()->ic_bss->ni_macaddr,
                                key->key, 0);
            break;
        }
        case APPLE80211_CIPHER_PMKSA:
            ieee80211_pmksa_add(fHalService->get80211Controller(), IEEE80211_AKM_8021X,
                                fHalService->get80211Controller()->ic_bss->ni_macaddr, key->key, 0);
            break;
    }
    //fInterface->postMessage(APPLE80211_M_CIPHER_KEY_CHANGED);
    if (key->key_cipher_type == APPLE80211_CIPHER_TKIP ||
        key->key_cipher_type == APPLE80211_CIPHER_AES_OCB ||
        key->key_cipher_type == APPLE80211_CIPHER_AES_CCM) {
        if (key->key_flags == 0) {
            // GTK installed — 4-way handshake complete, notify Apple supplicant
            postMessage(APPLE80211_M_RSN_HANDSHAKE_DONE, NULL, 0, false);
        }
    }
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getPHY_MODE(struct apple80211_phymode_data *pd)
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

IOReturn AirportItlwmSkywalkInterface::
getCHANNEL(struct apple80211_channel_data *cd)
{
    struct ieee80211com * ic = fHalService->get80211Controller();
    // Apple's IO80211 framework pre-zeroes channel data and returns success.
    // When not associated, channel=0 / flags=0.
    // Returning error here causes airportd getCHANNEL queries to fail during init.
    memset(cd, 0, sizeof(apple80211_channel_data));
    cd->version = APPLE80211_VERSION;
    cd->channel.version = APPLE80211_VERSION;
    if (ic->ic_state == IEEE80211_S_RUN) {
        cd->channel.channel = ieee80211_chan2ieee(ic, ic->ic_bss->ni_chan);
        cd->channel.flags = ieeeChanFlag2apple(ic->ic_bss->ni_chan->ic_flags, ic->ic_bss->ni_chw);
    }
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getSTATE(struct apple80211_state_data *sd)
{
    memset(sd, 0, sizeof(*sd));
    sd->version = APPLE80211_VERSION;
    sd->state = fHalService->get80211Controller()->ic_state;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getMCS_INDEX_SET(struct apple80211_mcs_index_set_data *ad)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    if (ad == NULL)
        return kIOReturnBadArgument;
    if (ic->ic_bss == NULL)
        return kApple80211ErrDriverNotAvailable;

    memset(ad, 0, sizeof(*ad));
    ad->version = APPLE80211_VERSION;
    size_t size = min(ARRAY_SIZE(ic->ic_bss->ni_rxmcs), ARRAY_SIZE(ad->mcs_set_map));
    bool hasAnyMcsBit = false;
    for (size_t i = 0; i < size; i++) {
        ad->mcs_set_map[i] = ic->ic_bss->ni_rxmcs[i];
        hasAnyMcsBit |= ad->mcs_set_map[i] != 0;
    }

    // AppleBCMWLANCore::getMCS_INDEX_SET delegates to
    // IO80211BssManager::getCurrentMCSSet(). That helper returns 0xe0822403
    // when there is no current BSS and 0xe00002f0 when the cached MCS set is
    // not marked valid. The port does not expose Apple's dedicated validity
    // bit, so the closest trustworthy local carrier is "current BSS exists but
    // the copied MCS map is still empty" -> no cached value.
    if (!hasAnyMcsBit)
        return kApple80211ErrNoCachedValue;

    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getVHT_MCS_INDEX_SET(struct apple80211_vht_mcs_index_set_data *data)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    if (ic->ic_bss == NULL || ic->ic_curmode < IEEE80211_MODE_11AC)
        return kIOReturnError;
    memset(data, 0, sizeof(struct apple80211_vht_mcs_index_set_data));
    data->version = APPLE80211_VERSION;
    data->mcs_map = ic->ic_bss->ni_vht_mcsinfo.tx_mcs_map;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getMCS_VHT(struct apple80211_mcs_vht_data *data)
{
    if (data == nullptr)
        return kIOReturnBadArgument;
    memset(data, 0, sizeof(struct apple80211_mcs_vht_data));
    data->version = APPLE80211_VERSION;
    // AppleBCMWLANCore::getMCS_VHT reads cached "nrate" state and only
    // populates the public blob when that transport currently encodes VHT.
    // Tahoe previously rebuilt the blob from ni_txmcs/ni_chw, which is not the
    // same producer path and diverges before association or before the next
    // firmware rate update lands.
    return fillTahoeMcsVhtFromCachedNrate(fHalService, data);
}

IOReturn AirportItlwmSkywalkInterface::
getRATE_SET(struct apple80211_rate_set_data *ad)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    if (ic->ic_bss == NULL) {
        // AppleBCMWLANCore::getRATE_SET delegates to
        // IO80211BssManager::getCurrentRateSet(), which returns 0xe0822403
        // when there is no current BSS. Tahoe previously leaked raw POSIX 6
        // here, which is not architecturally equivalent to the reference
        // producer/consumer contract.
        return kApple80211ErrDriverNotAvailable;
    }
    if (ic->ic_bss->ni_rates.rs_nrates == 0) {
        // The Apple helper splits "current BSS exists" from "cached rate-set
        // exists": an empty cached set returns 0xe00002f0 rather than success.
        return kApple80211ErrNoCachedValue;
    }
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

IOReturn AirportItlwmSkywalkInterface::
getOP_MODE(struct apple80211_opmode_data *od)
{
    // AppleBCMWLANCore::getOP_MODE starts the public carrier as
    // `version=1, op_mode=0`. It then ORs in APSTA bits, current-BSS
    // STA/IBSS mode via IO80211BssManager when associated, and monitor bits
    // from the core-private monitor byte. The local primary path owns only the
    // current-BSS STA/IBSS piece; APSTA remains routed through its owner.
    if (!TahoeOpModeContracts::initializePrimaryCarrier(od))
        return static_cast<IOReturn>(TahoeOpModeContracts::kInvalidArgumentStatus);
    struct ieee80211com *ic = fHalService->get80211Controller();
    if (ic->ic_state == IEEE80211_S_RUN && ic->ic_bss != NULL)
        TahoeOpModeContracts::publishAssociatedBssMode(od, ic->ic_bss->ni_capinfo);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getPOWER_DEBUG_INFO(apple80211_power_debug_info *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    // Tahoe builds this diagnostic reply from live power statistics, Core
    // snapshot fields, feature gates, and inactivity state. The Intel port
    // has no equivalent telemetry producer, so do not fabricate a blank one.
    (void)data;
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
getROAM_PROFILE(apple80211_roam_profile_all_bands *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    // Tahoe obtains each band from the RoamAdapter after primary-interface,
    // association, and firmware-I/O checks. A blank carrier with every band
    // marked successful did none of that work, so fail closed until a real
    // owner exists.
    (void)data;
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
getCOUNTRY_CHANNELS(apple80211_country_channel_data *data)
{
    // AppleBCMWLANCore::getCOUNTRY_CHANNELS is a fixed zero-fill trap path
    // over a 0x12d8-byte public carrier.
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    memset(reinterpret_cast<uint8_t *>(data), 0, 0x12d8);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getHW_SUPPORTED_CHANNELS(apple80211_sup_channel_data *data)
{
    // Tahoe carries APPLE80211_IOC_HW_SUPPORTED_CHANNELS through the same BSD
    // bridge family as SUPPORTED_CHANNELS. The public carrier is identical on
    // the family side, so route both selectors to the same lifted producer.
    return getSUPPORTED_CHANNELS(data);
}

IOReturn AirportItlwmSkywalkInterface::
getTRAP_CRASHTRACER_MINI_DUMP(apple80211_trap_mini_dump_data *data)
{
    // AppleBCMWLANCore::getTRAP_CRASHTRACER_MINI_DUMP zero-fills the caller
    // blob from +0x4 for 0x19000 bytes. Keep the same public crashtracer blob
    // shape instead of reporting generic unsupported.
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    uint8_t *raw = reinterpret_cast<uint8_t *>(data);
    memset(raw + 0x04, 0, 0x19000);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getBEACON_INFO(apple80211_beacon_info_t *data)
{
    // AppleBCMWLANCore::getBEACON_INFO enters a fixed trap/zero-fill path over
    // a 0x708-byte public carrier. Keep the public buffer contract explicit.
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    memset(reinterpret_cast<uint8_t *>(data), 0, 0x708);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getCHIP_DIAGS(appl80211_chip_diags_data *data)
{
    // AppleBCMWLANCore only opens this diagnostic carrier behind a private
    // owner. Without that owner the WCL query must miss and write nothing;
    // createLQM probes this selector with a 4-byte stack buffer.
    (void)data;
    return static_cast<IOReturn>(0xe00002c7);
}

IOReturn AirportItlwmSkywalkInterface::
getCUR_PMK(apple80211_pmk *)
{
    // AppleBCMWLANCore::getCUR_PMK defaults to 0xe00002c7 and only opens the
    // deeper debug/association subpath behind Apple-private gates. The port
    // must therefore expose the same public fail contract instead of generic
    // unsupported. Returning the documented Apple failure here also keeps
    // the getter credential-safe: it does not snapshot ic_psk into the
    // caller's buffer or otherwise expose raw PMK material to userspace.
    return static_cast<IOReturn>(0xe00002c7);
}

IOReturn AirportItlwmSkywalkInterface::
setCUR_PMK(struct apple80211_pmk *pmk)
{
    // Retained local ABI/private helper. Direct current 25C56 public
    // apple80211setCUR_PMK returns a fixed nonzero status before reading the
    // carrier, so processApple80211Ioctl no longer enters here for normal
    // Tahoe public SET. CIPHER_KEY and the PLTI DeliverPMK user-client path
    // remain separate PMK-ingestion paths and neither is redirected here.
    if (pmk == nullptr) {
        XYLog("setCUR_PMK NOT_READY pmk=NULL\n");
        return kIOReturnBadArgument;
    }
    return installExternalPmkLocked(pmk->apple_pmk_setter_source,
                                    pmk->apple_pmk_key_len,
                                    "CUR_PMK");
}

IOReturn AirportItlwmSkywalkInterface::
installExternalPmkLocked(const uint8_t *pmk_bytes,
                         uint32_t key_len,
                         const char *source_tag)
{
    // Shared local PMK ingestion for CIPHER_KEY and any retained private/ABI
    // setCUR_PMK caller: validate IEEE80211_PMK_LEN, copy the 32-byte PMK
    // into ic->ic_psk, set IEEE80211_F_PSK, refresh WPA/RSN PSK auth state
    // through ieee80211_ioctl_setwpaparms, and emit only non-secret
    // structural markers. This helper is not evidence that the current 25C56
    // public CUR_PMK wrapper performs a PMK install.
    struct ieee80211com *ic = fHalService
        ? fHalService->get80211Controller() : nullptr;
    if (ic == nullptr) {
        XYLog("install_external_pmk NOT_READY source=%s ic=NULL\n",
              source_tag != nullptr ? source_tag : "?");
        return kIOReturnNotReady;
    }
    if (pmk_bytes == nullptr) {
        XYLog("install_external_pmk REJECT_NULL source=%s\n",
              source_tag != nullptr ? source_tag : "?");
        return kIOReturnBadArgument;
    }
    if (key_len != IEEE80211_PMK_LEN) {
        XYLog("install_external_pmk REJECT_LEN source=%s key_len=%u expected=%u\n",
              source_tag != nullptr ? source_tag : "?",
              key_len, (unsigned)IEEE80211_PMK_LEN);
        return kIOReturnBadArgument;
    }
    memcpy(ic->ic_psk, pmk_bytes, sizeof(ic->ic_psk));
    ic->ic_flags |= IEEE80211_F_PSK;
    struct ieee80211_wpaparams wpa;
    memset(&wpa, 0, sizeof(wpa));
    wpa.i_enabled = 1;
    wpa.i_protos  = IEEE80211_WPA_PROTO_WPA1 |
                    IEEE80211_WPA_PROTO_WPA2;
    wpa.i_akms    = IEEE80211_WPA_AKM_PSK |
                    IEEE80211_WPA_AKM_SHA256_PSK;
    ieee80211_ioctl_setwpaparms(ic, &wpa);
    // Mirror the CIPHER_KEY install counter for backward compatibility
    // and bump the matching source-specific counter so an observer can
    // attribute the install to the correct carrier.
    ic->ic_external_pmk_owner = 0;
    __atomic_add_fetch(&setCipherKey_pmk_install_count, 1,
                       __ATOMIC_RELAXED);
    if (source_tag != nullptr && strcmp(source_tag, "CUR_PMK") == 0) {
        __atomic_add_fetch(&setCUR_PMK_pmk_install_count, 1,
                           __ATOMIC_RELAXED);
    }
    return kIOReturnSuccess;
}

void AirportItlwmSkywalkInterface::
clearExternalPmkEligibilityLocked(const char *reason_tag)
{
    // Reset the external-PMK host-supplicant state at lifecycle edges
    // that invalidate any previously delivered PMK: disassociate, leave,
    // setCLEAR_PMKSA_CACHE, association reset, reassociation start, and
    // RSN disable paths. The recovered Apple WCL contract is that PMK
    // ownership does not survive these edges; carrying a stale PMK into
    // a new association attempt would risk a host-supplicant MIC built
    // from a wrong PMK on a fresh edge.
    struct ieee80211com *ic = fHalService
        ? fHalService->get80211Controller() : nullptr;
    if (ic == nullptr) {
        XYLog("clear_external_pmk NOT_READY reason=%s ic=NULL\n",
              reason_tag != nullptr ? reason_tag : "?");
        return;
    }
#if __IO80211_TARGET >= __MAC_26_0
    // Route the ic_psk / IEEE80211_F_PSK / ic_external_pmk_owner
    // reset through the controller command gate so it is mutually
    // exclusive with any in-flight gated PLTI DeliverPMK install,
    // and so the pending PLTI association-target generation is
    // invalidated inside the SAME critical section. The gated
    // airportItlwmCancelAssocAction performs both the pending-target
    // zero AND the ic_psk / IEEE80211_F_PSK / ic_external_pmk_owner
    // clear under one command-gate hold, eliminating the
    // validation-vs-install race between deliverExternalPMK and a
    // concurrent reset edge.
    //
    // The inline ic_psk zero pattern is kept only on the
    // controller-back-pointer-missing fallback path (early bring-up
    // edge before bindController completes) so the host-supplicant
    // reset still happens deterministically even when the PLTI
    // surface is not yet wired up.
    if (instance != nullptr) {
        instance->cancelPendingAssocTarget(
            reason_tag != nullptr ? reason_tag : "clear_external_pmk");
    } else {
        memset(ic->ic_psk, 0, sizeof(ic->ic_psk));
        ic->ic_flags &= ~IEEE80211_F_PSK;
        ic->ic_external_pmk_owner = 0;
    }
#else
    // Older targets: no PLTI producer/cancel path exists, so the
    // inline clear is the only path.
    memset(ic->ic_psk, 0, sizeof(ic->ic_psk));
    ic->ic_flags &= ~IEEE80211_F_PSK;
    ic->ic_external_pmk_owner = 0;
#endif
    // Clear the per-node PMK and PSK-AKM PMK readiness flag on the
    // current bss node if one is bound. The recovered Apple owner
    // contract requires the prior owner state to evaporate at the
    // same reset edge that zeroes ic_psk; otherwise a fresh
    // ieee80211_recv_4way_msg1 on a re-association could read a
    // stale ni_pmk while ic_psk and the owner state are already
    // cleared, producing a PTK from invalid material.
    if (ic->ic_bss != nullptr) {
        memset(ic->ic_bss->ni_pmk, 0, sizeof(ic->ic_bss->ni_pmk));
        ic->ic_bss->ni_flags &= ~IEEE80211_NODE_PMK;
    }
    __atomic_add_fetch(&external_pmk_eligibility_clear_count, 1,
                       __ATOMIC_RELAXED);
}

IOReturn AirportItlwmSkywalkInterface::
getCOUNTRY_CHANNELS_INFO(apple80211_channels_info *data)
{
    return getCHANNELS_INFO(data);
}

IOReturn AirportItlwmSkywalkInterface::
getHP2P_CTRL(apple80211_hp2p_ctrl *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    // AppleBCMWLANCore::getHP2P_CTRL is a commander-backed HP2P/LLW query, but
    // its public fail path is explicit: when the hidden support gate at
    // +0x1510/+0xbf0 says LLW is unsupported, the visible return is
    // 0xe00002c7. The local Tahoe port has no HP2P/LLW owner at all, so that
    // support-missing path is the correct Apple-visible contract here.
    return static_cast<IOReturn>(0xe00002c7);
}

IOReturn AirportItlwmSkywalkInterface::
getSENSING_DATA(apple80211_sensing_data_t *data)
{
    // AppleBCMWLANCore::getSENSING_DATA always writes version=1, then exposes
    // the public fail split 0xe0822801 / 0xe00002c7 depending on deeper hidden
    // feature gates. Keep the stable outer contract here.
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    *reinterpret_cast<uint32_t *>(data) = 1;
    return static_cast<IOReturn>(0xe0822801);
}

IOReturn AirportItlwmSkywalkInterface::
getWCL_EXTENDED_BSS_INFO(apple80211_extended_bss_info *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    // Tahoe Core delegates a valid carrier to NetAdapter, which synchronizes
    // rate/MCS/RSN and optional MLO data. The Intel port has no equivalent
    // producer, so do not acknowledge an output it did not construct.
    (void)data;
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
getTXPOWER(struct apple80211_txpower_data *txd)
{
    if (txd == NULL)
        return kIOReturnBadArgument;
    memset(txd, 0, sizeof(*txd));
    txd->version = APPLE80211_VERSION;
    txd->txpower_unit = APPLE80211_UNIT_MW;
    uint8_t raw = 0;
    if (getTahoeCachedQTxpowerRaw(fHalService, &raw))
        txd->txpower = static_cast<int32_t>(decodeAppleTahoeQTxpowerRaw(raw));
    // AppleBCMWLANCore::getTXPOWER queries the one-byte "qtxpower" carrier and
    // translates it through a fixed __TEXT.__const table. The port now uses a
    // HAL-backed qtxpower cache with the same table-driven public encoding
    // instead of reading the unrelated ic_txpower scalar.
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getRATE(struct apple80211_rate_data *rd)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    if (rd == nullptr)
        return kIOReturnBadArgument;
    if (ic->ic_bss == NULL || ic->ic_state != IEEE80211_S_RUN) {
        // AppleBCMWLANCore::getRATE() returns 0xe0822403 until the BSS manager
        // reports an associated current network. Returning raw 6 here was a
        // local shortcut and confused Tahoe consumers that key off the Apple
        // error code.
        return kApple80211ErrDriverNotAvailable;
    }

    // AppleBCMWLANCore::getRATE only writes the public rate dword at +0x08:
    // it does not initialize version/num_radios. Mirror that carrier shape
    // while sourcing the value from the same normalized transport-rate cache
    // used by the Tahoe nrate-backed MCS/VHT/GI getters.
    return getTahoeCurrentRateMbps(fHalService, &rd->rate[0]);
}

IOReturn AirportItlwmSkywalkInterface::
getBSSID(struct apple80211_bssid_data *bd)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    // Apple's IO80211 framework (FUN_ffffff8002215524) pre-zeroes BSSID and returns success.
    // When not associated, BSSID is all-zero.
    // Returning error here causes airportd to fail during init and abort auto-join.
    memset(bd, 0, sizeof(*bd));
    bd->version = APPLE80211_VERSION;
    if (ic->ic_state == IEEE80211_S_RUN) {
        memcpy(bd->bssid.octet, ic->ic_bss->ni_bssid, APPLE80211_ADDR_LEN);
    }
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getTahoeCompactBSSID(void *data, uint32_t length)
{
    if (data == nullptr || length != APPLE80211_ADDR_LEN)
        return kIOReturnBadArgument;

    uint8_t *bssid = static_cast<uint8_t *>(data);
    struct ieee80211com *ic = fHalService->get80211Controller();

    memset(bssid, 0, APPLE80211_ADDR_LEN);
    if (ic->ic_state == IEEE80211_S_RUN && ic->ic_bss != NULL)
        memcpy(bssid, ic->ic_bss->ni_bssid, APPLE80211_ADDR_LEN);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getHW_ADDR(struct apple80211_hw_mac_address *data)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    // AppleBCMWLANCore::getHW_ADDR writes version=1 followed by the six-byte
    // hardware address, and IO80211Family's
    // WCLDeviceConfiguration::setHwMacAddr(...) consumes the same packed
    // `u32 version + u8[6]` layout. Leaving slot [511] unsupported was
    // therefore an architectural mismatch, not an optional capability.
    memset(data, 0, sizeof(*data));
    data->version = APPLE80211_VERSION;
    IEEE80211_ADDR_COPY(data->hw_addr, ic->ic_myaddr);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getCHIP_POWER_RANGE(apple80211_chip_power_limit *data)
{
    if (data == nullptr)
        return kIOReturnBadArgument;

    memset(data, 0, sizeof(*data));
    data->version = APPLE80211_VERSION;
    // AppleBCMWLANCore::getCHIP_POWER_RANGE is backed by
    // AppleBCMWLANConfigManager::copyWlanPwrDutyCycleTable(...), and that
    // config-manager does not invent zeros. It first looks up the exact
    // `wlan.chip.power.dutycycle` 0x30-byte property on the interface/provider
    // IOService path and, if absent, seeds the same six-entry table from the
    // built-in Tahoe defaults recovered at 0xffffff8001671920. Mirror that
    // property-or-default source here instead of returning synthetic success
    // with an empty payload.
    IORegistryEntry *sources[2] = {
        getTahoeChipPowerRegistryObject(instance),
        getTahoeChipPowerRegistryProvider(instance),
    };
    for (IORegistryEntry *source : sources) {
        if (copyTahoeChipPowerDutyCycle(source, data->wlan_pwr_duty_cycle))
            return kIOReturnSuccess;
    }
    memcpy(data->wlan_pwr_duty_cycle, kAppleTahoeChipPowerDutyCycleFallback,
           sizeof(kAppleTahoeChipPowerDutyCycleFallback));
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getLEAKY_AP_STATS_MODE(apple80211_leaky_ap_setting *)
{
    // Broadcom-private diagnostics surface. Keep it on the explicit
    // unsupported contract instead of advertising a shared Apple80211
    // producer that the recovered reference does not expose publicly.
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
getRANGING_ENABLE(apple80211_ranging_enable_request_t *)
{
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
getRANGING_START(apple80211_ranging_start_request_t *)
{
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
getTRAP_INFO(apple80211_trap_info_data *)
{
    // Trap/debug diagnostics surface. Keep the explicit unsupported contract
    // until a real public producer is recovered.
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
getRANGING_CAPS(apple80211_ranging_capabilities_t *)
{
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
getHE_COUNTERS(apple80211_he_counters_ctl *)
{
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
getWCL_WNM_OFFLOAD(apple80211_wcl_wnm_offload_t *)
{
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
getWIFI_NOISE_PER_ANT(apple80211_noise_per_ant_t *)
{
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
getFW_CLOCK_INFO(apple80211_fw_clock_info *)
{
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
getTIMESYNC_STATS(apple80211_timesync_stats *)
{
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
getSMARTCCA_OPMODE(apple80211_smartcca_opmode *)
{
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
getLQM_STATISTICS(apple80211_lqm_statistics *)
{
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
getCHIP_COUNTER_STATS(apple80211_chip_stats *)
{
    // AppleBCMWLANCore::getCHIP_COUNTER_STATS does not fall back to the generic
    // Tahoe unsupported path. The recovered public contract returns the fixed
    // Apple error 0xe00002e6 after a chip-generation gate, so leaving slot
    // [486] on kIOReturnUnsupported would advertise the wrong failure shape to
    // the caller even when no Broadcom-private stats producer is present.
    return static_cast<IOReturn>(0xe00002e6);
}

IOReturn AirportItlwmSkywalkInterface::
getVHT_CAPABILITY(struct apple80211_vht_capability *data)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    // AppleBCMWLANCore::getVHT_CAPABILITY does not use a generic unsupported
    // path. It returns 0x2d when the PHY gate rejects VHT, and otherwise
    // copies a 14-byte VHT capability IE body into the packed
    // apple80211_vht_capability payload after `version`. Our local ABI now
    // mirrors that recovered contract, so Tahoe should expose the same IE
    // shape instead of leaving slot [484] on kIOReturnUnsupported.
    if (data == nullptr)
        return kIOReturnBadArgument;
    if (hasCachedVhtCapability) {
        memcpy(data, &cachedVhtCapability, sizeof(*data));
        return kIOReturnSuccess;
    }
    if ((ic->ic_flags & IEEE80211_F_VHTON) == 0 || ic->ic_vhtcaps == 0)
        return 45;

    memset(data, 0, sizeof(*data));
    data->version = APPLE80211_VERSION;
    data->ie = IEEE80211_ELEMID_VHT_CAP;
    data->len = sizeof(struct ieee80211_ie_vhtcap) - 2;
    data->vht_cap_info = ic->ic_vhtcaps;
    data->rx_mcs_map = ic->ic_vht_rx_mcs_map;
    data->rx_highest = ic->ic_vht_rx_highest;
    data->tx_mcs_map = ic->ic_vht_tx_mcs_map;
    data->tx_highest = ic->ic_vht_tx_highest;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getHT_CAPABILITY(struct apple80211_ht_capability *data)
{
    struct ieee80211com *ic = fHalService->get80211Controller();

    // Remote otool on AppleBCMWLANCoreMac shows getHT_CAPABILITY is a real
    // producer: it first refreshes cached HT/VHT capability state and then
    // copies a contiguous 0x1c-byte HT capability IE body into caller
    // offsets +0x4..+0x1f. Our 802.11 stack already carries the same source
    // fields in `ieee80211com`, and the local `ieee80211_add_htcaps()`
    // generator writes them in the same order, so slot [481] must expose that
    // packed carrier instead of generic unsupported.
    if (data == nullptr)
        return kApple80211ErrInvalidArgumentRaw;
    if (hasCachedHtCapability) {
        memcpy(data, &cachedHtCapability, sizeof(*data));
        return kIOReturnSuccess;
    }

    memset(data, 0, sizeof(*data));
    data->version = APPLE80211_VERSION;
    data->hc_id = IEEE80211_ELEMID_HTCAPS;
    data->hc_len = sizeof(struct ieee80211_ie_htcap) - 2;
    data->hc_cap = ic->ic_htcaps;
    data->hc_param = ic->ic_ampdu_params;
    memcpy(data->hc_mcsset, ic->ic_sup_mcs, sizeof(ic->ic_sup_mcs));
    LE_WRITE_2(data->hc_mcsset + 10, ic->ic_max_rxrate & IEEE80211_MCS_RX_RATE_HIGH);
    data->hc_mcsset[12] = ic->ic_tx_mcs_set;
    data->hc_extcap = ic->ic_htxcaps;
    data->hc_txbf = ic->ic_txbfcaps;
    data->hc_antenna = ic->ic_aselcaps;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getHE_CAPABILITY(struct apple80211_he_capability *data)
{
    struct ieee80211com *ic = fHalService->get80211Controller();

    // AppleBCMWLANCore::getHE_CAPABILITY is not an unsupported slot. It
    // returns 0x2d when the PHY/capability gate rejects HE and otherwise
    // writes only three discontiguous fields inside a 0x24-byte opaque
    // carrier. Keep that shape intact instead of inventing a semantic HE IE
    // schema that the local headers never recovered.
    if (data == nullptr)
        return kIOReturnBadArgument;
    if ((ic->ic_flags & IEEE80211_F_HEON) == 0)
        return 45;

    memset(data, 0, sizeof(*data));
    data->version = APPLE80211_VERSION;
    data->capability_word = 0x0b00;
    data->capability_byte = 0x26;
    data->capability_tail = 0xfffafffafffafffaULL;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getGUARD_INTERVAL(apple80211_guard_interval_data *data)
{
    if (data == nullptr)
        return static_cast<IOReturn>(0xe00002c2);

    memset(data, 0, sizeof(*data));
    data->version = APPLE80211_VERSION;
    // AppleBCMWLANCore::getGUARD_INTERVAL reads cached "nrate" through the
    // same config path as getMCS_VHT. Accepted query statuses (success and
    // 0xe00002e3) decode the interval from the nrate word; other query errors
    // are returned unchanged without rebuilding the value from peer capability
    // flags.
    return fillTahoeGuardIntervalFromCachedNrate(fHalService, data);
}

IOReturn AirportItlwmSkywalkInterface::
getP2P_DEVICE_CAPABILITY(apple80211_p2p_device_capability *data)
{
    // AppleBCMWLANCore::getP2P_DEVICE_CAPABILITY zeroes the one-byte carrier
    // and only defers into AppleBCMWLANNANInterface when a NAN owner exists.
    // This port currently has no NAN object at all, so the Apple-shaped path
    // is the zeroed fast-path, not generic unsupported.
    if (data == nullptr)
        return kIOReturnBadArgument;

    data->capability = 0;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getPRIVATE_MAC(apple80211_private_mac_data *data)
{
    // Tahoe reads this carrier through BGScanAdapter and the private
    // "scanmac" IOVAR. This port has neither producer, so do not manufacture
    // a zero success result for a valid request.
    if (data == nullptr)
        return kApple80211ErrInvalidArgumentRaw;

    (void)data;
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
getTHERMAL_INDEX(apple80211_thermal_index_t *data)
{
    // Tahoe writes caller +4 from its thermal Core state. This port has neither
    // that state lifecycle nor the `tvpm` producer path, so do not report a
    // zero success carrier for a valid request.
    if (data == nullptr)
        return kIOReturnBadArgument;

    (void)data;
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
getPOWER_BUDGET(apple80211_power_budget_t *data)
{
    // Tahoe reads caller +4 from Core state populated through a `tvpm`
    // lifecycle. This port has neither that state lifecycle nor a producer,
    // so do not publish a default-only cache as a successful carrier.
    if (data == nullptr)
        return kIOReturnBadArgument;

    (void)data;
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
getOFFLOAD_TCPKA_ENABLE(apple80211_offload_tcpka_enable_t *data)
{
    // Remote otool recovered the full getter body:
    // - NULL -> 0xe00002c2
    // - feature/config gate failure -> 0xe00002c7
    // - keepalive object present -> write enabled flag at caller +0x4
    //
    // Tahoe therefore needs the exact carrier ABI here even before the deeper
    // keepalive object path is fully lifted. Returning the Apple unsupported
    // code when the local keepalive producer is absent is still materially more
    // correct than leaving slot [504] on generic kIOReturnUnsupported with the
    // wrong payload type.
    if (data == nullptr)
        return static_cast<IOReturn>(0xe00002c2);
    if (!cachedTcpkaOffloadSupported)
        return static_cast<IOReturn>(0xe00002c7);

    memset(data, 0, sizeof(*data));
    data->version = APPLE80211_VERSION;
    data->enabled = cachedTcpkaOffloadEnabled ? 1U : 0U;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getLQM_CONFIG(apple80211_lqm_config_t *data)
{
    // Tahoe checks for its AppleBCMWLANLQM owner before touching the caller
    // output and returns 0xe00002bc when that owner is absent. The Intel
    // telemetry timer is not that public configuration owner, so do not
    // synthesize a config carrier from a local default.
    (void)data;
    return kIOReturnError;
}

IOReturn AirportItlwmSkywalkInterface::
getLQM_SUMMARY(apple80211_lqm_summary *data)
{
    // IO80211LQMData::getLQM_SUMMARY simply zeroes a fixed 0x15a0-byte caller
    // buffer. Tahoe should therefore export the same summary blob ABI instead
    // of leaving slot [520] on generic unsupported.
    if (data == nullptr)
        return kIOReturnBadArgument;

    memset(data, 0, sizeof(*data));
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getDBG_GUARD_TIME_PARAMS(apple80211_dbg_guard_time_params *data)
{
    if (data == nullptr)
        return static_cast<IOReturn>(0xe00002c2);

    // Tahoe reads this reply through a private command transport. Intel has no
    // corresponding owner or transport, so do not synthesize output from a
    // setter-side cache. The local null guard is retained as a safety boundary.
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
getAWDL_RSDB_CAPS(apple80211_rsdb_capability *data)
{
    if (data == nullptr)
        return static_cast<IOReturn>(0xe00002c2);

    // Tahoe reads an opaque capability window from RSDB Core state with
    // observed ConfigManager/`rsdb` producer context. This port has no
    // matching lifecycle, so do not publish a default-only cache as success.
    (void)data;
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
getTKO_PARAMS(apple80211_tko_params *data)
{
    if (!cachedTcpkaOffloadSupported)
        return kIOReturnBadArgumentTahoe;
    if (data == nullptr)
        return kIOReturnSuccess;

    // Tahoe exposes six consecutive u32 fields at caller +0x4..+0x18 when the
    // keepalive owner exists; otherwise it returns 0xe00002bc. A NULL output
    // pointer is success after the owner gate. The local port preserves the
    // same public carrier from cached state.
    uint8_t *raw = reinterpret_cast<uint8_t *>(data);
    memset(raw, 0, 0x1c);
    for (size_t i = 0; i < 6; i++)
        *reinterpret_cast<uint32_t *>(raw + 4 + i * sizeof(uint32_t)) = cachedTkoParams[i];
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getTKO_DUMP(apple80211_tko_dump *)
{
    // AppleBCMWLANCore::getTKO_DUMP returns 0xe00002bc when the keepalive
    // owner at +0x15a8 is absent. This port does not yet lift that hidden
    // owner, so the exact Tahoe fail shape is more correct than generic
    // unsupported.
    return kIOReturnBadArgumentTahoe;
}

IOReturn AirportItlwmSkywalkInterface::
getBTCOEX_PROFILE(apple80211_btcoex_profile *)
{
    // AppleBCMWLANCore::getBTCOEX_PROFILE exposes the fixed Tahoe fail
    // 0xe00002c2 rather than kIOReturnUnsupported.
    return static_cast<IOReturn>(0xe00002c2);
}

IOReturn AirportItlwmSkywalkInterface::
getBTCOEX_PROFILE_ACTIVE(apple80211_btcoex_profile_active_data *data)
{
    if (data == nullptr)
        return static_cast<IOReturn>(0xe00002c2);

    // Reference Core fetches this dword through its commander and propagates
    // the transport result. There is no matching local GET producer, so do not
    // report the reset-only cache as live coexistence state.
    (void)data;
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
getMAX_NSS_FOR_AP(apple80211_btcoex_max_nss_for_ap_data *data)
{
    if (data == nullptr)
        return static_cast<IOReturn>(0xe00002c2);

    uint8_t *raw = reinterpret_cast<uint8_t *>(data);
    memset(raw, 0, 8);
    *reinterpret_cast<uint32_t *>(raw + 4) = static_cast<uint32_t>(fHalService->getDriverInfo()->getTxNSS());
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getBTCOEX_2G_CHAIN_DISABLE(apple80211_btcoex_2g_chain_disable *data)
{
    if (data == nullptr)
        return static_cast<IOReturn>(0xe00002c2);

    // Reference Core obtains this two-byte result through its commander and
    // propagates the transport status. There is no matching local GET
    // producer, so do not expose the reset-only cache as coexistence state.
    (void)data;
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
getBSS_BLACKLIST(bss_blacklist *data)
{
#if __IO80211_TARGET >= __MAC_26_0
    (void)data;
    if (instance == nullptr)
        return static_cast<IOReturn>(
            TahoeBssBlacklistContracts::kClassOwnerAbsentStatus);

    // AppleBCMWLANCore ignores the synchronous caller buffer and launches an
    // async lower-owner query. Its callback publishes message 0xa3 only when
    // the applied list is non-empty.
    return instance->queryBssBlacklistOwner();
#else
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;
    memset(data, 0, sizeof(cachedBssBlacklist));
    if (hasCachedBssBlacklist)
        memcpy(data, cachedBssBlacklist, sizeof(cachedBssBlacklist));
    return kIOReturnSuccess;
#endif
}

IOReturn AirportItlwmSkywalkInterface::
getTXRX_CHAIN_INFO(apple80211_txrx_chain_info *data)
{
    if (data == nullptr)
        return static_cast<IOReturn>(0xe00002c2);

    ItlDriverInfo *driverInfo = fHalService->getDriverInfo();
    const uint8_t txMask = driverInfo->getTxChainMask();
    const uint8_t rxMask = driverInfo->getRxChainMask();
    const TahoeTxRxChainContracts::Carrier carrier =
        TahoeTxRxChainContracts::build(rxMask, txMask, txMask, rxMask);
    memcpy(data, &carrier, sizeof(carrier));
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getMIMO_STATUS(apple80211_mimo_status *data)
{
    if (data == nullptr)
        return static_cast<IOReturn>(0xe00002c2);

    // Tahoe Core gates this selector on feature 0x2c and, when enabled, reads
    // `mimo_ps_status` through its Commander. The Intel port has no matching
    // MIMO owner or IOVAR backend, so do not fabricate a zeroed status view.
    (void)data;
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
getWCL_FW_HOT_CHANNELS(apple80211_fw_hot_channels *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    // Reference Core delegates this carrier to NetAdapter hot-channel state
    // and firmware transport. There is no matching local owner, so do not
    // fabricate a four-byte successful telemetry result.
    (void)data;
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
getWCL_TRAFFIC_COUNTERS(apple80211_wcl_traffic_counters *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    // Tahoe Core derives these seven counters from traffic owners, Core
    // counters, the real-time NAN TX reader, and continuous time. The Intel
    // port has none of those WCL sources behind this getter, so a zeroed
    // snapshot would acknowledge telemetry that was never collected.
    (void)data;
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
getRSN_XE(apple80211_rsn_xe_data *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    // Reference Core delegates this carrier to JoinAdapter association state.
    // There is no matching local owner, so do not expose a reset-only cache as
    // an applied RSNXE association result.
    (void)data;
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
getSIB_COEX_STATUS(apple80211_sib_coex_status *data)
{
    if (data == nullptr)
        return static_cast<IOReturn>(kApple80211ErrInvalidArgumentRaw);

    // Tahoe reads two live Core-state dwords; the local version/zero carrier
    // had no matching producer. Do not confuse legacy BTCOEX state with those
    // fields or acknowledge a snapshot that was never read.
    (void)data;
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
getWCL_LOW_LATENCY_INFO_STATS(apple80211_wcl_low_latency_stats *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    // The 25C56 Core getter fills all 0x7c bytes from its low-latency and
    // traffic-monitor owners. This port has no equivalent producer, so do not
    // acknowledge a synthetic all-zero snapshot.
    (void)data;
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
getDYNSAR_DETAIL(apple80211_dynsar_detail *data)
{
    auto *raw = reinterpret_cast<tahoeDynsarDetailRequest *>(data);
    if (raw == nullptr || raw->entry_index >= 4)
        return static_cast<IOReturn>(kApple80211ErrInvalidArgumentRaw);

    // The reference getter reads TxPowerManager-owned detail state and copies
    // a real 0x2d00-byte report. There is no matching local producer, so do
    // not acknowledge the former reset-only cache as a DynSAR snapshot.
    (void)data;
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
getSLOW_WIFI_FEATURE_ENABLED(apple80211_slow_wifi_feature_enabled *data)
{
    auto *raw = reinterpret_cast<tahoeSlowWifiFeatureEnabled *>(data);
    if (raw == nullptr)
        return static_cast<IOReturn>(0xe00002c2);

    // AppleBCMWLANCore writes only the enabled dword at +0x04. The framework
    // owns the carrier version field at +0x00, so preserve it verbatim.
    raw->enabled =
        (instance != nullptr &&
         instance->getTahoeOwnerRegistry().isSlowWifiFeatureEnabled())
            ? 1U
            : 0U;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getWCL_LOW_LATENCY_INFO(apple80211_low_latency_info *data)
{
    auto *raw = reinterpret_cast<tahoeLowLatencyInfo *>(data);
    if (raw == nullptr)
        return kIOReturnBadArgumentTahoe;

    // Apple returns zeros with success when the low-latency owner is absent,
    // and otherwise exposes exactly `u8 enabled, u8 powersave, u16 window`.
    if (instance == nullptr) {
        memset(raw, 0, sizeof(*raw));
        return kIOReturnSuccess;
    }

    const auto &owner = instance->getTahoeOwnerRegistry().qosDynsar;
    raw->enabled = owner.lowLatencyEnabled;
    raw->power_save = owner.lowLatencyPowerSave;
    raw->window = owner.lowLatencyWindow;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getWCL_GET_TX_BLANKING_STATUS(uint *data)
{
    // Apple accepts NULL and simply skips the store. Preserve that visible
    // contract instead of forcing an argument error.
    if (data != nullptr) {
        *data =
            (instance != nullptr &&
             instance->getTahoeOwnerRegistry().isTxBlankingStatusEnabled())
                ? 1U
                : 0U;
    }
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getSYSTEM_SLEEP_CONFIG(apple80211_system_sleep_config *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    // Apple only succeeds when the Bonjour-offload owner exists, then augments
    // the result via the hidden +0x1510 callback at slot +0x850. The local
    // port still has no Bonjour-offload owner at all, so the exact visible
    // Tahoe contract here is the owner-missing fail 0xe00002bc.
    return kIOReturnBadArgumentTahoe;
}

IOReturn AirportItlwmSkywalkInterface::
setWOW_TEST(apple80211_wow_test_data *data)
{
    auto *raw = reinterpret_cast<uint8_t *>(data);
    if (raw == nullptr)
        return static_cast<IOReturn>(0xe00002c2);

    uint32_t mode = *reinterpret_cast<uint32_t *>(raw + 4);
    if (mode < 1 || mode > 600)
        return static_cast<IOReturn>(0xe00002c2);

    // A valid request reaches a retrying firmware wake-test owner in Tahoe.
    // The port has no equivalent owner or transport, so it must not enable WoW
    // or report a successful test setup.
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setHT_CAPABILITY(apple80211_ht_capability *data)
{
    if (data == nullptr)
        return static_cast<IOReturn>(kApple80211ErrInvalidArgumentRaw);

    // AppleBCMWLANCore copies the contiguous 0x1c-byte HT capability IE body
    // into core state. Keep the same cached public carrier so later getter
    // reads can observe the last programmed payload.
    memcpy(&cachedHtCapability, data, sizeof(cachedHtCapability));
    hasCachedHtCapability = true;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setVHT_CAPABILITY(apple80211_vht_capability *data)
{
    if (data == nullptr)
        return static_cast<IOReturn>(kApple80211ErrInvalidArgumentRaw);

    struct ieee80211com *ic = fHalService ? fHalService->get80211Controller() : nullptr;
    if (ic == nullptr || (ic->ic_vhtcaps & 0x80) == 0)
        return 45;

    memcpy(&cachedVhtCapability, data, sizeof(cachedVhtCapability));
    hasCachedVhtCapability = true;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setPOWER_BUDGET(apple80211_power_budget_t *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    // The reference gates its firmware power-budget owner by feature bit 0x3b
    // and accepts only the inclusive public range 1..100.
    if (((cachedOSFeatureFlags >> 0x3b) & 1ULL) == 0)
        return kIOReturnBadArgumentTahoe;
    if (data->power_budget == 0 || data->power_budget >= 101)
        return kIOReturnBadArgumentTahoe;

    // A valid request reaches a firmware-backed owner in Tahoe. The port has
    // no equivalent owner or transport, so do not acknowledge a cache-only
    // policy update.
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setUSB_HOST_NOTIFICATION(apple80211_usb_host_notification_data *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    // Apple performs a hidden-owner transition followed by real Broadcom
    // commander IOVAR traffic. The Intel port has neither backend, so it must
    // not acknowledge a carrier after only synthetic local bookkeeping.
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setBYPASS_TX_POWER_CAP(apple80211_bypass_tx_power_cap *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    // Apple immediately sends txcapstate through its firmware owner. The
    // local Tahoe commander only records a synthetic completion and has no
    // matching Intel transport, so success here would be a false capability.
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setTRAFFIC_ENG_PARAMS(apple80211_traffic_eng_params *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    // Apple only accepts this selector when core-private +0x7584 bit 0 is set;
    // otherwise the public contract is a direct 0xe00002c7. The local owner
    // registry carries that recovered feature byte without enabling the deeper
    // traffic-engine backend.
    if (instance == nullptr ||
        !instance->getTahoeOwnerRegistry().isCongestionControlSupported()) {
        return static_cast<IOReturn>(TahoeQosDynsarContracts::kUnsupportedStatus);
    }

    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getRSSI(struct apple80211_rssi_data *rd)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    if (ic->ic_bss == NULL) {
        // Apple delegates RSSI reads to IO80211BssManager::getCurrentRSSI(),
        // which returns 0xe0822403 when there is no current BSS. Tahoe should
        // not collapse that state into raw POSIX 6.
        return kApple80211ErrDriverNotAvailable;
    }
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

IOReturn AirportItlwmSkywalkInterface::
getRSN_IE(struct apple80211_rsn_ie_data *data)
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

IOReturn AirportItlwmSkywalkInterface::
setRSN_IE(struct apple80211_rsn_ie_data *data)
{
    (void)data;
    return static_cast<IOReturn>(
        TahoeAssociationContracts::kPublicSetRsnIeReturn);
}

IOReturn AirportItlwmSkywalkInterface::
getAP_IE_LIST(struct apple80211_ap_ie_data *data)
{
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
}

IOReturn AirportItlwmSkywalkInterface::
getNOISE(struct apple80211_noise_data *nd)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    if (nd == NULL)
        return kIOReturnBadArgument;
    if (ic->ic_bss == NULL)
        return kApple80211ErrDriverNotAvailable;

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

IOReturn AirportItlwmSkywalkInterface::
getPOWERSAVE(struct apple80211_powersave_data *pd)
{
    pd->version = APPLE80211_VERSION;
    // AppleBCMWLANCore::getPOWERSAVE on Tahoe reads back a cached power-save
    // mode from core state instead of reporting the IOC as unsupported.  Live
    // 26.3 logs showed WCL sending APPLE80211_IOC_POWERSAVE with level 7 very
    // early during bring-up; our old kIOReturnUnsupported path made WCL log
    // "arg->powersave_level = 7 not supported" and diverged from the reference
    // contract before scan-complete delivery even mattered.
    pd->powersave_level = cachedPowersaveLevel;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setPOWERSAVE(struct apple80211_powersave_data *pd)
{
    if (pd == NULL)
        return kIOReturnBadArgument;

    // Recovered Apple contract:
    // - Tahoe WCL expects APPLE80211_IOC_POWERSAVE to succeed during startup.
    // - AppleBCMWLANCore::getPOWERSAVE returns the last accepted 32-bit level.
    // Mirroring that 1:1 externally means accepting the requested policy here
    // and returning the same cached value from getPOWERSAVE later.
    cachedPowersaveLevel = pd->powersave_level;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getNSS(struct apple80211_nss_data *data)
{
    memset(data, 0, sizeof(*data));
    data->version = APPLE80211_VERSION;
    data->nss = fHalService->getDriverInfo()->getTxNSS();
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setASSOCIATE(struct apple80211_assoc_data *ad)
{
    RT2_SET(3); sRT.assocCount++;
    if (airportItlwmRegDiagShouldBlock(kAirportItlwmRegDiagBlockPublicAssoc)) {
        airportItlwmRegDiagRecordBlock(kAirportItlwmRegDiagBlockPublicAssoc,
                                       kAirportItlwmRegDiagPathPublicAssoc,
                                       ad != nullptr ? ad->ad_ssid_len : 0);
        airportItlwmRegDiagRecordAssoc(kAirportItlwmRegDiagPathPublicAssoc,
                                       ad != nullptr ? ad->ad_ssid : nullptr,
                                       ad != nullptr ? ad->ad_ssid_len : 0,
                                       ad != nullptr ? reinterpret_cast<const uint8_t *>(&ad->ad_bssid) : nullptr,
                                       ad != nullptr ? ad->ad_auth_lower : 0,
                                       ad != nullptr ? ad->ad_auth_upper : 0,
                                       ad != nullptr ? ad->ad_rsn_ie_len : 0,
                                       kIOReturnUnsupported);
        return kIOReturnUnsupported;
    }

    struct apple80211_authtype_data auth_type_data;
    struct ieee80211com *ic = fHalService->get80211Controller();

    if (!ad) {
        airportItlwmRegDiagRecordAssoc(kAirportItlwmRegDiagPathPublicAssoc,
                                       nullptr, 0, nullptr, 0, 0, 0,
                                       kIOReturnError);
        return kIOReturnError;
    }

    if (ic->ic_state < IEEE80211_S_SCAN) {
        XYLog("DEBUG %s SKIP: ic_state=%d < SCAN\n", __FUNCTION__, ic->ic_state);
        airportItlwmRegDiagRecordAssoc(kAirportItlwmRegDiagPathPublicAssoc,
                                       ad->ad_ssid, ad->ad_ssid_len,
                                       reinterpret_cast<const uint8_t *>(&ad->ad_bssid),
                                       ad->ad_auth_lower, ad->ad_auth_upper,
                                       ad->ad_rsn_ie_len, kIOReturnSuccess);
        return kIOReturnSuccess;
    }

    if (ic->ic_state == IEEE80211_S_ASSOC || ic->ic_state == IEEE80211_S_AUTH) {
        XYLog("DEBUG %s SKIP: ic_state=%d (already in ASSOC/AUTH)\n", __FUNCTION__, ic->ic_state);
        airportItlwmRegDiagRecordAssoc(kAirportItlwmRegDiagPathPublicAssoc,
                                       ad->ad_ssid, ad->ad_ssid_len,
                                       reinterpret_cast<const uint8_t *>(&ad->ad_bssid),
                                       ad->ad_auth_lower, ad->ad_auth_upper,
                                       ad->ad_rsn_ie_len, kIOReturnSuccess);
        return kIOReturnSuccess;
    }

    if (ad->ad_mode != APPLE80211_AP_MODE_IBSS) {
        disassocIsVoluntary = false;
        auth_type_data.version = APPLE80211_VERSION;
        auth_type_data.authtype_upper = ad->ad_auth_upper;
        auth_type_data.authtype_lower = ad->ad_auth_lower;
        setAUTH_TYPE(&auth_type_data);
        const uint16_t rsnIeLen = static_cast<uint16_t>(ad->ad_rsn_ie[1] + 2);
        storeAssocRsnIeOverride(ic, ad->ad_rsn_ie, rsnIeLen);

        associateSSID(ad->ad_ssid, ad->ad_ssid_len, ad->ad_bssid, ad->ad_auth_lower, ad->ad_auth_upper, ad->ad_key.key, ad->ad_key.key_len, ad->ad_key.key_index, true, false);
    }
    airportItlwmRegDiagRecordAssoc(kAirportItlwmRegDiagPathPublicAssoc,
                                   ad->ad_ssid, ad->ad_ssid_len,
                                   reinterpret_cast<const uint8_t *>(&ad->ad_bssid),
                                   ad->ad_auth_lower, ad->ad_auth_upper,
                                   ad->ad_rsn_ie_len, kIOReturnSuccess);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setDISASSOCIATE(void *ad)
{
    RT2_SET(7);
    struct ieee80211com *ic = fHalService->get80211Controller();

    // External PMK eligibility does not survive any disassociate
    // edge that this selector represents, including the early-return
    // sub-paths where the function returns before publishing a deauth
    // or before resetting net80211 ESS state. Clear unconditionally
    // on entry so a stale PMK does not survive into the next
    // association attempt regardless of which sub-path the
    // disassociate edge takes from here. The clear logs only a
    // credential-safe reason marker and never the PMK bytes.
    clearExternalPmkEligibilityLocked("setDISASSOCIATE");

    if (ic->ic_state < IEEE80211_S_SCAN) {
        XYLog("DEBUG %s SKIP: ic_state=%d < SCAN\n", __FUNCTION__, ic->ic_state);
        return kIOReturnSuccess;
    }

    if (instance != nullptr && instance->getBssManager() != nullptr)
        instance->getBssManager()->setAdHocCreated(false);

    if (ic->ic_state > IEEE80211_S_AUTH && ic->ic_bss != NULL)
        IEEE80211_SEND_MGMT(ic, ic->ic_bss, IEEE80211_FC0_SUBTYPE_DEAUTH, IEEE80211_REASON_AUTH_LEAVE);

    if (ic->ic_state == IEEE80211_S_ASSOC || ic->ic_state == IEEE80211_S_AUTH) {
        XYLog("DEBUG %s SKIP: ic_state=%d (ASSOC/AUTH)\n", __FUNCTION__, ic->ic_state);
        return kIOReturnSuccess;
    }
    
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

IOReturn AirportItlwmSkywalkInterface::
getSUPPORTED_CHANNELS(struct apple80211_sup_channel_data *ad)
{
    if (!ad)
        return kIOReturnError;
    ad->version = APPLE80211_VERSION;
    ad->num_channels = 0;
    struct ieee80211com *ic = fHalService->get80211Controller();
    for (int i = 0; i < IEEE80211_CHAN_MAX; i++) {
        if (ic->ic_channels[i].ic_freq != 0) {
            ad->supported_channels[ad->num_channels].channel = ieee80211_chan2ieee(ic, &ic->ic_channels[i]);
            ad->supported_channels[ad->num_channels].flags = ieeeChanFlag2appleScanFlagVentura(ic->ic_channels[i].ic_flags);
            ad->num_channels++;
        }
    }
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getLOCALE(struct apple80211_locale_data *ld)
{
    if (!ld)
        return kIOReturnError;
    ld->version = APPLE80211_VERSION;
    ld->locale  = APPLE80211_LOCALE_FCC;
    
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getDEAUTH(struct apple80211_deauth_data *da)
{
    if (!da)
        return kIOReturnError;
    da->version = APPLE80211_VERSION;
    struct ieee80211com *ic = fHalService->get80211Controller();
    da->deauth_reason = ic->ic_deauth_reason;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getASSOCIATION_STATUS(struct apple80211_assoc_status_data *hv)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    
    if (!hv)
        return kIOReturnError;
    memset(hv, 0, sizeof(*hv));
    hv->version = APPLE80211_VERSION;
    if (ic->ic_state == IEEE80211_S_RUN) {
        hv->status = APPLE80211_STATUS_SUCCESS;
#ifdef USE_APPLE_SUPPLICANT
    } else {
        // Under the Apple supplicant contract the 802.11 core already tracks
        // the last auth/assoc status code in ic_assoc_status. Tahoe must not
        // collapse that back to UNAVAILABLE or CoreWiFi loses the real reason
        // coming from the association response path.
        hv->status = ic->ic_assoc_status;
    }
#else
    } else {
        hv->status = APPLE80211_STATUS_UNAVAILABLE;
    }
#endif
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setCLEAR_PMKSA_CACHE(void *req)
{
    (void)req;
    struct ieee80211com *ic = fHalService->get80211Controller();
    // PMKSA-cache clear semantically invalidates any held host PMK,
    // so drop external PMK eligibility before touching the node
    // cache. The clear is credential-safe and only emits structural
    // markers naming the reset reason.
    clearExternalPmkEligibilityLocked("setCLEAR_PMKSA_CACHE");
    //if doing background or active scan, don't free nodes.
    if ((ic->ic_flags & IEEE80211_F_BGSCAN) || (ic->ic_flags & IEEE80211_F_ASCAN))
        return kIOReturnSuccess;
    ieee80211_free_allnodes(ic, 0);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setDEAUTH(struct apple80211_deauth_data *da)
{
    // This public carrier is not the void DISASSOCIATE lifecycle. Without a
    // DEAUTH owner, do not acknowledge a reason/BSSID request that was not
    // applied to state or management transport.
    (void)da;
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
getMCS(struct apple80211_mcs_data* md)
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

/*
 * apple80211_link_changed_event_data response producer.
 *
 * The Tahoe userspace consumer reads voluntary at +0x1c (link-down)
 * or +0x1d (link-up) and reads either apple80211_link_down_reason
 * or the link-up RSSI from the +0x04 union, so this routine must
 * populate the 32-byte response shape:
 *   - on link-down: voluntary_down from the locally-tracked disassoc
 *     initiation flag, reason = DEAUTH (the BEACONLOST reason needs
 *     a HAL-side beacon-loss teardown signal not produced by this
 *     layer), and the current BSSID copied into the first six bytes
 *     of last_assoc so the upper layer can identify the dropped
 *     network.
 *   - on link-up: voluntary_up is 1 because every itlwm STA path
 *     reaches RUN through an explicit setASSOCIATE / setSCAN_REQ
 *     join sequence; rssi published from the current node.
 * snr / nf are populated when the HAL exposes a valid scalar
 * noise-floor sample; CCA remains zero because itlwm does not
 * currently expose channel CCA metrics from the iwx / iwm HAL.
 */
IOReturn AirportItlwmSkywalkInterface::
getLINK_CHANGED_EVENT_DATA(struct apple80211_link_changed_event_data *ed)
{
    if (ed == nullptr)
        return 16;

    struct ieee80211com *ic = fHalService->get80211Controller();

    bzero(ed, sizeof(apple80211_link_changed_event_data));
    ed->isLinkDown = !(instance->currentStatus & kIONetworkLinkActive);
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

IOReturn AirportItlwmSkywalkInterface::
setSCAN_REQ(struct apple80211_scan_data *sd)
{
    RT2_SET(2); sRT.scanReqCount++;
    struct ieee80211com *ic = fHalService->get80211Controller();
    if (fScanResultWrapping)
        return 22;
    if (ic->ic_state <= IEEE80211_S_INIT)
        return 22;

    /*
     * Reset SCAN_RESULT iterator — airportd/IO80211 framework reads
     * SCAN_RESULT immediately after SCAN_REQ returns success.  If
     * ic_tree already has nodes from the internal scan, the framework
     * can iterate them right away.  Without this reset, a stale
     * fScanResultWrapping=true from a previous cycle causes SCAN_RESULT
     * to return 5 ("end") before returning any nodes.
     */
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

IOReturn AirportItlwmSkywalkInterface::
setWCL_TRIGGER_CC(triggerCC *data)
{
    if (!data)
        return kIOReturnBadArgument;

    const uint8_t *raw = reinterpret_cast<const uint8_t *>(data);
    const uint32_t mode = *reinterpret_cast<const uint32_t *>(raw + 0x8);

    if (mode != 0 && mode != 1)
        return kIOReturnBadArgumentTahoe;

    // Tahoe routes the valid modes into distinct Scan/Join adapter pipelines.
    // The Intel cache formerly written here had no consumer and did not run
    // either pipeline, so it must not acknowledge the request as applied.
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setOS_FEATURE_FLAGS(apple80211_feature_flags *data)
{
    if (!data)
        return kIOReturnBadArgumentTahoe;

    const uint64_t flags = *reinterpret_cast<const uint64_t *>(data);

    // AppleBCMWLANCore::setOS_FEATURE_FLAGS is not an ack-only slot. It first
    // persists the incoming 64-bit word, then derives multiple cached booleans
    // and fans out follow-up configuration (DynSAR, 6G, KVR, AOP scan-forward,
    // adaptive 11r). We do not collapse that producer path back into an inline
    // success stub; the raw feature word must remain driver-owned state.
    cachedOSFeatureFlags = flags;

    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setDHCP_RENEWAL_DATA(apple80211_dhcp_renewal_data *data)
{
    if (!data)
        return kIOReturnBadArgumentTahoe;

    // Apple caches the first byte as a persistent bool instead of treating the
    // IOC as disposable. Carry the same state locally so later keepalive / PM
    // work does not lose the DHCP-renewal edge.
    cachedDhcpRenewalData = *reinterpret_cast<const uint8_t *>(data) != 0;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setBATTERY_POWERSAVE_CONFIG(apple80211_battery_ps_config *data)
{
    // Apple rejects NULL, then conditionally reaches its MIMO-power-save and
    // MRC-threshold firmware owner. Intel has no matching owner or transport,
    // so do not acknowledge a cache-only request.
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setPOWER_PROFILE(apple80211_power_profile *data)
{
    if (!data)
        return kIOReturnBadArgumentTahoe;

    // Tahoe stores the profile in Core and then routes it through a
    // ConfigManager/power-profile owner. Intel has no matching owner, so reject
    // before reading the carrier instead of acknowledging an unread cache.
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setIPV4_PARAMS(apple80211_ipv4_params *data)
{
    // Apple optionally calls the Infra IPv4 owner, persists state, and drives
    // IPv4 / keepalive notifications. AirportItlwm has none of that lifecycle
    // ownership, so a dead local cache must not acknowledge the request.
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setIPV6_PARAMS(apple80211_ipv6_params *data)
{
    // Apple optionally calls the Infra IPv6 owner and schedules its
    // notification path. Its raw Core body does not establish a safe NULL
    // return; preserve the local NULL guard, then fail closed because no
    // matching owner exists.
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setINFRA_ENUMERATED(apple80211_infra_enumerated *data)
{
    if (!data)
        return kIOReturnBadArgumentTahoe;

    // Tahoe reads byte +0 and, when set, drives Commander command-timeout
    // state through a stationary-boot notification. This port has neither
    // that owner nor a concrete local ABI for the opaque forward declaration,
    // so do not dereference or acknowledge a non-null carrier.
    (void)data;
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_SCAN_REQ(apple80211ScanRequest *req)
{
    RT2_SET(2); sRT.scanReqCount++;
    struct ieee80211com *ic = fHalService->get80211Controller();

    if (!req)
        return kIOReturnBadArgument;
    if (fScanResultWrapping)
        return 22;
    if (ic->ic_state <= IEEE80211_S_INIT)
        return 22;

    /*
     * WCL scan completion is owned by the Tahoe bulletin path below: the
     * timer publishes cached WCL_SCAN_RESULT entries followed by WCL_SCAN_DONE.
     * Starting an independent net80211 bgscan here leaves that scan without a
     * paired WCL completion owner and can move an associated interface out of
     * RUN after the WCL request has already completed.
     */
    fNextNodeToSend = NULL;
    fScanResultWrapping = false;
    if (scanSource) {
        scanSource->setTimeoutMS(100);
        scanSource->enable();
    }
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_ASSOCIATE(apple80211AssocCandidates *candidates)
{
    RT2_SET(3); sRT.assocCount++;
    if (!candidates) {
        airportItlwmRegDiagRecordAssoc(kAirportItlwmRegDiagPathHiddenAssoc,
                                       nullptr, 0, nullptr, 0, 0, 0,
                                       kIOReturnBadArgument);
        return kIOReturnBadArgument;
    }

    struct ieee80211com *ic = fHalService->get80211Controller();
    const uint8_t *raw = reinterpret_cast<const uint8_t *>(candidates);

    // Extract fields from the apple80211AssocCandidates carrier recovered from IO80211Family.
    uint16_t ap_mode = *reinterpret_cast<const uint16_t *>(
        raw + TahoeAssociationContracts::kApModeOffset);
    uint32_t auth_lower = *reinterpret_cast<const uint32_t *>(
        raw + TahoeAssociationContracts::kAuthLowerOffset);
    uint32_t auth_upper = *reinterpret_cast<const uint32_t *>(
        raw + TahoeAssociationContracts::kAuthUpperOffset);
    uint32_t auth_flags = *reinterpret_cast<const uint32_t *>(
        raw + TahoeAssociationContracts::kAuthFlagsOffset);
    uint32_t ssid_len = *reinterpret_cast<const uint32_t *>(
        raw + TahoeAssociationContracts::kSsidLengthOffset);
    const uint8_t *ssid = raw + TahoeAssociationContracts::kSsidOffset;
    uint16_t rsn_ie_len = *reinterpret_cast<const uint16_t *>(
        raw + TahoeAssociationContracts::kRsnIeLengthOffset);
    const uint8_t *rsn_ie = raw + TahoeAssociationContracts::kRsnIeOffset;
    uint16_t instant_hotspot_flags = *reinterpret_cast<const uint16_t *>(
        raw + TahoeAssociationContracts::kInstantHotspotFlagsOffset);
    uint8_t pmf_capability = *(raw + TahoeAssociationContracts::kPmfCapabilityOffset);
    uint32_t bss_info_flags = *reinterpret_cast<const uint32_t *>(
        raw + TahoeAssociationContracts::kBssInfoFlagsOffset);
    uint32_t candidate_count =
        *reinterpret_cast<const uint32_t *>(
            raw + TahoeAssociationContracts::kCandidateCountOffset);
    const struct ether_addr *context_bssid =
        reinterpret_cast<const struct ether_addr *>(
            raw + TahoeAssociationContracts::kContextBssidOffset);
    const struct ether_addr *candidate_bssid =
        reinterpret_cast<const struct ether_addr *>(
            raw + TahoeAssociationContracts::kFirstCandidateBssidOffset);
    const struct ether_addr *bssid = candidate_count > 0 ? candidate_bssid : context_bssid;

    if (ssid_len > APPLE80211_MAX_SSID_LEN)
        ssid_len = APPLE80211_MAX_SSID_LEN;

    auto &associationOwner = instance->getTahoeOwnerRegistry().association;
    associationOwner.hasCarrier = true;
    associationOwner.selectedFromCandidate = candidate_count > 0;
    associationOwner.apMode = ap_mode;
    associationOwner.authLower = auth_lower;
    associationOwner.authUpper = auth_upper;
    associationOwner.authFlags = auth_flags;
    associationOwner.ssidLength = ssid_len;
    associationOwner.rsnIeLength = rsn_ie_len;
    associationOwner.boundedRsnIeLength =
        TahoeAssociationContracts::boundedRsnIeLength(
            rsn_ie_len, APPLE80211_MAX_RSN_IE_LEN);
    associationOwner.instantHotspotFlags = instant_hotspot_flags;
    associationOwner.instantHotspotAppleDeviceFlags =
        TahoeAssociationContracts::instantHotspotAppleDeviceFlags(
            instant_hotspot_flags);
    associationOwner.pmfCapabilityField = pmf_capability;
    associationOwner.bssInfoFlags = bss_info_flags;
    associationOwner.candidateCount = candidate_count;
    memset(associationOwner.ssid, 0, sizeof(associationOwner.ssid));
    memcpy(associationOwner.ssid, ssid, ssid_len);
    memcpy(associationOwner.selectedBssid, bssid->octet,
           sizeof(associationOwner.selectedBssid));
    memcpy(associationOwner.candidateBssid, candidate_bssid->octet,
           sizeof(associationOwner.candidateBssid));
    memcpy(associationOwner.contextBssid, context_bssid->octet,
           sizeof(associationOwner.contextBssid));

    if (airportItlwmRegDiagShouldBlock(kAirportItlwmRegDiagBlockHiddenAssoc)) {
        airportItlwmRegDiagRecordBlock(kAirportItlwmRegDiagBlockHiddenAssoc,
                                       kAirportItlwmRegDiagPathHiddenAssoc,
                                       ssid_len);
        airportItlwmRegDiagRecordAssoc(kAirportItlwmRegDiagPathHiddenAssoc,
                                       ssid, ssid_len,
                                       reinterpret_cast<const uint8_t *>(bssid),
                                       auth_lower, auth_upper, rsn_ie_len,
                                       kIOReturnUnsupported);
        return kIOReturnUnsupported;
    }

    IO80211BssManager *bssManager =
        instance != nullptr ? instance->getBssManager() : nullptr;
    tahoeSeedBssManagerAuthContext(bssManager, associationOwner);
    tahoeSeedBssManagerAssociatedAuthType(
        bssManager, associationOwner.authLower, associationOwner.authUpper);

    if (ic->ic_state < IEEE80211_S_SCAN) {
        XYLog("DEBUG %s SKIP: ic_state=%d < SCAN\n", __FUNCTION__, ic->ic_state);
        airportItlwmRegDiagRecordAssoc(kAirportItlwmRegDiagPathHiddenAssoc,
                                       ssid, ssid_len,
                                       reinterpret_cast<const uint8_t *>(bssid),
                                       auth_lower, auth_upper, rsn_ie_len,
                                       kIOReturnSuccess);
        return kIOReturnSuccess;
    }

    if (ic->ic_state == IEEE80211_S_ASSOC || ic->ic_state == IEEE80211_S_AUTH) {
        XYLog("DEBUG %s SKIP: already in ASSOC/AUTH ic_state=%d\n", __FUNCTION__, ic->ic_state);
        airportItlwmRegDiagRecordAssoc(kAirportItlwmRegDiagPathHiddenAssoc,
                                       ssid, ssid_len,
                                       reinterpret_cast<const uint8_t *>(bssid),
                                       auth_lower, auth_upper, rsn_ie_len,
                                       kIOReturnSuccess);
        return kIOReturnSuccess;
    }

    if (ap_mode != APPLE80211_AP_MODE_IBSS) {
        disassocIsVoluntary = false;

        struct apple80211_authtype_data auth_type_data;
        auth_type_data.version = APPLE80211_VERSION;
        auth_type_data.authtype_upper = auth_upper;
        auth_type_data.authtype_lower = auth_lower;
        setAUTH_TYPE(&auth_type_data);

        if (rsn_ie_len > 0) {
            storeAssocRsnIeOverride(ic, rsn_ie,
                                    associationOwner.boundedRsnIeLength);
        }

        associateSSID(const_cast<uint8_t *>(ssid), ssid_len, *bssid,
                      auth_lower, auth_upper, NULL, 0, 0, false, true);
    }
    airportItlwmRegDiagRecordAssoc(kAirportItlwmRegDiagPathHiddenAssoc,
                                   ssid, ssid_len,
                                   reinterpret_cast<const uint8_t *>(bssid),
                                   auth_lower, auth_upper, rsn_ie_len,
                                   kIOReturnSuccess);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_LEAVE_NETWORK(apple80211_leave_network *data)
{
    if (!data)
        return kIOReturnError;

    struct ieee80211com *ic = fHalService->get80211Controller();

    // WCL leave is the canonical Apple lifecycle edge that invalidates
    // any externally delivered PMK. Clear before any early return so
    // the host supplicant PMK store does not survive a leave into the
    // next association attempt, regardless of the ic state at entry.
    clearExternalPmkEligibilityLocked("setWCL_LEAVE_NETWORK");

    if (ic->ic_state < IEEE80211_S_SCAN)
        return kIOReturnSuccess;

    if (instance != nullptr && instance->getBssManager() != nullptr)
        instance->getBssManager()->setAdHocCreated(false);

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

IOReturn AirportItlwmSkywalkInterface::
setWCL_SCAN_ABORT(void *data)
{
    (void)data;
    struct ieee80211com *ic = fHalService->get80211Controller();

    // AppleBCMWLANCore::setWCL_SCAN_ABORT is not a no-op: it dispatches into
    // scan-adapter-owned abort work.  The recovered WCLScanManager FSM shows
    // why that matters: SCAN_ABORT_REQ moves IN_PROGRESS -> ABORTED, and the
    // owner only returns ABORTED -> IDLE when it later receives SCAN_COMPLETE.
    //
    // Our old Tahoe path only cleared net80211 flags and returned success, so
    // WCL stayed stuck in SCAN_MANAGER_STATE_ABORTED / IN_PROGRESS and every
    // later external SCAN_REQ hit the family ignore path (0xe00002bc / 16).
    // Cancel the local fake-scan timer and synthesize the single SCAN_DONE edge
    // that our backend otherwise never emits for abort completion.
    if (scanSource) {
        scanSource->cancelTimeout();
        scanSource->disable();
    }

    if (ic->ic_flags & IEEE80211_F_BGSCAN)
        ic->ic_flags &= ~IEEE80211_F_BGSCAN;
    if (ic->ic_flags & IEEE80211_F_ASCAN)
        ic->ic_flags &= ~IEEE80211_F_ASCAN;

    fNextNodeToSend = NULL;
    fScanResultWrapping = false;

    if (instance && instance->fNetIf) {
        static UInt32 abortScanStatus = 0;
        instance->postMessage(instance->fNetIf, APPLE80211_M_SCAN_DONE,
                              &abortScanStatus, sizeof(abortScanStatus), true);
    }

    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setVIRTUAL_IF_CREATE(apple80211_virt_if_create_data *data)
{
    // AppleBCMWLANCore::setVIRTUAL_IF_CREATE is not a generic unsupported
    // setter. Tahoe exposes role-dependent public failures before the private
    // proximity/AWDL/NAN owner path takes over:
    // - NAN/NAN-data roles (8..10) -> 0xe00002c7
    // - proximity/AWDL creation without the hidden owner -> 0xe00002bd
    // Keep those public fail codes instead of collapsing everything into
    // kIOReturnUnsupported.
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    switch (data->role) {
        case 8:
        case 9:
        case 10:
            return static_cast<IOReturn>(0xe00002c7);
        case 6:
            /*
             * Role 6 is the proximity/AWDL public-failure path on
             * Tahoe. The hidden AWDL owner is not present on this
             * driver, so AppleBCMWLANCore returns the recovered
             * create-failed code 0xe00002bd before the AWDL owner is
             * consulted. This code path stays out of the APSTA owner
             * skeleton (which validates only role 7) so that the
             * AWDL public failure is preserved exactly.
             */
            return static_cast<IOReturn>(kAirportItlwmAPSTACreateFailedReturn);
        case 7: {
            /*
             * Recovered APSTA role-7 acquisition contract.
             *
             * Role 7 (APPLE80211_VIF_SOFT_AP) is the only public
             * create carrier routed into the host APSTA owner. The
             * owner owns the APSTA state block, station table,
             * AP-up gate, SoftAP selector mirror, and net80211
             * station-event binding, but a create request must not
             * report success until the lower HAL backend explicitly
             * advertises and starts AP/GO firmware mode.
             *
             * While the lower backend is fail-closed, create builds
             * and validates the owner shape, attempts the lower
             * start gate, clears the station-event callback before
             * releasing the owner, and returns the HAL failure. That
             * keeps role-7 externally unsupported without leaving a
             * registered owner behind from a failed create.
             */
            if (instance == nullptr) {
                return kIOReturnNotReady;
            }
            AirportItlwmAPSTAOwner *owner =
                instance->ensureAPSTAOwner(data);
            if (owner == nullptr) {
                return static_cast<IOReturn>(
                    kAirportItlwmAPSTARawInvalidArgumentReturn);
            }
            IOReturn lowerRet = owner->startLowerIfReady();
            if (lowerRet != kIOReturnSuccess) {
                instance->deleteAPSTAOwner();
                return lowerRet;
            }
            return kIOReturnSuccess;
        }
        default:
            return static_cast<IOReturn>(0xe0000001);
    }
}

IOReturn AirportItlwmSkywalkInterface::
setVIRTUAL_IF_DELETE(apple80211_virt_if_delete_data *data)
{
    /*
     * Pre-26 APSTA lifetime symmetry: VIRTUAL_IF_DELETE is not a protocol
     * vtable slot here, so the legacy branch routes the IOCTL switch directly
     * to the controller-owned APSTA delete path. Current 25C56 Tahoe public
     * SET is instead an unread fixed nonzero wrapper and bypasses this helper.
     * The controller cleanup remains separately owned by legacy, release, and
     * failed-create paths; it is not removed or reinterpreted by that public
     * Tahoe wrapper boundary.
     */
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;
    if (instance == nullptr)
        return kIOReturnNotReady;

    return instance->deleteAPSTAOwnerForBSDName(data->bsd_name);
}

IOReturn AirportItlwmSkywalkInterface::
setROAM_PROFILE(apple80211_roam_profile_all_bands *)
{
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setLEAKY_AP_STATS_MODE(apple80211_leaky_ap_setting *)
{
    // Broadcom-private diagnostics setter. Keep the explicit unsupported
    // contract rather than advertising a normal shared Apple80211 producer.
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setROAM_CACHE_UPDATE(apple80211_roam_cache_data *)
{
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setSET_WIFI_ASSERTION_STATE(apple80211_wifi_assertion_data *)
{
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_SET_ROAM_LOCK(apple80211_set_roam_lock *data)
{
    if (data == nullptr)
        return kApple80211ErrInvalidArgumentRaw;

    // Tahoe delegates byte-0 validation and the roam_off transport lifecycle
    // to RoamAdapter. Intel has no matching adaptive-roam owner or transport.
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setVOICE_IND_STATE(apple80211_voice_ind_state *)
{
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setMWS_ACCESSORY_POWER_LIMIT_WIFI_ENH(apple80211_mws_accessory_power_limit *)
{
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setHEARTBEAT(void *)
{
    // No Apple producer has been recovered for this selector on Tahoe.
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setINTERFACE_SETTING(apple80211_interface_setting *)
{
    // No Apple producer has been recovered for this selector on Tahoe.
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setWOW_LOW_POWER_MODE(apple80211_wow_low_power_mode *)
{
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_UPDATE_FAST_LANE(apple80211_fastlane *data)
{
    // Tahoe's non-null path drives Fast Lane capability and WME/ACM owner work.
    // The port has neither owner nor transport, so it must not acknowledge it.
    if (data == nullptr)
        return static_cast<IOReturn>(0xe00002bc);
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setSTAND_ALONE_MODE_STATE(apple80211_standalone_state *)
{
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setTIMESYNC_GPIO(apple80211_timesync_gpio *)
{
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setHOST_CLOCK_INFO(apple80211_host_clock_info *)
{
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setFW_CLOCK_SOURCE(apple80211_fw_clock_source *)
{
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setTIMESYNC_TX_POLICY(apple80211_timesync_tx_policy *)
{
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setTIMESYNC_RX_POLICY(apple80211_timesync_rx_policy *)
{
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setTIMESTAMPING_EN(apple80211_timestamping_en *)
{
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setMWS_TIME_SHARING_WIFI_ENH(apple80211_mws_time_sharing *)
{
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setSDB_ENABLE(apple80211_sdb_enable *)
{
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setBTCOEX_EXT_PROFILE(apple80211_btcoex_ext_profile *)
{
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setTX_MODE_CONFIG(apple80211_tx_mode_config *)
{
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setCHANNEL(apple80211_channel_data *data)
{
    // AppleBCMWLANCore::setCHANNEL gates malformed channels, resolves a
    // chanspec, then programs it through a hidden owner. The no-APSTA local
    // fallback has no such owner, so retain the malformed/unknown split but do
    // not acknowledge an otherwise-known channel from an unread cache.
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;
    if (data->channel.channel >= 0x100)
        return kApple80211ErrInvalidArgumentRaw;

    struct ieee80211com *ic = fHalService ? fHalService->get80211Controller() : nullptr;
    if (ic == nullptr || data->channel.channel == 0)
        return static_cast<IOReturn>(0xe00002c2);

    for (int i = 0; i <= IEEE80211_CHAN_MAX; i++) {
        if (ic->ic_channels[i].ic_freq == 0)
            continue;
        if (ieee80211_chan2ieee(ic, &ic->ic_channels[i]) == data->channel.channel)
            return kIOReturnUnsupported;
    }
    return static_cast<IOReturn>(0xe00002c2);
}

IOReturn AirportItlwmSkywalkInterface::
setTXPOWER(apple80211_txpower_data *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    // AppleBCMWLANCore serializes the caller carrier into a four-byte
    // qtxpower firmware IOVAR and returns the raw transport status. The Intel
    // port has no matching command owner, so do not fabricate that transition
    // by mutating the getter's BA-notification cache.
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setRATE(apple80211_rate_data *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    // AppleBCMWLANCore performs bg_rate GET/SET/GET firmware transactions and
    // returns their transport result. There is no equivalent Intel owner, so
    // fail closed before observing or caching the caller carrier.
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setIBSS_MODE(apple80211_network_data *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    // Apple carries this request through real ad-hoc/proximity/NAN lifecycle
    // owners. The local HAL is STA-only and has no equivalent IBSS creator, so
    // do not acknowledge a network mode that was not created.
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setIE(apple80211_ie_data *data)
{
    if (data == nullptr || instance == nullptr)
        return kApple80211ErrInvalidArgumentRaw;

    TahoePayloadBuilders::IEPayloads payload;
    if (!TahoePayloadBuilders::buildIE(data, &payload))
        return kApple80211ErrInvalidArgumentRaw;

    // Apple validates a 1..0x800-byte IE, then routes it either to WAPI
    // association work or to the vndr_ie commander path. Intel has neither
    // backend, so do not turn a validated public carrier into local success.
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setOFFLOAD_TCPKA_ENABLE(apple80211_offload_tcpka_enable_t *data)
{
    // AppleBCMWLANCore::setOFFLOAD_TCPKA_ENABLE does not use a bad-argument
    // code here. The slot defaults to 0xe00002c7 and only flips to success
    // once both the feature gate and keepalive owner object exist.
    if (data == nullptr || !cachedTcpkaOffloadSupported)
        return kIOReturnUnsupported;

    cachedTcpkaOffloadEnabled = data->enabled != 0;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setOFFLOAD_NDP(apple80211_offload_ndp_data *data)
{
    TahoeAsyncCommandContext asyncContext{};
    return (instance != nullptr)
               ? instance->getTahoeCommander().runSetOFFLOADNDP(data, &asyncContext)
               : kApple80211ErrInvalidArgumentRaw;
}

IOReturn AirportItlwmSkywalkInterface::
setOFFLOAD_ARP(apple80211_offload_arp_data *data)
{
    // Apple only writes this state after a private keepalive owner accepts the
    // request and queues its IPv4 notifications. Intel has no counterpart.
    if (data == nullptr || instance == nullptr || instance->fNetIf == nullptr)
        return kApple80211ErrInvalidArgumentRaw;

    return kApple80211ErrInvalidArgumentRaw;
}

IOReturn AirportItlwmSkywalkInterface::
setGAS_REQ(apple80211_gas_query_t *data)
{
    // Apple rejects NULL with 0xe00002c2. A valid request is accepted only
    // after its GAS adapter owns query state, starts ANQP transport, and can
    // later publish completion. The Intel path has no such backend.
    if (data == nullptr)
        return static_cast<IOReturn>(0xe00002c2);

    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setBTCOEX_PROFILE(apple80211_btcoex_profile *data)
{
    if (data == nullptr || instance == nullptr)
        return static_cast<IOReturn>(0xe00002c2);

    TahoePayloadBuilders::BtcoexProfilePayload payload;
    if (!TahoePayloadBuilders::buildBtcoexProfile(data, &payload))
        return static_cast<IOReturn>(0xe00002c2);
    if (payload.band >= 5 || payload.mode < 1 || payload.mode > 4 ||
        payload.profileIndex >= 10)
        return static_cast<IOReturn>(0xe00002c2);

    // Apple applies this record through mode-specific coexistence work and a
    // real commander IOVAR. The Intel port has no equivalent backend, so do
    // not acknowledge a validated carrier after synthetic bookkeeping only.
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setBTCOEX_PROFILE_ACTIVE(apple80211_btcoex_profile_active_data *data)
{
    if (data == nullptr || instance == nullptr)
        return static_cast<IOReturn>(0xe00002c2);

    // Apple programs btc_profile_active through its commander. There is no
    // Intel-equivalent transport or local SET-to-GET producer.
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setBTCOEX_2G_CHAIN_DISABLE(apple80211_btcoex_2g_chain_disable *data)
{
    if (data == nullptr || instance == nullptr)
        return static_cast<IOReturn>(0xe00002c2);

    // Apple emits the fixed six-byte chain-disable IOVAR. The Intel port has
    // no equivalent backend or local SET-to-GET producer.
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setRESET_CHIP(apple80211_reset_command *)
{
    // AppleBCMWLANCore::setRESET_CHIP is a trap-only debug selector. The only
    // safe caller-visible contract we can preserve without reproducing Apple's
    // trap path is the raw Tahoe fail 0x16 rather than generic unsupported.
    return kApple80211ErrInvalidArgumentRaw;
}

IOReturn AirportItlwmSkywalkInterface::
setCRASH(apple80211_crash_command *data)
{
    // AppleBCMWLANInfraProtocol::setCRASH exposes a visible split:
    // - NULL / invalid crash id -> 0x16
    // - valid crash id but no dbg owner -> 0x13
    // The actual owner callback remains Apple-private.
    if (data == nullptr)
        return kApple80211ErrInvalidArgumentRaw;

    const uint32_t crashId =
        *reinterpret_cast<const uint32_t *>(reinterpret_cast<const uint8_t *>(data) + 4);
    if (crashId < 9 || crashId > 15)
        return kApple80211ErrInvalidArgumentRaw;
    return static_cast<IOReturn>(0x13);
}

IOReturn AirportItlwmSkywalkInterface::
setRANGING_ENABLE(apple80211_ranging_enable_request_t *data)
{
    // AppleBCMWLANCore::setRANGING_ENABLE leaves the public path immediately
    // after a visible bad-argument gate and switches into a hidden ranging
    // owner. Preserve that gate and keep the non-public branch explicit.
    if (data == nullptr)
        return kIOReturnBadArgument;
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setRANGING_START(apple80211_ranging_start_request_t *data)
{
    // AppleBCMWLANCore::setRANGING_START likewise only exposes a public
    // bad-argument gate before handing off to hidden V3/V4 ranging owners.
    if (data == nullptr)
        return kIOReturnBadArgument;
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setTKO_PARAMS(apple80211_tko_params *data)
{
    // AppleBCMWLANCore::setTKO_PARAMS returns 0xe00002bc when the keepalive
    // owner is absent; otherwise it copies six dwords from caller +0x4.
    if (!cachedTcpkaOffloadSupported)
        return kIOReturnBadArgumentTahoe;

    if (data != nullptr) {
        const uint8_t *raw = reinterpret_cast<const uint8_t *>(data);
        for (size_t i = 0; i < sizeof(cachedTkoParams) / sizeof(cachedTkoParams[0]); i++) {
            cachedTkoParams[i] =
                *reinterpret_cast<const uint32_t *>(raw + 4 + i * sizeof(uint32_t));
        }
    }
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setHP2P_CTRL(apple80211_hp2p_ctrl *)
{
    // AppleBCMWLANCore::setHP2P_CTRL is a trap-only selector on Tahoe. Keep
    // it explicit rather than advertising a fake success producer.
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setSET_PROPERTY(apple80211_set_property_unserialized_data *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    // Tahoe routes this opaque carrier through a gated Core callback and reads
    // carrier +0x8 only in that owner path. The port has neither callback nor
    // carrier ABI, so reject before access rather than recording a successful
    // property operation that was never applied.
    (void)data;
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setSENSING_ENABLE(apple80211_sensing_enable_t *)
{
    // AppleBCMWLANSensingAdapter::setSENSING_ENABLE is an internal trap path,
    // not a normal public producer contract.
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setSENSING_DISABLE(apple80211_sensing_disable_t *)
{
    // AppleBCMWLANSensingAdapter::setSENSING_DISABLE defaults to the sensing
    // feature-missing fail 0xe0822801 and only flips once the hidden sensing
    // owner is present and feature-gated. Preserve that public fail contract.
    return static_cast<IOReturn>(0xe0822801);
}

IOReturn AirportItlwmSkywalkInterface::
setRANGING_AUTHENTICATE(apple80211_ranging_authenticate_request_t *data)
{
    // Apple routes this through a real proximity owner. AirportItlwm has no
    // proxd/wsec/ptk_start backend, so the commander takes Apple's null-owner
    // failure branch after validating the public carrier.
    TahoeAsyncCommandContext asyncContext{};
    return (instance != nullptr)
               ? instance->getTahoeCommander().runSetRangingAuthenticate(
                     data, 0, &asyncContext)
               : static_cast<IOReturn>(0xe0000001);
}

IOReturn AirportItlwmSkywalkInterface::
setPM_MODE(apple80211_pm_mode *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    // AppleBCMWLANCore forwards this carrier to NetAdapter::configurePM,
    // which sends IOC 0x56 and retains asynchronous transport status. The
    // Intel port has no matching owner, callback, or command path, so do not
    // substitute a cache-only POWERSAVE transition for that operation.
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setLQM_CONFIG(apple80211_lqm_config_t *data)
{
    // Tahoe rejects NULL with raw 0x16 before its owner/feature gates. For an
    // enabled, validated non-null carrier, it synchronizes eCounters and
    // configures its LQM, RSSI, and channel-quality owners. The local
    // statistics timer is an independent telemetry producer, not a substitute
    // public config owner.
    if (data == nullptr)
        return static_cast<IOReturn>(TahoeLqmContracts::kInvalidArgumentRaw);

    (void)data;
    return kIOReturnError;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_REAL_TIME_MODE(apple80211_wcl_real_time_mode *data)
{
    // Apple rejects NULL with 0xe00002bc. A valid request selects real-time
    // or default mode on its NetAdapter. Intel has no matching backend.
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_ACTION_FRAME(apple80211_wcl_action_frame *data)
{
    const uint32_t firmwareGeneration =
        (instance != nullptr && instance->fNetIf != nullptr)
            ? TahoePayloadBuilders::kActionFrameV2FirmwareThreshold
            : 0;

    TahoeAsyncCommandContext asyncContext{};
    const IOReturn rc =
        (instance != nullptr)
            ? instance->getTahoeCommander().runSetWCLActionFrame(
                  data, firmwareGeneration, &asyncContext)
            : kIOReturnBadArgumentTahoe;
    if (rc != kIOReturnSuccess)
        return rc;

    const auto &owner = instance->getTahoeOwnerRegistry().actionFrame;
    cachedLastActionFrameCategory = owner.category;
    cachedLastActionFrameChannel = owner.channel;
    cachedLastActionFrameLen = owner.frameLen;
    if (cachedLastActionFrameLen > sizeof(cachedLastActionFrame))
        cachedLastActionFrameLen = sizeof(cachedLastActionFrame);
    memset(cachedLastActionFrame, 0, sizeof(cachedLastActionFrame));
    if (cachedLastActionFrameLen != 0)
        memcpy(cachedLastActionFrame, owner.frame, cachedLastActionFrameLen);
    hasCachedLastActionFrame = owner.hasFrame;
    return rc;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_ROAM_USER_CACHE(apple80211_user_roam_cache *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    // Tahoe delegates channel-cache validation, mutation, and override state
    // to RoamAdapter. Intel has no matching adaptive-roam owner or transport.
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_REASSOC(apple80211_reassoc *data)
{
    struct ieee80211com *ic = fHalService->get80211Controller();

    // A reassociation start invalidates an externally delivered PMK:
    // the host supplicant must re-install a fresh PMK through
    // CIPHER_KEY(PMK), CIPHER_KEY(MSK), PLTI DeliverPMK, or a retained
    // private/ABI CUR_PMK path on the reassociated network. A locally owned
    // PSK PMK, however, must
    // SURVIVE this edge: this producer always reassociates to the
    // CURRENT BSS (ieee80211_send_mgmt(ic->ic_bss, REASSOC_REQ)
    // below), a PSK PMK is a pure function of passphrase+SSID and
    // stays valid across it, and wifid does not re-deliver key
    // material after WCL_REASSOC. Clearing unconditionally left the
    // post-reassoc 4-way M1 permanently deferred (owner=none,
    // ic_psk_nonzero_bytes=0) until the AP deauthed with reason 15
    // (4WAY-HANDSHAKE-TIMEOUT), churning RUN->AUTH every ~24s.
    bool reassoc_psk_present = false;
    {
        for (size_t psk_i = 0; psk_i < sizeof(ic->ic_psk); ++psk_i) {
            if (ic->ic_psk[psk_i] != 0) {
                reassoc_psk_present = true;
                break;
            }
        }
    }
    if (!reassoc_psk_present)
        clearExternalPmkEligibilityLocked("setWCL_REASSOC");

    // AppleBCMWLANCore::setWCL_REASSOC is not an ack-only stub: it snapshots
    // the recovered reassoc request, refuses NULL with 0xe00002bc, and bails
    // out with the same code when the interface is not associated. The actual
    // producer then delegates to NetAdapter::sendReassocCommand(...).
    //
    // The local port does not carry Apple's firmware command owner, but the
    // net80211 STA stack already owns reassociation frame generation via
    // `ieee80211_send_mgmt(..., REASSOC_REQ, ...)`. Preserve the same request
    // coverage and association gate instead of leaving slot [590] as inline
    // success.
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    memcpy(cachedReassocRequest, data, sizeof(*data));
    hasCachedReassocRequest = true;

    if (ic->ic_state != IEEE80211_S_RUN || ic->ic_bss == nullptr)
        return kIOReturnBadArgumentTahoe;

    /*
     * Reference parity for steady-state same-BSS reassociation.
     *
     * AppleBCMWLANNetAdapter::sendReassocCommand issues firmware WLC_REASSOC
     * against the current BSS: the host-visible link and PTK stay up, and the
     * WCL reassoc terminal selector is delivered by the firmware event path.
     * Sending an OTA REASSOC_REQ to the AP we are already running with makes
     * hostapd restart key/BA state and opens a data-path hole. For a live
     * same-BSS RSNA, mirror the firmware path: keep link/key state intact and
     * publish the WCL terminal success edge without emitting a management
     * reassociation frame.
     */
    if (ic->ic_bss->ni_port_valid &&
        (ic->ic_bss->ni_flags & IEEE80211_NODE_TXRXPROT)) {
        ic->ic_wcl_reassoc_owner_active = 1;
        ic->ic_wcl_reassoc_owner_last_leaf =
            IEEE80211_WCL_REASSOC_OWNER_LEAF_SAME_BSS_TRANSPARENT;
        ieee80211_wcl_reassoc_post_success(ic);
        return kIOReturnSuccess;
    }

    /*
     * Recovered host-owned WCL reassociation owner contract: open the
     * owner record before delegating to the lower owner so any later
     * terminal selector publication can be gated on real lower-owner
     * progression. The producer itself never publishes 0x49 / 0xcf;
     * publication is performed by the post-send-gated helpers in
     * net80211 after the lower owner has actually sent or attempted
     * to send the reassociation request. Pre-send abandonment closes
     * the owner state via the producer's synchronous return without
     * firing a terminal selector, matching the recovered Apple body.
     */
    ic->ic_wcl_reassoc_owner_active = 1;
    ic->ic_wcl_reassoc_owner_last_leaf =
        IEEE80211_WCL_REASSOC_OWNER_LEAF_SETUP;

    const int rc = ieee80211_send_mgmt(ic, ic->ic_bss,
                                       IEEE80211_FC0_SUBTYPE_REASSOC_REQ,
                                       0, 0);
    if (rc == 0) {
        /*
         * Lower host owner has accepted and sent the reassociation
         * request frame; the WCL terminal edge is now owned by the
         * net80211 reassoc-response RX path or the management-frame
         * timeout, not by this producer.
         */
        ic->ic_wcl_reassoc_owner_last_leaf =
            IEEE80211_WCL_REASSOC_OWNER_LEAF_REASSOC_REQ_SENT;
    } else {
        /*
         * The lower send was attempted and failed synchronously. This
         * is a real send-failure edge; the post-send gate accepts it
         * for terminal 0xcf publication.
         */
        ic->ic_wcl_reassoc_owner_last_leaf =
            IEEE80211_WCL_REASSOC_OWNER_LEAF_REASSOC_REQ_SEND_FAIL;
        ieee80211_wcl_reassoc_post_failure(ic, static_cast<u_int32_t>(rc));
    }
    return rc == 0 ? kIOReturnSuccess : static_cast<IOReturn>(rc);
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_LEGACY_ROAM_PROFILE_CONFIG(apple80211_legacy_roam_profile_config *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    // Tahoe hands legacy profile policy and transport to RoamAdapter. Intel
    // has no matching owner or Commander path, so reject before carrier read.
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_ROAM_PROFILE_CONFIG(apple80211_roam_profile_config *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    // Tahoe fans this request into RoamAdapter policy and transport lifecycles.
    // Intel has no matching modern profile owner or Commander backend.
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_ARP_MODE(apple80211_wcl_arp_mode *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    // Tahoe delegates ARP keepalive/GARP and optional WNM keepalive work to
    // hidden owners. Intel has no matching keepalive, WNM, or transport path.
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_CONFIG_BG_MOTIONPROFILE(apple80211_bg_motion_profile *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    // Tahoe delegates mapping and PNO/EPNO firmware work to BGScanAdapter.
    // Intel has no matching background-scan owner or Commander path.
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_CONFIG_BG_NETWORK(apple80211_bg_network *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    // Tahoe delegates PFN clear/configuration and Commander IOVAR work to
    // BGScanAdapter. Intel has no matching background-scan owner or transport.
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_CONFIG_BGSCAN(apple80211_bg_scan *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    // Tahoe delegates PFN/PNO/EPNO lifecycle and Commander IOVAR work to
    // BGScanAdapter. Intel has no matching background-scan owner or transport.
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_CONFIG_BG_PARAMS(apple80211_bg_params *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    // Tahoe delegates dynamic PFN override and unassociated-scan timing to
    // BGScanAdapter. Intel has no matching background-scan owner or transport.
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_JOIN_ABORT(apple80211_wcl_abort_join *data)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    const bool requestCompletion = data != nullptr &&
                                   *reinterpret_cast<const uint32_t *>(data) != 0;

    // A join abort tears down the in-flight auth/assoc state and
    // returns the WCL join manager to IDLE. The recovered Apple
    // contract treats this as an association-reset edge for the PMK
    // owner: any PMK that was delivered for the aborted association
    // attempt must not survive into the next join. Clear before the
    // state machine moves so the cleared eligibility is observable
    // to the next association edge.
    clearExternalPmkEligibilityLocked("setWCL_JOIN_ABORT");

    // AppleBCMWLANCore::setWCL_JOIN_ABORT does not reject NULL. It maps NULL to
    // `false`, delegates into JoinAdapter::abortFirmwareJoinSync(bool), and
    // when the incoming bool is true it publishes APPLE80211_M_WCL_JOIN_ABORT_COMPLETE
    // (0xD6). The WCLJoinManager symbolic FSM confirms why this matters:
    // JOIN_ABORT_REQ moves active join states into ABORTED and the manager only
    // returns ABORTED -> IDLE after the matching JOIN_ABORT_COMPLETE edge.
    //
    // Our local architecture does not have a separate JoinAdapter object, so
    // the closest 1:1 owner action is to tear down any in-flight auth/assoc
    // state in the same net80211 controller that `setASSOCIATE(...)` drives,
    // then emit the same completion bulletin. Leaving slot [598] as inline
    // success kept the consumer side permanently missing that completion edge.
    if (ic->ic_state >= IEEE80211_S_SCAN) {
        if (ic->ic_state > IEEE80211_S_AUTH && ic->ic_bss != NULL)
            IEEE80211_SEND_MGMT(ic, ic->ic_bss, IEEE80211_FC0_SUBTYPE_DEAUTH,
                                IEEE80211_REASON_AUTH_LEAVE);

        disassocIsVoluntary = true;
        ieee80211_del_ess(ic, nullptr, 0, 1);
        ieee80211_deselect_ess(ic);
#ifdef USE_APPLE_SUPPLICANT
        ic->ic_rsn_ie_override[1] = 0;
#endif
        ic->ic_assoc_status = APPLE80211_STATUS_UNAVAILABLE;
        ic->ic_deauth_reason = APPLE80211_REASON_ASSOC_LEAVING;
        ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
    }

    if (requestCompletion && instance && instance->fNetIf)
        instance->postMessage(instance->fNetIf,
                              APPLE80211_M_WCL_JOIN_ABORT_COMPLETE,
                              nullptr, 0, true);

    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_QOS_PARAMS(apple80211_wcl_qos_params *data)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    const auto *qos = reinterpret_cast<const tahoeWclQosParams *>(data);
    constexpr uint8_t kMissingQosOwnerFlags = 0x6d;

    if (qos == nullptr)
        return kIOReturnBadArgumentTahoe;

    const uint8_t flags = qos->flags;
    // Tahoe routes retry/lifetime, real-time-policy, and MLO bits through
    // NetAdapter/Core owners absent from Intel. Reject the complete request
    // before local mutation whenever one of those bits is present. Bit 0x80
    // stays an Apple no-op; the local RTS and PM actions remain below.
    if ((flags & kMissingQosOwnerFlags) != 0)
        return kIOReturnUnsupported;

    if ((flags & 0x02) != 0) {
        ic->ic_rtsthreshold = MIN(static_cast<uint32_t>(IEEE80211_RTS_MAX),
                                  qos->rts_threshold);
    }

    if ((flags & 0x10) != 0) {
        apple80211_powersave_data pd{};
        pd.version = APPLE80211_VERSION;
        pd.powersave_level = qos->powersave_mode;
        setPOWERSAVE(&pd);
    }

    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_LINK_UP_DONE(void *data)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    (void)data;

    // Apple routes LINK_UP_DONE into PowerManager::handleLinkUpConfiguration().
    // The local port has no separate Apple power-manager object, but it does
    // have the same two owner-side knobs that materially change post-link
    // runtime behavior: re-applying the current powersave mode and pushing a
    // MAC-context refresh through `ic_updateedca`. Leaving slot [603] as a
    // blind success stub meant neither happened on the Tahoe WCL path.
    if (cachedPowersaveLevel != APPLE80211_POWERSAVE_MODE_DISABLED) {
        apple80211_powersave_data pd{};
        pd.version = APPLE80211_VERSION;
        pd.powersave_level = cachedPowersaveLevel;
        setPOWERSAVE(&pd);
    }
    if (ic->ic_updateedca != nullptr)
        ic->ic_updateedca(ic);

    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_SET_SCAN_HOME_AWAY_TIME(scanHomeAndAwayTime *data)
{
    // AppleBCMWLANCore::setWCL_SET_SCAN_HOME_AWAY_TIME consumes a single dword
    // and forwards it into the scan-adapter owner. Preserve the same carrier
    // instead of acknowledging slot [604] and discarding the timing request.
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    // The Intel port has no scan-adapter iovar transport.
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_ULOFDMA_STATE(apple80211_wcl_ulofdma_state *data)
{
    // Apple rejects NULL with 0xe00002bc and otherwise invokes its 11ax
    // adapter. Intel has no equivalent owner or firmware transport.
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setMIMO_CONFIG(apple80211_mimo_config *data)
{
    // 25C56 enters a feature-gated MIMO power-owner path for nonnull input.
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    // Intel has no corresponding MIMO power owner or configuration transport.
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setFACETIME_WIFICALLING_PARAMS(apple80211_facetime_wificalling_params *data)
{
    // Apple reads the status dword and invokes its WiFi-call policy helper;
    // feature-gated PowerManager work is the actual operation. AirportItlwm
    // has no matching policy owner, so a cache-only status write must not
    // acknowledge that request.
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setDUAL_POWER_MODE(apple80211_dual_power_mode_params *data)
{
    const auto *params = reinterpret_cast<const tahoeDualPowerModeParams *>(data);

    if (params == nullptr)
        return kIOReturnBadArgumentTahoe;

    // The reference stores both values then reevaluates and sends txcapstate.
    // No Intel firmware command implements that owner path.
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setCONGESTION_CTRL_IND(apple80211_congestion_control_indication *data)
{
    // Tahoe feeds a traffic-monitor state path. The Intel port has no matching
    // monitor or WMM owner, so it must not acknowledge an unread carrier.
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    (void)data;
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setLMTPC_CONFIG(apple80211_lmtpc_config *data)
{
    // Apple rejects NULL, then routes its byte carrier into an LMTPC owner
    // that sends firmware lpc. Intel has no equivalent owner or transport, so
    // do not acknowledge a cache-only request.
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setLE_SCAN_PARAM(apple80211_le_scan_params *data)
{
    // AppleBCMWLANCore updates BTLE connection statistics directly: byte +0
    // selects the enabled or disabled counter, +4/+8 are accumulated only on
    // the enabled path, and dword +0xc selects a duty bucket 0..6. Its
    // optional IOReporting owner is a reporter, not the owner of this state.
    // Keep the port's NULL rejection as a local safety boundary; Apple reads
    // the carrier directly and has no corresponding NULL return path.
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    const auto *raw = reinterpret_cast<const uint8_t *>(data);
    uint32_t peak = 0;
    uint32_t total = 0;
    uint32_t duty = 0;
    memcpy(&peak, raw + 4, sizeof(peak));
    memcpy(&total, raw + 8, sizeof(total));
    memcpy(&duty, raw + 12, sizeof(duty));

    if (raw[0] != 0) {
        ++leScanEnabledCount;
        leScanPeakSum += peak;
        leScanTotalSum += total;
    } else {
        ++leScanDisabledCount;
    }

    if (duty <= 6)
        ++leScanDutyCount[duty];

    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setAP_MODE(apple80211_apmode_data *data)
{
    (void)data;

    // AppleBCMWLANCore::setAP_MODE returns the fixed Tahoe fail code on the
    // normal STA path without mutating AP state. The success edge is hidden
    // behind debug/feature gates that this port does not expose.
    return static_cast<IOReturn>(0xe00002c7);
}

IOReturn AirportItlwmSkywalkInterface::
setDBG_GUARD_TIME_PARAMS(apple80211_dbg_guard_time_params *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    // Tahoe sends a private command after packing selected carrier bytes. Intel
    // has no matching owner or transport, so do not acknowledge an unapplied
    // configuration. The local null guard remains a safety boundary.
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setPRIVATE_MAC(apple80211_private_mac_data *data)
{
    if (data == nullptr)
        return kApple80211ErrInvalidArgumentRaw;

    // Tahoe applies this carrier through BGScanAdapter and returns success only
    // after configuring private scan MAC state. There is no matching Intel
    // owner or "scanmac" backend, so reject valid local carriers before
    // reading them instead of manufacturing state visible through the getter.
    (void)data;
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setSET_MAC_ADDRESS(apple80211_set_mac_address_data *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    const uint8_t *mac = data->mac;
    struct ieee80211com *ic = fHalService ? fHalService->get80211Controller() : nullptr;
    if (ic == nullptr)
        return kIOReturnNotReady;

    IEEE80211_ADDR_COPY(ic->ic_myaddr, mac);
    if_setlladdr(&ic->ic_ac.ac_if, mac);
    if (mExpansionData2 != nullptr && mExpansionData2->fBSDInterface != nullptr) {
        ether_addr linkMac;
        memcpy(linkMac.octet, mac, IEEE80211_ADDR_LEN);
        setLinkLayerAddress(&linkMac);
    }
    setProperty(kIOMACAddress, const_cast<uint8_t *>(mac), kIOEthernetAddressSize);
    IORegistryEntry *legacy = getChildEntry(gIOServicePlane);
    if (legacy != nullptr &&
        legacy->metaCast("IOSkywalkLegacyEthernet") != nullptr)
        legacy->setProperty(kIOMACAddress, const_cast<uint8_t *>(mac),
                            kIOEthernetAddressSize);
    postMessage(APPLE80211_M_LINK_ADDRESS_CHANGED, const_cast<uint8_t *>(mac), 6, true);

    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setTHERMAL_INDEX(apple80211_thermal_index_t *data)
{
    // Tahoe feature-gates a `tvpm` firmware transaction before it can update
    // core state. The Intel port has neither that owner nor its transport, so
    // reject without consuming the carrier or manufacturing getter state.
    (void)data;
    return kIOReturnBadArgumentTahoe;
}

IOReturn AirportItlwmSkywalkInterface::
setDYNAMIC_RSSI_WINDOW_CONFIG(apple80211_dynamic_rssi_window_config *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    // Tahoe applies this through ConfigManager commander IOVARs.  No matching
    // local Dynamic-RSSI configurator is implemented, so do not acknowledge it.
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setREALTIME_QOS_MSCS(apple80211_state_data *data)
{
    if (data == nullptr)
        return kApple80211ErrInvalidArgumentRaw;

    // Tahoe gates and drives a QoS/MSCS firmware path. No matching local QoS
    // owner, event handler, or firmware request path is implemented.
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setRSN_XE(apple80211_rsn_xe_data *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    // Reference Core delegates validation and mutation to JoinAdapter. The
    // local port has no equivalent association owner, so do not acknowledge a
    // carrier that was not applied.
    (void)data;
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setGAS_ABORT(void *)
{
    // Tahoe reaches an Apple GAS owner, performs its private abort transport,
    // emits completion, and clears adapter state. Intel has no GAS/ANQP owner
    // or completion path, so do not acknowledge an abort operation that was
    // not performed. The reference ignores this selector's pointer, therefore
    // no local null distinction is invented.
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_LIMITED_AGGREGATION(apple80211_limited_aggregation_config *data)
{
    return data == nullptr ? kIOReturnBadArgumentTahoe : kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_BCN_MUTE_CONFIG(apple80211_bcn_mute_config *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    // Intel has no beacon-mitigation adapter or iovar workqueue transport.
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setEAP_FILTER_CONFIG(apple80211_eap_filter_config *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    // Tahoe defers this state to an EAPOL packet-filter firmware lifecycle.
    // Intel has no matching EAPOL-filter owner or Commander path.
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_ASSOCIATED_SLEEP(apple80211_associated_sleep_config *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    // Tahoe configures an external sleep backend for this carrier.  Intel has
    // no equivalent backend, so do not acknowledge an unconsumed request.
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_SOI_CONFIG(appl80211_sleep_on_inactivity_config *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    // Tahoe routes this carrier to a sleep configuration backend.  Intel has
    // no equivalent backend, so do not acknowledge an unconsumed request.
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setOS_ELIGIBILITY(apple80211_os_eligibility *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    // Tahoe applies an EDCA policy through its network adapter.  Intel has no
    // equivalent backend, so do not acknowledge an unconsumed request.
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setDBRG_ENTROPY(apple80211_drbg_entropy *)
{
    // AppleBCMWLANCore::setDBRG_ENTROPY is a trap-only debug selector, not a
    // public carrier producer.
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setBSS_BLACKLIST(bss_blacklist *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;
#if __IO80211_TARGET >= __MAC_26_0
    if (instance == nullptr)
        return static_cast<IOReturn>(
            TahoeBssBlacklistContracts::kClassOwnerAbsentStatus);

    return instance->setBssBlacklistOwner(
        reinterpret_cast<const uint8_t *>(data));
#else
    memset(cachedBssBlacklist, 0, sizeof(cachedBssBlacklist));
    memcpy(cachedBssBlacklist, data, sizeof(cachedBssBlacklist));
    hasCachedBssBlacklist = true;
    return kIOReturnSuccess;
#endif
}

IOReturn AirportItlwmSkywalkInterface::
setMWS_WIFI_TYPE_7_BITMAP_WIFI_ENH(apple80211_mws_wifi_channel_bitmap *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    // AppleBCMWLAN turns this opaque nine-dword carrier into a
    // firmware-specific coexistence IOVAR. Intel has no equivalent owner or
    // transport, so do not acknowledge a policy that was not applied.
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setMWS_COEX_BITMAP_WIFI_ENH(apple80211_mws_wifi_channel_bitmap *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    // AppleBCMWLAN maps this opaque nine-dword carrier to a firmware-specific
    // coexistence command. Intel has no equivalent owner or transport, so do
    // not acknowledge a policy that was not applied.
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setMWS_DISABLE_OCL_BITMAP_WIFI_ENH(apple80211_mws_wifi_channel_bitmap *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    // AppleBCMWLAN maps this opaque bitmap carrier to a firmware-specific
    // coexistence command. Intel has no equivalent owner or transport, so do
    // not acknowledge a policy that was not applied.
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setMWS_RFEM_CONFIG_WIFI_ENH(apple80211_mws_rfem_config *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    // AppleBCMWLAN maps this opaque ten-dword carrier to a firmware-specific
    // coexistence command. Intel has no equivalent owner or transport, so do
    // not acknowledge a policy that was not applied.
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setMWS_ASSOC_PROTECTION_BITMAP_WIFI_ENH(apple80211_mws_wifi_channel_bitmap *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    // AppleBCMWLAN maps this opaque bitmap carrier to a firmware-specific
    // coexistence command. Intel has no equivalent owner or transport, so do
    // not acknowledge a policy that was not applied.
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setMWS_SCAN_FREQ_WIFI_ENH(apple80211_mws_scan_freq *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    // AppleBCMWLAN maps this opaque carrier to a firmware-specific
    // coexistence command. Intel has no equivalent owner or transport, so do
    // not acknowledge a policy that was not applied.
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setMWS_SCAN_FREQ_MODE_WIFI_ENH(apple80211_mws_scan_freq_mode *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    // AppleBCMWLAN maps this opaque carrier to a firmware-specific
    // coexistence command. Intel has no equivalent owner or transport, so do
    // not acknowledge a policy that was not applied.
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setMWS_CONDITION_ID_BITMAP_WIFI_ENH(apple80211_mws_condition_id_config *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    const uint8_t *raw = reinterpret_cast<const uint8_t *>(data);
    if (raw[2] == 0)
        return kIOReturnBadArgumentTahoe;

    // AppleBCMWLAN submits each condition record through its MWS firmware
    // transport. Intel has no equivalent owner or transport, so do not
    // acknowledge a policy that was not applied.
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setMWS_ANTENNA_SELECTION_WIFI_ENH(apple80211_mws_antenna_selection *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    // AppleBCMWLAN maps this opaque carrier to a firmware-specific
    // coexistence command. Intel has no equivalent owner or transport, so do
    // not acknowledge a policy that was not applied.
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setNDD_REQ(apple80211_ndd_data *)
{
    // AppleBCMWLANCore::setNDD_REQ only succeeds when the NearbyDeviceDiscovery
    // owner at +0x7c90 exists. The current port does not lift that owner, so
    // the exact public-facing contract is the feature-gated Apple fail path.
    return static_cast<IOReturn>(0xe00002c7);
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_WNM_OPS(apple80211_wcl_wnm_config_t *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    // Tahoe applies WNM features through its adapter and commander work. No
    // matching local WNM configuration backend is implemented.
    return kIOReturnUnsupported;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_WNM_OFFLOAD(apple80211_wcl_wnm_offload_t *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    // Tahoe applies this through WnmAdapter and commander IOVAR work.  No
    // matching local WNM-offload configurator is implemented.
    return kIOReturnUnsupported;
}

extern OSDictionary *convertScanToDictionary(apple80211_scan_result *a1);

static uint8_t scanResultSsidLengthForNode(const struct ieee80211_node *node)
{
    return (node != nullptr)
        ? MIN(static_cast<uint8_t>(APPLE80211_MAX_SSID_LEN), node->ni_esslen)
        : 0;
}

static bool isRenderableScanNode(const struct ieee80211_node *node)
{
    if (node == nullptr || node->ni_chan == nullptr ||
        node->ni_chan == IEEE80211_CHAN_ANYC)
        return false;

    return TahoeScanContracts::hasRenderableBssid(node->ni_bssid);
}

static struct ieee80211_node *nextRenderableScanNode(struct ieee80211com *ic,
                                                     struct ieee80211_node *node)
{
    while (node != nullptr && !isRenderableScanNode(node))
        node = RB_NEXT(ieee80211_tree, &ic->ic_tree, node);
    return node;
}

static uint32_t buildTahoeCurrentBssIeStream(const struct ieee80211_node *node,
                                             uint8_t *dst,
                                             uint32_t capacity)
{
    if (node == nullptr)
        return 0;

    const uint8_t ssidLen = scanResultSsidLengthForNode(node);
    return TahoeBeaconIeBuilder::buildCurrentBssIeStream(
        node->ni_essid,
        ssidLen,
        node->ni_dtimcount,
        node->ni_dtimperiod,
        node->ni_rsnie_tlv,
        node->ni_rsnie_tlv_len,
        dst,
        capacity);
}

namespace {

static bool buildTahoeCurrentBssPayload(
    ItlHalService *hal,
    TahoeBssManagerContracts::BeaconPayload *payload)
{
    if (hal == nullptr || payload == nullptr)
        return false;

    struct ieee80211com *ic = hal->get80211Controller();
    if (ic == nullptr || ic->ic_state != IEEE80211_S_RUN || ic->ic_bss == nullptr)
        return false;

    struct ieee80211_node *node = ic->ic_bss;
    const uint16_t channelSpec =
        buildTahoeWclCurrentBssChanSpec(ic, node->ni_chan);
    if (channelSpec == 0)
        return false;

    bzero(payload, sizeof(*payload));
    payload->meta.ieLength = buildTahoeCurrentBssIeStream(
        node, payload->ie, static_cast<uint32_t>(sizeof(payload->ie)));
    payload->meta.channelSpec = channelSpec;
    payload->meta.ssidLength = MIN(
        static_cast<uint8_t>(sizeof(payload->meta.ssid)), node->ni_esslen);
    if (payload->meta.ssidLength != 0) {
        memcpy(payload->meta.ssid, node->ni_essid, payload->meta.ssidLength);
        payload->meta.flags |=
            TahoeBssManagerContracts::kSsidBytesPresentFlags;
    }

    const uint16_t primaryChannel =
        static_cast<uint16_t>(ieee80211_chan2ieee(ic, node->ni_chan));
    payload->meta.primaryChannel =
        static_cast<uint8_t>(MIN(primaryChannel, 0xff));
    memcpy(payload->meta.bssid, node->ni_bssid, sizeof(payload->meta.bssid));
    payload->meta.rssi = -(0 - IWM_MIN_DBM - node->ni_rssi);
    payload->meta.beaconInterval = node->ni_intval;
    payload->meta.capability = node->ni_capinfo;
    return true;
}

} // namespace

static int convertNodeToScanResult(ItlHalService *fHalService,
                                   struct ieee80211_node *fNextNodeToSend,
                                   apple80211_scan_result *result)
{
    bzero(result, sizeof(*result));
    result->version = APPLE80211_VERSION;
    result->asr_ie_len = static_cast<int16_t>(
        buildTahoeCurrentBssIeStream(
            fNextNodeToSend,
            result->asr_ie_data,
            static_cast<uint32_t>(sizeof(result->asr_ie_data))));
    result->asr_beacon_int = fNextNodeToSend->ni_intval;
    // Tahoe airportd candidate ingestion is sensitive to scan-result shape.
    // V16 left asr_rates empty because the loop iterated result->asr_nrates
    // immediately after bzero(), so nrates stayed 0 until after the copy.
    // That produces a malformed candidate even when the BSS is present in ic_tree.
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
    result->asr_channel.channel = ieee80211_chan2ieee(fHalService->get80211Controller(), fNextNodeToSend->ni_chan);
    result->asr_channel.flags = ieeeChanFlag2appleScanFlagVentura(fNextNodeToSend->ni_chan->ic_flags);
    result->asr_noise = fHalService->getDriverInfo()->getBSSNoise();
    result->asr_rssi = -(0 - IWM_MIN_DBM - fNextNodeToSend->ni_rssi);
    result->asr_snr = result->asr_rssi - result->asr_noise;
    memcpy(result->asr_bssid, fNextNodeToSend->ni_bssid, IEEE80211_ADDR_LEN);
    result->asr_ssid_len = scanResultSsidLengthForNode(fNextNodeToSend);
    if (result->asr_ssid_len != 0)
        memcpy(&result->asr_ssid, fNextNodeToSend->ni_essid, result->asr_ssid_len);
    return 0;
}

IOReturn AirportItlwmSkywalkInterface::
getCURRENT_NETWORK(apple80211_scan_result *sr)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    if (ic->ic_state != IEEE80211_S_RUN || ic->ic_bss == NULL) {
        // AppleBCMWLANCore::getCURRENT_NETWORK gates on
        // IO80211BssManager::isAssociated() and returns this status when the
        // current-BSS manager is not associated.
        return kApple80211ErrDriverNotAvailable;
    }
    convertNodeToScanResult(fHalService, ic->ic_bss, sr);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getTIMESYNC_INFO(apple80211_timesync_info *data)
{
    // AppleBCMWLANCore::getTIMESYNC_INFO routes through the hidden +0x1510
    // object into a 0x100-byte text producer. The recovered bus/time-sync
    // engine path always appends capability text first and then either:
    // - calls TimeSyncEngine::getTimeSyncInfo(...)
    // - or prints "TimeSync Engine not-instantiated"
    //
    // The local port has no timesync engine object, so the exact Apple-shaped
    // behavior for the "engine missing" case is a deterministic text report,
    // not generic unsupported.
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    bzero(data->text, sizeof(data->text));
    strlcat(data->text,
            "TimeSync Capability:\n\tFirmware: HW Timestamping Not capable\n\tHost: Timestamping NOT capable\n",
            sizeof(data->text));
    strlcat(data->text, "TimeSync Engine not-instantiated\n", sizeof(data->text));
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getCOLOCATED_NETWORK_SCOPE_ID(apple80211_colocated_network_scope_id *as)
{
    if (!as)
        return kIOReturnBadArgument;

    // Apple/WCL expects a 0x30-byte buffer here, not the 12-byte stub we had
    // before.  WCLConfigManager validates bulletinBoardMessage length == 0x30
    // before calling into the driver.  The old declaration kept the trailing
    // bytes uninitialized and made our Tahoe ABI diverge from Apple.
    //
    // AppleBCMWLAN sets version=1 (not APPLE80211_VERSION), then fills two
    // IDs via a lower helper when colocated-scope state exists.  We do not
    // have that lower scope provider yet, so the correct non-fabricated state
    // is a zeroed payload with version=1 and zero IDs.
    bzero(as, sizeof(*as));
    as->version = 1;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getSCAN_RESULT(struct apple80211_scan_result *sr)
{
    RT2_SET(4); sRT.scanResCount++;
    if (fNextNodeToSend == NULL) {
        if (fScanResultWrapping) {
            fScanResultWrapping = false;
            return 5;
        } else {
            struct ieee80211com *ic = fHalService->get80211Controller();
            fNextNodeToSend = nextRenderableScanNode(
                ic,
                RB_MIN(ieee80211_tree, &ic->ic_tree));
            if (fNextNodeToSend == NULL) {
                return 5;
            }
        }
    }
    convertNodeToScanResult(fHalService, fNextNodeToSend, sr);
    
    struct ieee80211com *ic = fHalService->get80211Controller();
    fNextNodeToSend = nextRenderableScanNode(
        ic,
        RB_NEXT(ieee80211_tree, &ic->ic_tree, fNextNodeToSend));
    if (fNextNodeToSend == NULL)
        fScanResultWrapping = true;

    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getWCL_BGSCAN_CACHE_RESULT(apple80211_bgscan_cached_network_data_list *data)
{
    if (!data)
        return kIOReturnError;

    struct ieee80211com *ic = fHalService->get80211Controller();
    bzero(data, sizeof(*data));

    uint64_t now = airport_up_time();
    uint32_t count = 0;
    struct ieee80211_node *ni;

    RB_FOREACH(ni, ieee80211_tree, &ic->ic_tree) {
        if (count >= APPLE80211_BGSCAN_MAX_NETWORKS)
            break;
        if (!isRenderableScanNode(ni))
            continue;

        struct apple80211_bgscan_cached_network_entry *entry = &data->entries[count];
        memcpy(entry->bssid, ni->ni_bssid, 6);
        entry->channel = ieee80211_chan2ieee(ic, ni->ni_chan);
        entry->rssi = -(0 - IWM_MIN_DBM - ni->ni_rssi);
        entry->capability = ni->ni_capinfo;
        entry->ssid_crc = ether_crc32_le_update(0xFFFFFFFF,
                            (const u_int8_t *)ni->ni_essid, ni->ni_esslen);
        entry->age_ms = (uint32_t)(now - ni->ni_age_ts);
        count++;
    }

    data->count = count;
    data->timestamp = now;

    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getWCL_CHANNELS_INFO(apple80211ChannelInfo *data)
{
    if (!data)
        return kIOReturnError;

    struct ieee80211com *ic = fHalService->get80211Controller();
    bzero(data, sizeof(*data));

    uint16_t count = 0;
    for (int i = 0; i < IEEE80211_CHAN_MAX && count < APPLE80211_WCL_MAX_CHANNELS; i++) {
        struct ieee80211_channel *ch = &ic->ic_channels[i];
        if (ch->ic_freq == 0)
            continue;

        struct apple80211_wcl_channel_entry *entry = &data->entries[count];
        entry->chan_spec = ieee80211_chan2ieee(ic, ch);
        entry->max_tx_power = 0;
        entry->min_tx_power = 0;
        entry->field4 = 0;
        entry->field5 = 0;
        entry->indoor = 0;

        uint8_t flags = 0;
        if (ch->ic_flags & IEEE80211_CHAN_PASSIVE)
            flags |= (1 << 2);  // bit2: passive
        if (ch->ic_flags & IEEE80211_CHAN_DFS)
            flags |= (1 << 1);  // bit1: radar/DFS
        if (IEEE80211_IS_CHAN_HT40(ch) || IEEE80211_IS_CHAN_VHT40(ch))
            flags |= (1 << 4);  // bit4: 40MHz
        if (IEEE80211_IS_CHAN_VHT80(ch))
            flags |= (1 << 5);  // bit5: 80MHz
        entry->flags = flags;
        count++;
    }

    data->num_channels = count;
    data->support_6e = 0;

    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getWCL_BSS_INFO(apple80211_beacon_msg *data)
{
    if (!data)
        return kIOReturnError;

    TahoeBssManagerContracts::BeaconPayload payload{};
    if (!buildTahoeCurrentBssPayload(fHalService, &payload)) {
        XYLog("WCL [526] %s invalid current channel\n", __FUNCTION__);
        return kIOReturnError;
    }

    bzero(data, sizeof(*data));
    memcpy(data->data, &payload, sizeof(payload));

    return kIOReturnSuccess;
}
