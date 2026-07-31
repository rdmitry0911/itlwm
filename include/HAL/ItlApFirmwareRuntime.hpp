/*
 * Copyright (C) 2026 itlwm contributors
 *
 * Shared ownership for the AP profile passed from AirportItlwm's role-7
 * interface to the Intel firmware backends.  The upper contract lends every
 * pointer only for startAPMode(); firmware restart and sleep replay therefore
 * require an owned snapshot rather than retaining the caller's stack buffers.
 */

#ifndef ItlApFirmwareRuntime_hpp
#define ItlApFirmwareRuntime_hpp

#include <HAL/ItlHalService.hpp>
#include <net80211/ieee80211_sae_engine.h>

enum ItlApFirmwareResourceStage : uint8_t {
    kItlApFirmwareResourceIdle = 0,
    kItlApFirmwareResourceBeacon,
    kItlApFirmwareResourceMac,
    kItlApFirmwareResourceBinding,
    kItlApFirmwareResourceMulticastStation,
    kItlApFirmwareResourceBroadcastStation,
    kItlApFirmwareResourceRunning,
    kItlApFirmwareResourceStopping,
};

struct ItlApFirmwareRuntime {
    struct ItlHalApConfig config;
    uint8_t ssid[IEEE80211_NWID_LEN];
    uint8_t credential[64];
    uint8_t rsnIE[64];
    uint8_t beacon[MCLBYTES];

    uint8_t stage;
    uint8_t macId;
    uint8_t macColor;
    uint8_t phyId;
    uint8_t broadcastStaId;
    uint8_t multicastStaId;
    uint8_t firstClientStaId;
    uint16_t broadcastQueueId;
    uint16_t multicastQueueId;
    uint16_t clientQueueId;
    uint16_t clientAid;
    uint8_t clientMac[IEEE80211_ADDR_LEN];
    uint8_t clientStationMac[IEEE80211_ADDR_LEN];
    uint8_t clientAssocIEs[512];
    size_t clientAssocIEsLength;
    bool clientAssociationPending;
    bool clientAuthenticated;
    bool clientAssociated;
    bool clientAuthorized;
    bool clientStationInstalled;
    bool clientPairwiseKeyInstalled;
    bool groupKeyInstalled;
    uint8_t clientPairwiseKey[16];
    uint8_t groupKey[16];
    uint8_t groupKeyId;
    uint64_t clientPairwiseTxPn;
    uint64_t groupTxPn;
    uint64_t clientRxPn[16];

    /*
     * WPA3 cannot use Tahoe's WPA2 key callback as its authenticator:
     * driver-resident SAE is the sole owner of the PMK.  Keep the common
     * SAE/4-way state next to the firmware-neutral client lifetime so IWM
     * and IWX cannot acquire different security semantics.
     */
    uint32_t localAuthMagic;
    struct ieee80211_sae_ap *sae;
    uint8_t localRsnState;
    uint8_t pmk[IEEE80211_PMK_LEN];
    uint8_t anonce[EAPOL_KEY_NONCE_LEN];
    uint8_t gtk[16];
    uint8_t igtk[16];
    struct ieee80211_ptk ptk;
    uint64_t replayCounter;
    uint8_t clientRsnIE[64];
    size_t clientRsnIELength;
    uint8_t gtkKeyId;
    uint8_t igtkKeyId;
    enum { kPowerSaveQueueLength = 16 };
    mbuf_t powerSaveQueue[kPowerSaveQueueLength];
    uint8_t powerSaveQueueHead;
    uint8_t powerSaveQueueTail;
    uint8_t powerSaveQueueCount;
    bool clientPowerSave;
    bool timSet;
    bool samePhyAsPrimary;
    bool replayAfterWake;
};

static constexpr uint32_t kItlApLocalAuthMagic = 0x41505333U;

static inline void
itl_ap_firmware_power_save_purge(struct ItlApFirmwareRuntime *runtime)
{
    if (runtime == NULL)
        return;
    while (runtime->powerSaveQueueCount != 0) {
        mbuf_t packet =
            runtime->powerSaveQueue[runtime->powerSaveQueueHead];
        runtime->powerSaveQueue[runtime->powerSaveQueueHead] = NULL;
        runtime->powerSaveQueueHead = static_cast<uint8_t>(
            (runtime->powerSaveQueueHead + 1) %
            ItlApFirmwareRuntime::kPowerSaveQueueLength);
        runtime->powerSaveQueueCount--;
        if (packet != NULL)
            mbuf_freem(packet);
    }
    runtime->powerSaveQueueHead = 0;
    runtime->powerSaveQueueTail = 0;
    runtime->clientPowerSave = false;
    runtime->timSet = false;
}

static inline void
itl_ap_firmware_sae_reset(struct ItlApFirmwareRuntime *runtime)
{
    if (runtime == NULL)
        return;
    if (runtime->localAuthMagic == kItlApLocalAuthMagic)
        ieee80211_sae_ap_destroy(&runtime->sae);
    else
        runtime->sae = NULL;
    explicit_bzero(runtime->pmk, sizeof(runtime->pmk));
}

static inline void
itl_ap_firmware_client_crypto_reset(struct ItlApFirmwareRuntime *runtime)
{
    if (runtime == NULL)
        return;
    runtime->clientAuthorized = false;
    runtime->clientPairwiseKeyInstalled = false;
    runtime->clientPairwiseTxPn = 0;
    explicit_bzero(runtime->clientPairwiseKey,
                   sizeof(runtime->clientPairwiseKey));
    explicit_bzero(runtime->clientRxPn, sizeof(runtime->clientRxPn));
    itl_ap_firmware_power_save_purge(runtime);
    runtime->localRsnState = 0;
    runtime->replayCounter = 0;
    explicit_bzero(runtime->anonce, sizeof(runtime->anonce));
    explicit_bzero(&runtime->ptk, sizeof(runtime->ptk));
}

static inline void
itl_ap_firmware_runtime_reset(struct ItlApFirmwareRuntime *runtime)
{
    if (runtime == NULL)
        return;
    if (runtime->localAuthMagic == kItlApLocalAuthMagic)
        itl_ap_firmware_sae_reset(runtime);
    if (runtime->localAuthMagic == kItlApLocalAuthMagic)
        itl_ap_firmware_power_save_purge(runtime);
    explicit_bzero(runtime, sizeof(*runtime));
    runtime->localAuthMagic = kItlApLocalAuthMagic;
    runtime->stage = kItlApFirmwareResourceIdle;
    runtime->broadcastQueueId = UINT16_MAX;
    runtime->multicastQueueId = UINT16_MAX;
    runtime->clientQueueId = UINT16_MAX;
}

static inline int
itl_ap_firmware_runtime_snapshot(struct ItlApFirmwareRuntime *runtime,
                                 const struct ItlHalApConfig *config)
{
    if (runtime == NULL || config == NULL || config->ssid == NULL ||
        config->ssidLength == 0 ||
        config->ssidLength > sizeof(runtime->ssid) ||
        config->credentialLength > sizeof(runtime->credential) ||
        (config->credentialLength != 0 && config->credential == NULL) ||
        config->rsnIELength > sizeof(runtime->rsnIE) ||
        (config->rsnIELength != 0 && config->rsnIE == NULL) ||
        config->beaconTemplate == NULL || config->beaconTemplateLength == 0 ||
        config->beaconTemplateLength > sizeof(runtime->beacon) ||
        config->channel == 0 || config->channel > IEEE80211_CHAN_MAX ||
        config->beaconInterval == 0 || config->dtimPeriod == 0 ||
        IEEE80211_IS_MULTICAST(config->bssid) ||
        IEEE80211_ADDR_EQ(config->bssid, etheranyaddr))
        return EINVAL;

    itl_ap_firmware_runtime_reset(runtime);
    runtime->config = *config;
    memcpy(runtime->ssid, config->ssid, config->ssidLength);
    if (config->credentialLength != 0)
        memcpy(runtime->credential, config->credential,
               config->credentialLength);
    if (config->rsnIELength != 0)
        memcpy(runtime->rsnIE, config->rsnIE, config->rsnIELength);
    memcpy(runtime->beacon, config->beaconTemplate,
           config->beaconTemplateLength);

    runtime->config.ssid = runtime->ssid;
    runtime->config.credential = config->credentialLength != 0 ?
        runtime->credential : NULL;
    runtime->config.rsnIE = config->rsnIELength != 0 ?
        runtime->rsnIE : NULL;
    runtime->config.beaconTemplate = runtime->beacon;
    if (config->authUpper == 0x1000) {
        arc4random_buf(runtime->gtk, sizeof(runtime->gtk));
        arc4random_buf(runtime->igtk, sizeof(runtime->igtk));
        runtime->gtkKeyId = 1;
        runtime->igtkKeyId = 4;
    }
    return 0;
}

#endif /* ItlApFirmwareRuntime_hpp */
