//
//  TahoePayloadBuilders.hpp
//  AirportItlwm
//

#ifndef TahoePayloadBuilders_hpp
#define TahoePayloadBuilders_hpp

#include <stdint.h>

#include "TahoePayloadParity.hpp"

#ifdef TAHOE_PAYLOAD_BUILDERS_STANDALONE_TEST
#include <string.h>

#ifndef ITLWM_STANDALONE_REAL_APPLE80211_IOCTL
#define APPLE80211_MAX_CC_LEN 3

struct apple80211_ie_data {
    uint32_t version;
    uint32_t frame_type_flags;
    uint32_t add;
    uint32_t signature_len;
    uint32_t ie_len;
    uint8_t ie[2048];
};

struct apple80211_country_code_data {
    uint32_t version;
    uint8_t cc[APPLE80211_MAX_CC_LEN];
};

#define APPLE80211_WCL_AUTH_ASSOC_COMPLETE_LEN 8
#define APPLE80211_M_WCL_AUTH_ASSOC_EVENT 78
struct apple80211_wcl_auth_assoc_complete_event {
    uint32_t status;
    uint32_t reason;
};
#endif /* ITLWM_STANDALONE_REAL_APPLE80211_IOCTL */

struct alignas(4) apple80211_offload_ndp_data {
    uint8_t bytes[8 + 4 * 16];
};

struct alignas(4) apple80211_usb_host_notification_data {
    uint8_t bytes[0x10];
};

struct alignas(4) apple80211_btcoex_profile {
    uint8_t bytes[0x38];
};

struct alignas(4) apple80211_btcoex_profile_active_data {
    uint8_t bytes[0x8];
};

struct alignas(4) apple80211_btcoex_2g_chain_disable {
    uint8_t bytes[0x6];
};

struct alignas(4) apple80211_bypass_tx_power_cap {
    uint8_t bytes[0x4];
};

struct alignas(4) apple80211_wcl_action_frame {
    uint8_t bytes[0x10 + 0x708];
};

struct alignas(4) apple80211_ranging_authenticate_request_t {
    uint8_t bytes[0x80];
};
#else
#include <Airport/Apple80211.h>
#endif

namespace TahoePayloadBuilders {

static constexpr uint16_t kActionFramePayloadCapacity = 0x708;
static constexpr uint16_t kActionFrameMaximumPayloadLength =
    kActionFramePayloadCapacity - 1;
static constexpr uint16_t kActionFrameV1TxPayloadSize = 0x724;
static constexpr uint16_t kActionFrameV2CommandHeaderSize = 0x34;
static constexpr uint32_t kActionFrameV2FirmwareThreshold = 0x15;
static constexpr uint32_t kActionFrameProgressFlagOffset = 0x4478;
static constexpr uint32_t kActionFrameProgressStartMsOffset = 0x4480;
static constexpr uint32_t kActionFrameProgressOverdueMs = 0x12d;
static constexpr uint32_t kActionFrameProgressOverdueLogLine = 0x3b1d;
static constexpr uint32_t kActionFrameProgressOverdueStatusLine = 0x3b1e;
static constexpr uint32_t kActionFrameProgressOverdueStatus = 0xe3ff852b;
static constexpr uint32_t kActionFrameProgressScanRejectStatus = 0xe00002d5;
static constexpr uint32_t kActionFrameProgressScanRejectLogLine = 0x00a5;

static_assert(kActionFramePayloadCapacity == 0x708,
              "Apple WCL action-frame V2 rejects total bytes >= 0x708");
static_assert(kActionFrameMaximumPayloadLength == 0x707,
              "Apple WCL action-frame V1 accepts total bytes up to 0x707");
static_assert(kActionFrameV1TxPayloadSize == 0x724,
              "Apple WCL action-frame V1 sends fixed CommandTxPayload size 0x724");
static_assert(kActionFrameV2CommandHeaderSize == 0x34,
              "Apple WCL action-frame V2 sends frameLen plus 0x34-byte command header");
static_assert(kActionFrameV2FirmwareThreshold == 0x15,
              "Apple WCL action-frame V2 threshold is core-private generation > 0x14");
static_assert(kActionFrameProgressFlagOffset == 0x4478,
              "AppleBCMWLANCore action-frame progress byte offset");
static_assert(kActionFrameProgressStartMsOffset == 0x4480,
              "AppleBCMWLANCore action-frame progress start-ms offset");
static_assert(kActionFrameProgressOverdueMs == 0x12d,
              "AppleBCMWLANCore action-frame overdue threshold");
static_assert(kActionFrameProgressScanRejectStatus == 0xe00002d5,
              "AppleBCMWLANScanAdapter rejects scans while action-frame is in progress");

inline bool isActionFrameProgressOverdue(uint64_t nowMs, uint64_t startMs)
{
    return static_cast<uint64_t>(nowMs - startMs) >= kActionFrameProgressOverdueMs;
}

inline uint32_t actionFrameV2CommandSize(uint16_t frameLen)
{
    return static_cast<uint32_t>(frameLen) + kActionFrameV2CommandHeaderSize;
}

struct IEPayloads {
    bool customAssoc = false;
    uint32_t frameTypeFlags = 0;
    uint32_t add = 0;
    uint32_t ieLen = 0;
    uint8_t ie[2048] = {};
};

struct NdpPayload {
    uint32_t count = 0;
    uint8_t addresses[4][16] = {};
    uint8_t linkLocalSeed[16] = {};
};

struct UsbHostNotificationPayloads {
    uint32_t present = 0;
    uint32_t change = 0;
    bool hasChangePayload = false;
};

struct BtcoexProfilePayload {
    uint16_t mode = 0;
    uint8_t band = 0;
    uint8_t profileIndex = 0;
    uint8_t profileEntry[0x38] = {};
};

struct BtcoexProfileActivePayload {
    uint32_t activeProfile = 0;
};

struct Btcoex2GChainDisablePayload {
    uint32_t header = 0x00060001;
    uint16_t chainDisable = 0;
} __attribute__((packed));

struct TxPowerCapBypassPayload {
    uint32_t enabled = 0;
};

struct ActionFramePayload {
    uint8_t category = 0;
    uint32_t channel = 0;
    uint16_t frameLen = 0;
    uint8_t frame[kActionFramePayloadCapacity] = {};
    bool useV2 = false;
};

struct RangingAuthenticatePayload {
    uint32_t role = 0;
    uint16_t pmkLen = 0;
    bool shouldPostCallback = false;
};

inline bool buildIE(const apple80211_ie_data *data, IEPayloads *payload)
{
    if (data == nullptr || payload == nullptr || data->ie_len == 0 ||
        data->ie_len > sizeof(data->ie))
        return false;
    payload->frameTypeFlags = data->frame_type_flags;
    payload->add = data->add;
    payload->ieLen = data->ie_len;
    if (payload->ieLen != 0)
        memcpy(payload->ie, data->ie, payload->ieLen);
    payload->customAssoc =
        data->frame_type_flags == 4 &&
        data->add != 0 &&
        data->ie_len != 0 &&
        data->ie[0] == 0x44;
    return true;
}

inline bool buildOffloadNdp(const apple80211_offload_ndp_data *data, NdpPayload *payload)
{
    if (data == nullptr || payload == nullptr)
        return false;
    const uint8_t *raw = reinterpret_cast<const uint8_t *>(data);
    payload->count = *reinterpret_cast<const uint32_t *>(raw + 4);
    if (payload->count > 4)
        payload->count = 4;
    memset(payload->addresses, 0, sizeof(payload->addresses));
    for (uint32_t i = 0; i < payload->count; i++)
        memcpy(payload->addresses[i], raw + 8 + i * 16, 16);
    memset(payload->linkLocalSeed, 0, sizeof(payload->linkLocalSeed));
    payload->linkLocalSeed[0] = 0xfe;
    payload->linkLocalSeed[1] = 0x80;
    if (payload->count != 0)
        memcpy(&payload->linkLocalSeed[8], &payload->addresses[0][8], 8);
    return true;
}

inline bool buildUsbHostNotification(const apple80211_usb_host_notification_data *data,
                                     UsbHostNotificationPayloads *payload)
{
    if (data == nullptr || payload == nullptr)
        return false;
    const uint8_t *raw = reinterpret_cast<const uint8_t *>(data);
    payload->present = *reinterpret_cast<const uint32_t *>(raw + 0xc);
    payload->change = *reinterpret_cast<const uint32_t *>(raw + 0x8);
    payload->hasChangePayload = payload->change < 2;
    return true;
}

inline bool buildBtcoexProfile(const apple80211_btcoex_profile *data,
                               BtcoexProfilePayload *payload)
{
    if (data == nullptr || payload == nullptr)
        return false;
    const uint8_t *raw = reinterpret_cast<const uint8_t *>(data);
    payload->mode = *reinterpret_cast<const uint16_t *>(raw);
    payload->band = raw[3];
    payload->profileIndex = raw[4];
    memcpy(payload->profileEntry, raw, sizeof(payload->profileEntry));
    return true;
}

inline bool buildBtcoexProfileActive(const apple80211_btcoex_profile_active_data *data,
                                     BtcoexProfileActivePayload *payload)
{
    if (data == nullptr || payload == nullptr)
        return false;
    const uint8_t *raw = reinterpret_cast<const uint8_t *>(data);
    payload->activeProfile = *reinterpret_cast<const uint32_t *>(raw + 4);
    return true;
}

inline bool buildBtcoex2GChainDisable(const apple80211_btcoex_2g_chain_disable *data,
                                      Btcoex2GChainDisablePayload *payload)
{
    if (data == nullptr || payload == nullptr)
        return false;
    const uint8_t *raw = reinterpret_cast<const uint8_t *>(data);
    payload->chainDisable = *reinterpret_cast<const uint16_t *>(raw + 4);
    return true;
}

inline bool buildTxPowerCapBypass(const apple80211_bypass_tx_power_cap *data,
                                  TxPowerCapBypassPayload *payload)
{
    if (data == nullptr || payload == nullptr)
        return false;
    payload->enabled = (*reinterpret_cast<const uint8_t *>(data) & 1U);
    return true;
}

inline bool buildActionFrame(const apple80211_wcl_action_frame *data,
                             uint32_t firmwareGeneration,
                             ActionFramePayload *payload)
{
    if (data == nullptr || payload == nullptr)
        return false;
    const uint8_t *raw = reinterpret_cast<const uint8_t *>(data);
    payload->category = raw[0];
    payload->channel = *reinterpret_cast<const uint32_t *>(raw + 4);
    payload->frameLen = *reinterpret_cast<const uint16_t *>(raw + 0xe);
    if (payload->frameLen > kActionFrameMaximumPayloadLength)
        return false;
    if (payload->frameLen != 0)
        memcpy(payload->frame, raw + 0x10, payload->frameLen);
    payload->useV2 = firmwareGeneration >= kActionFrameV2FirmwareThreshold;
    return true;
}

inline bool buildRangingAuthenticate(const apple80211_ranging_authenticate_request_t *data,
                                     uint32_t proximityOwnerId,
                                     RangingAuthenticatePayload *payload)
{
    if (data == nullptr || payload == nullptr)
        return false;
    const uint8_t *raw = reinterpret_cast<const uint8_t *>(data);
    payload->pmkLen = *reinterpret_cast<const uint16_t *>(raw + 0x70);
    payload->role = *reinterpret_cast<const uint32_t *>(raw + 4);
    payload->shouldPostCallback = (payload->role == 4) && proximityOwnerId != 0 && proximityOwnerId != 0xff;
    return payload->pmkLen != 0;
}

} // namespace TahoePayloadBuilders

#endif /* TahoePayloadBuilders_hpp */
