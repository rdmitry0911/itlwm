#!/usr/bin/env python3
"""Generate and verify EAP_FILTER_CONFIG false-success quarantine evidence."""

import argparse
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/eap_filter_config_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-479-eap-filter-config-quarantine-20260713.md"
SIGNAL_AUDIT = ROOT / "docs/tahoe_signal_chain_audit.md"
STALE_ANALYSIS = ROOT / "analysis/ANALYSIS_REPORT_2026-04-23.md"
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
    stale_analysis = STALE_ANALYSIS.read_text(encoding="utf-8")
    setter = section(
        cpp,
        "setEAP_FILTER_CONFIG(apple80211_eap_filter_config *data)",
        "setWCL_ASSOCIATED_SLEEP",
    )
    correction_heading = "## Q13 correction: `setEAP_FILTER_CONFIG` is packet-filter-backed"
    return {
        "schema": "itlwm-eap-filter-config-quarantine-v1",
        "source_base_revision": "067201ee76159c176b5536e5b86c1c2c5a900a4d",
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "infra_wrapper": "0x1000191ac",
            "core_setter": "0x10014294e",
            "null_status": "0xe00002bc",
            "state_offset": "0x0",
            "core_state_offset": "0x4d48",
            "packet_filters_owner": "0x10012f310",
            "delete_callsite": "0x10012f780",
            "configure_callsite": "0x10012f788",
            "delete_filter": "0x100135d70",
            "configure_filter": "0x100135022",
            "packet_filter_iovar": "pkt_filter_add",
            "commander_run_iovar_set": "0x10017b6e6",
        },
        "local": {
            "matching_eap_filter_backend_implemented": False,
            "request_false_success": False,
            "full_carrier_layout_proven": False,
            "valid_input_return_is_apple_parity": False,
        },
        "checks": {
            "reference_note": all(
                token in note
                for token in (
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "0x1000191ac",
                    "0x10014294e",
                    "`0xe00002bc`",
                    "`+0x4d48`",
                    "0x10012f310",
                    "0x10012f780",
                    "0x10012f788",
                    "0x100135022",
                    "`pkt_filter_add`",
                    "0x10017b6e6",
                    "0x100135d70",
                    "complete public-carrier allocation",
                    "valid-input return-code parity",
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
                    "cachedEapFilterConfig",
                    "return kIOReturnSuccess;",
                )
            ),
            "pseudo_state_removed": "cachedEapFilterConfig" not in cpp
            and "cachedEapFilterConfig" not in hpp,
            "scoped_packet_filter_backend_absent": all(
                not source_contains(token)
                for token in (
                    "configureEapolFilter(",
                    "deleteEapolFilter(",
                    "runIOVarSet(",
                )
            ),
            "stale_cache_claim_corrected": correction_heading in signal_audit
            and "`setEAP_FILTER_CONFIG(...)`" not in signal_audit[
                signal_audit.index("## Q13 Minimal Setter-Contract Zone"):
                signal_audit.index(correction_heading)
            ]
            and "current local handling already satisfies the visible carrier contract"
            not in stale_analysis,
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
        raise ValueError("EAP_FILTER_CONFIG quarantine checks failed: " + ", ".join(failed))
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
        print(f"EAP_FILTER_CONFIG quarantine validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
