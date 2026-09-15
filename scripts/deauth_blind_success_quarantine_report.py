#!/usr/bin/env python3
"""Generate and verify public IOC 29 DEAUTH proven-terminal evidence.

Supersedes the former blind-success fail-closed quarantine: the terminal owner
WCLNetManager::setDEAUTH @0xffffff80020f06f4 -> leaveNetworkCommand is now
proven, so setDEAUTH is implemented as a faithful net80211 mirror of
setDISASSOCIATE that publishes the caller's reason. This report verifies that
proven-terminal implementation instead of the old unsupported behavior."""

import argparse
import hashlib
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/deauth_blind_success_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-499-deauth-blind-success-quarantine-20260715.md"
RAW = ROOT / "docs/reference/artifacts/deauth-selector-dispatch-bootkc-current/raw.txt"
RAW_MANIFEST = RAW.with_name("SHA256SUMS.txt")
SIGNAL_AUDIT = ROOT / "docs/tahoe_signal_chain_audit.md"
CPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
HPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.hpp"
IOCTL = ROOT / "include/Airport/apple80211_ioctl.h"
LEGACY_STA = ROOT / "AirportItlwm/AirportSTAIOCTL.cpp"
SAP_PROTOCOL = ROOT / "include/Airport/IO80211SapProtocol.h"


def section(source, begin, end):
    start = source.index(begin)
    return source[start:source.index(end, start)]


def report():
    cpp = CPP.read_text(encoding="utf-8")
    hpp = HPP.read_text(encoding="utf-8")
    ioctl = IOCTL.read_text(encoding="utf-8")
    legacy_sta = LEGACY_STA.read_text(encoding="utf-8")
    sap_protocol = SAP_PROTOCOL.read_text(encoding="utf-8")
    note = " ".join(NOTE.read_text(encoding="utf-8").split())
    raw = RAW.read_text(encoding="utf-8")
    manifest = RAW_MANIFEST.read_text(encoding="utf-8")
    signal_audit = SIGNAL_AUDIT.read_text(encoding="utf-8")
    setter = section(
        cpp,
        "setDEAUTH(struct apple80211_deauth_data *da)",
        "getMCS(struct apple80211_mcs_data* md)",
    )
    ioc_route = section(
        cpp,
        "case APPLE80211_IOC_DEAUTH:",
        "case APPLE80211_IOC_RATE_SET:",
    )
    getter = section(
        cpp,
        "getDEAUTH(struct apple80211_deauth_data *da)",
        "getASSOCIATION_STATUS(struct apple80211_assoc_status_data *hv)",
    )
    legacy_ioc_route = section(
        legacy_sta,
        "case APPLE80211_IOC_DEAUTH:",
        "case APPLE80211_IOC_TX_ANTENNA:",
    )
    raw_digest = hashlib.sha256(RAW.read_bytes()).hexdigest()

    return {
        "schema": "itlwm-deauth-proven-terminal-v2",
        "source_base_revision": "40d5660130c60fe63a5017981d08228c43c2979d",
        "reference": {
            "bootkc_sha256": "eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d",
            "bootkc_uuid_x86_64": "F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5",
            "selector_wrapper": "0xffffff80021c3a1f",
            "selector": 29,
            "selector_gate_vtable_offset": "0xcc8",
            "terminal_vtable_offset": "0x2e0",
            "cast_failure_status": "0xe082280e",
            "proven_terminal_owner": "WCLNetManager::setDEAUTH(bulletinBoardMessage&)",
            "proven_terminal_addr_25C56": "0xffffff80020f06f4",
            "proven_terminal_tailcall": "leaveNetworkCommand",
            "proven_invalid_carrier_status": "0xe0000001",
        },
        "local": {
            "blind_success": False,
            "faithful_net80211_mirror": True,
            "publishes_caller_reason": True,
            "reference_leave_network_command_proven": True,
            "null_carrier_rejected": True,
        },
        "checks": {
            "reference_raw_manifest_matches": manifest == f"{raw_digest}  raw.txt\n",
            "reference_raw_has_current_gate_and_terminal_topology": all(
                token in raw
                for token in (
                    "eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d",
                    "F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5",
                    "0xffffff80021c3a1f",
                    "0x55-byte wrapper",
                    "movl   $0x1d, %esi",
                    "callq  *0xcc8(%rax)",
                    "testl  %eax, %eax",
                    "OSMetaClassBase::safeMetaCast",
                    "movq   0x2e0(%rax), %rax",
                    "movq   %rbx, %rsi",
                    "jmpq   *%rax",
                    "movl   $0xe082280e, %eax",
                )
            ),
            "reference_note_records_proven_terminal": all(
                token in note
                for token in (
                    "APPLE80211_IOC_DEAUTH",
                    "numeric 29",
                    "2026-09-15 superseding",
                    "WCLNetManager::setDEAUTH",
                    "0xffffff80020f06f4",
                    "leaveNetworkCommand",
                    "faithful net80211 mirror",
                    "ic->ic_deauth_reason = da->deauth_reason",
                    "invalid-carrier",
                )
            ),
            "typed_ioc29_route_and_abi_remain": (
                "#define APPLE80211_IOC_DEAUTH                    29" in ioctl
                and "struct apple80211_deauth_data" in ioctl
                and "deauth_reason" in ioctl
                and "deauth_ea" in ioctl
                and "case APPLE80211_IOC_DEAUTH:" in ioc_route
                and "getDEAUTH((apple80211_deauth_data *)req->req_data)" in ioc_route
                and "setDEAUTH((apple80211_deauth_data *)req->req_data)" in ioc_route
                and hpp.count("IOReturn setDEAUTH(apple80211_deauth_data *);") == 1
                and "Public IOC 29 is distinct from the void DISASSOCIATE carrier." in hpp
            ),
            "local_setter_implements_proven_terminal": (
                "AIRPORT_ITLWM_REQUIRE_LIVE_OPERATION();" in setter
                and "if (da == nullptr)" in setter
                and "return kIOReturnBadArgumentTahoe;" in setter
                and "copyin(reinterpret_cast<user_addr_t>(&da->deauth_reason)," in setter
                and "ic->ic_deauth_reason = deauthReason;" in setter
                and "ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);" in setter
                and "ieee80211_del_ess(ic, nullptr, 0, 1);" in setter
                and "IEEE80211_FC0_SUBTYPE_DEAUTH" in setter
                and "0xffffff80020f06f4" in setter
                and "leaveNetworkCommand" in setter
                and all(
                    token not in setter
                    for token in (
                        "return kIOReturnUnsupported;",
                        "(void)da;",
                        "ic->ic_deauth_reason = APPLE80211_REASON_ASSOC_LEAVING;",
                    )
                )
            ),
            "distinct_disassociate_not_delegated_or_substituted": (
                "setDISASSOCIATE(" not in setter
                and "APPLE80211_IOC_DISASSOCIATE" not in ioc_route
            ),
            "paired_get_legacy_and_apsta_surfaces_remain": (
                "da->version = APPLE80211_VERSION;" in getter
                and "da->deauth_reason = ic->ic_deauth_reason;" in getter
                and "return kIOReturnSuccess;" in getter
                and "IOCTL(request_type, DEAUTH, apple80211_deauth_data);" in legacy_ioc_route
                and "APSTA setSTA_DEAUTH slot mismatch" in sap_protocol
                and "APSTA concrete setSTA_DEAUTH byte offset mismatch" in sap_protocol
            ),
            "proven_terminal_classification_is_recorded": (
                "## 2026-09-15 superseding: public IOC 29 `setDEAUTH` proven-terminal implementation" in signal_audit
                and "leaveNetworkCommand" in signal_audit
                and "faithful net80211 mirror" in signal_audit
                and "ic->ic_deauth_reason = da->deauth_reason" in signal_audit
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
        raise ValueError("DEAUTH blind-success quarantine checks failed: " + ", ".join(failed))
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
        print(f"DEAUTH blind-success validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
