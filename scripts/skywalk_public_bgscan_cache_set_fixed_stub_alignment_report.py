#!/usr/bin/env python3
"""Generate and verify public BGSCAN cache SET fixed-stub evidence."""

import argparse
import hashlib
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/skywalk_public_bgscan_cache_set_fixed_stub_alignment_report.json"
NOTE = ROOT / "docs/reference/CR-575-skywalk-public-bgscan-cache-set-fixed-stub-alignment-20260716.md"
RAW = ROOT / "docs/reference/artifacts/skywalk-public-bgscan-cache-set-fixed-stub-bootkc-current/raw.txt"
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
    note = " ".join(NOTE.read_text(encoding="utf-8").split()).replace("`", "")
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
    bgscan_route = section(
        dispatcher,
        "case APPLE80211_IOC_BGSCAN_CACHE_RESULTS:",
        "case TahoeSkywalkIoctlRoutes::kIocWclBssInfo:",
    )
    tahoe_branch = section(
        bgscan_route,
        "#if __IO80211_TARGET >= __MAC_26_0",
        "#endif // __IO80211_TARGET >= __MAC_26_0",
    )
    before_tahoe_branch = bgscan_route[:bgscan_route.index("#if __IO80211_TARGET >= __MAC_26_0")]
    after_tahoe_branch = bgscan_route[
        bgscan_route.index("#endif // __IO80211_TARGET >= __MAC_26_0")
        + len("#endif // __IO80211_TARGET >= __MAC_26_0"):
    ]
    null_guard = "if (req->req_data == NULL)\n        return kIOReturnUnsupported;"
    active_source_markers = (
        "F8A028722A4A7FE100C6DE90 /* AirportItlwmSkywalkInterface.cpp in Sources */",
        "F8D94CD22B9ABFE20081A3C4 /* AirportItlwmSkywalkInterface.cpp in Sources */",
        "F8E94CD22B9ABFE20081A3C4 /* AirportItlwmSkywalkInterface.cpp in Sources */",
    )

    return {
        "schema": "itlwm-skywalk-public-bgscan-cache-set-fixed-stub-alignment-v1",
        "source_base_revision": "d8b7ee8f83c6c2399f9f6439d2549827db5b080d",
        "reference": {
            "bootkc_sha256": "eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d",
            "bootkc_uuid": "F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5",
            "embedded_kext_uuid": "8FB4B7F0-D656-3539-B8D6-C1327A50377C",
            "selector": 215,
            "get": {
                "nlist_index": 7900,
                "public_wrapper": "0xffffff80021c0034",
                "span_bytes": 85,
                "classification": "dynamic_wrapper_not_fixed_stub",
            },
            "set": {
                "nlist_index": 7929,
                "public_wrapper": "0xffffff80021c4f0e",
                "span_bytes": 11,
                "fixed_status": "0xe082280e",
                "body_sha256": "9e4580f0175946d7624b2451e6ffda84a93e91e3e2d852d7a2c7998ee2d78576",
            },
        },
        "scope": {
            "normal_nonnull_public_bsd_set_only": True,
            "existing_get_carrier_preserved": True,
            "outer_or_inner_null_fallback_modified": False,
            "pre26_route_modified": False,
            "virtual_interface_modified": False,
            "card_specific_route_modified": False,
            "legacy_v1_modified": False,
            "cache_update_modified": False,
            "deployment": False,
            "radio_or_association": False,
            "runtime_selector_invocation": False,
            "traffic": False,
        },
        "checks": {
            "reference_raw_manifest_matches": manifest == f"{raw_digest}  raw.txt\n",
            "reference_raw_distinguishes_dynamic_get_from_fixed_set": all(
                token in raw
                for token in (
                    "selector=APPLE80211_IOC_BGSCAN_CACHE_RESULTS",
                    "selector_value=215",
                    "get_nlist_index=7900",
                    "get_symbol_vmaddr=0xffffff80021c0034",
                    "get_span_bytes=85",
                    "get_classification=dynamic_wrapper_not_fixed_stub",
                    "set_nlist_index=7929",
                    "set_symbol_vmaddr=0xffffff80021c4f0e",
                    "set_span_bytes=11",
                    "set_body_hex=554889e5b80e2882e05dc3",
                    "set_body_sha256=9e4580f0175946d7624b2451e6ffda84a93e91e3e2d852d7a2c7998ee2d78576",
                    "set_fixed_status=0xe082280e",
                    "set_reads_neither_public_argument=true",
                    "set_n_type=0x0f",
                    "set_n_sect=1",
                    "set_n_desc=0x0000",
                )
            ),
            "note_records_asymmetric_scope_and_nonclaims": all(
                token in note
                for token in (
                    "normal, non-null Tahoe BSD SIOCSA80211 route",
                    "does not change the existing SIOCGA80211 background-scan cache-result producer",
                    "GET wrapper is a 85-byte dynamic wrapper",
                    "0xe082280e",
                    "reads neither public interface nor carrier argument",
                    "GET remains dynamic and untouched",
                    "Pre-26, null, unknown command, virtual-interface, card-specific, legacy V1, cache-update, firmware, deployment, association, radio, traffic, and runtime-selector behavior are not claimed or changed",
                )
            ),
            "get_remains_the_existing_dynamic_carrier_route": (
                "if (cmd == SIOCGA80211)" in before_tahoe_branch
                and "return getWCL_BGSCAN_CACHE_RESULT(" in before_tahoe_branch
                and "static_cast<IOReturn>(0xe082280e)" not in before_tahoe_branch
            ),
            "normal_nonnull_tahoe_public_set_returns_exact_fixed_status_unread": (
                "if (cmd == SIOCSA80211)" in tahoe_branch
                and "return static_cast<IOReturn>(0xe082280e);" in tahoe_branch
                and "req->" not in tahoe_branch
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
                < dispatcher.index("case APPLE80211_IOC_BGSCAN_CACHE_RESULTS:")
            ),
            "bsd_ingress_terminalizes_exact_nonunsupported_status": (
                "IOReturn ret = processApple80211Ioctl(normalizedCmd, req);" in bsd_bridge
                and "if (ret != kIOReturnUnsupported)\n            return ret;" in bsd_bridge
            ),
            "no_card_specific_bgscan_cache_route_is_introduced": (
                "BGSCAN_CACHE_RESULTS" not in routes
            ),
            "only_one_public_dispatch_case_for_bgscan_cache_results": (
                cpp.count("case APPLE80211_IOC_BGSCAN_CACHE_RESULTS:") == 1
            ),
            "all_active_skywalk_source_phases_and_target_guard_are_explicit": (
                all(marker in project for marker in active_source_markers)
                and "#if __IO80211_TARGET >= __MAC_26_0" in bgscan_route
                and "#endif // __IO80211_TARGET >= __MAC_26_0" in bgscan_route
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
            "Skywalk public BGSCAN cache SET alignment checks failed: "
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
            f"Skywalk public BGSCAN cache SET alignment validation failed: {exc}",
            file=sys.stderr,
        )
        sys.exit(1)
