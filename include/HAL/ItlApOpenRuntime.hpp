/*
 * Copyright (C) 2026 itlwm contributors
 *
 * Host-side, firmware-family-neutral framing for the first usable SoftAP
 * profile: one open-system client.  IWM and IWX deliberately share this
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

enum ItlApOpenRxDisposition : uint8_t {
    kItlApOpenRxNotOurs = 0,
    kItlApOpenRxConsumed,
    kItlApOpenRxReply,
    kItlApOpenRxAssociate,
    kItlApOpenRxData,
    kItlApOpenRxDisconnect,
};

struct ItlApOpenRxResult {
    uint8_t disposition;
    uint8_t station[IEEE80211_ADDR_LEN];
    uint8_t *reply;
    size_t replyLength;
    mbuf_t ethernetPacket;
};

/* OpenBSD's M_DEVBUF compatibility tag is private to the IWM headers. */
static constexpr int kItlApOpenMallocType = 2;

static inline void
itl_ap_open_rx_result_reset(struct ItlApOpenRxResult *result)
{
    if (result != NULL)
        bzero(result, sizeof(*result));
}

static inline bool
itl_ap_open_config_supported(const struct ItlHalApConfig *config)
{
    return config != NULL && config->authUpper == 0 &&
        config->credentialLength == 0 && config->rsnIELength == 0;
}

static inline bool
itl_ap_open_is_running(const struct ItlApFirmwareRuntime *runtime)
{
    return runtime != NULL &&
        runtime->stage == kItlApFirmwareResourceRunning &&
        itl_ap_open_config_supported(&runtime->config);
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

static inline int
itl_ap_open_build_auth_response(const struct ItlApFirmwareRuntime *runtime,
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
    if (LE_READ_2(body) != IEEE80211_AUTH_ALG_OPEN ||
        LE_READ_2(body + 2) != IEEE80211_AUTH_OPEN_REQUEST ||
        LE_READ_2(body + 4) != IEEE80211_STATUS_SUCCESS)
        return 0;

    const size_t responseLength = headerLength + 6;
    int error = itl_ap_open_alloc_reply(responseLength, &result->reply);
    if (error != 0)
        return error;
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
    LE_WRITE_2(out, IEEE80211_AUTH_ALG_OPEN);
    LE_WRITE_2(out + 2, IEEE80211_AUTH_OPEN_RESPONSE);
    LE_WRITE_2(out + 4, IEEE80211_STATUS_SUCCESS);
    result->replyLength = responseLength;
    result->disposition = kItlApOpenRxReply;
    IEEE80211_ADDR_COPY(result->station, request->i_addr2);
    return 0;
}

static inline int
itl_ap_open_parse_assoc(const struct ItlApFirmwareRuntime *runtime,
                        const struct ieee80211_frame *request,
                        size_t frameLength,
                        struct ItlApOpenRxResult *result)
{
    const size_t headerLength = sizeof(*request);
    const size_t fixedLength = 4;
    if (!itl_ap_open_is_running(runtime) || request == NULL || result == NULL ||
        frameLength < headerLength + fixedLength ||
        (request->i_fc[0] & IEEE80211_FC0_TYPE_MASK) !=
            IEEE80211_FC0_TYPE_MGT ||
        (request->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK) !=
            IEEE80211_FC0_SUBTYPE_ASSOC_REQ ||
        !itl_ap_open_addressed_to_bss(runtime, request))
        return 0;

    result->disposition = kItlApOpenRxConsumed;
    const uint8_t *body = reinterpret_cast<const uint8_t *>(request) +
        headerLength;
    const uint16_t capability = LE_READ_2(body);
    const uint8_t *cursor = body + fixedLength;
    const uint8_t *end = reinterpret_cast<const uint8_t *>(request) +
        frameLength;
    const uint8_t *ssid = NULL;
    const uint8_t *rates = NULL;
    const uint8_t *rsn = NULL;
    while (cursor + 2 <= end) {
        const size_t elementLength = cursor[1];
        if (cursor + 2 + elementLength > end)
            break;
        if (cursor[0] == IEEE80211_ELEMID_SSID)
            ssid = cursor;
        else if (cursor[0] == IEEE80211_ELEMID_RATES)
            rates = cursor;
        else if (cursor[0] == IEEE80211_ELEMID_RSN)
            rsn = cursor;
        cursor += 2 + elementLength;
    }

    const bool valid = runtime->clientAuthenticated &&
        IEEE80211_ADDR_EQ(runtime->clientMac, request->i_addr2) &&
        (capability & IEEE80211_CAPINFO_ESS) != 0 &&
        (capability & IEEE80211_CAPINFO_PRIVACY) == 0 && rsn == NULL &&
        ssid != NULL && ssid[1] == runtime->config.ssidLength &&
        memcmp(ssid + 2, runtime->ssid, ssid[1]) == 0 &&
        rates != NULL && rates[1] != 0 &&
        rates[1] <= IEEE80211_RATE_MAXSIZE;
    if (!valid)
        return 0;

    result->disposition = kItlApOpenRxAssociate;
    IEEE80211_ADDR_COPY(result->station, request->i_addr2);
    return 0;
}

static inline int
itl_ap_open_build_assoc_success(const struct ItlApFirmwareRuntime *runtime,
                                struct ItlApOpenRxResult *result)
{
    static const uint8_t rates2g[] = {
        0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24
    };
    static const uint8_t rates5g[] = {
        0x8c, 0x12, 0x98, 0x24, 0xb0, 0x48, 0x60, 0x6c
    };
    static const uint8_t extendedRates[] = { 0x30, 0x48, 0x60, 0x6c };
    if (!itl_ap_open_is_running(runtime) || result == NULL ||
        !runtime->clientStationInstalled || runtime->clientAid == 0)
        return EINVAL;

    const bool is2g = runtime->config.channel <= 14;
    const size_t responseLength = sizeof(struct ieee80211_frame) + 6 +
        2 + sizeof(rates2g) + (is2g ? 2 + sizeof(extendedRates) : 0);
    int error = itl_ap_open_alloc_reply(responseLength, &result->reply);
    if (error != 0)
        return error;
    struct ieee80211_frame *response =
        reinterpret_cast<struct ieee80211_frame *>(result->reply);
    response->i_fc[0] = IEEE80211_FC0_VERSION_0 |
        IEEE80211_FC0_TYPE_MGT | IEEE80211_FC0_SUBTYPE_ASSOC_RESP;
    response->i_fc[1] = IEEE80211_FC1_DIR_NODS;
    IEEE80211_ADDR_COPY(response->i_addr1, runtime->clientMac);
    IEEE80211_ADDR_COPY(response->i_addr2, runtime->config.bssid);
    IEEE80211_ADDR_COPY(response->i_addr3, runtime->config.bssid);
    uint8_t *out = result->reply + sizeof(*response);
    LE_WRITE_2(out, IEEE80211_CAPINFO_ESS |
        (is2g ? IEEE80211_CAPINFO_SHORT_SLOTTIME : 0));
    LE_WRITE_2(out + 2, IEEE80211_STATUS_SUCCESS);
    LE_WRITE_2(out + 4, runtime->clientAid | 0xc000);
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
    result->replyLength = static_cast<size_t>(out - result->reply);
    result->disposition = kItlApOpenRxReply;
    IEEE80211_ADDR_COPY(result->station, runtime->clientMac);
    return 0;
}

static inline int
itl_ap_open_decap_data(const struct ItlApFirmwareRuntime *runtime,
                       mbuf_t packet, size_t frameLength,
                       struct ItlApOpenRxResult *result)
{
    if (!itl_ap_open_is_running(runtime) || packet == NULL || result == NULL ||
        !runtime->clientAssociated || !runtime->clientAuthorized ||
        frameLength < sizeof(struct ieee80211_frame))
        return 0;
    const struct ieee80211_frame *wh =
        mtod(packet, const struct ieee80211_frame *);
    if ((wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) !=
            IEEE80211_FC0_TYPE_DATA ||
        (wh->i_fc[1] & IEEE80211_FC1_DIR_MASK) != IEEE80211_FC1_DIR_TODS ||
        (wh->i_fc[1] & IEEE80211_FC1_PROTECTED) != 0 ||
        !IEEE80211_ADDR_EQ(wh->i_addr1, runtime->config.bssid) ||
        !IEEE80211_ADDR_EQ(wh->i_addr2, runtime->clientMac))
        return 0;

    result->disposition = kItlApOpenRxConsumed;
    if ((wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_NODATA) != 0)
        return 0;
    const size_t headerLength = ieee80211_get_hdrlen(wh);
    if (headerLength < sizeof(*wh) ||
        frameLength < headerLength + LLC_SNAPFRAMELEN)
        return 0;
    struct llc llc;
    if (mbuf_copydata(packet, headerLength, sizeof(llc), &llc) != 0 ||
        llc.llc_dsap != LLC_SNAP_LSAP || llc.llc_ssap != LLC_SNAP_LSAP ||
        llc.llc_control != LLC_UI || llc.llc_snap.org_code[0] != 0 ||
        llc.llc_snap.org_code[1] != 0 || llc.llc_snap.org_code[2] != 0)
        return 0;
    const size_t payloadLength = frameLength - headerLength -
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
            headerLength + LLC_SNAPFRAMELEN, payloadLength,
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
                       mbuf_t ethernetPacket, mbuf_t *wirePacket)
{
    if (!itl_ap_open_is_running(runtime) || ethernetPacket == NULL ||
        wirePacket == NULL || !runtime->clientAssociated ||
        !runtime->clientAuthorized)
        return EINVAL;
    const size_t ethernetLength = mbuf_pkthdr_len(ethernetPacket);
    if (ethernetLength < ETHER_HDR_LEN ||
        ethernetLength - ETHER_HDR_LEN >
            MCLBYTES - sizeof(struct ieee80211_frame) - LLC_SNAPFRAMELEN)
        return EMSGSIZE;
    struct ether_header ethernet;
    if (mbuf_copydata(ethernetPacket, 0, sizeof(ethernet), &ethernet) != 0)
        return EINVAL;
    const bool multicast = IEEE80211_IS_MULTICAST(ethernet.ether_dhost);
    if (!multicast &&
        !IEEE80211_ADDR_EQ(ethernet.ether_dhost, runtime->clientMac))
        return EHOSTUNREACH;

    const size_t wireLength = sizeof(struct ieee80211_frame) +
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
    wh->i_fc[0] = IEEE80211_FC0_VERSION_0 | IEEE80211_FC0_TYPE_DATA;
    wh->i_fc[1] = IEEE80211_FC1_DIR_FROMDS;
    IEEE80211_ADDR_COPY(wh->i_addr1, ethernet.ether_dhost);
    IEEE80211_ADDR_COPY(wh->i_addr2, runtime->config.bssid);
    IEEE80211_ADDR_COPY(wh->i_addr3, ethernet.ether_shost);
    struct llc *llc = reinterpret_cast<struct llc *>(bytes + sizeof(*wh));
    llc->llc_dsap = LLC_SNAP_LSAP;
    llc->llc_ssap = LLC_SNAP_LSAP;
    llc->llc_control = LLC_UI;
    llc->llc_snap.ether_type = ethernet.ether_type;
    if (ethernetLength > ETHER_HDR_LEN && mbuf_copydata(ethernetPacket,
            ETHER_HDR_LEN, ethernetLength - ETHER_HDR_LEN,
            bytes + sizeof(*wh) + LLC_SNAPFRAMELEN) != 0) {
        mbuf_freem(*wirePacket);
        *wirePacket = NULL;
        return EINVAL;
    }
    return 0;
}

static inline int
itl_ap_open_classify_rx(const struct ItlApFirmwareRuntime *runtime,
                        mbuf_t packet, size_t frameLength,
                        struct ItlApOpenRxResult *result)
{
    itl_ap_open_rx_result_reset(result);
    if (!itl_ap_open_is_running(runtime) || packet == NULL ||
        frameLength < sizeof(struct ieee80211_frame))
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
    error = itl_ap_open_decap_data(runtime, packet, frameLength, result);
    if (error != 0 || result->disposition != kItlApOpenRxNotOurs)
        return error;

    if ((wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) ==
            IEEE80211_FC0_TYPE_MGT &&
        ((wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK) ==
             IEEE80211_FC0_SUBTYPE_DEAUTH ||
         (wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK) ==
             IEEE80211_FC0_SUBTYPE_DISASSOC) &&
        IEEE80211_ADDR_EQ(wh->i_addr1, runtime->config.bssid) &&
        IEEE80211_ADDR_EQ(wh->i_addr2, runtime->clientMac)) {
        result->disposition = kItlApOpenRxDisconnect;
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
