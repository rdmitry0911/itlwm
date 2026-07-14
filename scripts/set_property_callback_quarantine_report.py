#!/usr/bin/env python3
"""Generate and verify SET_PROPERTY callback quarantine evidence."""

import argparse
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/set_property_callback_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-479-set-property-callback-quarantine-20260714.md"
INVENTORY = ROOT / "docs/tahoe_discrepancy_inventory.md"
SIGNAL_AUDIT = ROOT / "docs/tahoe_signal_chain_audit.md"
CPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
HPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.hpp"
INFRA_PROTOCOL = ROOT / "include/Airport/IO80211InfraProtocol.h"
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
    infra_protocol = INFRA_PROTOCOL.read_text(encoding="utf-8")
    note = " ".join(NOTE.read_text(encoding="utf-8").split())
    inventory = INVENTORY.read_text(encoding="utf-8")
    signal_audit = SIGNAL_AUDIT.read_text(encoding="utf-8")
    setter = section(
        cpp,
        "setSET_PROPERTY(apple80211_set_property_unserialized_data *data)",
        "setSENSING_ENABLE(apple80211_sensing_enable_t *)",
    )

    return {
        "schema": "itlwm-set-property-callback-quarantine-v1",
        "source_base_revision": "84828a3810ca1e785c6882d2f4c0b99e59f2df02",
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "infra_wrapper": "0x100018914",
            "core_set_vtable_offset": "+0x728",
            "core_set_vtable_entry": "0x1003a1810",
            "core_setter": "0x1000da8de",
            "effective_carrier_pointer_offset": "+0x8",
            "core_state_root_offset": "+0x48",
            "gate_predicate_state_offset": "+0x7950",
            "gate_predicate_vtable_offset": "+0x90",
            "inflight_state_offset": "+0x794b",
            "core_callback_vtable_offset": "+0x790",
            "core_callback_vtable_entry": "0x1003a1878",
            "core_callback_target": "0x1000da982",
            "core_gate_vtable_offset": "+0x68",
            "command_gate_execute_vtable_offset": "+0x38",
            "gated_helper": "0x1000da8aa",
        },
        "local": {
            "opaque_carrier_abi_defined": False,
            "bsd_ioc_route": False,
            "property_callback_backend": False,
            "request_false_success": False,
            "null_return_is_apple_parity": False,
            "valid_input_return_is_apple_parity": False,
        },
        "checks": {
            "reference_note": all(
                token in note
                for token in (
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "`0x100018914`",
                    "`+0x728`",
                    "`0x1003a1810`",
                    "`0x1000da8de`",
                    "no NULL gate",
                    "`(Core + 0x48) + 0x7950`",
                    "`+0x90`",
                    "`+0x8`",
                    "`(Core + 0x48) + 0x794b`",
                    "`+0x790`",
                    "`0x1003a1878`",
                    "setProperties(OSObject*)",
                    "`0x1000da982`",
                    "virtual `+0x68`",
                    "virtual `+0x38`",
                    "`0x1000da8aa`",
                    "does not claim Apple NULL",
                    "callback, or valid-input return-code parity",
                )
            ),
            "setter_retains_local_null_safety_and_quarantines_without_reading": all(
                token in setter
                for token in (
                    "if (data == nullptr)",
                    "return kIOReturnBadArgumentTahoe;",
                    "(void)data;",
                    "return kIOReturnUnsupported;",
                    "port has neither callback nor",
                )
            )
            and all(
                token not in setter
                for token in (
                    "data->",
                    "reinterpret_cast",
                    "cachedSetPropertyIoctlSeen",
                    "return kIOReturnSuccess;",
                )
            ),
            "synthetic_state_removed": "cachedSetPropertyIoctlSeen" not in cpp + hpp,
            "virtual_slot_retained": (
                "virtual IOReturn setSET_PROPERTY(apple80211_set_property_unserialized_data *) override;"
                in hpp
            ),
            "opaque_abi_remains_forward_only": (
                "struct apple80211_set_property_unserialized_data;" in infra_protocol
                and not source_contains("struct apple80211_set_property_unserialized_data {")
                and not source_contains("APPLE80211_IOC_SET_PROPERTY")
            ),
            "scoped_callback_backend_absent": all(
                not source_contains(token)
                for token in (
                    "setPropertyIoctlGated(",
                    "cachedSetPropertyIoctlSeen",
                    "runSetPropertyIoctl",
                )
            ),
            "historical_claims_corrected": (
                "## 2026-07-14 correction: `SET_PROPERTY` is a gated callback control plane"
                in signal_audit
                and "Q13 correction: SET_PROPERTY callback quarantine" in inventory
                and "Preserve the caller-visible \"delegated setter\" contract" not in cpp
                and "`setSET_PROPERTY`, `setSENSING_DISABLE`" not in signal_audit
                and "`setSET_PROPERTY`, `setSENSING_DISABLE`" not in inventory
                and "- `582 setSET_PROPERTY`" not in inventory
            ),
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
        raise ValueError("SET_PROPERTY callback quarantine checks failed: " + ", ".join(failed))
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
        print(f"SET_PROPERTY callback quarantine validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
