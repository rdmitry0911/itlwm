//
//  TahoePayloadParity.hpp
//  AirportItlwm
//

#ifndef TahoePayloadParity_hpp
#define TahoePayloadParity_hpp

#include <stdint.h>

namespace TahoePayloadParity {

static constexpr uint32_t kVariablePayloadLength = 0xffffffffU;
static constexpr uint32_t kPayloadContractMinimum = 12;

struct PayloadContract {
    const char *name;
    const char *producer;
    const char *consumer;
    const char *appleReferenceCase;
    uint32_t payloadLength;
    const char *invalidSemantics;
};

inline const PayloadContract *payloadContracts(uint32_t *count)
{
    static const PayloadContract contracts[] = {
        {
            "ie-assoc-vendor",
            "TahoePayloadBuilders::buildIE",
            "AirportItlwmSkywalkInterface::setIE",
            "apple-ie-public-quarantine",
            kVariablePayloadLength,
            "null, zero, or overlength returns raw 0x16; valid carrier returns unsupported without synthetic IE state"
        },
        {
            "offload-ndp-ipv6",
            "TahoePayloadBuilders::buildOffloadNdp",
            "TahoeNdpOwner::apply",
            "apple-io80211-selector-surface",
            kVariablePayloadLength,
            "null or missing NDP owner returns raw 0x16; no synthetic NDP transport is emitted"
        },
        {
            "usb-host-notification",
            "TahoePayloadBuilders::buildUsbHostNotification",
            "AirportItlwmSkywalkInterface::setUSB_HOST_NOTIFICATION",
            "apple-io80211-selector-surface",
            4,
            "null returns 0xe00002bc; change payload is emitted only for change < 2"
        },
        {
            "btcoex-profile",
            "TahoePayloadBuilders::buildBtcoexProfile",
            "AirportItlwmSkywalkInterface::setBTCOEX_PROFILE",
            "apple-io80211-selector-surface",
            0x38,
            "null or invalid band/mode/index returns 0xe00002c2"
        },
        {
            "btcoex-profile-active",
            "TahoePayloadBuilders::buildBtcoexProfileActive",
            "AirportItlwmSkywalkInterface::setBTCOEX_PROFILE_ACTIVE",
            "apple-io80211-selector-surface",
            4,
            "null returns 0xe00002c2; active profile is read from caller +0x4"
        },
        {
            "btcoex-2g-chain-disable",
            "TahoePayloadBuilders::buildBtcoex2GChainDisable",
            "AirportItlwmSkywalkInterface::setBTCOEX_2G_CHAIN_DISABLE",
            "apple-io80211-selector-surface",
            6,
            "null returns 0xe00002c2; payload carries header 0x00060001 and caller +0x4"
        },
        {
            "tx-power-cap-quarantine",
            "no Intel TX-power-cap firmware backend",
            "AirportItlwmSkywalkInterface::setBYPASS_TX_POWER_CAP / setDUAL_POWER_MODE",
            "apple-tx-power-cap-firmware-owner",
            kVariablePayloadLength,
            "null returns 0xe00002bc; non-null input fails closed before synthetic completion"
        },
        {
            "wcl-action-frame-v1-v2",
            "TahoePayloadBuilders::buildActionFrame",
            "TahoeCommanderV2::runSetWCLActionFrame",
            "apple-wcl-action-frame-send",
            kVariablePayloadLength,
            "null or frameLen > 0x707 returns 0xe00002bc; V2 begins at firmware generation 0x15"
        },
        {
            "action-frame-progress",
            "TahoeOwnerRegistry::getActionFrameProgress",
            "scan/action-frame progress gate",
            "apple-action-frame-progress",
            1,
            "overdue progress clears after 0x12d ms; scan reject status is 0xe00002d5"
        },
        {
            "ranging-authenticate",
            "TahoePayloadBuilders::buildRangingAuthenticate",
            "AirportItlwmSkywalkInterface::setRANGING_AUTHENTICATE",
            "apple-ranging-authenticate-thunk",
            kVariablePayloadLength,
            "null or zero PMK length returns 0xe0000001; role 4 posts callback only with a proximity owner"
        },
        {
            "association-candidates-hidden",
            "AirportItlwmSkywalkInterface::setWCL_ASSOCIATE",
            "AirportItlwmSkywalkInterface::getAWDL_PEER_TRAFFIC_STATS",
            "apple-hidden-association-rsn",
            0x3ad8,
            "hidden carrier is accepted only at length 0x3ad8; null direct WCL associate returns 0xe00002c2"
        },
        {
            "bss-blacklist-async-owner",
            "AirportItlwm::setBssBlacklistOwner/queryBssBlacklistOwner",
            "IO80211Controller::postMessage 0xa3",
            "apple-bss-blacklist-async-owner",
            kVariablePayloadLength,
            "SET null returns 0xe00002bc; count >= 8 preserves applied state; GET never writes its caller buffer; empty applied list emits no event"
        },
        {
            "txrx-chain-info",
            "ItlDriverInfo::getTxChainMask/getRxChainMask",
            "AirportItlwmSkywalkInterface::getTXRX_CHAIN_INFO",
            "apple-txrx-chain-info",
            4,
            "null returns 0xe00002c2; configured RX/TX masks preserve independent byte order"
        },
        {
            "link-changed-32",
            "AirportItlwmSkywalkInterface::setLinkStateInternal",
            "AirportItlwmSkywalkInterface::getLINK_CHANGED_EVENT_DATA",
            "apple-event-three-abis",
            0x20,
            "null getter returns raw 16; publication is gated on parent link-state success; SNR/NF publish only with valid HAL noise"
        },
        {
            "bssid-changed-24",
            "AirportItlwmSkywalkInterface::publishTahoeBssidChangedFromCurrentBss",
            "IO80211Glue pending event queue",
            "apple-event-three-abis",
            0x18,
            "zero BSSID is rejected; same-BSS reason 1 suppresses duplicate publication"
        },
        {
            "wcl-link-state-16",
            "postTahoeWclLinkUpInd",
            "WCL event 0xd8 consumer",
            "apple-event-three-abis",
            0x10,
            "invalid link reason maps to 0xff while preserving BSSID/link-state fields"
        },
        {
            "wcl-auth-assoc-complete",
            "buildTahoeWclAuthAssocCompletePayload",
            "WCLJoinManager association/auth-complete path",
            "apple-wcl-auth-assoc-complete",
            0x08,
            "successful local STA_ASSOC_DONE maps firmware status/reason to two zero dwords before 0x4e publication"
        },
        {
            "wcl-scan-result",
            "buildTahoeWclScanResultPayload",
            "AirportItlwm::postWclScanDoneGated",
            "apple-wcl-scan-result",
            kVariablePayloadLength,
            "null controller/node/channel and zero BSSID reject before postMessage; metadata header is 0x44; flags are 0x2"
        },
        {
            "wcl-connect-complete",
            "buildTahoeWclConnectCompletePayload",
            "postTahoeWclConnectCompleteEvent",
            "apple-connect-complete",
            0xa4,
            "missing controller/HAL/RUN/BSS rejects before 0xd5 postMessage"
        },
        {
            "driver-available-0xf8",
            "postTahoeDriverAvailabilityTransition",
            "WCLSystemStateManager driverAvailableEventHandler",
            "apple-driver-available",
            0xf8,
            "boot-ready, power-off, and power-on keep distinct flags/reason dwords; available remains at +0x08"
        }
    };

    static_assert(sizeof(contracts) / sizeof(contracts[0]) >= kPayloadContractMinimum,
                  "automation payload parity requires at least twelve payload shapes");

    if (count != nullptr)
        *count = static_cast<uint32_t>(sizeof(contracts) / sizeof(contracts[0]));
    return contracts;
}

} // namespace TahoePayloadParity

#endif /* TahoePayloadParity_hpp */
