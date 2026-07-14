#!/usr/bin/env python3
"""Generate and verify BGSCAN false-success quarantine evidence."""

import argparse
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/bgscan_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-479-bgscan-quarantine-20260713.md"
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
        "setWCL_CONFIG_BGSCAN(apple80211_bg_scan *data)",
        "setWCL_CONFIG_BG_PARAMS",
    )
    inventory_q7 = section(
        inventory,
        "### 3. Former WCL adapter-plane stub cluster: historical lift versus owner parity",
        "### 4.",
    )
    correction_heading = "## Q13 correction: BGScanAdapter-backed producer quarantines"
    return {
        "schema": "itlwm-bgscan-quarantine-v1",
        "source_base_revision": "a02e2a5ac95c42c5fe979339fbe448db6a5807e0",
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "infra_wrapper": "0x10001928c",
            "core_setter": "0x100142b8a",
            "null_status": "0xe00002bc",
            "bgscan_adapter_offset": "0x1578",
            "adapter_setter": "0x10000f852",
            "configure_pfn": "0x10000f516",
            "config_pno": "0x10000fa18",
            "config_epno": "0x10000fc20",
            "pno_iovar": "scan_nprobes",
            "pno_payload_bytes": 4,
            "commander_run_iovar_set": "0x10017b6e6",
            "control_bytes": [0, 1, 2, 3, 4],
        },
        "local": {
            "matching_bgscan_pfn_pno_epno_backend_implemented": False,
            "request_false_success": False,
            "full_carrier_layout_proven": False,
            "valid_input_or_error_is_apple_parity": False,
        },
        "checks": {
            "reference_note": all(
                token in note
                for token in (
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "0x10001928c",
                    "0x100142b8a",
                    "`0xe00002bc`",
                    "`+0x1578`",
                    "0x10000f852",
                    "0x10000f516",
                    "0x10000fa18",
                    "0x10000fc20",
                    "`scan_nprobes`",
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
                    "cachedBgScanConfig",
                    "hasCachedBgScanConfig",
                    "data->raw",
                    "ic_bgscan_start",
                    "IEEE80211_F_BGSCAN",
                    "return kIOReturnSuccess;",
                )
            ),
            "pseudo_state_and_layout_removed": all(
                token not in cpp and token not in hpp
                for token in (
                    "cachedBgScanConfig",
                    "hasCachedBgScanConfig",
                    "struct apple80211_bg_scan",
                )
            ),
            "scoped_bgscan_backend_absent": all(
                not source_contains(token)
                for token in (
                    "AppleBCMWLANBGScanAdapter",
                    "configurePFN(",
                    "configPNO(",
                    "configEPNO(",
                    "scan_nprobes",
                    "runIOVarSet(",
                )
            ),
            "stale_q7_claim_corrected": correction_heading in signal_audit
            and "`setWCL_CONFIG_BG_MOTIONPROFILE`, `setWCL_CONFIG_BG_NETWORK`,\n`setWCL_CONFIG_BGSCAN`, and `setWCL_CONFIG_BG_PARAMS` are excluded from that\nfunctional closure"
            in inventory_q7
            and "- `setWCL_CONFIG_BGSCAN`" not in inventory_q7,
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
        raise ValueError("BGSCAN quarantine checks failed: " + ", ".join(failed))
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
        print(f"BGSCAN quarantine validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
