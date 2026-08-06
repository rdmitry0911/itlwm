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
#include <crypto/sha1.h>
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

/* AppleBCMWLAN's Tahoe APSTA station table has five 0x30-byte entries.
 * Keep the firmware-neutral runtime at that exact public limit; individual
 * backends still clamp a lower hardware limit when required. */
enum { kItlApFirmwareMaxClients = 5 };

enum ItlApFirmwareDeferredBaState : uint8_t {
    kItlApFirmwareDeferredBaIdle = 0,
    kItlApFirmwareDeferredBaReserved,
    kItlApFirmwareDeferredBaPublished,
    kItlApFirmwareDeferredBaClaimed,
};

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
    /*
     * MVM host commands may be doorbelled before a synchronous caller on
     * the RX completion path discovers that it cannot sleep.  Publishing BA
     * management here lets the existing serial AP client task own both the
     * firmware command and its dependent over-the-air response.
     */
    uint8_t clientDeferredBaState[kItlApRxBaTidCount];
    struct ItlApBlockAckAction clientDeferredBa[kItlApRxBaTidCount];
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
    /* A radio reset destroys firmware stations and transient SAE state, but
     * not the authenticator's bounded PMKSA cache.  Keep the cache beside the
     * per-peer runtime so a retained AP profile can accept Open-System auth
     * followed by an SAE PMKID association after wake. */
    uint8_t saePmksaPmk[IEEE80211_PMK_LEN];
    uint8_t saePmksaPmkid[IEEE80211_PMKID_LEN];
    uint8_t saePmksaSta[IEEE80211_ADDR_LEN];
    uint8_t saePmksaBssid[IEEE80211_ADDR_LEN];
    bool saePmksaValid;
    bool clientOpenAuthenticated;
    uint8_t localRsnState;
    /* RX validates M2/M4 without sleeping.  Firmware key commands and the
     * dependent M3/authorization edge are consumed by the backend's serial
     * AP client task, never by the notification path itself. */
    uint8_t localRsnPendingAction;
    /* Match the reference authenticator's bounded EAPOL-Key retry machine.
     * The timer callback only publishes localRsnPendingTimeoutState; its
     * backend owner performs every retransmit and disconnect in the existing
     * serialized AP client task. */
    CTimeout *localRsnTimeout;
    void *localRsnOwner;
    uint8_t localRsnTimeoutState;
    uint8_t localRsnPendingTimeoutState;
    uint8_t localRsnAttempts;
    uint8_t localRsnExpectedReplayCount;
    uint64_t localRsnExpectedReplay[4];
    uint64_t localRsnM2ReplayCounter;
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

static inline int
itl_ap_firmware_defer_ba(
    struct ItlApFirmwareClientRuntime *client,
    const struct ItlApBlockAckAction *action)
{
    if (client == NULL || action == NULL ||
        action->tid >= kItlApRxBaTidCount ||
        action->kind < kItlApBlockAckAddRequest ||
        action->kind > kItlApBlockAckDelete)
        return EINVAL;
    uint8_t expected = kItlApFirmwareDeferredBaIdle;
    if (!__atomic_compare_exchange_n(
            &client->clientDeferredBaState[action->tid], &expected,
            static_cast<uint8_t>(kItlApFirmwareDeferredBaReserved), false,
            __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        return EBUSY;
    client->clientDeferredBa[action->tid] = *action;
    __atomic_store_n(&client->clientDeferredBaState[action->tid],
        static_cast<uint8_t>(kItlApFirmwareDeferredBaPublished),
        __ATOMIC_RELEASE);
    return 0;
}

static inline bool
itl_ap_firmware_take_deferred_ba(
    struct ItlApFirmwareClientRuntime *client, uint8_t tid,
    struct ItlApBlockAckAction *action)
{
    if (client == NULL || action == NULL || tid >= kItlApRxBaTidCount)
        return false;
    uint8_t expected = kItlApFirmwareDeferredBaPublished;
    if (!__atomic_compare_exchange_n(
            &client->clientDeferredBaState[tid], &expected,
            static_cast<uint8_t>(kItlApFirmwareDeferredBaClaimed), false,
            __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        return false;
    *action = client->clientDeferredBa[tid];
    return true;
}

static inline void
itl_ap_firmware_complete_deferred_ba(
    struct ItlApFirmwareClientRuntime *client, uint8_t tid)
{
    if (client == NULL || tid >= kItlApRxBaTidCount)
        return;
    bzero(&client->clientDeferredBa[tid],
          sizeof(client->clientDeferredBa[tid]));
    __atomic_store_n(&client->clientDeferredBaState[tid],
        static_cast<uint8_t>(kItlApFirmwareDeferredBaIdle),
        __ATOMIC_RELEASE);
}

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
    uint8_t profilePmk[IEEE80211_PMK_LEN];
    uint8_t gtk[16];
    uint8_t igtk[16];
    uint8_t gtkKeyId;
    uint8_t igtkKeyId;
    struct ItlApFirmwareClientRuntime clients[kItlApFirmwareMaxClients];
    bool hidden;
    bool samePhyAsPrimary;
    bool replayAfterWake;
    bool csaPending;
    uint16_t csaTargetChannel;
    uint8_t csaMode;
    uint8_t csaCount;
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
itl_ap_firmware_sae_pmksa_clear(
    struct ItlApFirmwareClientRuntime *client)
{
    if (client == NULL)
        return;
    explicit_bzero(client->saePmksaPmk,
                   sizeof(client->saePmksaPmk));
    explicit_bzero(client->saePmksaPmkid,
                   sizeof(client->saePmksaPmkid));
    bzero(client->saePmksaSta, sizeof(client->saePmksaSta));
    bzero(client->saePmksaBssid, sizeof(client->saePmksaBssid));
    client->saePmksaValid = false;
}

static inline bool
itl_ap_firmware_sae_pmksa_matches(
    const struct ItlApFirmwareClientRuntime *client,
    const uint8_t *bssid, const uint8_t *station, const uint8_t *pmkid)
{
    return client != NULL && client->saePmksaValid && bssid != NULL &&
        station != NULL && pmkid != NULL &&
        IEEE80211_ADDR_EQ(client->saePmksaSta, station) &&
        IEEE80211_ADDR_EQ(client->saePmksaBssid, bssid) &&
        timingsafe_bcmp(client->saePmksaPmkid, pmkid,
                        sizeof(client->saePmksaPmkid)) == 0;
}

static inline void
itl_ap_firmware_client_crypto_reset(
    struct ItlApFirmwareClientRuntime *client)
{
    if (client == NULL)
        return;
    timeout_del(&client->localRsnTimeout);
    __atomic_store_n(&client->localRsnTimeoutState, 0,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&client->localRsnPendingTimeoutState, 0,
                     __ATOMIC_RELEASE);
    client->clientAuthorized = false;
    client->clientPairwiseKeyInstalled = false;
    client->clientPairwiseTxPn = 0;
    explicit_bzero(client->clientPairwiseKey,
                   sizeof(client->clientPairwiseKey));
    explicit_bzero(client->clientRxPn, sizeof(client->clientRxPn));
    itl_ap_firmware_power_save_purge(client);
    client->localRsnState = 0;
    client->localRsnPendingAction = 0;
    client->localRsnAttempts = 0;
    client->localRsnExpectedReplayCount = 0;
    client->localRsnM2ReplayCounter = 0;
    client->replayCounter = 0;
    explicit_bzero(client->localRsnExpectedReplay,
                   sizeof(client->localRsnExpectedReplay));
    explicit_bzero(client->anonce, sizeof(client->anonce));
    explicit_bzero(&client->ptk, sizeof(client->ptk));
}

static inline void
itl_ap_firmware_client_reset(struct ItlApFirmwareClientRuntime *client,
                             bool preserveSaePmksa = false)
{
    if (client == NULL)
        return;
    uint8_t cachedPmk[IEEE80211_PMK_LEN];
    uint8_t cachedPmkid[IEEE80211_PMKID_LEN];
    uint8_t cachedSta[IEEE80211_ADDR_LEN];
    uint8_t cachedBssid[IEEE80211_ADDR_LEN];
    const bool cached = preserveSaePmksa && client->saePmksaValid;
    if (cached) {
        memcpy(cachedPmk, client->saePmksaPmk, sizeof(cachedPmk));
        memcpy(cachedPmkid, client->saePmksaPmkid,
               sizeof(cachedPmkid));
        IEEE80211_ADDR_COPY(cachedSta, client->saePmksaSta);
        IEEE80211_ADDR_COPY(cachedBssid, client->saePmksaBssid);
    }
    timeout_del(&client->localRsnTimeout);
    timeout_free(&client->localRsnTimeout);
    itl_ap_firmware_sae_reset(client);
    itl_ap_firmware_power_save_purge(client);
    for (size_t tid = 0; tid < kItlApRxBaTidCount; tid++) {
        itl_ap_rx_ba_stop(&client->clientRxBa[tid]);
        itl_ap_tx_ba_reset(&client->clientTxBa[tid]);
    }
    explicit_bzero(client, sizeof(*client));
    client->staId = UINT8_MAX;
    client->queueId = UINT16_MAX;
    if (cached) {
        memcpy(client->saePmksaPmk, cachedPmk,
               sizeof(client->saePmksaPmk));
        memcpy(client->saePmksaPmkid, cachedPmkid,
               sizeof(client->saePmksaPmkid));
        IEEE80211_ADDR_COPY(client->saePmksaSta, cachedSta);
        IEEE80211_ADDR_COPY(client->saePmksaBssid, cachedBssid);
        client->saePmksaValid = true;
        explicit_bzero(cachedPmk, sizeof(cachedPmk));
        explicit_bzero(cachedPmkid, sizeof(cachedPmkid));
    }
}

static inline void
itl_ap_firmware_runtime_reset(struct ItlApFirmwareRuntime *runtime,
                              bool preserveSaePmksa = false)
{
    if (runtime == NULL)
        return;
    struct ItlApFirmwareSaePmksaSnapshot {
        uint8_t pmk[IEEE80211_PMK_LEN];
        uint8_t pmkid[IEEE80211_PMKID_LEN];
        uint8_t station[IEEE80211_ADDR_LEN];
        uint8_t bssid[IEEE80211_ADDR_LEN];
        bool valid;
    } cached[kItlApFirmwareMaxClients];
    bzero(cached, sizeof(cached));
    if (runtime->localAuthMagic == kItlApLocalAuthMagic) {
        for (size_t index = 0; index < kItlApFirmwareMaxClients; index++) {
            const struct ItlApFirmwareClientRuntime *client =
                &runtime->clients[index];
            if (preserveSaePmksa && client->saePmksaValid) {
                memcpy(cached[index].pmk, client->saePmksaPmk,
                       sizeof(cached[index].pmk));
                memcpy(cached[index].pmkid, client->saePmksaPmkid,
                       sizeof(cached[index].pmkid));
                IEEE80211_ADDR_COPY(cached[index].station,
                                    client->saePmksaSta);
                IEEE80211_ADDR_COPY(cached[index].bssid,
                                    client->saePmksaBssid);
                cached[index].valid = true;
            }
            itl_ap_firmware_client_reset(&runtime->clients[index]);
        }
    }
    explicit_bzero(runtime, sizeof(*runtime));
    runtime->localAuthMagic = kItlApLocalAuthMagic;
    runtime->stage = kItlApFirmwareResourceIdle;
    runtime->broadcastQueueId = UINT16_MAX;
    runtime->multicastQueueId = UINT16_MAX;
    for (size_t index = 0; index < kItlApFirmwareMaxClients; index++) {
        runtime->clients[index].staId = UINT8_MAX;
        runtime->clients[index].queueId = UINT16_MAX;
        if (cached[index].valid) {
            memcpy(runtime->clients[index].saePmksaPmk,
                   cached[index].pmk,
                   sizeof(runtime->clients[index].saePmksaPmk));
            memcpy(runtime->clients[index].saePmksaPmkid,
                   cached[index].pmkid,
                   sizeof(runtime->clients[index].saePmksaPmkid));
            IEEE80211_ADDR_COPY(
                runtime->clients[index].saePmksaSta,
                cached[index].station);
            IEEE80211_ADDR_COPY(
                runtime->clients[index].saePmksaBssid,
                cached[index].bssid);
            runtime->clients[index].saePmksaValid = true;
        }
    }
    explicit_bzero(cached, sizeof(cached));
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

enum ItlApCsaBeaconContract : uint8_t {
    kItlApCsaElementId = 37,
    kItlApCsaElementLength = 3,
    kItlApCsaDefaultCount = 3,
};

/*
 * Keep the CSA announcement in the owned beacon snapshot.  The firmware
 * backends decide when the actual radio/context switch occurs, but every
 * peer must first receive a standards-shaped {mode, channel, count} IE.
 * Appending is intentional: it leaves the caller's Apple-built template and
 * every variable-length security IE byte-for-byte stable.
 */
static inline int
itl_ap_beacon_begin_csa(uint8_t *beacon, size_t *beaconLength,
                        size_t beaconCapacity, uint8_t mode,
                        uint8_t channel, uint8_t count)
{
    const size_t fixedLength = sizeof(struct ieee80211_frame) + 12;
    if (beacon == NULL || beaconLength == NULL ||
        *beaconLength < fixedLength || *beaconLength > beaconCapacity ||
        channel == 0 || count == 0 || mode > 1 ||
        *beaconLength + 2 + kItlApCsaElementLength > beaconCapacity)
        return EINVAL;

    size_t offset = fixedLength;
    while (offset + 2 <= *beaconLength) {
        const size_t totalLength = 2 + beacon[offset + 1];
        if (offset + totalLength > *beaconLength)
            return EINVAL;
        if (beacon[offset] == kItlApCsaElementId)
            return EBUSY;
        offset += totalLength;
    }
    if (offset != *beaconLength)
        return EINVAL;

    beacon[offset + 0] = kItlApCsaElementId;
    beacon[offset + 1] = kItlApCsaElementLength;
    beacon[offset + 2] = mode;
    beacon[offset + 3] = channel;
    beacon[offset + 4] = count;
    *beaconLength += 2 + kItlApCsaElementLength;
    return 0;
}

static inline int
itl_ap_beacon_set_csa_count(uint8_t *beacon, size_t beaconLength,
                            uint8_t count)
{
    const size_t fixedLength = sizeof(struct ieee80211_frame) + 12;
    if (beacon == NULL || beaconLength < fixedLength || count == 0)
        return EINVAL;
    size_t offset = fixedLength;
    while (offset + 2 <= beaconLength) {
        const size_t totalLength = 2 + beacon[offset + 1];
        if (offset + totalLength > beaconLength)
            return EINVAL;
        if (beacon[offset] == kItlApCsaElementId) {
            if (beacon[offset + 1] != kItlApCsaElementLength)
                return EINVAL;
            beacon[offset + 4] = count;
            return 0;
        }
        offset += totalLength;
    }
    return ENOENT;
}

static inline int
itl_ap_beacon_set_channel(uint8_t *beacon, size_t beaconLength,
                          uint8_t channel)
{
    const size_t fixedLength = sizeof(struct ieee80211_frame) + 12;
    if (beacon == NULL || beaconLength < fixedLength || channel == 0)
        return EINVAL;
    size_t offset = fixedLength;
    while (offset + 2 <= beaconLength) {
        const size_t totalLength = 2 + beacon[offset + 1];
        if (offset + totalLength > beaconLength)
            return EINVAL;
        if ((beacon[offset] == 3 ||
             beacon[offset] == IEEE80211_ELEMID_HTOP) &&
            beacon[offset + 1] >= 1)
            beacon[offset + 2] = channel;
        offset += totalLength;
    }
    return offset == beaconLength ? 0 : EINVAL;
}

static inline int
itl_ap_beacon_end_csa(uint8_t *beacon, size_t *beaconLength,
                      uint8_t channel, bool commitChannel)
{
    const size_t fixedLength = sizeof(struct ieee80211_frame) + 12;
    if (beacon == NULL || beaconLength == NULL ||
        *beaconLength < fixedLength || channel == 0)
        return EINVAL;

    size_t csaOffset = SIZE_MAX;
    size_t offset = fixedLength;
    while (offset + 2 <= *beaconLength) {
        const size_t totalLength = 2 + beacon[offset + 1];
        if (offset + totalLength > *beaconLength)
            return EINVAL;
        if (beacon[offset] == kItlApCsaElementId) {
            if (beacon[offset + 1] != kItlApCsaElementLength)
                return EINVAL;
            csaOffset = offset;
        }
        offset += totalLength;
    }
    if (offset != *beaconLength || csaOffset == SIZE_MAX)
        return ENOENT;

    const size_t csaLength = 2 + kItlApCsaElementLength;
    memmove(beacon + csaOffset, beacon + csaOffset + csaLength,
            *beaconLength - csaOffset - csaLength);
    *beaconLength -= csaLength;
    explicit_bzero(beacon + *beaconLength, csaLength);
    return commitChannel ?
        itl_ap_beacon_set_channel(beacon, *beaconLength, channel) : 0;
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
    struct ItlApFirmwareClientRuntime *cachedClient = NULL;
    for (size_t index = 0; index < kItlApFirmwareMaxClients; index++) {
        struct ItlApFirmwareClientRuntime *candidate =
            &runtime->clients[index];
        if (!candidate->inUse && candidate->saePmksaValid &&
            IEEE80211_ADDR_EQ(candidate->saePmksaSta, station) &&
            IEEE80211_ADDR_EQ(candidate->saePmksaBssid,
                              runtime->config.bssid)) {
            cachedClient = candidate;
            break;
        }
    }
    struct ItlApFirmwareClientRuntime *selected = NULL;
    if (cachedClient != NULL &&
        cachedClient < &runtime->clients[limit])
        selected = cachedClient;
    for (size_t index = 0; index < limit; index++) {
        struct ItlApFirmwareClientRuntime *client =
            &runtime->clients[index];
        if (selected != NULL || client->inUse || client->saePmksaValid)
            continue;
        selected = client;
    }
    if (selected == NULL) {
        for (size_t index = 0; index < limit; index++) {
            struct ItlApFirmwareClientRuntime *client =
                &runtime->clients[index];
            if (!client->inUse) {
                selected = client;
                break;
            }
        }
    }
    /* Authentication state has no firmware resource yet.  Reclaim one such
     * incomplete slot so a stream of abandoned Auth/SAE commits cannot lock
     * every association slot indefinitely. */
    if (selected == NULL) {
        for (size_t index = 0; index < limit; index++) {
            struct ItlApFirmwareClientRuntime *client =
                &runtime->clients[index];
            if (client->clientAssociated || client->clientStationInstalled)
                continue;
            selected = client;
            break;
        }
    }
    if (selected == NULL)
        return NULL;

    const bool importCache = cachedClient != NULL;
    const size_t selectedIndex = static_cast<size_t>(
        selected - &runtime->clients[0]);
    uint8_t cachedPmk[IEEE80211_PMK_LEN];
    uint8_t cachedPmkid[IEEE80211_PMKID_LEN];
    if (importCache && cachedClient != selected) {
        memcpy(cachedPmk, cachedClient->saePmksaPmk,
               sizeof(cachedPmk));
        memcpy(cachedPmkid, cachedClient->saePmksaPmkid,
               sizeof(cachedPmkid));
    }
    itl_ap_firmware_client_reset(selected,
                                 importCache && cachedClient == selected);
    selected->inUse = true;
    selected->staId = static_cast<uint8_t>(
        runtime->firstClientStaId + selectedIndex);
    selected->clientAid = static_cast<uint16_t>(selectedIndex + 1);
    IEEE80211_ADDR_COPY(selected->clientMac, station);
    if (importCache && cachedClient != selected) {
        memcpy(selected->saePmksaPmk, cachedPmk,
               sizeof(selected->saePmksaPmk));
        memcpy(selected->saePmksaPmkid, cachedPmkid,
               sizeof(selected->saePmksaPmkid));
        IEEE80211_ADDR_COPY(selected->saePmksaSta, station);
        IEEE80211_ADDR_COPY(selected->saePmksaBssid,
                            runtime->config.bssid);
        selected->saePmksaValid = true;
        itl_ap_firmware_sae_pmksa_clear(cachedClient);
        explicit_bzero(cachedPmk, sizeof(cachedPmk));
        explicit_bzero(cachedPmkid, sizeof(cachedPmkid));
    }
    return selected;
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

    /* A destructive lower-radio epoch retains a bounded PMKSA census in
     * otherwise idle client slots.  Carry it into the replayed profile, then
     * prune entries unless this is the same pure-SAE BSSID. */
    itl_ap_firmware_runtime_reset(runtime, true);
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
    for (size_t index = 0; index < kItlApFirmwareMaxClients; index++) {
        struct ItlApFirmwareClientRuntime *client =
            &runtime->clients[index];
        if (client->saePmksaValid &&
            (config->authUpper != 0x1000 ||
             !IEEE80211_ADDR_EQ(client->saePmksaBssid,
                                config->bssid)))
            itl_ap_firmware_sae_pmksa_clear(client);
    }
    if (config->authUpper == 0x8) {
        char passphrase[65];
        bzero(passphrase, sizeof(passphrase));
        memcpy(passphrase, runtime->credential,
               config->credentialLength);
        const int deriveError = pbkdf2_sha1(
            passphrase, runtime->ssid, config->ssidLength, 4096,
            runtime->profilePmk, sizeof(runtime->profilePmk));
        explicit_bzero(passphrase, sizeof(passphrase));
        if (deriveError != 0) {
            itl_ap_firmware_runtime_reset(runtime);
            return deriveError;
        }
    }
    if (config->authUpper == 0x8 || config->authUpper == 0x1000) {
        arc4random_buf(runtime->gtk, sizeof(runtime->gtk));
        runtime->gtkKeyId = 1;
        if (config->authUpper == 0x1000) {
            arc4random_buf(runtime->igtk, sizeof(runtime->igtk));
            runtime->igtkKeyId = 4;
        }
    }
    return 0;
}

#endif /* ItlApFirmwareRuntime_hpp */
