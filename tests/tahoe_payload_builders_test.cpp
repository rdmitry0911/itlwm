#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include "AirportItlwm/AirportItlwmAPSTAInterface.hpp"
#include "AirportItlwm/TahoePayloadBuilders.hpp"

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
    require(offsetof(AirportItlwmAPSTAHostApModeHiddenLayout, hidden04) == 4,
            "APSTA hidden setter reads value at input +0x04");
    require(sizeof(AirportItlwmAPSTAHostApModeHiddenLayout) == 8,
            "APSTA hidden setter carrier includes version dword");
    require(kAirportItlwmAPSTAHiddenNotUpReturn == 6,
            "APSTA hidden AP-down return is raw 6");
    require(kAirportItlwmAPSTAHiddenInvalidArgumentReturn == 0x16,
            "APSTA hidden invalid return is raw 0x16");
    require(kAirportItlwmAPSTACsaNotUpReturn == 6,
            "APSTA CSA AP-down return is raw 6");
    require(kAirportItlwmAPSTACsaInvalidArgumentReturn == 0x16,
            "APSTA CSA invalid return is raw 0x16");
    require(kAirportItlwmAPSTAWifiNetworkInfoLengthTrapThreshold == 0x21,
            "APSTA Wi-Fi network info length trap threshold is 0x21");
    require(kAirportItlwmAPSTAWifiNetworkInfoMaxAcceptedLength == 0x20,
            "APSTA Wi-Fi network info accepts lengths below 0x21");
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
    std::cout << "tahoe payload builders ok: 17 contracts, 9 builder families, APSTA public setter carriers covered\n";
    return 0;
}
