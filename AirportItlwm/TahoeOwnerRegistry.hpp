//
//  TahoeOwnerRegistry.hpp
//  AirportItlwm
//

#ifndef TahoeOwnerRegistry_hpp
#define TahoeOwnerRegistry_hpp

#include "TahoeAssociationContracts.hpp"
#include "TahoeControllerContracts.hpp"
#include "TahoeHiddenInterfaceContracts.hpp"
#include "TahoePayloadBuilders.hpp"
#include "TahoeQosDynsarContracts.hpp"
#include <stdint.h>

struct TahoeOwnerRegistry {
    struct IEOwner {
        uint8_t assocIe[2048] = {};
        uint32_t assocIeLen = 0;
        bool hasAssocIe = false;

        uint8_t vendorIe[2048] = {};
        uint32_t vendorIeLen = 0;
        uint32_t vendorIeFlags = 0;
        bool hasVendorIe = false;
    } ie;

    struct NdpOwner {
        uint32_t count = 0;
        uint8_t addresses[4][16] = {};
        uint8_t linkLocalSeed[16] = {};
        bool hasCarrier = false;
        bool hiddenNotifyQueued = false;
    } ndp;

    struct UsbHostNotificationOwner {
        uint32_t sequenceNumber = 0;
        uint32_t change = 0;
        uint32_t present = 0;
        uint32_t lastAsymMitExtUsb = 0;
        uint32_t lastAsymMitExtUsbChange = 0;
        bool hasCarrier = false;
        bool hasChangePayload = false;
    } usbHostNotification;

    struct BtcoexOwner {
        uint8_t profileTable[10][0x38] = {};
        uint16_t profileValidMask = 0;
        uint16_t profileCommandMode = 0;
        uint8_t profileCommandBand = 0;
        uint8_t profileCommandIndex = 0;
        uint32_t activeProfile = 0;
        uint16_t chainDisable = 0;
    } btcoex;

    struct ActionFrameOwner {
        uint8_t category = 0;
        uint32_t channel = 0;
        uint16_t frameLen = 0;
        uint8_t frame[TahoePayloadBuilders::kActionFramePayloadCapacity] = {};
        bool useV2 = false;
        bool hasFrame = false;
        uint8_t progress = 0;
        uint64_t progressStartMs = 0;
    } actionFrame;

    struct RangingOwner {
        uint16_t pmkLen = 0;
        uint32_t role = 0;
        uint32_t proximityOwnerId = 0;
        bool postedCallback = false;
        bool hasCarrier = false;
    } ranging;

    struct TxPowerCapBypassOwner {
        bool enabled = false;
        bool sendEligible = false;
        uint32_t lastPayload = 0;
    } txPowerCapBypass;

    struct DualPowerModeOwner {
        int32_t primary = -1;
        int32_t secondary = -1;
    } dualPowerMode;

    struct ControllerOwner {
        bool promiscuousMode = false;
        bool multicastMode = false;
        uint32_t multicastCount = 0;
        uint8_t multicastList[TahoeControllerContracts::kMulticastMaxEntries]
                             [TahoeControllerContracts::kMulticastAddressLength] = {};
        uint16_t dataQueueDepth = TahoeControllerContracts::kAppleDataQueueDepthDefault;
        uint16_t coalesceQueueSize = 0;
        uint16_t coalesceTimeout = 0;
    } controller;

    struct HiddenInterfaceOwner {
        bool flowIdSupported = false;
        uint32_t flowQueueRequestCount = 0;
        uint32_t flowQueueReleaseCount = 0;
        uint32_t packetTimestampEnableCount = 0;
        uint32_t packetTimestampDisableCount = 0;
        bool packetTimestampingEnabled = false;
        bool hasLogPipeSurface = false;
        uint32_t virtualInterfaceEnableCount = 0;
        uint32_t virtualInterfaceDisableCount = 0;
        uint32_t virtualInterfaceCreateCount = 0;
    } hiddenInterface;

    struct QosDynsarOwner {
        uint64_t dynSarFailSafeStartTicks = 0;
        uint8_t congestionControlFeature = 0;
        uint32_t forceAwdlAmpdu = 0;
        uint32_t forceDisableAwdlAmpdu = 0;
        uint32_t hwFeatureFlags = 0;
        uint8_t splitTxStatus = 0;
        uint32_t txAddrResolveReqV4 = 0;
        uint32_t txAddrResolveReqV6 = 0;
        uint8_t slowWifiFeatureEnabled = 0;
        uint8_t lowLatencyEnabled = 0;
        uint8_t lowLatencyPowerSave = 0;
        uint16_t lowLatencyWindow = 0;
        uint8_t txBlankingStatus = 0;
    } qosDynsar;

    struct AssociationOwner {
        bool hasCarrier = false;
        // The ordinary IOC_ASSOCIATE path is still completed through
        // Tahoe's JoinAdapter carrier.  Keep its provenance explicit so an
        // exact duplicate public request can preserve its in-flight lease
        // without ever inheriting a preceding WCL candidate.
        bool publicCarrier = false;
        bool selectedFromCandidate = false;
        // Set immediately before the accepted WCL request resumes normal
        // scan/selection.  Parsed carriers that are blocked, stale, or never
        // reach a real association edge must not publish completion.
        bool authAssocCompletionArmed = false;
        // Accepted ingress identity, including discovery before AUTH. Epoch
        // binding is added by the lower selected-BSS owner, never guessed
        // from a later failed cleanup or a repeated same-BSSID request.
        uint64_t joinAttemptGeneration = 0;
        // The reference JoinAdapter publishes its candidate-matched 0xd3
        // completion once.  Keep the equivalent one-shot state alongside the
        // WCL carrier so an old candidate cannot complete a later public or
        // replacement association.
        bool authAssocCompletionPublished = false;
        // Normal 0xd3 success is not derived from association status alone.
        // Retain the exact successful authentication epoch/BSSID observed by
        // net80211 so the later fully-validated association edge can consume
        // only its own preceding authentication result.
        bool authSuccessRecorded = false;
        uint64_t authSuccessEpoch = 0;
        uint8_t authSuccessBssid[6] = {};
        // JoinAdapter's firmware-active fence survives the earlier AUTH and
        // ASSOC status edges.  It is retired only by the protocol terminal:
        // open RUN or an RSN handshake completion.  Keep that fact separate
        // from whether a later WCL publication itself succeeded.
        bool joinTerminalObserved = false;
        // Open networks have no RSN key-done callback.  Their post-RUN WCL
        // link/connect completion is therefore a separate one-shot owned by
        // this same authenticated and associated candidate.
        bool connectCompletionPublished = false;
        uint16_t apMode = 0;
        uint32_t authLower = 0;
        uint32_t authUpper = 0;
        uint32_t authFlags = 0;
        uint32_t ssidLength = 0;
        uint16_t rsnIeLength = 0;
        uint16_t boundedRsnIeLength = 0;
        uint16_t instantHotspotFlags = 0;
        uint8_t instantHotspotAppleDeviceFlags = 0;
        uint8_t pmfCapabilityField = 0;
        uint32_t bssInfoFlags = 0;
        uint32_t candidateCount = 0;
        uint8_t ssid[33] = {};
        uint8_t selectedBssid[6] = {};
        uint8_t candidateBssid[6] = {};
        uint8_t contextBssid[6] = {};
    } association;

    // Public IOC_ASSOCIATE has a lifecycle independent from WCL's candidate
    // ledger and from external-PMK cache maintenance.  In particular,
    // CLEAR_PMKSA_CACHE may run after an open-network association request and
    // must not revoke the completion that CoreWLAN is still waiting for.
    AssociationOwner publicAssociation;

    bool isDynSarFailSafeMode(uint64_t nowTicks) const
    {
        return TahoeQosDynsarContracts::isDynSarFailSafeMode(
            nowTicks, qosDynsar.dynSarFailSafeStartTicks);
    }

    bool isCongestionControlSupported() const
    {
        return TahoeQosDynsarContracts::congestionControlSupported(
            qosDynsar.congestionControlFeature);
    }

    bool isSlowWifiFeatureEnabled() const
    {
        return qosDynsar.slowWifiFeatureEnabled != 0;
    }

    bool isTxBlankingStatusEnabled() const
    {
        return TahoeQosDynsarContracts::txBlankingStatusEnabled(
            qosDynsar.txBlankingStatus);
    }

    void reset()
    {
        *this = TahoeOwnerRegistry();
    }

    void syncDualPowerMode(int32_t primaryValue, int32_t secondaryValue)
    {
        dualPowerMode.primary = primaryValue;
        dualPowerMode.secondary = secondaryValue;
        txPowerCapBypass.sendEligible =
            (primaryValue != -1) && (secondaryValue != -1);
    }

    void setActionFrameProgress(bool inProgress)
    {
        actionFrame.progress = inProgress ? 1 : 0;
    }

    bool checkActionFrameCompleteOverdue(uint64_t nowMs)
    {
        if ((actionFrame.progress & 1U) == 0)
            return false;
        if (!TahoePayloadBuilders::isActionFrameProgressOverdue(
                nowMs, actionFrame.progressStartMs))
            return false;
        actionFrame.progress = 0;
        return true;
    }

    bool getActionFrameProgress(uint64_t nowMs)
    {
        checkActionFrameCompleteOverdue(nowMs);
        return (actionFrame.progress & 1U) != 0;
    }
};

#endif /* TahoeOwnerRegistry_hpp */
