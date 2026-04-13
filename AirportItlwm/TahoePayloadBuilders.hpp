//
//  TahoePayloadBuilders.hpp
//  AirportItlwm
//

#ifndef TahoePayloadBuilders_hpp
#define TahoePayloadBuilders_hpp

#include <Airport/Apple80211.h>
#include <stdint.h>

namespace TahoePayloadBuilders {

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
    uint8_t frame[0x708] = {};
    bool useV2 = false;
};

struct RangingAuthenticatePayload {
    uint32_t role = 0;
    uint16_t pmkLen = 0;
    bool shouldPostCallback = false;
};

inline bool buildIE(const apple80211_ie_data *data, IEPayloads *payload)
{
    if (data == nullptr || payload == nullptr || data->ie_len > sizeof(data->ie))
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
    if (payload->frameLen >= sizeof(payload->frame))
        return false;
    if (payload->frameLen != 0)
        memcpy(payload->frame, raw + 0x10, payload->frameLen);
    payload->useV2 = firmwareGeneration >= 0x15;
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
