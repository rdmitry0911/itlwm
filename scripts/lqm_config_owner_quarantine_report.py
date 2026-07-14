#!/usr/bin/env python3
"""Generate and verify LQM_CONFIG owner-boundary quarantine evidence."""

import argparse
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/lqm_config_owner_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-479-lqm-config-owner-quarantine-20260714.md"
TIMER_NOTE = ROOT / "docs/reference/CR-479-driver-owned-lqm-statistics-producer-20260711.md"
INVENTORY = ROOT / "docs/tahoe_discrepancy_inventory.md"
SIGNAL_AUDIT = ROOT / "docs/tahoe_signal_chain_audit.md"
CPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
HPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.hpp"
V2 = ROOT / "AirportItlwm/AirportItlwmV2.cpp"
CONTRACTS = ROOT / "AirportItlwm/TahoeLqmContracts.hpp"
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
    v2 = V2.read_text(encoding="utf-8")
    contracts = CONTRACTS.read_text(encoding="utf-8")
    note = " ".join(NOTE.read_text(encoding="utf-8").split())
    timer_note = TIMER_NOTE.read_text(encoding="utf-8")
    inventory = INVENTORY.read_text(encoding="utf-8")
    signal_audit = SIGNAL_AUDIT.read_text(encoding="utf-8")
    getter = section(
        cpp,
        "getLQM_CONFIG(apple80211_lqm_config_t *data)",
        "getLQM_SUMMARY(apple80211_lqm_summary *data)",
    )
    setter = section(
        cpp,
        "setLQM_CONFIG(apple80211_lqm_config_t *data)",
        "setWCL_REAL_TIME_MODE",
    )
    dispatch = section(
        cpp,
        "case APPLE80211_IOC_LQM_CONFIG:",
        "case APPLE80211_IOC_PRIVATE_MAC:",
    )
    link_update = section(
        cpp,
        "setWCL_LINK_STATE_UPDATE(apple80211_wcl_update_link_state *data)",
        "setInterfaceEnable",
    )

    return {
        "schema": "itlwm-lqm-config-owner-quarantine-v1",
        "source_base_revision": "a9cdc6520ef658fb2b422e8691a5a3fd35836157",
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "infra_set_wrapper": "0x100018844",
            "core_set_vtable_offset": "+0x710",
            "core_setter": "0x100119d98",
            "core_getter": "0x100119800",
            "null_return": "0x16",
            "opaque_state_gate_offset": "+0x43f",
            "opaque_state_gate_return": "0x2d",
            "feature_bit": "0x27",
            "feature_off_return": "0xe00002bc",
            "ecounters_sync": "0x1001de336",
            "lqm_timer_setter": "0x10000be36",
            "rssi_configure": "0x10011a0ea",
            "channel_quality_configure": "0x10011a308",
            "owner_offset": "0x48+0x15e8",
        },
        "local": {
            "public_lqm_configuration_owner": False,
            "public_config_cache": False,
            "public_timer_retune": False,
            "driver_owned_statistics_timer_preserved": True,
            "setter_nonnull_is_feature_off_equivalent": True,
            "valid_input_return_is_apple_parity": False,
        },
        "checks": {
            "reference_note": all(
                token in note
                for token in (
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "`0x100018844`",
                    "`+0x710`",
                    "`0x100119d98`",
                    "`0x16`",
                    "`+0x43f`",
                    "`0x2d`",
                    "featureFlagIsBitSet(0x27)",
                    "`0xe00002bc`",
                    "`+0x4`, `+0x8`, and `+0xc`",
                    "`+0x11..+0x17`",
                    "`[0x0b, 0x9b]`",
                    "`+0x19..+0x20`",
                    "setEcountersEnableStateSync",
                    "`0x1001de336`",
                    "`0x10000be36`",
                    "`0x10011a0ea`",
                    "`0x10011a308`",
                    "`0x100119800`",
                    "`(Core + 0x48) + 0x15e8`",
                    "`0x24`",
                    "does not claim",
                    "feature-off-equivalent quarantine",
                    "not claimed as Apple missing-owner or valid-input return-code parity",
                )
            ),
            "getter_uses_no_owner_boundary": all(
                token in getter
                for token in (
                    "(void)data;",
                    "return kIOReturnError;",
                )
            )
            and all(
                token not in getter
                for token in (
                    "data->",
                    "memcpy",
                    "cachedLqmConfig",
                    "return kIOReturnSuccess;",
                )
            ),
            "setter_preserves_null_and_quarantines_nonnull": all(
                token in setter
                for token in (
                    "if (data == nullptr)",
                    "return static_cast<IOReturn>(TahoeLqmContracts::kInvalidArgumentRaw);",
                    "(void)data;",
                    "return kIOReturnError;",
                    "enabled, validated non-null carrier",
                )
            )
            and all(
                token not in setter
                for token in (
                    "data->",
                    "reinterpret_cast",
                    "hasInvalid",
                    "cachedLqmConfig",
                    "setTahoeLqmStatsInterval",
                    "return kIOReturnSuccess;",
                )
            ),
            "synthetic_public_config_state_removed": all(
                token not in cpp + hpp
                for token in (
                    "initializeTahoeLqmConfig",
                    "cachedLqmConfig",
                    "hasCachedLqmConfig",
                )
            ),
            "interface_slots_and_dispatch_retained": all(
                token in hpp
                for token in (
                    "virtual IOReturn getLQM_CONFIG(apple80211_lqm_config_t *) override;",
                    "virtual IOReturn setLQM_CONFIG(apple80211_lqm_config_t *) override;",
                )
            )
            and all(
                token in dispatch
                for token in (
                    "cmd == SIOCGA80211",
                    "getLQM_CONFIG",
                    "cmd == SIOCSA80211",
                    "setLQM_CONFIG",
                )
            ),
            "public_setter_does_not_retune_timer": "setTahoeLqmStatsInterval(" not in cpp,
            "driver_owned_timer_lifecycle_preserved": all(
                token in v2
                for token in (
                    "fTahoeLqmStatsIntervalMs =",
                    "TahoeLqmContracts::kDefaultStatsIntervalMs",
                    "fTahoeLqmStatsTimer = IOTimerEventSource::timerEventSource",
                    "void AirportItlwm::startTahoeLqmStatsTimer()",
                    "void AirportItlwm::stopTahoeLqmStatsTimer()",
                    "postMessage(that->fNetIf, TahoeLqmContracts::kEventMessage,",
                )
            )
            and "kEventMessage = 0x27" in contracts
            and all(
                token in link_update
                for token in (
                    "instance->stopTahoeLqmStatsTimer();",
                    "instance->startTahoeLqmStatsTimer();",
                )
            ),
            "scoped_configuration_backends_absent": all(
                not source_contains(token)
                for token in (
                    "setEcountersEnableStateSync",
                    "configureLqmRssiUpdates",
                    "configureLqmChanQUpdates",
                    '"rssi_event"',
                    '"chq_event"',
                )
            ),
            "historical_claims_corrected": (
                "### 2026-07-14 correction: `LQM_CONFIG` is an owner-backed control surface"
                in signal_audit
                and "LQM config is owner-quarantined; summary/telemetry remain separate"
                in inventory
                and "## 2026-07-14 correction: public `LQM_CONFIG` is not this timer's owner API"
                in timer_note
                and "local no-backend `0xe00002bc` quarantine" in inventory
                and "feature-off-equivalent local quarantine" in signal_audit
                and "feature-off-equivalent boundary" in inventory
                and "setLQM_CONFIG` rearms the same owner after exact carrier validation"
                not in inventory
                and all(
                    "quarantined at the no-owner boundary" not in text
                    for text in (inventory, signal_audit, timer_note, note)
                )
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
        raise ValueError("LQM_CONFIG owner quarantine checks failed: " + ", ".join(failed))
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
        print(f"LQM_CONFIG owner quarantine validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
