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
    bool samePhyAsPrimary;
    bool replayAfterWake;
};

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
}

static inline void
itl_ap_firmware_runtime_reset(struct ItlApFirmwareRuntime *runtime)
{
    if (runtime == NULL)
        return;
    explicit_bzero(runtime, sizeof(*runtime));
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
    return 0;
}

#endif /* ItlApFirmwareRuntime_hpp */
