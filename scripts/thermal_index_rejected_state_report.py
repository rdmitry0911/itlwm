#!/usr/bin/env python3
"""Generate and verify THERMAL_INDEX rejected-state correction evidence."""

import argparse
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/thermal_index_rejected_state_report.json"
NOTE = ROOT / "docs/reference/CR-479-thermal-index-rejected-state-20260714.md"
INVENTORY = ROOT / "docs/tahoe_discrepancy_inventory.md"
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
    note = " ".join(NOTE.read_text(encoding="utf-8").split())
    inventory = INVENTORY.read_text(encoding="utf-8")
    signal_audit = SIGNAL_AUDIT.read_text(encoding="utf-8")
    getter = section(
        cpp,
        "getTHERMAL_INDEX(apple80211_thermal_index_t *data)",
        "getPOWER_BUDGET(apple80211_power_budget_t *data)",
    )
    setter = section(
        cpp,
        "setTHERMAL_INDEX(apple80211_thermal_index_t *data)",
        "setDYNAMIC_RSSI_WINDOW_CONFIG",
    )
    dispatch = section(
        cpp,
        "case APPLE80211_IOC_THERMAL_INDEX:",
        "case APPLE80211_IOC_POWER_BUDGET:",
    )

    return {
        "schema": "itlwm-thermal-index-rejected-state-v1",
        "source_base_revision": "e47979db54a949d262f4bc82c83a8cbde77c1002",
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "infra_wrapper": "0x100018760",
            "core_vtable_offset": "+0x4f0",
            "core_setter": "0x100120586",
            "core_getter": "0x100106eda",
            "feature_helper": "0x1000c846e",
            "feature_bit": "0x3b",
            "valid_range": "1..100",
            "firmware_iovar": "tvpm",
            "firmware_set": "0x10017b6e6",
            "payload_bytes": 12,
            "state_offset": "0x48+0x0",
            "special_commit_status": "0xe3ff8117",
            "allocation_failure": "0x0c",
        },
        "local": {
            "thermal_owner_backend": False,
            "request_false_state": False,
            "getter_is_baseline_only": True,
            "valid_input_return_is_apple_parity": False,
        },
        "checks": {
            "reference_note": all(
                token in note
                for token in (
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "`0x100018760`",
                    "`+0x4f0`",
                    "`0x100120586`",
                    "featureFlagIsBitSet(0x3b)",
                    "`0x1000c846e`",
                    "`0xe00002bc`",
                    "`1..100`",
                    "12-byte",
                    "`+0x8`",
                    "runIOVarSet(\"tvpm\")",
                    "`0x10017b6e6`",
                    "`0xe3ff8117`",
                    "raw transport status",
                    "`0x0c`",
                    "`0x100106eda`",
                    "does not claim a complete enabled-path NULL contract",
                    "does not claim Apple valid-input return-code parity",
                )
            ),
            "setter_rejects_without_consuming_carrier": all(
                token in setter
                for token in (
                    "(void)data;",
                    "return kIOReturnBadArgumentTahoe;",
                )
            )
            and all(
                token not in setter
                for token in (
                    "data->",
                    "reinterpret_cast",
                    "cachedThermalIndex",
                    "return kIOReturnSuccess;",
                )
            ),
            "getter_is_default_only": all(
                token in getter
                for token in (
                    "memset(data, 0, sizeof(*data));",
                    "data->version = APPLE80211_VERSION;",
                    "data->thermal_index = 0;",
                    "return kIOReturnSuccess;",
                )
            )
            and "cachedThermalIndex" not in getter,
            "synthetic_rejected_state_removed": "cachedThermalIndex" not in cpp + hpp,
            "interface_slots_retained": all(
                token in hpp
                for token in (
                    "virtual IOReturn getTHERMAL_INDEX(apple80211_thermal_index_t *) override;",
                    "virtual IOReturn setTHERMAL_INDEX(apple80211_thermal_index_t *) override;",
                )
            ),
            "normal_bsd_setter_is_not_exposed": all(
                token in dispatch
                for token in (
                    "cmd == SIOCGA80211",
                    "getTHERMAL_INDEX",
                    "kIOReturnUnsupported",
                )
            )
            and "setTHERMAL_INDEX" not in dispatch,
            "scoped_owner_absent": all(
                not source_contains(token)
                for token in (
                    '\"tvpm\"',
                    "setThermalIndexToFirmware",
                    "configureThermalIndex",
                )
            ),
            "historical_claims_corrected": (
                "### `THERMAL_INDEX` correction: rejected setters must not manufacture getter state"
                in signal_audit
                and "Q13 correction: THERMAL_INDEX rejected-setter state" in inventory
                and "ends on the Apple bad-argument path rather than a simple carrier write"
                not in signal_audit
                and "*(param_1 + 0x128) + 0x0" not in signal_audit
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
        raise ValueError("THERMAL_INDEX rejected-state checks failed: " + ", ".join(failed))
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
        print(f"THERMAL_INDEX rejected-state validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
