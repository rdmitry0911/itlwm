#!/usr/bin/env python3
"""Generate and verify WCL ARP mode false-success quarantine evidence."""

import argparse
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/wcl_arp_mode_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-479-wcl-arp-mode-quarantine-20260713.md"
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
        "setWCL_ARP_MODE(apple80211_wcl_arp_mode *data)",
        "setWCL_CONFIG_BG_MOTIONPROFILE",
    )
    offload_arp = section(
        cpp,
        "setOFFLOAD_ARP(apple80211_offload_arp_data *data)",
        "setGAS_REQ",
    )
    inventory_q7 = section(
        inventory,
        "### 3. Former WCL adapter-plane stub cluster is closed as a queue",
        "### 4.",
    )
    correction_heading = "## Q13 correction: WCL ARP mode is KeepAlive/WnmAdapter-backed"
    return {
        "schema": "itlwm-wcl-arp-mode-quarantine-v1",
        "source_base_revision": "fb8a8d11ee24dae019110795f8cc1658bc3bf7e3",
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "infra_wrapper": "0x100018cec",
            "core_setter": "0x1001e85e8",
            "null_status": "0xe00002bc",
            "mode_offset": "0x8",
            "enabled_offset": "0x10",
            "program_arp_keepalive": "0x1000d9d6e",
            "stop_arp_keepalive": "0x1000d9cba",
            "program_garp": "0x10009cdea",
            "stop_garp": "0x10009d1e6",
            "wnm_adapter_offset": "0x15b0",
            "configure_wnm_keepalives": "0x1000ad5a0",
            "wnm_sideband_offsets": ["0x0", "0x2", "0x4", "0x5"],
        },
        "local": {
            "matching_keepalive_garp_wnm_backend_implemented": False,
            "request_false_success": False,
            "full_carrier_layout_proven": False,
            "valid_input_or_error_is_apple_parity": False,
        },
        "checks": {
            "reference_note": all(
                token in note
                for token in (
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "0x100018cec",
                    "0x1001e85e8",
                    "`0xe00002bc`",
                    "`+0x8`",
                    "`+0x10`",
                    "0x1000d9d6e",
                    "0x1000d9cba",
                    "0x10009cdea",
                    "0x10009d1e6",
                    "`+0x15b0`",
                    "0x1000ad5a0",
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
                    "cachedWclArpMode",
                    "hasCachedWclArpMode",
                    "data->",
                    "setOFFLOAD_ARP",
                    "return kIOReturnSuccess;",
                )
            ),
            "pseudo_state_and_layout_removed": all(
                token not in cpp and token not in hpp
                for token in (
                    "cachedWclArpMode",
                    "hasCachedWclArpMode",
                    "struct apple80211_wcl_arp_mode",
                )
            ),
            "scoped_keepalive_wnm_backend_absent": all(
                not source_contains(token)
                for token in (
                    "programARPKeepAlive(",
                    "stopARPKeepAlive(",
                    "programGARP(",
                    "stopGARP(",
                    "configureWNMKeepAlives(",
                )
            ),
            "direct_offload_remains_separate_quarantine": all(
                token in offload_arp
                for token in (
                    "return kApple80211ErrInvalidArgumentRaw;",
                    "Intel has no counterpart.",
                )
            )
            and "return kIOReturnSuccess;" not in offload_arp,
            "stale_q7_claim_corrected": correction_heading in signal_audit
            and "`setWCL_ARP_MODE` is excluded from that functional closure"
            in inventory_q7
            and "- `setWCL_ARP_MODE`" not in inventory_q7,
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
        raise ValueError("WCL ARP mode quarantine checks failed: " + ", ".join(failed))
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
        print(f"WCL ARP mode quarantine validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
