#!/usr/bin/env python3
"""Generate and verify PRIVATE_MAC no-producer quarantine evidence."""

import argparse
import hashlib
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/private_mac_rejected_state_report.json"
NOTE = ROOT / "docs/reference/CR-490-private-mac-no-producer-quarantine-20260715.md"
LEGACY_NOTE = ROOT / "docs/reference/CR-479-private-mac-rejected-state-20260714.md"
RAW = ROOT / "docs/reference/artifacts/private-mac-25c56/raw.txt"
RAW_MANIFEST = RAW.with_name("SHA256SUMS.txt")
INVENTORY = ROOT / "docs/tahoe_discrepancy_inventory.md"
SIGNAL_AUDIT = ROOT / "docs/tahoe_signal_chain_audit.md"
CPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
HPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.hpp"
V2 = ROOT / "AirportItlwm/AirportItlwmV2.cpp"
INFRA = ROOT / "include/Airport/IO80211InfraProtocol.h"
ABI = ROOT / "include/Airport/apple80211_ioctl.h"
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
    infra = INFRA.read_text(encoding="utf-8")
    abi = ABI.read_text(encoding="utf-8")
    note = " ".join(NOTE.read_text(encoding="utf-8").split())
    legacy_note = LEGACY_NOTE.read_text(encoding="utf-8")
    raw = RAW.read_text(encoding="utf-8")
    manifest = RAW_MANIFEST.read_text(encoding="utf-8")
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
    raw_digest = hashlib.sha256(RAW.read_bytes()).hexdigest()

    return {
        "schema": "itlwm-private-mac-no-producer-quarantine-v2",
        "source_base_revision": "20a8de84e3c2055a8ddbb1c10c76dfe4a97d6656",
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "image_uuid_x86_64": "149C0AD1-A92F-35BC-AA69-5C8815C5421E",
            "infra_slot": 490,
            "infra_get_wrapper": "0x1000172f4",
            "core_get_vtable_offset": "0x6e8",
            "core_getter": "0x100119538",
            "bgscan_adapter_from_core": "0x48+0x1578",
            "null_return": "0x16",
            "enabled_offset": "0x4",
            "timeout_offset": "0xc",
            "scanmac_iovar": "scanmac",
        },
        "local": {
            "private_mac_owner_backend": False,
            "synthetic_success": False,
            "null_return_is_apple_parity": True,
            "valid_input_return_is_apple_parity": False,
            "runtime_selector_invocation": False,
        },
        "checks": {
            "reference_raw_manifest_matches": manifest == f"{raw_digest}  raw.txt\n",
            "reference_raw_has_bgscan_transport_producer": all(
                token in raw
                for token in (
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "149C0AD1-A92F-35BC-AA69-5C8815C5421E",
                    "0x1000172f4",
                    "0x6e8(%rax)",
                    "0x100119538",
                    "0x48(%rdi)",
                    "0x1578(%rax)",
                    "isPrivateMacEnabled",
                    "0x4(%rbx)",
                    "getPrivateMacTimeout",
                    "0xc(%rbx)",
                    "\"scanmac\"",
                    "runIOVarGet",
                    "testl  %eax, %eax",
                    "$0x16",
                )
            ),
            "reference_note_has_scope_and_nonclaim": all(
                token in note
                for token in (
                    "slot `[490]`",
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "`0x1000172f4`",
                    "`0x6e8`",
                    "`0x100119538`",
                    "0x48",
                    "(Core + 0x48) + 0x1578",
                    "`0x16`",
                    "runIOVarGet(\"scanmac\")",
                    "not Apple valid-input return-code, full carrier-layout, BGScan-owner, IOVAR-result, or runtime-selector parity",
                )
            ),
            "active_v2_slot_and_dispatch_remain": (
                "// [490]" in hpp
                and "getPRIVATE_MAC" in hpp
                and "// [490]" in infra
                and "getPRIVATE_MAC" in infra
                and "fNetIf = new AirportItlwmSkywalkInterface;" in v2
                and all(
                    token in dispatch
                    for token in (
                        "cmd == SIOCGA80211",
                        "getPRIVATE_MAC",
                        "kIOReturnUnsupported",
                    )
                )
            ),
            "getter_preserves_null_and_fails_closed_without_output": all(
                token in getter
                for token in (
                    "if (data == nullptr)",
                    "return kApple80211ErrInvalidArgumentRaw;",
                    "(void)data;",
                    "return kIOReturnUnsupported;",
                )
            )
            and all(
                token not in getter
                for token in (
                    "data->",
                    "cachedPrivateMac",
                    "return kIOReturnSuccess;",
                    "memset",
                    "APPLE80211_VERSION",
                )
            ),
            "setter_remains_quarantined_without_consuming": all(
                token in setter
                for token in (
                    "if (data == nullptr)",
                    "return kApple80211ErrInvalidArgumentRaw;",
                    "(void)data;",
                    "return kIOReturnUnsupported;",
                )
            )
            and all(
                token not in setter
                for token in (
                    "cachedPrivateMac",
                    "data->",
                    "return kIOReturnSuccess;",
                )
            ),
            "no_matching_local_private_mac_producer": all(
                not source_contains(token)
                for token in (
                    "AppleBCMWLANBGScanAdapter::isPrivateMacEnabled",
                    "AppleBCMWLANBGScanAdapter::getPrivateMacTimeout",
                    "configureBGScanPrivateMac",
                    "enablePrivateMACForScans",
                    "disablePrivateMACForScans",
                    "cachedPrivateMac",
                )
            ),
            "abi_comment_keeps_only_observed_producer_split": all(
                token in abi
                for token in (
                    "obtains +0x4 and +0xc from BGScanAdapter",
                    "reply fields from \"scanmac\" only after that command succeeds",
                    "not write version; the exact field names remain unrecovered",
                )
            ),
            "historical_zero_baseline_claim_is_superseded": (
                "## 2026-07-15 correction: `PRIVATE_MAC` getter is a no-producer quarantine"
                in signal_audit
                and "`Q13 correction: PRIVATE_MAC getter no-producer quarantine`"
                in inventory
                and "Superseded on 2026-07-15" in legacy_note
                and "returns only the existing zero packed-carrier baseline" not in signal_audit
                and "getter retains its packed zero ABI carrier" not in inventory
            ),
            "no_synthetic_private_mac_state_remains": all(
                token not in cpp + hpp
                for token in (
                    "cachedPrivateMac",
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
