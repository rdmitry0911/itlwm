#!/usr/bin/env python3
"""Generate and verify USB host-notification quarantine evidence."""

import argparse
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/usb_host_notification_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-479-usb-host-notification-quarantine-20260713.md"
CPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
HPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.hpp"
SOURCE_ROOTS = (ROOT / "AirportItlwm", ROOT / "include", ROOT / "itl80211", ROOT / "itlwm")


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
    setter = section(
        cpp,
        "IOReturn AirportItlwmSkywalkInterface::\nsetUSB_HOST_NOTIFICATION",
        "IOReturn AirportItlwmSkywalkInterface::\nsetBYPASS_TX_POWER_CAP",
    )

    return {
        "schema": "itlwm-usb-host-notification-quarantine-v1",
        "source_base_revision": "0282d06e58bf6001efee0deb6a0b8e9564b0995d",
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "infra_wrapper": "0x100018890",
            "core_setter": "0x100120ae0",
            "infra_virtual_slot": "0x720",
            "hidden_owner_offset": "0x1510",
            "hidden_owner_virtual_offset": "0x170",
            "present_iovar": "asym_mit_ext_usb",
            "change_iovar": "asym_mit_ext_usb_chg",
            "present_offset": "0x0c",
            "conditional_change_offset": "0x08",
            "conditional_change_max": 1,
            "iovar_payload_bytes": 4,
        },
        "local": {
            "backend_usb_host_notification_owner": False,
            "false_success": False,
            "valid_input_return_is_apple_parity": False,
        },
        "checks": {
            "reference_note": all(
                token in note
                for token in (
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "0x100018890",
                    "0x100120ae0",
                    "`+0x720`",
                    "`+0x48 + 0x1510`",
                    "`+0x170`",
                    "`asym_mit_ext_usb`",
                    "`+0x0c`",
                    "`+0x08 <= 1`",
                    "`asym_mit_ext_usb_chg`",
                    "runIOVarSet",
                    "not independently establish a public null-input status",
                    "no Apple",
                    "valid-input return-code parity",
                )
            ),
            "setter_quarantines_nonnull": (
                "if (data == nullptr)" in setter
                and "return kIOReturnBadArgumentTahoe;" in setter
                and "return kIOReturnUnsupported;" in setter
                and "return kIOReturnSuccess;" not in setter
                and "runSetUSBHostNotification" not in setter
                and "TahoeAsyncCommandContext" not in setter
                and "cachedUsbHostNotification" not in setter
            ),
            "pseudo_state_removed": all(
                token not in cpp and token not in hpp
                for token in (
                    "cachedUsbHostNotificationSeq",
                    "cachedUsbHostNotificationChange",
                    "cachedUsbHostNotificationPresent",
                )
            ),
            "no_local_usb_iovar_backend": all(
                not source_contains(token)
                for token in (
                    '"asym_mit_ext_usb"',
                    '"asym_mit_ext_usb_chg"',
                    "AppleBCMWLANCommander",
                    "runIOVarSet(",
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
        raise ValueError("USB host notification quarantine checks failed: " + ", ".join(failed))
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
        print(f"USB host notification quarantine validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
