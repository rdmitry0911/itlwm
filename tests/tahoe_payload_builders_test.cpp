#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include "AirportItlwm/AirportItlwmAPSTAInterface.hpp"
#include "AirportItlwm/TahoeNrateContracts.hpp"
#include "AirportItlwm/TahoePayloadBuilders.hpp"
#include "AirportItlwm/TahoeQosDynsarContracts.hpp"
#include "AirportItlwm/TahoeSkywalkIoctlRoutes.hpp"

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

void testApstaPublicSetterContracts()
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
    require(!shouldRoute(kIocPeerCacheControl, false) &&
                shouldRoute(kIocPeerCacheControl, true),
            "Skywalk leaves peer-cache maximum/shared getters out of the APSTA route");
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
                probe.guard_interval == 400,
            "nrate VHT carrier decodes index, NSS, bandwidth, and guard interval");

    McsVhtProbe nonVht{};
    require(!fillMcsVhtFromNrate(kFamilyHt | 0x00010000 | 0x03, &nonVht),
            "nrate non-VHT family leaves MCS_VHT carrier untouched");
    require(nonVht.index == 0 && nonVht.nss == 0 && nonVht.bw == 0 &&
                nonVht.guard_interval == 0,
            "nrate non-VHT carrier remains zeroed");
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
    testApstaPublicSetterContracts();
    testPayloadContractInventory();
    testTahoeSkywalkIoctlRoutes();
    testTahoeQosDynsarContracts();
    testTahoeNrateContracts();
    std::cout << "tahoe payload builders ok: 17 contracts, 9 builder families, APSTA public setter carriers, Skywalk IOC routes and nrate contracts covered\n";
    return 0;
}
