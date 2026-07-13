#!/usr/bin/env python3
"""Generate and verify WCL WNM OPS false-success quarantine evidence."""

import argparse
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/wcl_wnm_ops_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-479-wcl-wnm-ops-quarantine-20260713.md"
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
        "setWCL_WNM_OPS(apple80211_wcl_wnm_config_t *data)",
        "setWCL_WNM_OFFLOAD",
    )
    correction_heading = "## Q13 correction: `setWCL_WNM_OPS` is WnmAdapter-backed"
    return {
        "schema": "itlwm-wcl-wnm-ops-quarantine-v1",
        "source_base_revision": "8f5a854da08b2ada95882532a61302e97d078b22",
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "infra_wrapper": "0x100019abe",
            "core_setter": "0x1001429b0",
            "wnm_adapter_offset": "0x15b0",
            "wnm_adapter_setter": "0x1000a7ff0",
            "configure_enterprise": "0x1000a8280",
            "configure_product_info": "0x1000a8480",
            "configure_beacon_reporting": "0x1000a9180",
            "configure_wnm": "0x1000aa9e0",
            "wnm_iovar": "wnm",
            "commander_run_iovar_set": "0x10017b6e6",
            "wnm_payload_bytes": 4,
            "null_status": "0xe00002bc",
        },
        "local": {
            "matching_wnm_configuration_backend_implemented": False,
            "request_false_success": False,
            "null_status_is_apple_parity": False,
            "valid_input_return_is_apple_parity": False,
        },
        "checks": {
            "reference_note": all(
                token in note
                for token in (
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "0x100019abe",
                    "0x1001429b0",
                    "`+0x15b0`",
                    "0x1000a7ff0",
                    "0x1000a8280",
                    "0x1000a8480",
                    "0x1000a9180",
                    "0x1000aa9e0",
                    "0x10017b6e6",
                    "`0xe00002bc`",
                    "`wnm`",
                    "four-byte transmit payload",
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
                    "cachedWnmConfig",
                    "hasCachedWnmConfig",
                    "return kIOReturnSuccess;",
                )
            ),
            "pseudo_state_removed": all(
                token not in cpp and token not in hpp
                for token in ("cachedWnmConfig", "hasCachedWnmConfig")
            ),
            "scoped_wnm_configuration_anchor_literals_absent": all(
                not source_contains(token)
                for token in (
                    "configureWnmFeatures(",
                    "configureEnterpriseFeatures(",
                    "configureProductInfoReporting(",
                    "configureBeaconReporting(",
                    "configureWNM(",
                )
            ),
            "opaque_cache_classification_removed": all(
                token in signal_audit
                for token in (
                    correction_heading,
                    "`0x100019abe`",
                    "`0x1001429b0`",
                    "`+0x15b0`",
                    "`0x1000a7ff0`",
                    "`0x1000aa9e0`",
                    "wnm",
                    "runIOVarSet",
                    "kIOReturnUnsupported",
                    "`setWCL_WNM_OPS` now returns `kIOReturnUnsupported` locally",
                )
            )
            and "`setWCL_WNM_OPS` preserves its separately recovered caller blob"
            not in signal_audit
            and "`setWCL_WNM_OPS(...)` remains a separately lifted Apple"
            not in signal_audit,
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
        raise ValueError("WCL WNM OPS quarantine checks failed: " + ", ".join(failed))
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
        print(f"WCL WNM OPS quarantine validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
