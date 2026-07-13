#!/usr/bin/env python3
"""Generate and verify Tahoe BSS blacklist async-owner evidence."""

import argparse
import hashlib
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/bss_blacklist_async_owner_report.json"
NOTE = ROOT / "docs/reference/CR-479-bss-blacklist-async-owner-20260713.md"
CONTRACT = ROOT / "AirportItlwm/TahoeBssBlacklistContracts.hpp"
CONTROLLER = ROOT / "AirportItlwm/AirportItlwmV2.cpp"
INTERFACE = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
NET80211 = ROOT / "itl80211/openbsd/net80211/ieee80211_var.h"
TEST = ROOT / "tests/tahoe_payload_builders_test.cpp"
ARTIFACT_DIR = ROOT / "docs/reference/artifacts/bss-blacklist-25C56"

ARTIFACT_SHA256 = {
    "aiam_bss_blacklist_core_helper_raw_exact_current_25C56_20260713.txt.gz": "d1eaf424a708d745413823807c3b142802d18837900ecebe098f8c1638b02138",
    "aiam_bss_blacklist_disasm_25C56_20260712.txt.gz": "45352774702a1af33e6fe451b845a3d55c164ba420deadcc0368405d7ca09d6e",
    "aiam_bss_blacklist_get_route_raw_current_25C56_20260713.txt.gz": "29a71bb786a0492e3c17ec5b88deec04971c5400fa55166a10224d3ce91fae1b",
    "aiam_bss_blacklist_get_wrapper_raw_current_25C56_20260713.txt.gz": "68fb2bc8be6e45dd1f6ad389cefa3bb8af6b1b6109d2f1c8a032ae3f8b3b1a39",
    "aiam_bss_blacklist_helper_listing_25C56_20260712.txt.gz": "70d11c13d9ed71e33fb8d94d0bddd771c179b6940cf8ee173a655949486f0c6c",
    "aiam_bss_blacklist_isbssid_decoded_pointer_scan_current_25C56_20260713.txt.gz": "17e1fcf4eb056b886202d5f74d5be7963eb837447eae4913ddb6f33c5b64618a",
    "aiam_bss_blacklist_set_route_raw_current_25C56_20260713.txt.gz": "bd4fa4027266c18a1c369f005ba2f0d7a3787122a5e98b3c2f1faaa7adbb0ff5",
    "aiam_bss_blacklist_set_wrapper_raw_current_25C56_20260713.txt.gz": "568400b60ce76e0d4097727bdcb5ccf02f3d522da01cc9c4df65d969f44f9adc",
    "aiam_bss_blacklist_wcl_fill_block_raw_current_25C56_20260713.txt.gz": "5e4ba345adb42aa2eee667703b9fdff18648ebdb42bc8c10dc0fc72b7beb8c0b",
    "aiam_bss_blacklist_wcl_joinrequest_flag_strings_current_25C56_20260713.txt.gz": "ce7b0202d69092240d7c3d3a04b278897e612c44912a74d039a19b993f629fe7",
    "aiam_bss_blacklist_wcl_sort_block_raw_current_25C56_20260713.txt.gz": "3488b73622842ee78feeb7cd5569d6a0a9658434ce39c954e4882b785dd9cff0",
}

TOKENS = {
    "note": [
        "selector `0x174`",
        "exactly `0x2b`",
        "raw `0x66`",
        "`0xe082280e`",
        "message `0xa3`",
        "`6 * count + 6`",
        "count is at least `8`",
        "does not claim Intel firmware MACLIST support",
    ],
    "contract": [
        "kRequestLength",
        "routePreflightStatus",
        "localAdmissionStatus",
        "wrapperStatus",
        "kNoInterfaceStatus",
        "kClassOwnerAbsentStatus",
        "kEventMessage = 0xa3",
        "decodeAppliedState",
        "eventTrailingOffset",
        "buildEventCarrier",
    ],
    "controller": [
        "ic_bss_blacklist_requested",
        "decodeAppliedState",
        "airportItlwmPublishBssBlacklist",
        "setBssBlacklistOwner",
        "queryBssBlacklistOwner",
        "self->postMessage",
    ],
    "interface": [
        "case APPLE80211_IOC_BSS_BLACKLIST",
        "routePreflightStatus",
        "isCommandProhibited(",
        "wrapperStatus",
        "getBSS_BLACKLIST",
        "setBSS_BLACKLIST",
    ],
    "net80211": [
        "ic_bss_blacklist_requested[43]",
        "ic_bss_blacklist_count",
        "ic_bss_blacklist_bssid[7][IEEE80211_ADDR_LEN]",
        "ic_bss_blacklist_event_body",
    ],
    "test": [
        "testTahoeBssBlacklistContracts",
        "BSS route returns 0x66 before malformed carrier validation",
        "BSS wrapper returns nonzero admission before owner cast",
        "BSS wrapper maps failed owner cast to 0xe082280e",
        "invalid BSS blacklist request leaves applied state unchanged",
        "one-entry BSS blacklist result publishes 12 bytes",
        "full BSS blacklist result keeps its tail at bytes 46 and 47",
    ],
}


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def missing_tokens(path, tokens):
    text = path.read_text(encoding="utf-8")
    return [token for token in tokens if token not in text]


def build_report():
    sources = {
        "note": NOTE,
        "contract": CONTRACT,
        "controller": CONTROLLER,
        "interface": INTERFACE,
        "net80211": NET80211,
        "test": TEST,
    }
    missing = {
        name: missing_tokens(sources[name], TOKENS[name]) for name in sources
    }
    artifacts = {}
    for name, expected in sorted(ARTIFACT_SHA256.items()):
        path = ARTIFACT_DIR / name
        actual = sha256(path) if path.exists() else None
        artifacts[name] = {
            "expected_sha256": expected,
            "actual_sha256": actual,
            "matches": actual == expected,
        }

    return {
        "schema": "itlwm-bss-blacklist-async-owner-v2",
        "source_base_revision": "4b2cea995201492fde5a23554d6cd3ca34ff0583",
        "reference": {
            "build": "macOS 26.2 (25C56)",
            "selector": "0x174",
            "request_length": 43,
            "event": "0xa3",
            "event_length": "6 * count + 6",
            "get_writes_sync_buffer": False,
            "invalid_count_preserves_applied": True,
            "no_interface_status": "0x66",
            "invalid_carrier_status": "0x16",
            "class_owner_absent_status": "0xe082280e",
            "admission_before_owner_cast": True,
        },
        "local": {
            "controller_owned_requested_state": True,
            "controller_owned_applied_state": True,
            "command_gated": True,
            "async_event_owner": True,
            "public_route_p0": True,
            "scan_results_hidden": False,
            "hard_exclusion_claimed": False,
        },
        "checks": {
            **{name: not values for name, values in missing.items()},
            "artifact_hashes": all(item["matches"] for item in artifacts.values()),
        },
        "missing_tokens": missing,
        "artifacts": artifacts,
    }


def validate(report):
    failures = [name for name, passed in report["checks"].items() if not passed]
    if failures:
        raise ValueError("BSS blacklist async-owner checks failed: " + ", ".join(failures))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--write", action="store_true")
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--output", type=Path, default=OUTPUT)
    args = parser.parse_args()
    if args.write == args.check:
        parser.error("select exactly one of --write or --check")

    report = build_report()
    validate(report)
    rendered = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.write:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered, encoding="utf-8")
    else:
        if not args.output.exists() or args.output.read_text(encoding="utf-8") != rendered:
            raise ValueError("checked-in report differs; rerun with --write")
    print(rendered, end="")


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"BSS blacklist async-owner validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
