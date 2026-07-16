#!/usr/bin/env python3
"""Generate and verify WCL extended-BSS producer evidence."""

import argparse
import hashlib
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/wcl_extended_bss_info_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-479-wcl-extended-bss-info-quarantine-20260714.md"
RAW = ROOT / "docs/reference/artifacts/wcl-extended-bss-info-25c56/raw.txt"
RAW_MANIFEST = RAW.with_name("SHA256SUMS.txt")
CPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
HPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.hpp"
V2 = ROOT / "AirportItlwm/AirportItlwmV2.cpp"
INFRA = ROOT / "include/Airport/IO80211InfraProtocol.h"


def section(source, begin, end):
    start = source.index(begin)
    return source[start:source.index(end, start)]


def ordered(source, tokens):
    positions = []
    cursor = 0
    for token in tokens:
        position = source.find(token, cursor)
        if position < 0:
            return False
        positions.append(position)
        cursor = position + len(token)
    return positions == sorted(positions)


def report():
    cpp = CPP.read_text(encoding="utf-8")
    hpp = HPP.read_text(encoding="utf-8")
    v2 = V2.read_text(encoding="utf-8")
    infra = INFRA.read_text(encoding="utf-8")
    note = NOTE.read_text(encoding="utf-8")
    raw = RAW.read_text(encoding="utf-8")
    manifest = RAW_MANIFEST.read_text(encoding="utf-8")
    getter = section(cpp, "getWCL_EXTENDED_BSS_INFO", "getTXPOWER")
    raw_digest = hashlib.sha256(RAW.read_bytes()).hexdigest()

    return {
        "schema": "itlwm-wcl-extended-bss-info-producer-v2",
        "source_parent_revision": "c12a66a1e618e7d37bd2d6771ec25b4c4204f5f1",
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "image_uuid_x86_64": "149C0AD1-A92F-35BC-AA69-5C8815C5421E",
            "infra_slot": 533,
            "infra_wrapper": "0x100017c58",
            "core_getter": "0x100132df6",
            "net_adapter_getter": "0x10019de64",
            "null_status": "0xe00002bc",
            "carrier_size": "0x214",
        },
        "local": {
            "false_success": False,
            "partial_local_producer": True,
            "full_apple_valid_input_parity": False,
            "mlo_context_is_zero_without_owner": True,
        },
        "external_runtime_evidence": {
            "verified_by_this_source_check": False,
            "selector_0x1cc_full_producer_trace": {
                "workspace_relative_path": (
                    "runtime-captures/"
                    "itlwm-wcl-extended-bss-info-regression-20260716/"
                    "main-full-producer-20260716T1838Z-linkstate.dtrace.log"
                ),
                "sha256": (
                    "5555ade49a794456069867bea2906ff758a5c643fae3f0b0d101fa9a28b8863b"
                ),
            },
            "four_radio_cycles": {
                "workspace_relative_path": (
                    "runtime-captures/"
                    "itlwm-wcl-extended-bss-info-regression-20260716/"
                    "full-producer-radio-cycles.log"
                ),
                "sha256": (
                    "371e57bb9d3af57b372564fb987f4a2686919b91bfb0658ca4eafd31b5680d8f"
                ),
            },
        },
        "checks": {
            "reference_raw_manifest_matches": manifest == f"{raw_digest}  raw.txt\n",
            "reference_raw_has_net_adapter_pipeline": all(
                token in raw
                for token in (
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "149C0AD1-A92F-35BC-AA69-5C8815C5421E",
                    "0x100017c58",
                    "0x420(%rax)",
                    "0x100132df6",
                    "0xe00002bc",
                    "0x15e0",
                    "0x10019de64",
                    "updateRateSetSync",
                    "0xbc(%rbx)",
                    "updateMCSSetSyc",
                    "0xcc(%rbx)",
                    "0xd4(%rbx)",
                    "getAssociatedWPARSNIESync",
                    "0x113(%rbx)",
                    "$0x101",
                    "$0x73",
                    "getMloContext",
                    "0xdc",
                )
            ),
            "reference_note_has_scope_and_nonclaim": all(
                token in note
                for token in (
                    "slot `[533]`",
                    "0x100017c58",
                    "0x100132df6",
                    "0x10019de64",
                    "partial local producer",
                    "not byte-identical",
                )
            ),
            "active_v2_slot_remains": (
                "// [533]" in hpp
                and "getWCL_EXTENDED_BSS_INFO" in hpp
                and "// [533]" in infra
                and "getWCL_EXTENDED_BSS_INFO" in infra
                and "fNetIf = new AirportItlwmSkywalkInterface;" in v2
            ),
            "exact_0214_carrier_layout": all(
                token in infra
                for token in (
                    "struct apple80211_extended_bss_info",
                    "rate_set",
                    "mcs_set",
                    "vht_mcs_set",
                    "he_mcs_set",
                    "mlo_context[0x37]",
                    "associated_rsn_ie[APPLE80211_MAX_RSN_IE_LEN]",
                    "sizeof(apple80211_extended_bss_info) == 0x214",
                    "offsetof(apple80211_extended_bss_info, rate_set) == 0x000",
                    "offsetof(apple80211_extended_bss_info, mcs_set) == 0x0bc",
                    "offsetof(apple80211_extended_bss_info, vht_mcs_set) == 0x0cc",
                    "offsetof(apple80211_extended_bss_info, he_mcs_set) == 0x0d4",
                    "offsetof(apple80211_extended_bss_info, mlo_context) == 0x0dc",
                    "offsetof(apple80211_extended_bss_info, associated_rsn_ie) ==",
                )
            ),
            "local_null_guard_is_retained": (
                "if (data == nullptr)" in getter
                and "return kIOReturnBadArgumentTahoe;" in getter
            ),
            "producer_order_is_explicit": ordered(
                getter,
                (
                    "memset(carrier, 0, sizeof(*carrier));",
                    "ic == nullptr",
                    "getRATE_SET(&carrier->rate_set)",
                    "getMCS_INDEX_SET(&carrier->mcs_set)",
                    "memcpy(carrier->associated_rsn_ie",
                    "return kIOReturnSuccess;",
                ),
            ),
            "rate_and_mcs_failures_propagate": all(
                token in getter
                for token in (
                    "IOReturn ret = getRATE_SET(&carrier->rate_set);",
                    "ret = getMCS_INDEX_SET(&carrier->mcs_set);",
                    "if (ret != kIOReturnSuccess)\n        return ret;",
                )
            ),
            "vht_he_have_explicit_defaults_and_producers": (
                "getVHT_MCS_INDEX_SET(&carrier->vht_mcs_set)" in getter
                and getter.count("mcs_map = 0xffff;") >= 2
                and "IEEE80211_F_HEON" in getter
                and "ni_he_mcs_nss_supp.tx_mcs_80" in getter
            ),
            "rsn_is_bounded_associated_raw_tlv": all(
                token in getter
                for token in (
                    "ni_rsnie_tlv != nullptr",
                    "ni_rsnie_tlv_len != 0",
                    "ni_rsnie_tlv_len <= sizeof(carrier->associated_rsn_ie)",
                    "memcpy(carrier->associated_rsn_ie, ic->ic_bss->ni_rsnie_tlv",
                )
            ),
            "mlo_is_not_fabricated": "mlo_context" not in getter,
            "external_runtime_evidence_is_documented": all(
                token in note
                for token in (
                    "not verified by this source-only script",
                    "main-full-producer-20260716T1838Z-linkstate.dtrace.log",
                    "5555ade49a794456069867bea2906ff758a5c643fae3f0b0d101fa9a28b8863b",
                    "full-producer-radio-cycles.log",
                    "371e57bb9d3af57b372564fb987f4a2686919b91bfb0658ca4eafd31b5680d8f",
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
        raise ValueError("WCL extended-BSS producer checks failed: " + ", ".join(failed))
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
        print(f"WCL extended-BSS producer validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
