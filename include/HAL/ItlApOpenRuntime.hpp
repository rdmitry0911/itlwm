/*
 * Copyright (C) 2026 itlwm contributors
 *
 * Host-side, firmware-family-neutral framing for the first usable SoftAP
 * profile: a bounded set of clients.  IWM and IWX deliberately share this
 * parser/builder so their externally visible authentication rules cannot
 * drift, while each backend retains ownership of its firmware station and
 * TX-queue commands.
 */

#ifndef ItlApOpenRuntime_hpp
#define ItlApOpenRuntime_hpp

#include <HAL/ItlApFirmwareRuntime.hpp>
#include <sys/malloc.h>
#include <net/ethernet.h>
#include <net/if_llc.h>
#include <net80211/ieee80211_priv.h>
#include <net80211/ieee80211_crypto.h>

enum ItlApOpenRxDisposition : uint8_t {
    kItlApOpenRxNotOurs = 0,
    kItlApOpenRxConsumed,
    kItlApOpenRxReply,
    kItlApOpenRxAssociate,
    kItlApOpenRxData,
    kItlApOpenRxPowerState,
    kItlApOpenRxPsPoll,
    kItlApOpenRxDisconnect,
};

struct ItlApOpenRxResult {
    uint8_t disposition;
    uint8_t station[IEEE80211_ADDR_LEN];
    uint8_t *reply;
    size_t replyLength;
    mbuf_t ethernetPacket;
    bool authenticationComplete;
    bool authenticationReplacesAssociation;
    uint8_t departingStation[IEEE80211_ADDR_LEN];
    bool powerSaveObserved;
    bool powerSave;
    bool timChanged;
    bool reassociation;
    size_t associationIEOffset;
    size_t clientIndex;
};

enum ItlApLocalRsnState : uint8_t {
    kItlApLocalRsnDisabled = 0,
    kItlApLocalRsnWaitM2,
    kItlApLocalRsnWaitM4,
    kItlApLocalRsnAuthorized,
};

enum ItlApLocalEapolAction : uint8_t {
    kItlApLocalEapolConsumed = 0,
    kItlApLocalEapolSendM3,
    kItlApLocalEapolResendM3,
    kItlApLocalEapolInstallPairwise,
};

/* OpenBSD's M_DEVBUF compatibility tag is private to the IWM headers. */
static constexpr int kItlApOpenMallocType = 2;

static inline void
itl_ap_open_rx_result_reset(struct ItlApOpenRxResult *result)
{
    if (result != NULL) {
        bzero(result, sizeof(*result));
        result->clientIndex = SIZE_MAX;
    }
}

static inline bool
itl_ap_open_config_supported(const struct ItlHalApConfig *config)
{
    return config != NULL && config->authUpper == 0 &&
        config->credentialLength == 0 && config->rsnIELength == 0;
}

static inline bool
itl_ap_wpa2_rsn_ie_supported(const uint8_t *rsnIE, size_t rsnIELength)
{
    if (rsnIE == NULL || rsnIELength < 20 || rsnIELength > 64 ||
        rsnIE[0] != IEEE80211_ELEMID_RSN ||
        static_cast<size_t>(rsnIE[1]) + 2 != rsnIELength)
        return false;

    static const uint8_t ccmpSuite[] = { 0x00, 0x0f, 0xac, 0x04 };
    static const uint8_t pskSuite[] = { 0x00, 0x0f, 0xac, 0x02 };
    const uint8_t *cursor = rsnIE + 2;
    const uint8_t *end = rsnIE + rsnIELength;
    if (cursor + 2 + sizeof(ccmpSuite) + 2 > end ||
        LE_READ_2(cursor) != 1 ||
        memcmp(cursor + 2, ccmpSuite, sizeof(ccmpSuite)) != 0)
        return false;
    cursor += 2 + sizeof(ccmpSuite);
    const uint16_t pairwiseCount = LE_READ_2(cursor);
    cursor += 2;
    if (pairwiseCount == 0 ||
        pairwiseCount > static_cast<uint16_t>((end - cursor) / 4))
        return false;
    bool hasCCMP = false;
    for (uint16_t i = 0; i < pairwiseCount; i++) {
        if (memcmp(cursor + i * 4, ccmpSuite, sizeof(ccmpSuite)) == 0)
            hasCCMP = true;
    }
    cursor += pairwiseCount * 4;
    if (!hasCCMP || cursor + 2 > end)
        return false;
    const uint16_t akmCount = LE_READ_2(cursor);
    cursor += 2;
    if (akmCount == 0 ||
        akmCount > static_cast<uint16_t>((end - cursor) / 4))
        return false;
    for (uint16_t i = 0; i < akmCount; i++) {
        if (memcmp(cursor + i * 4, pskSuite, sizeof(pskSuite)) == 0)
            return true;
    }
    return false;
}

static inline bool
itl_ap_wpa2_config_supported(const struct ItlHalApConfig *config)
{
    return config != NULL && config->authUpper == 0x8 &&
        config->credential != NULL && config->credentialLength >= 8 &&
        config->credentialLength <= 63 &&
        itl_ap_wpa2_rsn_ie_supported(config->rsnIE,
                                      config->rsnIELength);
}

static inline bool
itl_ap_wpa3_rsn_ie_supported(const uint8_t *rsnIE, size_t rsnIELength)
{
    if (rsnIE == NULL || rsnIELength < 26 || rsnIELength > 64 ||
        rsnIE[0] != IEEE80211_ELEMID_RSN ||
        static_cast<size_t>(rsnIE[1]) + 2 != rsnIELength)
        return false;

    static const uint8_t ccmpSuite[] = { 0x00, 0x0f, 0xac, 0x04 };
    static const uint8_t saeSuite[] = { 0x00, 0x0f, 0xac, 0x08 };
    static const uint8_t bipCmac128Suite[] = { 0x00, 0x0f, 0xac, 0x06 };
    const uint8_t *cursor = rsnIE + 2;
    const uint8_t *end = rsnIE + rsnIELength;
    if (cursor + 2 + sizeof(ccmpSuite) + 2 > end ||
        LE_READ_2(cursor) != 1 ||
        memcmp(cursor + 2, ccmpSuite, sizeof(ccmpSuite)) != 0)
        return false;
    cursor += 2 + sizeof(ccmpSuite);
    const uint16_t pairwiseCount = LE_READ_2(cursor);
    cursor += 2;
    if (pairwiseCount == 0 ||
        pairwiseCount > static_cast<uint16_t>((end - cursor) / 4))
        return false;
    bool hasCCMP = false;
    for (uint16_t i = 0; i < pairwiseCount; i++) {
        if (memcmp(cursor + i * 4, ccmpSuite, sizeof(ccmpSuite)) == 0)
            hasCCMP = true;
    }
    cursor += pairwiseCount * 4;
    if (!hasCCMP || cursor + 2 > end)
        return false;
    const uint16_t akmCount = LE_READ_2(cursor);
    cursor += 2;
    if (akmCount == 0 ||
        akmCount > static_cast<uint16_t>((end - cursor) / 4))
        return false;
    bool hasSAE = false;
    for (uint16_t i = 0; i < akmCount; i++) {
        if (memcmp(cursor + i * 4, saeSuite, sizeof(saeSuite)) == 0)
            hasSAE = true;
    }
    cursor += akmCount * 4;
    if (!hasSAE || cursor + 2 > end ||
        (LE_READ_2(cursor) & 0x00c0) != 0x00c0)
        return false;
    cursor += 2;
    if (cursor + 2 > end)
        return false;
    const uint16_t pmkidCount = LE_READ_2(cursor);
    cursor += 2;
    const size_t pmkidBytes = static_cast<size_t>(pmkidCount) *
        IEEE80211_PMKID_LEN;
    if (pmkidBytes > static_cast<size_t>(end - cursor))
        return false;
    cursor += pmkidBytes;
    return cursor + sizeof(bipCmac128Suite) <= end &&
        memcmp(cursor, bipCmac128Suite, sizeof(bipCmac128Suite)) == 0;
}

static inline bool
itl_ap_wpa3_config_supported(const struct ItlHalApConfig *config)
{
#if ITL_SAE_DRIVER_CRYPTO_AVAILABLE && !defined(IEEE80211_STA_ONLY)
    return config != NULL && config->authUpper == 0x1000 &&
        config->credential != NULL && config->credentialLength >= 8 &&
        config->credentialLength <= 63 &&
        itl_ap_wpa3_rsn_ie_supported(config->rsnIE,
                                      config->rsnIELength);
#else
    (void)config;
    return false;
#endif
}

static inline bool
itl_ap_client_config_supported(const struct ItlHalApConfig *config)
{
    return itl_ap_open_config_supported(config) ||
        itl_ap_wpa2_config_supported(config) ||
        itl_ap_wpa3_config_supported(config);
}

static inline bool
itl_ap_client_is_secure(const struct ItlApFirmwareRuntime *runtime)
{
    return runtime != NULL &&
        (itl_ap_wpa2_config_supported(&runtime->config) ||
         itl_ap_wpa3_config_supported(&runtime->config));
}

static inline bool
itl_ap_client_uses_local_sae(const struct ItlApFirmwareRuntime *runtime)
{
    return runtime != NULL &&
        itl_ap_wpa3_config_supported(&runtime->config);
}

static inline uint64_t
itl_ap_key_rsc(const struct ItlHalApKey *key)
{
    if (key == NULL || key->rsc == NULL || key->rscLength < 6)
        return 0;
    const uint8_t *rsc = static_cast<const uint8_t *>(key->rsc);
    return static_cast<uint64_t>(rsc[0]) |
        static_cast<uint64_t>(rsc[1]) << 8 |
        static_cast<uint64_t>(rsc[2]) << 16 |
        static_cast<uint64_t>(rsc[3]) << 24 |
        static_cast<uint64_t>(rsc[4]) << 32 |
        static_cast<uint64_t>(rsc[5]) << 40;
}

static inline bool
itl_ap_power_save_should_buffer(const struct ItlApFirmwareRuntime *runtime,
                                const struct ItlApFirmwareClientRuntime *client,
                                mbuf_t ethernetPacket)
{
    if (runtime == NULL || client == NULL || ethernetPacket == NULL ||
        !client->clientPowerSave || !client->clientAssociated ||
        mbuf_pkthdr_len(ethernetPacket) < ETHER_HDR_LEN)
        return false;
    struct ether_header ethernet;
    if (mbuf_copydata(ethernetPacket, 0, sizeof(ethernet), &ethernet) != 0)
        return false;
    return !IEEE80211_IS_MULTICAST(ethernet.ether_dhost) &&
        IEEE80211_ADDR_EQ(ethernet.ether_dhost, client->clientMac) &&
        ethernet.ether_type != htons(ETHERTYPE_PAE);
}

static inline int
itl_ap_power_save_enqueue(struct ItlApFirmwareClientRuntime *client,
                          mbuf_t packet)
{
    if (client == NULL || packet == NULL)
        return EINVAL;
    if (client->powerSaveQueueCount >=
        ItlApFirmwareClientRuntime::kPowerSaveQueueLength)
        return ENOBUFS;
    client->powerSaveQueue[client->powerSaveQueueTail] = packet;
    client->powerSaveQueueTail = static_cast<uint8_t>(
        (client->powerSaveQueueTail + 1) %
        ItlApFirmwareClientRuntime::kPowerSaveQueueLength);
    client->powerSaveQueueCount++;
    return 0;
}

static inline mbuf_t
itl_ap_power_save_dequeue(struct ItlApFirmwareClientRuntime *client)
{
    if (client == NULL || client->powerSaveQueueCount == 0)
        return NULL;
    mbuf_t packet = client->powerSaveQueue[client->powerSaveQueueHead];
    client->powerSaveQueue[client->powerSaveQueueHead] = NULL;
    client->powerSaveQueueHead = static_cast<uint8_t>(
        (client->powerSaveQueueHead + 1) %
        ItlApFirmwareClientRuntime::kPowerSaveQueueLength);
    client->powerSaveQueueCount--;
    return packet;
}

static inline void
itl_ap_power_save_requeue_front(struct ItlApFirmwareClientRuntime *client,
                                mbuf_t packet)
{
    if (client == NULL || packet == NULL ||
        client->powerSaveQueueCount >=
            ItlApFirmwareClientRuntime::kPowerSaveQueueLength)
        return;
    client->powerSaveQueueHead = static_cast<uint8_t>(
        (client->powerSaveQueueHead +
         ItlApFirmwareClientRuntime::kPowerSaveQueueLength - 1) %
        ItlApFirmwareClientRuntime::kPowerSaveQueueLength);
    client->powerSaveQueue[client->powerSaveQueueHead] = packet;
    client->powerSaveQueueCount++;
}

static inline int
itl_ap_power_save_set_tim(struct ItlApFirmwareRuntime *runtime,
                          struct ItlApFirmwareClientRuntime *client, bool set,
                          bool *changed)
{
    if (changed != NULL)
        *changed = false;
    if (runtime == NULL || client == NULL || changed == NULL ||
        client->clientAid == 0 ||
        runtime->config.beaconTemplateLength <
            sizeof(struct ieee80211_frame) + 12)
        return EINVAL;
    size_t offset = sizeof(struct ieee80211_frame) + 12;
    while (offset + 2 <= runtime->config.beaconTemplateLength) {
        const size_t elementLength = runtime->beacon[offset + 1];
        if (offset + 2 + elementLength >
            runtime->config.beaconTemplateLength)
            return EINVAL;
        if (runtime->beacon[offset] == IEEE80211_ELEMID_TIM) {
            if (elementLength < 4)
                return EINVAL;
            const size_t bitmapOffset = runtime->beacon[offset + 4] & 0xfe;
            const size_t aidByte = client->clientAid >> 3;
            const size_t bitmapLength = elementLength - 3;
            if (aidByte < bitmapOffset ||
                aidByte >= bitmapOffset + bitmapLength)
                return ENOTSUP;
            uint8_t *bitmap = runtime->beacon + offset + 5 +
                aidByte - bitmapOffset;
            const uint8_t previous = *bitmap;
            if (set)
                *bitmap |= 1U << (client->clientAid & 7);
            else
                *bitmap &= static_cast<uint8_t>(
                    ~(1U << (client->clientAid & 7)));
            client->timSet = (*bitmap &
                (1U << (client->clientAid & 7))) != 0;
            *changed = previous != *bitmap;
            return 0;
        }
        offset += 2 + elementLength;
    }
    return ENOENT;
}

static inline bool
itl_ap_open_is_running(const struct ItlApFirmwareRuntime *runtime)
{
    return runtime != NULL &&
        runtime->stage == kItlApFirmwareResourceRunning &&
        itl_ap_client_config_supported(&runtime->config);
}

static inline bool
itl_ap_open_addressed_to_bss(const struct ItlApFirmwareRuntime *runtime,
                             const struct ieee80211_frame *wh)
{
    return IEEE80211_ADDR_EQ(wh->i_addr1, runtime->config.bssid) &&
        IEEE80211_ADDR_EQ(wh->i_addr3, runtime->config.bssid);
}

static inline int
itl_ap_open_alloc_reply(size_t length, uint8_t **reply)
{
    if (reply == NULL || length == 0 || length > MCLBYTES)
        return EINVAL;
    *reply = static_cast<uint8_t *>(
        malloc(length, kItlApOpenMallocType, M_NOWAIT | M_ZERO));
    return *reply != NULL ? 0 : ENOMEM;
}

static inline uint16_t
itl_ap_open_duration(const struct ItlApFirmwareRuntime *runtime)
{
    return runtime->config.channel <= 14 ? 0x013a : 0x003c;
}

static inline int
itl_ap_open_build_probe_response(const struct ItlApFirmwareRuntime *runtime,
                                 const struct ieee80211_frame *request,
                                 size_t frameLength,
                                 struct ItlApOpenRxResult *result)
{
    const size_t headerLength = sizeof(*request);
    const size_t fixedLength = headerLength + 12;
    if (!itl_ap_open_is_running(runtime) || request == NULL || result == NULL ||
        frameLength < headerLength + 2 ||
        runtime->config.beaconTemplateLength < fixedLength ||
        (request->i_fc[0] & IEEE80211_FC0_TYPE_MASK) !=
            IEEE80211_FC0_TYPE_MGT ||
        (request->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK) !=
            IEEE80211_FC0_SUBTYPE_PROBE_REQ)
        return 0;

    if (!IEEE80211_ADDR_EQ(request->i_addr1, runtime->config.bssid) &&
        !IEEE80211_IS_MULTICAST(request->i_addr1))
        return 0;

    result->disposition = kItlApOpenRxConsumed;
    const uint8_t *cursor = reinterpret_cast<const uint8_t *>(request) +
        headerLength;
    const uint8_t *end = reinterpret_cast<const uint8_t *>(request) +
        frameLength;
    const uint8_t *ssid = NULL;
    while (cursor + 2 <= end) {
        const size_t elementLength = cursor[1];
        if (cursor + 2 + elementLength > end)
            break;
        if (cursor[0] == IEEE80211_ELEMID_SSID) {
            ssid = cursor;
            break;
        }
        cursor += 2 + elementLength;
    }
    const bool wildcard = ssid != NULL && ssid[1] == 0;
    const bool exact = ssid != NULL &&
        ssid[1] == runtime->config.ssidLength &&
        memcmp(ssid + 2, runtime->ssid, ssid[1]) == 0;
    if (!wildcard && !exact)
        return 0;

    const size_t templateLength = runtime->config.beaconTemplateLength;
    int error = itl_ap_open_alloc_reply(templateLength, &result->reply);
    if (error != 0)
        return error;

    const uint8_t *source = runtime->beacon;
    memcpy(result->reply, source, fixedLength);
    size_t inputOffset = fixedLength;
    size_t outputOffset = fixedLength;
    while (inputOffset + 2 <= templateLength) {
        const size_t totalLength = 2 + source[inputOffset + 1];
        if (inputOffset + totalLength > templateLength)
            break;
        if (source[inputOffset] != IEEE80211_ELEMID_TIM) {
            memcpy(result->reply + outputOffset,
                   source + inputOffset, totalLength);
            outputOffset += totalLength;
        }
        inputOffset += totalLength;
    }

    struct ieee80211_frame *response =
        reinterpret_cast<struct ieee80211_frame *>(result->reply);
    response->i_fc[0] = IEEE80211_FC0_VERSION_0 |
        IEEE80211_FC0_TYPE_MGT | IEEE80211_FC0_SUBTYPE_PROBE_RESP;
    response->i_fc[1] = IEEE80211_FC1_DIR_NODS;
    LE_WRITE_2(response->i_dur, itl_ap_open_duration(runtime));
    IEEE80211_ADDR_COPY(response->i_addr1, request->i_addr2);
    IEEE80211_ADDR_COPY(response->i_addr2, runtime->config.bssid);
    IEEE80211_ADDR_COPY(response->i_addr3, runtime->config.bssid);
    result->replyLength = outputOffset;
    result->disposition = kItlApOpenRxReply;
    IEEE80211_ADDR_COPY(result->station, request->i_addr2);
    return 0;
}

static inline void
itl_ap_open_begin_client_auth(struct ItlApFirmwareRuntime *runtime,
                              struct ItlApFirmwareClientRuntime *client,
                              struct ItlApOpenRxResult *result)
{
    if (client->timSet && result != NULL)
        (void)itl_ap_power_save_set_tim(
            runtime, client, false, &result->timChanged);
    client->clientAssociationPending = false;
    client->clientReassociationPending = false;
    client->clientAuthenticated = false;
    client->clientAssociated = false;
    client->clientAuthorized = false;
    client->clientAssocIEsLength = 0;
    itl_ap_firmware_client_crypto_reset(client);
    client->clientRsnIELength = 0;
    explicit_bzero(client->clientRsnIE, sizeof(client->clientRsnIE));
}

static inline int
itl_ap_open_build_auth_response(struct ItlApFirmwareRuntime *runtime,
                                const struct ieee80211_frame *request,
                                size_t frameLength,
                                struct ItlApOpenRxResult *result)
{
    const size_t headerLength = sizeof(*request);
    if (!itl_ap_open_is_running(runtime) || request == NULL || result == NULL ||
        frameLength < headerLength + 6 ||
        (request->i_fc[0] & IEEE80211_FC0_TYPE_MASK) !=
            IEEE80211_FC0_TYPE_MGT ||
        (request->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK) !=
            IEEE80211_FC0_SUBTYPE_AUTH ||
        !itl_ap_open_addressed_to_bss(runtime, request))
        return 0;

    result->disposition = kItlApOpenRxConsumed;
    const uint8_t *body = reinterpret_cast<const uint8_t *>(request) +
        headerLength;
    const uint16_t algorithm = LE_READ_2(body);
    const uint16_t transaction = LE_READ_2(body + 2);
    const uint16_t peerStatus = LE_READ_2(body + 4);
    uint16_t responseStatus = IEEE80211_STATUS_SUCCESS;
    uint16_t responseTransaction = IEEE80211_AUTH_OPEN_RESPONSE;
    uint8_t saeBody[IEEE80211_SAE_ENGINE_HNP_COMMIT_BODY_LEN];
    bzero(saeBody, sizeof(saeBody));
    size_t saeBodyLength = 0;
    bool authenticationComplete = false;
    bool clientAllocated = false;
    struct ItlApFirmwareClientRuntime *client =
        itl_ap_firmware_find_client(runtime, request->i_addr2);

    if (algorithm == IEEE80211_AUTH_ALG_SAE &&
        itl_ap_client_uses_local_sae(runtime)) {
        if (peerStatus != IEEE80211_SAE_AP_STATUS_SUCCESS ||
            (transaction != 1 && transaction != 2)) {
            responseStatus = IEEE80211_SAE_AP_STATUS_UNSPECIFIED;
            responseTransaction = transaction;
        } else if (transaction == 1) {
            if (client == NULL) {
                client = itl_ap_firmware_allocate_client(
                    runtime, request->i_addr2);
                clientAllocated = client != NULL;
            }
            if (client == NULL) {
                responseStatus = IEEE80211_STATUS_TOOMANY;
                responseTransaction = 1;
                goto build_response;
            }
            result->authenticationReplacesAssociation =
                client->clientAssociated;
            IEEE80211_ADDR_COPY(result->departingStation,
                                client->clientMac);
            itl_ap_firmware_sae_reset(client);
            itl_ap_open_begin_client_auth(runtime, client, result);
            responseStatus = ieee80211_sae_ap_begin_hnp(
                runtime->config.bssid, request->i_addr2,
                runtime->credential, runtime->config.credentialLength,
                body + 6, frameLength - headerLength - 6,
                &client->sae, saeBody, sizeof(saeBody), &saeBodyLength);
            responseTransaction = 1;
            if (responseStatus != IEEE80211_SAE_AP_STATUS_SUCCESS) {
                itl_ap_firmware_sae_reset(client);
                if (clientAllocated) {
                    itl_ap_firmware_client_reset(client);
                    client = NULL;
                }
            }
        } else {
            responseTransaction = 2;
            if (client == NULL || client->sae == NULL) {
                responseStatus = IEEE80211_SAE_AP_STATUS_UNSPECIFIED;
            } else {
                uint8_t pmkid[IEEE80211_PMKID_LEN];
                bzero(pmkid, sizeof(pmkid));
                responseStatus = ieee80211_sae_ap_confirm(
                    client->sae, request->i_addr2,
                    body + 6, frameLength - headerLength - 6,
                    saeBody, sizeof(saeBody), &saeBodyLength,
                    client->pmk, sizeof(client->pmk),
                    pmkid, sizeof(pmkid));
                explicit_bzero(pmkid, sizeof(pmkid));
                authenticationComplete =
                    responseStatus == IEEE80211_SAE_AP_STATUS_SUCCESS;
                if (!authenticationComplete)
                    explicit_bzero(client->pmk, sizeof(client->pmk));
            }
        }
    } else if (algorithm == IEEE80211_AUTH_ALG_OPEN &&
               !itl_ap_client_uses_local_sae(runtime) &&
               transaction == IEEE80211_AUTH_OPEN_REQUEST &&
               peerStatus == IEEE80211_STATUS_SUCCESS) {
        if (client == NULL) {
            client = itl_ap_firmware_allocate_client(
                runtime, request->i_addr2);
            clientAllocated = client != NULL;
        }
        if (client == NULL) {
            responseStatus = IEEE80211_STATUS_TOOMANY;
            responseTransaction = IEEE80211_AUTH_OPEN_RESPONSE;
            goto build_response;
        }
        result->authenticationReplacesAssociation =
            client->clientAssociated;
        IEEE80211_ADDR_COPY(result->departingStation,
                            client->clientMac);
        itl_ap_open_begin_client_auth(runtime, client, result);
        responseTransaction = IEEE80211_AUTH_OPEN_RESPONSE;
        authenticationComplete = true;
    } else {
        return 0;
    }

build_response:
    const size_t responseLength = headerLength + 6 + saeBodyLength;
    int error = itl_ap_open_alloc_reply(responseLength, &result->reply);
    if (error != 0) {
        if (clientAllocated)
            itl_ap_firmware_client_reset(client);
        explicit_bzero(saeBody, sizeof(saeBody));
        return error;
    }
    struct ieee80211_frame *response =
        reinterpret_cast<struct ieee80211_frame *>(result->reply);
    response->i_fc[0] = IEEE80211_FC0_VERSION_0 |
        IEEE80211_FC0_TYPE_MGT | IEEE80211_FC0_SUBTYPE_AUTH;
    response->i_fc[1] = IEEE80211_FC1_DIR_NODS;
    LE_WRITE_2(response->i_dur, itl_ap_open_duration(runtime));
    IEEE80211_ADDR_COPY(response->i_addr1, request->i_addr2);
    IEEE80211_ADDR_COPY(response->i_addr2, runtime->config.bssid);
    IEEE80211_ADDR_COPY(response->i_addr3, runtime->config.bssid);
    uint8_t *out = result->reply + headerLength;
    LE_WRITE_2(out, algorithm);
    LE_WRITE_2(out + 2, responseTransaction);
    LE_WRITE_2(out + 4, responseStatus);
    if (saeBodyLength != 0)
        memcpy(out + 6, saeBody, saeBodyLength);
    explicit_bzero(saeBody, sizeof(saeBody));
    result->replyLength = responseLength;
    result->disposition = kItlApOpenRxReply;
    result->authenticationComplete = authenticationComplete;
    result->clientIndex = itl_ap_firmware_client_index(runtime, client);
    IEEE80211_ADDR_COPY(result->station, request->i_addr2);
    return 0;
}

static inline int
itl_ap_open_parse_assoc(struct ItlApFirmwareRuntime *runtime,
                        const struct ieee80211_frame *request,
                        size_t frameLength,
                        struct ItlApOpenRxResult *result)
{
    const size_t headerLength = sizeof(*request);
    if (!itl_ap_open_is_running(runtime) || request == NULL || result == NULL ||
        frameLength < headerLength + 4)
        return 0;
    const uint8_t subtype = request->i_fc[0] &
        IEEE80211_FC0_SUBTYPE_MASK;
    const bool reassociation =
        subtype == IEEE80211_FC0_SUBTYPE_REASSOC_REQ;
    const size_t fixedLength = reassociation ? 10 : 4;
    if (frameLength < headerLength + fixedLength ||
        (request->i_fc[0] & IEEE80211_FC0_TYPE_MASK) !=
            IEEE80211_FC0_TYPE_MGT ||
        (subtype != IEEE80211_FC0_SUBTYPE_ASSOC_REQ &&
         subtype != IEEE80211_FC0_SUBTYPE_REASSOC_REQ) ||
        !itl_ap_open_addressed_to_bss(runtime, request))
        return 0;

    result->disposition = kItlApOpenRxConsumed;
    struct ItlApFirmwareClientRuntime *client =
        itl_ap_firmware_find_client(runtime, request->i_addr2);
    if (client == NULL)
        return 0;
    const uint8_t *body = reinterpret_cast<const uint8_t *>(request) +
        headerLength;
    const uint16_t capability = LE_READ_2(body);
    const uint8_t *cursor = body + fixedLength;
    const uint8_t *end = reinterpret_cast<const uint8_t *>(request) +
        frameLength;
    const uint8_t *ssid = NULL;
    const uint8_t *rates = NULL;
    const uint8_t *extendedRates = NULL;
    const uint8_t *rsn = NULL;
    const uint8_t *htCapabilities = NULL;
    bool qos = false;
    while (cursor + 2 <= end) {
        const size_t elementLength = cursor[1];
        if (cursor + 2 + elementLength > end)
            break;
        if (cursor[0] == IEEE80211_ELEMID_SSID)
            ssid = cursor;
        else if (cursor[0] == IEEE80211_ELEMID_RATES)
            rates = cursor;
        else if (cursor[0] == IEEE80211_ELEMID_XRATES)
            extendedRates = cursor;
        else if (cursor[0] == IEEE80211_ELEMID_RSN)
            rsn = cursor;
        else if (cursor[0] == IEEE80211_ELEMID_HTCAPS &&
                 elementLength == 26)
            htCapabilities = cursor;
        else if (cursor[0] == IEEE80211_ELEMID_QOS_CAP &&
                 elementLength >= 1)
            qos = true;
        else if (cursor[0] == IEEE80211_ELEMID_VENDOR &&
                 elementLength == 7 &&
                 memcmp(cursor + 2, MICROSOFT_OUI, 3) == 0 &&
                 cursor[5] == WME_OUI_TYPE &&
                 cursor[6] == WME_INFO_OUI_SUBTYPE &&
                 cursor[7] == WME_VERSION)
            qos = true;
        cursor += 2 + elementLength;
    }

    const bool secure = itl_ap_client_is_secure(runtime);
    const bool localSae = itl_ap_client_uses_local_sae(runtime);
    const size_t rsnLength = rsn != NULL ?
        static_cast<size_t>(rsn[1]) + 2 : 0;
    const bool rsnValid = !secure || (rsn != NULL &&
        (localSae ? itl_ap_wpa3_rsn_ie_supported(rsn, rsnLength) :
                    itl_ap_wpa2_rsn_ie_supported(rsn, rsnLength)));
    uint16_t legacyRateMask = rates != NULL ?
        itl_hal_ap_legacy_rate_mask(rates + 2, rates[1]) : 0;
    if (extendedRates != NULL)
        legacyRateMask |= itl_hal_ap_legacy_rate_mask(
            extendedRates + 2, extendedRates[1]);
    if (runtime->config.channel > 14)
        legacyRateMask &= 0x0ff0;
    uint8_t htMcs[2] = { 0, 0 };
    bool ht = qos && itl_hal_ap_ht_enabled(&runtime->config) &&
        htCapabilities != NULL;
    if (ht) {
        htMcs[0] = htCapabilities[5] & runtime->config.htMcsSet[0];
        htMcs[1] = htCapabilities[6] & runtime->config.htMcsSet[1];
        ht = htMcs[0] != 0;
    }
    const bool valid = client->clientAuthenticated &&
        (!localSae || ieee80211_sae_ap_is_accepted(client->sae) != 0) &&
        (capability & IEEE80211_CAPINFO_ESS) != 0 &&
        (secure ? (capability & IEEE80211_CAPINFO_PRIVACY) != 0 &&
                  rsnValid
                : (capability & IEEE80211_CAPINFO_PRIVACY) == 0 &&
                  rsn == NULL) &&
        ssid != NULL && ssid[1] == runtime->config.ssidLength &&
        memcmp(ssid + 2, runtime->ssid, ssid[1]) == 0 &&
        rates != NULL && rates[1] != 0 &&
        rates[1] <= IEEE80211_RATE_MAXSIZE && legacyRateMask != 0;
    if (!valid)
        return 0;

    client->clientRsnIELength = 0;
    explicit_bzero(client->clientRsnIE, sizeof(client->clientRsnIE));
    if (secure && rsnLength <= sizeof(client->clientRsnIE)) {
        client->clientRsnIELength = rsnLength;
        memcpy(client->clientRsnIE, rsn, rsnLength);
    }
    client->clientLegacyRateMask = legacyRateMask;
    client->clientQos = qos;
    client->clientHt = ht;
    client->clientHtNss = ht ? (htMcs[1] != 0 ? 2 : 1) : 0;
    client->clientHtCapabilities = ht ?
        LE_READ_2(htCapabilities + 2) & runtime->config.htCapabilities : 0;
    client->clientHtAmpduParams = ht ? htCapabilities[4] : 0;
    memcpy(client->clientHtMcs, htMcs, sizeof(client->clientHtMcs));

    result->disposition = kItlApOpenRxAssociate;
    result->reassociation = reassociation;
    result->associationIEOffset = headerLength + fixedLength;
    result->clientIndex = itl_ap_firmware_client_index(runtime, client);
    IEEE80211_ADDR_COPY(result->station, request->i_addr2);
    return 0;
}

static inline int
itl_ap_open_build_assoc_success(const struct ItlApFirmwareRuntime *runtime,
                                const struct ItlApFirmwareClientRuntime *client,
                                bool reassociation,
                                struct ItlApOpenRxResult *result)
{
    static const uint8_t rates2g[] = {
        0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24
    };
    static const uint8_t rates5g[] = {
        0x8c, 0x12, 0x98, 0x24, 0xb0, 0x48, 0x60, 0x6c
    };
    static const uint8_t extendedRates[] = { 0x30, 0x48, 0x60, 0x6c };
    if (!itl_ap_open_is_running(runtime) || client == NULL || result == NULL ||
        !client->clientStationInstalled || client->clientAid == 0)
        return EINVAL;

    const bool is2g = runtime->config.channel <= 14;
    const bool secure = itl_ap_client_is_secure(runtime);
    const size_t htLength = client->clientHt ?
        kItlHalApHtCapabilityIELength + kItlHalApHtOperationIELength : 0;
    const size_t responseLength = sizeof(struct ieee80211_frame) + 6 +
        2 + sizeof(rates2g) + (is2g ? 2 + sizeof(extendedRates) : 0) +
        (secure ? runtime->config.rsnIELength : 0) +
        htLength +
        (client->clientQos ? sizeof(kItlHalApWmmParameterIE) : 0);
    int error = itl_ap_open_alloc_reply(responseLength, &result->reply);
    if (error != 0)
        return error;
    struct ieee80211_frame *response =
        reinterpret_cast<struct ieee80211_frame *>(result->reply);
    response->i_fc[0] = IEEE80211_FC0_VERSION_0 |
        IEEE80211_FC0_TYPE_MGT |
        (reassociation ? IEEE80211_FC0_SUBTYPE_REASSOC_RESP :
                         IEEE80211_FC0_SUBTYPE_ASSOC_RESP);
    response->i_fc[1] = IEEE80211_FC1_DIR_NODS;
    IEEE80211_ADDR_COPY(response->i_addr1, client->clientMac);
    IEEE80211_ADDR_COPY(response->i_addr2, runtime->config.bssid);
    IEEE80211_ADDR_COPY(response->i_addr3, runtime->config.bssid);
    uint8_t *out = result->reply + sizeof(*response);
    LE_WRITE_2(out, IEEE80211_CAPINFO_ESS |
        (secure ? IEEE80211_CAPINFO_PRIVACY : 0) |
        (is2g ? IEEE80211_CAPINFO_SHORT_SLOTTIME : 0));
    LE_WRITE_2(out + 2, IEEE80211_STATUS_SUCCESS);
    LE_WRITE_2(out + 4, client->clientAid | 0xc000);
    out += 6;
    *out++ = IEEE80211_ELEMID_RATES;
    *out++ = sizeof(rates2g);
    memcpy(out, is2g ? rates2g : rates5g, sizeof(rates2g));
    out += sizeof(rates2g);
    if (is2g) {
        *out++ = IEEE80211_ELEMID_XRATES;
        *out++ = sizeof(extendedRates);
        memcpy(out, extendedRates, sizeof(extendedRates));
        out += sizeof(extendedRates);
    }
    if (secure) {
        memcpy(out, runtime->rsnIE, runtime->config.rsnIELength);
        out += runtime->config.rsnIELength;
    }
    if (client->clientHt) {
        out += itl_hal_ap_build_ht_capability_ie(
            out, static_cast<size_t>(result->reply + responseLength - out),
            &runtime->config);
        out += itl_hal_ap_build_ht_operation_ie(
            out, static_cast<size_t>(result->reply + responseLength - out),
            &runtime->config);
    }
    if (client->clientQos) {
        memcpy(out, kItlHalApWmmParameterIE,
               sizeof(kItlHalApWmmParameterIE));
        out += sizeof(kItlHalApWmmParameterIE);
    }
    result->replyLength = static_cast<size_t>(out - result->reply);
    result->disposition = kItlApOpenRxReply;
    result->reassociation = reassociation;
    result->clientIndex = itl_ap_firmware_client_index(runtime, client);
    IEEE80211_ADDR_COPY(result->station, client->clientMac);
    return 0;
}

static inline int
itl_ap_open_decap_data(struct ItlApFirmwareRuntime *runtime,
                       mbuf_t packet, size_t frameLength,
                       bool hardwareDecrypted,
                       struct ItlApOpenRxResult *result)
{
    if (!itl_ap_open_is_running(runtime) || packet == NULL || result == NULL ||
        frameLength < sizeof(struct ieee80211_frame))
        return 0;
    const struct ieee80211_frame *wh =
        mtod(packet, const struct ieee80211_frame *);
    if ((wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) !=
            IEEE80211_FC0_TYPE_DATA ||
        (wh->i_fc[1] & IEEE80211_FC1_DIR_MASK) != IEEE80211_FC1_DIR_TODS ||
        !IEEE80211_ADDR_EQ(wh->i_addr1, runtime->config.bssid))
        return 0;
    /* The secondary GO MAC owns every ToDS data frame addressed to it,
     * including malformed, pre-association, or closed-port traffic.  Mark
     * ownership before validation so a rejected AP frame can never fall
     * through into the primary STA node tree. */
    result->disposition = kItlApOpenRxConsumed;
    struct ItlApFirmwareClientRuntime *client =
        itl_ap_firmware_find_client(runtime, wh->i_addr2);
    if (client == NULL || !client->clientAssociated)
        return 0;
    result->clientIndex = itl_ap_firmware_client_index(runtime, client);

    result->powerSaveObserved = true;
    result->powerSave =
        (wh->i_fc[1] & IEEE80211_FC1_PWR_MGT) != 0;
    if ((wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_NODATA) != 0) {
        result->disposition = kItlApOpenRxPowerState;
        return 0;
    }
    const size_t headerLength = ieee80211_get_hdrlen(wh);
    if (headerLength < sizeof(*wh) ||
        frameLength < headerLength + LLC_SNAPFRAMELEN)
        return 0;
    const bool secure = itl_ap_client_is_secure(runtime);
    const bool protectedFrame =
        (wh->i_fc[1] & IEEE80211_FC1_PROTECTED) != 0;
    size_t payloadOffset = headerLength;
    size_t payloadEnd = frameLength;
    if (protectedFrame) {
        if (!secure || !client->clientPairwiseKeyInstalled ||
            !hardwareDecrypted ||
            frameLength < headerLength + IEEE80211_CCMP_HDRLEN +
                LLC_SNAPFRAMELEN + IEEE80211_CCMP_MICLEN)
            return 0;
        uint8_t ccmp[IEEE80211_CCMP_HDRLEN];
        if (mbuf_copydata(packet, headerLength, sizeof(ccmp), ccmp) != 0 ||
            (ccmp[3] & IEEE80211_WEP_EXTIV) == 0 ||
            ((ccmp[3] >> 6) & 3) != 0)
            return 0;
        const uint64_t packetNumber =
            static_cast<uint64_t>(ccmp[0]) |
            static_cast<uint64_t>(ccmp[1]) << 8 |
            static_cast<uint64_t>(ccmp[4]) << 16 |
            static_cast<uint64_t>(ccmp[5]) << 24 |
            static_cast<uint64_t>(ccmp[6]) << 32 |
            static_cast<uint64_t>(ccmp[7]) << 40;
        const uint8_t tid = ieee80211_has_qos(wh) ?
            ieee80211_get_qos(wh) & IEEE80211_QOS_TID : 0;
        if (packetNumber == 0 || packetNumber <= client->clientRxPn[tid])
            return 0;
        client->clientRxPn[tid] = packetNumber;
        payloadOffset += IEEE80211_CCMP_HDRLEN;
        payloadEnd -= IEEE80211_CCMP_MICLEN;
    }
    if (payloadEnd < payloadOffset + LLC_SNAPFRAMELEN)
        return 0;
    struct llc llc;
    if (mbuf_copydata(packet, payloadOffset, sizeof(llc), &llc) != 0 ||
        llc.llc_dsap != LLC_SNAP_LSAP || llc.llc_ssap != LLC_SNAP_LSAP ||
        llc.llc_control != LLC_UI || llc.llc_snap.org_code[0] != 0 ||
        llc.llc_snap.org_code[1] != 0 || llc.llc_snap.org_code[2] != 0)
        return 0;
    const bool eapol = llc.llc_snap.ether_type == htons(ETHERTYPE_PAE);
    if (secure && !eapol && (!protectedFrame || !client->clientAuthorized))
        return 0;
    if (!secure && protectedFrame)
        return 0;
    const size_t payloadLength = payloadEnd - payloadOffset -
        LLC_SNAPFRAMELEN;
    const size_t ethernetLength = ETHER_HDR_LEN + payloadLength;
    if (ethernetLength > MCLBYTES)
        return EMSGSIZE;
    unsigned int maxChunks = 1;
    if (mbuf_allocpacket(MBUF_DONTWAIT, ethernetLength, &maxChunks,
            &result->ethernetPacket) != 0 || result->ethernetPacket == NULL)
        return ENOMEM;
    mbuf_setlen(result->ethernetPacket, ethernetLength);
    mbuf_pkthdr_setlen(result->ethernetPacket, ethernetLength);
    struct ether_header *ethernet =
        mtod(result->ethernetPacket, struct ether_header *);
    IEEE80211_ADDR_COPY(ethernet->ether_dhost, wh->i_addr3);
    IEEE80211_ADDR_COPY(ethernet->ether_shost, wh->i_addr2);
    ethernet->ether_type = llc.llc_snap.ether_type;
    if (payloadLength != 0 && mbuf_copydata(packet,
            payloadOffset + LLC_SNAPFRAMELEN, payloadLength,
            reinterpret_cast<uint8_t *>(ethernet) + ETHER_HDR_LEN) != 0) {
        mbuf_freem(result->ethernetPacket);
        result->ethernetPacket = NULL;
        return EINVAL;
    }
    result->disposition = kItlApOpenRxData;
    return 0;
}

static inline int
itl_ap_open_encap_data(const struct ItlApFirmwareRuntime *runtime,
                       const struct ItlApFirmwareClientRuntime *client,
                       mbuf_t ethernetPacket, bool moreData,
                       mbuf_t *wirePacket)
{
    if (!itl_ap_open_is_running(runtime) || client == NULL ||
        ethernetPacket == NULL || wirePacket == NULL ||
        !client->clientAssociated)
        return EINVAL;
    const size_t ethernetLength = mbuf_pkthdr_len(ethernetPacket);
    const size_t headerLength = client->clientQos ?
        sizeof(struct ieee80211_qosframe) :
        sizeof(struct ieee80211_frame);
    if (ethernetLength < ETHER_HDR_LEN ||
        ethernetLength - ETHER_HDR_LEN >
            MCLBYTES - headerLength - LLC_SNAPFRAMELEN)
        return EMSGSIZE;
    struct ether_header ethernet;
    if (mbuf_copydata(ethernetPacket, 0, sizeof(ethernet), &ethernet) != 0)
        return EINVAL;
    const bool multicast = IEEE80211_IS_MULTICAST(ethernet.ether_dhost);
    const bool secure = itl_ap_client_is_secure(runtime);
    const bool eapol = ethernet.ether_type == htons(ETHERTYPE_PAE);
    if (secure && !eapol && (!client->clientAuthorized ||
        (multicast ? !runtime->groupKeyInstalled :
                     !client->clientPairwiseKeyInstalled)))
        return EACCES;
    if (!multicast &&
        !IEEE80211_ADDR_EQ(ethernet.ether_dhost, client->clientMac))
        return EHOSTUNREACH;

    const size_t wireLength = headerLength +
        LLC_SNAPFRAMELEN + ethernetLength - ETHER_HDR_LEN;
    unsigned int maxChunks = 1;
    *wirePacket = NULL;
    if (mbuf_allocpacket(MBUF_DONTWAIT, wireLength, &maxChunks,
            wirePacket) != 0 || *wirePacket == NULL)
        return ENOMEM;
    mbuf_setlen(*wirePacket, wireLength);
    mbuf_pkthdr_setlen(*wirePacket, wireLength);
    uint8_t *bytes = mtod(*wirePacket, uint8_t *);
    bzero(bytes, wireLength);
    struct ieee80211_frame *wh =
        reinterpret_cast<struct ieee80211_frame *>(bytes);
    wh->i_fc[0] = IEEE80211_FC0_VERSION_0 | IEEE80211_FC0_TYPE_DATA |
        (client->clientQos ? IEEE80211_FC0_SUBTYPE_QOS : 0);
    wh->i_fc[1] = IEEE80211_FC1_DIR_FROMDS |
        (secure && !eapol ? IEEE80211_FC1_PROTECTED : 0) |
        (moreData ? IEEE80211_FC1_MORE_DATA : 0);
    IEEE80211_ADDR_COPY(wh->i_addr1, ethernet.ether_dhost);
    IEEE80211_ADDR_COPY(wh->i_addr2, runtime->config.bssid);
    IEEE80211_ADDR_COPY(wh->i_addr3, ethernet.ether_shost);
    if (client->clientQos) {
        struct ieee80211_qosframe *qos =
            reinterpret_cast<struct ieee80211_qosframe *>(bytes);
        LE_WRITE_2(qos->i_qos, 0); /* Best Effort, TID 0, normal ACK. */
    }
    struct llc *llc = reinterpret_cast<struct llc *>(bytes + headerLength);
    llc->llc_dsap = LLC_SNAP_LSAP;
    llc->llc_ssap = LLC_SNAP_LSAP;
    llc->llc_control = LLC_UI;
    llc->llc_snap.ether_type = ethernet.ether_type;
    if (ethernetLength > ETHER_HDR_LEN && mbuf_copydata(ethernetPacket,
            ETHER_HDR_LEN, ethernetLength - ETHER_HDR_LEN,
            bytes + headerLength + LLC_SNAPFRAMELEN) != 0) {
        mbuf_freem(*wirePacket);
        *wirePacket = NULL;
        return EINVAL;
    }
    return 0;
}

static inline int
itl_ap_local_eapol_packet(const struct ItlApFirmwareRuntime *runtime,
                          const struct ItlApFirmwareClientRuntime *client,
                          const void *eapol, size_t eapolLength,
                          mbuf_t *packet)
{
    if (runtime == NULL || client == NULL || eapol == NULL || packet == NULL ||
        eapolLength < sizeof(struct ieee80211_eapol_key) ||
        eapolLength > MCLBYTES - ETHER_HDR_LEN)
        return EINVAL;
    const size_t packetLength = ETHER_HDR_LEN + eapolLength;
    unsigned int maxChunks = 1;
    *packet = NULL;
    if (mbuf_allocpacket(MBUF_DONTWAIT, packetLength, &maxChunks, packet) != 0 ||
        *packet == NULL)
        return ENOMEM;
    mbuf_setlen(*packet, packetLength);
    mbuf_pkthdr_setlen(*packet, packetLength);
    struct ether_header *ethernet = mtod(*packet, struct ether_header *);
    IEEE80211_ADDR_COPY(ethernet->ether_dhost, client->clientMac);
    IEEE80211_ADDR_COPY(ethernet->ether_shost, runtime->config.bssid);
    ethernet->ether_type = htons(ETHERTYPE_PAE);
    memcpy(reinterpret_cast<uint8_t *>(ethernet) + ETHER_HDR_LEN,
           eapol, eapolLength);
    return 0;
}

static inline int
itl_ap_local_sae_build_m1(struct ItlApFirmwareRuntime *runtime,
                          struct ItlApFirmwareClientRuntime *client,
                          uint8_t *frame, size_t frameCapacity,
                          size_t *frameLength)
{
    if (!itl_ap_client_uses_local_sae(runtime) || client == NULL || frame == NULL ||
        frameLength == NULL || frameCapacity < sizeof(struct ieee80211_eapol_key) ||
        !client->clientAssociated ||
        ieee80211_sae_ap_is_accepted(client->sae) == 0)
        return EINVAL;
    bzero(frame, frameCapacity);
    struct ieee80211_eapol_key *key =
        reinterpret_cast<struct ieee80211_eapol_key *>(frame);
    key->version = EAPOL_VERSION;
    key->type = EAPOL_KEY;
    key->desc = EAPOL_KEY_DESC_IEEE80211;
    BE_WRITE_2(key->info, EAPOL_KEY_PAIRWISE | EAPOL_KEY_KEYACK |
        EAPOL_KEY_DESC_AKM_DEFINED);
    BE_WRITE_2(key->keylen, 16);
    client->replayCounter = 1;
    BE_WRITE_8(key->replaycnt, client->replayCounter);
    arc4random_buf(client->anonce, sizeof(client->anonce));
    memcpy(key->nonce, client->anonce, sizeof(key->nonce));
    BE_WRITE_2(key->paylen, 0);
    BE_WRITE_2(key->len, sizeof(*key) - 4);
    client->localRsnState = kItlApLocalRsnWaitM2;
    *frameLength = sizeof(*key);
    return 0;
}

static inline void
itl_ap_local_sae_note_m1_result(struct ItlApFirmwareClientRuntime *client,
                                bool sent)
{
    if (client != NULL && !sent) {
        client->localRsnState = kItlApLocalRsnDisabled;
        client->replayCounter = 0;
        explicit_bzero(client->anonce, sizeof(client->anonce));
    }
}

static inline int
itl_ap_local_sae_handle_eapol(struct ItlApFirmwareRuntime *runtime,
                              struct ItlApFirmwareClientRuntime *client,
                              const uint8_t *eapol, size_t eapolLength,
                              enum ItlApLocalEapolAction *action)
{
    if (action != NULL)
        *action = kItlApLocalEapolConsumed;
    if (!itl_ap_client_uses_local_sae(runtime) || client == NULL || action == NULL ||
        eapol == NULL || eapolLength < sizeof(struct ieee80211_eapol_key) ||
        eapolLength > 512 || !client->clientAssociated)
        return EINVAL;

    uint8_t frame[512];
    memcpy(frame, eapol, eapolLength);
    struct ieee80211_eapol_key *key =
        reinterpret_cast<struct ieee80211_eapol_key *>(frame);
    const size_t declaredLength = 4 + BE_READ_2(key->len);
    const uint16_t keyInfo = BE_READ_2(key->info);
    const size_t keyDataLength = BE_READ_2(key->paylen);
    if (key->type != EAPOL_KEY || key->desc != EAPOL_KEY_DESC_IEEE80211 ||
        declaredLength != eapolLength ||
        keyDataLength > eapolLength - sizeof(*key) ||
        (keyInfo & EAPOL_KEY_VERSION_MASK) != EAPOL_KEY_DESC_AKM_DEFINED ||
        (keyInfo & EAPOL_KEY_PAIRWISE) == 0 ||
        (keyInfo & EAPOL_KEY_KEYMIC) == 0 ||
        (keyInfo & (EAPOL_KEY_KEYACK | EAPOL_KEY_REQUEST |
                    EAPOL_KEY_ERROR)) != 0) {
        explicit_bzero(frame, sizeof(frame));
        return EACCES;
    }

    if (client->localRsnState == kItlApLocalRsnWaitM2) {
        if (BE_READ_8(key->replaycnt) != client->replayCounter) {
            explicit_bzero(frame, sizeof(frame));
            return EACCES;
        }
        const uint8_t *rsn = NULL;
        const uint8_t *cursor = reinterpret_cast<const uint8_t *>(key + 1);
        const uint8_t *end = cursor + keyDataLength;
        while (cursor + 2 <= end) {
            const size_t elementLength = cursor[1];
            if (cursor + 2 + elementLength > end)
                break;
            if (cursor[0] == IEEE80211_ELEMID_RSN) {
                rsn = cursor;
                break;
            }
            cursor += 2 + elementLength;
        }
        if (rsn == NULL || client->clientRsnIELength !=
                static_cast<size_t>(rsn[1]) + 2 ||
            memcmp(rsn, client->clientRsnIE,
                   client->clientRsnIELength) != 0) {
            explicit_bzero(frame, sizeof(frame));
            return EACCES;
        }
        struct ieee80211_ptk transientPtk;
        explicit_bzero(&transientPtk, sizeof(transientPtk));
        ieee80211_derive_ptk(IEEE80211_AKM_SAE, client->pmk,
            runtime->config.bssid, client->clientMac,
            client->anonce, key->nonce, &transientPtk);
        const int micError =
            ieee80211_eapol_key_check_mic(key, transientPtk.kck);
        if (micError != 0) {
            explicit_bzero(&transientPtk, sizeof(transientPtk));
            explicit_bzero(frame, sizeof(frame));
            return EACCES;
        }
        memcpy(&client->ptk, &transientPtk, sizeof(client->ptk));
        explicit_bzero(&transientPtk, sizeof(transientPtk));
        explicit_bzero(frame, sizeof(frame));
        *action = kItlApLocalEapolSendM3;
        return 0;
    }

    if (client->localRsnState == kItlApLocalRsnWaitM4) {
        const uint64_t replay = BE_READ_8(key->replaycnt);
        if (replay == client->replayCounter && keyDataLength == 0) {
            const int micError =
                ieee80211_eapol_key_check_mic(key, client->ptk.kck);
            explicit_bzero(frame, sizeof(frame));
            if (micError != 0)
                return EACCES;
            *action = kItlApLocalEapolInstallPairwise;
            return 0;
        }
        if (client->replayCounter == 0 ||
            replay != client->replayCounter - 1 || keyDataLength == 0) {
            explicit_bzero(frame, sizeof(frame));
            return EACCES;
        }
        const uint8_t *rsn = NULL;
        const uint8_t *cursor = reinterpret_cast<const uint8_t *>(key + 1);
        const uint8_t *end = cursor + keyDataLength;
        while (cursor + 2 <= end) {
            const size_t elementLength = cursor[1];
            if (cursor + 2 + elementLength > end)
                break;
            if (cursor[0] == IEEE80211_ELEMID_RSN) {
                rsn = cursor;
                break;
            }
            cursor += 2 + elementLength;
        }
        const int micError = rsn != NULL &&
            client->clientRsnIELength ==
                static_cast<size_t>(rsn[1]) + 2 &&
            memcmp(rsn, client->clientRsnIE,
                   client->clientRsnIELength) == 0 ?
            ieee80211_eapol_key_check_mic(key, client->ptk.kck) : EACCES;
        explicit_bzero(frame, sizeof(frame));
        if (micError != 0)
            return EACCES;
        *action = kItlApLocalEapolResendM3;
        return 0;
    }
    explicit_bzero(frame, sizeof(frame));
    return EALREADY;
}

static inline int
itl_ap_local_sae_build_m3(struct ieee80211com *ic,
                          struct ItlApFirmwareRuntime *runtime,
                          struct ItlApFirmwareClientRuntime *client,
                          uint8_t *frame, size_t frameCapacity,
                          size_t *frameLength)
{
#ifdef IEEE80211_STA_ONLY
    (void)ic;
    (void)runtime;
    (void)client;
    (void)frame;
    (void)frameCapacity;
    (void)frameLength;
    return ENOTSUP;
#else
    if (ic == NULL || !itl_ap_client_uses_local_sae(runtime) || client == NULL ||
        frame == NULL || frameLength == NULL || frameCapacity < 256 ||
        (client->localRsnState != kItlApLocalRsnWaitM2 &&
         client->localRsnState != kItlApLocalRsnWaitM4) ||
        client->clientRsnIELength == 0)
        return EINVAL;
    const bool retry = client->localRsnState == kItlApLocalRsnWaitM4;
    bzero(frame, frameCapacity);
    struct ieee80211_eapol_key *key =
        reinterpret_cast<struct ieee80211_eapol_key *>(frame);
    key->version = EAPOL_VERSION;
    key->type = EAPOL_KEY;
    key->desc = EAPOL_KEY_DESC_IEEE80211;
    BE_WRITE_2(key->info, EAPOL_KEY_PAIRWISE | EAPOL_KEY_KEYACK |
        EAPOL_KEY_KEYMIC | EAPOL_KEY_INSTALL | EAPOL_KEY_SECURE |
        EAPOL_KEY_ENCRYPTED | EAPOL_KEY_DESC_AKM_DEFINED);
    BE_WRITE_2(key->keylen, 16);
    if (!retry)
        client->replayCounter++;
    BE_WRITE_8(key->replaycnt, client->replayCounter);
    memcpy(key->nonce, client->anonce, sizeof(key->nonce));
    uint8_t *cursor = reinterpret_cast<uint8_t *>(key + 1);
    memcpy(cursor, runtime->rsnIE, runtime->config.rsnIELength);
    cursor += runtime->config.rsnIELength;
    *cursor++ = IEEE80211_ELEMID_VENDOR;
    *cursor++ = 6 + sizeof(runtime->gtk);
    memcpy(cursor, IEEE80211_OUI, 3);
    cursor += 3;
    *cursor++ = IEEE80211_KDE_GTK;
    *cursor++ = runtime->gtkKeyId & 3;
    *cursor++ = 0;
    memcpy(cursor, runtime->gtk, sizeof(runtime->gtk));
    cursor += sizeof(runtime->gtk);
    *cursor++ = IEEE80211_ELEMID_VENDOR;
    *cursor++ = 4 + 2 + 6 + sizeof(runtime->igtk);
    memcpy(cursor, IEEE80211_OUI, 3);
    cursor += 3;
    *cursor++ = 9;
    *cursor++ = runtime->igtkKeyId;
    *cursor++ = 0;
    bzero(cursor, 6);
    cursor += 6;
    memcpy(cursor, runtime->igtk, sizeof(runtime->igtk));
    cursor += sizeof(runtime->igtk);
    const size_t plainLength = static_cast<size_t>(
        cursor - reinterpret_cast<uint8_t *>(key + 1));
    if (sizeof(*key) + plainLength + 16 > frameCapacity) {
        if (!retry)
            client->replayCounter--;
        explicit_bzero(frame, frameCapacity);
        return EMSGSIZE;
    }
    BE_WRITE_2(key->paylen, plainLength);
    BE_WRITE_2(key->len, sizeof(*key) + plainLength - 4);
    ieee80211_eapol_key_encrypt(ic, key, client->ptk.kek);
    ieee80211_eapol_key_mic(key, client->ptk.kck);
    *frameLength = sizeof(*key) + BE_READ_2(key->paylen);
    return 0;
#endif
}

static inline void
itl_ap_local_sae_note_m3_result(struct ItlApFirmwareClientRuntime *client,
                                bool sent, bool retry)
{
    if (client == NULL)
        return;
    if (sent) {
        client->localRsnState = kItlApLocalRsnWaitM4;
    } else if (!retry && client->replayCounter != 0) {
        client->replayCounter--;
    }
}

static inline void
itl_ap_local_sae_complete_4way(struct ItlApFirmwareClientRuntime *client,
                               bool installed)
{
    if (client == NULL)
        return;
    client->localRsnState = installed ? kItlApLocalRsnAuthorized :
                                         kItlApLocalRsnDisabled;
    if (!installed)
        explicit_bzero(&client->ptk, sizeof(client->ptk));
}

static inline int
itl_ap_open_classify_rx(struct ItlApFirmwareRuntime *runtime,
                        mbuf_t packet, size_t frameLength,
                        bool hardwareDecrypted,
                        struct ItlApOpenRxResult *result)
{
    itl_ap_open_rx_result_reset(result);
    if (!itl_ap_open_is_running(runtime) || packet == NULL ||
        frameLength < 2)
        return 0;
    const uint8_t *frameControl = mtod(packet, const uint8_t *);
    if ((frameControl[0] & IEEE80211_FC0_TYPE_MASK) ==
            IEEE80211_FC0_TYPE_CTL &&
        (frameControl[0] & IEEE80211_FC0_SUBTYPE_MASK) ==
            IEEE80211_FC0_SUBTYPE_PS_POLL &&
        frameLength >= sizeof(struct ieee80211_frame_pspoll)) {
        const struct ieee80211_frame_pspoll *poll =
            mtod(packet, const struct ieee80211_frame_pspoll *);
        if (IEEE80211_ADDR_EQ(poll->i_bssid, runtime->config.bssid)) {
            result->disposition = kItlApOpenRxConsumed;
            struct ItlApFirmwareClientRuntime *client =
                itl_ap_firmware_find_client(runtime, poll->i_ta);
            if (client != NULL && client->clientAssociated &&
                (LE_READ_2(poll->i_aid) & 0x3fff) == client->clientAid) {
                result->disposition = kItlApOpenRxPsPoll;
                result->powerSaveObserved = true;
                result->powerSave = true;
                result->clientIndex =
                    itl_ap_firmware_client_index(runtime, client);
                IEEE80211_ADDR_COPY(result->station, poll->i_ta);
            }
            return 0;
        }
    }
    if (frameLength < sizeof(struct ieee80211_frame))
        return 0;
    const struct ieee80211_frame *wh =
        mtod(packet, const struct ieee80211_frame *);
    int error = itl_ap_open_build_probe_response(
        runtime, wh, frameLength, result);
    if (error != 0 || result->disposition != kItlApOpenRxNotOurs)
        return error;
    error = itl_ap_open_build_auth_response(runtime, wh, frameLength, result);
    if (error != 0 || result->disposition != kItlApOpenRxNotOurs)
        return error;
    error = itl_ap_open_parse_assoc(runtime, wh, frameLength, result);
    if (error != 0 || result->disposition != kItlApOpenRxNotOurs)
        return error;
    error = itl_ap_open_decap_data(runtime, packet, frameLength,
                                   hardwareDecrypted, result);
    if (error != 0 || result->disposition != kItlApOpenRxNotOurs)
        return error;

    if ((wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) ==
            IEEE80211_FC0_TYPE_MGT &&
        ((wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK) ==
             IEEE80211_FC0_SUBTYPE_DEAUTH ||
         (wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK) ==
             IEEE80211_FC0_SUBTYPE_DISASSOC) &&
        IEEE80211_ADDR_EQ(wh->i_addr1, runtime->config.bssid)) {
        struct ItlApFirmwareClientRuntime *client =
            itl_ap_firmware_find_client(runtime, wh->i_addr2);
        if (client == NULL)
            return 0;
        if (itl_ap_client_uses_local_sae(runtime) &&
            client->clientAuthorized &&
            (((wh->i_fc[1] & IEEE80211_FC1_PROTECTED) == 0) ||
             !hardwareDecrypted)) {
            result->disposition = kItlApOpenRxConsumed;
            return 0;
        }
        result->disposition = kItlApOpenRxDisconnect;
        result->clientIndex = itl_ap_firmware_client_index(runtime, client);
        IEEE80211_ADDR_COPY(result->station, wh->i_addr2);
    }
    return 0;
}

static inline void
itl_ap_open_release_result(struct ItlApOpenRxResult *result)
{
    if (result == NULL)
        return;
    if (result->reply != NULL) {
        explicit_bzero(result->reply, result->replyLength);
        ::free(result->reply);
    }
    if (result->ethernetPacket != NULL)
        mbuf_freem(result->ethernetPacket);
    itl_ap_open_rx_result_reset(result);
}

#endif /* ItlApOpenRuntime_hpp */
