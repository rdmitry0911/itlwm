#!/usr/bin/env python3
"""Generate and verify WCL Roam Lock false-success quarantine evidence."""

import argparse
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/wcl_roam_lock_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-479-wcl-roam-lock-quarantine-20260714.md"
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
    normalized_signal_audit = " ".join(signal_audit.split())
    setter = section(
        cpp,
        "setWCL_SET_ROAM_LOCK(apple80211_set_roam_lock *data)",
        "setVOICE_IND_STATE",
    )
    correction_heading = "## Q13 correction: WCL Roam Lock is RoamAdapter-backed"
    return {
        "schema": "itlwm-wcl-roam-lock-quarantine-v1",
        "source_base_revision": "462c08ec925994d9854de3dcbc3d368c30bb883d",
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "infra_wrapper": "0x100018adc",
            "infra_virtual_offset": "0x4b0",
            "core_setter": "0x10011ed1e",
            "core_null_cold_path": "0x1002082a6",
            "roam_adapter_offset": "0x15c0",
            "adapter_setter": "0x10001e4e0",
            "adapter_callback": "0x10001e59e",
            "commander_send_iovar_set": "0x10017b900",
            "null_status": "0x16",
            "effective_input_byte": "0x0",
            "transport_payload_bytes": "0x4",
        },
        "local": {
            "matching_roam_adapter_backend_implemented": False,
            "request_false_success": False,
            "complete_public_carrier_layout_proven": False,
            "valid_input_or_error_is_apple_parity": False,
        },
        "checks": {
            "reference_note": all(
                token in note
                for token in (
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "0x100018adc",
                    "`+0x4b0`",
                    "0x10011ed1e",
                    "0x1002082a6",
                    "raw `0x16`",
                    "`+0x15c0`",
                    "0x10001e4e0",
                    "4-byte boolean",
                    "`\"roam_off\"`",
                    "0x10017b900",
                    "0x10001e59e",
                    "complete public carrier allocation",
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
                    "cachedWclRoamLocked",
                    "hasCachedWclRoamLock",
                    "data->",
                    "return kIOReturnSuccess;",
                )
            ),
            "pseudo_state_and_layout_removed": all(
                token not in cpp and token not in hpp
                for token in (
                    "cachedWclRoamLocked",
                    "hasCachedWclRoamLock",
                    "struct apple80211_set_roam_lock",
                )
            ),
            "scoped_roam_adapter_backend_absent": all(
                not source_contains(token)
                for token in (
                    "setRoamLock(",
                    "handleRoamOffAsyncCallBack(",
                    "sendIOVarSet(",
                    "runIOVarSet(",
                    "\"roam_off\"",
                )
            ),
            "stale_q13_claim_corrected": correction_heading in signal_audit
            and "roam-lock recovery demonstrates a RoamAdapter transport lifecycle and is reclassified"
            in normalized_signal_audit
            and "- `setWCL_SET_ROAM_LOCK`\n- `setHEARTBEAT`" not in signal_audit,
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
        raise ValueError("WCL Roam Lock quarantine checks failed: " + ", ".join(failed))
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
        print(f"WCL Roam Lock quarantine validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
