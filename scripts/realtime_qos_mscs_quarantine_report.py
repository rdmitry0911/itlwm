#!/usr/bin/env python3
"""Generate and verify REALTIME_QOS_MSCS false-success quarantine evidence."""

import argparse
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/realtime_qos_mscs_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-479-realtime-qos-mscs-quarantine-20260713.md"
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
        "setREALTIME_QOS_MSCS(apple80211_state_data *data)",
        "setRSN_XE",
    )
    correction_heading = "## Q13 correction: `setREALTIME_QOS_MSCS` is QoS/MSCS-backed"
    return {
        "schema": "itlwm-realtime-qos-mscs-quarantine-v1",
        "source_base_revision": "70512f429121d631dac5a92f8b50ef6abc31d01f",
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "infra_wrapper": "0x1000189ac",
            "core_virtual_offset": "0x7b0",
            "core_setter": "0x1001e81a4",
            "feature_bit": "0x5f",
            "qos_enabled_offset": "0x7579",
            "bss_mscs_capability_virtual": "0x290",
            "state_offset": "0x4",
            "mscs_enabled_offset": "0x757b",
            "sender": "0x10013d028",
            "configurator": "0x10013cda6",
            "qos_iovar": "WL_QOS_CMD_RAV_MSCS",
            "qos_request_bytes": 16,
            "event_handler": "0x1001de8dc",
            "null_status_after_gates": "0x16",
        },
        "local": {
            "matching_qos_mscs_backend_implemented": False,
            "request_false_success": False,
            "null_status_is_apple_parity": False,
            "valid_input_return_is_apple_parity": False,
        },
        "checks": {
            "reference_note": all(
                token in note
                for token in (
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "0x1000189ac",
                    "`+0x7b0`",
                    "0x1001e81a4",
                    "`0x5f`",
                    "`+0x7579`",
                    "`+0x290`",
                    "`+0x757b`",
                    "0x10013d028",
                    "0x10013cda6",
                    "`WL_QOS_CMD_RAV_MSCS`",
                    "qosSetIOVar",
                    "0x1001de8dc",
                    "`0x16`",
                    "complete public carrier allocation",
                    "does not claim Apple null-input status",
                    "not Apple valid-input return-code parity",
                )
            ),
            "setter_quarantines_nonnull": all(
                token in setter
                for token in (
                    "if (data == nullptr)",
                    "return kApple80211ErrInvalidArgumentRaw;",
                    "return kIOReturnUnsupported;",
                )
            )
            and all(
                token not in setter
                for token in (
                    "cachedRealTimeQosMscs",
                    "return kIOReturnSuccess;",
                )
            ),
            "pseudo_state_removed": "cachedRealTimeQosMscs" not in cpp
            and "cachedRealTimeQosMscs" not in hpp,
            "scoped_qos_mscs_anchor_literals_absent": all(
                not source_contains(token)
                for token in (
                    "sendQoSMgmtMSCSReq(",
                    "confiQoSMgmtMSCS(",
                    "qosSetIOVar(",
                    "handleMSCSEvent(",
                )
            ),
            "opaque_cache_classification_removed": all(
                token in signal_audit
                for token in (
                    correction_heading,
                    "`0x1000189ac`",
                    "`+0x7b0`",
                    "`0x1001e81a4`",
                    "`0x10013d028`",
                    "`0x10013cda6`",
                    "`WL_QOS_CMD_RAV_MSCS`",
                    "`0x1001de8dc`",
                    "kIOReturnUnsupported",
                )
            )
            and "`setREALTIME_QOS_MSCS(...)` were already lifted in code"
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
        raise ValueError("REALTIME_QOS_MSCS quarantine checks failed: " + ", ".join(failed))
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
        print(f"REALTIME_QOS_MSCS quarantine validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
