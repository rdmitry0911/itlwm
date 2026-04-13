//
//  TahoeCommanderV2.hpp
//  AirportItlwm
//

#ifndef TahoeCommanderV2_hpp
#define TahoeCommanderV2_hpp

#include "TahoeErrorMap.hpp"
#include "TahoeOwnerRegistry.hpp"
#include "TahoeOwners.hpp"
#include "TahoePayloadBuilders.hpp"
#include <Airport/Apple80211.h>

class TahoeCommanderV2 {
public:
    explicit TahoeCommanderV2(TahoeOwnerRegistry *registry = nullptr)
    : registry(registry),
      usbHostOwner(registry),
      btcoexOwner(registry),
      txPowerCapOwner(registry) {}

    void bind(TahoeOwnerRegistry *newRegistry)
    {
        registry = newRegistry;
        usbHostOwner.bind(newRegistry);
        btcoexOwner.bind(newRegistry);
        txPowerCapOwner.bind(newRegistry);
    }

    TahoeOwnerRegistry *getRegistry() const { return registry; }

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

        TahoeOwnerBase::completeSync(asyncContext, 552,
                                     TahoeCommandRouter::routeIE(payload.customAssoc));
        return kIOReturnSuccess;
    }

    IOReturn runSetOFFLOADNDP(const apple80211_offload_ndp_data *data,
                              TahoeAsyncCommandContext *asyncContext = nullptr)
    {
        if (data == nullptr || registry == nullptr)
            return TahoeErrorMap::kAppleInvalidArgumentRaw;

        TahoePayloadBuilders::NdpPayload payload;
        if (!TahoePayloadBuilders::buildOffloadNdp(data, &payload))
            return TahoeErrorMap::kAppleInvalidArgumentRaw;

        registry->ndp.count = payload.count;
        memset(registry->ndp.addresses, 0, sizeof(registry->ndp.addresses));
        memcpy(registry->ndp.addresses, payload.addresses, sizeof(payload.addresses));
        memcpy(registry->ndp.linkLocalSeed, payload.linkLocalSeed, sizeof(payload.linkLocalSeed));
        registry->ndp.hasCarrier = true;
        registry->ndp.hiddenNotifyQueued = true;

        TahoeOwnerBase::completeSync(asyncContext, 554, TahoeCommandRouter::routeOffloadNdp());
        return kIOReturnSuccess;
    }

    IOReturn runSetUSBHostNotification(const apple80211_usb_host_notification_data *data,
                                       TahoeAsyncCommandContext *asyncContext = nullptr)
    {
        return usbHostOwner.apply(data, asyncContext);
    }

    IOReturn runSetBTCOEXProfile(const apple80211_btcoex_profile *data,
                                 TahoeAsyncCommandContext *asyncContext = nullptr)
    {
        return btcoexOwner.applyProfile(data, asyncContext);
    }

    IOReturn runSetBTCOEXProfileActive(const apple80211_btcoex_profile_active_data *data,
                                       TahoeAsyncCommandContext *asyncContext = nullptr)
    {
        return btcoexOwner.applyActive(data, asyncContext);
    }

    IOReturn runSetBTCOEX2GChainDisable(const apple80211_btcoex_2g_chain_disable *data,
                                        TahoeAsyncCommandContext *asyncContext = nullptr)
    {
        return btcoexOwner.apply2GChainDisable(data, asyncContext);
    }

    IOReturn runSetBypassTxPowerCap(const apple80211_bypass_tx_power_cap *data,
                                    TahoeAsyncCommandContext *asyncContext = nullptr)
    {
        return txPowerCapOwner.apply(data, asyncContext);
    }

    IOReturn runSetWCLActionFrame(const apple80211_wcl_action_frame *data,
                                  uint32_t firmwareGeneration,
                                  TahoeAsyncCommandContext *asyncContext = nullptr)
    {
        if (data == nullptr || registry == nullptr)
            return TahoeErrorMap::kAppleBadArgumentTahoe;

        TahoePayloadBuilders::ActionFramePayload payload;
        if (!TahoePayloadBuilders::buildActionFrame(data, firmwareGeneration, &payload))
            return TahoeErrorMap::kAppleBadArgumentTahoe;

        registry->actionFrame.category = payload.category;
        registry->actionFrame.channel = payload.channel;
        registry->actionFrame.frameLen = payload.frameLen;
        memset(registry->actionFrame.frame, 0, sizeof(registry->actionFrame.frame));
        if (payload.frameLen != 0)
            memcpy(registry->actionFrame.frame, payload.frame, payload.frameLen);
        registry->actionFrame.useV2 = payload.useV2;
        registry->actionFrame.hasFrame = true;

        TahoeOwnerBase::completeSync(asyncContext, 620, TahoeCommandRouter::routeActionFrame());
        return kIOReturnSuccess;
    }

    IOReturn runSetRangingAuthenticate(const apple80211_ranging_authenticate_request_t *data,
                                       uint32_t proximityOwnerId,
                                       TahoeAsyncCommandContext *asyncContext = nullptr)
    {
        if (data == nullptr || registry == nullptr)
            return TahoeErrorMap::kAppleRangingInvalid;

        TahoePayloadBuilders::RangingAuthenticatePayload payload;
        if (!TahoePayloadBuilders::buildRangingAuthenticate(data, proximityOwnerId, &payload))
            return TahoeErrorMap::kAppleRangingInvalid;

        registry->ranging.pmkLen = payload.pmkLen;
        registry->ranging.role = payload.role;
        registry->ranging.proximityOwnerId = proximityOwnerId;
        registry->ranging.postedCallback = payload.shouldPostCallback;
        registry->ranging.hasCarrier = true;

        TahoeOwnerBase::completeSync(asyncContext, 567, TahoeCommandRouter::routeRanging());
        return kIOReturnSuccess;
    }

private:
    TahoeOwnerRegistry *registry;
    TahoeUsbHostOwner usbHostOwner;
    TahoeBtcoexOwner btcoexOwner;
    TahoeTxPowerCapOwner txPowerCapOwner;
};

#endif /* TahoeCommanderV2_hpp */
