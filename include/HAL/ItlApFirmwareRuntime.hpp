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
#include <HAL/ItlApBlockAckRuntime.hpp>
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

enum { kItlApFirmwareMaxClients = 4 };

struct ItlApFirmwareClientRuntime {
    uint8_t staId;
    uint16_t queueId;
    uint16_t clientAid;
    uint8_t clientMac[IEEE80211_ADDR_LEN];
    uint8_t clientStationMac[IEEE80211_ADDR_LEN];
    uint8_t clientAssocIEs[512];
    size_t clientAssocIEsLength;
    bool clientAssociationPending;
    bool clientReassociationPending;
    bool clientAuthenticated;
    bool clientAssociated;
    bool clientAuthorized;
    bool clientStationInstalled;
    bool clientQos;
    bool clientStationQos;
    bool clientHt;
    bool clientStationHt;
    uint8_t clientHtNss;
    uint8_t clientStationHtNss;
    uint16_t clientHtCapabilities;
    uint8_t clientHtAmpduParams;
    uint8_t clientHtMcs[2];
    uint16_t clientRxBaMask;
    struct ItlApRxBaRuntime clientRxBa[kItlApRxBaTidCount];
    uint16_t clientTxBaMask;
    uint8_t clientTxDialogToken;
    uint16_t clientTxSequence[kItlApRxBaTidCount];
    struct ItlApTxBaRuntime clientTxBa[kItlApRxBaTidCount];
    bool rateControlConfigured;
    uint16_t clientLegacyRateMask;
    bool clientPairwiseKeyInstalled;
    uint8_t clientPairwiseKey[16];
    uint64_t clientPairwiseTxPn;
    uint64_t clientRxPn[16];

    /*
     * WPA3 cannot use Tahoe's WPA2 key callback as its authenticator:
     * driver-resident SAE is the sole owner of the PMK.  Keep the common
     * SAE/4-way state next to the firmware-neutral client lifetime so IWM
     * and IWX cannot acquire different security semantics.
     */
    struct ieee80211_sae_ap *sae;
    uint8_t localRsnState;
    uint8_t pmk[IEEE80211_PMK_LEN];
    uint8_t anonce[EAPOL_KEY_NONCE_LEN];
    struct ieee80211_ptk ptk;
    uint64_t replayCounter;
    uint8_t clientRsnIE[64];
    size_t clientRsnIELength;
    enum { kPowerSaveQueueLength = 16 };
    mbuf_t powerSaveQueue[kPowerSaveQueueLength];
    uint8_t powerSaveQueueHead;
    uint8_t powerSaveQueueTail;
    uint8_t powerSaveQueueCount;
    bool clientPowerSave;
    bool timSet;
    bool inUse;
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
    bool groupKeyInstalled;
    uint8_t groupKey[16];
    uint8_t groupKeyId;
    uint64_t groupTxPn;
    uint32_t localAuthMagic;
    uint8_t gtk[16];
    uint8_t igtk[16];
    uint8_t gtkKeyId;
    uint8_t igtkKeyId;
    struct ItlApFirmwareClientRuntime clients[kItlApFirmwareMaxClients];
    bool hidden;
    bool samePhyAsPrimary;
    bool replayAfterWake;
};

static constexpr uint32_t kItlApLocalAuthMagic = 0x41505333U;

static inline void
itl_ap_firmware_power_save_purge(struct ItlApFirmwareClientRuntime *client)
{
    if (client == NULL)
        return;
    while (client->powerSaveQueueCount != 0) {
        mbuf_t packet =
            client->powerSaveQueue[client->powerSaveQueueHead];
        client->powerSaveQueue[client->powerSaveQueueHead] = NULL;
        client->powerSaveQueueHead = static_cast<uint8_t>(
            (client->powerSaveQueueHead + 1) %
            ItlApFirmwareClientRuntime::kPowerSaveQueueLength);
        client->powerSaveQueueCount--;
        if (packet != NULL)
            mbuf_freem(packet);
    }
    client->powerSaveQueueHead = 0;
    client->powerSaveQueueTail = 0;
    client->clientPowerSave = false;
    client->timSet = false;
}

static inline void
itl_ap_firmware_sae_reset(struct ItlApFirmwareClientRuntime *client)
{
    if (client == NULL)
        return;
    ieee80211_sae_ap_destroy(&client->sae);
    explicit_bzero(client->pmk, sizeof(client->pmk));
}

static inline void
itl_ap_firmware_client_crypto_reset(
    struct ItlApFirmwareClientRuntime *client)
{
    if (client == NULL)
        return;
    client->clientAuthorized = false;
    client->clientPairwiseKeyInstalled = false;
    client->clientPairwiseTxPn = 0;
    explicit_bzero(client->clientPairwiseKey,
                   sizeof(client->clientPairwiseKey));
    explicit_bzero(client->clientRxPn, sizeof(client->clientRxPn));
    itl_ap_firmware_power_save_purge(client);
    client->localRsnState = 0;
    client->replayCounter = 0;
    explicit_bzero(client->anonce, sizeof(client->anonce));
    explicit_bzero(&client->ptk, sizeof(client->ptk));
}

static inline void
itl_ap_firmware_client_reset(struct ItlApFirmwareClientRuntime *client)
{
    if (client == NULL)
        return;
    itl_ap_firmware_sae_reset(client);
    itl_ap_firmware_power_save_purge(client);
    for (size_t tid = 0; tid < kItlApRxBaTidCount; tid++) {
        itl_ap_rx_ba_stop(&client->clientRxBa[tid]);
        itl_ap_tx_ba_reset(&client->clientTxBa[tid]);
    }
    explicit_bzero(client, sizeof(*client));
    client->staId = UINT8_MAX;
    client->queueId = UINT16_MAX;
}

static inline void
itl_ap_firmware_runtime_reset(struct ItlApFirmwareRuntime *runtime)
{
    if (runtime == NULL)
        return;
    if (runtime->localAuthMagic == kItlApLocalAuthMagic) {
        for (size_t index = 0; index < kItlApFirmwareMaxClients; index++)
            itl_ap_firmware_client_reset(&runtime->clients[index]);
    }
    explicit_bzero(runtime, sizeof(*runtime));
    runtime->localAuthMagic = kItlApLocalAuthMagic;
    runtime->stage = kItlApFirmwareResourceIdle;
    runtime->broadcastQueueId = UINT16_MAX;
    runtime->multicastQueueId = UINT16_MAX;
    for (size_t index = 0; index < kItlApFirmwareMaxClients; index++) {
        runtime->clients[index].staId = UINT8_MAX;
        runtime->clients[index].queueId = UINT16_MAX;
    }
}

static inline size_t
itl_ap_firmware_client_limit(const struct ItlApFirmwareRuntime *runtime)
{
    if (runtime == NULL || runtime->config.maxStations == 0)
        return 0;
    return MIN(static_cast<size_t>(runtime->config.maxStations),
               static_cast<size_t>(kItlApFirmwareMaxClients));
}

static inline int
itl_ap_firmware_set_client_limit(struct ItlApFirmwareRuntime *runtime,
                                 uint32_t maxStations)
{
    if (runtime == NULL || maxStations == 0)
        return EINVAL;
    uint32_t effective = MIN(
        maxStations, static_cast<uint32_t>(kItlApFirmwareMaxClients));
    /* A live maxassoc reduction must never orphan an existing station in a
     * now-unsearchable slot.  Apple supplies current+requested to firmware,
     * but slot churn can leave a live client above that numeric count. */
    for (size_t index = 0; index < kItlApFirmwareMaxClients; index++) {
        if (runtime->clients[index].inUse)
            effective = MAX(effective, static_cast<uint32_t>(index + 1));
    }
    runtime->config.maxStations = effective;
    return 0;
}

/*
 * Apple changes closednet on an already running SoftAP.  Firmware still
 * owns the beacon cadence, so keep the currently uploaded template in the
 * runtime snapshot and rewrite only its SSID IE: a hidden beacon carries a
 * zero-length SSID, while directed probe responses are rebuilt with the
 * retained real SSID by ItlApOpenRuntime.
 */
static inline int
itl_ap_beacon_set_hidden(uint8_t *beacon, size_t *beaconLength,
                         size_t beaconCapacity, const uint8_t *ssid,
                         size_t ssidLength, bool *currentHidden,
                         bool hidden)
{
    const size_t fixedLength = sizeof(struct ieee80211_frame) + 12;
    if (beacon == NULL || beaconLength == NULL || currentHidden == NULL ||
        ssid == NULL || ssidLength == 0 || ssidLength > IEEE80211_NWID_LEN ||
        *beaconLength < fixedLength || *beaconLength > beaconCapacity)
        return EINVAL;
    if (*currentHidden == hidden)
        return 0;

    size_t offset = fixedLength;
    while (offset + 2 <= *beaconLength) {
        uint8_t *element = beacon + offset;
        const size_t elementLength = element[1];
        const size_t totalLength = 2 + elementLength;
        if (offset + totalLength > *beaconLength)
            return EINVAL;
        if (element[0] != IEEE80211_ELEMID_SSID) {
            offset += totalLength;
            continue;
        }

        const size_t tailOffset = offset + totalLength;
        const size_t tailLength = *beaconLength - tailOffset;
        if (hidden) {
            if (elementLength != ssidLength ||
                memcmp(element + 2, ssid, ssidLength) != 0)
                return EINVAL;
            memmove(element + 2, element + 2 + ssidLength, tailLength);
            explicit_bzero(beacon + *beaconLength - ssidLength,
                           ssidLength);
            element[1] = 0;
            *beaconLength -= ssidLength;
        } else {
            if (elementLength != 0 ||
                *beaconLength + ssidLength > beaconCapacity)
                return EINVAL;
            memmove(element + 2 + ssidLength, element + 2, tailLength);
            element[1] = static_cast<uint8_t>(ssidLength);
            memcpy(element + 2, ssid, ssidLength);
            *beaconLength += ssidLength;
        }
        *currentHidden = hidden;
        return 0;
    }
    return ENOENT;
}

static inline int
itl_ap_firmware_set_hidden(struct ItlApFirmwareRuntime *runtime, bool hidden)
{
    if (runtime == NULL || runtime->config.beaconTemplate != runtime->beacon)
        return EINVAL;
    return itl_ap_beacon_set_hidden(
        runtime->beacon, &runtime->config.beaconTemplateLength,
        sizeof(runtime->beacon), runtime->ssid, runtime->config.ssidLength,
        &runtime->hidden, hidden);
}

static inline struct ItlApFirmwareClientRuntime *
itl_ap_firmware_find_client(struct ItlApFirmwareRuntime *runtime,
                            const uint8_t *station)
{
    if (runtime == NULL || station == NULL)
        return NULL;
    const size_t limit = itl_ap_firmware_client_limit(runtime);
    for (size_t index = 0; index < limit; index++) {
        struct ItlApFirmwareClientRuntime *client =
            &runtime->clients[index];
        if (client->inUse &&
            IEEE80211_ADDR_EQ(client->clientMac, station))
            return client;
    }
    return NULL;
}

static inline struct ItlApFirmwareClientRuntime *
itl_ap_firmware_allocate_client(struct ItlApFirmwareRuntime *runtime,
                                const uint8_t *station)
{
    struct ItlApFirmwareClientRuntime *existing =
        itl_ap_firmware_find_client(runtime, station);
    if (existing != NULL)
        return existing;
    if (runtime == NULL || station == NULL)
        return NULL;
    const size_t limit = itl_ap_firmware_client_limit(runtime);
    for (size_t index = 0; index < limit; index++) {
        struct ItlApFirmwareClientRuntime *client =
            &runtime->clients[index];
        if (client->inUse)
            continue;
        itl_ap_firmware_client_reset(client);
        client->inUse = true;
        client->staId = static_cast<uint8_t>(
            runtime->firstClientStaId + index);
        client->clientAid = static_cast<uint16_t>(index + 1);
        IEEE80211_ADDR_COPY(client->clientMac, station);
        return client;
    }
    /* Authentication state has no firmware resource yet.  Reclaim one such
     * incomplete slot so a stream of abandoned Auth/SAE commits cannot lock
     * every association slot indefinitely. */
    for (size_t index = 0; index < limit; index++) {
        struct ItlApFirmwareClientRuntime *client =
            &runtime->clients[index];
        if (client->clientAssociated || client->clientStationInstalled)
            continue;
        itl_ap_firmware_client_reset(client);
        client->inUse = true;
        client->staId = static_cast<uint8_t>(
            runtime->firstClientStaId + index);
        client->clientAid = static_cast<uint16_t>(index + 1);
        IEEE80211_ADDR_COPY(client->clientMac, station);
        return client;
    }
    return NULL;
}

static inline size_t
itl_ap_firmware_client_index(const struct ItlApFirmwareRuntime *runtime,
                             const struct ItlApFirmwareClientRuntime *client)
{
    if (runtime == NULL || client == NULL ||
        client < &runtime->clients[0] ||
        client >= &runtime->clients[kItlApFirmwareMaxClients])
        return SIZE_MAX;
    return static_cast<size_t>(client - &runtime->clients[0]);
}

static inline struct ItlApFirmwareClientRuntime *
itl_ap_firmware_client_at(struct ItlApFirmwareRuntime *runtime, size_t index)
{
    if (runtime == NULL || index >= itl_ap_firmware_client_limit(runtime) ||
        !runtime->clients[index].inUse)
        return NULL;
    return &runtime->clients[index];
}

static inline struct ItlApFirmwareClientRuntime *
itl_ap_firmware_find_tx_client(struct ItlApFirmwareRuntime *runtime,
                               const uint8_t *destination)
{
    if (runtime == NULL || destination == NULL)
        return NULL;
    if (!IEEE80211_IS_MULTICAST(destination)) {
        struct ItlApFirmwareClientRuntime *client =
            itl_ap_firmware_find_client(runtime, destination);
        return client != NULL && client->clientAssociated ? client : NULL;
    }
    const size_t limit = itl_ap_firmware_client_limit(runtime);
    for (size_t index = 0; index < limit; index++) {
        if (runtime->clients[index].inUse &&
            runtime->clients[index].clientAssociated &&
            (runtime->config.rsnIELength == 0 ||
             runtime->clients[index].clientAuthorized))
            return &runtime->clients[index];
    }
    return NULL;
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
        config->maxStations == 0 ||
        config->beaconInterval == 0 || config->dtimPeriod == 0 ||
        IEEE80211_IS_MULTICAST(config->bssid) ||
        IEEE80211_ADDR_EQ(config->bssid, etheranyaddr))
        return EINVAL;

    itl_ap_firmware_runtime_reset(runtime);
    runtime->config = *config;
    runtime->config.maxStations = MIN(
        config->maxStations,
        static_cast<uint32_t>(kItlApFirmwareMaxClients));
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
