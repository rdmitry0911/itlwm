#!/usr/bin/env python3
"""Generate and verify public AWDL BTLE/peer-db fixed-stub evidence."""

import argparse
import hashlib
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/skywalk_public_awdl_btle_peer_db_fixed_stubs_alignment_report.json"
NOTE = ROOT / "docs/reference/CR-585-skywalk-public-awdl-btle-peer-db-fixed-stubs-alignment-20260716.md"
RAW = ROOT / "docs/reference/artifacts/skywalk-public-awdl-btle-peer-db-fixed-stubs-bootkc-current/raw.txt"
RAW_MANIFEST = RAW.with_name("SHA256SUMS.txt")
CPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
ROUTES = ROOT / "AirportItlwm/TahoeSkywalkIoctlRoutes.hpp"
PROJECT = ROOT / "itlwm.xcodeproj/project.pbxproj"

SURFACES = (
    ("AWDL_BTLE_PEER_INDICATION", 201, 8103, "0xffffff80021bfe28", 8113, "0xffffff80021c4d4c"),
    ("AWDL_BTLE_STATE_PARAMS", 202, 8008, "0xffffff80021bfe33", 8019, "0xffffff80021c4d57"),
    ("AWDL_PEER_DATABASE", 203, 7768, "0xffffff80021bfe3e", 7805, "0xffffff80021c4d62"),
    ("AWDL_BTLE_ENABLE_SYNC_WITH_PARAMS", 204, 8224, "0xffffff80021bfe49", 8225, "0xffffff80021c4d6d"),
)


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
    bsd_bridge = section(cpp, "processBSDCommand(ifnet_t interface, UInt cmd, void *data)", "processApple80211Ioctl(UInt cmd, apple80211req *req)")
    dispatcher = section(cpp, "processApple80211Ioctl(UInt cmd, apple80211req *req)", "IOReturn AirportItlwmSkywalkInterface::\ngetAUTH_TYPE")
    awdl_route = section(dispatcher, "case APPLE80211_IOC_AWDL_BTLE_PEER_INDICATION:", "case APPLE80211_IOC_MIS_MAX_STA:")
    tahoe_branch = section(awdl_route, "#if __IO80211_TARGET >= __MAC_26_0", "#endif // __IO80211_TARGET >= __MAC_26_0")
    after_tahoe_branch = awdl_route[awdl_route.index("#endif // __IO80211_TARGET >= __MAC_26_0") + len("#endif // __IO80211_TARGET >= __MAC_26_0"):]
    null_guard = "if (req->req_data == NULL)\n        return kIOReturnUnsupported;"
    case_markers = tuple(f"case APPLE80211_IOC_{name}:" for name, *_ in SURFACES)
    active_source_markers = (
        "F8A028722A4A7FE100C6DE90 /* AirportItlwmSkywalkInterface.cpp in Sources */",
        "F8D94CD22B9ABFE20081A3C4 /* AirportItlwmSkywalkInterface.cpp in Sources */",
        "F8E94CD22B9ABFE20081A3C4 /* AirportItlwmSkywalkInterface.cpp in Sources */",
    )
    raw_surface_tokens = tuple(
        token
        for name, selector, get_nlist, get_vm, set_nlist, set_vm in SURFACES
        for token in (
            f"selector=APPLE80211_IOC_{name}",
            f"selector_value={selector}",
            f"get_nlist_index={get_nlist}",
            f"get_symbol_vmaddr={get_vm}",
            f"set_nlist_index={set_nlist}",
            f"set_symbol_vmaddr={set_vm}",
        )
    )
    return {
        "schema": "itlwm-skywalk-public-awdl-btle-peer-db-fixed-stubs-alignment-v1",
        "source_base_revision": "4f94c66bb731f6e259704b2412cb4f0583149d87",
        "reference": {
            "bootkc_sha256": "eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d",
            "bootkc_uuid": "F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5",
            "embedded_kext_uuid": "8FB4B7F0-D656-3539-B8D6-C1327A50377C",
            "fixed_status": "0xe082280e",
            "body_sha256": "9e4580f0175946d7624b2451e6ffda84a93e91e3e2d852d7a2c7998ee2d78576",
            "surfaces": [
                {"name": name, "selector": selector, "get_nlist_index": get_nlist, "get_public_wrapper": get_vm, "set_nlist_index": set_nlist, "set_public_wrapper": set_vm}
                for name, selector, get_nlist, get_vm, set_nlist, set_vm in SURFACES
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
            "reference_raw_records_all_eight_exact_fixed_leaves": all(token in raw for token in raw_surface_tokens)
            and raw.count("get_body_hex=554889e5b80e2882e05dc3") == 4
            and raw.count("set_body_hex=554889e5b80e2882e05dc3") == 4
            and raw.count("get_reads_neither_public_argument=true") == 4
            and raw.count("set_reads_neither_public_argument=true") == 4
            and raw.count("get_n_type=0x0f") == 4
            and raw.count("set_n_type=0x0f") == 4,
            "note_records_scope_and_nonclaims": all(token in note for token in (
                "normal, non-null Tahoe BSD GET and SET routes",
                "0xe082280e",
                "reads neither public interface nor carrier argument",
                "does not allocate, inspect, retain, or synthesize AWDL BTLE indication, state, peer-database, or sync-with-parameters state",
                "No AWDL_PIGGYBACK_SCAN_REQ, AWDL_PRIVATE_ELECTION_ID, AWDL_QUIET, AWDL_PEER_TRAFFIC_REGISTRATION, virtual-interface, card-specific, legacy V1, firmware, deployment, runtime-selector, association, radio, or traffic claim is made",
            )),
            "normal_nonnull_tahoe_public_get_and_set_return_exact_fixed_status_unread": all(marker in awdl_route for marker in case_markers)
            and "if (cmd == SIOCGA80211 || cmd == SIOCSA80211)" in tahoe_branch
            and "return static_cast<IOReturn>(0xe082280e);" in tahoe_branch
            and "req->" not in tahoe_branch
            and "return kIOReturnSuccess;" not in tahoe_branch,
            "pre26_and_unknown_command_fallback_remain_unsupported": "SIOCSA80211" not in after_tahoe_branch
            and "SIOCGA80211" not in after_tahoe_branch
            and "return kIOReturnUnsupported;" in after_tahoe_branch,
            "null_fallback_remains_outside_this_layer": null_guard in dispatcher
            and dispatcher.index(null_guard) < dispatcher.index("case APPLE80211_IOC_AWDL_BTLE_PEER_INDICATION:"),
            "bsd_ingress_terminalizes_exact_nonunsupported_status": "IOReturn ret = processApple80211Ioctl(normalizedCmd, req);" in bsd_bridge
            and "if (ret != kIOReturnUnsupported)\n            return ret;" in bsd_bridge,
            "no_card_specific_awdl_btle_or_peer_db_route_is_introduced": all(name not in routes for name, *_ in SURFACES),
            "only_one_public_dispatch_case_per_awdl_selector": all(cpp.count(marker) == 1 for marker in case_markers),
            "all_active_skywalk_source_phases_and_target_guard_are_explicit": all(marker in project for marker in active_source_markers)
            and "#if __IO80211_TARGET >= __MAC_26_0" in awdl_route
            and "#endif // __IO80211_TARGET >= __MAC_26_0" in awdl_route,
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
        raise ValueError("Skywalk public AWDL BTLE/peer-db alignment checks failed: " + ", ".join(failed))
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
        print(f"Skywalk public AWDL BTLE/peer-db validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
