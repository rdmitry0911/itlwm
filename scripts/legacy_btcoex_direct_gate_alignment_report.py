#!/usr/bin/env python3
"""Generate and verify Tahoe legacy BTCOEX direct-gate alignment evidence."""

import argparse
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/legacy_btcoex_direct_gate_alignment_report.json"
NOTE = ROOT / "docs/reference/CR-479-legacy-btcoex-direct-gate-alignment-20260714.md"
RAW = ROOT / "docs/reference/artifacts/legacy-btcoex-direct-gate-26.3/raw.txt"
RAW_MANIFEST = RAW.with_name("SHA256SUMS.txt")
SKYWALK = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
V2_HPP = ROOT / "AirportItlwm/AirportItlwmV2.hpp"
V2_CPP = ROOT / "AirportItlwm/AirportItlwmV2.cpp"
V1_HPP = ROOT / "AirportItlwm/AirportItlwm.hpp"
V1_CPP = ROOT / "AirportItlwm/AirportSTAIOCTL.cpp"
ROUTES = ROOT / "AirportItlwm/TahoeSkywalkIoctlRoutes.hpp"

STATUS = "0xe082280e"
GET_OFFSETS = {
    "mode": "0xdc5e6",
    "profiles": "0xdd502",
    "config": "0xdd50d",
    "options": "0xdd6c4",
}
SET_OFFSETS = {
    "mode": "0xe1388",
    "profiles": "0xe23dc",
    "config": "0xe23e7",
    "options": "0xe2632",
}
SELECTOR_CASES = (
    "APPLE80211_IOC_BTCOEX_PROFILES",
    "APPLE80211_IOC_BTCOEX_CONFIG",
    "APPLE80211_IOC_BTCOEX_OPTIONS",
    "APPLE80211_IOC_BTCOEX_MODE",
)
ROUTE_CASES = (
    "kIocBtcoexProfiles",
    "kIocBtcoexConfig",
    "kIocBtcoexOptions",
    "kIocBtcoexMode",
)
CACHE_FIELDS = ("btcProfile", "btcConfig", "btcOptions", "btcMode")


def section(source, begin, end):
    start = source.index(begin)
    return source[start:source.index(end, start)]


def raw_offset(offset):
    return "0x" + offset[2:].zfill(8)


def is_direct_leaf(raw, offset):
    start = raw.index(raw_offset(offset))
    window = raw[start:start + 640]
    move = window.index("mov eax, 0xe082280e")
    ret = window.index("ret")
    return move < ret


def report():
    note = NOTE.read_text(encoding="utf-8")
    raw = RAW.read_text(encoding="utf-8")
    raw_manifest = RAW_MANIFEST.read_text(encoding="utf-8")
    skywalk = SKYWALK.read_text(encoding="utf-8")
    v2_hpp = V2_HPP.read_text(encoding="utf-8")
    v2_cpp = V2_CPP.read_text(encoding="utf-8")
    v1_hpp = V1_HPP.read_text(encoding="utf-8")
    v1_cpp = V1_CPP.read_text(encoding="utf-8")
    routes = ROUTES.read_text(encoding="utf-8")
    gate = section(
        skywalk,
        "        case APPLE80211_IOC_BTCOEX_PROFILES:\n",
        "        case APPLE80211_IOC_BGSCAN_CACHE_RESULTS:",
    )
    route_gate = section(routes, "inline bool shouldRoute", "        default:")

    return {
        "schema": "itlwm-legacy-btcoex-direct-gate-alignment-v1",
        "source_base_revision": "c427073dacfe842e45a958026599a9a137806a86",
        "reference": {
            "bundle": "com.apple.iokit.IO80211Family",
            "bundle_version": "1200.13.1",
            "platform": "26.3",
            "image_sha256": "77fad8c22845b5ba2c5d808134f0dccd2cc5d42f006410aa89f7448360b72672",
            "direct_status": STATUS,
            "get_offsets": GET_OFFSETS,
            "set_offsets": SET_OFFSETS,
        },
        "scope": {
            "active_target": "Tahoe V2/Skywalk only",
            "v1_legacy_target_modified": False,
            "runtime_selector_invocation": False,
        },
        "checks": {
            "reference_raw_has_all_direct_leaves": all(
                is_direct_leaf(raw, offset)
                for offset in (*GET_OFFSETS.values(), *SET_OFFSETS.values())
            ),
            "reference_raw_manifest_is_portable": raw_manifest.strip().endswith("  raw.txt"),
            "reference_note_has_identity_and_offsets": all(
                token in note
                for token in (
                    "77fad8c22845b5ba2c5d808134f0dccd2cc5d42f006410aa89f7448360b72672",
                    "1200.13.1",
                    "26.3",
                    STATUS,
                    *GET_OFFSETS.values(),
                    *SET_OFFSETS.values(),
                    "`AirportSTAIOCTL.cpp` is deliberately outside",
                )
            ),
            "skywalk_combines_all_four_selector_cases": all(
                token in gate for token in SELECTOR_CASES
            ),
            "skywalk_returns_exact_direct_gate": (
                "return kApple80211ClassOwnerAbsent;" in gate
                and "instance->" not in gate
                and "req->req_data" not in gate
                and "kIOReturnSuccess" not in gate
                and "IOMalloc" not in gate
                and "memcpy" not in gate
            ),
            "v2_synthetic_cache_fields_removed": all(
                token not in v2_hpp and token not in v2_cpp and token not in skywalk
                for token in CACHE_FIELDS
            ),
            "tahoe_card_specific_route_is_retained": all(
                token in route_gate for token in ROUTE_CASES
            ),
            "v1_scope_is_not_changed": (
                "`AirportSTAIOCTL.cpp` is deliberately outside" in note
                and all(
                    f"FUNC_IOCTL({name}," in v1_hpp
                    for name in ("BTCOEX_PROFILES", "BTCOEX_CONFIG", "BTCOEX_OPTIONS", "BTCOEX_MODE")
                )
                and all(
                    f"{name}(OSObject *object" in v1_cpp
                    for name in ("getBTCOEX_PROFILES", "setBTCOEX_PROFILES", "getBTCOEX_CONFIG", "setBTCOEX_CONFIG", "getBTCOEX_OPTIONS", "setBTCOEX_OPTIONS", "getBTCOEX_MODE", "setBTCOEX_MODE")
                )
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
    failed = [key for key, passed in value["checks"].items() if not passed]
    if failed:
        raise ValueError("legacy BTCOEX direct-gate checks failed: " + ", ".join(failed))
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
        print(f"legacy BTCOEX direct-gate validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
