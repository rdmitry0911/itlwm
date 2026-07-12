#!/usr/bin/env python3
"""Generate and verify WCL BCN mute false-success quarantine evidence."""

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/wcl_bcn_mute_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-479-wcl-bcn-mute-quarantine-20260712.md"
CPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
HPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.hpp"
SOURCE_ROOTS = (ROOT / "AirportItlwm", ROOT / "itlwm")


def section(source, begin, end):
    start = source.index(begin)
    return source[start:source.index(end, start)]


def source_contains(token):
    return any(
        token in path.read_text(encoding="utf-8", errors="ignore")
        for root in SOURCE_ROOTS
        for path in root.rglob("*")
        if path.is_file()
    )


def report():
    cpp = CPP.read_text(encoding="utf-8")
    hpp = HPP.read_text(encoding="utf-8")
    note = NOTE.read_text(encoding="utf-8")
    setter = section(
        cpp,
        "setWCL_BCN_MUTE_CONFIG(apple80211_bcn_mute_config *data)",
        "setEAP_FILTER_CONFIG",
    )
    return {
        "schema": "itlwm-wcl-bcn-mute-quarantine-v1",
        "source_base_revision": "bb52a74",
        "reference": {
            "core_setter": "0xffffff80016176b0",
            "net_adapter_offset": "0x128, 0x15e0",
            "adapter_setter": "0xffffff8001528c38",
            "feature_gate": "0x52",
            "firmware_iovar": "bcn_mute_miti_config",
        },
        "local": {
            "backend_beacon_mitigation_adapter": False,
            "request_false_success": False,
        },
        "checks": {
            "reference_note": all(
                token in note
                for token in (
                    "FUN_ffffff80016176b0",
                    "Core",
                    "+0x128, +0x15e0",
                    "FUN_ffffff8001528c38",
                    "feature 0x52",
                    "bcn_mute_miti_config",
                    "workqueue",
                )
            ),
            "setter_quarantines_nonnull": (
                "if (data == nullptr)" in setter
                and "return kIOReturnBadArgumentTahoe;" in setter
                and "return kIOReturnUnsupported;" in setter
                and "cachedBcnMuteConfig" not in setter
                and "return kIOReturnSuccess;" not in setter
            ),
            "pseudo_state_removed": "cachedBcnMuteConfig" not in cpp
            and "cachedBcnMuteConfig" not in hpp
            and "hasCachedBcnMuteConfig" not in cpp
            and "hasCachedBcnMuteConfig" not in hpp,
            "intel_source_has_no_bcn_mute_backend": not any(
                source_contains(token)
                for token in (
                    "bcn_mute_miti_config",
                    "configureBeaconMitigationParams",
                    "BeaconMitigationIovar",
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
        raise ValueError("WCL BCN mute quarantine checks failed: " + ", ".join(failed))
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
        print(f"WCL BCN mute quarantine validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
