#!/usr/bin/env python3
"""Generate and verify IPv4/IPv6 parameter quarantine evidence."""

import argparse
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/ip_params_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-479-ip-params-quarantine-20260714.md"
INVENTORY = ROOT / "docs/tahoe_discrepancy_inventory.md"
SIGNAL_AUDIT = ROOT / "docs/tahoe_signal_chain_audit.md"
ARP_NOTE = ROOT / "docs/reference/CR-479-arp-offload-null-owner-quarantine-20260712.md"
NDP_NOTE = ROOT / "docs/reference/CR-479-ndp-offload-null-owner-quarantine-20260712.md"
WCL_ARP_NOTE = ROOT / "docs/reference/CR-479-wcl-arp-mode-quarantine-20260713.md"
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
    arp_note = ARP_NOTE.read_text(encoding="utf-8")
    ndp_note = NDP_NOTE.read_text(encoding="utf-8")
    wcl_arp_note = WCL_ARP_NOTE.read_text(encoding="utf-8")
    ipv4 = section(
        cpp,
        "setIPV4_PARAMS(apple80211_ipv4_params *data)",
        "setIPV6_PARAMS(apple80211_ipv6_params *data)",
    )
    ipv6 = section(
        cpp,
        "setIPV6_PARAMS(apple80211_ipv6_params *data)",
        "setINFRA_ENUMERATED(apple80211_infra_enumerated *data)",
    )

    return {
        "schema": "itlwm-ip-params-quarantine-v1",
        "source_base_revision": "782c5a6d078a1f48bab06fcfa9778d82f47b89bd",
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "ipv4_infra_wrapper": "0x1000190cc",
            "ipv4_core_setter": "0x100142776",
            "ipv4_null_return": "0xe00002bc",
            "ipv4_owner_offset": "+0x2c18",
            "ipv4_state_offsets": ["+0x250c", "+0x2514", "+0x2518", "+0x251c"],
            "ipv6_infra_wrapper": "0x100019334",
            "ipv6_core_setter": "0x100142a50",
            "ipv6_count_offset": "+0x0",
            "ipv6_entry_stride": "+0x10",
            "ipv6_max_entries": 10,
        },
        "local": {
            "ip_lifecycle_backend": False,
            "ipv4_null_return_is_apple_parity": True,
            "ipv6_null_return_is_apple_parity": False,
            "request_false_success": False,
            "valid_input_return_is_apple_parity": False,
        },
        "checks": {
            "reference_note": all(
                token in note
                for token in (
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "`0x1000190cc`",
                    "`0x100142776`",
                    "`0xe00002bc`",
                    "`+0xc`",
                    "`0x100019334`",
                    "`0x100142a50`",
                    "`+4 + 0x10*i`",
                    "`0xa0`-byte",
                    "does not establish a safe Apple NULL return contract",
                    "Apple valid-input return-code parity is claimed",
                )
            ),
            "ipv4_setter_preserves_null_and_quarantines_nonnull": all(
                token in ipv4
                for token in (
                    "if (data == nullptr)",
                    "return kIOReturnBadArgumentTahoe;",
                    "return kIOReturnUnsupported;",
                )
            ) and all(
                token not in ipv4
                for token in (
                    "tahoeIPv4ParamsContract",
                    "cachedIPv4",
                    "reinterpret_cast",
                    "data->",
                    "return kIOReturnSuccess;",
                )
            ),
            "ipv6_setter_preserves_local_safety_and_quarantines_nonnull": all(
                token in ipv6
                for token in (
                    "if (data == nullptr)",
                    "return kIOReturnBadArgumentTahoe;",
                    "return kIOReturnUnsupported;",
                )
            ) and all(
                token not in ipv6
                for token in (
                    "tahoeIPv6ParamsHeader",
                    "cachedIPv6",
                    "reinterpret_cast",
                    "data->",
                    "return kIOReturnSuccess;",
                )
            ),
            "synthetic_ip_state_removed": all(
                token not in cpp + hpp
                for token in (
                    "tahoeIPv4ParamsContract",
                    "tahoeIPv6ParamsHeader",
                    "cachedIPv4Address",
                    "cachedIPv4Netmask",
                    "cachedIPv4Reserved",
                    "cachedIPv4Gateway",
                    "cachedIPv4GatewayTail",
                    "cachedIPv6Count",
                    "cachedIPv6Addresses",
                    "cachedIPv6LinkLocalAddress",
                )
            ),
            "interface_slots_retained": all(
                token in hpp
                for token in (
                    "virtual IOReturn setIPV4_PARAMS(apple80211_ipv4_params *data) override;",
                    "virtual IOReturn setIPV6_PARAMS(apple80211_ipv6_params *data) override;",
                )
            ),
            "scoped_lifecycle_backend_absent": all(
                not source_contains(token)
                for token in (
                    "setIPv4Params(",
                    "setIPv6Params(",
                    "handleIPv4AddressNotificationGated(",
                    "handleIPv6AddressNotificationGated(",
                    "handleKeepaliveDataNotificationGated(",
                    "setIPv4Addr(",
                )
            ),
            "historical_claims_corrected": (
                "### `IPV4_PARAMS` / `IPV6_PARAMS` correction" in signal_audit
                and "IPV4_PARAMS/IPV6_PARAMS correction:" in inventory
                and "quarantined with IPV6_PARAMS" in arp_note
                and "quarantined with IPV4_PARAMS" in ndp_note
                and "paired IP-parameter quarantine" in wcl_arp_note
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
        raise ValueError("IPv4/IPv6 parameter quarantine checks failed: " + ", ".join(failed))
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
        print(f"IPv4/IPv6 parameter quarantine validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
