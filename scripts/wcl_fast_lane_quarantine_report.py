#!/usr/bin/env python3
"""Generate and verify WCL Fast Lane native-EDCA recovery evidence."""

import argparse
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/wcl_fast_lane_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-479-wcl-fast-lane-quarantine-20260714.md"
INVENTORY = ROOT / "docs/tahoe_discrepancy_inventory.md"
SIGNAL_AUDIT = ROOT / "docs/tahoe_signal_chain_audit.md"
CPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
HPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.hpp"
def section(source, begin, end):
    start = source.index(begin)
    return source[start:source.index(end, start)]


def report():
    cpp = CPP.read_text(encoding="utf-8")
    hpp = HPP.read_text(encoding="utf-8")
    note = NOTE.read_text(encoding="utf-8")
    normalized_note = " ".join(note.split())
    inventory = INVENTORY.read_text(encoding="utf-8")
    signal_audit = SIGNAL_AUDIT.read_text(encoding="utf-8")
    iwn = (ROOT / "itlwm/hal_iwn/ItlIwn.cpp").read_text(encoding="utf-8")
    iwm = (ROOT / "itlwm/hal_iwm/mac80211.cpp").read_text(encoding="utf-8")
    iwx = (ROOT / "itlwm/hal_iwx/ItlIwx.cpp").read_text(encoding="utf-8")
    setter = section(
        cpp,
        "setWCL_UPDATE_FAST_LANE(apple80211_fastlane *data)",
        "setSTAND_ALONE_MODE_STATE",
    )

    return {
        "schema": "itlwm-wcl-fast-lane-native-edca-bridge-v1",
        "runtime_boundary": {
            "good_parent": "1044083fc36b4ab4cb2f4c7a401fe150ac88d2b1",
            "bad_quarantine": "3a8a5a32c243bdab9591e8a7357894f9b61790e3",
        },
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "infra_slot": 632,
            "infra_wrapper": "0x100019916",
            "capability_bind": "0x1003964b0",
            "capability_vtable_offset": "0x1a8",
            "net_adapter_getter": "0x1000c7dd6",
            "net_adapter_state_offset": "0x15e0",
            "acm_override": "0x10019e2c6",
            "wme_async": "0x100013664",
            "fast_lane_callback": "0x1000168c4",
            "firmware_iovar": "wme_ac_sta",
            "null_status": "0xe00002bc",
        },
        "local": {
            "native_vo_acm_override": True,
            "native_edca_refresh": True,
            "apple_wme_iovar_transport": False,
            "valid_input_return_is_apple_parity": False,
        },
        "checks": {
            "reference_note": all(
                token in normalized_note
                for token in (
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "slot `[632]`",
                    "`0x100019916`",
                    "`0xe00002bc`",
                    "`+0x1a8`",
                    "`0x1003964b0`",
                    "IOUserNetworkWLAN::setFastlaneCapable(bool)",
                    "`0x1000c7dd6`",
                    "`+0x15e0`",
                    "`0x10019e2c6`",
                    "`0x1000168c4`",
                    "`0x100013664`",
                    "`wme_ac_sta`",
                    "sendIOVarSet",
                    "Runtime recovery",
                    "1044083fc36b4ab4cb2f4c7a401fe150ac88d2b1",
                    "3a8a5a32c243bdab9591e8a7357894f9b61790e3",
                    "does not claim full Apple valid-input return-code",
                )
            ),
            "setter_applies_native_bridge": (
                "if (data == nullptr)" in setter
                and "return static_cast<IOReturn>(0xe00002bc);" in setter
                and "raw[0] == 0 || raw[1] == 0" in setter
                and "ic->ic_edca_ac[EDCA_AC_VO].ac_acm = 0;" in setter
                and "ic->ic_updateedca(ic);" in setter
                and "return kIOReturnSuccess;" in setter
            ),
            "interface_slot_retained": "virtual IOReturn setWCL_UPDATE_FAST_LANE(apple80211_fastlane *) override;"
            in hpp,
            "native_backend_routes": (
                "IWN_CMD_EDCA_PARAMS" in iwn
                and "iwm_updateedca" in iwm
                and "iwx_updateedca" in iwx
            ),
            "runtime_contract_recorded": (
                "Fast Lane recovery:" in inventory
                and "native host-side" in inventory
                and "Fast Lane recovery:" in signal_audit
                and "native VO ACM override" in signal_audit
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
        raise ValueError("WCL Fast Lane quarantine checks failed: " + ", ".join(failed))
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
        print(f"WCL Fast Lane quarantine validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
