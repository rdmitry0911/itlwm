#!/usr/bin/env python3
"""Generate and verify IE public-setter and ABI quarantine evidence."""

import argparse
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/ie_public_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-479-ie-public-quarantine-20260713.md"
SIGNAL_AUDIT = ROOT / "docs/tahoe_signal_chain_audit.md"
INVENTORY = ROOT / "docs/tahoe_discrepancy_inventory.md"
CONTRACTS = ROOT / "evidence/contracts/apple_wifi_contract_inventory.json"
CPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
HPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.hpp"
LEGACY_CPP = ROOT / "AirportItlwm/AirportAWDL.cpp"
HEADER = ROOT / "include/Airport/apple80211_ioctl.h"
BUILDERS = ROOT / "AirportItlwm/TahoePayloadBuilders.hpp"
TEST = ROOT / "tests/tahoe_payload_builders_test.cpp"
APSTA = ROOT / "AirportItlwm/AirportItlwmAPSTAInterface.hpp"
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
    legacy_cpp = LEGACY_CPP.read_text(encoding="utf-8")
    header = HEADER.read_text(encoding="utf-8")
    builders = BUILDERS.read_text(encoding="utf-8")
    test = TEST.read_text(encoding="utf-8")
    apsta = APSTA.read_text(encoding="utf-8")
    note = NOTE.read_text(encoding="utf-8")
    signal_audit = SIGNAL_AUDIT.read_text(encoding="utf-8")
    inventory = INVENTORY.read_text(encoding="utf-8")
    contracts = CONTRACTS.read_text(encoding="utf-8")
    setter = section(
        cpp,
        "IOReturn AirportItlwmSkywalkInterface::\nsetIE",
        "IOReturn AirportItlwmSkywalkInterface::\nsetOFFLOAD_TCPKA_ENABLE",
    )
    legacy_setter = section(
        legacy_cpp,
        "IOReturn AirportItlwm::\nsetIE",
        "IOReturn AirportItlwm::\nsetP2P_SCAN",
    )
    ie_struct = section(
        header,
        "struct apple80211_ie_data",
        "struct apple80211_p2p_listen_data",
    )
    builder = section(
        builders,
        "inline bool buildIE",
        "inline bool buildOffloadNdp",
    )

    return {
        "schema": "itlwm-ie-public-quarantine-v1",
        "source_base_revision": "e55bcd64321591754ea3faca9d09e0b7faa83740",
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "infra_wrapper": "0x100018230",
            "infra_virtual_offset": "0x528",
            "core_setter": "0x100121826",
            "vendor_setter": "0x10012109c",
            "custom_assoc_setter": "0x10003eeac",
            "invalid_status": "0x16",
            "accepted_ie_length": "1..0x800",
            "carrier_bytes": 0x814,
            "ie_offset": "0x14",
            "vendor_iovar": "vndr_ie",
            "custom_iovar": "wapiie",
        },
        "local": {
            "backend_ie_commander": False,
            "false_success": False,
            "valid_input_return_is_apple_parity": False,
        },
        "checks": {
            "reference_note": all(
                token in note
                for token in (
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "0x100018230",
                    "`+0x528`",
                    "0x100121826",
                    "`0x16`",
                    "`1..0x800`",
                    "`0x814`",
                    "`+0x14`",
                    "0x10003eeac",
                    "`wapiie`",
                    "0x10012109c",
                    "`vndr_ie`",
                    "runVirtualIOVarSet",
                    "not Apple valid-input return-code parity",
                )
            ),
            "carrier_abi_matches_reference": all(
                token in ie_struct
                for token in (
                    "uint32_t    signature_len;      // 12",
                    "uint32_t    ie_len;             // 16",
                    "uint8_t     ie[2048];",
                    "sizeof(struct apple80211_ie_data) == 0x814",
                    "__offsetof(struct apple80211_ie_data, ie) == 0x14",
                )
            )
            and "uint32_t    pad1;" not in ie_struct,
            "builder_preserves_exact_invalid_range": all(
                token in builder
                for token in (
                    "data->ie_len == 0",
                    "data->ie_len > sizeof(data->ie)",
                    "data->ie[0] == 0x44",
                )
            )
            and all(
                token in test
                for token in (
                    "sizeof(apple80211_ie_data) == 0x814",
                    "offsetof(apple80211_ie_data, ie) == 0x14",
                    "IE builder rejects zero-length IE",
                )
            ),
            "skywalk_setter_quarantines_valid": all(
                token in setter
                for token in (
                    "if (data == nullptr || instance == nullptr)",
                    "TahoePayloadBuilders::IEPayloads payload;",
                    "!TahoePayloadBuilders::buildIE(data, &payload)",
                    "return kApple80211ErrInvalidArgumentRaw;",
                    "return kIOReturnUnsupported;",
                )
            )
            and all(
                token not in setter
                for token in (
                    "runSetIE(",
                    "TahoeAsyncCommandContext",
                    "cachedAssocIe",
                    "cachedVendorIe",
                    "return kIOReturnSuccess;",
                )
            ),
            "legacy_setter_quarantines_valid": all(
                token in legacy_setter
                for token in (
                    "data == nullptr || data->ie_len == 0",
                    "data->ie_len > sizeof(data->ie)",
                    "return static_cast<IOReturn>(0x16);",
                    "return kIOReturnUnsupported;",
                )
            )
            and "return kIOReturnSuccess;" not in legacy_setter,
            "dead_skywalk_pseudo_state_removed": all(
                token not in cpp and token not in hpp
                for token in (
                    "cachedAssocIe",
                    "cachedAssocIeLen",
                    "hasCachedAssocIe",
                    "cachedVendorIe",
                    "cachedVendorIeLen",
                    "cachedVendorIeFlags",
                    "hasCachedVendorIe",
                )
            ),
            "no_local_ie_backend_execution": all(
                not source_contains(token)
                for token in (
                    '"wapiie"',
                    "AppleBCMWLANCommander",
                    "runVirtualIOVarSet(",
                    "runIOVarSet(",
                    "setVendorIE(",
                )
            ),
            "vndr_ie_scaffold_is_not_a_backend": all(
                token in apsta
                for token in (
                    'kAirportItlwmAPSTAVndrIEIovarName[] = "vndr_ie"',
                    'sizeof("vndr_ie")',
                )
            ),
            "stale_ie_claims_corrected": all(
                token in source
                for source in (signal_audit, inventory)
                for token in (
                    "ie_len == 0",
                    "`1..0x800`",
                    "`+0x14`",
                )
            )
            and all(
                "must not reject `ie_len == 0`" not in source
                and "no longer rejects `ie_len == 0`" not in source
                for source in (signal_audit, inventory)
            )
            and "ie-valid-quarantined" in contracts,
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
        raise ValueError("IE public quarantine checks failed: " + ", ".join(failed))
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
        print(f"IE public quarantine validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
