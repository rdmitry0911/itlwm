#!/usr/bin/env python3
"""Generate and verify WCL WNM Offload false-success quarantine evidence."""

import argparse
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/wcl_wnm_offload_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-479-wcl-wnm-offload-quarantine-20260713.md"
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
        "setWCL_WNM_OFFLOAD(apple80211_wcl_wnm_offload_t *data)",
        "extern OSDictionary *convertScanToDictionary",
    )
    correction_heading = "## Q13 correction: `setWCL_WNM_OFFLOAD` is WnmAdapter-backed"
    return {
        "schema": "itlwm-wcl-wnm-offload-quarantine-v1",
        "source_base_revision": "d6bb93490bc6f7e51db8626c125238b18d071289",
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "infra_wrapper": "0x100019af6",
            "core_setter": "0x1001429d2",
            "wnm_adapter_offset": "0x15b0",
            "wnm_adapter_setter": "0x1000a99e0",
            "unconfigure_offloads": "0x1000a9c80",
            "configure_offloads": "0x1000a9f60",
            "configure_dms": "0x1000ae2e0",
            "configure_wnm_dms_dependency": "0x1000ae160",
            "tclas_iovar": "tclas_add",
            "dms_iovar": "wnm_dms_set",
            "dependency_iovar": "wnm_dms_dependency",
            "null_status": "0xe00002bc",
        },
        "local": {
            "matching_wnm_offload_configurator_implemented": False,
            "request_false_success": False,
            "null_status_is_apple_parity": False,
            "valid_input_return_is_apple_parity": False,
        },
        "checks": {
            "reference_note": all(
                token in note
                for token in (
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "0x100019af6",
                    "0x1001429d2",
                    "`+0x15b0`",
                    "0x1000a99e0",
                    "0x1000a9c80",
                    "0x1000a9f60",
                    "0x1000ae2e0",
                    "0x1000ae160",
                    "`0xe00002bc`",
                    "`tclas_add`",
                    "`wnm_dms_set`",
                    "`wnm_dms_dependency`",
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
                    "cachedWnmOffload",
                    "hasCachedWnmOffload",
                    "return kIOReturnSuccess;",
                )
            ),
            "pseudo_state_removed": all(
                token not in cpp and token not in hpp
                for token in ("cachedWnmOffload", "hasCachedWnmOffload")
            ),
            "scoped_wnm_offload_anchor_literals_absent": all(
                not source_contains(token)
                for token in (
                    "configureWnmOffloadFeatures(",
                    "configureDMS(",
                    "configureWNMDMSDependency(",
                    '"tclas_add"',
                    '"wnm_dms_set"',
                    '"wnm_dms_dependency"',
                )
            ),
            "opaque_cache_classification_removed": all(
                token in signal_audit
                for token in (
                    correction_heading,
                    "`0x100019af6`",
                    "`0x1001429d2`",
                    "`+0x15b0`",
                    "`0x1000a99e0`",
                    "tclas_add",
                    "wnm_dms_set",
                    "wnm_dms_dependency",
                    "kIOReturnUnsupported",
                    "`setWCL_WNM_OFFLOAD` now returns `kIOReturnUnsupported` locally",
                )
            )
            and "setWCL_WNM_OPS` / `setWCL_WNM_OFFLOAD` preserve"
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
        raise ValueError("WCL WNM Offload quarantine checks failed: " + ", ".join(failed))
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
        print(f"WCL WNM Offload quarantine validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
