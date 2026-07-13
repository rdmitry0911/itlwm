#!/usr/bin/env python3
"""Generate and verify BG motion-profile false-success quarantine evidence."""

import argparse
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/bg_motion_profile_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-479-bg-motion-profile-quarantine-20260713.md"
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
    setter = section(
        cpp,
        "setWCL_CONFIG_BG_MOTIONPROFILE(apple80211_bg_motion_profile *data)",
        "setWCL_CONFIG_BG_NETWORK",
    )
    inventory_q7 = section(
        inventory,
        "### 3. Former WCL adapter-plane stub cluster is closed as a queue",
        "### 4.",
    )
    correction_heading = (
        "## Q13 correction: BG motion-profile, BG network, and BG params are BGScanAdapter-backed"
    )
    return {
        "schema": "itlwm-bg-motion-profile-quarantine-v1",
        "source_base_revision": "d4085ec9753b2ce7a30a470252418fa10bb89a33",
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "infra_wrapper": "0x10001921c",
            "core_setter": "0x100142b46",
            "null_status": "0xe00002bc",
            "bgscan_adapter_offset": "0x1578",
            "adapter_setter": "0x10000e856",
            "mapping": "0x10000e96e",
            "mapping_iovar": "mpf_map",
            "pno": "0x10000eb3a",
            "epno": "0x10000ec9a",
            "pno_epno_iovar": "pfn_mpfset",
            "commander_run_iovar_set": "0x10017b6e6",
            "partial_pno_gate": "data+0x1",
        },
        "local": {
            "matching_bgscan_backend_implemented": False,
            "request_false_success": False,
            "full_carrier_layout_proven": False,
            "valid_input_or_error_is_apple_parity": False,
        },
        "checks": {
            "reference_note": all(
                token in note
                for token in (
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "0x10001921c",
                    "0x100142b46",
                    "`0xe00002bc`",
                    "`+0x1578`",
                    "0x10000e856",
                    "0x10000e96e",
                    "`mpf_map`",
                    "0x10017b6e6",
                    "0x10000eb3a",
                    "0x10000ec9a",
                    "`pfn_mpfset`",
                    "`data + 1`",
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
                    "data->raw",
                    "cachedBgMotionProfile",
                    "hasCachedBgMotionProfile",
                    "return kIOReturnSuccess;",
                )
            ),
            "pseudo_state_and_layout_removed": all(
                token not in cpp and token not in hpp
                for token in (
                    "cachedBgMotionProfile",
                    "hasCachedBgMotionProfile",
                    "struct apple80211_bg_motion_profile",
                )
            ),
            "scoped_bgscan_backend_absent": all(
                not source_contains(token)
                for token in (
                    "configureMotionProfileMapping(",
                    "configureMotionProfilePNO(",
                    "configureMotionProfileEPNO(",
                    "mpf_map",
                    "pfn_mpfset",
                    "runIOVarSet(",
                )
            ),
            "stale_q7_claim_corrected": correction_heading in signal_audit
            and "`setWCL_CONFIG_BG_MOTIONPROFILE`, `setWCL_CONFIG_BG_NETWORK`, and\n`setWCL_CONFIG_BG_PARAMS` are excluded from that functional closure"
            in inventory_q7
            and "- `setWCL_CONFIG_BG_MOTIONPROFILE`" not in inventory_q7,
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
        raise ValueError("BG motion-profile quarantine checks failed: " + ", ".join(failed))
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
        print(f"BG motion-profile quarantine validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
