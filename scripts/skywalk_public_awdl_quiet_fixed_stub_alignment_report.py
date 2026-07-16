#!/usr/bin/env python3
"""Generate and verify public AWDL quiet fixed-stub evidence."""

import argparse
import hashlib
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/skywalk_public_awdl_quiet_fixed_stub_alignment_report.json"
NOTE = ROOT / "docs/reference/CR-584-skywalk-public-awdl-quiet-fixed-stub-alignment-20260716.md"
RAW = ROOT / "docs/reference/artifacts/skywalk-public-awdl-quiet-fixed-stub-bootkc-current/raw.txt"
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
    note = " ".join(NOTE.read_text(encoding="utf-8").split()).replace(chr(96), "")
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
    quiet_route = section(
        dispatcher,
        "case APPLE80211_IOC_AWDL_QUIET:",
        "default:",
    )
    tahoe_branch = section(
        quiet_route,
        "#if __IO80211_TARGET >= __MAC_26_0",
        "#endif // __IO80211_TARGET >= __MAC_26_0",
    )
    after_tahoe_branch = quiet_route[
        quiet_route.index("#endif // __IO80211_TARGET >= __MAC_26_0")
        + len("#endif // __IO80211_TARGET >= __MAC_26_0") :
    ]
    null_guard = "if (req->req_data == NULL)\n        return kIOReturnUnsupported;"
    active_source_markers = (
        "F8A028722A4A7FE100C6DE90 /* AirportItlwmSkywalkInterface.cpp in Sources */",
        "F8D94CD22B9ABFE20081A3C4 /* AirportItlwmSkywalkInterface.cpp in Sources */",
        "F8E94CD22B9ABFE20081A3C4 /* AirportItlwmSkywalkInterface.cpp in Sources */",
    )
    raw_tokens = (
        "selector=APPLE80211_IOC_AWDL_QUIET",
        "selector_value=168",
        "get_nlist_index=7232",
        "get_symbol_vmaddr=0xffffff80021bfbdf",
        "set_nlist_index=7259",
        "set_symbol_vmaddr=0xffffff80021c4ab9",
    )
    return {
        "schema": "itlwm-skywalk-public-awdl-quiet-fixed-stub-alignment-v1",
        "source_base_revision": "fa3e50c78a5aa96ab8355c26347efc639fa5561f",
        "reference": {
            "bootkc_sha256": "eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d",
            "bootkc_uuid": "F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5",
            "embedded_kext_uuid": "8FB4B7F0-D656-3539-B8D6-C1327A50377C",
            "fixed_status": "0xe082280e",
            "body_sha256": "9e4580f0175946d7624b2451e6ffda84a93e91e3e2d852d7a2c7998ee2d78576",
            "surfaces": [
                {
                    "name": "AWDL_QUIET",
                    "selector": 168,
                    "get_nlist_index": 7232,
                    "get_public_wrapper": "0xffffff80021bfbdf",
                    "set_nlist_index": 7259,
                    "set_public_wrapper": "0xffffff80021c4ab9",
                }
            ],
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
            "reference_raw_records_both_exact_fixed_leaves": all(token in raw for token in raw_tokens)
            and raw.count("get_body_hex=554889e5b80e2882e05dc3") == 1
            and raw.count("set_body_hex=554889e5b80e2882e05dc3") == 1
            and raw.count("get_reads_neither_public_argument=true") == 1
            and raw.count("set_reads_neither_public_argument=true") == 1
            and raw.count("get_n_type=0x0f") == 1
            and raw.count("set_n_type=0x0f") == 1,
            "note_records_scope_and_nonclaims": all(
                token in note
                for token in (
                    "normal, non-null Tahoe BSD GET and SET routes",
                    "0xe082280e",
                    "reads neither public interface nor carrier argument",
                    "does not allocate, inspect, retain, or synthesize AWDL quiet state",
                    "No AWDL_PEER_TRAFFIC_REGISTRATION, AWDL_FORCED_ROAM_CONFIG, AWDL RSSI, AES-key, scan-reservation, control, social-slot, virtual-interface, card-specific, legacy V1, firmware, deployment, runtime-selector, association, radio, or traffic claim is made",
                )
            ),
            "normal_nonnull_tahoe_public_get_and_set_return_exact_fixed_status_unread": "case APPLE80211_IOC_AWDL_QUIET:"
            in quiet_route
            and "if (cmd == SIOCGA80211 || cmd == SIOCSA80211)" in tahoe_branch
            and "return static_cast<IOReturn>(0xe082280e);" in tahoe_branch
            and "req->" not in tahoe_branch
            and "return kIOReturnSuccess;" not in tahoe_branch,
            "pre26_and_unknown_command_fallback_remain_unsupported": "SIOCSA80211"
            not in after_tahoe_branch
            and "SIOCGA80211" not in after_tahoe_branch
            and "return kIOReturnUnsupported;" in after_tahoe_branch,
            "null_fallback_remains_outside_this_layer": null_guard in dispatcher
            and dispatcher.index(null_guard)
            < dispatcher.index("case APPLE80211_IOC_AWDL_QUIET:"),
            "bsd_ingress_terminalizes_exact_nonunsupported_status": "IOReturn ret = processApple80211Ioctl(normalizedCmd, req);"
            in bsd_bridge
            and "if (ret != kIOReturnUnsupported)\n            return ret;" in bsd_bridge,
            "no_card_specific_awdl_quiet_route_is_introduced": "AWDL_QUIET" not in routes,
            "only_one_public_dispatch_case_exists": cpp.count(
                "case APPLE80211_IOC_AWDL_QUIET:"
            )
            == 1,
            "all_active_skywalk_source_phases_and_target_guard_are_explicit": all(
                marker in project for marker in active_source_markers
            )
            and "#if __IO80211_TARGET >= __MAC_26_0" in quiet_route
            and "#endif // __IO80211_TARGET >= __MAC_26_0" in quiet_route,
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
            "Skywalk public AWDL quiet alignment checks failed: " + ", ".join(failed)
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
        print(f"Skywalk public AWDL quiet validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
