#!/usr/bin/env python3
"""Generate and verify WCL modern Roam Profile false-success evidence."""

import argparse
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/wcl_roam_profile_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-479-wcl-roam-profile-quarantine-20260714.md"
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
        "setWCL_ROAM_PROFILE_CONFIG(apple80211_roam_profile_config *data)",
        "setWCL_ARP_MODE",
    )
    correction_heading = "## Q13 correction: WCL Roam Profile Config is RoamAdapter-backed"
    return {
        "schema": "itlwm-wcl-roam-profile-quarantine-v1",
        "source_base_revision": "4be063c7e51e75bc23f73345fa9bb095053a2861",
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "infra_wrapper": "0x100018b74",
            "infra_virtual_offset": "0x6d8",
            "core_setter": "0x100141e10",
            "adapter_null_cold_path": "0x1001a01a0",
            "roam_adapter_offset": "0x15c0",
            "adapter_setter": "0x10001c3f8",
            "per_band_setter": "0x10001bfca",
            "profile_callback": "0x10001bd9a",
            "disable_6g": "0x10001c5b0",
            "disable_6g_callback": "0x10001de02",
            "candidate_boost": "0x10001c6ba",
            "multi_ap": "0x10001c322",
            "commander_send_iovar_set": "0x10017b900",
            "commander_run_iovar_set": "0x10017b6e6",
            "null_status": "0xe00002bc",
            "pseudo_layout_bytes": "0x23c",
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
                    "0x100018b74",
                    "`+0x6d8`",
                    "0x100141e10",
                    "0x1001a01a0",
                    "`0xe00002bc`",
                    "`+0x15c0`",
                    "0x10001c3f8",
                    "`+0x0`",
                    "`+0xb8`",
                    "`+0x170`",
                    "0x10001bfca",
                    "0x10001bd9a",
                    "0x10001c5b0",
                    "`+0x238`",
                    "0x10001de02",
                    "0x10001c6ba",
                    "0x10001c322",
                    "0x10017b900",
                    "0x10017b6e6",
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
                    "cachedRoamProfileConfig",
                    "hasCachedRoamProfileConfig",
                    "data->",
                    "return kIOReturnSuccess;",
                )
            ),
            "pseudo_state_and_layout_removed": all(
                token not in cpp and token not in hpp
                for token in (
                    "cachedRoamProfileConfig",
                    "hasCachedRoamProfileConfig",
                    "struct apple80211_roam_profile_config",
                )
            ),
            "scoped_roam_adapter_backend_absent": all(
                not source_contains(token)
                for token in (
                    "setRoamingProfileV6(",
                    "handleRoamProfileAsyncCallBack(",
                    "disable6GForRoamScans(",
                    "disable6GForRoamScansCallback(",
                    "applyRoamingCandidateBoost(",
                    "configureMultiAPBit(",
                    "sendIOVarSet(",
                    "runIOVarSet(",
                    "\"roam_prof\"",
                    "\"join_pref\"",
                    "\"roam_multi_ap_env\"",
                )
            ),
            "historical_claims_corrected": correction_heading in signal_audit
            and "modern-profile recovery demonstrates a RoamAdapter policy and transport lifecycle and is reclassified"
            in normalized_signal_audit
            and "Modern `setWCL_ROAM_PROFILE_CONFIG` is now explicitly a no-local-backend quarantine"
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
        raise ValueError("WCL Roam Profile quarantine checks failed: " + ", ".join(failed))
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
        print(f"WCL Roam Profile quarantine validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
