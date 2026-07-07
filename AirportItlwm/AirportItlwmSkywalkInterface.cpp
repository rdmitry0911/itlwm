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
#include "TahoeLeScanContracts.hpp"
#include "TahoeNrateContracts.hpp"
#include "TahoeQosDynsarContracts.hpp"
#include "Airport/IO80211BssManager.h"
#include "Airport/WCLBulletinBoard.h"
#include <sys/CTimeout.hpp>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSMetaClass.h>
#include <crypto/sha1.h>
#include <net80211/ieee80211_node.h>
#include <net80211/ieee80211_ioctl.h>
#include <net80211/ieee80211_priv.h>

#define super IO80211InfraProtocol
OSDefineMetaClassAndStructors(AirportItlwmSkywalkInterface, IO80211InfraProtocol);

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

static constexpr IOReturn kApple80211ErrDriverNotAvailable = 0xe0822403;
static constexpr IOReturn kApple80211ErrConfigNoValue =
    static_cast<IOReturn>(TahoeNrateContracts::kConfigNoValueStatus);
static constexpr IOReturn kApple80211ErrNoCachedValue = 0xe00002f0;
static constexpr IOReturn kApple80211ErrInvalidArgumentRaw = 0x16;
static constexpr uint32_t kIo80211InputProbeLimit = 64;
static constexpr int32_t kIo80211InputStageEntry = 1000;
static constexpr int32_t kIo80211InputStageReturn = 2000;
static constexpr uint32_t kExternalPmkWaitTimeoutMs = 3000;

// Counter of successful APPLE80211_CIPHER_PMK installs through
// setCIPHER_KEY into ieee80211com::ic_psk. Atomic-relaxed so
// concurrent IOCTL paths do not lose increments.
extern "C" volatile uint64_t setCipherKey_pmk_install_count = 0;
// Counter of successful apple80211setCUR_PMK installs through the
// alternate selector into ieee80211com::ic_psk. Same atomic-relaxed
// contract as the CIPHER_KEY counter so an observer can attribute
// each PMK install to the carrier that delivered it.
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

static bool isInvalidTahoeLqmThresholdByte(uint8_t value)
{
    // AppleBCMWLANCore::setLQM_CONFIG rejects any byte in the inclusive range
    // [0x0b, 0x9b] for the seven-byte threshold subfield starting at +0x11.
    return static_cast<uint8_t>(value - 0x0bU) < 0x91U;
}

static void initializeTahoeLqmConfig(apple80211_lqm_config_t *config)
{
    memset(config, 0, sizeof(*config));
    config->version = APPLE80211_VERSION;
    // IO80211LQMData mirrors one interval across the first three dwords of the
    // public carrier. Tahoe family-side validation only accepts 1000 or 5000
    // in that role; initialize the local cache to the lower valid interval
    // instead of inventing a non-Apple default.
    config->sample_period_ms = 1000;
    config->tx_per_interval_ms = 1000;
    config->rx_loss_interval_ms = 1000;
}

namespace {

struct TahoeWclCurrentBssMetaData {
    uint32_t ieLen;               // 0x00
    uint16_t chanSpec;            // 0x04
    uint8_t ssid[32];             // 0x06
    uint8_t ssidLen;              // 0x26
    uint8_t primaryChannel;       // 0x27
    uint8_t reserved28;           // 0x28
    uint8_t bssid[IEEE80211_ADDR_LEN]; // 0x29
    uint8_t reserved2f;           // 0x2f
    int32_t rssi;                 // 0x30
    uint16_t reserved34;          // 0x34
    uint16_t reserved36;          // 0x36
    uint16_t beaconInterval;      // 0x38
    uint16_t capability;          // 0x3a
    uint32_t reserved3c;          // 0x3c
    uint32_t flags;               // 0x40
} __attribute__((packed));

static_assert(sizeof(TahoeWclCurrentBssMetaData) == APPLE80211_WCL_BSS_INFO_HEADER_LEN,
              "Tahoe WCL current-BSS metadata must match Apple 0x44 header");

struct TahoeWclCurrentBssPayload {
    TahoeWclCurrentBssMetaData meta;
    uint8_t ie[APPLE80211_WCL_BSS_INFO_MAX_IE_LEN];
} __attribute__((packed));

static_assert(sizeof(TahoeWclCurrentBssPayload) == APPLE80211_WCL_BSS_INFO_LEN,
              "Tahoe WCL current-BSS payload must match Apple 0x844 output");

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

}

static uint8_t tahoeMimoBandwidthCodeFromWidth(uint8_t width)
{
    switch (width) {
        case IEEE80211_CHAN_WIDTH_20_NOHT:
        case IEEE80211_CHAN_WIDTH_20:
            return 1;
        case IEEE80211_CHAN_WIDTH_40:
            return 2;
        case IEEE80211_CHAN_WIDTH_80:
        case IEEE80211_CHAN_WIDTH_80P80:
        case IEEE80211_CHAN_WIDTH_160:
            return 3;
        default:
            return 0;
    }
}

static uint8_t tahoeMimoBandwidthCarrier(uint8_t code)
{
    static constexpr uint8_t kAppleMimoBandwidthTable[4] = {0x50, 0x14, 0x28, 0x50};
    return code < sizeof(kAppleMimoBandwidthTable) ? kAppleMimoBandwidthTable[code] : 0;
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

static uint8_t encodeAppleTahoeQTxpowerFromMw(uint32_t mw)
{
    // AppleBCMWLANCore::setTXPOWER(version==1) converts the public mW carrier
    // back into the one-byte qtxpower transport. Preserve that public encoding
    // with the same table Apple uses on the getter side instead of falling back
    // to `ic_txpower`.
    uint32_t bestIndex = 0;
    uint32_t bestDelta = UINT32_MAX;
    for (uint32_t i = 0; i < sizeof(kAppleTahoeQTxpowerTable) / sizeof(kAppleTahoeQTxpowerTable[0]); i++) {
        uint32_t tableValue = kAppleTahoeQTxpowerTable[i];
        uint32_t delta = (tableValue > mw) ? (tableValue - mw) : (mw - tableValue);
        if (delta < bestDelta) {
            bestDelta = delta;
            bestIndex = i;
        }
    }
    return static_cast<uint8_t>(0x99U + bestIndex);
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

static void setTahoeCachedQTxpowerRaw(ItlHalService *hal, uint8_t raw)
{
    if (hal == nullptr)
        return;

    if (auto *iwm = OSDynamicCast(ItlIwm, hal)) {
        struct iwm_softc *sc = &iwm->com;
        sc->sc_last_qtxpower_raw = raw;
        sc->sc_has_last_qtxpower_raw = true;
        return;
    }

    if (auto *iwx = OSDynamicCast(ItlIwx, hal)) {
        struct iwx_softc *sc = &iwx->com;
        sc->sc_last_qtxpower_raw = raw;
        sc->sc_has_last_qtxpower_raw = true;
    }
}

static IOReturn getTahoeCachedNrate(ItlHalService *hal, uint32_t *rate)
{
    if (hal == nullptr || rate == nullptr)
        return kIOReturnError;

    *rate = 0;

    if (auto *iwm = OSDynamicCast(ItlIwm, hal)) {
        struct iwm_softc *sc = &iwm->com;
        *rate = sc->lq_sta.rs_drv.last_rate_n_flags;
        return kIOReturnSuccess;
    }

    if (auto *iwx = OSDynamicCast(ItlIwx, hal)) {
        struct iwx_softc *sc = &iwx->com;
        if (!sc->sc_has_last_rate_n_flags)
            return kApple80211ErrConfigNoValue;
        *rate = sc->sc_last_rate_n_flags;
        return kIOReturnSuccess;
    }

    return kApple80211ErrConfigNoValue;
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

struct triggerCCSnapshot
{
    uint64_t qword0;
    uint64_t qword1;
    uint64_t qword2;
    uint64_t qword3;
} __attribute__((packed));
static_assert(sizeof(triggerCCSnapshot) == 0x20,
              "triggerCCSnapshot must match the first four qwords cached by Apple");

struct tahoeIPv4ParamsContract
{
    uint32_t address;
    uint32_t netmask;
    uint32_t gateway;
    uint16_t gatewayTail;
} __attribute__((packed));
static_assert(sizeof(tahoeIPv4ParamsContract) == 0x0E,
              "tahoeIPv4ParamsContract must match recovered Apple field coverage");

struct tahoeIPv6ParamsHeader
{
    uint32_t count;
    uint8_t addresses[10][16];
} __attribute__((packed));
static_assert(sizeof(tahoeIPv6ParamsHeader) == 0xA4,
              "tahoeIPv6ParamsHeader must match recovered Apple count+address coverage");

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

struct tahoeWclUlofdmaState
{
    uint32_t mode;
} __attribute__((packed));
static_assert(sizeof(tahoeWclUlofdmaState) == 0x4,
              "tahoeWclUlofdmaState must match the Apple dword carrier");

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

struct tahoeFaceTimeWiFiCallingParams
{
    uint32_t status;
} __attribute__((packed));
static_assert(sizeof(tahoeFaceTimeWiFiCallingParams) == 0x4,
              "tahoeFaceTimeWiFiCallingParams must match Apple status dword");

struct tahoeDualPowerModeParams
{
    int32_t primary;
    int32_t secondary;
} __attribute__((packed));
static_assert(sizeof(tahoeDualPowerModeParams) == 0x8,
              "tahoeDualPowerModeParams must match two Apple mode dwords");

struct tahoeCongestionControlIndication
{
    uint8_t enabled;
} __attribute__((packed));
static_assert(sizeof(tahoeCongestionControlIndication) == 0x1,
              "tahoeCongestionControlIndication must match Apple bool carrier");

struct tahoeLmtpcConfig
{
    uint8_t value;
} __attribute__((packed));
static_assert(sizeof(tahoeLmtpcConfig) == 0x1,
              "tahoeLmtpcConfig must match Apple byte carrier");

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
              "tahoeDynsarDetailRequest must match the recovered Apple offsets");

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

struct apple80211_set_roam_lock
{
    uint8_t roam_off;
} __attribute__((packed));
static_assert(sizeof(apple80211_set_roam_lock) == 0x1,
              "apple80211_set_roam_lock must match the one-byte WCL IOUC payload");

struct apple80211_legacy_roam_profile_config
{
    uint8_t raw[0x60];
} __attribute__((packed));
static_assert(sizeof(apple80211_legacy_roam_profile_config) == 0x60,
              "legacy roam profile config must match WCLRoamProfile payload size");

struct apple80211_roam_profile_config
{
    uint8_t raw[0x23c];
} __attribute__((packed));
static_assert(sizeof(apple80211_roam_profile_config) == 0x23c,
              "roam profile config must match WCLRoamProfile payload size");

struct apple80211_wcl_arp_mode
{
    uint16_t wnm_interval_a;
    uint16_t wnm_interval_b;
    uint8_t wnm_enabled_a;
    uint8_t wnm_enabled_b;
    uint16_t reserved06;
    uint32_t mode;
    uint32_t interval;
    uint8_t enabled;
    uint8_t reserved11[3];
} __attribute__((packed));
static_assert(offsetof(apple80211_wcl_arp_mode, mode) == 0x08,
              "apple80211_wcl_arp_mode::mode must match Apple offset +0x8");
static_assert(offsetof(apple80211_wcl_arp_mode, interval) == 0x0c,
              "apple80211_wcl_arp_mode::interval must match Apple offset +0xc");
static_assert(offsetof(apple80211_wcl_arp_mode, enabled) == 0x10,
              "apple80211_wcl_arp_mode::enabled must match Apple offset +0x10");
static_assert(sizeof(apple80211_wcl_arp_mode) == 0x14,
              "apple80211_wcl_arp_mode must preserve the recovered Tahoe offsets");

struct apple80211_bg_motion_profile
{
    uint8_t raw[0x40];
} __attribute__((packed));

struct apple80211_bg_network
{
    uint8_t raw[0x12c0];
} __attribute__((packed));
static_assert(sizeof(apple80211_bg_network) == 0x12c0,
              "apple80211_bg_network must preserve the full Apple copy range");

struct apple80211_bg_scan
{
    uint8_t raw[8];
} __attribute__((packed));

struct apple80211_bg_params
{
    uint8_t raw[0x20];
} __attribute__((packed));
static_assert(sizeof(apple80211_bg_params) == 0x20,
              "apple80211_bg_params must preserve the recovered 0x20 payload");

struct apple80211_pm_mode
{
    uint32_t version;
    uint32_t mode;
} __attribute__((packed));
static_assert(sizeof(apple80211_pm_mode) == 0x8,
              "apple80211_pm_mode must preserve the recovered version+mode layout");

struct apple80211_user_roam_cache
{
    uint8_t raw[0x7c];
} __attribute__((packed));
static_assert(sizeof(apple80211_user_roam_cache) == 0x7c,
              "apple80211_user_roam_cache must cover the recovered count/override tail");

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

    // AUTHTYPE_WPA3_SAE AUTHTYPE_WPA3_FT_SAE
    // we don't really support WPA3, but we have announced we support WPA3 in card capability function. so we fake it as WPA2 to support some WPA2/WPA3 mix wifi connection.
    if (authtype_upper == APPLE80211_AUTHTYPE_WPA3_SAE || authtype_upper == APPLE80211_AUTHTYPE_WPA3_FT_SAE) {
        wpa.i_protos |= IEEE80211_WPA_PROTO_WPA2;
        authtype_upper |= APPLE80211_AUTHTYPE_WPA2_PSK;// hack
    }
    // AUTHTYPE_WPA3_ENTERPRISE AUTHTYPE_WPA3_FT_ENTERPRISE
    if (authtype_upper == APPLE80211_AUTHTYPE_WPA3_ENTERPRISE || authtype_upper == APPLE80211_AUTHTYPE_WPA3_FT_ENTERPRISE) {
        wpa.i_protos |= IEEE80211_WPA_PROTO_WPA2;
        authtype_upper |= APPLE80211_AUTHTYPE_WPA2;// hack
    }
    
    if (authtype_upper & (APPLE80211_AUTHTYPE_WPA | APPLE80211_AUTHTYPE_WPA_PSK | APPLE80211_AUTHTYPE_WPA2 | APPLE80211_AUTHTYPE_WPA2_PSK | APPLE80211_AUTHTYPE_SHA256_PSK | APPLE80211_AUTHTYPE_SHA256_8021X)) {
        wpa.i_protos = IEEE80211_WPA_PROTO_WPA1 | IEEE80211_WPA_PROTO_WPA2;
    }
    
    if (authtype_upper & (APPLE80211_AUTHTYPE_WPA_PSK | APPLE80211_AUTHTYPE_WPA2_PSK | APPLE80211_AUTHTYPE_SHA256_PSK)) {
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
    if (authtype_upper & (APPLE80211_AUTHTYPE_WPA | APPLE80211_AUTHTYPE_WPA2 | APPLE80211_AUTHTYPE_SHA256_8021X)) {
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
    // CR-068 runtime showed that admitting public request numbers here leaks
    // raw `1` for CHANNEL/ROAM_PROFILE and still leaves SSID/BSSID/CURRENT_NETWORK
    // on `0xe0822403`, so public current-link state must be produced by the
    // recovered setCurrentApAddress(NULL/BSSID) path instead.
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
    if ((cmd == SIOCGA80211 || cmd == SIOCSA80211) && data != NULL) {
        apple80211req *req = static_cast<apple80211req *>(data);
        IOReturn ret = processApple80211Ioctl(cmd, req);
        if (ret != kIOReturnUnsupported)
            return ret;
    }
    return super::processBSDCommand(interface, cmd, data);
}

IOReturn AirportItlwmSkywalkInterface::
processApple80211Ioctl(UInt cmd, apple80211req *req)
{
    // Tahoe's non-virtual interface path still queries VIRTUAL_IF_ROLE/PARENT
    // during airportd/CoreWiFi _initInterface.  Reverse docs record Apple's
    // expected behavior here: non-virtual interfaces do NOT return raw POSIX
    // ENXIO/6, they return the Apple80211-specific -3903 (0xe082280e) code.
    //
    // Important Tahoe-specific detail from the live 853f68e logs:
    // our earlier source already had these switch cases, yet the framework
    // still logged raw `6` for both IOCTLs.  The reason is that these Apple
    // wrappers are payload-less safe-to-fail requests, while our bridge used
    // to reject *all* req_data == NULL requests before the switch.  That sent
    // VIRTUAL_IF_ROLE/PARENT straight back into the framework fallback path,
    // which is exactly where the raw POSIX-style ENXIO/6 surfaced.
    //
    // So the real 1:1 fix is not "add cases somewhere in the switch".  The
    // fix is to intercept these payload-less Tahoe IOCTLs *before* the generic
    // req_data validation and return the Apple-specific not-a-virtual-interface
    // contract directly.
    static const IOReturn kApple80211NotVirtualInterface =
        static_cast<IOReturn>(0xe082280e);
    static const IOReturn kApple80211ClassOwnerAbsent =
        static_cast<IOReturn>(0xe082280e);

    if (req == NULL)
        return kIOReturnUnsupported;

    switch (req->req_type) {
        case APPLE80211_IOC_VIRTUAL_IF_ROLE:
        case APPLE80211_IOC_VIRTUAL_IF_PARENT:
            return kApple80211NotVirtualInterface;
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
            return (cmd == SIOCGA80211)
                       ? getSSID((apple80211_ssid_data *)req->req_data)
                       : (instance != NULL
                              ? instance->setAPSTA_SSID(this, (apple80211_ssid_data *)req->req_data)
                              : kIOReturnNotReady);
        case APPLE80211_IOC_BSSID:
            // Same bootstrap contract as SSID: airportd queries BSSID during
            // _initInterface and expects success with an all-zero BSSID before
            // association. Letting the active WCL path leak 0xe0822403 keeps
            // the interface in "driver not available" state even though the
            // interface itself is already attached as Wi-Fi en0.
            return (cmd == SIOCGA80211)
                       ? getBSSID((apple80211_bssid_data *)req->req_data)
                       : kIOReturnUnsupported;
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
            if (cmd == SIOCSA80211)
                return setAUTH_TYPE((apple80211_authtype_data *)req->req_data);
            return kIOReturnUnsupported;
        case APPLE80211_IOC_AP_MODE:
            return (cmd == SIOCSA80211) ? setAP_MODE((apple80211_apmode_data *)req->req_data)
                                        : kIOReturnUnsupported;
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
        case APPLE80211_IOC_STATE:
            return (cmd == SIOCGA80211) ? getSTATE((apple80211_state_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_PHY_MODE:
            return (cmd == SIOCGA80211) ? getPHY_MODE((apple80211_phymode_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_NOISE:
            return (cmd == SIOCGA80211) ? getNOISE((apple80211_noise_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_RSSI:
            return (cmd == SIOCGA80211) ? getRSSI((apple80211_rssi_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_ASSOCIATE:
            return (cmd == SIOCSA80211) ? setASSOCIATE((apple80211_assoc_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_DISASSOCIATE:
            return (cmd == SIOCSA80211) ? setDISASSOCIATE(req->req_data)
                                        : kIOReturnUnsupported;
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
            return (cmd == SIOCGA80211) ? getOP_MODE((apple80211_opmode_data *)req->req_data)
                                        : kIOReturnUnsupported;
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
        case APPLE80211_IOC_BTCOEX_PROFILES: {
            if (instance == NULL)
                return kIOReturnNotReady;
            auto *data = (apple80211_btc_profiles_data *)req->req_data;
            if (data == NULL)
                return kIOReturnError;
            if (cmd == SIOCGA80211) {
                if (instance->btcProfile == NULL)
                    return kIOReturnError;
                memcpy(data, instance->btcProfile, sizeof(*data));
                return kIOReturnSuccess;
            }
            if (cmd == SIOCSA80211) {
                if (instance->btcProfile != NULL) {
                    IOFree(instance->btcProfile,
                           sizeof(struct apple80211_btc_profiles_data));
                    instance->btcProfile = NULL;
                }
                instance->btcProfile =
                    (struct apple80211_btc_profiles_data *)IOMalloc(sizeof(*data));
                if (instance->btcProfile == NULL)
                    return kIOReturnNoMemory;
                memcpy(instance->btcProfile, data, sizeof(*data));
                return kIOReturnSuccess;
            }
            return kIOReturnUnsupported;
        }
        case APPLE80211_IOC_BTCOEX_CONFIG: {
            if (instance == NULL)
                return kIOReturnNotReady;
            auto *data = (apple80211_btc_config_data *)req->req_data;
            if (data == NULL)
                return kIOReturnError;
            if (cmd == SIOCGA80211) {
                memcpy(data, &instance->btcConfig, sizeof(*data));
                return kIOReturnSuccess;
            }
            if (cmd == SIOCSA80211) {
                memcpy(&instance->btcConfig, data, sizeof(*data));
                return kIOReturnSuccess;
            }
            return kIOReturnUnsupported;
        }
        case APPLE80211_IOC_BTCOEX_OPTIONS: {
            if (instance == NULL)
                return kIOReturnNotReady;
            auto *data = (apple80211_btc_options_data *)req->req_data;
            if (data == NULL)
                return kIOReturnError;
            if (cmd == SIOCGA80211) {
                data->version = APPLE80211_VERSION;
                data->btc_options = instance->btcOptions;
                return kIOReturnSuccess;
            }
            if (cmd == SIOCSA80211) {
                instance->btcOptions = data->btc_options;
                return kIOReturnSuccess;
            }
            return kIOReturnUnsupported;
        }
        case APPLE80211_IOC_BTCOEX_MODE: {
            if (instance == NULL)
                return kIOReturnNotReady;
            auto *data = (apple80211_btc_mode_data *)req->req_data;
            if (data == NULL)
                return kIOReturnError;
            if (cmd == SIOCGA80211) {
                data->version = APPLE80211_VERSION;
                data->btc_mode = instance->btcMode;
                return kIOReturnSuccess;
            }
            if (cmd == SIOCSA80211) {
                instance->btcMode = data->btc_mode;
                return kIOReturnSuccess;
            }
            return kIOReturnUnsupported;
        }
        case APPLE80211_IOC_BGSCAN_CACHE_RESULTS:
            return (cmd == SIOCGA80211) ? getWCL_BGSCAN_CACHE_RESULT((apple80211_bgscan_cached_network_data_list *)req->req_data)
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
        case APPLE80211_IOC_CUR_PMK:
            // Tahoe Skywalk current-PMK carrier. The active local
            // ingress for selector 0x168 / IOC 360 is the
            // card-specific bridge: IO80211Family routes the IOCTL
            // into handleCardSpecific(...) on the V2 controller,
            // routeTahoeSkywalkIoctl(...) gates it through
            // shouldRouteTahoeSkywalkIoctlReq(...) (which lists
            // APPLE80211_IOC_CUR_PMK explicitly), and the request
            // lands here. The set-side path enters the shared
            // external-PMK ingestion helper through setCUR_PMK; the
            // get-side path remains credential-safe and returns the
            // documented Apple failure 0xe00002c7 from getCUR_PMK
            // without snapshotting PMK material into the caller
            // buffer.
            return (cmd == SIOCSA80211) ? setCUR_PMK((struct apple80211_pmk *)req->req_data)
                                        : getCUR_PMK((struct apple80211_pmk *)req->req_data);
        case APPLE80211_IOC_ASSOCIATION_STATUS:
            return (cmd == SIOCGA80211) ? getASSOCIATION_STATUS((apple80211_assoc_status_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_COUNTRY_CODE:
            if (instance == NULL)
                return kIOReturnNotReady;
            if (cmd == SIOCGA80211)
                return instance->getCOUNTRY_CODE(this, (apple80211_country_code_data *)req->req_data);
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
            return (cmd == SIOCSA80211) ? setVIRTUAL_IF_DELETE((apple80211_virt_if_delete_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_SCAN_REQ:
            return (cmd == SIOCSA80211) ? setSCAN_REQ((apple80211_scan_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_IBSS_MODE:
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
    memset(&cachedRequestedChannel, 0, sizeof(cachedRequestedChannel));
    hasCachedRequestedChannel = false;
    cachedBgRate = 0;
    hasCachedBgRate = false;
    cachedThermalIndex = 0;
    cachedPowerBudget = 0;
    memset(cachedDynsarHeader0, 0, sizeof(cachedDynsarHeader0));
    memset(cachedDynsarHeader1, 0, sizeof(cachedDynsarHeader1));
    memset(cachedDynsarPayload, 0, sizeof(cachedDynsarPayload));
    cachedSlowWifiFeatureEnabled = false;
    cachedLowLatencyEnabled = 0;
    cachedLowLatencyPowerSave = 0;
    cachedLowLatencyWindow = 0;
    cachedTxBlankingStatus = false;
    cachedPrivateMacState = 0;
    cachedPrivateMacTimeoutSeconds = 0;
    memset(cachedPrivateMacPrimary, 0, sizeof(cachedPrivateMacPrimary));
    memset(cachedPrivateMacSecondary, 0, sizeof(cachedPrivateMacSecondary));
    cachedTcpkaOffloadSupported = false;
    cachedTcpkaOffloadEnabled = false;
    cachedWowTestMode = 0;
    cachedOSFeatureFlags = 0;
    cachedDhcpRenewalData = false;
    cachedBatteryPowerSaveMode = 0;
    cachedPowerProfile = 0;
    memset(&cachedHtCapability, 0, sizeof(cachedHtCapability));
    hasCachedHtCapability = false;
    cachedIbssMode = 0;
    cachedIbssAuthLower = 0;
    cachedIbssAuthUpper = 0;
    memset(&cachedIbssChannel, 0, sizeof(cachedIbssChannel));
    cachedIbssSsidLen = 0;
    memset(cachedIbssSsid, 0, sizeof(cachedIbssSsid));
    hasCachedIbssNetwork = false;
    cachedUlofdmaState = 0;
    cachedMimoConfig = 0;
    cachedFaceTimeWiFiCallingStatus = 0;
    cachedDualPowerModePrimary = -1;
    cachedDualPowerModeSecondary = -1;
    cachedCongestionControlEnabled = false;
    cachedLmtpcValue = 0;
    memset(&cachedLeScanOwnerState, 0, sizeof(cachedLeScanOwnerState));
    hasCachedLeScanParams = false;
    cachedRealTimeMode = false;
    cachedQosLongRetryLimit = 0;
    cachedQosRtsThreshold = 0;
    cachedQosLifetimeAc3 = 0;
    cachedQosLifetimeAc2 = 0;
    cachedQosFlags = 0;
    cachedIPv4Address = 0;
    cachedIPv4Netmask = 0;
    cachedIPv4Reserved = 0;
    cachedIPv4Gateway = 0;
    cachedIPv4GatewayTail = 0;
    cachedIPv6Count = 0;
    memset(cachedIPv6Addresses, 0, sizeof(cachedIPv6Addresses));
    memset(cachedIPv6LinkLocalAddress, 0, sizeof(cachedIPv6LinkLocalAddress));
    cachedInfraEnumerated = false;
    memset(cachedUserRoamCache, 0, sizeof(cachedUserRoamCache));
    hasCachedUserRoamCache = false;
    cachedWclRoamLocked = false;
    hasCachedWclRoamLock = false;
    cachedPmMode = 0;
    initializeTahoeLqmConfig(&cachedLqmConfig);
    hasCachedLqmConfig = false;
    memset(&cachedVhtCapability, 0, sizeof(cachedVhtCapability));
    hasCachedVhtCapability = false;
    cachedScanHomeAwayTime = 0;
    cachedGasQueryIssued = false;
    cachedSetPropertyIoctlSeen = false;
    memset(cachedWnmConfig, 0, sizeof(cachedWnmConfig));
    hasCachedWnmConfig = false;
    memset(cachedWnmOffload, 0, sizeof(cachedWnmOffload));
    hasCachedWnmOffload = false;
    memset(cachedReassocRequest, 0, sizeof(cachedReassocRequest));
    hasCachedReassocRequest = false;
    memset(cachedLegacyRoamProfileConfig, 0, sizeof(cachedLegacyRoamProfileConfig));
    hasCachedLegacyRoamProfileConfig = false;
    memset(cachedRoamProfileConfig, 0, sizeof(cachedRoamProfileConfig));
    hasCachedRoamProfileConfig = false;
    memset(cachedWclArpMode, 0, sizeof(cachedWclArpMode));
    hasCachedWclArpMode = false;
    memset(cachedBgMotionProfile, 0, sizeof(cachedBgMotionProfile));
    hasCachedBgMotionProfile = false;
    memset(cachedBgNetwork, 0, sizeof(cachedBgNetwork));
    hasCachedBgNetwork = false;
    memset(cachedBgScanConfig, 0, sizeof(cachedBgScanConfig));
    hasCachedBgScanConfig = false;
    memset(cachedBgParams, 0, sizeof(cachedBgParams));
    hasCachedBgParams = false;
    memset(cachedTriggerCC, 0, sizeof(cachedTriggerCC));
    cachedTriggerCCMode = 0;
    hasCachedTriggerCC = false;
    cachedUsbHostNotificationSeq = 0;
    cachedUsbHostNotificationChange = 0;
    cachedUsbHostNotificationPresent = 0;
    memset(cachedAssocIe, 0, sizeof(cachedAssocIe));
    cachedAssocIeLen = 0;
    hasCachedAssocIe = false;
    memset(cachedVendorIe, 0, sizeof(cachedVendorIe));
    cachedVendorIeLen = 0;
    cachedVendorIeFlags = 0;
    hasCachedVendorIe = false;
    memset(cachedBtcoexProfiles, 0, sizeof(cachedBtcoexProfiles));
    cachedBtcoexProfileValidMask = 0;
    cachedBtcoexProfileActive = 0;
    cachedBtcoex2GChainDisable = 0;
    memset(cachedLastActionFrame, 0, sizeof(cachedLastActionFrame));
    cachedLastActionFrameLen = 0;
    cachedLastActionFrameChannel = 0;
    cachedLastActionFrameCategory = 0;
    hasCachedLastActionFrame = false;
    memset(cachedDbgGuardTimeParams, 0, sizeof(cachedDbgGuardTimeParams));
    hasCachedDbgGuardTimeParams = false;
    cachedDynamicRssiWindowConfig = 0;
    cachedRealTimeQosMscs = 0;
    memset(cachedBcnMuteConfig, 0, sizeof(cachedBcnMuteConfig));
    hasCachedBcnMuteConfig = false;
    cachedEapFilterConfig = 0;
    cachedBypassTxPowerCapEnabled = false;
    cachedWowEnabled = false;
    memset(cachedAssociatedSleepConfig, 0, sizeof(cachedAssociatedSleepConfig));
    hasCachedAssociatedSleepConfig = false;
    memset(cachedSoiConfig, 0, sizeof(cachedSoiConfig));
    hasCachedSoiConfig = false;
    cachedOsEligibility = 0;
    memset(cachedBssBlacklist, 0, sizeof(cachedBssBlacklist));
    hasCachedBssBlacklist = false;
    cachedRsnXeLength = 0;
    memset(cachedRsnXe, 0, sizeof(cachedRsnXe));
    hasCachedRsnXe = false;
    cachedAwdlRsdbCaps = 0;
    memset(cachedTkoParams, 0, sizeof(cachedTkoParams));
    memset(cachedMwsWifiType7Bitmap, 0, sizeof(cachedMwsWifiType7Bitmap));
    memset(cachedMwsCoexBitmap, 0, sizeof(cachedMwsCoexBitmap));
    memset(cachedMwsDisableOclBitmap, 0, sizeof(cachedMwsDisableOclBitmap));
    memset(cachedMwsRfemConfig, 0, sizeof(cachedMwsRfemConfig));
    memset(cachedMwsAssocProtectionBitmap, 0, sizeof(cachedMwsAssocProtectionBitmap));
    memset(cachedMwsScanFreq, 0, sizeof(cachedMwsScanFreq));
    memset(cachedMwsScanFreqMode, 0, sizeof(cachedMwsScanFreqMode));
    memset(cachedMwsConditionIdConfig, 0, sizeof(cachedMwsConditionIdConfig));
    cachedMwsConditionIdCount = 0;
    hasCachedMwsConditionIdConfig = false;
    memset(cachedMwsAntennaSelection, 0, sizeof(cachedMwsAntennaSelection));
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

IOReturn AirportItlwmSkywalkInterface::
reportDataPathEvents(UInt type, void *data, unsigned long dataLen, bool gated)
{
    IOReturn ret = IO80211SkywalkInterface::reportDataPathEvents(type, data, dataLen, gated);
    if (fMcsSeedBurst != 0) {
        fMcsSeedBurst--;
        seedBssManagerMcs();
    }
    return ret;
}

void AirportItlwmSkywalkInterface::
seedBssManagerMcs()
{
    struct ieee80211com *ic = fHalService ? fHalService->get80211Controller() : nullptr;
    if (ic == nullptr || ic->ic_state != IEEE80211_S_RUN || ic->ic_bss == nullptr)
        return;

    const uintptr_t kKernelVA = 0xffffff8000000000ULL;
    const uintptr_t kWCLConfigManagerId = 1;
#define AIAM_RD(dst, addr) do { \
        uintptr_t _a = (addr); \
        if (_a < kKernelVA) return; \
        (dst) = *(volatile uintptr_t *)_a; \
    } while (0)
    uintptr_t p120, glue, givars, wclglue, wivars, bb, bbh, cfg;
    uintptr_t cfgIvars, bss, cacheObj;
    AIAM_RD(p120,     (uintptr_t)this + 0x120);
    AIAM_RD(glue,     p120 + 0xd8);
    AIAM_RD(givars,   glue + 0x18);
    AIAM_RD(wclglue,  givars + 0x18);
    AIAM_RD(wivars,   wclglue + 0x18);
    AIAM_RD(bb,       wivars + 0x8);
    AIAM_RD(bbh,      bb + 0x10);
    AIAM_RD(cfg,      bbh + 0xb70 + kWCLConfigManagerId * 0x18);
    AIAM_RD(cfgIvars, cfg + 0x20);
    AIAM_RD(bss,      cfgIvars + 0x18);
    AIAM_RD(cacheObj, bss + 0x10);
    if (cacheObj < kKernelVA)
        return;
#undef AIAM_RD

    apple80211_mcs_index_set_data mcs;
    if (getMCS_INDEX_SET(&mcs) != kIOReturnSuccess)
        return;

    ((IO80211BssManager *)bss)->setMCSIndexSet(mcs);
}

extern "C" uint64_t mach_continuous_time(void);
extern "C" void     absolutetime_to_nanoseconds(uint64_t abstime, uint64_t *result);

void AirportItlwmSkywalkInterface::
postLqmUpdateBulletin()
{
    struct ieee80211com *ic = fHalService ? fHalService->get80211Controller() : nullptr;
    if (ic == nullptr || ic->ic_state != IEEE80211_S_RUN || ic->ic_bss == nullptr)
        return;

    const uintptr_t kKernelVA = 0xffffff8000000000ULL;
    const uintptr_t kWCLNetManagerId = 4;
#define AIAM_RDV(dst, addr) do { \
        uintptr_t _a = (addr); \
        (dst) = (_a >= kKernelVA) ? *(volatile uintptr_t *)_a : 0; \
    } while (0)
    uintptr_t p120, glue, givars, wclglue, wivars, bb, bbh, nm, S, s8, s18, sink;
    AIAM_RDV(p120,    (uintptr_t)this + 0x120);
    AIAM_RDV(glue,    p120 + 0xd8);
    AIAM_RDV(givars,  glue + 0x18);
    AIAM_RDV(wclglue, givars + 0x18);
    AIAM_RDV(wivars,  wclglue + 0x18);
    AIAM_RDV(bb,      wivars + 0x8);
    AIAM_RDV(bbh,     bb + 0x10);
    AIAM_RDV(nm,      bbh + 0xb70 + kWCLNetManagerId * 0x18);
    AIAM_RDV(S,       nm + 0x20);
    AIAM_RDV(s8,      S + 0x8);
    AIAM_RDV(s18,     s8 + 0x18);
    AIAM_RDV(sink,    s18);
    if (nm < kKernelVA || S < kKernelVA)
        return;
#undef AIAM_RDV

    struct ieee80211_node *ni = ic->ic_bss;
    int rssi_c = IWM_MIN_DBM + ni->ni_rssi;
    if (rssi_c > 0)
        rssi_c = 0;
    if (rssi_c < -100)
        rssi_c = -100;
    int q = (rssi_c + 100) * 100 / 70;
    if (q < 0)
        q = 0;
    if (q > 100)
        q = 100;

    unsigned int rate_mbps = 0;
    if (ni->ni_txrate < ni->ni_rates.rs_nrates)
        rate_mbps = (ni->ni_rates.rs_rates[ni->ni_txrate] & 0x7f) / 2;
    if (rate_mbps == 0)
        rate_mbps = 6;
    int32_t noise = fHalService->getDriverInfo()->getBSSNoise();
    bool hasNoise = (noise != 0 && noise != -127);
    int snr = 0;
    if (hasNoise) {
        snr = rssi_c - noise;
        if (snr < 0)
            snr = 0;
        if (snr > 127)
            snr = 127;
    }

    unsigned char ev[0x1dc];
    bzero(ev, sizeof(ev));
    ev[0x00] = 1;
    *(int32_t *)(ev + 0x04) = rssi_c;
    if (hasNoise) {
        ev[0x0b] = 1;
        *(int16_t *)(ev + 0x0c) = (int16_t)snr;
        ev[0x0e] = 1;
        *(int16_t *)(ev + 0x10) = (int16_t)noise;
    }
    ev[0x12] = 1;
    ev[0x13] = (unsigned char)(signed char)rssi_c;
    ev[0x1d8] = 1;
    ev[0x1d9] = 1;
    ev[0x30] = 1;
    *(unsigned int *)(ev + 0x28) = 1;
    *(unsigned int *)(ev + 0x24) = 1;

    bulletinBoardMessage msg;
    bzero(&msg, sizeof(msg));
    msg.msgWord0 = (0x27u << 16) | 1u;
    msg.size = sizeof(ev);
    msg.payload = ev;

    uint64_t ns = 0;
    absolutetime_to_nanoseconds(mach_continuous_time(), &ns);
    uint64_t ts_ms = ns / 1000000ULL;

    if (sink >= kKernelVA) {
        ((WCLNetManager *)nm)->handleLqmUpdate(msg);
    } else {
        *(volatile uint64_t *)(S + 0x150) = ts_ms;
        *(volatile uint64_t *)(S + 0x158) = ts_ms;
    }
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
     * `voluntary_up = 1` and `rssi` from the current node; on link-down
     * `voluntary_down` from the locally tracked disassociation
     * initiator, `reason = APPLE80211_LINK_DOWN_REASON_DEAUTH`, and the
     * current BSSID copied into `last_assoc[0..5]`. Fields itlwm does
     * not produce remain zero per the bzero entry contract.
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
        ed.isLinkDown = isLinkDown ? 1 : 0;
        if (isLinkDown) {
            ed.voluntary_down = instance->disassocIsVoluntary ? 1 : 0;
            ed.reason = APPLE80211_LINK_DOWN_REASON_DEAUTH;
            if (ic != nullptr && ic->ic_bss != nullptr)
                memcpy(ed.last_assoc, ic->ic_bss->ni_bssid,
                       IEEE80211_ADDR_LEN);
        } else {
            ed.voluntary_up = 1;
            if (ic != nullptr && ic->ic_bss != nullptr)
                ed.rssi = (uint32_t)(-(0 - IWM_MIN_DBM - ic->ic_bss->ni_rssi));
        }
        instance->postMessage(instance->fNetIf, APPLE80211_M_LINK_CHANGED,
                              &ed, sizeof(ed), true);

        /* Tahoe APPLE80211_M_BSSID_CHANGED 24-byte compact carrier.
         *
         * The recovered Apple writer for the BSSID-changed event delivers
         * a 24-byte payload through the IOUC 0x1b1 selector path with the
         * BSSID at offset 0x00 and the reason at offset 0x14, after
         * applying two producer-side gates:
         *
         *   1. Zero-BSSID rejection. A proposed BSSID whose six octets
         *      are all zero is rejected before publication. Mirror this
         *      gate locally: an all-zero ni_bssid does not produce a
         *      BSSID-changed publication and does not update the
         *      last-published-BSSID tracker.
         *   2. Same-BSS reason-1 suppression. A publication whose reason
         *      field equals APPLE80211_BSSID_CHANGE_REASON_SAME_BSS (1)
         *      and whose BSSID matches the last published BSSID is
         *      suppressed. Mirror this gate locally by classifying the
         *      proposed BSSID against the tracker: if the tracker is
         *      valid and the proposed BSSID matches the last published
         *      BSSID, the reason is APPLE80211_BSSID_CHANGE_REASON_SAME_BSS
         *      and the publication is suppressed; otherwise the reason
         *      is APPLE80211_BSSID_CHANGE_REASON_INITIAL (0) and the
         *      24-byte payload is published exactly once.
         *
         * Tahoe userspace length-checks this carrier (prior zero-length
         * publication produced an `expected=24 actual=0` CoreWiFi
         * rejection), so the populated 24-byte payload is the only valid
         * shape. After a non-suppressed publication, the tracker is
         * updated to the published BSSID and marked valid. On link-down
         * the tracker is invalidated so the next link-up always
         * republishes for a fresh association edge.
         */
        if (isLinkDown) {
            this->fLastPublishedBssidValid = false;
        } else if (ic != nullptr && ic->ic_bss != nullptr) {
            uint8_t proposedBssid[IEEE80211_ADDR_LEN];
            memcpy(proposedBssid, ic->ic_bss->ni_bssid,
                   IEEE80211_ADDR_LEN);
            const bool zeroBssidRejected =
                (proposedBssid[0] | proposedBssid[1] | proposedBssid[2] |
                 proposedBssid[3] | proposedBssid[4] |
                 proposedBssid[5]) == 0;
            if (zeroBssidRejected) {
            } else {
                const bool sameBssAsLastPublished =
                    this->fLastPublishedBssidValid &&
                    memcmp(proposedBssid, this->fLastPublishedBssid,
                           IEEE80211_ADDR_LEN) == 0;
                const uint32_t classifiedReason = sameBssAsLastPublished
                    ? APPLE80211_BSSID_CHANGE_REASON_SAME_BSS
                    : APPLE80211_BSSID_CHANGE_REASON_INITIAL;
                const bool sameBssidWithReason1 =
                    (classifiedReason ==
                         APPLE80211_BSSID_CHANGE_REASON_SAME_BSS) &&
                    sameBssAsLastPublished;
                if (!sameBssidWithReason1) {
                    apple80211_bssid_changed_event_data bd;
                    bzero(&bd, sizeof(bd));
                    memcpy(bd.bssid, proposedBssid, IEEE80211_ADDR_LEN);
                    bd.reason = classifiedReason;
                    postTahoeBssidChangedThroughInfraHelper(
                        this, instance, &bd, "setLinkStateInternal");
                    memcpy(this->fLastPublishedBssid, proposedBssid,
                           IEEE80211_ADDR_LEN);
                    this->fLastPublishedBssidValid = true;
                }
            }
        }

        /*
         * Tahoe APPLE80211_M_SSID_CHANGED zero-length carrier.
         *
         * The recovered airportd `ssidChanged` block does not consume
         * payload bytes; it schedules a fresh `__associatedNetwork` read
         * and forwards that object through `setAssociatedNetwork:`. Keep
         * this on the same accepted Skywalk terminal transition edge as
         * LINK_CHANGED and BSSID_CHANGED so userspace refreshes after the
         * BSSID/scan-cache publishers have had their chance to update the
         * framework-visible current network.
         */
        instance->postMessage(instance->fNetIf, APPLE80211_M_SSID_CHANGED,
                              NULL, 0, true);
    }
    if (ret && state == kIO80211NetworkLinkUp) {
        fMcsSeedBurst = 400;
        seedBssManagerMcs();
        postLqmUpdateBulletin();
    }
#endif
    return ret;
}

void AirportItlwmSkywalkInterface::
setCurrentApAddress(ether_addr *addr)
{
    IO80211InfraInterface::setCurrentApAddress(addr);

#if __IO80211_TARGET >= __MAC_26_0
    /* Tahoe APPLE80211_M_BSSID_CHANGED 24-byte compact carrier - producer
     * hook on the natural Apple framework setCurrentApAddress entry.
     *
     * The recovered Apple writer for the BSSID-changed event is reached
     * whenever the framework hands the local kext a new current AP
     * address. Mirror the two recovered producer-side gates against the
     * incoming addr parameter:
     *
     *   1. Zero-BSSID rejection. A null addr pointer or an addr whose
     *      six octets are all zero is rejected before publication. The
     *      rejection also invalidates the last-published-BSSID tracker
     *      so the next non-zero call always republishes for a fresh
     *      association edge.
     *   2. Same-BSS reason-1 classification + suppression. Classify the
     *      proposed BSSID against the shared
     *      fLastPublishedBssid / fLastPublishedBssidValid tracker. A
     *      match selects APPLE80211_BSSID_CHANGE_REASON_SAME_BSS = 1
     *      and suppresses; a non-match selects
     *      APPLE80211_BSSID_CHANGE_REASON_INITIAL = 0 and publishes the
     *      populated 24-byte payload through the standard
     *      IO80211Controller::postMessage / IO80211Glue pending-queue
     *      routing, then updates the tracker.
     *
     * The tracker is shared with the link-state-success publisher in
     * setLinkStateInternal: whichever publisher fires first updates the
     * tracker; if the other publisher also runs for the same accepted
     * edge it observes a matching BSSID and is classified as
     * APPLE80211_BSSID_CHANGE_REASON_SAME_BSS, so the suppression gate
     * prevents double publication. Tahoe userspace length-checks this
     * carrier (prior zero-length publication produced an
     * `expected=24 actual=0` CoreWiFi rejection), so the populated
     * 24-byte payload is the only valid Tahoe shape.
     */
    if (instance != nullptr && instance->fNetIf != nullptr) {
        if (addr == nullptr) {
            this->fLastPublishedBssidValid = false;
        } else {
            uint8_t proposedBssid[IEEE80211_ADDR_LEN];
            memcpy(proposedBssid, addr->octet, IEEE80211_ADDR_LEN);
            const bool zeroBssidRejected =
                (proposedBssid[0] | proposedBssid[1] | proposedBssid[2] |
                 proposedBssid[3] | proposedBssid[4] |
                 proposedBssid[5]) == 0;
            if (zeroBssidRejected) {
                this->fLastPublishedBssidValid = false;
            } else {
                const bool sameBssAsLastPublished =
                    this->fLastPublishedBssidValid &&
                    memcmp(proposedBssid, this->fLastPublishedBssid,
                           IEEE80211_ADDR_LEN) == 0;
                const uint32_t classifiedReason = sameBssAsLastPublished
                    ? APPLE80211_BSSID_CHANGE_REASON_SAME_BSS
                    : APPLE80211_BSSID_CHANGE_REASON_INITIAL;
                const bool sameBssidWithReason1 =
                    (classifiedReason ==
                         APPLE80211_BSSID_CHANGE_REASON_SAME_BSS) &&
                    sameBssAsLastPublished;
                if (!sameBssidWithReason1) {
                    apple80211_bssid_changed_event_data bd;
                    bzero(&bd, sizeof(bd));
                    memcpy(bd.bssid, proposedBssid, IEEE80211_ADDR_LEN);
                    bd.reason = classifiedReason;
                    postTahoeBssidChangedThroughInfraHelper(
                        this, instance, &bd, "setCurrentApAddress");
                    memcpy(this->fLastPublishedBssid, proposedBssid,
                           IEEE80211_ADDR_LEN);
                    this->fLastPublishedBssidValid = true;
                }
            }
        }
    }
#endif
    seedBssManagerMcs();
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_LINK_STATE_UPDATE(apple80211_wcl_update_link_state *data)
{
    return IO80211InfraInterface::setWCL_LINK_STATE_UPDATE(data);
}

SInt32 AirportItlwmSkywalkInterface::
setInterfaceEnable(bool enable)
{
    // Apple performs the lifted subclass body on a hidden low-latency object.
    // The local Tahoe port currently aliases that identity to `fNetIf`, so
    // replaying the subclass link-up side effects here poisons the main infra
    // interface before any association exists.  Driver visibility is already
    // recovered by the surrounding ready-edge property/bulletin publication;
    // keep only the base enable on `fNetIf` and reserve link-up for the real
    // association-success path.
    SInt32 ret = IO80211InfraInterface::setInterfaceEnable(enable);
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
    memset(&this->cachedRequestedChannel, 0, sizeof(this->cachedRequestedChannel));
    this->hasCachedRequestedChannel = false;
    this->cachedBgRate = 0;
    this->hasCachedBgRate = false;
    this->cachedThermalIndex = 0;
    this->cachedPowerBudget = 0;
    memset(this->cachedDynsarHeader0, 0, sizeof(this->cachedDynsarHeader0));
    memset(this->cachedDynsarHeader1, 0, sizeof(this->cachedDynsarHeader1));
    memset(this->cachedDynsarPayload, 0, sizeof(this->cachedDynsarPayload));
    this->cachedSlowWifiFeatureEnabled = false;
    this->cachedLowLatencyEnabled = 0;
    this->cachedLowLatencyPowerSave = 0;
    this->cachedLowLatencyWindow = 0;
    this->cachedTxBlankingStatus = false;
    this->cachedPrivateMacState = 0;
    this->cachedPrivateMacTimeoutSeconds = 0;
    memset(this->cachedPrivateMacPrimary, 0, sizeof(this->cachedPrivateMacPrimary));
    memset(this->cachedPrivateMacSecondary, 0, sizeof(this->cachedPrivateMacSecondary));
    this->cachedTcpkaOffloadSupported = false;
    this->cachedTcpkaOffloadEnabled = false;
    this->cachedWowTestMode = 0;
    this->cachedOSFeatureFlags = 0;
    this->cachedDhcpRenewalData = false;
    this->cachedBatteryPowerSaveMode = 0;
    this->cachedPowerProfile = 0;
    memset(&this->cachedHtCapability, 0, sizeof(this->cachedHtCapability));
    this->hasCachedHtCapability = false;
    this->cachedIbssMode = 0;
    this->cachedIbssAuthLower = 0;
    this->cachedIbssAuthUpper = 0;
    memset(&this->cachedIbssChannel, 0, sizeof(this->cachedIbssChannel));
    this->cachedIbssSsidLen = 0;
    memset(this->cachedIbssSsid, 0, sizeof(this->cachedIbssSsid));
    this->hasCachedIbssNetwork = false;
    this->cachedUlofdmaState = 0;
    this->cachedMimoConfig = 0;
    this->cachedFaceTimeWiFiCallingStatus = 0;
    this->cachedDualPowerModePrimary = -1;
    this->cachedDualPowerModeSecondary = -1;
    this->cachedCongestionControlEnabled = false;
    this->cachedLmtpcValue = 0;
    memset(&this->cachedLeScanOwnerState, 0, sizeof(this->cachedLeScanOwnerState));
    this->hasCachedLeScanParams = false;
    this->cachedRealTimeMode = false;
    this->cachedQosLongRetryLimit = 0;
    this->cachedQosRtsThreshold = 0;
    this->cachedQosLifetimeAc3 = 0;
    this->cachedQosLifetimeAc2 = 0;
    this->cachedQosFlags = 0;
    this->cachedIPv4Address = 0;
    this->cachedIPv4Netmask = 0;
    this->cachedIPv4Reserved = 0;
    this->cachedIPv4Gateway = 0;
    this->cachedIPv4GatewayTail = 0;
    this->cachedIPv6Count = 0;
    memset(this->cachedIPv6Addresses, 0, sizeof(this->cachedIPv6Addresses));
    memset(this->cachedIPv6LinkLocalAddress, 0, sizeof(this->cachedIPv6LinkLocalAddress));
    this->cachedInfraEnumerated = false;
    memset(this->cachedUserRoamCache, 0, sizeof(this->cachedUserRoamCache));
    this->hasCachedUserRoamCache = false;
    this->cachedWclRoamLocked = false;
    this->hasCachedWclRoamLock = false;
    this->cachedPmMode = 0;
    initializeTahoeLqmConfig(&this->cachedLqmConfig);
    this->hasCachedLqmConfig = false;
    memset(&this->cachedVhtCapability, 0, sizeof(this->cachedVhtCapability));
    this->hasCachedVhtCapability = false;
    this->cachedScanHomeAwayTime = 0;
    this->cachedGasQueryIssued = false;
    this->cachedSetPropertyIoctlSeen = false;
    memset(this->cachedWnmConfig, 0, sizeof(this->cachedWnmConfig));
    this->hasCachedWnmConfig = false;
    memset(this->cachedWnmOffload, 0, sizeof(this->cachedWnmOffload));
    this->hasCachedWnmOffload = false;
    memset(this->cachedReassocRequest, 0, sizeof(this->cachedReassocRequest));
    this->hasCachedReassocRequest = false;
    memset(this->cachedLegacyRoamProfileConfig, 0, sizeof(this->cachedLegacyRoamProfileConfig));
    this->hasCachedLegacyRoamProfileConfig = false;
    memset(this->cachedRoamProfileConfig, 0, sizeof(this->cachedRoamProfileConfig));
    this->hasCachedRoamProfileConfig = false;
    memset(this->cachedWclArpMode, 0, sizeof(this->cachedWclArpMode));
    this->hasCachedWclArpMode = false;
    memset(this->cachedBgMotionProfile, 0, sizeof(this->cachedBgMotionProfile));
    this->hasCachedBgMotionProfile = false;
    memset(this->cachedBgNetwork, 0, sizeof(this->cachedBgNetwork));
    this->hasCachedBgNetwork = false;
    memset(this->cachedBgScanConfig, 0, sizeof(this->cachedBgScanConfig));
    this->hasCachedBgScanConfig = false;
    memset(this->cachedBgParams, 0, sizeof(this->cachedBgParams));
    this->hasCachedBgParams = false;
    memset(this->cachedTriggerCC, 0, sizeof(this->cachedTriggerCC));
    this->cachedTriggerCCMode = 0;
    this->hasCachedTriggerCC = false;
    this->cachedUsbHostNotificationSeq = 0;
    this->cachedUsbHostNotificationChange = 0;
    this->cachedUsbHostNotificationPresent = 0;
    memset(this->cachedAssocIe, 0, sizeof(this->cachedAssocIe));
    this->cachedAssocIeLen = 0;
    this->hasCachedAssocIe = false;
    memset(this->cachedVendorIe, 0, sizeof(this->cachedVendorIe));
    this->cachedVendorIeLen = 0;
    this->cachedVendorIeFlags = 0;
    this->hasCachedVendorIe = false;
    memset(this->cachedBtcoexProfiles, 0, sizeof(this->cachedBtcoexProfiles));
    this->cachedBtcoexProfileValidMask = 0;
    this->cachedBtcoexProfileActive = 0;
    this->cachedBtcoex2GChainDisable = 0;
    memset(this->cachedLastActionFrame, 0, sizeof(this->cachedLastActionFrame));
    this->cachedLastActionFrameLen = 0;
    this->cachedLastActionFrameChannel = 0;
    this->cachedLastActionFrameCategory = 0;
    this->hasCachedLastActionFrame = false;
    memset(this->cachedDbgGuardTimeParams, 0, sizeof(this->cachedDbgGuardTimeParams));
    this->hasCachedDbgGuardTimeParams = false;
    this->cachedDynamicRssiWindowConfig = 0;
    this->cachedRealTimeQosMscs = 0;
    memset(this->cachedBcnMuteConfig, 0, sizeof(this->cachedBcnMuteConfig));
    this->hasCachedBcnMuteConfig = false;
    this->cachedEapFilterConfig = 0;
    this->cachedBypassTxPowerCapEnabled = false;
    this->cachedWowEnabled = false;
    memset(this->cachedAssociatedSleepConfig, 0, sizeof(this->cachedAssociatedSleepConfig));
    this->hasCachedAssociatedSleepConfig = false;
    memset(this->cachedSoiConfig, 0, sizeof(this->cachedSoiConfig));
    this->hasCachedSoiConfig = false;
    this->cachedOsEligibility = 0;
    memset(this->cachedBssBlacklist, 0, sizeof(this->cachedBssBlacklist));
    this->hasCachedBssBlacklist = false;
    this->cachedRsnXeLength = 0;
    memset(this->cachedRsnXe, 0, sizeof(this->cachedRsnXe));
    this->hasCachedRsnXe = false;
    this->cachedAwdlRsdbCaps = 0;
    memset(this->cachedTkoParams, 0, sizeof(this->cachedTkoParams));
    memset(this->cachedMwsWifiType7Bitmap, 0, sizeof(this->cachedMwsWifiType7Bitmap));
    memset(this->cachedMwsCoexBitmap, 0, sizeof(this->cachedMwsCoexBitmap));
    memset(this->cachedMwsDisableOclBitmap, 0, sizeof(this->cachedMwsDisableOclBitmap));
    memset(this->cachedMwsRfemConfig, 0, sizeof(this->cachedMwsRfemConfig));
    memset(this->cachedMwsAssocProtectionBitmap, 0, sizeof(this->cachedMwsAssocProtectionBitmap));
    memset(this->cachedMwsScanFreq, 0, sizeof(this->cachedMwsScanFreq));
    memset(this->cachedMwsScanFreqMode, 0, sizeof(this->cachedMwsScanFreqMode));
    memset(this->cachedMwsConditionIdConfig, 0, sizeof(this->cachedMwsConditionIdConfig));
    this->cachedMwsConditionIdCount = 0;
    this->hasCachedMwsConditionIdConfig = false;
    memset(this->cachedMwsAntennaSelection, 0, sizeof(this->cachedMwsAntennaSelection));
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
    if (ic->ic_state == IEEE80211_S_RUN) {
        memcpy(sd->ssid_bytes, ic->ic_des_essid, strlen((const char*)ic->ic_des_essid));
        sd->ssid_len = (uint32_t)strlen((const char*)ic->ic_des_essid);
    }
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
            // APPLE80211_CIPHER_PMK (=6) is one of the two host-side
            // PSK PMK delivery carriers on Tahoe Skywalk: the other is
            // apple80211setCUR_PMK below. Both carriers must converge on
            // the same local PMK sink (ieee80211com::ic_psk) so the
            // OpenBSD-derived PAE supplicant reads a consistent PMK in
            // ieee80211_recv_4way_msg1 regardless of which carrier
            // delivered it. installExternalPmkLocked centralises the
            // 32-byte validation, ic_psk copy, IEEE80211_F_PSK flag,
            // and WPA/RSN PSK auth-state refresh, and emits only
            // non-secret structural markers.
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
    struct ieee80211com *ic = fHalService->get80211Controller();
    pd->version = APPLE80211_VERSION;
    pd->phy_mode = APPLE80211_MODE_11A
    | APPLE80211_MODE_11B
    | APPLE80211_MODE_11G
    | APPLE80211_MODE_11N;
    
    if (ic->ic_flags & IEEE80211_F_VHTON)
        pd->phy_mode |= APPLE80211_MODE_11AC;
    
    if (ic->ic_flags & IEEE80211_F_HEON)
        pd->phy_mode |= APPLE80211_MODE_11AX;
    
    switch (fHalService->get80211Controller()->ic_curmode) {
        case IEEE80211_MODE_AUTO:
            pd->active_phy_mode = APPLE80211_MODE_AUTO;
            break;
        case IEEE80211_MODE_11A:
            pd->active_phy_mode = APPLE80211_MODE_11A;
            break;
        case IEEE80211_MODE_11B:
            pd->active_phy_mode = APPLE80211_MODE_11B;
            break;
        case IEEE80211_MODE_11G:
            pd->active_phy_mode = APPLE80211_MODE_11G;
            break;
        case IEEE80211_MODE_11N:
            pd->active_phy_mode = APPLE80211_MODE_11N;
            break;
        case IEEE80211_MODE_11AC:
            pd->active_phy_mode = APPLE80211_MODE_11AC;
            break;
        case IEEE80211_MODE_11AX:
            pd->active_phy_mode = APPLE80211_MODE_11AX;
            break;
            
        default:
            pd->active_phy_mode = APPLE80211_MODE_AUTO;
            break;
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
    od->version = APPLE80211_VERSION;
    od->op_mode = APPLE80211_M_STA;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getPOWER_DEBUG_INFO(apple80211_power_debug_info *data)
{
    // AppleBCMWLANCore::getPOWER_DEBUG_INFO zeroes the leading public qword
    // and then copies a fixed 0x2c0 telemetry snapshot from core state. The
    // local port does not lift the hidden power-debug owner yet, but it can
    // still preserve the caller-visible fixed carrier shape instead of generic
    // unsupported.
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    uint8_t *raw = reinterpret_cast<uint8_t *>(data);
    memset(raw, 0, 0x580);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getROAM_PROFILE(apple80211_roam_profile_all_bands *data)
{
    // AppleBCMWLANCore::getROAM_PROFILE writes the three per-band metadata
    // words at +0x4/+0x84/+0x104 and marks each successful band payload at
    // +0xc + band*0x80. Preserve that public multi-band carrier instead of
    // returning generic unsupported.
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    uint8_t *raw = reinterpret_cast<uint8_t *>(data);
    memset(raw, 0, 0x180);
    *reinterpret_cast<uint32_t *>(raw + 0x04) = 4;
    *reinterpret_cast<uint32_t *>(raw + 0x84) = 2;
    *reinterpret_cast<uint32_t *>(raw + 0x104) = 0x400;
    raw[0x0c] = 1;
    raw[0x8c] = 1;
    raw[0x10c] = 1;
    return kIOReturnSuccess;
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
    // AppleBCMWLANCore::getCHIP_DIAGS drives a fixed-size diagnostic carrier
    // through a gated callback path. Even without the hidden owner, keeping
    // the public 0x14-byte carrier is more faithful than generic unsupported.
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    memset(reinterpret_cast<uint8_t *>(data), 0, 0x14);
    return kIOReturnSuccess;
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
    // Tahoe apple80211setCUR_PMK is the alternate PSK PMK delivery
    // selector (0x168 / IOC 360). The Apple carrier struct
    // apple80211_pmk holds the validated key length at offset 0x04
    // and the source key bytes at offset 0x10. The accepted Apple
    // semantics are that CUR_PMK and CIPHER_KEY case 6/9 both
    // converge on the same host-side PMK owner, so the local
    // implementation funnels both into the shared external-PMK
    // helper. The active local ingress is the V2 controller
    // card-specific bridge: IO80211Family delivers SIOCSA80211 and
    // SIOCGA80211 ioctls into handleCardSpecific(...), which
    // routeTahoeSkywalkIoctl(...) forwards to processApple80211Ioctl
    // when shouldRouteTahoeSkywalkIoctlReq(...) accepts the selector
    // (APPLE80211_IOC_CUR_PMK is explicitly listed in that allow
    // list), and processApple80211Ioctl dispatches the SIOCSA80211
    // direction here.
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
    // Shared external-PMK ingestion. The contract is the same for the
    // CIPHER_KEY(PMK) and CUR_PMK carriers: validate IEEE80211_PMK_LEN,
    // copy the 32-byte PMK into ic->ic_psk, set IEEE80211_F_PSK, refresh
    // WPA/RSN PSK auth state through ieee80211_ioctl_setwpaparms so the
    // OpenBSD-derived 4-way supplicant has consistent state at the
    // first M1, and emit only non-secret structural markers (length,
    // nonzero-byte count, policy flags, source tag). A malformed
    // delivery returns an explicit IOReturn error so a future change to
    // the carrier caller cannot silently turn a rejected PMK into
    // kIOReturnSuccess. Per the Apple reference contract the PMK byte
    // store must be present in ic_psk before the first 4-way M1 is
    // consumed, regardless of whether the install arrived before or
    // after setWCL_ASSOCIATE.
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
    // AppleBCMWLANCore::getWCL_EXTENDED_BSS_INFO exposes only one public gate:
    // NULL -> 0xe00002bc, non-NULL -> delegate to the NetAdapter owner. The
    // hidden owner is still absent locally, so preserve the same null gate and
    // the non-error outer contract instead of generic unsupported.
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;
    return kIOReturnSuccess;
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
    if (ic->ic_bss == NULL || ic->ic_state != IEEE80211_S_RUN) {
        // AppleBCMWLANCore::getRATE() returns 0xe0822403 until the BSS manager
        // reports an associated current network. Returning raw 6 here was a
        // local shortcut and confused Tahoe consumers that key off the Apple
        // error code.
        return kApple80211ErrDriverNotAvailable;
    }
    int nss;
    int sgi;
    int index = 0;
    memset(rd, 0, sizeof(*rd));
    rd->version = APPLE80211_VERSION;
    rd->num_radios = 1;
    sgi = ieee80211_node_supports_sgi(ic->ic_bss);
    if (ic->ic_curmode == IEEE80211_MODE_11AC) {
        if (sgi)
            index += 1;
        nss = fHalService->getDriverInfo()->getTxNSS();
        switch (ic->ic_bss->ni_chw) {
            case IEEE80211_CHAN_WIDTH_40:
                index += 4;
                break;
            case IEEE80211_CHAN_WIDTH_80:
                index += 8;
                break;
            case IEEE80211_CHAN_WIDTH_80P80:
            case IEEE80211_CHAN_WIDTH_160:
                index += 12;
                break;

            default:
                break;
        }
        index += 2 * (nss - 1);
        const struct ieee80211_vht_rateset *rs = &ieee80211_std_ratesets_11ac[index];
        rd->rate[0] = rs->rates[ic->ic_bss->ni_txmcs % rs->nrates] / 2;
    } else if (ic->ic_curmode == IEEE80211_MODE_11N) {
        int is_40mhz = ic->ic_bss->ni_chw == IEEE80211_CHAN_WIDTH_40;
        if (sgi)
            index += 1;
        if (is_40mhz)
            index += (IEEE80211_HT_RATESET_MIMO4_SGI + 1);
        index += (ic->ic_bss->ni_txmcs / 16);
        nss = ic->ic_bss->ni_txmcs / 8 + 1;
        index += 2 * (nss - 1);
        rd->rate[0] = ieee80211_std_ratesets_11n[index].rates[ic->ic_bss->ni_txmcs % 8] / 2;
    } else {
        rd->rate[0] = ic->ic_bss->ni_rates.rs_rates[ic->ic_bss->ni_txrate];
    }
    return kIOReturnSuccess;
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
    struct ieee80211com *ic = fHalService->get80211Controller();
    struct ieee80211_node *ni = ic->ic_bss;

    // AppleBCMWLANCore::getGUARD_INTERVAL is a real producer, not a generic
    // unsupported slot: NULL returns 0xe00002c2, and the normal path derives
    // the interval from cached "nrate" state with a deterministic fallback to
    // long GI (800 ns) when no recognized short-GI encoding is available.
    if (data == nullptr)
        return static_cast<IOReturn>(0xe00002c2);

    memset(data, 0, sizeof(*data));
    data->version = APPLE80211_VERSION;
    data->interval = APPLE80211_GI_LONG;

    if (ni == nullptr)
        return kIOReturnSuccess;

    switch (ni->ni_chw) {
        case IEEE80211_CHAN_WIDTH_40:
            if (ieee80211_node_supports_ht_sgi40(ni))
                data->interval = APPLE80211_GI_SHORT;
            break;
        case IEEE80211_CHAN_WIDTH_80:
            if (ieee80211_node_supports_vht_sgi80(ni))
                data->interval = APPLE80211_GI_SHORT;
            break;
        case IEEE80211_CHAN_WIDTH_80P80:
        case IEEE80211_CHAN_WIDTH_160:
            if (ieee80211_node_supports_vht_sgi160(ni))
                data->interval = APPLE80211_GI_SHORT;
            break;
        case IEEE80211_CHAN_WIDTH_20:
        case IEEE80211_CHAN_WIDTH_20_NOHT:
        default:
            if (ieee80211_node_supports_ht_sgi20(ni))
                data->interval = APPLE80211_GI_SHORT;
            break;
    }

    return kIOReturnSuccess;
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
    // AppleBCMWLANCore::getPRIVATE_MAC does not use kIOReturnUnsupported. It
    // rejects NULL with raw 0x16 and otherwise writes a packed 0x1c carrier
    // covering offsets +0x4..+0x1b. The exact semantic names of the trailing
    // "scanmac" fields are still only partially recovered, so keep the ABI
    // offset-accurate and state-backed instead of inventing names or collapsing
    // the slot into a generic unsupported path.
    if (data == nullptr)
        return kApple80211ErrInvalidArgumentRaw;

    memset(data, 0, sizeof(*data));
    data->version = APPLE80211_VERSION;
    data->enabled = 0;
    data->scanmac_state = cachedPrivateMacState;
    data->timeout_seconds = cachedPrivateMacTimeoutSeconds;
    memcpy(data->primary_mac, cachedPrivateMacPrimary, sizeof(data->primary_mac));
    memcpy(data->secondary_mac, cachedPrivateMacSecondary, sizeof(data->secondary_mac));
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getTHERMAL_INDEX(apple80211_thermal_index_t *data)
{
    // AppleBCMWLANCore::getTHERMAL_INDEX is a plain core-state carrier getter:
    // it writes a 32-bit scalar at caller offset +4 from core-state base +0x0.
    // Tahoe should therefore expose a real `version + u32` payload here rather
    // than leaving slot [500] on kIOReturnUnsupported.
    if (data == nullptr)
        return kIOReturnBadArgument;

    memset(data, 0, sizeof(*data));
    data->version = APPLE80211_VERSION;
    data->thermal_index = cachedThermalIndex;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getPOWER_BUDGET(apple80211_power_budget_t *data)
{
    // AppleBCMWLANCore::getPOWER_BUDGET is the neighboring scalar carrier:
    // it writes a 32-bit value at caller offset +4 from core-state base +0x4.
    // The important Tahoe mismatch was architectural reachability/ABI, not a
    // complex helper path, so slot [503] must return the same 8-byte carrier
    // shape instead of generic unsupported.
    if (data == nullptr)
        return kIOReturnBadArgument;

    memset(data, 0, sizeof(*data));
    data->version = APPLE80211_VERSION;
    data->power_budget = cachedPowerBudget;
    return kIOReturnSuccess;
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
    // The Tahoe LQM getter is a real producer, not an unsupported slot.
    // IO80211LQMData exposes a stable 0x24-byte carrier and AppleBCMWLANCore
    // forwards the same public ABI from its own owner state. The local port
    // does not have the hidden Broadcom LQM owner, but it can still preserve
    // the exact caller-visible carrier and validation contract.
    if (data == nullptr)
        return kIOReturnBadArgument;

    if (!hasCachedLqmConfig)
        initializeTahoeLqmConfig(&cachedLqmConfig);

    memcpy(data, &cachedLqmConfig, sizeof(*data));
    data->version = APPLE80211_VERSION;
    return kIOReturnSuccess;
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

    // AppleBCMWLANCore consumes and re-exposes a compact 8-byte public carrier:
    // u16 @ +0x4, u16 @ +0x8, u8 @ +0xa, u8 @ +0xb. Preserve that caller-visible
    // Tahoe ABI directly from the cached setter-side state.
    uint8_t *raw = reinterpret_cast<uint8_t *>(data);
    memset(raw, 0, 0x0c);
    memcpy(raw + 4, cachedDbgGuardTimeParams + 0, 2);
    memcpy(raw + 8, cachedDbgGuardTimeParams + 4, 2);
    raw[10] = cachedDbgGuardTimeParams[6];
    raw[11] = cachedDbgGuardTimeParams[7];
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getAWDL_RSDB_CAPS(apple80211_rsdb_capability *data)
{
    if (data == nullptr)
        return static_cast<IOReturn>(0xe00002c2);

    // AppleBCMWLANCore copies an 8-byte capability carrier from core state at
    // caller +0x4. This port has no separate AWDL/RSDB owner, so preserve the
    // same visible ABI with the locally cached carrier instead of generic
    // unsupported.
    uint8_t *raw = reinterpret_cast<uint8_t *>(data);
    memset(raw, 0, 0x0c);
    *reinterpret_cast<uint64_t *>(raw + 4) = cachedAwdlRsdbCaps;
    return kIOReturnSuccess;
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

    uint8_t *raw = reinterpret_cast<uint8_t *>(data);
    memset(raw, 0, 8);
    // Apple reads the dedicated "btc_profile_active" property here. Using the
    // coarse controller-wide btcMode conflates two different selectors and
    // loses the exact value that setBTCOEX_PROFILE_ACTIVE previously accepted.
    *reinterpret_cast<uint32_t *>(raw + 4) = cachedBtcoexProfileActive;
    return kIOReturnSuccess;
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

    uint8_t *raw = reinterpret_cast<uint8_t *>(data);
    memset(raw, 0, 8);
    // Apple writes the two-byte 2G chain-disable carrier at caller +0x4/+0x5.
    // Preserve the value accepted by the paired setter instead of returning a
    // fixed version-like marker.
    *reinterpret_cast<uint16_t *>(raw + 4) = cachedBtcoex2GChainDisable;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getBSS_BLACKLIST(bss_blacklist *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    // Apple routes this through an async blacklist owner, but the caller-visible
    // surface is still the same opaque public blob. Preserve the raw blob from
    // cached setter-side state instead of leaving slot [514] unsupported.
    memset(data, 0, sizeof(cachedBssBlacklist));
    if (hasCachedBssBlacklist)
        memcpy(data, cachedBssBlacklist, sizeof(cachedBssBlacklist));
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getTXRX_CHAIN_INFO(apple80211_txrx_chain_info *data)
{
    if (data == nullptr)
        return static_cast<IOReturn>(0xe00002c2);

    uint8_t *raw = reinterpret_cast<uint8_t *>(data);
    memset(raw, 0, 4);
    // Apple exposes four one-byte chain masks. The local Intel source does not
    // have Broadcom iovar owners for hw_rxchain/hw_txchain/txchain/rxchain, but
    // it does have a stable NSS/antenna view via DriverInfo. Mirror that public
    // shape rather than reporting generic unsupported.
    uint8_t nss = static_cast<uint8_t>(MAX(1, fHalService->getDriverInfo()->getTxNSS()));
    uint8_t limitedNss = nss < 8 ? nss : 8;
    uint8_t mask = static_cast<uint8_t>((1u << limitedNss) - 1u);
    raw[0] = mask;
    raw[1] = mask;
    raw[2] = mask;
    raw[3] = mask;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getMIMO_STATUS(apple80211_mimo_status *data)
{
    if (data == nullptr)
        return static_cast<IOReturn>(0xe00002c2);

    uint8_t *raw = reinterpret_cast<uint8_t *>(data);
    memset(raw, 0, 10);
    // AppleBCMWLANCore::getMIMO_STATUS exposes a compact 10-byte carrier. The
    // recovered body maps width codes through {0x50,0x14,0x28,0x50} for offsets
    // +4 and +9, so use the negotiated local channel width instead of treating
    // those bytes as NSS.
    raw[0] = 1;
    raw[1] = static_cast<uint8_t>(((cachedMimoConfig & 0xff) < 3) ? (cachedMimoConfig & 0xff) : 3);
    int txNss = fHalService->getDriverInfo()->getTxNSS();
    raw[6] = static_cast<uint8_t>(txNss < 3 ? txNss : 3);
    struct ieee80211com *ic = fHalService->get80211Controller();
    const uint8_t bwCode = (ic != nullptr && ic->ic_bss != nullptr)
                               ? tahoeMimoBandwidthCodeFromWidth(ic->ic_bss->ni_chw)
                               : 0;
    const uint8_t bwCarrier = tahoeMimoBandwidthCarrier(bwCode);
    raw[4] = bwCarrier;
    raw[9] = bwCarrier;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getWCL_FW_HOT_CHANNELS(apple80211_fw_hot_channels *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    // Apple delegates this selector into the net adapter. Until that hidden
    // owner is lifted, preserve the visible Tahoe ABI as a zeroed carrier
    // instead of a generic unsupported return.
    memset(data, 0, sizeof(uint32_t));
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getWCL_TRAFFIC_COUNTERS(apple80211_wcl_traffic_counters *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    // Apple exposes seven u64 counters here. The local port does not yet lift
    // the hidden WCL traffic owner, so preserve the same carrier shape with a
    // zeroed snapshot rather than returning unsupported.
    memset(data, 0, sizeof(uint64_t) * 7);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getRSN_XE(apple80211_rsn_xe_data *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    uint8_t *raw = reinterpret_cast<uint8_t *>(data);
    memset(raw, 0, 6 + sizeof(cachedRsnXe));
    *reinterpret_cast<uint16_t *>(raw + 4) = cachedRsnXeLength;
    size_t copyLen = static_cast<size_t>(cachedRsnXeLength);
    if (copyLen > sizeof(cachedRsnXe))
        copyLen = sizeof(cachedRsnXe);
    memcpy(raw + 6, cachedRsnXe, copyLen);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getSIB_COEX_STATUS(apple80211_sib_coex_status *data)
{
    if (data == nullptr)
        return static_cast<IOReturn>(kApple80211ErrInvalidArgumentRaw);

    uint8_t *raw = reinterpret_cast<uint8_t *>(data);
    memset(raw, 0, 12);
    // AppleBCMWLANCore writes version=1, then only asks the hidden SIB owner to
    // fill +0x4/+0x8 when that owner exists. The local port has no SIB owner, so
    // preserve the visible owner-missing success carrier instead of conflating
    // this selector with the legacy BTCOEX_MODE/OPTIONS state.
    *reinterpret_cast<uint32_t *>(raw + 0) = APPLE80211_VERSION;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getWCL_LOW_LATENCY_INFO_STATS(apple80211_wcl_low_latency_stats *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    // Apple exposes a fixed 0x7c-byte carrier here. The hidden low-latency
    // owner is still absent, so keep the caller-visible ABI as a zeroed
    // snapshot rather than leaving slot [534] unsupported.
    memset(data, 0, 0x7c);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getDYNSAR_DETAIL(apple80211_dynsar_detail *data)
{
    auto *raw = reinterpret_cast<tahoeDynsarDetailRequest *>(data);
    if (raw == nullptr || raw->entry_index >= 4)
        return static_cast<IOReturn>(kApple80211ErrInvalidArgumentRaw);

    // AppleBCMWLANCore::getDYNSAR_DETAIL is a strict versioned carrier: NULL or
    // entry_index>=4 returns raw 0x16, otherwise version=1 plus two bank-local
    // headers and a fixed 0x2d00 payload copy. Preserve that visible contract
    // from local cache instead of turning the slot into a generic unsupported
    // or inventing semantic field names that are still unrecovered.
    raw->version = APPLE80211_VERSION;
    const uint32_t bank = raw->bank_selector != 0 ? 1U : 0U;
    raw->header0 = cachedDynsarHeader0[bank];
    raw->header1 = cachedDynsarHeader1[bank];
    memcpy(raw->payload, cachedDynsarPayload[raw->entry_index], sizeof(raw->payload));
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getSLOW_WIFI_FEATURE_ENABLED(apple80211_slow_wifi_feature_enabled *data)
{
    auto *raw = reinterpret_cast<tahoeSlowWifiFeatureEnabled *>(data);
    if (raw == nullptr)
        return static_cast<IOReturn>(0xe00002c2);

    // AppleBCMWLANCore writes a compact `version + u32 enabled` carrier here.
    // Keep that exact public shape from local state instead of exposing the
    // selector as unsupported while the deeper hidden policy owner remains
    // unrecovered.
    raw->version = APPLE80211_VERSION;
    raw->enabled = cachedSlowWifiFeatureEnabled ? 1U : 0U;
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
    raw->enabled = cachedLowLatencyEnabled;
    raw->power_save = cachedLowLatencyPowerSave;
    raw->window = cachedLowLatencyWindow;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getWCL_GET_TX_BLANKING_STATUS(uint *data)
{
    // Apple accepts NULL and simply skips the store. Preserve that visible
    // contract instead of forcing an argument error.
    if (data != nullptr)
        *data = cachedTxBlankingStatus ? 1U : 0U;
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
    // The recovered Apple path retries configureWoWTestModeEntry() up to five
    // times around the `wake_event` IOVAR and leaves WoW enabled after a
    // successful setup. The local Tahoe port still lacks Apple's commander
    // backend, but it can still mirror those externally visible side effects
    // instead of behaving like a one-shot scalar cache.
    if (mode < 1 || mode > 600)
        return static_cast<IOReturn>(0xe00002c2);

    for (int retries = 0; retries < 5; retries++) {
        cachedWowTestMode = mode;
        cachedWowEnabled = true;
        if (instance && instance->fNetIf != nullptr)
            instance->fNetIf->setWoWEnabled(true);
        return kIOReturnSuccess;
    }
    return static_cast<IOReturn>(0xe00002c2);
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

    // Apple gates TVPM by feature bit 0x3b before validating the caller's PPM
    // index. The visible range check is intentionally non-intuitive: values
    // 1..100 reject with 0xe00002bc, while 0 and >=101 are accepted.
    if (((cachedOSFeatureFlags >> 0x3b) & 1ULL) == 0)
        return kIOReturnBadArgumentTahoe;
    if (data->power_budget >= 1 && data->power_budget <= 100)
        return kIOReturnBadArgumentTahoe;

    cachedPowerBudget = data->power_budget;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setUSB_HOST_NOTIFICATION(apple80211_usb_host_notification_data *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    TahoeAsyncCommandContext asyncContext{};
    const IOReturn rc =
        (instance != nullptr)
            ? instance->getTahoeCommander().runSetUSBHostNotification(data, &asyncContext)
            : kIOReturnBadArgumentTahoe;
    if (rc != kIOReturnSuccess)
        return rc;

    // The recovered Apple producer first routes through the hidden +0x1510
    // owner and then programs commander IOVARs. That hidden type gate is still
    // unrecovered here, so the new TahoeCommander layer mirrors the Apple-
    // visible command split and preserves the two IOVAR payloads separately.
    const auto &owner = instance->getTahoeOwnerRegistry().usbHostNotification;
    cachedUsbHostNotificationSeq = owner.sequenceNumber;
    cachedUsbHostNotificationChange = owner.change;
    cachedUsbHostNotificationPresent = owner.present;
    return rc;
}

IOReturn AirportItlwmSkywalkInterface::
setBYPASS_TX_POWER_CAP(apple80211_bypass_tx_power_cap *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    // Apple stores the first byte as a bool and immediately pushes the new
    // policy to firmware. The sync-only TahoeCommander layer now owns that
    // internal state and send-eligibility decision instead of leaving this as
    // a freestanding interface-side bool cache.
    TahoeAsyncCommandContext asyncContext{};
    const IOReturn rc =
        (instance != nullptr)
            ? instance->getTahoeCommander().runSetBypassTxPowerCap(data, &asyncContext)
            : kIOReturnBadArgumentTahoe;
    if (rc != kIOReturnSuccess)
        return rc;

    cachedBypassTxPowerCapEnabled =
        instance->getTahoeOwnerRegistry().txPowerCapBypass.enabled;
    return rc;
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
#ifdef USE_APPLE_SUPPLICANT
    struct ieee80211com *ic = fHalService->get80211Controller();
    if (!data)
        return kIOReturnError;
    static_assert(sizeof(ic->ic_rsn_ie_override) == APPLE80211_MAX_RSN_IE_LEN, "Max RSN IE length mismatch");
    const uint16_t copyLen = TahoeAssociationContracts::boundedRsnIeLength(
        data->len, APPLE80211_MAX_RSN_IE_LEN);
    memset(ic->ic_rsn_ie_override, 0, sizeof(ic->ic_rsn_ie_override));
    if (copyLen > 0)
        memcpy(ic->ic_rsn_ie_override, data->ie, copyLen);
    if (ic->ic_state == IEEE80211_S_RUN && ic->ic_bss != nullptr &&
        copyLen >= 2 && ic->ic_rsn_ie_override[1] > 0)
        ieee80211_save_ie(ic->ic_rsn_ie_override, &ic->ic_bss->ni_rsnie);
    return kIOReturnSuccess;
#else
    return kIOReturnUnsupported;
#endif
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

    struct apple80211_rsn_ie_data rsn_ie_data;
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
        memset(&rsn_ie_data, 0, sizeof(rsn_ie_data));
        rsn_ie_data.version = APPLE80211_VERSION;
        rsn_ie_data.len = ad->ad_rsn_ie[1] + 2;
        memcpy(rsn_ie_data.ie, ad->ad_rsn_ie, rsn_ie_data.len);
        setRSN_IE(&rsn_ie_data);

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
    return kIOReturnSuccess;
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
 * snr / nf / cca remain zero because itlwm does not currently
 * expose per-beacon noise-floor or channel CCA metrics from the
 * iwx / iwm HAL; populating them is a separate parity layer.
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
            ed->rssi = (uint32_t)(-(0 - IWM_MIN_DBM - ic->ic_bss->ni_rssi));
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

    const auto *snapshot = reinterpret_cast<const triggerCCSnapshot *>(data);
    const uint8_t *raw = reinterpret_cast<const uint8_t *>(data);
    const uint32_t mode = *reinterpret_cast<const uint32_t *>(raw + 0x8);

    // Tahoe 26.x calls APPLE80211_IOC_WCL_TRIGGER_CC during the scan manager
    // bring-up path.  The Apple producer does not treat it as optional:
    // AppleBCMWLANCore::setWCL_TRIGGER_CC first copies the first four qwords of
    // the request into adapter-owned state, then accepts mode 0/1 and returns
    // 0xe00002bc only for any other mode.  Returning unsupported here is what
    // produced the live INTERNAL WCL_TRIGGER_CC -> 0xe00002c7 failure.
    memcpy(cachedTriggerCC, snapshot, sizeof(*snapshot));
    cachedTriggerCCMode = mode;
    hasCachedTriggerCC = true;

    if (mode == 0 || mode == 1)
        return kIOReturnSuccess;
    return kIOReturnBadArgumentTahoe;
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
    if (!data)
        return kIOReturnBadArgumentTahoe;

    // Apple stores the first dword and hands it into the battery-save path.
    // Even before the deeper power manager parity is finished, this IOC must
    // remain a state carrier rather than a blind success stub.
    cachedBatteryPowerSaveMode = *reinterpret_cast<const uint32_t *>(data);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setPOWER_PROFILE(apple80211_power_profile *data)
{
    if (!data)
        return kIOReturnBadArgumentTahoe;

    // AppleBCMWLANCore caches the first power-profile dword at +0x29f0 before
    // dispatching into its power-policy vtable. Preserve the same cached owner
    // state here instead of dropping the profile on the floor.
    cachedPowerProfile = *reinterpret_cast<const uint32_t *>(data);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setIPV4_PARAMS(apple80211_ipv4_params *data)
{
    if (!data)
        return kIOReturnBadArgumentTahoe;

    const auto *ipv4 = reinterpret_cast<const tahoeIPv4ParamsContract *>(data);

    // The Apple producer persists IPv4/mask/router fields in core state,
    // triggers IPv4 notifications, and only then decides whether keepalive data
    // needs refreshing. Returning success without carrying the values breaks the
    // producer/consumer contract even if user-visible network behavior has not
    // reached that path yet.
    cachedIPv4Address = ipv4->address;
    cachedIPv4Netmask = ipv4->netmask;
    cachedIPv4Reserved = 0;
    cachedIPv4Gateway = ipv4->gateway;
    cachedIPv4GatewayTail = ipv4->gatewayTail;

    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setIPV6_PARAMS(apple80211_ipv6_params *data)
{
    if (!data)
        return kIOReturnBadArgumentTahoe;

    const auto *ipv6 = reinterpret_cast<const tahoeIPv6ParamsHeader *>(data);
    cachedIPv6Count = MIN(ipv6->count, static_cast<uint32_t>(10));
    memset(cachedIPv6Addresses, 0, sizeof(cachedIPv6Addresses));
    memset(cachedIPv6LinkLocalAddress, 0, sizeof(cachedIPv6LinkLocalAddress));
    if (cachedIPv6Count != 0)
        memcpy(cachedIPv6Addresses, ipv6->addresses, cachedIPv6Count * sizeof(cachedIPv6Addresses[0]));

    // Apple seeds a dedicated link-local fe80:: prefix after refreshing the
    // cached IPv6 table. Keep that exact state edge so later consumers do not
    // observe "success" with no link-local cache behind it.
    cachedIPv6LinkLocalAddress[0] = 0xfe;
    cachedIPv6LinkLocalAddress[1] = 0x80;
    if (cachedIPv6Count != 0)
        memcpy(&cachedIPv6LinkLocalAddress[8], &cachedIPv6Addresses[0][8], 8);

    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setINFRA_ENUMERATED(apple80211_infra_enumerated *data)
{
    if (!data)
        return kIOReturnBadArgumentTahoe;

    // Apple's contract here really is minimal: validate non-null and succeed.
    // Our old kIOReturnError path still diverged from that producer shape.
    cachedInfraEnumerated = true;
    return kIOReturnSuccess;
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
            struct apple80211_rsn_ie_data rsn_ie_data;
            memset(&rsn_ie_data, 0, sizeof(rsn_ie_data));
            rsn_ie_data.version = APPLE80211_VERSION;
            rsn_ie_data.len = associationOwner.boundedRsnIeLength;
            memcpy(rsn_ie_data.ie, rsn_ie, rsn_ie_data.len);
            setRSN_IE(&rsn_ie_data);
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
     * Tahoe APSTA lifetime symmetry: VIRTUAL_IF_DELETE is not a
     * protocol vtable slot here, so route the IOCTL switch directly
     * to the controller-owned APSTA delete path. The delete carrier
     * contains only the BSD name; the controller matches that name
     * against the existing role-7 APSTA owner and otherwise fails
     * closed without creating AP state or reporting AP/GO support.
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
    // WCLRoamManager sends selector 0x1ac with exactly one payload byte.
    // AppleBCMWLANCore rejects NULL with raw 0x16, then forwards data[0] as
    // the `roam_off` bool to AppleBCMWLANRoamAdapter::setRoamLock(bool).
    if (data == nullptr)
        return kApple80211ErrInvalidArgumentRaw;

    cachedWclRoamLocked = data->roam_off != 0;
    hasCachedWclRoamLock = true;
    return kIOReturnSuccess;
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
    // The recovered Tahoe visible contract is minimal: Apple rejects NULL with
    // 0xe00002bc and otherwise reports success from the public setter surface
    // before the deeper traffic-policy owner work.
    if (data == nullptr)
        return static_cast<IOReturn>(0xe00002bc);
    return kIOReturnSuccess;
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
    // AppleBCMWLANCore::setCHANNEL preserves the public request carrier, gates
    // channel ids >= 0x100 with raw 0x16, then only later resolves the hidden
    // chanspec/property owner. Keep the same caller-visible split here instead
    // of dropping the selector on the floor as generic unsupported.
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;
    if (data->channel.channel >= 0x100)
        return kApple80211ErrInvalidArgumentRaw;

    cachedRequestedChannel = *data;
    hasCachedRequestedChannel = true;

    struct ieee80211com *ic = fHalService ? fHalService->get80211Controller() : nullptr;
    if (ic == nullptr || data->channel.channel == 0)
        return static_cast<IOReturn>(0xe00002c2);

    for (int i = 0; i <= IEEE80211_CHAN_MAX; i++) {
        if (ic->ic_channels[i].ic_freq == 0)
            continue;
        if (ieee80211_chan2ieee(ic, &ic->ic_channels[i]) == data->channel.channel)
            return kIOReturnSuccess;
    }
    return static_cast<IOReturn>(0xe00002c2);
}

IOReturn AirportItlwmSkywalkInterface::
setTXPOWER(apple80211_txpower_data *data)
{
    // AppleBCMWLANCore::setTXPOWER re-encodes the caller-visible unit/value
    // pair back into the one-byte qtxpower transport. Preserve that transport
    // instead of falling back to `ic_txpower`.
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    uint8_t raw;
    if (data->version == APPLE80211_VERSION &&
        data->txpower_unit == APPLE80211_UNIT_MW) {
        raw = encodeAppleTahoeQTxpowerFromMw(static_cast<uint32_t>(MAX(0, data->txpower)));
    } else {
        int scaled = data->txpower << 2;
        scaled = MAX(-128, MIN(127, scaled));
        raw = static_cast<uint8_t>(static_cast<int8_t>(scaled));
    }

    setTahoeCachedQTxpowerRaw(fHalService, raw);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setRATE(apple80211_rate_data *data)
{
    // AppleBCMWLANCore::setRATE updates the public bg_rate property path. We
    // do not carry Apple's property manager, but preserving the public dword
    // avoids advertising the selector as unsupported.
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    cachedBgRate = data->rate[0];
    hasCachedBgRate = true;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setIBSS_MODE(apple80211_network_data *data)
{
    // AppleBCMWLANCore::setIBSS_MODE has a visible success contract even
    // though the real owner path toggles hidden proximity/NAN interfaces. Keep
    // the same public carrier coverage instead of leaving Tahoe on generic
    // unsupported.
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    cachedIbssMode = data->nd_mode;
    cachedIbssAuthLower = data->nd_auth_lower;
    cachedIbssAuthUpper = data->nd_auth_upper;
    cachedIbssChannel = data->nd_channel;
    cachedIbssSsidLen = MIN(data->nd_ssid_len, static_cast<uint32_t>(sizeof(cachedIbssSsid)));
    memset(cachedIbssSsid, 0, sizeof(cachedIbssSsid));
    if (cachedIbssSsidLen != 0)
        memcpy(cachedIbssSsid, data->nd_ssid, cachedIbssSsidLen);
    hasCachedIbssNetwork = true;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setIE(apple80211_ie_data *data)
{
    TahoeAsyncCommandContext asyncContext{};
    const IOReturn rc =
        (instance != nullptr)
            ? instance->getTahoeCommander().runSetIE(data, &asyncContext)
            : kApple80211ErrInvalidArgumentRaw;
    if (rc != kIOReturnSuccess)
        return rc;

    const auto &owner = instance->getTahoeOwnerRegistry().ie;
    cachedAssocIeLen = owner.assocIeLen;
    memset(cachedAssocIe, 0, sizeof(cachedAssocIe));
    if (cachedAssocIeLen != 0)
        memcpy(cachedAssocIe, owner.assocIe, cachedAssocIeLen);
    hasCachedAssocIe = owner.hasAssocIe;

    cachedVendorIeLen = owner.vendorIeLen;
    cachedVendorIeFlags = owner.vendorIeFlags;
    memset(cachedVendorIe, 0, sizeof(cachedVendorIe));
    if (cachedVendorIeLen != 0)
        memcpy(cachedVendorIe, owner.vendorIe, cachedVendorIeLen);
    hasCachedVendorIe = owner.hasVendorIe;
    return rc;
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
    const IOReturn rc =
        (instance != nullptr)
            ? instance->getTahoeCommander().runSetOFFLOADNDP(data, &asyncContext)
            : kApple80211ErrInvalidArgumentRaw;
    if (rc != kIOReturnSuccess)
        return rc;

    const auto &owner = instance->getTahoeOwnerRegistry().ndp;
    cachedIPv6Count = owner.count;
    memset(cachedIPv6Addresses, 0, sizeof(cachedIPv6Addresses));
    memcpy(cachedIPv6Addresses, owner.addresses, sizeof(owner.addresses));
    memset(cachedIPv6LinkLocalAddress, 0, sizeof(cachedIPv6LinkLocalAddress));
    memcpy(cachedIPv6LinkLocalAddress, owner.linkLocalSeed, sizeof(owner.linkLocalSeed));
    return rc;
}

IOReturn AirportItlwmSkywalkInterface::
setOFFLOAD_ARP(apple80211_offload_arp_data *data)
{
    // AppleBCMWLANCore::setOFFLOAD_ARP rejects NULL and "no Infra owner" with
    // raw 0x16, then copies the IPv4 / keepalive carrier into core-owned state
    // before running the hidden IPv4 / keepalive notification hooks. We do not
    // have those hidden gated callbacks yet, but the same cached IPv4 fields
    // already back our lifted `setIPV4_PARAMS(...)` path, so keep the exact
    // field coverage instead of dropping this producer on the floor.
    if (data == nullptr || instance == nullptr || instance->fNetIf == nullptr)
        return kApple80211ErrInvalidArgumentRaw;

    cachedDhcpRenewalData = data->keepalive_enabled != 0;
    if (data->has_ipv4_address != 0) {
        cachedIPv4Address = data->ipv4_address;
        cachedIPv4Reserved = 0;
    }
    cachedIPv4Gateway = data->gateway;
    cachedIPv4GatewayTail = data->gateway_tail;

    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setGAS_REQ(apple80211_gas_query_t *data)
{
    // AppleBCMWLANCore::setGAS_REQ rejects NULL with 0xe00002c2, then
    // delegates the request into the GAS adapter. Preserve that public gate.
    if (data == nullptr)
        return static_cast<IOReturn>(0xe00002c2);

    cachedGasQueryIssued = true;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setBTCOEX_PROFILE(apple80211_btcoex_profile *data)
{
    TahoeAsyncCommandContext asyncContext{};
    const IOReturn rc =
        (instance != nullptr)
            ? instance->getTahoeCommander().runSetBTCOEXProfile(data, &asyncContext)
            : static_cast<IOReturn>(0xe00002c2);
    if (rc != kIOReturnSuccess)
        return rc;

    const auto &owner = instance->getTahoeOwnerRegistry().btcoex;
    memcpy(cachedBtcoexProfiles, owner.profileTable, sizeof(cachedBtcoexProfiles));
    cachedBtcoexProfileValidMask = owner.profileValidMask;
    return rc;
}

IOReturn AirportItlwmSkywalkInterface::
setBTCOEX_PROFILE_ACTIVE(apple80211_btcoex_profile_active_data *data)
{
    TahoeAsyncCommandContext asyncContext{};
    const IOReturn rc =
        (instance != nullptr)
            ? instance->getTahoeCommander().runSetBTCOEXProfileActive(data, &asyncContext)
            : static_cast<IOReturn>(0xe00002c2);
    if (rc != kIOReturnSuccess)
        return rc;

    cachedBtcoexProfileActive = instance->getTahoeOwnerRegistry().btcoex.activeProfile;
    return rc;
}

IOReturn AirportItlwmSkywalkInterface::
setBTCOEX_2G_CHAIN_DISABLE(apple80211_btcoex_2g_chain_disable *data)
{
    TahoeAsyncCommandContext asyncContext{};
    const IOReturn rc =
        (instance != nullptr)
            ? instance->getTahoeCommander().runSetBTCOEX2GChainDisable(data, &asyncContext)
            : static_cast<IOReturn>(0xe00002c2);
    if (rc != kIOReturnSuccess)
        return rc;

    cachedBtcoex2GChainDisable = instance->getTahoeOwnerRegistry().btcoex.chainDisable;
    return rc;
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
    // AppleBCMWLANCore::setSET_PROPERTY runs through a gated property callback
    // path. Preserve the caller-visible "delegated setter" contract instead of
    // reporting generic unsupported.
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    cachedSetPropertyIoctlSeen = true;
    return kIOReturnSuccess;
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
    // Apple routes this through the proximity owner family after validating
    // PMK length and role. The local commander layer now keeps the same public
    // state transition and owner-targeted callback decision instead of
    // collapsing the selector into an unconditional failure.
    constexpr uint32_t kLocalTahoeProximityOwnerId = 1;
    TahoeAsyncCommandContext asyncContext{};
    return (instance != nullptr)
               ? instance->getTahoeCommander().runSetRangingAuthenticate(
                     data, kLocalTahoeProximityOwnerId, &asyncContext)
               : static_cast<IOReturn>(0xe0000001);
}

IOReturn AirportItlwmSkywalkInterface::
setPM_MODE(apple80211_pm_mode *data)
{
    // AppleBCMWLANCore::setPM_MODE is a thin producer: it forwards the dword at
    // caller +0x4 into NetAdapter::configurePM(...). The recovered helper maps
    // any non-zero mode onto the same PM request bit family-side consumers use
    // when they later query powersave state. Re-enter the lifted POWERSAVE path
    // instead of leaving slot [584] unsupported.
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    cachedPmMode = data->mode;

    apple80211_powersave_data pd{};
    pd.version = APPLE80211_VERSION;
    pd.powersave_level = data->mode;
    const IOReturn rc = setPOWERSAVE(&pd);
    return rc;
}

IOReturn AirportItlwmSkywalkInterface::
setLQM_CONFIG(apple80211_lqm_config_t *data)
{
    // AppleBCMWLANCore::setLQM_CONFIG is not a blind setter. Tahoe validates
    // the public 0x24-byte carrier before forwarding it into the LQM owner.
    // Reproduce those exact caller-visible checks so the port no longer
    // advertises slot [577] as unsupported.
    if (data == nullptr)
        return kIOReturnBadArgument;
    if (data->sample_period_ms < 1000 || data->tx_per_interval_ms < 1000 ||
        data->rx_loss_interval_ms < 1000)
        return static_cast<IOReturn>(0x2d);

    const uint8_t *thresholdBytes =
        reinterpret_cast<const uint8_t *>(data) + 0x11;
    for (size_t i = 0; i < 7; i++) {
        if (isInvalidTahoeLqmThresholdByte(thresholdBytes[i]))
            return static_cast<IOReturn>(0x16);
    }
    for (size_t i = 0; i < sizeof(data->opaque_tail_19); i++) {
        if (data->opaque_tail_19[i] > 99)
            return static_cast<IOReturn>(0x16);
    }

    memcpy(&cachedLqmConfig, data, sizeof(cachedLqmConfig));
    cachedLqmConfig.version = APPLE80211_VERSION;
    hasCachedLqmConfig = true;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_REAL_TIME_MODE(apple80211_wcl_real_time_mode *data)
{
    const auto *mode = reinterpret_cast<const tahoeWclRealTimeMode *>(data);

    // AppleBCMWLANCore::setWCL_REAL_TIME_MODE is a real producer with two
    // branches only: NULL -> 0xe00002bc, nonzero first byte -> NetAdapter
    // real-time mode, zero -> NetAdapter default mode. Keep the same gate and
    // preserve the selected mode in driver-owned state rather than leaving the
    // slot as inline success.
    if (mode == nullptr)
        return kIOReturnBadArgumentTahoe;

    cachedRealTimeMode = mode->enabled != 0;
    return kIOReturnSuccess;
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
    // AppleBCMWLANCore::setWCL_ROAM_USER_CACHE delegates into the roam adapter
    // `cmdROAM_USER_CACHE(...)`. The recovered helper family shows that the
    // caller-visible payload carries channel entries from offset 0x0 in 0x0c
    // strides, a channel count at +0x78, and an override byte at +0x7a.
    // Persist that exact 0x7c blob so later roam-owner lifts retain the same
    // request state instead of losing it behind an inline success stub.
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    memcpy(cachedUserRoamCache, data, sizeof(*data));
    hasCachedUserRoamCache = true;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_REASSOC(apple80211_reassoc *data)
{
    struct ieee80211com *ic = fHalService->get80211Controller();

    // A reassociation start invalidates an EXTERNALLY delivered PMK:
    // the host supplicant must re-install a fresh PMK through
    // CIPHER_KEY(PMK), CIPHER_KEY(MSK), or CUR_PMK on the
    // reassociated network. A locally owned PSK PMK, however, must
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
    // WCLRoamProfile::setRoamingProfile(legacy) consumes exactly 0x60 bytes,
    // and AppleBCMWLANRoamAdapter stores that profile before it reconfigures
    // join preferences / Multi-AP state. The local port does not have the
    // hidden roam owner object yet, but dropping the payload on the floor was
    // still an architectural mismatch because later roam decisions had no
    // stable source of truth at all.
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    memcpy(cachedLegacyRoamProfileConfig, data, sizeof(*data));
    hasCachedLegacyRoamProfileConfig = true;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_ROAM_PROFILE_CONFIG(apple80211_roam_profile_config *data)
{
    // WCLRoamProfile::setRoamingProfile(modern) ships a 0x23c payload into the
    // roam adapter. Apple's downstream helper fans this out into join
    // preference and per-band policy programming. Persist the exact carrier so
    // the local port no longer acknowledges this producer and immediately loses
    // the only recovered roam configuration state.
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    memcpy(cachedRoamProfileConfig, data, sizeof(*data));
    hasCachedRoamProfileConfig = true;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_ARP_MODE(apple80211_wcl_arp_mode *data)
{
    // AppleBCMWLANCore::setWCL_ARP_MODE has three distinct pieces:
    // - NULL -> 0xe00002bc
    // - mode 0/1 choose the keepalive/GARP owner path, anything else -> 0xe00002bc
    // - optional WNM sideband update consumes the two u16s at offsets +0/+2
    //
    // We still lack Apple's hidden keepalive and WNM owners, but the local
    // port now preserves the exact recovered carrier and reuses the lifted
    // OFFLOAD_ARP path so the same IPv4/keepalive state is available to later
    // consumer paths instead of vanishing in an inline success stub.
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;
    if (data->mode > 1)
        return kIOReturnBadArgumentTahoe;

    memcpy(cachedWclArpMode, data, sizeof(*data));
    hasCachedWclArpMode = true;

    apple80211_offload_arp_data arp{};
    arp.version = APPLE80211_VERSION;
    arp.has_ipv4_address = cachedIPv4Address != 0 ? 1U : 0U;
    arp.ipv4_address = cachedIPv4Address;
    arp.keepalive_enabled = data->enabled != 0 ? 1U : 0U;
    arp.gateway = cachedIPv4Gateway;
    arp.gateway_tail = cachedIPv4GatewayTail;

    const IOReturn carrierRc = setOFFLOAD_ARP(&arp);
    if (carrierRc != kIOReturnSuccess && carrierRc != kApple80211ErrInvalidArgumentRaw)
        return carrierRc;

    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_CONFIG_BG_MOTIONPROFILE(apple80211_bg_motion_profile *data)
{
    // AppleBGScanAdapter first validates its internal motion-profile mapping,
    // then programs PNO/EPNO from the incoming blob. The recovered helper
    // rejects a zero PNO-count byte with a generic error, so keep that gate
    // instead of advertising unconditional success.
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;
    if (data->raw[1] == 0)
        return kIOReturnError;

    memcpy(cachedBgMotionProfile, data, sizeof(cachedBgMotionProfile));
    hasCachedBgMotionProfile = true;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_CONFIG_BG_NETWORK(apple80211_bg_network *data)
{

    // Apple clears PFN state, resets its internal "cached network available"
    // flags, and only then copies the full 0x12c0 request into adapter-owned
    // storage. Preserve the full request and clear the current cache iterator
    // so later BGSCAN_CACHE_RESULT consumers observe the new network set instead
    // of stale cached nodes. The scan trigger itself belongs to WCL_CONFIG_BGSCAN
    // or explicit scan requests, not this producer.
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    memcpy(cachedBgNetwork, data, sizeof(*data));
    hasCachedBgNetwork = true;
    fNextNodeToSend = nullptr;
    fScanResultWrapping = false;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_CONFIG_BGSCAN(apple80211_bg_scan *data)
{
    struct ieee80211com *ic = fHalService->get80211Controller();

    // AppleBCMWLANCore::setWCL_CONFIG_BGSCAN is a tiny command multiplexer:
    // byte 0 resets PFN mode, byte 1 rewrites scan_nprobes using byte 2, and
    // byte 3 toggles suspend/resume using byte 4. Mirror that split locally so
    // the request is preserved and the available bgscan owner is exercised.
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    memcpy(cachedBgScanConfig, data, sizeof(*data));
    hasCachedBgScanConfig = true;

    if (data->raw[0] != 0) {
        if (ic->ic_flags & IEEE80211_F_BGSCAN)
            ic->ic_flags &= ~IEEE80211_F_BGSCAN;
        if (ic->ic_flags & IEEE80211_F_ASCAN)
            ic->ic_flags &= ~IEEE80211_F_ASCAN;
    }

    IOReturn rc = kIOReturnSuccess;
    if (data->raw[3] != 0) {
        if (data->raw[4] != 0) {
            if (ic->ic_bgscan_start != nullptr && ic->ic_state == IEEE80211_S_RUN) {
                const int bgscanRc = ic->ic_bgscan_start(ic);
                if (bgscanRc != 0)
                    rc = static_cast<IOReturn>(bgscanRc);
            }
        } else {
            if (ic->ic_flags & IEEE80211_F_BGSCAN)
                ic->ic_flags &= ~IEEE80211_F_BGSCAN;
        }
    }

    return rc;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_CONFIG_BG_PARAMS(apple80211_bg_params *data)
{

    // AppleBGScanAdapter::setWCL_CONFIG_BG_PARAMS carries two independent
    // sub-commands out of a 0x20 blob. The local bgscan engine does not expose
    // those hidden helper entrypoints, but preserving the exact payload keeps the
    // owner-side state reachable instead of acknowledging and discarding it. This
    // producer does not start a scan; that belongs to WCL_CONFIG_BGSCAN or an
    // explicit scan request.
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    memcpy(cachedBgParams, data, sizeof(*data));
    hasCachedBgParams = true;

    return kIOReturnSuccess;
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

    // AppleBCMWLANCore::setWCL_QOS_PARAMS is not one monolithic blob. The
    // recovered NetAdapter helper applies independent knobs under a flag byte:
    // long retry limit, RTS threshold, powersave mode, and two "lifetime"
    // buckets. We only claim parity where the local port has a real owner:
    // RTS threshold lives in `ieee80211com`, powersave already has a lifted
    // Tahoe IOC path, and the remaining Apple-configured words are preserved
    // as driver-owned state instead of being acknowledged and discarded.
    if (qos == nullptr)
        return kIOReturnBadArgumentTahoe;

    cachedQosFlags = qos->flags;
    if ((qos->flags & 0x01) != 0)
        cachedQosLongRetryLimit = qos->long_retry_limit;
    if ((qos->flags & 0x02) != 0)
        cachedQosRtsThreshold = qos->rts_threshold;
    if ((qos->flags & 0x04) != 0)
        cachedQosLifetimeAc3 = qos->lifetime_ac3;
    if ((qos->flags & 0x08) != 0)
        cachedQosLifetimeAc2 = qos->lifetime_ac2;

    if ((qos->flags & 0x02) != 0) {
        ic->ic_rtsthreshold = MIN(static_cast<uint32_t>(IEEE80211_RTS_MAX),
                                  cachedQosRtsThreshold);
    }

    if ((qos->flags & 0x10) != 0) {
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

    cachedScanHomeAwayTime = data->milliseconds;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_ULOFDMA_STATE(apple80211_wcl_ulofdma_state *data)
{
    const auto *state = reinterpret_cast<const tahoeWclUlofdmaState *>(data);

    // AppleBCMWLANCore::setWCL_ULOFDMA_STATE is a plain 11ax-adapter producer:
    // NULL -> 0xe00002bc, otherwise forward the first dword to the owner at
    // core +0x15c8. Even before that hidden owner is lifted 1:1, slot [608]
    // must preserve the exact dword carrier instead of sitting on generic
    // kIOReturnUnsupported.
    if (state == nullptr)
        return kIOReturnBadArgumentTahoe;

    cachedUlofdmaState = state->mode;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setMIMO_CONFIG(apple80211_mimo_config *data)
{
    const auto *config = reinterpret_cast<const uint32_t *>(data);

    // AppleBCMWLANCore::setMIMO_CONFIG rejects NULL with 0xe00002bc, reads the
    // first dword as the caller-visible mode, and then pushes that mode into
    // the MIMO power-save owner. The hidden owner is still outside this batch,
    // but acknowledging slot [614] without caching the selected mode was still
    // a producer mismatch.
    if (config == nullptr)
        return kIOReturnBadArgumentTahoe;

    cachedMimoConfig = *config;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setFACETIME_WIFICALLING_PARAMS(apple80211_facetime_wificalling_params *data)
{
    const auto *params = reinterpret_cast<const tahoeFaceTimeWiFiCallingParams *>(data);

    // Apple stores a single FaceTime/WiFi-calling status dword and hands it to
    // setWiFiCallPolicies(...). Preserve that status verbatim so slot [623] no
    // longer drops an Apple-owned carrier behind kIOReturnUnsupported.
    if (params == nullptr)
        return kIOReturnBadArgumentTahoe;

    cachedFaceTimeWiFiCallingStatus = params->status;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setDUAL_POWER_MODE(apple80211_dual_power_mode_params *data)
{
    const auto *params = reinterpret_cast<const tahoeDualPowerModeParams *>(data);

    // AppleBCMWLANCore::setDUAL_POWER_MODE persists two signed dwords at core
    // offsets +0x4d3c/+0x4d40, conditionally arms a one-byte "type-2 present"
    // flag at +0x4d44, and always re-enters tx-power-cap state handling. The
    // local port still lacks that config-owner choreography, but it must stop
    // advertising slot [631] as entirely unsupported.
    if (params == nullptr)
        return kIOReturnBadArgumentTahoe;

    cachedDualPowerModePrimary = params->primary;
    cachedDualPowerModeSecondary = params->secondary;
    if (instance != nullptr)
        instance->getTahoeOwnerRegistry().syncDualPowerMode(params->primary, params->secondary);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setCONGESTION_CTRL_IND(apple80211_congestion_control_indication *data)
{
    const auto *ind = reinterpret_cast<const tahoeCongestionControlIndication *>(data);

    // AppleBCMWLANCore::setCONGESTION_CTRL_IND is a tiny core-state write:
    // log gate, then a bool at core +0x79d2. There is no extra hidden helper
    // to wait for, so slot [634] should behave as a real bool carrier here.
    if (ind == nullptr)
        return kIOReturnBadArgumentTahoe;

    cachedCongestionControlEnabled = ind->enabled != 0;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setLMTPC_CONFIG(apple80211_lmtpc_config *data)
{
    const auto *config = reinterpret_cast<const tahoeLmtpcConfig *>(data);

    // AppleBCMWLANCore::setLMTPC_CONFIG rejects NULL, copies one byte into the
    // core-owned cache at +0x4594, and then re-enters setLMTPC(). Preserve the
    // same byte carrier so slot [638] no longer diverges at the producer ABI.
    if (config == nullptr)
        return kIOReturnBadArgumentTahoe;

    cachedLmtpcValue = config->value;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setLE_SCAN_PARAM(apple80211_le_scan_params *data)
{
    const auto *params =
        reinterpret_cast<const TahoeLeScanContracts::Carrier *>(data);

    // AppleBCMWLANCore::setLE_SCAN_PARAM first requires the BTLE reporting
    // owner at core +0x15a8; once present, NULL is a successful no-op and the
    // request copies only six dwords from +0x4..+0x18 into owner +0x24..+0x38.
    // Model the owner-present state locally instead of preserving the ignored
    // caller dword at +0x0.
    if (params == nullptr)
        return kIOReturnSuccess;

    if (TahoeLeScanContracts::copyOwnerStateFromCarrier(params,
            &cachedLeScanOwnerState))
        hasCachedLeScanParams = true;
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

    const uint8_t *raw = reinterpret_cast<const uint8_t *>(data);
    memset(cachedDbgGuardTimeParams, 0, sizeof(cachedDbgGuardTimeParams));
    memcpy(cachedDbgGuardTimeParams + 0, raw + 4, 2);
    memcpy(cachedDbgGuardTimeParams + 4, raw + 8, 2);
    cachedDbgGuardTimeParams[6] = raw[10];
    cachedDbgGuardTimeParams[7] = raw[11];
    hasCachedDbgGuardTimeParams = true;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setPRIVATE_MAC(apple80211_private_mac_data *data)
{
    if (data != nullptr) {
        const uint8_t *raw = reinterpret_cast<const uint8_t *>(data);
        cachedPrivateMacTimeoutSeconds = *reinterpret_cast<const uint32_t *>(raw + 0x0c);
        memcpy(cachedPrivateMacPrimary, raw + 0x10, sizeof(cachedPrivateMacPrimary));
    }

    // AppleBCMWLANCore::setPRIVATE_MAC returns the raw Tahoe code 0x16 even on
    // the visible success-looking path, so match that public contract instead
    // of generic unsupported.
    return static_cast<IOReturn>(0x16);
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
    setProperty(kIOMACAddress, const_cast<uint8_t *>(mac), kIOEthernetAddressSize);
    postMessage(APPLE80211_M_LINK_ADDRESS_CHANGED, const_cast<uint8_t *>(mac), 6, true);

    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setTHERMAL_INDEX(apple80211_thermal_index_t *data)
{
    if (data != nullptr)
        cachedThermalIndex = *reinterpret_cast<const uint32_t *>(reinterpret_cast<const uint8_t *>(data) + 4);

    return kIOReturnBadArgumentTahoe;
}

IOReturn AirportItlwmSkywalkInterface::
setDYNAMIC_RSSI_WINDOW_CONFIG(apple80211_dynamic_rssi_window_config *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    cachedDynamicRssiWindowConfig = *reinterpret_cast<const uint32_t *>(data);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setREALTIME_QOS_MSCS(apple80211_state_data *data)
{
    if (data == nullptr)
        return kApple80211ErrInvalidArgumentRaw;

    cachedRealTimeQosMscs = *reinterpret_cast<const uint32_t *>(reinterpret_cast<const uint8_t *>(data) + 4);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setRSN_XE(apple80211_rsn_xe_data *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    const uint8_t *raw = reinterpret_cast<const uint8_t *>(data);
    cachedRsnXeLength = *reinterpret_cast<const uint16_t *>(raw + 4);
    size_t copyLen = MIN(static_cast<size_t>(cachedRsnXeLength), sizeof(cachedRsnXe));
    memset(cachedRsnXe, 0, sizeof(cachedRsnXe));
    memcpy(cachedRsnXe, raw + 6, copyLen);
    hasCachedRsnXe = true;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setGAS_ABORT(void *)
{
    // AppleBCMWLANCore forwards this selector to the GAS adapter and exposes no
    // public payload. Match the visible success contract instead of generic
    // unsupported.
    return kIOReturnSuccess;
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

    memcpy(cachedBcnMuteConfig, data, sizeof(cachedBcnMuteConfig));
    hasCachedBcnMuteConfig = true;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setEAP_FILTER_CONFIG(apple80211_eap_filter_config *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    cachedEapFilterConfig = *reinterpret_cast<const uint32_t *>(data);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_ASSOCIATED_SLEEP(apple80211_associated_sleep_config *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    memcpy(cachedAssociatedSleepConfig, data, sizeof(cachedAssociatedSleepConfig));
    hasCachedAssociatedSleepConfig = true;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_SOI_CONFIG(appl80211_sleep_on_inactivity_config *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    const uint8_t *raw = reinterpret_cast<const uint8_t *>(data);
    memcpy(cachedSoiConfig, raw, sizeof(cachedSoiConfig));
    hasCachedSoiConfig = true;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setOS_ELIGIBILITY(apple80211_os_eligibility *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    cachedOsEligibility = *reinterpret_cast<const uint32_t *>(data);
    return kIOReturnSuccess;
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

    memset(cachedBssBlacklist, 0, sizeof(cachedBssBlacklist));
    memcpy(cachedBssBlacklist, data, sizeof(cachedBssBlacklist));
    hasCachedBssBlacklist = true;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setMWS_WIFI_TYPE_7_BITMAP_WIFI_ENH(apple80211_mws_wifi_channel_bitmap *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    memcpy(cachedMwsWifiType7Bitmap, reinterpret_cast<const uint32_t *>(data),
           sizeof(cachedMwsWifiType7Bitmap));
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setMWS_COEX_BITMAP_WIFI_ENH(apple80211_mws_wifi_channel_bitmap *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    memcpy(cachedMwsCoexBitmap, reinterpret_cast<const uint32_t *>(data),
           sizeof(cachedMwsCoexBitmap));
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setMWS_DISABLE_OCL_BITMAP_WIFI_ENH(apple80211_mws_wifi_channel_bitmap *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    memcpy(cachedMwsDisableOclBitmap, reinterpret_cast<const uint32_t *>(data),
           sizeof(cachedMwsDisableOclBitmap));
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setMWS_RFEM_CONFIG_WIFI_ENH(apple80211_mws_rfem_config *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    memcpy(cachedMwsRfemConfig, reinterpret_cast<const uint32_t *>(data),
           sizeof(cachedMwsRfemConfig));
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setMWS_ASSOC_PROTECTION_BITMAP_WIFI_ENH(apple80211_mws_wifi_channel_bitmap *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    memcpy(cachedMwsAssocProtectionBitmap, reinterpret_cast<const uint32_t *>(data),
           sizeof(cachedMwsAssocProtectionBitmap));
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setMWS_SCAN_FREQ_WIFI_ENH(apple80211_mws_scan_freq *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    memcpy(cachedMwsScanFreq, reinterpret_cast<const uint32_t *>(data),
           sizeof(cachedMwsScanFreq));
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setMWS_SCAN_FREQ_MODE_WIFI_ENH(apple80211_mws_scan_freq_mode *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    const uint8_t *raw = reinterpret_cast<const uint8_t *>(data);
    // AppleBCMWLANCore reorders the public carrier before caching it:
    // +0x24 -> slot0, then +0x04/+0x08/+0x0c.
    cachedMwsScanFreqMode[0] = *reinterpret_cast<const uint32_t *>(raw + 0x24);
    cachedMwsScanFreqMode[1] = *reinterpret_cast<const uint32_t *>(raw + 0x04);
    cachedMwsScanFreqMode[2] = *reinterpret_cast<const uint32_t *>(raw + 0x08);
    cachedMwsScanFreqMode[3] = *reinterpret_cast<const uint32_t *>(raw + 0x0c);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setMWS_CONDITION_ID_BITMAP_WIFI_ENH(apple80211_mws_condition_id_config *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    const uint8_t *raw = reinterpret_cast<const uint8_t *>(data);
    if (raw[2] == 0)
        return kIOReturnBadArgumentTahoe;

    cachedMwsConditionIdCount = raw[2];
    size_t bytesToCopy = static_cast<size_t>(cachedMwsConditionIdCount) * 0x28;
    if (bytesToCopy > sizeof(cachedMwsConditionIdConfig))
        bytesToCopy = sizeof(cachedMwsConditionIdConfig);

    memset(cachedMwsConditionIdConfig, 0, sizeof(cachedMwsConditionIdConfig));
    memcpy(cachedMwsConditionIdConfig, raw + 0x28, bytesToCopy);
    hasCachedMwsConditionIdConfig = true;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setMWS_ANTENNA_SELECTION_WIFI_ENH(apple80211_mws_antenna_selection *data)
{
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    const uint8_t *raw = reinterpret_cast<const uint8_t *>(data);
    memcpy(cachedMwsAntennaSelection, raw, 8 * sizeof(uint16_t));
    cachedMwsAntennaSelection[8] = *reinterpret_cast<const uint16_t *>(raw + 0x10);
    return kIOReturnSuccess;
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
    // AppleBCMWLANCore::setWCL_WNM_OPS is a real producer with only one gate
    // visible at the core layer: NULL -> 0xe00002bc, otherwise delegate into
    // WnmAdapter::configureWnmFeatures(...). The WCL-side consumer mutates a
    // large opaque blob up to offsets 0x334+, so preserve the recovered full
    // carrier instead of keeping slot [625] unsupported.
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    memcpy(cachedWnmConfig, data, sizeof(*data));
    hasCachedWnmConfig = true;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_WNM_OFFLOAD(apple80211_wcl_wnm_offload_t *data)
{
    // AppleBCMWLANCore::setWCL_WNM_OFFLOAD has the same core-layer contract:
    // NULL -> 0xe00002bc, otherwise delegate into WnmAdapter
    // configureWnmOffloadFeatures(...). Recovered WCLWnmAgent helpers mutate
    // the caller blob up to offset 0x2c, so keep the full opaque carrier.
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    memcpy(cachedWnmOffload, data, sizeof(*data));
    hasCachedWnmOffload = true;
    return kIOReturnSuccess;
}

extern OSDictionary *convertScanToDictionary(apple80211_scan_result *a1);

static int convertNodeToScanResult(ItlHalService *fHalService,
                                   struct ieee80211_node *fNextNodeToSend,
                                   apple80211_scan_result *result)
{
    bzero(result, sizeof(*result));
    result->version = APPLE80211_VERSION;
    if (fNextNodeToSend->ni_rsnie_tlv && fNextNodeToSend->ni_rsnie_tlv_len > 0) {
        result->asr_ie_len = fNextNodeToSend->ni_rsnie_tlv_len;
        memcpy(result->asr_ie_data, fNextNodeToSend->ni_rsnie_tlv,
               MIN(result->asr_ie_len, sizeof(result->asr_ie_data)));
    } else {
        result->asr_ie_len = 0;
    }
    result->asr_beacon_int = fNextNodeToSend->ni_intval;
    // Tahoe airportd candidate ingestion is sensitive to scan-result shape.
    // V16 left asr_rates empty because the loop iterated result->asr_nrates
    // immediately after bzero(), so nrates stayed 0 until after the copy.
    // That produces a malformed candidate even when the BSS is present in ic_tree.
    result->asr_nrates = MIN((uint8_t)fNextNodeToSend->ni_rates.rs_nrates,
                             (uint8_t)APPLE80211_MAX_RATES);
    for (uint8_t i = 0; i < result->asr_nrates; i++)
        result->asr_rates[i] = fNextNodeToSend->ni_rates.rs_rates[i];
    result->asr_age = (uint32_t)(airport_up_time() - fNextNodeToSend->ni_age_ts);
    result->asr_cap = fNextNodeToSend->ni_capinfo;
    result->asr_channel.version = APPLE80211_VERSION;
    result->asr_channel.channel = ieee80211_chan2ieee(fHalService->get80211Controller(), fNextNodeToSend->ni_chan);
    result->asr_channel.flags = ieeeChanFlag2appleScanFlagVentura(fNextNodeToSend->ni_chan->ic_flags);
    result->asr_noise = fHalService->getDriverInfo()->getBSSNoise();
    result->asr_rssi = -(0 - IWM_MIN_DBM - fNextNodeToSend->ni_rssi);
    result->asr_snr = result->asr_rssi - result->asr_noise;
    memcpy(result->asr_bssid, fNextNodeToSend->ni_bssid, IEEE80211_ADDR_LEN);
    result->asr_ssid_len = fNextNodeToSend->ni_esslen;
    if (result->asr_ssid_len != 0)
        memcpy(&result->asr_ssid, fNextNodeToSend->ni_essid, result->asr_ssid_len);
    return 0;
}

IOReturn AirportItlwmSkywalkInterface::
getCURRENT_NETWORK(apple80211_scan_result *sr)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    if (ic->ic_state != IEEE80211_S_RUN || ic->ic_bss == NULL)
        return kIOReturnError;
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
            fNextNodeToSend = RB_MIN(ieee80211_tree, &fHalService->get80211Controller()->ic_tree);
            if (fNextNodeToSend == NULL) {
                return 5;
            }
        }
    }
    convertNodeToScanResult(fHalService, fNextNodeToSend, sr);
    
    fNextNodeToSend = RB_NEXT(ieee80211_tree, &HalService->get80211Controller()->ic_tree, fNextNodeToSend);
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
        if (ni->ni_chan == NULL || ni->ni_chan == IEEE80211_CHAN_ANYC)
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

    struct ieee80211com *ic = fHalService->get80211Controller();
    if (ic->ic_state != IEEE80211_S_RUN || ic->ic_bss == NULL)
        return kIOReturnError;

    seedBssManagerMcs();

    struct ieee80211_node *ni = ic->ic_bss;
    const uint16_t chanSpec = buildTahoeWclCurrentBssChanSpec(ic, ni->ni_chan);
    if (chanSpec == 0) {
        XYLog("WCL [526] %s invalid current channel\n", __FUNCTION__);
        return kIOReturnError;
    }

    bzero(data, sizeof(*data));

    auto *payload = reinterpret_cast<TahoeWclCurrentBssPayload *>(data->data);
    uint32_t ieLen = 0;
    if (ni->ni_rsnie_tlv != nullptr && ni->ni_rsnie_tlv_len != 0) {
        ieLen = ni->ni_rsnie_tlv_len;
        if (ieLen > APPLE80211_WCL_BSS_INFO_MAX_IE_LEN)
            ieLen = APPLE80211_WCL_BSS_INFO_MAX_IE_LEN;
        memcpy(payload->ie, ni->ni_rsnie_tlv, ieLen);
    }

    payload->meta.ieLen = ieLen;
    payload->meta.chanSpec = chanSpec;
    payload->meta.ssidLen = MIN(static_cast<uint8_t>(sizeof(payload->meta.ssid)), ni->ni_esslen);
    if (payload->meta.ssidLen != 0) {
        memcpy(payload->meta.ssid, ni->ni_essid, payload->meta.ssidLen);
        payload->meta.flags |= 0x6;
    }

    const uint16_t primaryChannel =
        static_cast<uint16_t>(ieee80211_chan2ieee(ic, ni->ni_chan));
    payload->meta.primaryChannel = static_cast<uint8_t>(MIN(primaryChannel, 0xff));
    memcpy(payload->meta.bssid, ni->ni_bssid, sizeof(payload->meta.bssid));
    payload->meta.rssi = -(0 - IWM_MIN_DBM - ni->ni_rssi);
    payload->meta.beaconInterval = ni->ni_intval;
    payload->meta.capability = ni->ni_capinfo;

    return kIOReturnSuccess;
}
