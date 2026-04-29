//
//  TahoeCommanderV2.hpp
//  AirportItlwm
//

#ifndef TahoeCommanderV2_hpp
#define TahoeCommanderV2_hpp

#include "TahoeCompletion.hpp"
#include "TahoeErrorMap.hpp"
#include "TahoeOwnerRegistry.hpp"
#include "TahoeOwners.hpp"
#include "TahoePayloadBuilders.hpp"
#include <Airport/Apple80211.h>

class TahoeCommanderV2 {
public:
    explicit TahoeCommanderV2(TahoeOwnerRegistry *registry = nullptr)
    : registry(registry),
      nextToken(1),
      joinIeOwner(registry),
      ndpOwner(registry),
      usbHostOwner(registry),
      btcoexOwner(registry),
      actionFrameOwner(registry),
      rangingOwner(registry),
      txPowerCapOwner(registry) {}

    void bind(TahoeOwnerRegistry *newRegistry)
    {
        registry = newRegistry;
        joinIeOwner.bind(newRegistry);
        ndpOwner.bind(newRegistry);
        usbHostOwner.bind(newRegistry);
        btcoexOwner.bind(newRegistry);
        actionFrameOwner.bind(newRegistry);
        rangingOwner.bind(newRegistry);
        txPowerCapOwner.bind(newRegistry);
    }

    TahoeOwnerRegistry *getRegistry() const { return registry; }

    IOReturn runSetIE(const apple80211_ie_data *data,
                      TahoeAsyncCommandContext *asyncContext = nullptr)
    {
        TahoePayloadBuilders::IEPayloads payload;
        if (!TahoePayloadBuilders::buildIE(data, &payload))
            return TahoeErrorMap::kAppleInvalidArgumentRaw;

        const IOReturn rc = joinIeOwner.apply(data, asyncContext);
        if (rc != kIOReturnSuccess)
            return rc;

        if (payload.customAssoc)
            return dispatchIOVarSet(552, TahoeCommandRouter::routeIE(true), payload.ieLen,
                                    0, asyncContext, false);
        return dispatchVirtualIOVarSet(552, TahoeCommandRouter::routeIE(false), payload.ieLen,
                                       0, asyncContext, false);
    }

    IOReturn runSetOFFLOADNDP(const apple80211_offload_ndp_data *data,
                              TahoeAsyncCommandContext *asyncContext = nullptr)
    {
        const IOReturn rc = ndpOwner.apply(data, asyncContext);
        if (rc != kIOReturnSuccess)
            return rc;

        const auto &owner = registry->ndp;
        IOReturn transportRc = dispatchHiddenCallback(554, TahoeCommandRouter::routeOffloadNdp(),
                                                      owner.count * 16, 0, asyncContext, false);
        if (transportRc != kIOReturnSuccess)
            return transportRc;
        return dispatchVirtualIOCtlSet(554, TahoeCommandRouter::routeOffloadNdp(),
                                       owner.count * 16, 0, asyncContext, false);
    }

    IOReturn runSetUSBHostNotification(const apple80211_usb_host_notification_data *data,
                                       TahoeAsyncCommandContext *asyncContext = nullptr)
    {
        const IOReturn rc = usbHostOwner.apply(data, asyncContext);
        if (rc != kIOReturnSuccess)
            return rc;

        const auto &owner = registry->usbHostNotification;
        IOReturn transportRc = dispatchHiddenCallback(579, TahoeCommandRouter::routeUSBHostNotification(),
                                                      sizeof(owner.present), 0, asyncContext, false);
        if (transportRc != kIOReturnSuccess)
            return transportRc;
        transportRc = dispatchIOVarSet(579, TahoeCommandRouter::routeUSBHostNotification(),
                                       sizeof(owner.present), 0, asyncContext, false);
        if (transportRc != kIOReturnSuccess || !owner.hasChangePayload)
            return transportRc;
        return dispatchIOVarSet(579, TahoeCommandRouter::routeUSBHostNotification(),
                                sizeof(owner.change), 0, asyncContext, false);
    }

    IOReturn runSetBTCOEXProfile(const apple80211_btcoex_profile *data,
                                 TahoeAsyncCommandContext *asyncContext = nullptr)
    {
        const IOReturn rc = btcoexOwner.applyProfile(data, asyncContext);
        if (rc != kIOReturnSuccess)
            return rc;
        return dispatchIOVarSet(571, TahoeCommandRouter::routeBtcoex(), 0x38, 0, asyncContext, false);
    }

    IOReturn runSetBTCOEXProfileActive(const apple80211_btcoex_profile_active_data *data,
                                       TahoeAsyncCommandContext *asyncContext = nullptr)
    {
        const IOReturn rc = btcoexOwner.applyActive(data, asyncContext);
        if (rc != kIOReturnSuccess)
            return rc;
        return dispatchIOVarSet(572, TahoeCommandRouter::routeBtcoex(), sizeof(uint32_t), 0,
                                asyncContext, false);
    }

    IOReturn runSetBTCOEX2GChainDisable(const apple80211_btcoex_2g_chain_disable *data,
                                        TahoeAsyncCommandContext *asyncContext = nullptr)
    {
        const IOReturn rc = btcoexOwner.apply2GChainDisable(data, asyncContext);
        if (rc != kIOReturnSuccess)
            return rc;
        return dispatchIOVarSet(574, TahoeCommandRouter::routeBtcoex(), 6, 0, asyncContext, false);
    }

    IOReturn runSetBypassTxPowerCap(const apple80211_bypass_tx_power_cap *data,
                                    TahoeAsyncCommandContext *asyncContext = nullptr)
    {
        const IOReturn rc = txPowerCapOwner.apply(data, asyncContext);
        if (rc != kIOReturnSuccess)
            return rc;
        const auto &owner = registry->txPowerCapBypass;
        if (!owner.sendEligible)
            return kIOReturnSuccess;
        return dispatchIOVarSet(622, TahoeCommandRouter::routeBypassTxPowerCap(),
                                sizeof(uint32_t), 0, asyncContext, false);
    }

    IOReturn runSetWCLActionFrame(const apple80211_wcl_action_frame *data,
                                  uint32_t firmwareGeneration,
                                  TahoeAsyncCommandContext *asyncContext = nullptr)
    {
        const IOReturn rc = actionFrameOwner.apply(data, firmwareGeneration, asyncContext);
        if (rc != kIOReturnSuccess)
            return rc;

        const auto &owner = registry->actionFrame;
        if (owner.useV2)
            return dispatchIssueCommand(620, TahoeCommandRouter::routeActionFrame(),
                                        owner.frameLen, 0, asyncContext, true);
        return dispatchIOVarSet(620, TahoeCommandRouter::routeActionFrame(),
                                TahoePayloadBuilders::kActionFrameV1TxPayloadSize,
                                0, asyncContext, false);
    }

    IOReturn runSetRangingAuthenticate(const apple80211_ranging_authenticate_request_t *data,
                                       uint32_t proximityOwnerId,
                                       TahoeAsyncCommandContext *asyncContext = nullptr)
    {
        const IOReturn rc = rangingOwner.apply(data, proximityOwnerId, asyncContext);
        if (rc != kIOReturnSuccess)
            return rc;

        const auto &owner = registry->ranging;
        IOReturn transportRc = dispatchIOVarSet(567, TahoeCommandRouter::routeRanging(),
                                                sizeof(uint32_t), 0, asyncContext, false);
        if (transportRc != kIOReturnSuccess)
            return transportRc;
        transportRc = dispatchIOVarSet(567, TahoeCommandRouter::routeRanging(),
                                       sizeof(uint32_t), 0, asyncContext, false);
        if (transportRc != kIOReturnSuccess)
            return transportRc;
        transportRc = dispatchVirtualIOCtlSet(567, TahoeCommandRouter::routeRanging(),
                                              sizeof(uint32_t), 0, asyncContext, false);
        if (transportRc != kIOReturnSuccess)
            return transportRc;
        transportRc = dispatchIssueCommand(567, TahoeCommandRouter::routeRanging(),
                                           owner.pmkLen + 0x20, 0, asyncContext, true);
        if (transportRc != kIOReturnSuccess)
            return transportRc;
        if (!owner.postedCallback)
            return kIOReturnSuccess;
        return dispatchHiddenCallback(567, TahoeCommandRouter::routeRanging(),
                                      owner.pmkLen, 0, asyncContext, false);
    }

private:
    IOReturn dispatchTransport(uint32_t selector,
                               uint32_t owner,
                               uint32_t transport,
                               uint32_t requestBytes,
                               uint32_t responseBytes,
                               TahoeAsyncCommandContext *asyncContext,
                               bool async)
    {
        if (asyncContext != nullptr) {
            TahoeCompletion::begin(asyncContext, nextToken++, selector, owner, transport,
                                   requestBytes, responseBytes, async);
            TahoeCompletion::complete(asyncContext, 0);
        }
        return kIOReturnSuccess;
    }

    IOReturn dispatchIOVarSet(uint32_t selector,
                              uint32_t owner,
                              uint32_t requestBytes,
                              uint32_t responseBytes,
                              TahoeAsyncCommandContext *asyncContext,
                              bool async)
    {
        return dispatchTransport(selector, owner, kTahoeTransportIOVarSet, requestBytes,
                                 responseBytes, asyncContext, async);
    }

    IOReturn dispatchVirtualIOVarSet(uint32_t selector,
                                     uint32_t owner,
                                     uint32_t requestBytes,
                                     uint32_t responseBytes,
                                     TahoeAsyncCommandContext *asyncContext,
                                     bool async)
    {
        return dispatchTransport(selector, owner, kTahoeTransportVirtualIOVarSet, requestBytes,
                                 responseBytes, asyncContext, async);
    }

    IOReturn dispatchVirtualIOCtlSet(uint32_t selector,
                                     uint32_t owner,
                                     uint32_t requestBytes,
                                     uint32_t responseBytes,
                                     TahoeAsyncCommandContext *asyncContext,
                                     bool async)
    {
        return dispatchTransport(selector, owner, kTahoeTransportVirtualIOCtlSet, requestBytes,
                                 responseBytes, asyncContext, async);
    }

    IOReturn dispatchIssueCommand(uint32_t selector,
                                  uint32_t owner,
                                  uint32_t requestBytes,
                                  uint32_t responseBytes,
                                  TahoeAsyncCommandContext *asyncContext,
                                  bool async)
    {
        return dispatchTransport(selector, owner, kTahoeTransportIssueCommand, requestBytes,
                                 responseBytes, asyncContext, async);
    }

    IOReturn dispatchHiddenCallback(uint32_t selector,
                                    uint32_t owner,
                                    uint32_t requestBytes,
                                    uint32_t responseBytes,
                                    TahoeAsyncCommandContext *asyncContext,
                                    bool async)
    {
        return dispatchTransport(selector, owner, kTahoeTransportHiddenCallback, requestBytes,
                                 responseBytes, asyncContext, async);
    }

    TahoeOwnerRegistry *registry;
    uint32_t nextToken;
    TahoeJoinIeOwner joinIeOwner;
    TahoeNdpOwner ndpOwner;
    TahoeUsbHostOwner usbHostOwner;
    TahoeBtcoexOwner btcoexOwner;
    TahoeActionFrameOwner actionFrameOwner;
    TahoeRangingOwner rangingOwner;
    TahoeTxPowerCapOwner txPowerCapOwner;
};

#endif /* TahoeCommanderV2_hpp */
