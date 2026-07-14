#!/usr/bin/env python3
"""Generate and verify WCL legacy Roam Profile false-success evidence."""

import argparse
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/wcl_legacy_roam_profile_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-479-wcl-legacy-roam-profile-quarantine-20260714.md"
SIGNAL_AUDIT = ROOT / "docs/tahoe_signal_chain_audit.md"
INVENTORY = ROOT / "docs/tahoe_discrepancy_inventory.md"
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
    note = NOTE.read_text(encoding="utf-8")
    signal_audit = SIGNAL_AUDIT.read_text(encoding="utf-8")
    inventory = INVENTORY.read_text(encoding="utf-8")
    normalized_signal_audit = " ".join(signal_audit.split())
    normalized_inventory = " ".join(inventory.split())
    setter = section(
        cpp,
        "setWCL_LEGACY_ROAM_PROFILE_CONFIG(apple80211_legacy_roam_profile_config *data)",
        "setWCL_ROAM_PROFILE_CONFIG",
    )
    correction_heading = "## Q13 correction: WCL Legacy Roam Profile Config is RoamAdapter-backed"
    return {
        "schema": "itlwm-wcl-legacy-roam-profile-quarantine-v1",
        "source_base_revision": "0ea727804317adb3c830e1730ac19cb31ce30b39",
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "infra_wrapper": "0x100018b28",
            "infra_virtual_offset": "0x6d0",
            "core_setter": "0x100141de4",
            "adapter_null_cold_path": "0x10019ffce",
            "roam_adapter_offset": "0x15c0",
            "adapter_setter": "0x10001c272",
            "profile_setter": "0x10001a17e",
            "profile_v4": "0x10001a782",
            "profile_v2": "0x10001b3c4",
            "profile_callback": "0x10001bd9a",
            "multi_ap": "0x10001c322",
            "multi_ap_callback": "0x10001e809",
            "commander_send_iovar_set": "0x10017b900",
            "null_status": "0xe00002bc",
            "pseudo_layout_bytes": "0x60",
        },
        "local": {
            "matching_roam_adapter_backend_implemented": False,
            "request_false_success": False,
            "complete_public_carrier_layout_proven": False,
            "valid_input_or_error_is_apple_parity": False,
        },
        "checks": {
            "reference_note": all(
                token in note
                for token in (
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "0x100018b28",
                    "`+0x6d0`",
                    "0x100141de4",
                    "0x10019ffce",
                    "`0xe00002bc`",
                    "`+0x15c0`",
                    "0x10001c272",
                    "0x10001a17e",
                    "0x10001a782",
                    "0x10001b3c4",
                    "0x10001bd9a",
                    "0x10001c322",
                    "0x10001e809",
                    "0x10017b900",
                    "complete public carrier layout",
                )
            ),
            "setter_quarantines_nonnull": all(
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
                    "cachedLegacyRoamProfileConfig",
                    "hasCachedLegacyRoamProfileConfig",
                    "data->",
                    "return kIOReturnSuccess;",
                )
            ),
            "pseudo_state_and_layout_removed": all(
                token not in cpp and token not in hpp
                for token in (
                    "cachedLegacyRoamProfileConfig",
                    "hasCachedLegacyRoamProfileConfig",
                    "struct apple80211_legacy_roam_profile_config",
                )
            ),
            "scoped_roam_adapter_backend_absent": all(
                not source_contains(token)
                for token in (
                    "setRoamingProfile(",
                    "setRoamingProfileV4(",
                    "setRoamingProfileV2(",
                    "handleRoamProfileAsyncCallBack(",
                    "configureMultiAPBit(",
                    "sendIOVarSet(",
                    "runIOVarSet(",
                    "\"roam_prof\"",
                    "\"roam_multi_ap_env\"",
                )
            ),
            "historical_claims_corrected": correction_heading in signal_audit
            and "modern-profile recovery demonstrates a RoamAdapter policy and transport lifecycle and is reclassified"
            in normalized_signal_audit
            and "Legacy `setWCL_LEGACY_ROAM_PROFILE_CONFIG` is now explicitly a no-local-backend quarantine"
            in normalized_inventory,
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
        raise ValueError("WCL legacy Roam Profile quarantine checks failed: " + ", ".join(failed))
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
        print(f"WCL legacy Roam Profile quarantine validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
