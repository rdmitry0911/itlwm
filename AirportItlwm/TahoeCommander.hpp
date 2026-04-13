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

private:
    TahoeOwnerRegistry *registry;
};

#endif /* TahoeCommander_hpp */
