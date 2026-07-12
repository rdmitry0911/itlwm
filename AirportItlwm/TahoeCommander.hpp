//
//  TahoeCommander.hpp
//  AirportItlwm
//

#ifndef TahoeCommander_hpp
#define TahoeCommander_hpp

#include "TahoeAsyncCommandContext.hpp"
#include "TahoeErrorMap.hpp"
#include "TahoeOwnerRegistry.hpp"
#include "TahoePayloadBuilders.hpp"
#include <Airport/Apple80211.h>

class TahoeCommander {
public:
    explicit TahoeCommander(TahoeOwnerRegistry *registry = nullptr)
    : registry(registry) {}

    void bind(TahoeOwnerRegistry *newRegistry)
    {
        registry = newRegistry;
    }

    TahoeOwnerRegistry *getRegistry() const
    {
        return registry;
    }

    IOReturn runSetIE(const apple80211_ie_data *data,
                      TahoeAsyncCommandContext *asyncContext = nullptr)
    {
        if (data == nullptr || registry == nullptr)
            return TahoeErrorMap::kAppleInvalidArgumentRaw;

        TahoePayloadBuilders::IEPayloads payload;
        if (!TahoePayloadBuilders::buildIE(data, &payload))
            return TahoeErrorMap::kAppleInvalidArgumentRaw;

        if (payload.customAssoc) {
            memset(registry->ie.assocIe, 0, sizeof(registry->ie.assocIe));
            if (payload.ieLen != 0)
                memcpy(registry->ie.assocIe, payload.ie, payload.ieLen);
            registry->ie.assocIeLen = payload.ieLen;
            registry->ie.hasAssocIe = true;
        } else {
            memset(registry->ie.vendorIe, 0, sizeof(registry->ie.vendorIe));
            if (payload.ieLen != 0)
                memcpy(registry->ie.vendorIe, payload.ie, payload.ieLen);
            registry->ie.vendorIeLen = payload.ieLen;
            registry->ie.vendorIeFlags = payload.frameTypeFlags;
            registry->ie.hasVendorIe = true;
        }

        if (asyncContext != nullptr) {
            asyncContext->selector = 552;
            asyncContext->owner = payload.customAssoc ? 0x1528 : 0x1520;
            asyncContext->status = 0;
            asyncContext->async = false;
            asyncContext->completed = true;
        }
        return kIOReturnSuccess;
    }

    IOReturn runSetOFFLOADNDP(const apple80211_offload_ndp_data *data,
                              TahoeAsyncCommandContext * = nullptr)
    {
        if (data == nullptr || registry == nullptr)
            return TahoeErrorMap::kAppleInvalidArgumentRaw;

        return TahoeErrorMap::kAppleInvalidArgumentRaw;
    }

    IOReturn runSetUSBHostNotification(const apple80211_usb_host_notification_data *data,
                                       TahoeAsyncCommandContext *asyncContext = nullptr)
    {
        if (data == nullptr || registry == nullptr)
            return TahoeErrorMap::kAppleBadArgumentTahoe;

        TahoePayloadBuilders::UsbHostNotificationPayloads payload;
        if (!TahoePayloadBuilders::buildUsbHostNotification(data, &payload))
            return TahoeErrorMap::kAppleBadArgumentTahoe;

        registry->usbHostNotification.sequenceNumber =
            *reinterpret_cast<const uint32_t *>(reinterpret_cast<const uint8_t *>(data) + 4);
        registry->usbHostNotification.change = payload.change;
        registry->usbHostNotification.present = payload.present;
        registry->usbHostNotification.lastAsymMitExtUsb = payload.present;
        registry->usbHostNotification.lastAsymMitExtUsbChange = payload.change;
        registry->usbHostNotification.hasChangePayload = payload.hasChangePayload;
        registry->usbHostNotification.hasCarrier = true;

        if (asyncContext != nullptr) {
            // Tahoe slot [579] setUSB_HOST_NOTIFICATION.
            asyncContext->selector = 579;
            asyncContext->owner = 0x1510;
            asyncContext->status = 0;
            asyncContext->async = false;
            asyncContext->completed = true;
        }
        return kIOReturnSuccess;
    }

    IOReturn runSetBTCOEXProfileActive(const apple80211_btcoex_profile_active_data *data,
                                       TahoeAsyncCommandContext *asyncContext = nullptr)
    {
        if (data == nullptr || registry == nullptr)
            return TahoeErrorMap::kAppleInvalidArgument;

        TahoePayloadBuilders::BtcoexProfileActivePayload payload;
        if (!TahoePayloadBuilders::buildBtcoexProfileActive(data, &payload))
            return TahoeErrorMap::kAppleInvalidArgument;

        registry->btcoex.activeProfile = payload.activeProfile;
        if (asyncContext != nullptr) {
            // Tahoe slot [572] setBTCOEX_PROFILE_ACTIVE.
            asyncContext->selector = 572;
            asyncContext->owner = 0x1520;
            asyncContext->status = 0;
            asyncContext->async = false;
            asyncContext->completed = true;
        }
        return kIOReturnSuccess;
    }

    IOReturn runSetBTCOEXProfile(const apple80211_btcoex_profile *data,
                                 TahoeAsyncCommandContext *asyncContext = nullptr)
    {
        if (data == nullptr || registry == nullptr)
            return TahoeErrorMap::kAppleInvalidArgument;

        TahoePayloadBuilders::BtcoexProfilePayload payload;
        if (!TahoePayloadBuilders::buildBtcoexProfile(data, &payload))
            return TahoeErrorMap::kAppleInvalidArgument;
        if (payload.band >= 5 || payload.mode < 1 || payload.mode > 4 || payload.profileIndex >= 10)
            return TahoeErrorMap::kAppleInvalidArgument;

        memcpy(registry->btcoex.profileTable[payload.profileIndex],
               payload.profileEntry,
               sizeof(registry->btcoex.profileTable[payload.profileIndex]));
        registry->btcoex.profileValidMask |= static_cast<uint16_t>(1U << payload.profileIndex);
        registry->btcoex.profileCommandMode = payload.mode;
        registry->btcoex.profileCommandBand = payload.band;
        registry->btcoex.profileCommandIndex = payload.profileIndex;

        if (asyncContext != nullptr) {
            asyncContext->selector = 571;
            asyncContext->owner = 0x1520;
            asyncContext->status = 0;
            asyncContext->async = false;
            asyncContext->completed = true;
        }
        return kIOReturnSuccess;
    }

    IOReturn runSetBTCOEX2GChainDisable(const apple80211_btcoex_2g_chain_disable *data,
                                        TahoeAsyncCommandContext *asyncContext = nullptr)
    {
        if (data == nullptr || registry == nullptr)
            return TahoeErrorMap::kAppleInvalidArgument;

        TahoePayloadBuilders::Btcoex2GChainDisablePayload payload;
        if (!TahoePayloadBuilders::buildBtcoex2GChainDisable(data, &payload))
            return TahoeErrorMap::kAppleInvalidArgument;

        registry->btcoex.chainDisable = payload.chainDisable;
        if (asyncContext != nullptr) {
            // Tahoe slot [574] setBTCOEX_2G_CHAIN_DISABLE.
            asyncContext->selector = 574;
            asyncContext->owner = 0x1520;
            asyncContext->status = 0;
            asyncContext->async = false;
            asyncContext->completed = true;
        }
        return kIOReturnSuccess;
    }

    IOReturn runSetBypassTxPowerCap(const apple80211_bypass_tx_power_cap *data,
                                    TahoeAsyncCommandContext *asyncContext = nullptr)
    {
        if (data == nullptr || registry == nullptr)
            return TahoeErrorMap::kAppleBadArgumentTahoe;

        TahoePayloadBuilders::TxPowerCapBypassPayload payload;
        if (!TahoePayloadBuilders::buildTxPowerCapBypass(data, &payload))
            return TahoeErrorMap::kAppleBadArgumentTahoe;

        registry->txPowerCapBypass.enabled = payload.enabled != 0;
        registry->txPowerCapBypass.lastPayload = payload.enabled;
        registry->txPowerCapBypass.sendEligible =
            (registry->dualPowerMode.primary != -1) &&
            (registry->dualPowerMode.secondary != -1);

        if (asyncContext != nullptr) {
            // Tahoe slot [622] setBYPASS_TX_POWER_CAP.
            asyncContext->selector = 622;
            asyncContext->owner = 0x1520;
            asyncContext->status = 0;
            asyncContext->async = false;
            asyncContext->completed = true;
        }
        return kIOReturnSuccess;
    }

    IOReturn runSetWCLActionFrame(const apple80211_wcl_action_frame *data,
                                  uint32_t firmwareGeneration,
                                  TahoeAsyncCommandContext * = nullptr)
    {
        if (data == nullptr || registry == nullptr)
            return TahoeErrorMap::kAppleBadArgumentTahoe;

        TahoePayloadBuilders::ActionFramePayload payload;
        if (!TahoePayloadBuilders::buildActionFrame(data, firmwareGeneration, &payload))
            return TahoeErrorMap::kAppleBadArgumentTahoe;

        // This legacy path has the same missing Intel firmware transport.
        return TahoeErrorMap::kAppleUnsupported;
    }

    IOReturn runSetRangingAuthenticate(const apple80211_ranging_authenticate_request_t *data,
                                       uint32_t,
                                       TahoeAsyncCommandContext * = nullptr)
    {
        if (data == nullptr || registry == nullptr)
            return TahoeErrorMap::kAppleRangingInvalid;

        TahoePayloadBuilders::RangingAuthenticatePayload payload;
        if (!TahoePayloadBuilders::buildRangingAuthenticate(data, 0, &payload))
            return TahoeErrorMap::kAppleRangingInvalid;

        // The legacy entry shares the same missing proximity firmware backend.
        return TahoeErrorMap::kAppleRangingInvalid;
    }

private:
    TahoeOwnerRegistry *registry;
};

#endif /* TahoeCommander_hpp */
