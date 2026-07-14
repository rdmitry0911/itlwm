#!/usr/bin/env python3
"""Generate and verify FACETIME_WIFICALLING_PARAMS quarantine evidence."""

import argparse
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/facetime_wificalling_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-479-facetime-wificalling-quarantine-20260714.md"
INVENTORY = ROOT / "docs/tahoe_discrepancy_inventory.md"
SIGNAL_AUDIT = ROOT / "docs/tahoe_signal_chain_audit.md"
CPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
HPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.hpp"
SOURCE_ROOTS = (
    ROOT / "AirportItlwm",
    ROOT / "include",
    ROOT / "itl80211",
    ROOT / "itlwm",
)


def section(source, begin, end):
    start = source.index(begin)
    return source[start:source.index(end, start)]


def source_contains(token):
    for root in SOURCE_ROOTS:
        for path in root.rglob("*"):
            if path.suffix not in {".c", ".cc", ".cpp", ".h", ".hpp"}:
                continue
            if token in path.read_text(encoding="utf-8", errors="ignore"):
                return True
    return False


def report():
    cpp = CPP.read_text(encoding="utf-8")
    hpp = HPP.read_text(encoding="utf-8")
    note = " ".join(NOTE.read_text(encoding="utf-8").split())
    inventory = INVENTORY.read_text(encoding="utf-8")
    signal_audit = SIGNAL_AUDIT.read_text(encoding="utf-8")
    setter = section(
        cpp,
        "setFACETIME_WIFICALLING_PARAMS(apple80211_facetime_wificalling_params *data)",
        "setDUAL_POWER_MODE(apple80211_dual_power_mode_params *data)",
    )

    return {
        "schema": "itlwm-facetime-wificalling-quarantine-v1",
        "source_base_revision": "cc8e28f5a2c463a000d4175bdd866fc47cebdf37",
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "infra_wrapper": "0x100019094",
            "core_setter": "0x100142714",
            "null_return": "0xe00002bc",
            "status_offset": "+0x0",
            "policy_helper": "0x100139fbc",
            "feature_flag": "0x2c",
            "power_manager_offset": "+0x1590",
            "power_policy": "0x100174780",
        },
        "local": {
            "wifi_call_policy_backend": False,
            "request_false_success": False,
            "null_return_is_apple_parity": True,
            "valid_input_return_is_apple_parity": False,
        },
        "checks": {
            "reference_note": all(
                token in note
                for token in (
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "`0x100019094`",
                    "`0x100142714`",
                    "`0xe00002bc`",
                    "`+0x0`",
                    "`0x100139fbc`",
                    "feature flag `0x2c`",
                    "`+0x48` state block at `+0x1590`",
                    "`0x100174780`",
                    "No status validation, feature-state behavior, PowerManager action",
                    "Apple valid-input return-code parity is claimed",
                )
            ),
            "setter_preserves_null_and_quarantines_nonnull": all(
                token in setter
                for token in (
                    "if (data == nullptr)",
                    "return kIOReturnBadArgumentTahoe;",
                    "return kIOReturnUnsupported;",
                )
            )
            and all(
                token not in setter
                for token in (
                    "tahoeFaceTimeWiFiCallingParams",
                    "cachedFaceTimeWiFiCallingStatus",
                    "data->",
                    "return kIOReturnSuccess;",
                )
            ),
            "synthetic_status_state_removed": all(
                token not in cpp + hpp
                for token in (
                    "tahoeFaceTimeWiFiCallingParams",
                    "cachedFaceTimeWiFiCallingStatus",
                )
            ),
            "interface_slot_retained": "virtual IOReturn setFACETIME_WIFICALLING_PARAMS(apple80211_facetime_wificalling_params *) override;"
            in hpp,
            "scoped_policy_backend_absent": all(
                not source_contains(token)
                for token in (
                    "setWiFiCallPolicies(",
                    "setWiFiCallPowerPolicy(",
                )
            ),
            "historical_claims_corrected": (
                "FACETIME_WIFICALLING_PARAMS correction:" in signal_audit
                and "prior status-cache success was removed" in inventory
                and "Preserve that status verbatim" not in cpp
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
        raise ValueError("FACETIME_WIFICALLING_PARAMS quarantine checks failed: " + ", ".join(failed))
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
        print(f"FACETIME_WIFICALLING_PARAMS quarantine validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
