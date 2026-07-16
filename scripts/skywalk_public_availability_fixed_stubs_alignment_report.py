#!/usr/bin/env python3
"""Generate and verify Skywalk public AVAILABILITY fixed-stub evidence."""

import argparse
import hashlib
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/skywalk_public_availability_fixed_stubs_alignment_report.json"
NOTE = ROOT / "docs/reference/CR-572-skywalk-public-availability-fixed-stubs-alignment-20260716.md"
RAW = ROOT / "docs/reference/artifacts/skywalk-availability-public-fixed-stubs-bootkc-current/raw.txt"
RAW_MANIFEST = RAW.with_name("SHA256SUMS.txt")
CPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
ROUTES = ROOT / "AirportItlwm/TahoeSkywalkIoctlRoutes.hpp"
PROJECT = ROOT / "itlwm.xcodeproj/project.pbxproj"


def section(source, begin, end):
    start = source.index(begin)
    return source[start:source.index(end, start)]


def report():
    cpp = CPP.read_text(encoding="utf-8")
    routes = ROUTES.read_text(encoding="utf-8")
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
    availability_route = section(
        dispatcher,
        "case APPLE80211_IOC_AVAILABILITY:",
        "case APPLE80211_IOC_BTCOEX_PROFILES:",
    )
    tahoe_branch = section(
        availability_route,
        "#if __IO80211_TARGET >= __MAC_26_0",
        "#endif // __IO80211_TARGET >= __MAC_26_0",
    )
    after_tahoe_branch = availability_route[
        availability_route.index("#endif // __IO80211_TARGET >= __MAC_26_0")
        + len("#endif // __IO80211_TARGET >= __MAC_26_0"):
    ]

    null_guard = "if (req->req_data == NULL)\n        return kIOReturnUnsupported;"
    active_source_markers = (
        "F8A028722A4A7FE100C6DE90 /* AirportItlwmSkywalkInterface.cpp in Sources */",
        "F8D94CD22B9ABFE20081A3C4 /* AirportItlwmSkywalkInterface.cpp in Sources */",
        "F8E94CD22B9ABFE20081A3C4 /* AirportItlwmSkywalkInterface.cpp in Sources */",
    )
    return {
        "schema": "itlwm-skywalk-public-availability-fixed-stubs-alignment-v1",
        "source_base_revision": "93bed316659a361459c5d54d8ec845e7d3801799",
        "reference": {
            "bootkc_sha256": "eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d",
            "bootkc_uuid": "F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5",
            "embedded_kext_uuid": "8FB4B7F0-D656-3539-B8D6-C1327A50377C",
            "selector": 105,
            "fixed_status": "0xe082280e",
            "get_nlist_index": 7366,
            "get_public_wrapper": "0xffffff80021bf39c",
            "get_next_symbol": "0xffffff80021bf3a7",
            "set_nlist_index": 7393,
            "set_public_wrapper": "0xffffff80021c421d",
            "set_next_symbol": "0xffffff80021c4228",
            "body_sha256": "9e4580f0175946d7624b2451e6ffda84a93e91e3e2d852d7a2c7998ee2d78576",
        },
        "scope": {
            "normal_nonnull_public_bsd_get_set_only": True,
            "outer_or_inner_null_fallback_modified": False,
            "pre26_route_modified": False,
            "card_specific_route_modified": False,
            "legacy_v1_modified": False,
            "virtual_interface_modified": False,
            "deployment": False,
            "radio_or_association": False,
            "runtime_selector_invocation": False,
            "traffic": False,
        },
        "checks": {
            "reference_raw_manifest_matches": manifest == f"{raw_digest}  raw.txt\n",
            "reference_raw_records_exact_fixed_get_and_set_leaves": all(
                token in raw
                for token in (
                    "selector=APPLE80211_IOC_AVAILABILITY",
                    "selector_value=105",
                    "fixed_status=0xe082280e",
                    "nlist_index=7366",
                    "n_type=0x0f",
                    "n_sect=1",
                    "n_desc=0x0000",
                    "__Z25apple80211getAVAILABILITYP23IO80211SkywalkInterfacePv",
                    "symbol_vmaddr=0xffffff80021bf39c",
                    "symbol_vmaddr_end=0xffffff80021bf3a7",
                    "next_nlist_index=7314",
                    "__Z24apple80211getRSSI_BOUNDSP23IO80211SkywalkInterfaceP27apple80211_rssi_bounds_data",
                    "nlist_index=7393",
                    "__Z25apple80211setAVAILABILITYP23IO80211SkywalkInterfacePv",
                    "symbol_vmaddr=0xffffff80021c421d",
                    "symbol_vmaddr_end=0xffffff80021c4228",
                    "next_nlist_index=7349",
                    "__Z24apple80211setRSSI_BOUNDSP23IO80211SkywalkInterfaceP27apple80211_rssi_bounds_data",
                    "body_hex=554889e5b80e2882e05dc3",
                    "body_sha256=9e4580f0175946d7624b2451e6ffda84a93e91e3e2d852d7a2c7998ee2d78576",
                    "reads_neither_public_argument=true",
                )
            ),
            "note_records_scope_and_nonclaims": all(
                token in note
                for token in (
                    "selector 105",
                    "0xffffff80021bf39c",
                    "0xffffff80021c421d",
                    "0xe082280e",
                    "normal non-null Tahoe BSD GET and SET",
                    "does not allocate, inspect, or synthesize an availability carrier",
                    "No legacy V1, virtual-interface, card-specific, radio, association, firmware, deployment, or runtime-selector claim",
                )
            ),
            "normal_nonnull_tahoe_public_get_and_set_return_exact_fixed_status_unread": (
                "case APPLE80211_IOC_AVAILABILITY:" in availability_route
                and "if (cmd == SIOCGA80211 || cmd == SIOCSA80211)" in tahoe_branch
                and "return static_cast<IOReturn>(0xe082280e);" in tahoe_branch
                and "req->" not in tahoe_branch
                and "AVAILABILITY(" not in tahoe_branch
                and "return kIOReturnSuccess;" not in tahoe_branch
            ),
            "pre26_and_unknown_command_fallback_remain_unsupported": (
                "SIOCSA80211" not in after_tahoe_branch
                and "SIOCGA80211" not in after_tahoe_branch
                and "return kIOReturnUnsupported;" in after_tahoe_branch
            ),
            "null_fallback_remains_outside_this_layer": (
                null_guard in dispatcher
                and dispatcher.index(null_guard)
                < dispatcher.index("case APPLE80211_IOC_AVAILABILITY:")
            ),
            "bsd_ingress_terminalizes_exact_nonunsupported_status": (
                "IOReturn ret = processApple80211Ioctl(normalizedCmd, req);" in bsd_bridge
                and "if (ret != kIOReturnUnsupported)\n            return ret;" in bsd_bridge
            ),
            "no_card_specific_availability_route_is_introduced": (
                "kIocAvailability" not in routes
                and "case kIocAvailability:" not in routes
            ),
            "only_the_public_dispatch_case_names_availability_in_active_source": (
                cpp.count("APPLE80211_IOC_AVAILABILITY") == 1
            ),
            "all_active_skywalk_source_phases_and_target_guard_are_explicit": (
                all(marker in project for marker in active_source_markers)
                and "#if __IO80211_TARGET >= __MAC_26_0" in availability_route
                and "#endif // __IO80211_TARGET >= __MAC_26_0" in availability_route
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
            "Skywalk public AVAILABILITY alignment checks failed: "
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
            f"Skywalk public AVAILABILITY alignment validation failed: {exc}",
            file=sys.stderr,
        )
        sys.exit(1)
