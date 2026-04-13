//
//  TahoeOwners.hpp
//  AirportItlwm
//

#ifndef TahoeOwners_hpp
#define TahoeOwners_hpp

#include "TahoeCommandRouter.hpp"
#include "TahoeErrorMap.hpp"
#include "TahoeOwnerBase.hpp"
#include "TahoeOwnerRegistry.hpp"
#include "TahoePayloadBuilders.hpp"
#include <Airport/Apple80211.h>

class TahoeUsbHostOwner : public TahoeOwnerBase {
public:
    explicit TahoeUsbHostOwner(TahoeOwnerRegistry *registry = nullptr) : registry(registry) {}

    void bind(TahoeOwnerRegistry *newRegistry) { registry = newRegistry; }

    IOReturn apply(const apple80211_usb_host_notification_data *data,
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

        completeSync(asyncContext, 579, TahoeCommandRouter::routeUSBHostNotification());
        return kIOReturnSuccess;
    }

private:
    TahoeOwnerRegistry *registry;
};

class TahoeBtcoexOwner : public TahoeOwnerBase {
public:
    explicit TahoeBtcoexOwner(TahoeOwnerRegistry *registry = nullptr) : registry(registry) {}

    void bind(TahoeOwnerRegistry *newRegistry) { registry = newRegistry; }

    IOReturn applyProfile(const apple80211_btcoex_profile *data,
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

        completeSync(asyncContext, 571, TahoeCommandRouter::routeBtcoex());
        return kIOReturnSuccess;
    }

    IOReturn applyActive(const apple80211_btcoex_profile_active_data *data,
                         TahoeAsyncCommandContext *asyncContext = nullptr)
    {
        if (data == nullptr || registry == nullptr)
            return TahoeErrorMap::kAppleInvalidArgument;

        TahoePayloadBuilders::BtcoexProfileActivePayload payload;
        if (!TahoePayloadBuilders::buildBtcoexProfileActive(data, &payload))
            return TahoeErrorMap::kAppleInvalidArgument;

        registry->btcoex.activeProfile = payload.activeProfile;
        completeSync(asyncContext, 572, TahoeCommandRouter::routeBtcoex());
        return kIOReturnSuccess;
    }

    IOReturn apply2GChainDisable(const apple80211_btcoex_2g_chain_disable *data,
                                 TahoeAsyncCommandContext *asyncContext = nullptr)
    {
        if (data == nullptr || registry == nullptr)
            return TahoeErrorMap::kAppleInvalidArgument;

        TahoePayloadBuilders::Btcoex2GChainDisablePayload payload;
        if (!TahoePayloadBuilders::buildBtcoex2GChainDisable(data, &payload))
            return TahoeErrorMap::kAppleInvalidArgument;

        registry->btcoex.chainDisable = payload.chainDisable;
        completeSync(asyncContext, 574, TahoeCommandRouter::routeBtcoex());
        return kIOReturnSuccess;
    }

private:
    TahoeOwnerRegistry *registry;
};

class TahoeTxPowerCapOwner : public TahoeOwnerBase {
public:
    explicit TahoeTxPowerCapOwner(TahoeOwnerRegistry *registry = nullptr) : registry(registry) {}

    void bind(TahoeOwnerRegistry *newRegistry) { registry = newRegistry; }

    IOReturn apply(const apple80211_bypass_tx_power_cap *data,
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

        completeSync(asyncContext, 622, TahoeCommandRouter::routeBypassTxPowerCap());
        return kIOReturnSuccess;
    }

private:
    TahoeOwnerRegistry *registry;
};

#endif /* TahoeOwners_hpp */
