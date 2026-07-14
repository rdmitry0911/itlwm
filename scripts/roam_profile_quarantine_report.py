#!/usr/bin/env python3
"""Generate and verify ROAM_PROFILE quarantine evidence."""

import argparse
import hashlib
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/roam_profile_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-479-roam-profile-quarantine-20260715.md"
RAW = ROOT / "docs/reference/artifacts/roam-profile-25c56/raw.txt"
RAW_MANIFEST = RAW.with_name("SHA256SUMS.txt")
CPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
HPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.hpp"
V2 = ROOT / "AirportItlwm/AirportItlwmV2.cpp"
INFRA = ROOT / "include/Airport/IO80211InfraProtocol.h"


def section(source, begin, end):
    start = source.index(begin)
    return source[start:source.index(end, start)]


def report():
    cpp = CPP.read_text(encoding="utf-8")
    hpp = HPP.read_text(encoding="utf-8")
    v2 = V2.read_text(encoding="utf-8")
    infra = INFRA.read_text(encoding="utf-8")
    note = NOTE.read_text(encoding="utf-8")
    raw = RAW.read_text(encoding="utf-8")
    manifest = RAW_MANIFEST.read_text(encoding="utf-8")
    getter = section(
        cpp,
        "getROAM_PROFILE(apple80211_roam_profile_all_bands *data)",
        "getCOUNTRY_CHANNELS",
    )
    raw_digest = hashlib.sha256(RAW.read_bytes()).hexdigest()

    return {
        "schema": "itlwm-roam-profile-quarantine-v1",
        "source_base_revision": "3e0160b787b0d98b0c0e5f6fe053b31d8059f30f",
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "image_uuid_x86_64": "149C0AD1-A92F-35BC-AA69-5C8815C5421E",
            "infra_slot": 485,
            "infra_wrapper": "0x1000171c4",
            "core_getter": "0x100140c0c",
            "core_vtable_offset": "0x768",
            "roam_adapter": "0x10001d198",
            "per_band_getter": "0x10001d216",
            "not_associated_status": "0xe0822403",
            "primary_missing_status": "0x16",
        },
        "local": {
            "false_success": False,
            "null_return_is_apple_parity": False,
            "valid_input_return_is_apple_parity": False,
            "runtime_selector_invocation": False,
        },
        "checks": {
            "reference_raw_manifest_matches": manifest == f"{raw_digest}  raw.txt\n",
            "reference_raw_has_roam_pipeline": all(
                token in raw
                for token in (
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "149C0AD1-A92F-35BC-AA69-5C8815C5421E",
                    "0x1000171c4",
                    "0x768(%rax)",
                    "0x100140c0c",
                    "0x15c0(%rax)",
                    "0x10001d198",
                    "getPrimaryInterface",
                    "$0x16",
                    "0x10001d216",
                    "$0xe0822403",
                    '\"roam_prof\"',
                    "runIOVarGet",
                    "movl   %eax, %r13d",
                )
            ),
            "reference_note_has_scope_and_nonclaim": all(
                token in note
                for token in (
                    "slot `[485]`",
                    "0x1000171c4",
                    "0x100140c0c",
                    "0x10001d198",
                    "0x10001d216",
                    "not Apple null, valid-input error-code,\noutput-layout, association, firmware, or runtime-selector parity",
                )
            ),
            "active_v2_slot_remains": (
                "// [485]" in hpp
                and "getROAM_PROFILE" in hpp
                and "// [485]" in infra
                and "getROAM_PROFILE" in infra
                and "fNetIf = new AirportItlwmSkywalkInterface;" in v2
            ),
            "local_null_guard_is_retained": (
                "if (data == nullptr)" in getter
                and "return kIOReturnBadArgumentTahoe;" in getter
            ),
            "nonnull_path_fails_closed_without_synthetic_bands": (
                "(void)data;" in getter
                and "return kIOReturnUnsupported;" in getter
                and "kIOReturnSuccess" not in getter
                and "memset" not in getter
                and "0x180" not in getter
                and "reinterpret_cast" not in getter
            ),
            "no_local_roam_firmware_backend_is_introduced": all(
                token not in getter
                for token in (
                    "getPrimaryInterface",
                    "isAssociated",
                    "getRoamProfilePerBand",
                    "runIOVarGet",
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
        raise ValueError("ROAM_PROFILE quarantine checks failed: " + ", ".join(failed))
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
        print(f"ROAM_PROFILE quarantine validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
