#!/usr/bin/env python3
"""Generate and verify TX-power-cap false-success quarantine evidence."""

import argparse
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/tx_power_cap_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-479-tx-power-cap-quarantine-20260713.md"
CPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
HPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.hpp"
COMMANDER = ROOT / "AirportItlwm/TahoeCommanderV2.hpp"
PARITY = ROOT / "AirportItlwm/TahoePayloadParity.hpp"


def section(source, begin, end):
    start = source.index(begin)
    return source[start:source.index(end, start)]


def report():
    cpp = CPP.read_text(encoding="utf-8")
    hpp = HPP.read_text(encoding="utf-8")
    commander = COMMANDER.read_text(encoding="utf-8")
    parity = PARITY.read_text(encoding="utf-8")
    note = NOTE.read_text(encoding="utf-8")
    bypass = section(cpp, "setBYPASS_TX_POWER_CAP", "setTRAFFIC_ENG_PARAMS")
    dual = section(cpp, "setDUAL_POWER_MODE", "setCONGESTION_CTRL_IND")
    transport = section(commander, "IOReturn dispatchTransport", "IOReturn dispatchIOVarSet")

    return {
        "schema": "itlwm-tx-power-cap-quarantine-v2",
        "source_base_revision": "fd46c864637321893c56cb36f578ff39f8b0a460",
        "reference": {
            "dual_power_bridge": "0xffffff8001522f42",
            "dual_power_core": "0xffffff80016176e2",
            "txcap_sender": "0xffffff800160b3e0",
            "firmware_iovar": "txcapstate",
        },
        "local": {
            "backend_tx_power_cap_owner": False,
            "dual_power_false_success": False,
            "bypass_false_success": False,
            "valid_input_return_is_apple_parity": False,
        },
        "checks": {
            "reference_note": all(
                token in note
                for token in (
                    "0xffffff8001522f42",
                    "0xffffff80016176e2",
                    "0xffffff800160b3e0",
                    "txcapstate",
                    "not Apple valid-input return-code parity",
                )
            ),
            "bypass_quarantines_nonnull": (
                "if (data == nullptr)" in bypass
                and "return kIOReturnBadArgumentTahoe;" in bypass
                and "return kIOReturnUnsupported;" in bypass
                and "runSetBypassTxPowerCap" not in bypass
                and "cachedBypassTxPowerCapEnabled" not in bypass
                and "return kIOReturnSuccess;" not in bypass
            ),
            "dual_quarantines_nonnull": (
                "if (params == nullptr)" in dual
                and "return kIOReturnBadArgumentTahoe;" in dual
                and "return kIOReturnUnsupported;" in dual
                and "syncDualPowerMode" not in dual
                and "cachedDualPowerMode" not in dual
                and "return kIOReturnSuccess;" not in dual
            ),
            "pseudo_state_removed": all(
                token not in cpp and token not in hpp
                for token in (
                    "cachedBypassTxPowerCapEnabled",
                    "cachedDualPowerModePrimary",
                    "cachedDualPowerModeSecondary",
                )
            ),
            "synthetic_transport_is_not_hardware": (
                "TahoeCompletion::complete(asyncContext, 0);" in transport
                and "return kIOReturnSuccess;" in transport
                and "fHalService" not in transport
            ),
            "stale_payload_claim_retired": (
                "tx-power-cap-quarantine" in parity
                and "no Intel TX-power-cap firmware backend" in parity
                and "tx-power-cap-bypass" not in parity
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
        raise ValueError("TX-power-cap quarantine checks failed: " + ", ".join(failed))
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
        print(f"TX-power-cap quarantine validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
