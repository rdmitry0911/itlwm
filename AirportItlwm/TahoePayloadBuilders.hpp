//
//  TahoePayloadBuilders.hpp
//  AirportItlwm
//

#ifndef TahoePayloadBuilders_hpp
#define TahoePayloadBuilders_hpp

#include <Airport/Apple80211.h>
#include <stdint.h>

namespace TahoePayloadBuilders {

struct UsbHostNotificationPayloads {
    uint32_t present = 0;
    uint32_t change = 0;
    bool hasChangePayload = false;
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

} // namespace TahoePayloadBuilders

#endif /* TahoePayloadBuilders_hpp */
