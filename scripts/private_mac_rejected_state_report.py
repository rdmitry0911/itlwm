#!/usr/bin/env python3
"""Generate and verify PRIVATE_MAC rejected-state quarantine evidence."""

import argparse
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/private_mac_rejected_state_report.json"
NOTE = ROOT / "docs/reference/CR-479-private-mac-rejected-state-20260714.md"
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
        "getPRIVATE_MAC(apple80211_private_mac_data *data)",
        "getTHERMAL_INDEX(apple80211_thermal_index_t *data)",
    )
    setter = section(
        cpp,
        "setPRIVATE_MAC(apple80211_private_mac_data *data)",
        "setSET_MAC_ADDRESS(apple80211_set_mac_address_data *data)",
    )
    dispatch = section(
        cpp,
        "case APPLE80211_IOC_PRIVATE_MAC:",
        "case APPLE80211_IOC_SET_MAC_ADDRESS:",
    )

    return {
        "schema": "itlwm-private-mac-rejected-state-v1",
        "source_base_revision": "483ca3d30b448d608c477d99d1fa7252e0c66a84",
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "infra_set_wrapper": "0x100018528",
            "core_set_vtable_offset": "+0x6f0",
            "core_setter": "0x10011ee12",
            "infra_get_wrapper": "0x1000172f4",
            "core_get_vtable_offset": "+0x6e8",
            "core_getter": "0x100119538",
            "null_return": "0x16",
            "timeout_offset": "+0xc",
            "mac_offset": "+0x10",
            "enabled_offset": "+0x4",
            "scanmac_iovar": "scanmac",
        },
        "local": {
            "private_mac_owner_backend": False,
            "rejected_request_false_state": False,
            "getter_is_baseline_only": True,
            "valid_input_return_is_apple_parity": False,
        },
        "checks": {
            "reference_note": all(
                token in note
                for token in (
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "`0x100018528`",
                    "`+0x6f0`",
                    "`0x10011ee12`",
                    "`0x1000172f4`",
                    "`+0x6e8`",
                    "`0x100119538`",
                    "`0x16`",
                    "`+0xc`",
                    "`+0x10`",
                    "`+0x4`",
                    "configureBGScanPrivateMacTimeout",
                    "configureBGScanPrivateMac",
                    "enablePrivateMACForScans",
                    "disablePrivateMACForScans",
                    "runIOVarGet(\"scanmac\")",
                    "does not claim live Tahoe",
                    "valid-input return-code parity",
                )
            ),
            "setter_preserves_null_and_quarantines_without_consuming": all(
                token in setter
                for token in (
                    "if (data == nullptr)",
                    "return kApple80211ErrInvalidArgumentRaw;",
                    "(void)data;",
                    "return kIOReturnUnsupported;",
                    "before\n    // reading them",
                )
            )
            and all(
                token not in setter
                for token in (
                    "data->",
                    "reinterpret_cast",
                    "cachedPrivateMac",
                    "return kIOReturnSuccess;",
                )
            ),
            "getter_is_zero_baseline_only": all(
                token in getter
                for token in (
                    "memset(data, 0, sizeof(*data));",
                    "data->version = APPLE80211_VERSION;",
                    "data->enabled = 0;",
                    "return kIOReturnSuccess;",
                )
            )
            and all(
                token not in getter
                for token in (
                    "cachedPrivateMac",
                    "data->scanmac_state =",
                    "data->timeout_seconds =",
                    "memcpy(data->primary_mac",
                    "memcpy(data->secondary_mac",
                )
            ),
            "synthetic_private_mac_state_removed": "cachedPrivateMac" not in cpp + hpp,
            "interface_slots_and_dispatch_retained": all(
                token in hpp
                for token in (
                    "virtual IOReturn getPRIVATE_MAC(apple80211_private_mac_data *) override;",
                    "virtual IOReturn setPRIVATE_MAC(apple80211_private_mac_data *) override;",
                )
            )
            and all(
                token in dispatch
                for token in (
                    "cmd == SIOCGA80211",
                    "getPRIVATE_MAC",
                    "kIOReturnUnsupported",
                )
            )
            and "setPRIVATE_MAC" not in dispatch,
            "scoped_owner_backend_absent": all(
                not source_contains(token)
                for token in (
                    "AppleBCMWLANBGScanAdapter",
                    "configureBGScanPrivateMacTimeout",
                    "configureBGScanPrivateMac",
                    "enablePrivateMACForScans",
                    "disablePrivateMACForScans",
                    "isPrivateMacEnabled",
                    "getPrivateMacTimeout",
                )
            ),
            "historical_claims_corrected": (
                "## 2026-07-14 correction: `PRIVATE_MAC` is BGScanAdapter-backed"
                in signal_audit
                and "Q13 correction: PRIVATE_MAC ownerless rejected state" in inventory
                and "`setPRIVATE_MAC -> 0x16`" not in signal_audit
                and "fixed-fail selectors (`AP_MODE`, `PRIVATE_MAC`," not in inventory
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
        raise ValueError("PRIVATE_MAC rejected-state checks failed: " + ", ".join(failed))
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
        print(f"PRIVATE_MAC rejected-state validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
