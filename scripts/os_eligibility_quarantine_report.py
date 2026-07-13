#!/usr/bin/env python3
"""Generate and verify OS eligibility false-success quarantine evidence."""

import argparse
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/os_eligibility_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-479-os-eligibility-quarantine-20260713.md"
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
        "setOS_ELIGIBILITY(apple80211_os_eligibility *data)",
        "setDBRG_ENTROPY",
    )
    prior_zone = section(
        signal_audit,
        "## Q13 Minimal Setter-Contract Zone:",
        "## Q13 correction: `setWCL_ASSOCIATED_SLEEP`",
    )
    return {
        "schema": "itlwm-os-eligibility-quarantine-v1",
        "source_base_revision": "60f22819afc1dcf88170c6d7444ea6d93be23e27",
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "infra_wrapper": "0x100019830",
            "infra_virtual_offset": "0x7d0",
            "core_setter": "0x100143ed6",
            "core_state_offset": "0x8cec",
            "net_adapter_offset": "0x15e0",
            "net_adapter_setter": "0x100014cc8",
            "edca_iovar": "wme_ac_sta",
        },
        "local": {
            "backend_aggressive_edca": False,
            "request_false_success": False,
            "null_status_is_apple_parity": False,
            "valid_input_return_is_apple_parity": False,
        },
        "checks": {
            "reference_note": all(
                token in note
                for token in (
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "0x100019830",
                    "`+0x7d0`",
                    "0x100143ed6",
                    "`+0x8cec`",
                    "`+0x15e0`",
                    "0x100014cc8",
                    "`wme_ac_sta`",
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
                for token in ("cachedOsEligibility", "return kIOReturnSuccess;")
            ),
            "pseudo_state_removed": "cachedOsEligibility" not in cpp
            and "cachedOsEligibility" not in hpp,
            "intel_source_has_no_edca_backend": all(
                not source_contains(token)
                for token in (
                    "configureAggressiveEDCA(",
                    '"wme_ac_sta"',
                    "configureShortRetryLimit(",
                )
            ),
            "stale_cache_claim_corrected": all(
                token in signal_audit
                for token in (
                    "setOS_ELIGIBILITY` is NetAdapter-backed",
                    "`+0x7d0`",
                    "`+0x15e0`",
                    "wme_ac_sta",
                    "kIOReturnUnsupported",
                )
            )
            and "setOS_ELIGIBILITY(...)" not in prior_zone,
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
            "OS eligibility quarantine checks failed: " + ", ".join(failed)
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
        print(f"OS eligibility quarantine validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
