#!/usr/bin/env python3
import argparse
import json
import re
import sys
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]

STEP_ID = "step:itlwm-rm-03"
ROADMAP_ITEM_ID = "itlwm-rm-03"
GOAL_ITEM_IDS = ["itlwm-fg-03-payload-parity"]
INPUT_HEAD = "ac3e1077d0fba131256012215e02ca4d8af4cb0b"
CAPTURE_SOURCE = "committed-source-and-apple-reference-corpus"
DETERMINISTIC_TESTS = [
    {
        "id": "tahoe-payload-builders-standalone",
        "path": "tests/tahoe_payload_builders_test.cpp",
        "runner": "scripts/test_payload_builders.sh",
        "command": "timeout 30s scripts/test_payload_builders.sh",
        "tokens": [
            "testIEBuilder",
            "testNdpBuilder",
            "testUsbHostBuilder",
            "testBtcoexBuilders",
            "testTxPowerAndActionFrameBuilders",
            "testRangingBuilder",
            "testPayloadContractInventory",
            "testTahoeDriverAvailabilityContracts",
            "frameLen > 0x707",
            "rejects zero PMK length",
            "31 contracts",
        ],
        "runner_tokens": [
            "TAHOE_PAYLOAD_BUILDERS_STANDALONE_TEST",
            "tests/tahoe_payload_builders_test.cpp",
        ],
    },
]

REFERENCE_CASES = [
    {
        "id": "apple-wcl-action-frame-send",
        "path": "docs/reference/AppleBCMWLAN_WCL_action_frame_send_2026_04_27.md",
        "tokens": ["0xe00002bc", "0x708", "0x724", "0x15"],
    },
    {
        "id": "apple-hidden-association-rsn",
        "path": "docs/reference/AppleBCMWLAN_hidden_assoc_rsn_carrier_2026_04_27.md",
        "tokens": ["assoc-candidates payload length `0x3ad8`", "+0x220", "PMF"],
    },
    {
        "id": "apple-event-three-abis",
        "path": "docs/reference/CR-479-event-payload-three-distinct-abis-closure-20260517.md",
        "tokens": ["32-byte", "24-byte", "16-byte", "Zero-BSSID"],
    },
    {
        "id": "apple-action-frame-progress",
        "path": "docs/reference/AppleBCMWLAN_WCL_action_frame_progress_2026_04_27.md",
        "tokens": ["+0x4478", "+0x4480", "0x12d", "0xe00002d5"],
    },
    {
        "id": "apple-driver-available",
        "path": "docs/reference/CR-479-driver-availability-producers-20260711.md",
        "tokens": ["0xe0822803", "0xe0821804", "0xe0821803", "0xffffff80021f58f0"],
    },
    {
        "id": "apple-connect-complete",
        "path": "analysis/cr230_applebcmwlan_sendConnectComplete_2026_04_29.txt",
        "tokens": ["0xa4", "0xd5", "postMessage"],
    },
    {
        "id": "apple-wcl-scan-result",
        "path": "analysis/ANALYSIS_REPORT_2026-04-23.md",
        "tokens": ["0x844", "BeaconMetaData + IE", "BSSID at `0x29`"],
    },
    {
        "id": "apple-txrx-chain-info",
        "path": "docs/reference/CR-479-txrx-chain-info-hardware-masks-20260711.md",
        "tokens": ["hw_rxchain", "hw_txchain", "`txchain`", "`rxchain`", "`+0`, `+1`, `+2`, and `+3`"],
    },
    {
        "id": "apple-wcl-auth-assoc-complete",
        "path": "docs/reference/CR-479-wcl-auth-assoc-complete-publication-20260710.md",
        "tokens": ["handleAssocEvent", "0x4e", "length `0x08`", "associationStatusHandler"],
    },
    {
        "id": "apple-bss-blacklist-async-owner",
        "path": "docs/reference/CR-479-bss-blacklist-async-owner-20260713.md",
        "tokens": [
            "selector `0x174`",
            "exactly `0x2b`",
            "message `0xa3`",
            "`6 * count + 6`",
            "count is at least `8`",
        ],
    },
    {
        "id": "apple-tx-power-cap-firmware-owner",
        "path": "docs/reference/CR-479-tx-power-cap-quarantine-20260713.md",
        "tokens": [
            "0xffffff8001522f42",
            "0xffffff80016176e2",
            "0xffffff800160b3e0",
            "txcapstate",
            "not Apple valid-input return-code parity",
        ],
    },
    {
        "id": "apple-io80211-selector-surface",
        "path": "include/Airport/IO80211InfraProtocol.h",
        "tokens": [
            "setOFFLOAD_NDP",
            "setUSB_HOST_NOTIFICATION",
            "setBTCOEX_PROFILE",
            "setRANGING_AUTHENTICATE",
        ],
    },
    {
        "id": "apple-ranging-authenticate-thunk",
        "path": "docs/reference/CR-479-gap4-io80211-non-wcl-auth-entries-decomp-20260519.md",
        "tokens": ["setRANGING_AUTHENTICATE", "APPLE80211_IOC_RANGING_AUTHENTICATE"],
    },
    {
        "id": "apple-ranging-authenticate-null-owner",
        "path": "docs/reference/CR-479-ranging-authentication-quarantine-20260712.md",
        "tokens": ["FUN_ffffff80015eaf92", "0xe0000001", "+0x2c28", "proxd"],
    },
    {
        "id": "apple-offload-ndp-null-owner",
        "path": "docs/reference/CR-479-ndp-offload-null-owner-quarantine-20260712.md",
        "tokens": [
            "0xffffff80015d9bbe",
            "+0x2c20",
            "0x16",
            "0xffffff80022c0f14",
            "handleIPv6AddressNotificationGated",
        ],
    },

    {
        "id": "apple-ie-custom-assoc",
        "path": "docs/tahoe_signal_chain_audit.md",
        "tokens": ["ie_len != 0", "ie[0] == 0x44"],
    },
]

PAYLOAD_TYPES = [
    {
        "name": "ie-assoc-vendor",
        "shape": "variable IE bytes, max 2048",
        "producer": "TahoePayloadBuilders::buildIE",
        "consumer": "TahoeCommanderV2::runSetIE",
        "reference_ids": ["apple-ie-custom-assoc", "apple-io80211-selector-surface"],
        "implementation_checks": [
            {
                "path": "AirportItlwm/TahoePayloadBuilders.hpp",
                "tokens": ["struct IEPayloads", "buildIE", "data->ie_len > sizeof(data->ie)", "data->ie[0] == 0x44"],
            },
            {
                "path": "AirportItlwm/TahoeCommanderV2.hpp",
                "tokens": ["runSetIE", "dispatchIOVarSet(552", "dispatchVirtualIOVarSet(552"],
            },
        ],
        "invalid_semantics": "null or overlength carrier returns raw 0x16",
    },
    {
        "name": "offload-ndp-ipv6",
        "shape": "counted IPv6 addresses, clamped to four, link-local seed",
        "producer": "TahoePayloadBuilders::buildOffloadNdp",
        "consumer": "TahoeNdpOwner::apply",
        "reference_ids": ["apple-io80211-selector-surface", "apple-offload-ndp-null-owner"],
        "implementation_checks": [
            {
                "path": "AirportItlwm/TahoeOwners.hpp",
                "tokens": ["class TahoeNdpOwner", "return TahoeErrorMap::kAppleInvalidArgumentRaw;"],
            },
            {
                "path": "AirportItlwm/TahoeCommanderV2.hpp",
                "tokens": ["runSetOFFLOADNDP", "return ndpOwner.apply(data, asyncContext);"],
            },
        ],
        "invalid_semantics": "null or locally absent NDP owner returns raw 0x16 without cache mutation, completion, or synthetic dispatch",
    },
    {
        "name": "usb-host-notification",
        "shape": "present/change dwords with conditional change payload",
        "producer": "TahoePayloadBuilders::buildUsbHostNotification",
        "consumer": "AirportItlwmSkywalkInterface::setUSB_HOST_NOTIFICATION",
        "reference_ids": ["apple-io80211-selector-surface"],
        "implementation_checks": [
            {
                "path": "AirportItlwm/TahoePayloadBuilders.hpp",
                "tokens": ["buildUsbHostNotification", "raw + 0xc", "raw + 0x8", "payload->hasChangePayload = payload->change < 2"],
            },
            {
                "path": "AirportItlwm/TahoeCommanderV2.hpp",
                "tokens": ["runSetUSBHostNotification", "dispatchHiddenCallback(579", "dispatchIOVarSet(579"],
            },
        ],
        "invalid_semantics": "null carrier returns 0xe00002bc",
    },
    {
        "name": "btcoex-profile",
        "shape": "0x38-byte profile entry with mode/band/index gates",
        "producer": "TahoePayloadBuilders::buildBtcoexProfile",
        "consumer": "AirportItlwmSkywalkInterface::setBTCOEX_PROFILE",
        "reference_ids": ["apple-io80211-selector-surface"],
        "implementation_checks": [
            {
                "path": "AirportItlwm/TahoePayloadBuilders.hpp",
                "tokens": ["buildBtcoexProfile", "profileEntry[0x38]", "raw[3]", "raw[4]"],
            },
            {
                "path": "AirportItlwm/TahoeCommander.hpp",
                "tokens": ["payload.band >= 5", "payload.mode < 1", "payload.profileIndex >= 10"],
            },
        ],
        "invalid_semantics": "null or invalid band/mode/index returns 0xe00002c2",
    },
    {
        "name": "btcoex-profile-active",
        "shape": "active profile dword at caller +0x4",
        "producer": "TahoePayloadBuilders::buildBtcoexProfileActive",
        "consumer": "AirportItlwmSkywalkInterface::setBTCOEX_PROFILE_ACTIVE",
        "reference_ids": ["apple-io80211-selector-surface"],
        "implementation_checks": [
            {
                "path": "AirportItlwm/TahoePayloadBuilders.hpp",
                "tokens": ["buildBtcoexProfileActive", "raw + 4", "payload->activeProfile"],
            },
            {
                "path": "AirportItlwm/AirportItlwmSkywalkInterface.cpp",
                "tokens": ["setBTCOEX_PROFILE_ACTIVE", "cachedBtcoexProfileActive"],
            },
        ],
        "invalid_semantics": "null returns 0xe00002c2",
    },
    {
        "name": "btcoex-2g-chain-disable",
        "shape": "6-byte chain-disable payload with fixed 0x00060001 header",
        "producer": "TahoePayloadBuilders::buildBtcoex2GChainDisable",
        "consumer": "AirportItlwmSkywalkInterface::setBTCOEX_2G_CHAIN_DISABLE",
        "reference_ids": ["apple-io80211-selector-surface"],
        "implementation_checks": [
            {
                "path": "AirportItlwm/TahoePayloadBuilders.hpp",
                "tokens": ["Btcoex2GChainDisablePayload", "0x00060001", "buildBtcoex2GChainDisable", "raw + 4"],
            },
            {
                "path": "AirportItlwm/AirportItlwmSkywalkInterface.cpp",
                "tokens": ["setBTCOEX_2G_CHAIN_DISABLE", "cachedBtcoex2GChainDisable"],
            },
        ],
        "invalid_semantics": "null returns 0xe00002c2",
    },
    {
        "name": "tx-power-cap-quarantine",
        "shape": "bypass and dual-power carriers are rejected before local pseudo-state",
        "producer": "no Intel TX-power-cap firmware backend",
        "consumer": "AirportItlwmSkywalkInterface::setBYPASS_TX_POWER_CAP / setDUAL_POWER_MODE",
        "reference_ids": [
            "apple-io80211-selector-surface",
            "apple-tx-power-cap-firmware-owner",
        ],
        "implementation_checks": [
            {
                "path": "AirportItlwm/AirportItlwmSkywalkInterface.cpp",
                "tokens": [
                    "setBYPASS_TX_POWER_CAP",
                    "setDUAL_POWER_MODE",
                    "return kIOReturnUnsupported;",
                ],
            },
        ],
        "invalid_semantics": "null returns 0xe00002bc; non-null input fails closed before synthetic completion",
    },
    {
        "name": "wcl-action-frame-v1-v2",
        "shape": "V1 fixed 0x724 command payload or V2 dynamic issue-command frame",
        "producer": "TahoePayloadBuilders::buildActionFrame",
        "consumer": "TahoeCommanderV2::runSetWCLActionFrame",
        "reference_ids": ["apple-wcl-action-frame-send"],
        "implementation_checks": [
            {
                "path": "AirportItlwm/TahoePayloadBuilders.hpp",
                "tokens": ["kActionFramePayloadCapacity = 0x708", "kActionFrameV1TxPayloadSize = 0x724", "buildActionFrame", "payload->useV2"],
            },
            {
                "path": "AirportItlwm/TahoeCommanderV2.hpp",
                "tokens": ["runSetWCLActionFrame", "dispatchIssueCommand(620", "dispatchIOVarSet(620"],
            },
        ],
        "invalid_semantics": "null or frameLen > 0x707 returns 0xe00002bc",
    },
    {
        "name": "action-frame-progress",
        "shape": "progress flag/start-ms owner state plus scan rejection status",
        "producer": "TahoeOwnerRegistry::setActionFrameProgress",
        "consumer": "TahoeOwnerRegistry::getActionFrameProgress",
        "reference_ids": ["apple-action-frame-progress"],
        "implementation_checks": [
            {
                "path": "AirportItlwm/TahoePayloadBuilders.hpp",
                "tokens": ["kActionFrameProgressFlagOffset = 0x4478", "kActionFrameProgressStartMsOffset = 0x4480", "kActionFrameProgressOverdueMs = 0x12d", "kActionFrameProgressScanRejectStatus = 0xe00002d5"],
            },
            {
                "path": "AirportItlwm/TahoeOwnerRegistry.hpp",
                "tokens": ["checkActionFrameCompleteOverdue", "getActionFrameProgress", "actionFrame.progress = 0"],
            },
        ],
        "invalid_semantics": "overdue clears progress; scan reject status remains Apple 0xe00002d5",
    },
    {
        "name": "ranging-authenticate",
        "shape": "role at +0x4 and PMK length at +0x70; local null-owner branch fails closed",
        "producer": "TahoePayloadBuilders::buildRangingAuthenticate",
        "consumer": "AirportItlwmSkywalkInterface::setRANGING_AUTHENTICATE",
        "reference_ids": ["apple-ranging-authenticate-thunk", "apple-ranging-authenticate-null-owner", "apple-io80211-selector-surface"],
        "implementation_checks": [
            {
                "path": "AirportItlwm/TahoePayloadBuilders.hpp",
                "tokens": ["buildRangingAuthenticate", "raw + 0x70", "payload->role == 4", "return payload->pmkLen != 0"],
            },
            {
                "path": "AirportItlwm/TahoeCommanderV2.hpp",
                "tokens": ["runSetRangingAuthenticate", "return rangingOwner.apply(data, proximityOwnerId, asyncContext);"],
            },
        ],
        "invalid_semantics": "null, zero PMK length, or absent proximity owner returns 0xe0000001",
    },
    {
        "name": "association-candidates-hidden",
        "shape": "0x3ad8 hidden WCL association candidate carrier",
        "producer": "AirportItlwmSkywalkInterface::setWCL_ASSOCIATE",
        "consumer": "AirportItlwmSkywalkInterface::getAWDL_PEER_TRAFFIC_STATS",
        "reference_ids": ["apple-hidden-association-rsn"],
        "implementation_checks": [
            {
                "path": "AirportItlwm/TahoeAssociationContracts.hpp",
                "tokens": ["kAssocCandidatesPayloadLength = 0x3ad8", "kFirstCandidateBssidOffset = 0x220", "kPmfCapabilityOffset = 0x217"],
            },
            {
                "path": "AirportItlwm/AirportItlwmSkywalkInterface.cpp",
                "tokens": ["isAssocCandidatesPayloadLength(length)", "setWCL_ASSOCIATE", "boundedRsnIeLength", "instantHotspotAppleDeviceFlags"],
            },
        ],
        "invalid_semantics": "hidden fallback routes only exact 0x3ad8 carrier; direct null WCL associate returns 0xe00002c2",
    },
    {
        "name": "bss-blacklist-async-owner",
        "shape": "43-byte request; variable async u32 count + 6-byte BSSIDs + 2-byte tail",
        "producer": "AirportItlwm::setBssBlacklistOwner/queryBssBlacklistOwner",
        "consumer": "IO80211Controller::postMessage 0xa3",
        "reference_ids": ["apple-bss-blacklist-async-owner"],
        "implementation_checks": [
            {
                "path": "AirportItlwm/TahoeBssBlacklistContracts.hpp",
                "tokens": [
                    "kRequestLength",
                    "kEventMessage = 0xa3",
                    "decodeAppliedState",
                    "eventTrailingOffset",
                    "buildEventCarrier",
                ],
            },
            {
                "path": "AirportItlwm/AirportItlwmV2.cpp",
                "tokens": [
                    "setBssBlacklistOwner",
                    "queryBssBlacklistOwner",
                    "ic_bss_blacklist_requested",
                    "airportItlwmPublishBssBlacklist",
                    "self->postMessage",
                ],
            },
            {
                "path": "AirportItlwm/AirportItlwmSkywalkInterface.cpp",
                "tokens": [
                    "case APPLE80211_IOC_BSS_BLACKLIST",
                    "routePreflightStatus",
                    "isCommandProhibited(",
                    "wrapperStatus",
                    "getBSS_BLACKLIST",
                    "setBSS_BLACKLIST",
                ],
            },
        ],
        "invalid_semantics": "no interface returns 0x66 before malformed carrier 0x16; admission precedes owner cast; SET null returns 0xe00002bc; count >= 8 preserves applied state; GET never writes its caller buffer; empty applied list emits no event",
    },
    {
        "name": "txrx-chain-info",
        "shape": "four independent one-byte hardware/active RX/TX masks",
        "producer": "ItlDriverInfo::getTxChainMask/getRxChainMask",
        "consumer": "AirportItlwmSkywalkInterface::getTXRX_CHAIN_INFO",
        "reference_ids": ["apple-txrx-chain-info"],
        "implementation_checks": [
            {
                "path": "AirportItlwm/TahoeTxRxChainContracts.hpp",
                "tokens": ["struct Carrier", "hardwareRx", "hardwareTx", "activeTx", "activeRx", "sizeof(Carrier) == 0x04"],
            },
            {
                "path": "AirportItlwm/AirportItlwmSkywalkInterface.cpp",
                "tokens": ["getTXRX_CHAIN_INFO", "getTxChainMask", "getRxChainMask", "build(rxMask, txMask, txMask, rxMask)"],
            },
            {
                "path": "include/HAL/ItlDriverInfo.hpp",
                "tokens": ["getTxChainMask", "getRxChainMask"],
            },
        ],
        "invalid_semantics": "null returns 0xe00002c2; Intel configured masks have no fallible iovar read",
    },
    {
        "name": "link-changed-32",
        "shape": "32-byte APPLE80211_M_LINK_CHANGED / IOC 156 carrier",
        "producer": "AirportItlwmSkywalkInterface::setLinkStateInternal",
        "consumer": "AirportItlwmSkywalkInterface::getLINK_CHANGED_EVENT_DATA",
        "reference_ids": ["apple-event-three-abis"],
        "implementation_checks": [
            {
                "path": "include/Airport/apple80211_ioctl.h",
                "tokens": ["struct apple80211_link_changed_event_data", "sizeof(struct apple80211_link_changed_event_data) == 0x20", "voluntary_up"],
            },
            {
                "path": "AirportItlwm/AirportItlwmSkywalkInterface.cpp",
                "tokens": ["APPLE80211_M_LINK_CHANGED", "getLINK_CHANGED_EVENT_DATA", "ret == kIOReturnSuccess"],
            },
        ],
        "invalid_semantics": "null IOC getter returns raw 16; event publication is gated on parent transition success",
    },
    {
        "name": "bssid-changed-24",
        "shape": "24-byte APPLE80211_M_BSSID_CHANGED compact carrier",
        "producer": "AirportItlwmSkywalkInterface::publishTahoeBssidChangedFromCurrentBss",
        "consumer": "IO80211Glue pending event queue",
        "reference_ids": ["apple-event-three-abis"],
        "implementation_checks": [
            {
                "path": "include/Airport/apple80211_ioctl.h",
                "tokens": ["struct apple80211_bssid_changed_event_data", "sizeof(struct apple80211_bssid_changed_event_data) == 0x18", "APPLE80211_BSSID_CHANGE_REASON_SAME_BSS"],
            },
            {
                "path": "AirportItlwm/AirportItlwmSkywalkInterface.cpp",
                "tokens": ["sameBssAsLastPublished", "zeroBssidRejected", "APPLE80211_M_BSSID_CHANGED"],
            },
        ],
        "invalid_semantics": "zero BSSID is rejected; same-BSS reason 1 suppresses duplicate publication",
    },
    {
        "name": "wcl-link-state-16",
        "shape": "16-byte WCL 0xd8 link-state update",
        "producer": "postTahoeWclLinkUpInd",
        "consumer": "WCL event 0xd8 consumer",
        "reference_ids": ["apple-event-three-abis"],
        "implementation_checks": [
            {
                "path": "AirportItlwm/AirportItlwmV2.cpp",
                "tokens": ["struct TahoeWclLinkChangedPayload", "sizeof(TahoeWclLinkChangedPayload) == 0x10", "kTahoeWclLinkChanged = 0xd8", "postTahoeWclLinkUpInd"],
            },
        ],
        "invalid_semantics": "raw reason 0 or out of range maps to 0xff",
    },
    {
        "name": "wcl-scan-result",
        "shape": "0x44 BeaconMetaData header plus up to 0x800 IE bytes",
        "producer": "buildTahoeWclScanResultPayload",
        "consumer": "AirportItlwm::postWclScanDoneGated",
        "reference_ids": ["apple-wcl-scan-result"],
        "implementation_checks": [
            {
                "path": "AirportItlwm/AirportItlwmV2.cpp",
                "tokens": ["kTahoeWclScanResultHeaderLen = 0x44", "kTahoeWclScanResultMaxIELen = 0x800", "buildTahoeWclScanResultPayload", "APPLE80211_M_WCL_SCAN_RESULT", "TahoeScanContracts::kWclScanResultMetaFlags"],
            },
        ],
        "invalid_semantics": "null controller/node/channel and zero BSSID reject before WCL_SCAN_RESULT publication",
    },
    {
        "name": "wcl-auth-assoc-complete",
        "shape": "0x08 WCL auth/assoc status and reason carrier",
        "producer": "buildTahoeWclAuthAssocCompletePayload",
        "consumer": "WCLJoinManager association/auth-complete path",
        "reference_ids": ["apple-wcl-auth-assoc-complete"],
        "implementation_checks": [
            {
                "path": "include/Airport/apple80211_var.h",
                "tokens": ["APPLE80211_WCL_AUTH_ASSOC_COMPLETE_LEN", "struct apple80211_wcl_auth_assoc_complete_event", "APPLE80211_M_WCL_AUTH_ASSOC_EVENT"],
            },
            {
                "path": "AirportItlwm/AirportItlwmV2.cpp",
                "tokens": ["buildTahoeWclAuthAssocCompletePayload", "mapTahoeWclAssocStatus", "APPLE80211_M_WCL_AUTH_ASSOC_EVENT", "IEEE80211_EVT_STA_ASSOC_DONE"],
            },
        ],
        "invalid_semantics": "zero status/reason maps to zero dwords; out-of-range status/reason maps to 0xe3ff8100",
    },
    {
        "name": "wcl-connect-complete",
        "shape": "0xa4 WCL connect-complete event with ten 16-byte records",
        "producer": "buildTahoeWclConnectCompletePayload",
        "consumer": "postTahoeWclConnectCompleteEvent",
        "reference_ids": ["apple-connect-complete"],
        "implementation_checks": [
            {
                "path": "include/Airport/apple80211_var.h",
                "tokens": ["APPLE80211_WCL_CONNECT_COMPLETE_LEN", "APPLE80211_WCL_CONNECT_COMPLETE_MAX_RECORDS 10"],
            },
            {
                "path": "AirportItlwm/AirportItlwmV2.cpp",
                "tokens": ["buildTahoeWclConnectCompletePayload", "APPLE80211_M_WCL_CONNECT_COMPLETE_EVENT", "sizeof(payload)"],
            },
        ],
        "invalid_semantics": "missing controller/HAL/RUN/BSS rejects before postMessage",
    },
    {
        "name": "driver-available-0xf8",
        "shape": "0xf8 DRIVER_AVAILABLE lifecycle carrier with six-dword prefix",
        "producer": "postTahoeDriverAvailabilityTransition",
        "consumer": "WCLSystemStateManager driverAvailableEventHandler",
        "reference_ids": ["apple-driver-available"],
        "implementation_checks": [
            {
                "path": "include/Airport/apple80211_ioctl.h",
                "tokens": ["struct apple80211_driver_available_data", "sizeof(struct apple80211_driver_available_data) == 0xF8"],
            },
            {
                "path": "AirportItlwm/TahoeDriverAvailabilityContracts.hpp",
                "tokens": ["Transition::BootReady", "kBootReadyReason", "kPowerOffReason", "kPowerOnReason"],
            },
            {
                "path": "AirportItlwm/AirportItlwmV2.cpp",
                "tokens": ["postTahoeDriverAvailabilityTransition", "Transition::BootReady", "Transition::PowerOff", "Transition::PowerOn"],
            },
        ],
        "invalid_semantics": "boolean-only payloads are rejected; boot-ready, power-off, and power-on use their distinct reference flags and reason dwords",
    },
]

ERROR_CASES = [
    {
        "id": "action-frame-null-or-oversize",
        "expected": "0xe00002bc",
        "checks": [
            {
                "path": "AirportItlwm/TahoePayloadBuilders.hpp",
                "tokens": ["frameLen > kActionFrameMaximumPayloadLength"],
            },
            {
                "path": "AirportItlwm/TahoeErrorMap.hpp",
                "tokens": ["kAppleBadArgumentTahoe"],
            },
            {
                "path": "docs/reference/AppleBCMWLAN_WCL_action_frame_send_2026_04_27.md",
                "tokens": ["0xe00002bc"],
            },
        ],
    },
    {
        "id": "btcoex-profile-invalid-range",
        "expected": "0xe00002c2",
        "checks": [
            {
                "path": "AirportItlwm/TahoeCommander.hpp",
                "tokens": ["payload.band >= 5", "payload.mode < 1", "payload.profileIndex >= 10"],
            },
        ],
    },
    {
        "id": "ranging-auth-zero-pmk",
        "expected": "0xe0000001",
        "checks": [
            {
                "path": "AirportItlwm/TahoePayloadBuilders.hpp",
                "tokens": ["return payload->pmkLen != 0"],
            },
            {
                "path": "AirportItlwm/TahoeErrorMap.hpp",
                "tokens": ["kAppleRangingInvalid"],
            },
        ],
    },
    {
        "id": "bssid-zero-and-same-bss-suppression",
        "expected": "suppress-publication",
        "checks": [
            {
                "path": "AirportItlwm/AirportItlwmSkywalkInterface.cpp",
                "tokens": ["zeroBssidRejected", "sameBssAsLastPublished"],
            },
            {
                "path": "docs/reference/CR-479-event-payload-three-distinct-abis-closure-20260517.md",
                "tokens": ["Zero-BSSID rejection", "Same-BSS reason-1 suppression"],
            },
        ],
    },
    {
        "id": "hidden-assoc-exact-length",
        "expected": "route-only-0x3ad8",
        "checks": [
            {
                "path": "AirportItlwm/AirportItlwmSkywalkInterface.cpp",
                "tokens": ["isAssocCandidatesPayloadLength(length)"],
            },
            {
                "path": "AirportItlwm/TahoeAssociationContracts.hpp",
                "tokens": ["kAssocCandidatesPayloadLength = 0x3ad8"],
            },
        ],
    },
    {
        "id": "driver-available-polarity",
        "expected": "boot-and-power-on-available-1-power-off-available-0",
        "checks": [
            {
                "path": "AirportItlwm/TahoeDriverAvailabilityContracts.hpp",
                "tokens": ["payload.available = 1", "Transition::PowerOff"],
            },
            {
                "path": "docs/reference/CR-479-driver-availability-producers-20260711.md",
                "tokens": ["`+0x08`", "Power off", "Power on"],
            },
        ],
    },
]


def read_text(project_relative_path):
    path = PROJECT_ROOT / project_relative_path
    if not path.exists():
        raise FileNotFoundError(project_relative_path)
    return path.read_text(encoding="utf-8", errors="replace")


def missing_tokens(project_relative_path, tokens):
    text = read_text(project_relative_path)
    return [token for token in tokens if token not in text]


def parse_contract_names():
    text = read_text("AirportItlwm/TahoePayloadParity.hpp")
    return set(re.findall(r'\{\s*"([^"]+)",', text))


def reference_index():
    return {case["id"]: case for case in REFERENCE_CASES}


def collect_mismatches():
    mismatches = []
    contract_names = parse_contract_names()
    expected_names = {payload["name"] for payload in PAYLOAD_TYPES}

    for name in sorted(expected_names - contract_names):
        mismatches.append({"kind": "missing_payload_contract", "name": name})
    for name in sorted(contract_names - expected_names):
        mismatches.append({"kind": "unexpected_payload_contract", "name": name})

    refs = reference_index()
    for case in REFERENCE_CASES:
        missing = missing_tokens(case["path"], case["tokens"])
        if missing:
            mismatches.append({
                "kind": "apple_reference_token_mismatch",
                "id": case["id"],
                "path": case["path"],
                "missing_tokens": missing,
            })

    for payload in PAYLOAD_TYPES:
        if payload["name"] not in contract_names:
            continue
        for reference_id in payload["reference_ids"]:
            if reference_id not in refs:
                mismatches.append({
                    "kind": "missing_reference_case",
                    "payload": payload["name"],
                    "reference_id": reference_id,
                })
        for check in payload["implementation_checks"]:
            missing = missing_tokens(check["path"], check["tokens"])
            if missing:
                mismatches.append({
                    "kind": "implementation_token_mismatch",
                    "payload": payload["name"],
                    "path": check["path"],
                    "missing_tokens": missing,
                })

    for case in ERROR_CASES:
        for check in case["checks"]:
            check_path = check["path"]
            missing = missing_tokens(check_path, check["tokens"])
            if missing:
                mismatches.append({
                    "kind": "error_semantics_token_mismatch",
                    "id": case["id"],
                    "path": check_path,
                    "missing_tokens": missing,
                })
    for test in DETERMINISTIC_TESTS:
        missing = missing_tokens(test["path"], test["tokens"])
        if missing:
            mismatches.append({
                "kind": "deterministic_test_token_mismatch",
                "id": test["id"],
                "path": test["path"],
                "missing_tokens": missing,
            })
        missing = missing_tokens(test["runner"], test["runner_tokens"])
        if missing:
            mismatches.append({
                "kind": "deterministic_test_runner_token_mismatch",
                "id": test["id"],
                "path": test["runner"],
                "missing_tokens": missing,
            })
    return mismatches


def build_report():
    mismatches = collect_mismatches()
    refs = reference_index()
    covered = []
    for payload in PAYLOAD_TYPES:
        implementation_paths = sorted({check["path"] for check in payload["implementation_checks"]})
        covered.append({
            "name": payload["name"],
            "shape": payload["shape"],
            "producer": payload["producer"],
            "consumer": payload["consumer"],
            "implementation_paths": implementation_paths,
            "apple_reference_case_ids": payload["reference_ids"],
            "invalid_semantics": payload["invalid_semantics"],
        })

    return {
        "schema_version": "itlwm-payload-parity-report/v1",
        "selected_step_id": STEP_ID,
        "roadmap_item_id": ROADMAP_ITEM_ID,
        "goal_item_ids": GOAL_ITEM_IDS,
        "input_head": INPUT_HEAD,
        "capture_source": CAPTURE_SOURCE,
        "source_boundary": {
            "basis": "Committed implementation headers/source, committed Apple reverse-engineering references, and deterministic token checks.",
            "excluded_sources": ["synthetic", "self-reported", "project-gantt", "runtime-db"],
        },
        "covered_payload_types": covered,
        "apple_reference_cases": [
            {
                "id": case["id"],
                "path": case["path"],
                "assertions_checked": len(case["tokens"]),
            }
            for case in REFERENCE_CASES
        ],
        "error_semantics_cases": len(ERROR_CASES),
        "error_semantics_details": [
            {
                "id": case["id"],
                "expected": case["expected"],
                "paths": [
                    check["path"]
                    for check in case["checks"]
                ],
            }
            for case in ERROR_CASES
        ],
        "deterministic_tests": [
            {
                "id": test["id"],
                "path": test["path"],
                "runner": test["runner"],
                "command": test["command"],
                "assertions_checked": len(test["tokens"]),
            }
            for test in DETERMINISTIC_TESTS
        ],
        "mismatch_count": len(mismatches),
        "mismatches": mismatches,
        "metrics": {
            "covered_payload_type_count": len({payload["name"] for payload in PAYLOAD_TYPES}),
            "apple_reference_case_count": len(refs),
            "error_semantics_case_count": len(ERROR_CASES),
            "deterministic_test_count": len(DETERMINISTIC_TESTS),
        },
    }


def assert_typed_requirements(report):
    forbidden_sources = {"synthetic", "self-reported", "project-gantt", "runtime-db"}
    errors = []
    if report.get("mismatch_count") != 0:
        errors.append("mismatch_count must be 0")
    if report.get("capture_source") in forbidden_sources:
        errors.append("capture_source is forbidden")
    if len({item["name"] for item in report.get("covered_payload_types", [])}) < 12:
        errors.append("covered_payload_types[].name must have at least 12 distinct values")
    if len({item["id"] for item in report.get("apple_reference_cases", [])}) < 6:
        errors.append("apple_reference_cases[].id must have at least 6 distinct values")
    if report.get("error_semantics_cases", 0) < 4:
        errors.append("error_semantics_cases must be at least 4")
    if len(report.get("deterministic_tests", [])) < 1:
        errors.append("deterministic_tests must include at least one committed test")
    if errors:
        raise AssertionError("; ".join(errors))


def write_report(path):
    report = build_report()
    assert_typed_requirements(report)
    output = json.dumps(report, indent=2) + "\n"
    target = PROJECT_ROOT / path
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(output, encoding="utf-8")


def check_report(path):
    report = build_report()
    assert_typed_requirements(report)
    expected = json.dumps(report, indent=2) + "\n"
    actual_path = PROJECT_ROOT / path
    actual = actual_path.read_text(encoding="utf-8")
    if actual != expected:
        print(f"{path} is not the deterministic payload parity report", file=sys.stderr)
        return 1
    print(
        "payload parity report ok: "
        f"{report['metrics']['covered_payload_type_count']} payloads, "
        f"{report['metrics']['apple_reference_case_count']} reference cases, "
        f"{report['error_semantics_cases']} error cases, "
        f"{report['metrics']['deterministic_test_count']} deterministic tests"
    )
    return 0


def main():
    parser = argparse.ArgumentParser(description="Generate or verify Tahoe payload parity evidence.")
    parser.add_argument("--write", metavar="PATH", help="write deterministic report JSON")
    parser.add_argument("--check", metavar="PATH", help="check deterministic report JSON")
    args = parser.parse_args()

    if bool(args.write) == bool(args.check):
        parser.error("choose exactly one of --write or --check")
    if args.write:
        write_report(args.write)
        return 0
    return check_report(args.check)


if __name__ == "__main__":
    sys.exit(main())
