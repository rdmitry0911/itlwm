#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <type_traits>

#include "AirportItlwm/AirportItlwmAPSTAInterface.hpp"
#include "AirportItlwm/TahoeAssociationContracts.hpp"
#include "AirportItlwm/TahoeCapabilityContracts.hpp"
#include "AirportItlwm/TahoeLeScanContracts.hpp"
#include "AirportItlwm/TahoeLqmContracts.hpp"
#include "AirportItlwm/TahoeMimoContracts.hpp"
#include "AirportItlwm/TahoeNrateContracts.hpp"
#include "AirportItlwm/TahoeOpModeContracts.hpp"
#include "AirportItlwm/TahoePayloadBuilders.hpp"
#include "AirportItlwm/TahoeQosDynsarContracts.hpp"
#include "AirportItlwm/TahoeSkywalkIoctlRoutes.hpp"
#include "include/Airport/IO80211BssManager.h"

namespace {

void require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "payload builder test failed: " << message << '\n';
        std::exit(1);
    }
}

void put16(uint8_t *data, uint32_t offset, uint16_t value)
{
    std::memcpy(data + offset, &value, sizeof(value));
}

void put32(uint8_t *data, uint32_t offset, uint32_t value)
{
    std::memcpy(data + offset, &value, sizeof(value));
}

template <typename T>
uint8_t *raw(T &value)
{
    return reinterpret_cast<uint8_t *>(&value);
}

void testIEBuilder()
{
    apple80211_ie_data ie{};
    TahoePayloadBuilders::IEPayloads payload{};

    ie.frame_type_flags = 4;
    ie.add = 1;
    ie.ie_len = 3;
    ie.ie[0] = 0x44;
    ie.ie[1] = 0xaa;
    ie.ie[2] = 0xbb;

    require(TahoePayloadBuilders::buildIE(&ie, &payload), "IE builder accepts custom-assoc IE");
    require(payload.customAssoc, "IE builder marks 0x44 frame-type 4 add=1 as custom assoc");
    require(payload.ieLen == 3, "IE builder preserves IE length");
    require(payload.ie[1] == 0xaa && payload.ie[2] == 0xbb, "IE builder copies IE bytes");

    ie.ie_len = sizeof(ie.ie) + 1;
    require(!TahoePayloadBuilders::buildIE(&ie, &payload), "IE builder rejects overlength IE");
    require(!TahoePayloadBuilders::buildIE(nullptr, &payload), "IE builder rejects null input");
}

void testNdpBuilder()
{
    apple80211_offload_ndp_data ndp{};
    TahoePayloadBuilders::NdpPayload payload{};
    uint8_t *data = raw(ndp);

    put32(data, 4, 6);
    for (uint32_t i = 0; i < 4 * 16; i++)
        data[8 + i] = static_cast<uint8_t>(0x10 + i);

    require(TahoePayloadBuilders::buildOffloadNdp(&ndp, &payload), "NDP builder accepts carrier");
    require(payload.count == 4, "NDP builder clamps address count to four");
    require(payload.addresses[3][15] == static_cast<uint8_t>(0x10 + 63), "NDP builder copies clamped address bytes");
    require(payload.linkLocalSeed[0] == 0xfe && payload.linkLocalSeed[1] == 0x80,
            "NDP builder seeds link-local prefix");
    require(std::memcmp(&payload.linkLocalSeed[8], &payload.addresses[0][8], 8) == 0,
            "NDP builder derives link-local tail from first IPv6 address");
    require(!TahoePayloadBuilders::buildOffloadNdp(nullptr, &payload), "NDP builder rejects null input");
}

void testUsbHostBuilder()
{
    apple80211_usb_host_notification_data usb{};
    TahoePayloadBuilders::UsbHostNotificationPayloads payload{};
    uint8_t *data = raw(usb);

    put32(data, 0x8, 1);
    put32(data, 0xc, 0xabcdef01);
    require(TahoePayloadBuilders::buildUsbHostNotification(&usb, &payload),
            "USB host builder accepts carrier");
    require(payload.change == 1 && payload.present == 0xabcdef01,
            "USB host builder extracts change and present dwords");
    require(payload.hasChangePayload, "USB host builder emits change payload for change < 2");

    put32(data, 0x8, 2);
    require(TahoePayloadBuilders::buildUsbHostNotification(&usb, &payload),
            "USB host builder accepts second carrier");
    require(!payload.hasChangePayload, "USB host builder suppresses change payload for change >= 2");
}

void testBtcoexBuilders()
{
    apple80211_btcoex_profile profile{};
    TahoePayloadBuilders::BtcoexProfilePayload profilePayload{};
    uint8_t *profileRaw = raw(profile);

    put16(profileRaw, 0, 3);
    profileRaw[3] = 4;
    profileRaw[4] = 9;
    profileRaw[0x37] = 0x5a;
    require(TahoePayloadBuilders::buildBtcoexProfile(&profile, &profilePayload),
            "BTCOEX profile builder accepts profile");
    require(profilePayload.mode == 3 && profilePayload.band == 4 && profilePayload.profileIndex == 9,
            "BTCOEX profile builder extracts mode, band, and index");
    require(profilePayload.profileEntry[0x37] == 0x5a, "BTCOEX profile builder copies 0x38-byte entry");

    apple80211_btcoex_profile_active_data active{};
    TahoePayloadBuilders::BtcoexProfileActivePayload activePayload{};
    put32(raw(active), 4, 7);
    require(TahoePayloadBuilders::buildBtcoexProfileActive(&active, &activePayload),
            "BTCOEX active-profile builder accepts carrier");
    require(activePayload.activeProfile == 7, "BTCOEX active-profile builder reads caller +0x4");

    apple80211_btcoex_2g_chain_disable chain{};
    TahoePayloadBuilders::Btcoex2GChainDisablePayload chainPayload{};
    put16(raw(chain), 4, 0x1234);
    require(sizeof(chainPayload) == 6, "BTCOEX 2G chain-disable payload is six bytes");
    require(TahoePayloadBuilders::buildBtcoex2GChainDisable(&chain, &chainPayload),
            "BTCOEX 2G chain-disable builder accepts carrier");
    require(chainPayload.header == 0x00060001, "BTCOEX 2G chain-disable builder keeps fixed header");
    require(chainPayload.chainDisable == 0x1234, "BTCOEX 2G chain-disable builder reads caller +0x4");
}

void testTxPowerAndActionFrameBuilders()
{
    apple80211_bypass_tx_power_cap txPower{};
    TahoePayloadBuilders::TxPowerCapBypassPayload txPayload{};
    raw(txPower)[0] = 3;
    require(TahoePayloadBuilders::buildTxPowerCapBypass(&txPower, &txPayload),
            "TX power-cap bypass builder accepts carrier");
    require(txPayload.enabled == 1, "TX power-cap bypass builder keeps low bit only");

    apple80211_wcl_action_frame action{};
    TahoePayloadBuilders::ActionFramePayload actionPayload{};
    uint8_t *data = raw(action);
    data[0] = 0x7f;
    put32(data, 4, 149);
    put16(data, 0xe, 4);
    data[0x10] = 0xde;
    data[0x11] = 0xad;
    data[0x12] = 0xbe;
    data[0x13] = 0xef;

    require(TahoePayloadBuilders::buildActionFrame(
                &action, TahoePayloadBuilders::kActionFrameV2FirmwareThreshold - 1, &actionPayload),
            "action-frame builder accepts V1 carrier");
    require(!actionPayload.useV2, "action-frame builder selects V1 below threshold");
    require(actionPayload.category == 0x7f && actionPayload.channel == 149 && actionPayload.frameLen == 4,
            "action-frame builder extracts category, channel, and frame length");
    require(actionPayload.frame[0] == 0xde && actionPayload.frame[3] == 0xef,
            "action-frame builder copies frame body");

    require(TahoePayloadBuilders::buildActionFrame(
                &action, TahoePayloadBuilders::kActionFrameV2FirmwareThreshold, &actionPayload),
            "action-frame builder accepts V2 carrier");
    require(actionPayload.useV2, "action-frame builder selects V2 at threshold");
    require(TahoePayloadBuilders::actionFrameV2CommandSize(actionPayload.frameLen) == 0x38,
            "action-frame V2 command size includes the 0x34-byte Apple header");

    put16(data, 0xe, TahoePayloadBuilders::kActionFramePayloadCapacity);
    require(!TahoePayloadBuilders::buildActionFrame(
                &action, TahoePayloadBuilders::kActionFrameV2FirmwareThreshold, &actionPayload),
            "action-frame builder rejects frameLen > 0x707");
}

void testRangingBuilder()
{
    apple80211_ranging_authenticate_request_t ranging{};
    TahoePayloadBuilders::RangingAuthenticatePayload payload{};
    uint8_t *data = raw(ranging);

    put32(data, 4, 4);
    put16(data, 0x70, 0);
    require(!TahoePayloadBuilders::buildRangingAuthenticate(&ranging, 1, &payload),
            "ranging builder rejects zero PMK length");

    put16(data, 0x70, 32);
    require(TahoePayloadBuilders::buildRangingAuthenticate(&ranging, 1, &payload),
            "ranging builder accepts nonzero PMK length");
    require(payload.role == 4 && payload.pmkLen == 32, "ranging builder extracts role and PMK length");
    require(payload.shouldPostCallback, "ranging builder posts role-4 callback with proximity owner");

    require(TahoePayloadBuilders::buildRangingAuthenticate(&ranging, 0xff, &payload),
            "ranging builder accepts proximity sentinel owner");
    require(!payload.shouldPostCallback, "ranging builder suppresses callback for proximity sentinel owner");
}

void testApstaPublicContracts()
{
    require(offsetof(AirportItlwmAPSTAHostApModeHiddenOutputLayout, hidden00) == 0,
            "APSTA hidden getter writes value at output +0x00");
    require(kAirportItlwmAPSTAGetHostApModeHiddenValue == 1,
            "APSTA hidden getter writes fixed value 1");
    require(kAirportItlwmAPSTAGetHostApModeHiddenInvalidArgumentReturn == 0x16,
            "APSTA hidden getter null return is raw 0x16");
    require(offsetof(AirportItlwmAPSTAHostApModeHiddenLayout, hidden04) == 4,
            "APSTA hidden setter reads value at input +0x04");
    require(sizeof(AirportItlwmAPSTAHostApModeHiddenLayout) == 8,
            "APSTA hidden setter carrier includes version dword");
    require(kAirportItlwmAPSTASetSsidStateMutationCount == 0,
            "APSTA setSSID does not mutate recovered SSID state");
    require(kAirportItlwmAPSTASetMisMaxStaReturn == 0,
            "APSTA MIS_MAX_STA AP-down/no-owner path returns success");
    require(kAirportItlwmAPSTASetSoftAPExtCapsReturn == 0,
            "APSTA SoftAP ext-cap no-owner path returns success");
    require(kAirportItlwmAPSTASetSoftAPExtCapsStateClear61Offset == 0x61,
            "APSTA SoftAP ext-cap setter clears state byte +0x61");
    require(kAirportItlwmAPSTAHiddenNotUpReturn == 6,
            "APSTA hidden AP-down return is raw 6");
    require(kAirportItlwmAPSTAHiddenInvalidArgumentReturn == 0x16,
            "APSTA hidden invalid return is raw 0x16");
    require(kAirportItlwmAPSTAHiddenClearPowerSaveState ==
            kAirportItlwmAPSTAPowerStateOff,
            "APSTA hidden clear tail clears power-save state");
    require(kAirportItlwmAPSTAHiddenClearPowerSaveReason == 9,
            "APSTA hidden clear tail uses reason 9");
    require(kAirportItlwmAPSTACsaNotUpReturn == 6,
            "APSTA CSA AP-down return is raw 6");
    require(kAirportItlwmAPSTACsaInvalidArgumentReturn == 0x16,
            "APSTA CSA invalid return is raw 0x16");
    require(kAirportItlwmAPSTASetChannelLocalCsaTriggerCount == 0,
            "APSTA setCHANNEL does not synthesize a CSA trigger");
    require(kAirportItlwmAPSTASetChannelNoOwnerRoutesPrimary == 1,
            "APSTA setCHANNEL no-owner route preserves primary channel setter");
    require(kAirportItlwmAPSTACsaMinimumPrimaryChannel == 1,
            "APSTA CSA helper rejects primary channel zero");
    require(kAirportItlwmAPSTACsaMaximumExcludedPrimaryChannel == 0x100,
            "APSTA CSA helper accepts primary channels 1..255");
    require(kAirportItlwmAPSTASetMaxAssocNoLocalClamp == 1,
            "APSTA setMaxAssoc uses cap gate instead of local clamp");
    require(kAirportItlwmAPSTASetMaxAssocPayloadAddsAssociatedCount == 1,
            "APSTA setMaxAssoc payload includes associated station count");
    require(kAirportItlwmAPSTAWifiNetworkInfoLengthTrapThreshold == 0x21,
            "APSTA Wi-Fi network info length trap threshold is 0x21");
    require(kAirportItlwmAPSTAWifiNetworkInfoMaxAcceptedLength == 0x20,
            "APSTA Wi-Fi network info accepts lengths below 0x21");
    require(kAirportItlwmAPSTAWifiNetworkInfoFeatureGate46 == 0x46,
            "APSTA Wi-Fi network info is gated by feature 0x46");
    require(kAirportItlwmAPSTAWifiNetworkInfoLocalFeatureGate46Enabled == 0,
            "APSTA Wi-Fi network info feature gate is unavailable locally");
    require(kAirportItlwmAPSTAWifiNetworkInfoFeatureDisabledCopyCount == 0,
            "APSTA Wi-Fi network info disabled-feature path does not copy state");
    require(kAirportItlwmAPSTASetCipherKeyNotUpReturn == 6,
            "APSTA CIPHER_KEY AP-down return is raw 6");
    require(kAirportItlwmAPSTASetCipherKeyCipherNone == 0,
            "APSTA CIPHER_KEY cipher-none carrier value is zero");
    require(kAirportItlwmAPSTASetCipherKeyUnsupportedCipherReturn == 0,
            "APSTA CIPHER_KEY cipher-none and unsupported cipher return success after AP-up gate");
    require(kAirportItlwmAPSTAStaAuthorizePreAPUpTableMutationCount == 0,
            "APSTA STA_AUTHORIZE does not mutate station table before AP-up");
    require(kAirportItlwmAPSTAStaDeauthTailcallVtableOffset == 0x1040,
            "APSTA STA_DEAUTH tailcalls STA_DISASSOCIATE vtable slot");
    require(kAirportItlwmAPSTAStaDisassocVirtualIoctlSelector == 0xc9,
            "APSTA STA_DEAUTH and STA_DISASSOCIATE share the recovered selector");
    require(offsetof(AirportItlwmAPSTASoftAPParamsOutputLayout, param14) == 0x14,
            "APSTA SoftAP params getter writes beacon interval at output +0x14");
    require(offsetof(AirportItlwmAPSTASoftAPParamsOutputLayout, mode16) == 0x16,
            "APSTA SoftAP params getter writes mode at output +0x16");
    require(offsetof(AirportItlwmAPSTASoftAPParamsOutputLayout, enabled17) == 0x17,
            "APSTA SoftAP params getter writes enabled bit at output +0x17");
    require(offsetof(AirportItlwmAPSTASoftAPParamsOutputLayout, param18) == 0x18,
            "APSTA SoftAP params getter writes byte field at output +0x18");
    require(kAirportItlwmAPSTASetSoftAPParamsClearPowerState ==
            kAirportItlwmAPSTAPowerStateOff,
            "APSTA SoftAP params disable path clears power-save state");
    require(kAirportItlwmAPSTASetSoftAPParamsHoldPowerState ==
            kAirportItlwmAPSTAPowerStateOn,
            "APSTA SoftAP params hold path enters power-save state 1");
    require(kAirportItlwmAPSTASetSoftAPParamsHoldPowerReason == 0,
            "APSTA SoftAP params hold path uses reason 0");
    require(kAirportItlwmAPSTAResetPowerSaveState ==
            kAirportItlwmAPSTAPowerStateOff,
            "APSTA reset drives power-save state off");
    require(kAirportItlwmAPSTAResetPowerSaveReason ==
            kAirportItlwmAPSTAPowerStateReasonReset,
            "APSTA reset uses the recovered power-save reset reason");
    require(kAirportItlwmAPSTAInitSoftAPDefaultDtimPeriod == 1,
            "APSTA initSoftAPParameters default DTIM is 1");
    require(kAirportItlwmAPSTAInitSoftAPDefaultParam18 == 0x0f,
            "APSTA initSoftAPParameters state +0x18 default is 0x0f");
    require(kAirportItlwmAPSTAInitSoftAPDefaultParam1c == 0x1e,
            "APSTA initSoftAPParameters state +0x1c default is 0x1e");
    require(kAirportItlwmAPSTAInitSoftAPDefaultParam20 == 0x708,
            "APSTA initSoftAPParameters state +0x20 default is 0x708");
    require(kAirportItlwmAPSTAInitSoftAPDefaultParam24 == 0x0a,
            "APSTA initSoftAPParameters state +0x24 default is 0x0a");
    require(kAirportItlwmAPSTAInitSoftAPDefaultParam28 == 3,
            "APSTA initSoftAPParameters state +0x28 default is 3");
    require(offsetof(AirportItlwmAPSTASsidDataLayout, length04) ==
                kAirportItlwmAPSTAGetSsidOutputLengthOffset,
            "APSTA getSSID writes length at output +0x04");
    require(offsetof(AirportItlwmAPSTASsidDataLayout, ssid08) ==
                kAirportItlwmAPSTAGetSsidOutputBytesOffset,
            "APSTA getSSID writes SSID bytes at output +0x08");
    require(kAirportItlwmAPSTAGetSsidInvalidArgumentReturn == 0x16,
            "APSTA getSSID rejects oversized cached SSIDs with raw 0x16");
    require(offsetof(AirportItlwmAPSTAStateDataLayout, state04) ==
                kAirportItlwmAPSTAGetStateOutputValueOffset,
            "APSTA getSTATE writes state at output +0x04");
    require(kAirportItlwmAPSTAGetStateOutputValue == 4,
            "APSTA getSTATE publishes SoftAP state 4");
    require(offsetof(AirportItlwmAPSTAOpModeDataLayout, type00) ==
                kAirportItlwmAPSTAGetOpModeOutputTypeOffset,
            "APSTA getOP_MODE writes type at output +0x00");
    require(offsetof(AirportItlwmAPSTAOpModeDataLayout, mode04) ==
                kAirportItlwmAPSTAGetOpModeOutputModeOffset,
            "APSTA getOP_MODE writes mode at output +0x04");
    require(kAirportItlwmAPSTAGetOpModeTypeValue == 1 &&
                kAirportItlwmAPSTAGetOpModeAPUpValue == 8 &&
                kAirportItlwmAPSTAGetOpModeAPDownValue == 0,
            "APSTA getOP_MODE publishes type 1 and AP-up mode 8");
    require(offsetof(AirportItlwmAPSTAPeerCacheMaximumSizeLayout, maximum04) ==
                kAirportItlwmAPSTAGetPeerCacheMaximumSizeOutputOffset,
            "APSTA peer-cache maximum getter writes value at output +0x04");
    require(kAirportItlwmAPSTAGetPeerCacheMaximumSizeValue == 8,
            "APSTA peer-cache maximum size is 8");
    require(kAirportItlwmAPSTAGetSoftAPStatsCopySize == 0x58,
            "APSTA SoftAP stats getter copies 0x58 bytes");
    require(offsetof(AirportItlwmAPSTAPeerCacheControlLayout, command04) == 0x04,
            "APSTA peer-cache control reads command at input +0x04");
    require(offsetof(AirportItlwmAPSTAPeerCacheControlLayout, value08) == 0x08,
            "APSTA peer-cache control reads dword at input +0x08");
    require(offsetof(AirportItlwmAPSTAPeerCacheControlLayout, value0c) == 0x0c,
            "APSTA peer-cache control reads word at input +0x0c");
    require(offsetof(AirportItlwmAPSTAPeerCacheControlLayout, value0e) == 0x0e,
            "APSTA peer-cache control reads word at input +0x0e");
    require(kAirportItlwmAPSTASetPeerCacheControlEventId == 0x33,
            "APSTA peer-cache control helper posts event 0x33");
    require(kAirportItlwmAPSTASetPeerCacheControlLocalEventPostCount == 0,
            "APSTA peer-cache control does not synthesize local event posts");
    require(kAirportItlwmAPSTASetPeerCacheControlReturn == 0,
            "APSTA peer-cache control returns success");
    require(kAirportItlwmAPSTAGetStationListNullReturn == 0x16,
            "APSTA getSTATION_LIST null return is raw 0x16");
    require(kAirportItlwmAPSTAGetStationListNotUpReturn == 0x39,
            "APSTA getSTATION_LIST AP-down return is 0x39");
    require(kAirportItlwmAPSTAGetStaIEListNotFoundReturn == 2,
            "APSTA getSTA_IE_LIST missing station return is 2");
    require(kAirportItlwmAPSTAGetStaStatsNotUpReturn == 0x39,
            "APSTA getSTA_STATS AP-down return is 0x39");
    require(kAirportItlwmAPSTAGetKeyRscOutputLengthValue == 8,
            "APSTA getKEY_RSC writes an 8-byte RSC length");
}

void testPayloadContractInventory()
{
    uint32_t count = 0;
    const TahoePayloadParity::PayloadContract *contracts =
        TahoePayloadParity::payloadContracts(&count);
    require(contracts != nullptr, "payload contract inventory is addressable");
    require(count >= TahoePayloadParity::kPayloadContractMinimum,
            "payload contract inventory meets minimum shape count");

    const char *required[] = {
        "ie-assoc-vendor",
        "offload-ndp-ipv6",
        "usb-host-notification",
        "btcoex-profile",
        "btcoex-profile-active",
        "btcoex-2g-chain-disable",
        "tx-power-cap-bypass",
        "wcl-action-frame-v1-v2",
        "action-frame-progress",
        "ranging-authenticate",
        "association-candidates-hidden",
        "link-changed-32",
        "bssid-changed-24",
        "wcl-link-state-16",
        "wcl-scan-result",
        "wcl-connect-complete",
        "driver-available-0xf8",
    };

    for (const char *name : required) {
        bool found = false;
        for (uint32_t i = 0; i < count; i++) {
            if (std::strcmp(contracts[i].name, name) == 0) {
                found = contracts[i].producer != nullptr &&
                        contracts[i].consumer != nullptr &&
                        contracts[i].invalidSemantics != nullptr;
                break;
            }
        }
        require(found, name);
    }
}

void testTahoeSkywalkIoctlRoutes()
{
    using namespace TahoeSkywalkIoctlRoutes;

    require(shouldRoute(kIocChipCounterStats, false) &&
                !shouldRoute(kIocChipCounterStats, true),
            "Skywalk routes CHIP_COUNTER_STATS getter to fixed Apple fail contract");
    require(shouldRoute(kIocBgscanCacheResults, false) &&
                !shouldRoute(kIocBgscanCacheResults, true),
            "Skywalk routes BGSCAN_CACHE_RESULTS getter to cached network producer");
    require(shouldRoute(kIocDbgGuardTimeParams, false) &&
                shouldRoute(kIocDbgGuardTimeParams, true),
            "Skywalk routes DBG_GUARD_TIME_PARAMS getter/setter pair");
    require(shouldRoute(kIocAwdlRsdbCaps, false) &&
                !shouldRoute(kIocAwdlRsdbCaps, true),
            "Skywalk routes AWDL_RSDB_CAPS getter only");
    require(shouldRoute(kIocTkoParams, false) &&
                shouldRoute(kIocTkoParams, true),
            "Skywalk routes TKO_PARAMS getter/setter pair");
    require(shouldRoute(kIocTkoDump, false) &&
                !shouldRoute(kIocTkoDump, true),
            "Skywalk routes TKO_DUMP getter only");
    require(shouldRoute(kIocBtcoexProfile, false) &&
                shouldRoute(kIocBtcoexProfile, true),
            "Skywalk routes BTCOEX_PROFILE getter/setter pair");
    require(shouldRoute(kIocBtcoexProfileActive, false) &&
                shouldRoute(kIocBtcoexProfileActive, true),
            "Skywalk routes BTCOEX_PROFILE_ACTIVE getter/setter pair");
    require(shouldRoute(kIocMaxNssForAp, false) &&
                !shouldRoute(kIocMaxNssForAp, true),
            "Skywalk routes MAX_NSS_FOR_AP getter only");
    require(shouldRoute(kIocBtcoex2GChainDisable, false) &&
                shouldRoute(kIocBtcoex2GChainDisable, true),
            "Skywalk routes BTCOEX_2G_CHAIN_DISABLE getter/setter pair");
    require(!shouldRoute(kIocBtCoexFlags, false) &&
                shouldRoute(kIocBtCoexFlags, true),
            "Skywalk routes BT_COEX_FLAGS setter fixed-fail only");
    require(!shouldRoute(kIocBtPower, false) &&
                shouldRoute(kIocBtPower, true),
            "Skywalk routes BT_POWER setter fixed-fail only");
    require(shouldRoute(kIocPeerCacheMaximumSize, false) &&
                !shouldRoute(kIocPeerCacheMaximumSize, true),
            "Skywalk routes APSTA peer-cache maximum getter");
    require(!shouldRoute(kIocPeerCacheControl, false) &&
                shouldRoute(kIocPeerCacheControl, true),
            "Skywalk routes APSTA peer-cache control setter only");
}

void testTahoeAssociationContracts()
{
    using namespace TahoeAssociationContracts;

    require(kHiddenAssociateSelector == 0x45,
            "hidden associate selector is 0x45");
    require(kHiddenAssociateCompleteSelector == 0x46,
            "hidden associate complete selector is 0x46");
    require(kAssocCandidatesPayloadLength == 0x3ad8,
            "hidden associate carrier length is 0x3ad8");
    require(kPublicProtmodeUnsupportedStatus == 0xe00002c7,
            "public PROTMODE getter is an Apple unsupported shim");
    require(kPublicRsnIeGateSelector == 0x29 &&
                kPublicRsnIeVendorVirtualOffset == 0x398,
            "public RSN_IE getter gates then calls vendor virtual +0x398");
    require(kPublicRsnIeNoOwnerStatus == 0xe082280e,
            "public RSN_IE getter no-owner failure is 0xe082280e");
    require(kPublicSetRsnIeReturn == 0 &&
                kPublicSetRsnIeMutationCount == 0,
            "public setRSN_IE is a success no-op");
    require(boundedRsnIeLength(7, 11) == 7,
            "RSN IE helper keeps in-bounds length");
    require(boundedRsnIeLength(17, 11) == 11,
            "RSN IE helper clamps overlength copy");
}

void testTahoeQosDynsarContracts()
{
    using namespace TahoeQosDynsarContracts;

    require(kCongestionControlFeatureOffset == 0x7584,
            "QoS/DynSAR congestion feature byte is core-private +0x7584");
    require(kCongestionControlFeatureBit == 0x01,
            "QoS/DynSAR congestion feature uses bit 0");
    require(kUnsupportedStatus == 0xe00002c7,
            "QoS/DynSAR unsupported gate returns Apple status 0xe00002c7");
    require(!congestionControlSupported(0),
            "QoS/DynSAR congestion gate rejects a clear feature byte");
    require(congestionControlSupported(kCongestionControlFeatureBit),
            "QoS/DynSAR congestion gate accepts bit 0");
    require(congestionControlSupported(0xff),
            "QoS/DynSAR congestion gate ignores unrelated high bits");
    require(isDynSarFailSafeMode(0x400, 0),
            "DynSAR fail-safe window accepts elapsed ticks below threshold");
    require(!isDynSarFailSafeMode((kDynSarFailSafeWindow << kDynSarFailSafeShift), 0),
            "DynSAR fail-safe window rejects threshold elapsed ticks");
}

void testTahoeOpModeContracts()
{
    using namespace TahoeOpModeContracts;

    struct OpModeProbe {
        uint32_t version;
        uint32_t op_mode;
    } probe{};

    require(kInvalidArgumentStatus == 0x16,
            "primary OP_MODE null return is raw 0x16");
    require(kPrimaryVersion == 1,
            "primary OP_MODE writes version 1");
    require(kPrimaryInitialMode == 0,
            "primary OP_MODE starts with no mode bits");
    require(kPrimaryMonitorBit == 0x10,
            "primary OP_MODE monitor bit is the recovered late OR bit");
    require(kPrimaryStaBitMutationCount == 0,
            "primary OP_MODE does not synthesize STA bit");
    require(!initializePrimaryCarrier<OpModeProbe>(nullptr),
            "primary OP_MODE helper rejects null output");
    require(initializePrimaryCarrier(&probe),
            "primary OP_MODE helper accepts output carrier");
    require(probe.version == 1 && probe.op_mode == 0,
            "primary OP_MODE helper initializes version and zero mode");
}

void testTahoeNrateContracts()
{
    using namespace TahoeNrateContracts;

    struct McsVhtProbe {
        uint32_t index;
        uint32_t nss;
        uint32_t bw;
        uint32_t guard_interval;
    } probe{};

    require(kConfigNoValueStatus == 0xe00002e3,
            "nrate no-value status is Apple 0xe00002e3");
    require(isAcceptedQueryStatus(0),
            "nrate success status is accepted for decode");
    require(isAcceptedQueryStatus(kConfigNoValueStatus),
            "nrate no-value status is accepted for zero-output decode");
    require(!isAcceptedQueryStatus(0xe00002c7),
            "nrate unsupported status is returned without decode");

    uint32_t index = 0;
    require(decodeMcsIndexFromNrate(kFamilyLegacy | 0x5a, &index) && index == 0x5a,
            "nrate legacy family publishes low byte MCS index");
    require(decodeMcsIndexFromNrate(kFamilyHt | 0x1d, &index) && index == 0x0d,
            "nrate HT family publishes low nibble MCS index");
    require(!decodeMcsIndexFromNrate(0, &index),
            "nrate unknown family does not publish MCS index");

    require(fillMcsVhtFromNrate(kFamilyVht | 0x00030000 | kShortGuardIntervalBit | 0x25,
                                &probe),
            "nrate VHT family populates MCS_VHT carrier");
    require(probe.index == 5 && probe.nss == 2 && probe.bw == 80 &&
                probe.guard_interval == kGuardIntervalShort,
            "nrate VHT carrier decodes index, NSS, bandwidth, and guard interval");

    McsVhtProbe nonVht{};
    require(!fillMcsVhtFromNrate(kFamilyHt | 0x00010000 | 0x03, &nonVht),
            "nrate non-VHT family leaves MCS_VHT carrier untouched");
    require(nonVht.index == 0 && nonVht.nss == 0 && nonVht.bw == 0 &&
                nonVht.guard_interval == 0,
            "nrate non-VHT carrier remains zeroed");

    uint32_t interval = 0;
    require(decodeGuardIntervalFromNrate(kFamilyVht, &interval) &&
                interval == kGuardIntervalLong,
            "guard interval VHT family defaults to long GI without bit 23");
    require(decodeGuardIntervalFromNrate(kFamilyVht | kShortGuardIntervalBit,
                                         &interval) &&
                interval == kGuardIntervalShort,
            "guard interval VHT family uses bit 23 for short GI");
    require(decodeGuardIntervalFromNrate(kFamilyHt | (0U << kHtGuardIntervalSelectorShift),
                                         &interval) &&
                interval == kGuardIntervalLong,
            "guard interval HT selector 0 maps to 800 ns");
    require(decodeGuardIntervalFromNrate(kFamilyHt | (1U << kHtGuardIntervalSelectorShift),
                                         &interval) &&
                interval == kGuardIntervalLong,
            "guard interval HT selector 1 maps to 800 ns");
    require(decodeGuardIntervalFromNrate(kFamilyHt | (2U << kHtGuardIntervalSelectorShift),
                                         &interval) &&
                interval == kGuardIntervalHtWide,
            "guard interval HT selector 2 maps to 1600 ns");
    require(decodeGuardIntervalFromNrate(kFamilyHt | (3U << kHtGuardIntervalSelectorShift),
                                         &interval) &&
                interval == kGuardIntervalHtUltraWide,
            "guard interval HT selector 3 maps to 3200 ns");
    require(decodeGuardIntervalFromNrate(0, &interval) &&
                interval == kGuardIntervalLong,
            "guard interval unknown accepted nrate falls back to 800 ns");

    uint32_t nrate = 0;
    uint32_t mbps = 0;
    require(normalizeIwmRateNFlagsToAppleNrate(
                kIwmRateMcsVhtMask | (1U << 4) | (2U << 11) |
                    kIwmRateMcsSgiMask | 0x05,
                &nrate),
            "iwm VHT raw rate normalizes to Apple nrate");
    require(nrate == (kFamilyVht | kBandwidth80 |
                      kShortGuardIntervalBit | (2U << 4) | 0x05),
            "iwm VHT nrate carries family, NSS, bandwidth, SGI, and MCS");
    require(decodeRateMbpsFromNrate(nrate, &mbps) && mbps == 520,
            "Apple nrate VHT rate decodes to integer Mbps");

    require(normalizeIwxRateNFlagsToAppleNrate(
                kIwxRateModVht | (1U << 4) | (2U << 11) |
                    kIwxRateMcsSgiMask | 0x05,
                &nrate),
            "iwx VHT raw rate normalizes to Apple nrate");
    require(nrate == (kFamilyVht | kBandwidth80 |
                      kShortGuardIntervalBit | (2U << 4) | 0x05),
            "iwx VHT nrate matches the Apple VHT carrier");

    require(normalizeIwmRateNFlagsToAppleNrate(
                kIwmRateMcsHtMask | (1U << 11) | 0x0b, &nrate),
            "iwm HT raw rate normalizes to Apple nrate");
    require(nrate == (kFamilyHt | kBandwidth40 | 0x0b),
            "iwm HT nrate carries family, bandwidth, and MCS");
    require(decodeRateMbpsFromNrate(nrate, &mbps) && mbps == 108,
            "Apple nrate HT rate decodes to integer Mbps");

    require(normalizeIwmRateNFlagsToAppleNrate(3, &nrate) &&
                nrate == (kFamilyLegacy | 108),
            "iwm legacy 54M PLCP normalizes to Apple legacy nrate");
    require(normalizeIwxRateNFlagsToAppleNrate(kIwxRateModLegacyOfdm | 7,
                                               &nrate) &&
                nrate == (kFamilyLegacy | 108),
            "iwx legacy 54M index normalizes to Apple legacy nrate");
    require(decodeRateMbpsFromNrate(nrate, &mbps) && mbps == 54,
            "Apple legacy nrate decodes half-Mbps carrier to Mbps");

    require(!decodeGuardIntervalFromNrate(0, nullptr),
            "guard interval decoder rejects null output");
}

void testTahoeLeScanContracts()
{
    using namespace TahoeLeScanContracts;

    require(kBadArgumentStatus == 0xe00002bc,
            "LE-scan absent-owner status is Apple bad-argument 0xe00002bc");
    require(sizeof(Carrier) == kCarrierSize && kCarrierSize == 0x1c,
            "LE-scan carrier spans input offsets +0x0..+0x18");
    require(offsetof(Carrier, value04) == 0x04,
            "LE-scan first copied dword is at input +0x04");
    require(offsetof(Carrier, value18) == 0x18,
            "LE-scan last copied dword is at input +0x18");
    require(sizeof(OwnerState) == kOwnerStateSize && kOwnerStateSize == 0x18,
            "LE-scan owner state is six copied dwords");
    require(kCopiedDwordCount == 6,
            "LE-scan copies exactly six dwords into owner state");

    Carrier carrier{};
    OwnerState state{};
    carrier.ignored00 = 0xaaaaaaaa;
    carrier.value04 = 0x11111111;
    carrier.value08 = 0x22222222;
    carrier.value0c = 0x33333333;
    carrier.value10 = 0x44444444;
    carrier.value14 = 0x55555555;
    carrier.value18 = 0x66666666;

    require(copyOwnerStateFromCarrier(&carrier, &state),
            "LE-scan owner-state helper accepts a complete carrier");
    require(state.value04 == carrier.value04 && state.value08 == carrier.value08 &&
                state.value0c == carrier.value0c && state.value10 == carrier.value10 &&
                state.value14 == carrier.value14 && state.value18 == carrier.value18,
            "LE-scan owner-state helper copies +0x04..+0x18 dwords");
    require(state.value04 != carrier.ignored00,
            "LE-scan owner-state helper ignores caller dword +0x00");
    require(!copyOwnerStateFromCarrier(nullptr, &state),
            "LE-scan owner-state helper rejects null carrier");
    require(!copyOwnerStateFromCarrier(&carrier, nullptr),
            "LE-scan owner-state helper rejects null owner state");
}

void testTahoeMimoContracts()
{
    using namespace TahoeMimoContracts;

    require(kBadArgumentStatus == 0xe00002bc,
            "MIMO null/absent-owner status is Apple bad-argument 0xe00002bc");
    require(sizeof(StatusCarrier) == kStatusCarrierSize &&
                kStatusCarrierSize == 0x21,
            "MIMO status carrier spans Apple writes through output +0x20");
    require(offsetof(StatusCarrier, version) == 0x00,
            "MIMO status version dword is at output +0x00");
    require(offsetof(StatusCarrier, ownerValue04) == 0x04,
            "MIMO status owner dword is at output +0x04");
    require(offsetof(StatusCarrier, coreValue08) == 0x08,
            "MIMO status core +0x4564 dword is output +0x08");
    require(offsetof(StatusCarrier, coreValue0c) == 0x0c,
            "MIMO status core +0x4568 dword is output +0x0c");
    require(offsetof(StatusCarrier, coreValue11) == 0x11,
            "MIMO status unaligned core dword is output +0x11");
    require(offsetof(StatusCarrier, coreValue15) == 0x15,
            "MIMO status core word is output +0x15");
    require(offsetof(StatusCarrier, coreValue17) == 0x17,
            "MIMO status core byte is output +0x17");
    require(offsetof(StatusCarrier, coreValue18) == 0x18,
            "MIMO status second core byte is output +0x18");
    require(offsetof(StatusCarrier, coreValue19) == 0x19,
            "MIMO status core qword is output +0x19");
    require(kSetConfigStatusMutationCount == 0,
            "setMIMO_CONFIG does not mutate the MIMO status carrier");

    StatusCarrier status{};
    std::memset(&status, 0xa5, sizeof(status));
    require(initializeStatusCarrier(&status),
            "MIMO status initializer accepts a carrier");
    require(status.version == kStatusVersion && kStatusVersion == 1,
            "MIMO status initializer writes Apple version 1");
    require(status.ownerValue04 == 0 && status.coreValue08 == 0 &&
                status.coreValue0c == 0 && status.coreValue11 == 0 &&
                status.coreValue15 == 0 && status.coreValue17 == 0 &&
                status.coreValue18 == 0 && status.coreValue19 == 0,
            "MIMO status initializer zeroes hidden owner/core fields");
    require(!initializeStatusCarrier(nullptr),
            "MIMO status initializer rejects null carrier");
}

void testTahoeLqmContracts()
{
    using namespace TahoeLqmContracts;

    uint8_t carrier[kCarrierSize] = {};
    require(kCarrierSize == 0x24, "LQM config carrier size stays 0x24");
    require(kVersion == 1, "LQM config public version is 1");
    require(kInvalidArgumentRaw == 0x16,
            "LQM invalid carrier paths return raw 0x16");
    require(kFeatureDisabledStatus == 0x2d,
            "LQM feature-disabled gate is the only recovered 0x2d path");
    require(!hasInvalidInterval(kMinimumIntervalMs, kMinimumIntervalMs,
                                kMinimumIntervalMs),
            "LQM accepts intervals at the Apple minimum");
    require(hasInvalidInterval(kMinimumIntervalMs - 1, kMinimumIntervalMs,
                               kMinimumIntervalMs),
            "LQM rejects sample interval below the Apple minimum");
    require(hasInvalidInterval(kMinimumIntervalMs, kMinimumIntervalMs - 1,
                               kMinimumIntervalMs),
            "LQM rejects tx interval below the Apple minimum");
    require(hasInvalidInterval(kMinimumIntervalMs, kMinimumIntervalMs,
                               kMinimumIntervalMs - 1),
            "LQM rejects rx interval below the Apple minimum");
    require(isInvalidThresholdByte(kThresholdInvalidLow),
            "LQM rejects low threshold boundary");
    require(isInvalidThresholdByte(kThresholdInvalidHigh),
            "LQM rejects high threshold boundary");
    require(!isInvalidThresholdByte(kThresholdInvalidLow - 1),
            "LQM accepts threshold below invalid window");
    require(!isInvalidThresholdByte(kThresholdInvalidHigh + 1),
            "LQM accepts threshold above invalid window");
    require(!hasInvalidThresholdBytes(carrier),
            "LQM accepts zero threshold bytes");
    carrier[kThresholdOffset] = kThresholdInvalidLow;
    require(hasInvalidThresholdBytes(carrier),
            "LQM rejects threshold bytes in invalid window");
    carrier[kThresholdOffset] = 0;
    carrier[kTailOffset] = kTailMaximumAcceptedValue;
    require(!hasInvalidTailBytes(carrier),
            "LQM accepts tail byte 99");
    carrier[kTailOffset] = kTailMaximumAcceptedValue + 1;
    require(hasInvalidTailBytes(carrier),
            "LQM rejects tail byte 100");
    require(hasInvalidThresholdBytes(nullptr),
            "LQM threshold helper rejects null carrier");
    require(hasInvalidTailBytes(nullptr),
            "LQM tail helper rejects null carrier");
}

void testTahoeBssManagerWriterContracts()
{
    using MCSWriter =
        void (IO80211BssManager::*)(apple80211_mcs_index_set_data &);
    using VHTMCSWriter =
        void (IO80211BssManager::*)(apple80211_vht_mcs_index_set_data &);
    using HEMCSWriter =
        void (IO80211BssManager::*)(apple80211_he_mcs_index_set_data &);
    using RateWriter =
        void (IO80211BssManager::*)(apple80211_rate_set_data &);
    using AssocSsidWriter =
        IOReturn (IO80211BssManager::*)(const unsigned char *, unsigned long);
    using AssocRsnIeWriter =
        IOReturn (IO80211BssManager::*)(const unsigned char *, unsigned long);

    static_assert(std::is_same<decltype(&IO80211BssManager::setMCSIndexSet),
                               MCSWriter>::value,
                  "BssManager setMCSIndexSet keeps Apple writer signature");
    static_assert(std::is_same<decltype(&IO80211BssManager::setVHTMCSIndexSet),
                               VHTMCSWriter>::value,
                  "BssManager setVHTMCSIndexSet keeps Apple writer signature");
    static_assert(std::is_same<decltype(&IO80211BssManager::setHEMCSIndexSet),
                               HEMCSWriter>::value,
                  "BssManager setHEMCSIndexSet keeps Apple writer signature");
    static_assert(std::is_same<decltype(&IO80211BssManager::setRateSet),
                               RateWriter>::value,
                  "BssManager setRateSet keeps Apple writer signature");
    static_assert(std::is_same<decltype(&IO80211BssManager::setAssocSSID),
                               AssocSsidWriter>::value,
                  "BssManager setAssocSSID keeps Apple writer signature");
    static_assert(std::is_same<decltype(&IO80211BssManager::setAssocRSNIE),
                               AssocRsnIeWriter>::value,
                  "BssManager setAssocRSNIE keeps Apple writer signature");
}

void testTahoeCapabilityContracts()
{
    using namespace TahoeCapabilityContracts;

    uint8_t capabilities[24] = {};
    applyAppleConsistentCardCapabilityCluster(capabilities);

    require(capabilities[2] == kCardCapabilityByte2,
            "CARD_CAPABILITIES cap[2] matches Apple-consistent cluster");
    require(capabilities[3] == kCardCapabilityByte3,
            "CARD_CAPABILITIES cap[3] matches Apple-consistent cluster");
    require(capabilities[5] == kCardCapabilityByte5,
            "CARD_CAPABILITIES cap[5] matches Apple-consistent cluster");
    require(capabilities[6] == kCardCapabilityByte6,
            "CARD_CAPABILITIES cap[6] matches Apple-consistent cluster");
    require(capabilities[8] == kCardCapabilityByte8 &&
                capabilities[9] == kCardCapabilityByte9,
            "CARD_CAPABILITIES tail word is little-endian 0x0201");
    require(!hasAppleImpossibleAdvancedAkmBits(
                capabilities[2], capabilities[3], capabilities[6]),
            "CARD_CAPABILITIES cluster clears Apple-impossible AKM bits");
    require(hasAppleImpossibleAdvancedAkmBits(0xef, 0x2b, 0x8c),
            "CARD_CAPABILITIES rejects old over-advertised cluster");
}

} // namespace

int main()
{
    testIEBuilder();
    testNdpBuilder();
    testUsbHostBuilder();
    testBtcoexBuilders();
    testTxPowerAndActionFrameBuilders();
    testRangingBuilder();
    testApstaPublicContracts();
    testPayloadContractInventory();
    testTahoeSkywalkIoctlRoutes();
    testTahoeAssociationContracts();
    testTahoeQosDynsarContracts();
    testTahoeOpModeContracts();
    testTahoeNrateContracts();
    testTahoeLeScanContracts();
    testTahoeMimoContracts();
    testTahoeLqmContracts();
    testTahoeBssManagerWriterContracts();
    testTahoeCapabilityContracts();
    std::cout << "tahoe payload builders ok: 20 contracts, 9 builder families, APSTA public setter carriers, Skywalk IOC routes, association RSN, CARD_CAPABILITIES, OP_MODE, nrate, LE-scan, MIMO, LQM and BssManager writer contracts covered\n";
    return 0;
}
