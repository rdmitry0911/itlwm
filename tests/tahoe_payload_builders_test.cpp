#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <type_traits>

#include "AirportItlwm/AirportItlwmAPSTAInterface.hpp"
#include "AirportItlwm/AirportItlwmAPSTAEventContracts.hpp"
#include "include/Airport/apple80211_ioctl.h"
#include "AirportItlwm/TahoeAssociationAuthContracts.hpp"
#include "AirportItlwm/TahoeAssociationContracts.hpp"
#include "AirportItlwm/TahoeBeaconIeBuilder.hpp"
#include "AirportItlwm/TahoeBssBlacklistContracts.hpp"
#include "AirportItlwm/TahoeBssManagerContracts.hpp"
#include "AirportItlwm/TahoeCapabilityContracts.hpp"
#include "AirportItlwm/TahoeDriverAvailabilityContracts.hpp"
#include "AirportItlwm/TahoeLqmContracts.hpp"
#include "AirportItlwm/TahoeNrateContracts.hpp"
#include "AirportItlwm/TahoeOpModeContracts.hpp"
#include "AirportItlwm/TahoeOwnerRegistry.hpp"
#include "AirportItlwm/TahoePayloadBuilders.hpp"
#include "AirportItlwm/TahoePhyModeContracts.hpp"
#include "AirportItlwm/TahoeQosDynsarContracts.hpp"
#include "AirportItlwm/TahoeScanContracts.hpp"
#include "AirportItlwm/TahoeSkywalkIoctlRoutes.hpp"
#include "AirportItlwm/TahoeTxRxChainContracts.hpp"
#include "itlwm/hal_iwx/IwxMfpIgtkContracts.hpp"
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

    require(sizeof(apple80211_ie_data) == 0x814,
            "IE carrier uses the recovered 0x814-byte ABI");
    require(offsetof(apple80211_ie_data, ie) == 0x14,
            "IE bytes begin at the recovered +0x14 ABI offset");

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

    ie.ie_len = 0;
    require(!TahoePayloadBuilders::buildIE(&ie, &payload), "IE builder rejects zero-length IE");

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
    using namespace AirportItlwmAPSTAEventContracts;

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
    require(kAirportItlwmAPSTASetSoftAPExtCapsHasNullGuard == 0,
            "APSTA SoftAP ext-cap setter has no reference null guard");
    require(kAirportItlwmAPSTASetMisMaxStaHasNullGuardAfterAPUp == 0,
            "APSTA MIS_MAX_STA AP-up path has no reference null guard");
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
    require(kAirportItlwmAPSTASetHostApModeNotUpReturn == 6,
            "APSTA HOST_AP_MODE AP-down return is raw 6");
    require(kAirportItlwmAPSTASetHostApModeInvalidArgumentReturn == 0x16,
            "APSTA HOST_AP_MODE invalid carrier return is raw 0x16");
    require(offsetof(AirportItlwmAPSTAHostApModeNetworkDataLayout, ssidLength1c) ==
                kAirportItlwmAPSTAHostApModeNetworkDataSsidLengthOffset,
            "APSTA HOST_AP_MODE reads SSID length at input +0x1c");
    require(offsetof(AirportItlwmAPSTAHostApModeNetworkDataLayout, ssid20) ==
                kAirportItlwmAPSTAHostApModeNetworkDataSsidBytesOffset,
            "APSTA HOST_AP_MODE reads SSID bytes at input +0x20");
    require(offsetof(AirportItlwmAPSTAHostApModeNetworkDataLayout, vendorIELength2dc) ==
                kAirportItlwmAPSTAHostApModeNetworkDataVendorIELengthOffset,
            "APSTA HOST_AP_MODE reads vendor IE length at input +0x2dc");
    require(kAirportItlwmAPSTAHostApModeSsidLengthTrapThreshold == 0x21 &&
                kAirportItlwmAPSTAHostApModeSsidLengthMaxAccepted == 0x20,
            "APSTA HOST_AP_MODE accepts SSID lengths below 0x21");
    require(kAirportItlwmAPSTAHostApModeVendorIELengthTrapThreshold == 0x101 &&
                kAirportItlwmAPSTAHostApModeVendorIELengthMaxAccepted == 0x100,
            "APSTA HOST_AP_MODE accepts vendor IE lengths below 0x101");
    require(kAirportItlwmAPSTACsaMinimumPrimaryChannel == 1,
            "APSTA CSA helper rejects primary channel zero");
    require(kAirportItlwmAPSTACsaMaximumExcludedPrimaryChannel == 0x100,
            "APSTA CSA helper accepts primary channels 1..255");
    require(kAirportItlwmAPSTASetMaxAssocNoLocalClamp == 1,
            "APSTA setMaxAssoc uses cap gate instead of local clamp");
    require(kAirportItlwmAPSTASetMaxAssocPayloadAddsAssociatedCount == 1,
            "APSTA setMaxAssoc payload includes associated station count");
    require(kAirportItlwmAPSTASetSoftAPParamsHasNullGuard == 0,
            "APSTA setSOFTAP_PARAMS has no reference null guard");
    require(kAirportItlwmAPSTAWifiNetworkInfoLengthTrapThreshold == 0x21,
            "APSTA Wi-Fi network info length trap threshold is 0x21");
    require(kAirportItlwmAPSTAWifiNetworkInfoMaxAcceptedLength == 0x20,
            "APSTA Wi-Fi network info accepts lengths below 0x21");
    require(kAirportItlwmAPSTAWifiNetworkInfoFeatureGate46 == 0x46,
            "APSTA Wi-Fi network info is gated by feature 0x46");
    require(kAirportItlwmAPSTACoreFeatureFlagStoreOffset == 0x45a8,
            "APSTA core feature flags live at core-expansion +0x45a8");
    require(kAirportItlwmAPSTACoreFeatureFlagByteCount == 0x10 &&
                kAirportItlwmAPSTACoreFeatureFlagMaxExclusive == 0x80,
            "APSTA core feature flag bitmap covers 128 bits");
    require(kAirportItlwmAPSTAWifiNetworkInfoFeatureGate46ByteIndex == 0x08 &&
                kAirportItlwmAPSTAWifiNetworkInfoFeatureGate46BitMask == 0x40,
            "APSTA Wi-Fi network-info feature 0x46 maps to byte 8 bit 6");
    require(kAirportItlwmAPSTASetCipherKeyNotUpReturn == 6,
            "APSTA CIPHER_KEY AP-down return is raw 6");
    require(kAirportItlwmAPSTASetCipherKeyCipherNone == 0,
            "APSTA CIPHER_KEY cipher-none carrier value is zero");
    require(kAirportItlwmAPSTASetCipherKeyUnsupportedCipherReturn == 0,
            "APSTA CIPHER_KEY cipher-none and unsupported cipher return success after AP-up gate");
    require(sizeof(apple80211_rsn_conf_data) ==
                kAirportItlwmAPSTARSNConfCarrierSize,
            "APSTA RSN_CONF carrier is 0xa4 bytes");
    require(offsetof(apple80211_rsn_conf_data, pairwiseVersionCount08) ==
                kAirportItlwmAPSTARSNConfVersionCountOffset,
            "APSTA RSN_CONF reads pairwise version/count at input +0x08");
    require(offsetof(apple80211_rsn_conf_data, pairwiseVersionList0c) ==
                kAirportItlwmAPSTARSNConfVersionListOffset,
            "APSTA RSN_CONF reads pairwise version list at input +0x0c");
    require(offsetof(apple80211_rsn_conf_data, pairwiseCipherCount2c) ==
                kAirportItlwmAPSTARSNConfPairwiseCipherCountOffset,
            "APSTA RSN_CONF reads pairwise cipher count at input +0x2c");
    require(offsetof(apple80211_rsn_conf_data, pairwiseCipherList30) ==
                kAirportItlwmAPSTARSNConfPairwiseCipherListOffset,
            "APSTA RSN_CONF reads pairwise ciphers at input +0x30");
    require(offsetof(apple80211_rsn_conf_data, groupVersionCount58) ==
                kAirportItlwmAPSTARSNConfGroupVersionCountOffset,
            "APSTA RSN_CONF reads group version/count at input +0x58");
    require(offsetof(apple80211_rsn_conf_data, groupVersionList5c) ==
                kAirportItlwmAPSTARSNConfGroupVersionListOffset,
            "APSTA RSN_CONF reads group version list at input +0x5c");
    require(offsetof(apple80211_rsn_conf_data, groupCipherCount7c) ==
                kAirportItlwmAPSTARSNConfGroupCipherCountOffset,
            "APSTA RSN_CONF reads group cipher count at input +0x7c");
    require(offsetof(apple80211_rsn_conf_data, groupCipherList80) ==
                kAirportItlwmAPSTARSNConfGroupCipherListOffset,
            "APSTA RSN_CONF reads group ciphers at input +0x80");
    require(offsetof(apple80211_rsn_conf_data, mfpA0) ==
                kAirportItlwmAPSTARSNConfMfpWordOffset,
            "APSTA RSN_CONF reads MFP word at input +0xa0");
    require(kAirportItlwmAPSTARSNConfRejectedReturn == 0xe00002d5 &&
                kAirportItlwmAPSTARSNConfGateBit == 0x10,
            "APSTA RSN_CONF rejected gate is state +0x29b bit 0x10");
    require(kAirportItlwmAPSTARSNConfHasNullGuard == 0,
            "APSTA RSN_CONF has no recovered local null guard");
    require(kAirportItlwmAPSTARSNConfPairwiseCipherValue1Mask == 0x02 &&
                kAirportItlwmAPSTARSNConfPairwiseCipherValue2Mask == 0x04,
            "APSTA RSN_CONF pairwise cipher mask values match disasm");
    require(kAirportItlwmAPSTARSNConfGroupCipherValue4Mask == 0x40 &&
                kAirportItlwmAPSTARSNConfGroupCipherValue8Mask == 0x80 &&
                kAirportItlwmAPSTARSNConfGroupCipherValue1000Mask == 0x40000,
            "APSTA RSN_CONF group cipher mask values match disasm");
    require(kAirportItlwmAPSTARSNConfAppleCipherMap[0] == 0 &&
                kAirportItlwmAPSTARSNConfAppleCipherMap[1] == 1 &&
                kAirportItlwmAPSTARSNConfAppleCipherMap[2] == 1 &&
                kAirportItlwmAPSTARSNConfAppleCipherMap[3] == 2 &&
                kAirportItlwmAPSTARSNConfAppleCipherMap[4] == 4 &&
                kAirportItlwmAPSTARSNConfAppleCipherMap[5] == 4 &&
                kAirportItlwmAPSTARSNConfAppleCipherMap[8] == 0x100,
            "APSTA RSN_CONF Apple cipher map table matches recovered BootKC data");
    require(kAirportItlwmAPSTAStaAuthorizePreAPUpTableMutationCount == 0,
            "APSTA STA_AUTHORIZE does not mutate station table before AP-up");
    require(kAirportItlwmAPSTAStaDeauthTailcallVtableOffset == 0x1040,
            "APSTA STA_DEAUTH tailcalls STA_DISASSOCIATE vtable slot");
    require(kAirportItlwmAPSTAStaDisassocVirtualIoctlSelector == 0xc9,
            "APSTA STA_DEAUTH and STA_DISASSOCIATE share the recovered selector");
    require(kAirportItlwmAPSTAStaDisassocHasNullGuard == 0,
            "APSTA STA_DISASSOCIATE has no reference null guard");
    require(offsetof(AirportItlwmAPSTASoftAPParamsOutputLayout, param14) == 0x14,
            "APSTA SoftAP params getter writes beacon interval at output +0x14");
    require(offsetof(AirportItlwmAPSTASoftAPParamsOutputLayout, mode16) == 0x16,
            "APSTA SoftAP params getter writes mode at output +0x16");
    require(offsetof(AirportItlwmAPSTASoftAPParamsOutputLayout, enabled17) == 0x17,
            "APSTA SoftAP params getter writes enabled bit at output +0x17");
    require(offsetof(AirportItlwmAPSTASoftAPParamsOutputLayout, param18) == 0x18,
            "APSTA SoftAP params getter writes byte field at output +0x18");
    require(kAirportItlwmAPSTAGetSoftAPParamsHasNullGuard == 0,
            "APSTA getSOFTAP_PARAMS has no reference null guard");
    require(sizeof(((AirportItlwmAPSTAStateBlock *)0)->softapParam28) ==
                kAirportItlwmAPSTASetSoftAPParamsStateParam28Size,
            "APSTA SoftAP params state +0x28 stores zero-extended input byte as dword");
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
    require(kAirportItlwmAPSTAGetSoftAPStatsHasNullGuard == 0,
            "APSTA getSOFTAP_STATS has no reference null guard");
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
    require(kAirportItlwmAPSTAGetStationListNullBeforeAPDown == 1,
            "APSTA getSTATION_LIST checks null before AP-down state");
    require(kAirportItlwmAPSTAGetStationListNotUpReturn == 0x39,
            "APSTA getSTATION_LIST AP-down return is 0x39");
    require(kAirportItlwmAPSTAGetStaIEListNullReturn == 0x16,
            "APSTA getSTA_IE_LIST null return is raw 0x16");
    require(kAirportItlwmAPSTAGetStaIEListNullBeforeStationSearch == 1,
            "APSTA getSTA_IE_LIST checks null before station search");
    require(kAirportItlwmAPSTAGetStaIEListNotFoundReturn == 2,
            "APSTA getSTA_IE_LIST missing station return is 2");
    require(kAirportItlwmAPSTAGetStaIEListOutputSourceOffset ==
                kAirportItlwmAPSTAStationTableFirstEntryOffset &&
            kAirportItlwmAPSTAGetStaIEListOutputSkipsActiveFlag == 1,
            "APSTA getSTA_IE_LIST copies MAC bytes, not station active flag");
    require(sizeof(AirportItlwmAPSTAAuthIndMessageLayout) ==
                kAirportItlwmAPSTAEventAuthIndMessageSize,
            "APSTA auth-ind message payload is 0x6c bytes");
    require(kAirportItlwmAPSTAEventAuthIndMessageId == 0x98 &&
                kAirportItlwmAPSTAEventAuthIndTypeValue == 5,
            "APSTA auth-ind posts message 0x98 with type value 5");
    require(offsetof(AirportItlwmAPSTAAuthIndMessageLayout, status08) ==
                kAirportItlwmAPSTAEventAuthIndStatusOffset,
            "APSTA auth-ind status lives at payload +0x08");
    require(offsetof(AirportItlwmAPSTAAuthIndMessageLayout, macDword0c) ==
                kAirportItlwmAPSTAEventAuthIndMacDwordOffset &&
            offsetof(AirportItlwmAPSTAAuthIndMessageLayout, macTail10) ==
                kAirportItlwmAPSTAEventAuthIndMacTailOffset,
            "APSTA auth-ind MAC is copied at payload +0x0c/+0x10");
    require(kAirportItlwmAPSTAEventAuthIndRequiredStatus == 0 &&
                kAirportItlwmAPSTAEventAuthIndRequiredAuthType == 3,
            "APSTA auth-ind body requires success status and auth type 3");
    require(kAirportItlwmAPSTAEventAuthIndReasonTrapThreshold == 0x2e &&
                kAirportItlwmAPSTAEventAuthIndReasonAppleBase == 0xe0823000 &&
                kAirportItlwmAPSTAEventAuthIndReasonFallback == 0xe3ff8100,
            "APSTA auth-ind reason mapping matches recovered branch");
    require(offsetof(AirportItlwmAPSTAAuthIndMessageLayout, chunkType1Data18) ==
                kAirportItlwmAPSTAEventAuthIndChunkType1OutputOffset &&
            offsetof(AirportItlwmAPSTAAuthIndMessageLayout, chunkType2Data54) ==
                kAirportItlwmAPSTAEventAuthIndChunkType2OutputOffset,
            "APSTA auth-ind chunk outputs live at +0x18 and +0x54");
    require(associationIsAdmitted(0, 0, false, 0),
            "APSTA association admits non-Apple stations on an open SSID");
    require(!associationIsAdmitted(1, 0, true, 0) &&
                !associationIsAdmitted(0, 1, true, 0),
            "APSTA association requires success status and reason");
    require(!associationIsAdmitted(0, 0, false, 1) &&
                associationIsAdmitted(0, 0, true, 1),
            "APSTA hidden association requires a recognized Apple IE");
    require(isRecognizedAppleOUI(kAirportItlwmAPSTAAppleIEOui) &&
                isRecognizedAppleOUI(kAirportItlwmAPSTAAppleIEBsOui) &&
                isRecognizedAppleOUI(kAirportItlwmAPSTAAppleIEDeviceInfoOui),
            "APSTA association admission recognizes all three Apple OUI families");
    require(isInstantHotspotOUI(kAirportItlwmAPSTAAppleIEOui) &&
                !isInstantHotspotOUI(kAirportItlwmAPSTAAppleIEBsOui) &&
                !isInstantHotspotOUI(kAirportItlwmAPSTAAppleIEDeviceInfoOui),
            "APSTA Instant Hotspot flags accept only the primary Apple OUI");
    require(kAirportItlwmAPSTAEventAssocFullTableStillPosts == 1 &&
                kAirportItlwmAPSTAEventAssocMessageAppleFlagUsesCurrentIE == 1,
            "APSTA association message survives a full table and uses current Apple IE state");
    require(kAirportItlwmAPSTAEventRemovalCopiesMacShadow == 1 &&
                kAirportItlwmAPSTAEventIncomingHalEchoCommandCount == 0,
            "APSTA removal updates the event MAC without echoing incoming events to HAL");

    uint8_t category = 0;
    uint8_t action = 0;
    uint8_t actionV1[kAirportItlwmAPSTAActionFrameMinimumLength] = {};
    actionV1[1] = 1;
    actionV1[kAirportItlwmAPSTAActionFrameVersion1CategoryOffset] =
        kAirportItlwmAPSTAActionFrameLphsCategory;
    actionV1[kAirportItlwmAPSTAActionFrameVersion1ActionOffset] =
        kAirportItlwmAPSTAActionFrameLphsActionSleep;
    require(parseActionFrame(actionV1, sizeof(actionV1), &category, &action) &&
                isLphsStateAction(category, action) &&
                action == kAirportItlwmAPSTAStationTableLowPowerSleepState,
            "APSTA LPHS v1 parser reads category/action at +0x10/+0x11");

    uint8_t actionV2[kAirportItlwmAPSTAActionFrameVersion2MinimumLength] = {};
    actionV2[1] = 2;
    actionV2[kAirportItlwmAPSTAActionFrameVersion2CategoryOffset] =
        kAirportItlwmAPSTAActionFrameLphsCategory;
    actionV2[kAirportItlwmAPSTAActionFrameVersion2ActionOffset] =
        kAirportItlwmAPSTAActionFrameLphsActionAwake;
    require(!parseActionFrame(actionV2, sizeof(actionV2) - 1,
                              &category, &action),
            "APSTA LPHS v2 parser requires 0x1a payload bytes");
    require(parseActionFrame(actionV2, sizeof(actionV2), &category, &action) &&
                isLphsStateAction(category, action) &&
                action == kAirportItlwmAPSTAStationTableAwakeSleepState,
            "APSTA LPHS v2 parser reads category/action at +0x18/+0x19");

    uint8_t rejectedVersion[kAirportItlwmAPSTAActionFrameMinimumLength] = {};
    rejectedVersion[1] = 3;
    require(!parseActionFrame(rejectedVersion, sizeof(rejectedVersion),
                              &category, &action),
            "APSTA action-frame parser rejects byte-swapped version 3");
    uint8_t unknownVersion[kAirportItlwmAPSTAActionFrameMinimumLength] = {};
    require(parseActionFrame(unknownVersion, sizeof(unknownVersion),
                             &category, &action) &&
                category == kAirportItlwmAPSTAActionFrameUnknownCategoryAction &&
                action == kAirportItlwmAPSTAActionFrameUnknownCategoryAction,
            "APSTA action-frame parser preserves 0xaa for unknown version zero");
    require(!softAPConcurrencyIsEnabled(false, 0x1b) &&
                softAPConcurrencyIsEnabled(true, 0x01) &&
                softAPConcurrencyIsEnabled(true, 0x10) &&
                !softAPConcurrencyIsEnabled(true, 0x20),
            "APSTA concurrency requires feature 0x46 and private mask 0x1b");
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
        "tx-power-cap-quarantine",
        "wcl-action-frame-v1-v2",
        "action-frame-progress",
        "ranging-authenticate",
        "association-candidates-hidden",
        "bss-blacklist-async-owner",
        "txrx-chain-info",
        "link-changed-32",
        "bssid-changed-24",
        "wcl-link-state-16",
        "wcl-auth-assoc-complete",
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
    require(kIocSlowWifiFeatureEnabled == 0x187 &&
                !shouldRoute(kIocSlowWifiFeatureEnabled, false) &&
                !shouldRoute(kIocSlowWifiFeatureEnabled, true),
            "Skywalk leaves slow-wifi selector 0x187 to InfraProtocol");
    require(shouldRoute(kIocWclBssInfo, false) &&
                !shouldRoute(kIocWclBssInfo, true),
            "Skywalk routes WCL BSS_INFO selector 0x1b1 getter only");
    require(!shouldRoute(kIocPeerCacheControl, false) &&
                shouldRoute(kIocPeerCacheControl, true),
            "Skywalk routes APSTA peer-cache control setter only");
    require(!shouldRoute(kIocHostApMode, false) &&
                shouldRoute(kIocHostApMode, true),
            "Skywalk routes HOST_AP_MODE setter through APSTA owner");
    require(!shouldRoute(kIocRsnConf, false) &&
                shouldRoute(kIocRsnConf, true),
            "Skywalk routes RSN_CONF setter through APSTA owner");
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
    require(sizeof(IO80211AuthContext) == 0x10,
            "BssManager auth context is four dwords");
    require(offsetof(IO80211AuthContext, authLower) == 0x00 &&
                offsetof(IO80211AuthContext, authUpper) == 0x04 &&
                offsetof(IO80211AuthContext, authFlags) == 0x08 &&
                offsetof(IO80211AuthContext, bssInfoFlags) == 0x0c,
            "BssManager auth context offsets match WCL carrier copy");
    require(kAssociatedAuthTypePayloadLength == 0x0c,
            "associated auth type carrier is three dwords");
    require(kAssociatedAuthTypeVersionOffset == 0x00 &&
                kAssociatedAuthTypeLowerOffset == 0x04 &&
                kAssociatedAuthTypeUpperOffset == 0x08,
            "associated auth type offsets match Apple getter/writer ABI");
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
    require(kSlowWifiFeatureEnabledOffset == 0x7569,
            "slow-wifi enabled carrier is core-private +0x7569");
    require(kLowLatencyOwnerOffset == 0x2c28,
            "low-latency carrier is sourced from owner +0x2c28");
    require(kTxBlankingStatusOffset == 0x4ce8,
            "tx-blanking status carrier reads core-private +0x4ce8");
    require(!txBlankingStatusEnabled(0) &&
                txBlankingStatusEnabled(kTxBlankingStatusBit),
            "tx-blanking status exposes bit 0 only");
    TahoeOwnerRegistry registry;
    require(!registry.isSlowWifiFeatureEnabled(),
            "QoS/DynSAR owner starts with slow-wifi disabled");
    require(!registry.isTxBlankingStatusEnabled(),
            "QoS/DynSAR owner starts with tx-blanking disabled");
    registry.qosDynsar.slowWifiFeatureEnabled = 1;
    registry.qosDynsar.lowLatencyEnabled = 1;
    registry.qosDynsar.lowLatencyPowerSave = 2;
    registry.qosDynsar.lowLatencyWindow = 0x1234;
    registry.qosDynsar.txBlankingStatus = kTxBlankingStatusBit;
    require(registry.isSlowWifiFeatureEnabled(),
            "QoS/DynSAR owner exposes slow-wifi enabled state");
    require(registry.isTxBlankingStatusEnabled(),
            "QoS/DynSAR owner exposes tx-blanking bit 0");
    registry.reset();
    require(!registry.isSlowWifiFeatureEnabled() &&
                !registry.isTxBlankingStatusEnabled(),
            "QoS/DynSAR owner reset restores retained local zero carriers");
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
    require(kAssociatedStaMode == 0x01,
            "primary OP_MODE associated infrastructure mode is STA");
    require(kAssociatedIbssMode == 0x02,
            "primary OP_MODE associated adhoc mode is IBSS");
    require(kCurrentBssIbssCapabilityBit == 0x02,
            "primary OP_MODE reads IBSS from current-BSS capability bit 1");
    require(kPrimaryMonitorBit == 0x10,
            "primary OP_MODE monitor bit is the recovered late OR bit");
    require(kPrimaryAssociatedModeMutationCount == 1,
            "primary OP_MODE has one BssManager-associated mode OR");
    require(!initializePrimaryCarrier<OpModeProbe>(nullptr),
            "primary OP_MODE helper rejects null output");
    require(initializePrimaryCarrier(&probe),
            "primary OP_MODE helper accepts output carrier");
    require(probe.version == 1 && probe.op_mode == 0,
            "primary OP_MODE helper initializes version and zero mode");
    require(modeForAssociatedBss(0) == kAssociatedStaMode,
            "primary OP_MODE maps non-IBSS current BSS to STA");
    require(modeForAssociatedBss(kCurrentBssIbssCapabilityBit) ==
                kAssociatedIbssMode,
            "primary OP_MODE maps IBSS current BSS capability to IBSS");
    publishAssociatedBssMode(&probe, 0);
    require(probe.op_mode == kAssociatedStaMode,
            "primary OP_MODE publishes associated STA mode");
    publishAssociatedBssMode(&probe, kCurrentBssIbssCapabilityBit);
    require((probe.op_mode & kAssociatedIbssMode) != 0,
            "primary OP_MODE ORs associated IBSS mode");
}

void testTahoePhyModeContracts()
{
    using namespace TahoePhyModeContracts;

    struct PhyModeProbe {
        uint32_t version;
        uint32_t phy_mode;
        uint32_t active_phy_mode;
    } probe{};

    const uint32_t iwnSupported =
        kModeAuto | kMode11A | kMode11B | kMode11G | kMode11N;
    require(buildSupportedPhyMode(true, true, true, true, false, false) ==
                iwnSupported,
            "PHY_MODE iwn-class HT hardware excludes unsupported 11ac/11ax");
    require(buildSupportedPhyMode(true, true, true, true, true, false) ==
                (iwnSupported | kMode11AC),
            "PHY_MODE VHT hardware adds 11ac to the supported vector");
    require(buildSupportedPhyMode(true, true, true, true, true, true) ==
                (iwnSupported | kMode11AC | kMode11AX),
            "PHY_MODE HE hardware adds 11ax without dropping legacy modes");
    require(!hasCompleteVhtCapability(0x12345678, false),
            "PHY_MODE VHT requires a populated MCS/support carrier");
    require(hasCompleteVhtCapability(0x12345678, true),
            "PHY_MODE VHT accepts populated capability plus MCS carrier");
    require(!hasCompleteHeCapability(true, false),
            "PHY_MODE HE requires both capability and MCS carriers");
    require(hasCompleteHeCapability(true, true),
            "PHY_MODE HE accepts complete HE capability publication");

    require(initializePhyModeCarrier(&probe, iwnSupported),
            "PHY_MODE carrier initializes with supported vector");
    require(probe.version == kVersion && probe.phy_mode == iwnSupported,
            "PHY_MODE carrier writes version and supported modes");
    require(probe.active_phy_mode == kModeUnknown,
            "PHY_MODE active mode remains unknown before association");

    publishAssociatedActiveMode(
        &probe,
        activePhyModeForAssociatedBss(false, false, true, false, true));
    require(probe.active_phy_mode == kMode11N,
            "PHY_MODE active BSS publishes HT before legacy 2 GHz rates");

    require(activePhyModeForAssociatedBss(true, true, true, false, true) ==
                kMode11AX,
            "PHY_MODE active BSS prefers HE over VHT and HT");
    require(activePhyModeForAssociatedBss(false, true, true, false, true) ==
                kMode11AC,
            "PHY_MODE active BSS prefers VHT over HT");
    require(activePhyModeForAssociatedBss(false, false, false, true, false) ==
                kMode11A,
            "PHY_MODE active legacy 5 GHz maps to 11a");
    require(activePhyModeForAssociatedBss(false, false, false, false, true) ==
                kMode11G,
            "PHY_MODE active legacy 2 GHz OFDM maps to 11g");
    require(activePhyModeForAssociatedBss(false, false, false, false, false) ==
                kMode11B,
            "PHY_MODE active legacy 2 GHz CCK maps to 11b");

    require(!initializePhyModeCarrier<PhyModeProbe>(nullptr, iwnSupported),
            "PHY_MODE carrier rejects null output");
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
    require(buildHtNrateFromMcs(0x0f, true, &nrate) &&
                nrate == (kFamilyHt | kBandwidth40 | 0x0f),
            "iwn HT MCS carrier builds Apple HT nrate");

    require(normalizeIwmRateNFlagsToAppleNrate(3, &nrate) &&
                nrate == (kFamilyLegacy | 108),
            "iwm legacy 54M PLCP normalizes to Apple legacy nrate");
    require(normalizeIwxRateNFlagsToAppleNrate(kIwxRateModLegacyOfdm | 7,
                                               &nrate) &&
                nrate == (kFamilyLegacy | 108),
            "iwx legacy 54M index normalizes to Apple legacy nrate");
    require(buildLegacyNrateFromHalfMbps(108, &nrate) &&
                nrate == (kFamilyLegacy | 108),
            "iwn legacy half-Mbps carrier builds Apple legacy nrate");
    require(decodeRateMbpsFromNrate(nrate, &mbps) && mbps == 54,
            "Apple legacy nrate decodes half-Mbps carrier to Mbps");

    require(!decodeGuardIntervalFromNrate(0, nullptr),
            "guard interval decoder rejects null output");
}

void testTahoeBssBlacklistContracts()
{
    using namespace TahoeBssBlacklistContracts;

    require(APPLE80211_IOC_BSS_BLACKLIST == 0x174,
            "BSS blacklist BSD route uses selector 0x174");
    require(kSelector == APPLE80211_IOC_BSS_BLACKLIST,
            "BSS blacklist contract selector matches the BSD route");
    require(kRequestLength == 0x2b && sizeof(AppliedState) == 0x2b,
            "BSS blacklist request is count plus seven six-byte BSSIDs");
    require(kEventMessage == 0xa3 && sizeof(EventCarrier) == 0x30,
            "BSS blacklist async result uses message 0xa3 and 48-byte capacity");
    require(offsetof(EventCarrier, body) == 0x04,
            "BSS blacklist event variable body begins after its count");

    uint8_t request[kRequestLength] = {};
    request[0] = 1;
    const uint8_t bssid[kBssidLength] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    std::memcpy(request + 1, bssid, sizeof(bssid));

    require(routePreflightStatus(false, 0, nullptr) == kNoInterfaceStatus,
            "BSS route returns 0x66 before malformed carrier validation");
    require(routePreflightStatus(false, kRequestLength, request) ==
                kNoInterfaceStatus,
            "BSS route returns 0x66 for an absent interface with valid carrier");
    require(routePreflightStatus(true, kRequestLength, nullptr) ==
                kInvalidArgumentStatus,
            "BSS route returns 0x16 for a null carrier after interface admission");
    require(routePreflightStatus(true, kRequestLength - 1, request) ==
                kInvalidArgumentStatus &&
                routePreflightStatus(true, kRequestLength + 1, request) ==
                    kInvalidArgumentStatus,
            "BSS route returns 0x16 for either wrong carrier length");
    require(routePreflightStatus(true, kRequestLength, request) ==
                kSuccessStatus,
            "BSS route accepts an interface-backed exact carrier");
    require(localAdmissionStatus(false) == kSuccessStatus &&
                localAdmissionStatus(true) == 1,
            "BSS local bool admission preserves raw 0 and 1 statuses");
    require(wrapperStatus(0x12345678, false) == 0x12345678 &&
                wrapperStatus(1, false) == 1,
            "BSS wrapper returns nonzero admission before owner cast");
    require(wrapperStatus(kSuccessStatus, false) == kClassOwnerAbsentStatus,
            "BSS wrapper maps failed owner cast to 0xe082280e");
    require(wrapperStatus(kSuccessStatus, true) == kSuccessStatus,
            "BSS wrapper dispatches only after admission and owner cast");

    AppliedState applied{};
    require(decodeAppliedState(request, &applied),
            "BSS blacklist owner accepts one-entry request");
    require(applied.count == 1 &&
                std::memcmp(applied.bssids[0], bssid, sizeof(bssid)) == 0,
            "BSS blacklist owner preserves the applied BSSID");

    EventCarrier event{};
    require(buildEventCarrier(applied.count, &applied.bssids[0][0], &event) == 12,
            "one-entry BSS blacklist result publishes 12 bytes");
    const uint8_t *eventRaw = reinterpret_cast<const uint8_t *>(&event);
    require(event.count == 1 &&
                std::memcmp(event.body, bssid, sizeof(bssid)) == 0 &&
                eventRaw[eventTrailingOffset(1)] == 0 &&
                eventRaw[eventTrailingOffset(1) + 1] == 0,
            "BSS blacklist result preserves BSSID and zeros its dynamic tail");

    request[0] = 8;
    AppliedState unchanged;
    std::memset(&unchanged, 0x5a, sizeof(unchanged));
    AppliedState before = unchanged;
    require(!decodeAppliedState(request, &unchanged),
            "BSS blacklist lower owner rejects count eight");
    require(std::memcmp(&unchanged, &before, sizeof(unchanged)) == 0,
            "invalid BSS blacklist request leaves applied state unchanged");

    std::memset(&applied, 0, sizeof(applied));
    applied.count = kMaxEntries;
    for (size_t i = 0; i < sizeof(applied.bssids); i++)
        reinterpret_cast<uint8_t *>(applied.bssids)[i] = static_cast<uint8_t>(i);
    require(buildEventCarrier(applied.count, &applied.bssids[0][0], &event) ==
                kEventCapacity,
            "seven-entry BSS blacklist result publishes full 48-byte carrier");
    eventRaw = reinterpret_cast<const uint8_t *>(&event);
    require(eventRaw[eventTrailingOffset(kMaxEntries)] == 0 &&
                eventRaw[eventTrailingOffset(kMaxEntries) + 1] == 0,
            "full BSS blacklist result keeps its tail at bytes 46 and 47");
    require(buildEventCarrier(0, nullptr, &event) == 0 && event.count == 0,
            "empty BSS blacklist query succeeds without an event");
}

void testTahoeLqmContracts()
{
    using namespace TahoeLqmContracts;

    uint8_t carrier[kCarrierSize] = {};
    require(kCarrierSize == 0x24, "LQM config carrier size stays 0x24");
    require(kVersion == 1, "LQM config public version is 1");
    require(kDefaultStatsIntervalMs == 5000,
            "driver-owned LQM stats timer uses the Apple 5000 ms default");
    require(kEventMessage == 0x27 && kEventSize == 0x1dc,
            "driver-owned LQM event preserves the Apple selector and size");
    require(kInvalidArgumentRaw == 0x16,
            "LQM invalid carrier paths return raw 0x16");
    require(kFeatureDisabledStatus == 0x2d,
            "LQM feature-disabled gate is the only recovered 0x2d path");
    require(kLinkChangedSnrOffset == 0x08 && kLinkChangedNfOffset == 0x0a,
            "link-changed carrier exposes SNR/NF at +0x08/+0x0a");
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

    uint16_t snr = 0;
    uint16_t nf = 0;
    require(buildLinkChangedSignalMetrics(-63, -95, &snr, &nf),
            "link-changed signal helper accepts valid noise floor");
    require(snr == 32, "link-changed signal helper derives SNR from RSSI minus NF");
    require(static_cast<int16_t>(nf) == -95,
            "link-changed signal helper preserves signed NF in raw carrier word");
    require(buildLinkChangedSignalMetrics(-10, -200, &snr, &nf) &&
                snr == kMaximumSnr,
            "link-changed signal helper clamps SNR to Apple byte-range maximum");
    require(!buildLinkChangedSignalMetrics(-63, kInvalidNoiseZero, &snr, &nf),
            "link-changed signal helper rejects zero noise sentinel");
    require(!buildLinkChangedSignalMetrics(-63, kInvalidNoiseSentinel, &snr, &nf),
            "link-changed signal helper rejects -127 noise sentinel");
    require(!buildLinkChangedSignalMetrics(-63, -95, nullptr, &nf),
            "link-changed signal helper rejects null SNR output");
    require(!buildLinkChangedSignalMetrics(-63, -95, &snr, nullptr),
            "link-changed signal helper rejects null NF output");

    require(sizeof(EventData) == kEventSize,
            "LQM event carrier remains 0x1dc bytes");
    require(offsetof(EventData, rssi) == 0x04 &&
                offsetof(EventData, snr) == 0x0c &&
                offsetof(EventData, noise) == 0x10,
            "LQM signal fields preserve recovered offsets");
    require(offsetof(EventData, countersValid) == 0x30 &&
                offsetof(EventData, eventValid) == 0x1d8 &&
                offsetof(EventData, counterSnapshotChanged) == 0x1d9,
            "LQM validity fields preserve recovered offsets");

    const CounterSnapshot previous{1, 2, 90, 180, 40};
    const CounterSnapshot current{2, 3, 100, 200, 50};
    EventData event{};
    require(buildEventData(-63, -95, current, &previous, &event),
            "LQM event builder accepts real signal and changed counters");
    require(event.hasRssi == 1 && event.rssi == -63 &&
                event.hasCurrentBssRssi == 1 &&
                event.currentBssRssi == -63,
            "LQM event builder carries current BSS RSSI");
    require(event.hasNoise == 1 && event.noise == -95 &&
                event.hasSnr == 1 && event.snr == 32,
            "LQM event builder carries real noise and derived SNR");
    require(event.txErrors == current.txErrors &&
                event.rxErrors == current.rxErrors &&
                event.txFrames == current.txFrames &&
                event.rxFrames == current.rxFrames &&
                event.beaconFrames == current.beaconFrames,
            "LQM event builder carries the changed hardware snapshot");
    require(event.countersValid == 1 && event.eventValid == 1 &&
                event.counterSnapshotChanged == 1,
            "LQM changed snapshot sets only recovered validity gates");

    require(buildEventData(-63, kInvalidNoiseSentinel, current, &current,
                           &event),
            "LQM event builder accepts a valid RSSI without noise data");
    require(event.hasNoise == 0 && event.hasSnr == 0,
            "LQM event builder does not claim missing noise or SNR");
    require(event.countersValid == 0 &&
                event.counterSnapshotChanged == 0 &&
                event.txErrors == 0 && event.rxErrors == 0 &&
                event.txFrames == 0 && event.rxFrames == 0 &&
                event.beaconFrames == 0,
            "LQM unchanged generation clears counter fields like Apple");
    require(event.eventValid == 1,
            "LQM signal event remains valid when counters are unchanged");
    require(!buildEventData(-101, -95, current, nullptr, &event) &&
                !buildEventData(1, -95, current, nullptr, &event),
            "LQM event builder rejects RSSI outside the reference range");
    require(!buildEventData(-63, -95, current, nullptr, nullptr),
            "LQM event builder rejects a null output carrier");
}

void testTahoeTxRxChainContracts()
{
    using namespace TahoeTxRxChainContracts;

    const Carrier carrier = build(0x06, 0x03, 0x02, 0x04);
    require(sizeof(carrier) == 0x04,
            "TXRX_CHAIN_INFO carrier remains four bytes");
    require(carrier.hardwareRx == 0x06 && carrier.hardwareTx == 0x03,
            "TXRX_CHAIN_INFO preserves hardware RX/TX mask order");
    require(carrier.activeTx == 0x02 && carrier.activeRx == 0x04,
            "TXRX_CHAIN_INFO preserves independent active TX/RX masks");
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
    using NetworkFlagsWriter =
        void (IO80211BssManager::*)(bool, unsigned int);
    using AssocSsidWriter =
        IOReturn (IO80211BssManager::*)(const unsigned char *, unsigned long);
    using AssociatedAuthTypeWriter =
        void (IO80211BssManager::*)(unsigned char *, unsigned short);
    using AssocRsnIeWriter =
        IOReturn (IO80211BssManager::*)(const unsigned char *, unsigned long);
    using AdHocCreatedWriter = void (IO80211BssManager::*)(bool);
    using CurrentBssGetter = IO80211BSSBeacon *(IO80211BssManager::*)() const;
    using CurrentBssWriter =
        void (IO80211BssManager::*)(IO80211BSSBeacon *, bool);

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
    static_assert(std::is_same<decltype(&IO80211BssManager::setNetworkFlags),
                               NetworkFlagsWriter>::value,
                  "BssManager setNetworkFlags keeps Apple writer signature");
    static_assert(std::is_same<decltype(&IO80211BssManager::setAssocSSID),
                               AssocSsidWriter>::value,
                  "BssManager setAssocSSID keeps Apple writer signature");
    static_assert(std::is_same<decltype(&IO80211BssManager::setAssociatedAuthType),
                               AssociatedAuthTypeWriter>::value,
                  "BssManager setAssociatedAuthType keeps Apple writer signature");
    static_assert(std::is_same<decltype(&IO80211BssManager::setAssocRSNIE),
                               AssocRsnIeWriter>::value,
                  "BssManager setAssocRSNIE keeps Apple writer signature");
    static_assert(std::is_same<decltype(&IO80211BssManager::setAdHocCreated),
                               AdHocCreatedWriter>::value,
                  "BssManager setAdHocCreated keeps Apple writer signature");
    static_assert(std::is_same<decltype(&IO80211BssManager::getCurrentBSS),
                               CurrentBssGetter>::value,
                  "BssManager getCurrentBSS keeps Apple current-BSS signature");
    static_assert(std::is_same<decltype(&IO80211BssManager::setCurrentBSS),
                               CurrentBssWriter>::value,
                  "BssManager setCurrentBSS keeps Apple current-BSS signature");
}

void testTahoeBssidChangedCarrierLayout()
{
    struct Channel {
        uint32_t version;
        uint32_t channel;
        uint32_t flags;
    };
    struct BssidChangedCarrier {
        uint8_t bssid[6];
        uint8_t pad06[2];
        Channel channel;
        uint32_t reason;
    };

    require(sizeof(BssidChangedCarrier) == 0x18,
            "BSSID_CHANGED compact carrier remains 24 bytes");
    require(offsetof(BssidChangedCarrier, bssid) == 0x00,
            "BSSID_CHANGED carrier stores BSSID at +0x00");
    require(offsetof(BssidChangedCarrier, channel) == 0x08,
            "BSSID_CHANGED carrier stores apple80211_channel at +0x08");
    require(sizeof(Channel) == 0x0c,
            "apple80211_channel remains the 12-byte embedded channel carrier");
    require(offsetof(BssidChangedCarrier, reason) == 0x14,
            "BSSID_CHANGED carrier stores reason at +0x14");
}

void testTahoeCapabilityContracts()
{
    using namespace TahoeCapabilityContracts;

    uint8_t capabilities[24] = {};
    applyAppleConsistentCardCapabilityCluster(capabilities);

    require(kApple80211BindCardCapabilitiesLength == 0x15,
            "Apple80211 bind CARD_CAPABILITIES compact length remains 21 bytes");
    require(capabilities[0] == kCardCapabilityByte0,
            "CARD_CAPABILITIES cap[0] matches Apple request bitmap byte");
    require(capabilities[1] == kCardCapabilityByte1,
            "CARD_CAPABILITIES cap[1] matches Apple request bitmap base");
    require(capabilities[2] == kCardCapabilityByte2,
            "CARD_CAPABILITIES cap[2] matches Apple-consistent cluster");
    require(capabilities[3] == kCardCapabilityByte3,
            "CARD_CAPABILITIES cap[3] matches Apple-consistent cluster");
    require(capabilities[5] == kCardCapabilityByte5,
            "CARD_CAPABILITIES cap[5] matches Apple-consistent cluster");
    require(capabilities[6] == kCardCapabilityByte6,
            "CARD_CAPABILITIES cap[6] matches Apple-consistent cluster");
    require(capabilities[7] == kCardCapabilityByte7,
            "CARD_CAPABILITIES cap[7] exposes current scan/profile request gates");
    require(capabilities[8] == kCardCapabilityByte8 &&
                capabilities[9] == kCardCapabilityByte9,
            "CARD_CAPABILITIES tail word is little-endian 0x0201");
    require(!hasAppleImpossibleAdvancedAkmBits(
                capabilities[2], capabilities[3], capabilities[6]),
            "CARD_CAPABILITIES cluster clears Apple-impossible AKM bits");
    require(hasAppleImpossibleAdvancedAkmBits(0xef, 0x2b, 0x8c),
            "CARD_CAPABILITIES rejects old over-advertised cluster");
}

void testIwxMfpIgtkContracts()
{
    using namespace IwxMfpIgtkContracts;

    require(kAx211FirmwareApi == 68,
            "AX211 IGTK gate pins the recovered API-68 header value");
    require(kFirmwareMfpFlag == 0x4,
            "AX211 IGTK gate pins firmware MFP flag bit 2");
    require(kMultiQueueRxCapability == 68,
            "AX211 IGTK v2 gate pins MULTI_QUEUE_RX_SUPPORT capability 68");
    require(kIgtkInstallCipher == 2 && kIgtkDeleteFlag == 0x800,
            "AX211 IGTK command uses CCM install and NOT_VALID delete flags");
    require(kMgmtMcastKeyCommand == 0x1f && kStationId == 0,
            "AX211 IGTK command and station ids match the recovered ABI");
    require(sizeof(MgmtMcastKeyCommandV2) == 0x34,
            "AX211 IGTK v2 command has recovered 0x34-byte ABI");
    require(offsetof(MgmtMcastKeyCommandV2, igtk) == 0x04 &&
                offsetof(MgmtMcastKeyCommandV2, key_id) == 0x24 &&
                offsetof(MgmtMcastKeyCommandV2, sta_id) == 0x28 &&
                offsetof(MgmtMcastKeyCommandV2, receive_seq_cnt) == 0x2c,
            "AX211 IGTK v2 member offsets match the firmware ABI");
    require(hasValidIgtkShape(4, 16) && hasValidIgtkShape(5, 16),
            "AX211 IGTK v2 accepts only standard 16-byte IGTK slots");
    require(!hasValidIgtkShape(3, 16) && !hasValidIgtkShape(6, 16) &&
                !hasValidIgtkShape(4, 32),
            "AX211 IGTK v2 rejects unsupported key ids and lengths");
    require(hasExactAbiPrerequisites(kAx211FirmwareApi, kFirmwareMfpFlag,
                                     true),
            "AX211 IGTK gate accepts the complete recovered ABI proof");
    require(!hasExactAbiPrerequisites(kAx211FirmwareApi - 1,
                                      kFirmwareMfpFlag, true) &&
                !hasExactAbiPrerequisites(kAx211FirmwareApi, 0, true) &&
                !hasExactAbiPrerequisites(kAx211FirmwareApi,
                                          kFirmwareMfpFlag, false),
            "AX211 IGTK gate rejects stale API, missing MFP, and missing MQ RX");
}

static constexpr size_t kTahoeScanResultMaxRates = 15;
static constexpr size_t kTahoeScanResultMaxSsidLength = 32;
static constexpr size_t kTahoeScanResultIeDataLength = 2116;

struct TahoeScanResultChannelProbe {
    uint32_t version;
    uint32_t channel;
    uint32_t flags;
};

struct TahoeScanResultLayoutProbe {
    uint32_t version;
    TahoeScanResultChannelProbe asr_channel;
    int16_t asr_unk;
    int16_t asr_noise;
    int16_t asr_snr;
    int16_t asr_rssi;
    int16_t asr_beacon_int;
    int16_t asr_cap;
    uint8_t asr_bssid[6];
    uint8_t asr_nrates;
    uint8_t asr_nr_unk;
    uint32_t asr_rates[kTahoeScanResultMaxRates];
    uint8_t asr_ssid_len;
    uint8_t asr_ssid[kTahoeScanResultMaxSsidLength];
    int16_t unk;
    uint8_t unk2;
    uint32_t asr_age;
    uint16_t unk3;
    int16_t asr_ie_len;
    uint8_t asr_ie_data[kTahoeScanResultIeDataLength];
    uint64_t asr_timestamp;
} __attribute__((packed));

void testTahoeScanResultLayout()
{
    const uint8_t zeroBssid[TahoeScanContracts::kBssidLength] = {};
    const uint8_t validBssid[TahoeScanContracts::kBssidLength] = {
        0x80, 0xe4, 0xba, 0x20, 0xef, 0xf9,
    };

    require(kTahoeScanResultMaxRates == 15,
            "Tahoe scan-result carrier preserves 15 legacy rates before SSID");
    require(kTahoeScanResultMaxSsidLength == 0x20,
            "Tahoe scan-result SSID writer clamps to Apple BssManager max length");
    require(offsetof(TahoeScanResultLayoutProbe, asr_bssid) == 0x1c,
            "Tahoe scan-result BSSID stays at +0x1c");
    require(offsetof(TahoeScanResultLayoutProbe, asr_ssid_len) == 0x60,
            "Tahoe scan-result SSID length matches Apple BssManager +0x60 writer");
    require(offsetof(TahoeScanResultLayoutProbe, asr_ssid) == 0x61,
            "Tahoe scan-result SSID bytes match Apple BssManager +0x61 writer");
    require(offsetof(TahoeScanResultLayoutProbe, asr_ie_len) == 0x8a,
            "Tahoe scan-result IE length remains at +0x8a");
    require(offsetof(TahoeScanResultLayoutProbe, asr_ie_data) == 0x8c,
            "Tahoe scan-result IE data remains at +0x8c");
    require(offsetof(TahoeScanResultLayoutProbe, asr_timestamp) == 0x8d0,
            "Tahoe scan-result timestamp matches Apple80211 +0x8d0 reader");
    require(sizeof(TahoeScanResultLayoutProbe) == 0x8d8,
            "Tahoe scan-result carrier size matches Apple80211GetWithIOCTL 0x8d8 buffer");
    require(TahoeScanContracts::kWclScanResultMetaFlags == 0x2,
            "WCL scan-result BeaconMetaData flags match Apple bit-1-only builder");
    require((TahoeScanContracts::kWclScanResultMetaFlags &
             TahoeScanContracts::kWclScanResultSsidPresentLegacyMask) == 0,
            "WCL scan-result BeaconMetaData clears the legacy bit-2 SSID hint");
    require(!TahoeScanContracts::hasRenderableBssid(nullptr),
            "scan renderability rejects null BSSID");
    require(!TahoeScanContracts::hasRenderableBssid(zeroBssid),
            "scan renderability rejects zero BSSID");
    require(TahoeScanContracts::hasRenderableBssid(validBssid),
            "scan renderability accepts nonzero BSSID");
}

void testTahoeCurrentNetworkCarrierContract()
{
    require(offsetof(TahoeScanResultLayoutProbe, asr_channel) == 0x04,
            "CURRENT_NETWORK BssManager writer preserves scan channel at +0x4");
    require(offsetof(TahoeScanResultLayoutProbe, asr_rssi) == 0x16,
            "CURRENT_NETWORK BssManager writer preserves RSSI at +0x16");
    require(offsetof(TahoeScanResultLayoutProbe, asr_bssid) == 0x1c,
            "CURRENT_NETWORK BssManager writer publishes BSSID at +0x1c");
    require(offsetof(TahoeScanResultLayoutProbe, asr_ssid_len) == 0x60,
            "CURRENT_NETWORK BssManager writer publishes SSID length at +0x60");
    require(offsetof(TahoeScanResultLayoutProbe, asr_ssid) == 0x61,
            "CURRENT_NETWORK BssManager writer publishes SSID bytes at +0x61");
    require(offsetof(TahoeScanResultLayoutProbe, asr_ie_len) == 0x8a,
            "CURRENT_NETWORK BssManager writer preserves IE length at +0x8a");
    require(offsetof(TahoeScanResultLayoutProbe, asr_ie_data) == 0x8c,
            "CURRENT_NETWORK BssManager writer preserves IE data at +0x8c");
    require(offsetof(TahoeScanResultLayoutProbe, asr_timestamp) == 0x8d0,
            "CURRENT_NETWORK preserves Apple80211 timestamp at +0x8d0");
    require(kTahoeScanResultMaxSsidLength == 0x20,
            "CURRENT_NETWORK BssManager writer clamps SSID length to 0x20");
}

void testTahoeBeaconIeBuilder()
{
    const uint8_t completeTail[] = {
        0x00, 0x03, 'f', 'o', 'o',
        0x05, 0x04, 0x02, 0x03, 0x00, 0x00,
        0x30, 0x02, 0xaa, 0xbb,
    };
    uint8_t out[64] = {};
    uint32_t len = TahoeBeaconIeBuilder::buildCurrentBssIeStream(
        reinterpret_cast<const uint8_t *>("bar"), 3, 7, 1,
        completeTail, sizeof(completeTail), out, sizeof(out));
    require(len == sizeof(completeTail),
            "beacon IE builder preserves full raw tagged tail length");
    require(std::memcmp(out, completeTail, sizeof(completeTail)) == 0,
            "beacon IE builder preserves full raw tagged tail bytes");

    const uint8_t rsnOnlyTail[] = { 0x30, 0x02, 0xaa, 0xbb };
    std::memset(out, 0, sizeof(out));
    len = TahoeBeaconIeBuilder::buildCurrentBssIeStream(
        reinterpret_cast<const uint8_t *>("AIAM"), 4, 6, 0,
        rsnOnlyTail, sizeof(rsnOnlyTail), out, sizeof(out));
    const uint8_t expectedRebuilt[] = {
        0x00, 0x04, 'A', 'I', 'A', 'M',
        0x05, 0x04, 0x06, 0x01, 0x00, 0x00,
        0x30, 0x02, 0xaa, 0xbb,
    };
    require(len == sizeof(expectedRebuilt),
            "beacon IE builder rebuilds SSID/TIM before RSN tail");
    require(std::memcmp(out, expectedRebuilt, sizeof(expectedRebuilt)) == 0,
            "beacon IE builder emits CoreWLAN-visible SSID and TIM IEs");

    const uint8_t malformedTail[] = { 0x30, 0x04, 0xaa };
    std::memset(out, 0, sizeof(out));
    len = TahoeBeaconIeBuilder::buildCurrentBssIeStream(
        reinterpret_cast<const uint8_t *>("x"), 1, 0, 1,
        malformedTail, sizeof(malformedTail), out, sizeof(out));
    require(len == 9,
            "beacon IE builder drops malformed raw tail instead of copying a partial IE");
    require(out[0] == 0x00 && out[1] == 0x01 && out[2] == 'x',
            "beacon IE builder keeps reconstructed SSID when raw tail is malformed");
    require(out[3] == 0x05 && out[4] == 0x04,
            "beacon IE builder keeps reconstructed TIM when raw tail is malformed");
}

void testTahoeBssManagerContracts()
{
    using namespace TahoeBssManagerContracts;

    require(sizeof(BeaconMetaData) == 0x44,
            "BssManager BeaconMetaData is the exact 0x44 carrier");
    require(sizeof(BeaconPayload) == 0x844,
            "BssManager BeaconPayload is the exact 0x844 carrier");
    require(kBssManagerObjectSize == 0x18 &&
                kBssBeaconObjectSize == 0x18,
            "BssManager and BSSBeacon object sizes match their metaclasses");
    require(kBaseCurrentBssStateFeatureGate == 0x60 &&
                kCurrentBssPrivateStateMask == 0x01 &&
                kCurrentBssChannelMessage == 0x52,
            "BssManager current-BSS state uses the exact feature and message gates");
    require(offsetof(BeaconMetaData, channelSpec) == 0x04 &&
            offsetof(BeaconMetaData, ssid) == 0x06 &&
            offsetof(BeaconMetaData, ssidLength) == 0x26 &&
            offsetof(BeaconMetaData, primaryChannel) == 0x27,
            "BssManager channel and SSID fields preserve reference offsets");
    require(offsetof(BeaconMetaData, bssid) == 0x29 &&
            offsetof(BeaconMetaData, rssi) == 0x30 &&
            offsetof(BeaconMetaData, noise) == 0x34 &&
            offsetof(BeaconMetaData, snr) == 0x36 &&
            offsetof(BeaconMetaData, beaconInterval) == 0x38 &&
            offsetof(BeaconMetaData, flags) == 0x40,
            "BssManager identity and signal fields preserve reference offsets");

    BeaconPayload payload{};
    payload.meta.ieLength = 3;
    payload.meta.channelSpec = 0xc024;
    payload.meta.ssidLength = 4;
    payload.meta.flags = kSsidBytesPresentFlags;
    payload.ie[0] = 0x00;
    payload.ie[1] = 0x01;
    payload.ie[2] = 'x';
    require(raw(payload)[0] == 3 && raw(payload)[4] == 0x24 &&
            raw(payload)[5] == 0xc0 && raw(payload)[0x26] == 4 &&
            raw(payload)[0x40] == 0x6 && raw(payload)[0x44 + 2] == 'x',
            "BssManager payload serializes metadata and IE bytes contiguously");
}

void testTahoeAssociationAuthContracts()
{
    using namespace TahoeAssociationAuthContracts;

    const uint32_t mixedTransition = kAuthWpa2Psk | kAuthWpa3Sae;
    uint32_t local = localAuthMaskWithoutFallbackRewrite(mixedTransition);
    require(local == kAuthWpa2Psk,
            "association auth keeps explicit WPA2 PSK from mixed transition auth");
    require(usesLocalWpaProtocol(local) && usesLocalPskAkm(local),
            "association auth maps explicit WPA2 PSK to local RSN/PSK");
    require(usesLocalLegacyPskAkm(local) && !usesLocalSha256PskAkm(local),
            "ordinary WPA2 PSK does not implicitly select SHA256-PSK");
    require(!usesLocalEnterpriseAkm(local),
            "association auth does not add enterprise AKM to PSK auth");

    local = localAuthMaskWithoutFallbackRewrite(kAuthWpa3Sae);
    require(local == 0,
            "association auth does not rewrite pure WPA3 SAE to WPA2 PSK");
    require(!usesLocalWpaProtocol(local) && !usesLocalPskAkm(local),
            "association auth leaves pure WPA3 SAE outside local WPA mapping");

    local = localAuthMaskWithoutFallbackRewrite(kAuthSha256Psk);
    require(!usesLocalLegacyPskAkm(local) && usesLocalSha256PskAkm(local),
            "explicit SHA256 PSK remains distinguishable from ordinary PSK");
    require(mayUseLocalPskPmk(kAuthSha256Psk),
            "PLTI accepts an explicit SHA256 PSK selector");

    local = localAuthMaskWithoutFallbackRewrite(kAuthWpa2Psk | kAuthSha256Psk);
    require(usesLocalLegacyPskAkm(local) && usesLocalSha256PskAkm(local),
            "an explicit dual-PSK selector preserves both advertised AKMs");

    local = localAuthMaskWithoutFallbackRewrite(kAuthWpa3Enterprise);
    require(local == 0,
            "association auth does not rewrite pure WPA3 enterprise to WPA2");
    require(isWpa3OnlyAuth(kAuthWpa3FtEnterprise),
            "association auth identifies WPA3-only carriers without mapping them");
    require(!isWpa3OnlyAuth(mixedTransition),
            "association auth does not classify mixed transition auth as WPA3-only");
    require(requiresUnsupportedWpa3Auth(kAuthWpa3Sae),
            "association auth rejects pure WPA3 SAE before legacy association");
    require(requiresUnsupportedWpa3Auth(kAuthWpa3Sae | (1U << 31)),
            "association auth rejects WPA3 SAE with unrelated bits and no fallback");
    require(requiresUnsupportedWpa3Auth(mixedTransition | (1U << 31)),
            "association auth rejects an unobserved WPA3 plus PSK carrier");
    require(!requiresUnsupportedWpa3Auth(mixedTransition),
            "association auth preserves explicit WPA2 PSK transition fallback");
    require(requiresUnsupportedWpa3Auth(kAuthWpa3Enterprise | kAuthWpa2),
            "association auth rejects unimplemented WPA3 enterprise transition");
    require(mayUseLocalPskPmk(mixedTransition),
            "PLTI permits only the audited WPA3 PSK transition carrier");
    require(!mayUseLocalPskPmk(mixedTransition | (1U << 31)),
            "PLTI rejects an unobserved WPA3 plus PSK carrier");
}

void testTahoeCountryCodeCarrierContracts()
{
    require(APPLE80211_MAX_CC_LEN == 3,
            "country-code compact Apple80211 carrier is three alpha2 bytes");
    require(offsetof(apple80211_country_code_data, version) == 0,
            "country-code full struct keeps version at +0");
    require(offsetof(apple80211_country_code_data, cc) == 4,
            "country-code full struct keeps alpha2 bytes after version");
    require(sizeof(apple80211_country_code_data) == 8,
            "country-code full struct remains distinct from compact CFString carrier");
}

void testTahoeWclAuthAssocCarrierContracts()
{
    require(APPLE80211_M_WCL_AUTH_ASSOC_EVENT == 0x4e,
            "WCL auth/assoc complete uses AppleBCMWLAN handleAssocEvent selector 0x4e");
    require(APPLE80211_WCL_AUTH_ASSOC_COMPLETE_LEN == 0x08,
            "WCL auth/assoc complete carrier length is 8 bytes");
    require(sizeof(apple80211_wcl_auth_assoc_complete_event) == 0x08,
            "WCL auth/assoc complete carrier is two dwords");
    require(offsetof(apple80211_wcl_auth_assoc_complete_event, status) == 0x00,
            "WCL auth/assoc status lives at +0x00");
    require(offsetof(apple80211_wcl_auth_assoc_complete_event, reason) == 0x04,
            "WCL auth/assoc reason lives at +0x04");
}

void testTahoeDriverAvailabilityContracts()
{
    using namespace TahoeDriverAvailabilityContracts;

    require(sizeof(apple80211_driver_available_data) == 0xf8,
            "driver-availability carrier is exactly 0xf8 bytes");
    require(offsetof(apple80211_driver_available_data, version) == 0x00 &&
            offsetof(apple80211_driver_available_data, flags) == 0x04 &&
            offsetof(apple80211_driver_available_data, available) == 0x08 &&
            offsetof(apple80211_driver_available_data, status) == 0x0c &&
            offsetof(apple80211_driver_available_data, reason) == 0x10 &&
            offsetof(apple80211_driver_available_data, sub_reason) == 0x14,
            "driver-availability prefix keeps six independent dwords");

    const apple80211_driver_available_data boot = build(Transition::BootReady);
    require(boot.version == 3 && boot.flags == 0x20 && boot.available == 1 &&
            boot.status == 0 && boot.reason == 0xe0822803 && boot.sub_reason == 0,
            "boot-ready carrier matches AppleBCMWLANCore::bootChipImage");

    const apple80211_driver_available_data powerOff = build(Transition::PowerOff);
    require(powerOff.version == 3 && powerOff.flags == 0 && powerOff.available == 0 &&
            powerOff.status == 0 && powerOff.reason == 0xe0821804 &&
            powerOff.sub_reason == 0,
            "power-off carrier matches AppleBCMWLANCore::powerOff");

    const apple80211_driver_available_data powerOn = build(Transition::PowerOn);
    require(powerOn.version == 3 && powerOn.flags == 0 && powerOn.available == 1 &&
            powerOn.status == 0 && powerOn.reason == 0xe0821803 &&
            powerOn.sub_reason == 0,
            "power-on carrier matches AppleBCMWLANCore::powerOn");

    const uint8_t zeroPad[sizeof(powerOn.pad)] = {};
    require(std::memcmp(boot.pad, zeroPad, sizeof(zeroPad)) == 0 &&
            std::memcmp(powerOff.pad, zeroPad, sizeof(zeroPad)) == 0 &&
            std::memcmp(powerOn.pad, zeroPad, sizeof(zeroPad)) == 0,
            "normal driver-availability transitions keep fault detail zeroed");
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
    testTahoePhyModeContracts();
    testTahoeNrateContracts();
    testTahoeBssBlacklistContracts();
    testTahoeLqmContracts();
    testTahoeTxRxChainContracts();
    testTahoeBssManagerWriterContracts();
    testTahoeBssidChangedCarrierLayout();
    testTahoeCapabilityContracts();
    testIwxMfpIgtkContracts();
    testTahoeScanResultLayout();
    testTahoeCurrentNetworkCarrierContract();
    testTahoeBeaconIeBuilder();
    testTahoeBssManagerContracts();
    testTahoeAssociationAuthContracts();
    testTahoeCountryCodeCarrierContracts();
    testTahoeWclAuthAssocCarrierContracts();
    testTahoeDriverAvailabilityContracts();
    std::cout << "tahoe payload builders ok: 30 contracts, 10 builder families, APSTA public setter carriers, Skywalk IOC routes, association RSN/auth, WCL auth/assoc complete, driver-availability lifecycle, BSSID_CHANGED, CARD_CAPABILITIES, scan/current-network layout/renderability, beacon IE stream, driver-owned BssManager, BSS blacklist async owner, OP_MODE, PHY_MODE, nrate, TXRX chain masks, LQM, country-code, AX211 IGTK ABI and BssManager writer contracts covered\n";
    return 0;
}
