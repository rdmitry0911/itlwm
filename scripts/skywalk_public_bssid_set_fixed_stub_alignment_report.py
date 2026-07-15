#!/usr/bin/env python3
"""Generate and verify Skywalk public BSSID SET fixed-stub evidence."""

import argparse
import hashlib
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/skywalk_public_bssid_set_fixed_stub_alignment_report.json"
NOTE = ROOT / "docs/reference/CR-532-skywalk-public-bssid-set-fixed-stub-alignment-20260715.md"
RAW = ROOT / "docs/reference/artifacts/skywalk-bssid-set-public-fixed-stub-bootkc-current/raw.txt"
RAW_MANIFEST = RAW.with_name("SHA256SUMS.txt")
CPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
V2 = ROOT / "AirportItlwm/AirportItlwmV2.cpp"
V1 = ROOT / "AirportItlwm/AirportSTAIOCTL.cpp"
ROUTES = ROOT / "AirportItlwm/TahoeSkywalkIoctlRoutes.hpp"
IOCTL = ROOT / "include/Airport/apple80211_ioctl.h"
PROJECT = ROOT / "itlwm.xcodeproj/project.pbxproj"


def section(source, begin, end):
    start = source.index(begin)
    return source[start:source.index(end, start)]


def report():
    cpp = CPP.read_text(encoding="utf-8")
    v2 = V2.read_text(encoding="utf-8")
    v1 = V1.read_text(encoding="utf-8")
    routes = ROUTES.read_text(encoding="utf-8")
    ioctl = IOCTL.read_text(encoding="utf-8")
    project = PROJECT.read_text(encoding="utf-8")
    note = " ".join(NOTE.read_text(encoding="utf-8").split())
    raw = RAW.read_text(encoding="utf-8")
    manifest = RAW_MANIFEST.read_text(encoding="utf-8")
    raw_digest = hashlib.sha256(RAW.read_bytes()).hexdigest()

    bsd_bridge = section(
        cpp,
        "processBSDCommand(ifnet_t interface, UInt cmd, void *data)",
        "processApple80211Ioctl(UInt cmd, apple80211req *req)",
    )
    dispatcher = section(
        cpp,
        "processApple80211Ioctl(UInt cmd, apple80211req *req)",
        "IOReturn AirportItlwmSkywalkInterface::\ngetAUTH_TYPE",
    )
    bssid_route = section(
        dispatcher,
        "case APPLE80211_IOC_BSSID:",
        "case APPLE80211_IOC_SCAN_RESULT:",
    )
    tahoe_branch = section(
        bssid_route,
        "#if __IO80211_TARGET >= __MAC_26_0",
        "#endif // __IO80211_TARGET >= __MAC_26_0",
    )
    after_tahoe_branch = bssid_route[
        bssid_route.index("#endif // __IO80211_TARGET >= __MAC_26_0")
        + len("#endif // __IO80211_TARGET >= __MAC_26_0"):
    ]
    legacy_bssid = section(
        v1,
        "getBSSID(OSObject *object,",
        "getSCAN_RESULT(OSObject *object,",
    )
    card_specific = section(
        v2,
        "SInt32 AirportItlwm::handleCardSpecific(",
        "IOReturn AirportItlwm::enableAdapter",
    )
    card_policy = section(
        routes,
        "case kIocSsid:",
        "case kIocBtCoexFlags:",
    )
    active_source_markers = (
        "F8A028722A4A7FE100C6DE90 /* AirportItlwmSkywalkInterface.cpp in Sources */",
        "F8D94CD22B9ABFE20081A3C4 /* AirportItlwmSkywalkInterface.cpp in Sources */",
        "F8E94CD22B9ABFE20081A3C4 /* AirportItlwmSkywalkInterface.cpp in Sources */",
    )
    null_guard = "if (req->req_data == NULL)\n        return kIOReturnUnsupported;"

    return {
        "schema": "itlwm-skywalk-public-bssid-set-fixed-stub-alignment-v1",
        "source_base_revision": "0009dcc7355d18806a1eff59a07e914bc67d99ab",
        "reference": {
            "bootkc_sha256": "eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d",
            "bootkc_uuid": "F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5",
            "embedded_kext_uuid": "8FB4B7F0-D656-3539-B8D6-C1327A50377C",
            "nlist_index": 7057,
            "public_wrapper": "0xffffff80021c372e",
            "next_symbol": "0xffffff80021c3739",
            "fixed_status": "0xe082280e",
            "body_sha256": "9e4580f0175946d7624b2451e6ffda84a93e91e3e2d852d7a2c7998ee2d78576",
        },
        "scope": {
            "normal_nonnull_public_bsd_set_only": True,
            "get_bootstrap_route_modified": False,
            "outer_or_inner_null_fallback_modified": False,
            "pre26_route_modified": False,
            "card_specific_set_route_modified": False,
            "legacy_v1_modified": False,
            "carrier_access": False,
            "deployment": False,
            "radio_or_association": False,
            "runtime_selector_invocation": False,
            "traffic": False,
        },
        "checks": {
            "reference_raw_manifest_matches": manifest == f"{raw_digest}  raw.txt\n",
            "reference_raw_has_exact_unread_public_set_fixed_stub": all(
                token in raw
                for token in (
                    "eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d",
                    "F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5",
                    "8FB4B7F0-D656-3539-B8D6-C1327A50377C",
                    "symbol=__Z18apple80211setBSSIDP23IO80211SkywalkInterfaceP21apple80211_bssid_data",
                    "nlist_index=7057",
                    "n_type=0x0f",
                    "n_sect=1",
                    "n_desc=0x0000",
                    "symbol_vmaddr=0xffffff80021c372e",
                    "symbol_vmaddr_end=0xffffff80021c3739",
                    "symbol_fileoff=0x20c372e",
                    "symbol_fileoff_end=0x20c3739",
                    "symbol_bytes=0x0b",
                    "next_nlist_index=7167",
                    "__Z21apple80211setSCAN_REQP23IO80211SkywalkInterfaceP20apple80211_scan_data",
                    "next_symbol_vmaddr=0xffffff80021c3739",
                    "body_sha256=9e4580f0175946d7624b2451e6ffda84a93e91e3e2d852d7a2c7998ee2d78576",
                    "0xe082280e",
                    "reads neither public argument",
                )
            ),
            "note_records_scope_and_nonclaims": all(
                token in note
                for token in (
                    "IOC 9",
                    "0xffffff80021c372e",
                    "0xe082280e",
                    "compile-time Tahoe-only guard",
                    "normal non-null Tahoe BSD SET",
                    "card-specific SET route remains excluded",
                    "does not claim outer-null, carrier/ABI, V1, card-specific GET, GET/bootstrap behavior, association, firmware, runtime-execution, or broader Tahoe behavior parity",
                    "No private carrier or selector is constructed or invoked",
                )
            ),
            "normal_nonnull_tahoe_public_set_returns_exact_unread_status": (
                "if (cmd == SIOCSA80211)" in tahoe_branch
                and "return static_cast<IOReturn>(0xe082280e);" in tahoe_branch
                and "req->req_data" not in tahoe_branch
                and "getBSSID" not in tahoe_branch
                and "return kIOReturnSuccess;" not in tahoe_branch
            ),
            "bootstrap_get_and_pre26_fallback_remain_separate": (
                "if (cmd == SIOCGA80211)" in after_tahoe_branch
                and "getTahoeCompactBSSID(req->req_data, req->req_len)" in after_tahoe_branch
                and "return getBSSID((apple80211_bssid_data *)req->req_data);" in after_tahoe_branch
                and "return kIOReturnUnsupported;" in after_tahoe_branch
            ),
            "null_fallback_remains_outside_this_layer": (
                null_guard in dispatcher
                and dispatcher.index(null_guard)
                < dispatcher.index("case APPLE80211_IOC_BSSID:")
            ),
            "bsd_ingress_terminalizes_exact_nonunsupported_status": (
                "IOReturn ret = processApple80211Ioctl(normalizedCmd, req);" in bsd_bridge
                and "if (ret != kIOReturnUnsupported)\n            return ret;" in bsd_bridge
            ),
            "card_specific_set_route_remains_excluded_before_dispatch": (
                "case kIocBssid:" in card_policy
                and "return !isSet;" in card_policy
                and "routeTahoeSkywalkIoctl(interface, &req," in card_specific
                and "isSet ? SIOCSA80211 : SIOCGA80211" in card_specific
            ),
            "legacy_v1_route_remains_separate": (
                "case APPLE80211_IOC_BSSID:  // 9\n            IOCTL(request_type, BSSID, apple80211_bssid_data);" in v1
                and "return static_cast<IOReturn>(0xe082280e);" in legacy_bssid
            ),
            "selector_and_carrier_abi_remain_explicit": (
                "#define APPLE80211_IOC_BSSID                     9" in ioctl
                and "struct apple80211_bssid_data" in ioctl
            ),
            "all_active_skywalk_source_phases_and_target_guard_are_explicit": (
                all(marker in project for marker in active_source_markers)
                and "#if __IO80211_TARGET >= __MAC_26_0" in bssid_route
                and "#endif // __IO80211_TARGET >= __MAC_26_0" in bssid_route
            ),
        },
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--write", action="store_true")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if args.write == args.check:
        parser.error("select exactly one of --write or --check")

    value = report()
    failed = [name for name, passed in value["checks"].items() if not passed]
    if failed:
        raise ValueError(
            "Skywalk public BSSID SET alignment validation failed: "
            + ", ".join(failed)
        )
    rendered = json.dumps(value, indent=2, sort_keys=True) + "\n"
    if args.write:
        OUTPUT.parent.mkdir(parents=True, exist_ok=True)
        OUTPUT.write_text(rendered, encoding="utf-8")
    elif not OUTPUT.exists() or OUTPUT.read_text(encoding="utf-8") != rendered:
        raise ValueError("checked-in report differs; rerun with --write")
    print(rendered, end="")


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(
            f"Skywalk public BSSID SET alignment validation failed: {exc}",
            file=sys.stderr,
        )
        sys.exit(1)
