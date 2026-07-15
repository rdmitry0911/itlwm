#!/usr/bin/env python3
"""Generate and verify Skywalk public PROTMODE fixed-stub evidence."""

import argparse
import hashlib
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/skywalk_public_protmode_fixed_stub_alignment_report.json"
NOTE = ROOT / "docs/reference/CR-529-skywalk-public-protmode-fixed-stub-alignment-20260715.md"
RAW = ROOT / "docs/reference/artifacts/skywalk-protmode-public-fixed-stub-bootkc-current/raw.txt"
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
    protmode_route = section(
        dispatcher,
        "case APPLE80211_IOC_PROTMODE:",
        "case APPLE80211_IOC_HOST_AP_MODE:",
    )
    tahoe_branch = section(
        protmode_route,
        "#if __IO80211_TARGET >= __MAC_26_0",
        "#endif // __IO80211_TARGET >= __MAC_26_0",
    )
    after_tahoe_branch = protmode_route[
        protmode_route.index("#endif // __IO80211_TARGET >= __MAC_26_0")
        + len("#endif // __IO80211_TARGET >= __MAC_26_0"):
    ]
    legacy_helpers = section(
        v1,
        "getPROTMODE(OSObject *object,",
        "getTXPOWER(OSObject *object,",
    )
    card_specific = section(
        v2,
        "SInt32 AirportItlwm::handleCardSpecific(",
        "IOReturn AirportItlwm::enableAdapter",
    )
    active_source_markers = (
        "F8A028722A4A7FE100C6DE90 /* AirportItlwmSkywalkInterface.cpp in Sources */",
        "F8D94CD22B9ABFE20081A3C4 /* AirportItlwmSkywalkInterface.cpp in Sources */",
        "F8E94CD22B9ABFE20081A3C4 /* AirportItlwmSkywalkInterface.cpp in Sources */",
    )
    null_guard = "if (req->req_data == NULL)\n        return kIOReturnUnsupported;"

    return {
        "schema": "itlwm-skywalk-public-protmode-fixed-stub-alignment-v1",
        "source_base_revision": "bf891c2efbf85af20eea2b904097d029f91fd5fd",
        "reference": {
            "bootkc_sha256": "eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d",
            "bootkc_uuid": "F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5",
            "embedded_kext_uuid": "8FB4B7F0-D656-3539-B8D6-C1327A50377C",
            "fixed_status": "0xe082280e",
            "body_sha256": "9e4580f0175946d7624b2451e6ffda84a93e91e3e2d852d7a2c7998ee2d78576",
            "get": {
                "nlist_index": 7146,
                "public_wrapper": "0xffffff80021be4bb",
                "next_symbol": "0xffffff80021be4c6",
            },
            "set": {
                "nlist_index": 7164,
                "public_wrapper": "0xffffff80021c3679",
                "next_symbol": "0xffffff80021c3684",
            },
        },
        "scope": {
            "normal_nonnull_public_bsd_get_and_set_only": True,
            "outer_or_inner_null_fallback_modified": False,
            "pre26_route_modified": False,
            "card_specific_route_modified": False,
            "legacy_v1_modified": False,
            "carrier_access": False,
            "deployment": False,
            "radio_or_association": False,
            "runtime_selector_invocation": False,
            "traffic": False,
        },
        "checks": {
            "reference_raw_manifest_matches": manifest == f"{raw_digest}  raw.txt\n",
            "reference_raw_has_exact_unread_get_and_set_fixed_stubs": all(
                token in raw
                for token in (
                    "eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d",
                    "F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5",
                    "8FB4B7F0-D656-3539-B8D6-C1327A50377C",
                    "symbol=__Z21apple80211getPROTMODEP23IO80211SkywalkInterfacePv",
                    "nlist_index=7146",
                    "symbol_vmaddr=0xffffff80021be4bb",
                    "symbol_vmaddr_end=0xffffff80021be4c6",
                    "symbol_fileoff=0x20be4bb",
                    "symbol_fileoff_end=0x20be4c6",
                    "next_nlist_index=7105",
                    "__Z20apple80211getTXPOWERP23IO80211SkywalkInterfaceP23apple80211_txpower_data",
                    "symbol=__Z21apple80211setPROTMODEP23IO80211SkywalkInterfacePv",
                    "nlist_index=7164",
                    "symbol_vmaddr=0xffffff80021c3679",
                    "symbol_vmaddr_end=0xffffff80021c3684",
                    "symbol_fileoff=0x20c3679",
                    "symbol_fileoff_end=0x20c3684",
                    "next_nlist_index=7126",
                    "__Z20apple80211setTXPOWERP23IO80211SkywalkInterfaceP23apple80211_txpower_data",
                    "n_type=0x0f",
                    "n_sect=1",
                    "n_desc=0x0000",
                    "55 48 89 e5 b8 0e 28 82 e0 5d c3",
                    "0xe082280e",
                    "reads neither public argument",
                )
            )
            and raw.count(
                "body_sha256=9e4580f0175946d7624b2451e6ffda84a93e91e3e2d852d7a2c7998ee2d78576"
            )
            == 2,
            "note_records_scope_and_nonclaims": all(
                token in note
                for token in (
                    "IOC 6",
                    "0xffffff80021be4bb",
                    "0xffffff80021c3679",
                    "0xe082280e",
                    "compile-time Tahoe-only guard",
                    "normal non-null Tahoe BSD GET and SET",
                    "card-specific route remains excluded",
                    "does not claim outer-null, carrier/ABI, V1, card-specific, CHANNEL, POWERSAVE, association, firmware, runtime-execution, or broader Tahoe behavior parity",
                    "No private carrier or selector is constructed or invoked",
                )
            ),
            "normal_nonnull_tahoe_public_get_and_set_return_exact_unread_status": (
                "if (cmd == SIOCGA80211 || cmd == SIOCSA80211)" in tahoe_branch
                and "return static_cast<IOReturn>(0xe082280e);" in tahoe_branch
                and "req->req_data" not in tahoe_branch
                and "getPROTMODE" not in tahoe_branch
                and "setPROTMODE" not in tahoe_branch
                and "return kIOReturnSuccess;" not in tahoe_branch
            ),
            "null_and_pre26_fallbacks_remain_outside_this_layer": (
                null_guard in dispatcher
                and dispatcher.index(null_guard)
                < dispatcher.index("case APPLE80211_IOC_PROTMODE:")
                and "return kIOReturnUnsupported;" in after_tahoe_branch
            ),
            "bsd_ingress_terminalizes_exact_nonunsupported_status": (
                "IOReturn ret = processApple80211Ioctl(normalizedCmd, req);" in bsd_bridge
                and "if (ret != kIOReturnUnsupported)\n            return ret;" in bsd_bridge
            ),
            "card_specific_route_remains_excluded_before_dispatch": (
                "kIocProtmode" not in routes
                and "default:\n            return false;" in routes
                and "routeTahoeSkywalkIoctl(interface, &req," in card_specific
                and "isSet ? SIOCSA80211 : SIOCGA80211" in card_specific
            ),
            "legacy_v1_route_and_helpers_remain_separate": (
                "case APPLE80211_IOC_PROTMODE:\n            IOCTL(request_type, PROTMODE, apple80211_protmode_data);" in v1
                and "TahoeAssociationContracts::kPublicProtmodeUnsupportedStatus" in legacy_helpers
                and "return kIOReturnError;" in legacy_helpers
            ),
            "selector_and_carrier_abi_remain_explicit": (
                "#define APPLE80211_IOC_PROTMODE                  6" in ioctl
                and "struct apple80211_protmode_data" in ioctl
            ),
            "all_active_skywalk_source_phases_and_target_guard_are_explicit": (
                all(marker in project for marker in active_source_markers)
                and "#if __IO80211_TARGET >= __MAC_26_0" in protmode_route
                and "#endif // __IO80211_TARGET >= __MAC_26_0" in protmode_route
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
            "Skywalk public PROTMODE alignment validation failed: "
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
            f"Skywalk public PROTMODE alignment validation failed: {exc}",
            file=sys.stderr,
        )
        sys.exit(1)
