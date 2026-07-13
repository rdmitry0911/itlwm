#!/usr/bin/env python3
"""Generate and verify Dynamic RSSI Window false-success quarantine evidence."""

import argparse
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/dynamic_rssi_window_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-479-dynamic-rssi-window-quarantine-20260713.md"
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
    note = NOTE.read_text(encoding="utf-8")
    signal_audit = SIGNAL_AUDIT.read_text(encoding="utf-8")
    setter = section(
        cpp,
        "setDYNAMIC_RSSI_WINDOW_CONFIG(apple80211_dynamic_rssi_window_config *data)",
        "setREALTIME_QOS_MSCS",
    )
    correction_heading = (
        "## Q13 correction: `setDYNAMIC_RSSI_WINDOW_CONFIG` is "
        "ConfigManager-backed"
    )
    prior_to_correction = signal_audit[:signal_audit.index(correction_heading)]
    return {
        "schema": "itlwm-dynamic-rssi-window-quarantine-v1",
        "source_base_revision": "780f5f576c6c41ce604f3af233995b55a6b38b32",
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "infra_wrapper": "0x100019530",
            "core_setter": "0x10014365e",
            "core_configure": "0x100140672",
            "config_manager_offset": "0x1558",
            "config_manager_setter": "0x10008c6a6",
            "valid_range": "2..16",
            "null_status": "0xe00002bc",
            "invalid_status": "0xe00002c2",
            "feature_unavailable_status": "0xe00002c7",
            "rssi_iovar": "rssi_win",
            "rssi_payload_bytes": 4,
            "rssi_payload_or": "0x200",
            "snr_iovar": "snr_win",
            "snr_payload_bytes": 4,
        },
        "local": {
            "matching_dynamic_rssi_configurator_implemented": False,
            "request_false_success": False,
            "null_status_is_apple_parity": False,
            "valid_input_return_is_apple_parity": False,
        },
        "checks": {
            "reference_note": all(
                token in note
                for token in (
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "0x100019530",
                    "0x10014365e",
                    "0x100140672",
                    "`+0x1558`",
                    "0x10008c6a6",
                    "`0xe00002bc`",
                    "`0xe00002c2`",
                    "`0xe00002c7`",
                    "`rssi_win`",
                    "`snr_win`",
                    "`0x200`",
                    "complete public carrier allocation",
                    "does not claim Apple null-input status",
                    "not Apple valid-input return-code parity",
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
                    "cachedDynamicRssiWindowConfig",
                    "return kIOReturnSuccess;",
                )
            ),
            "pseudo_state_removed": "cachedDynamicRssiWindowConfig" not in cpp
            and "cachedDynamicRssiWindowConfig" not in hpp,
            "scoped_dynamic_rssi_anchor_literals_absent": all(
                not source_contains(token)
                for token in (
                    "configureDynamicRssiWindow(",
                    '"rssi_win"',
                    '"snr_win"',
                    "runIOVarSet(",
                )
            ),
            "opaque_cache_classification_removed": all(
                token in signal_audit
                for token in (
                    "setDYNAMIC_RSSI_WINDOW_CONFIG` is ConfigManager-backed",
                    "`0x100019530`",
                    "`0x10014365e`",
                    "`0x100140672`",
                    "`+0x1558`",
                    "`0x10008c6a6`",
                    "rssi_win",
                    "snr_win",
                    "kIOReturnUnsupported",
                )
            )
            and "setDYNAMIC_RSSI_WINDOW_CONFIG(...)" not in prior_to_correction,
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
        raise ValueError(
            "Dynamic RSSI Window quarantine checks failed: " + ", ".join(failed)
        )
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
        print(
            f"Dynamic RSSI Window quarantine validation failed: {exc}",
            file=sys.stderr,
        )
        sys.exit(1)
