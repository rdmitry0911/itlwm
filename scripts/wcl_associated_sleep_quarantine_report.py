#!/usr/bin/env python3
"""Generate and verify WCL associated-sleep false-success quarantine evidence."""

import argparse
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/wcl_associated_sleep_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-479-wcl-associated-sleep-quarantine-20260713.md"
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
        "setWCL_ASSOCIATED_SLEEP(apple80211_associated_sleep_config *data)",
        "setWCL_SOI_CONFIG",
    )
    return {
        "schema": "itlwm-wcl-associated-sleep-quarantine-v1",
        "source_base_revision": "32a5572d1ab0d2f1c71ca34569d84beadbbc8df6",
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "infra_wrapper": "0x100019498",
            "infra_virtual_offset": "0x778",
            "core_setter": "0x100142fce",
            "power_state_adapter_offset": "0x8c88",
            "beacon_soi": "0x100041352",
            "data_soi": "0x100041a0c",
            "excess_pm_alert": "0x100041ce6",
            "roam_scan_associated_sleep": "0x100041eb6",
        },
        "local": {
            "backend_power_state_adapter": False,
            "request_false_success": False,
            "null_status_is_apple_parity": False,
            "valid_input_return_is_apple_parity": False,
        },
        "checks": {
            "reference_note": all(
                token in note
                for token in (
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "0x100019498",
                    "`+0x778`",
                    "0x100142fce",
                    "`+0x8c88`",
                    "0x100041352",
                    "0x100041a0c",
                    "0x100041ce6",
                    "0x100041eb6",
                    "no Apple null-input status claim",
                    "complete public carrier allocation",
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
                    "cachedAssociatedSleepConfig",
                    "hasCachedAssociatedSleepConfig",
                    "return kIOReturnSuccess;",
                )
            ),
            "pseudo_state_removed": all(
                token not in cpp and token not in hpp
                for token in (
                    "cachedAssociatedSleepConfig",
                    "hasCachedAssociatedSleepConfig",
                )
            ),
            "intel_source_has_no_power_state_adapter_backend": all(
                not source_contains(token)
                for token in (
                    "PowerStateAdapter",
                    "configureBeaconSOI(",
                    "configureDataSOI(",
                    "configureExcessPMAlert(",
                    "configureRoamScanForAssociatedSleep(",
                )
            ),
            "stale_cache_claim_corrected": all(
                token in signal_audit
                for token in (
                    "setWCL_ASSOCIATED_SLEEP` is PowerStateAdapter-backed",
                    "`+0x778`",
                    "`+0x8c88`",
                    "kIOReturnUnsupported",
                )
            )
            and "setWCL_ASSOCIATED_SLEEP(...)\n- `setWCL_SOI_CONFIG" not in signal_audit,
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
            "WCL associated-sleep quarantine checks failed: " + ", ".join(failed)
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
            f"WCL associated-sleep quarantine validation failed: {exc}",
            file=sys.stderr,
        )
        sys.exit(1)
