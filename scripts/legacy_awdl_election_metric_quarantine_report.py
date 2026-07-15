#!/usr/bin/env python3
"""Generate and verify legacy AWDL election-metric no-owner evidence."""

import argparse
import hashlib
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/legacy_awdl_election_metric_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-502-legacy-awdl-election-metric-no-owner-quarantine-20260715.md"
RAW = ROOT / "docs/reference/artifacts/awdl-election-metric-public-wrapper-bootkc-current/raw.txt"
RAW_MANIFEST = RAW.with_name("SHA256SUMS.txt")
CPP = ROOT / "AirportItlwm/AirportVirtualIOCTL.cpp"
HPP = ROOT / "AirportItlwm/AirportItlwm.hpp"
IOCTL = ROOT / "include/Airport/apple80211_ioctl.h"
PROJECT = ROOT / "itlwm.xcodeproj/project.pbxproj"
SOURCE_ROOTS = (
    ROOT / "AirportItlwm",
    ROOT / "include",
    ROOT / "itl80211",
    ROOT / "itlwm",
)


def section(source, begin, end):
    start = source.index(begin)
    return source[start:source.index(end, start)]


def code_contains(token):
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
    ioctl = IOCTL.read_text(encoding="utf-8")
    project = PROJECT.read_text(encoding="utf-8")
    note = " ".join(NOTE.read_text(encoding="utf-8").split())
    raw = RAW.read_text(encoding="utf-8")
    manifest = RAW_MANIFEST.read_text(encoding="utf-8")
    raw_digest = hashlib.sha256(RAW.read_bytes()).hexdigest()

    dispatch = section(
        cpp,
        "case APPLE80211_IOC_AWDL_ELECTION_METRIC:",
        "case APPLE80211_IOC_AWDL_BSSID:",
    )
    setter = section(
        cpp,
        "setAWDL_ELECTION_METRIC(OSObject *object, struct apple80211_awdl_election_metric *data)",
        "getAWDL_ELECTION_METRIC(OSObject *object, struct apple80211_awdl_election_metric *data)",
    )
    getter = section(
        cpp,
        "getAWDL_ELECTION_METRIC(OSObject *object, struct apple80211_awdl_election_metric *data)",
        "getSYNC_ENABLED(OSObject *object, struct apple80211_awdl_sync_enabled *data)",
    )
    virtual_request = section(
        cpp,
        "apple80211VirtualRequest(UInt request_type, int request_number,",
        "setAWDL_PEER_TRAFFIC_REGISTRATION(OSObject *object,",
    )
    ioct_macro = section(hpp, "#define IOCTL(REQ_TYPE, REQ, DATA_TYPE)", "#define IOCTL_GET")
    carrier = section(
        ioctl,
        "struct apple80211_awdl_election_metric",
        "struct apple80211_awdl_sync_enabled",
    )
    tahoe_marker = "F8E94CA52B9ABFE20081A3C4 /* Sources */ = {"
    tahoe_start = project.index(tahoe_marker)
    tahoe_phase = project[tahoe_start:project.index("\t\t};", tahoe_start)]

    return {
        "schema": "itlwm-legacy-awdl-election-metric-no-owner-quarantine-v1",
        "source_base_revision": "40ed258a1afc4a22f54b55ef52bedfaa3ac4fdc8",
        "reference": {
            "bootkc_sha256": "eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d",
            "bootkc_uuid": "F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5",
            "embedded_kext_uuid": "8FB4B7F0-D656-3539-B8D6-C1327A50377C",
            "public_wrapper": "0xffffff80021c44b5",
            "next_symbol": "0xffffff80021c450a",
            "selector": 126,
            "gate_vtable_offset": "0xcc8",
            "tail_vtable_offset": "0x10c8",
            "owner_mismatch_status": "0xe082280e",
        },
        "scope": {
            "deployment": False,
            "radio_or_association": False,
            "runtime_selector_invocation": False,
            "tahoe_source_modified_by_this_layer": False,
            "traffic": False,
        },
        "checks": {
            "reference_raw_manifest_matches": manifest == f"{raw_digest}  raw.txt\n",
            "reference_raw_records_conditional_dispatch_only": all(
                token in raw
                for token in (
                    "eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d",
                    "F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5",
                    "8FB4B7F0-D656-3539-B8D6-C1327A50377C",
                    "symbol_vmaddr=0xffffff80021c44b5",
                    "next_symbol_vmaddr=0xffffff80021c450a",
                    "selector=126",
                    "+0xcc8",
                    "+0x10c8",
                    "0xe082280e",
                    "does not dereference or validate the carrier",
                    "the terminal method, carrier layout",
                )
            ),
            "note_records_scope_reference_and_nonclaims": all(
                token in note
                for token in (
                    "IOC 126",
                    "0xffffff80021c44b5",
                    "0x7e",
                    "+0xcc8",
                    "+0x10c8",
                    "0xe082280e",
                    "not claim Apple valid-input return-code parity",
                    "no deployment, radio, association, AWDL, P2P, scan, firmware, event, traffic, or runtime-execution claim",
                )
            ),
            "selector_carrier_header_and_virtual_route_remain": (
                "#define APPLE80211_IOC_AWDL_ELECTION_METRIC 126" in ioctl
                and "uint32_t    version;" in carrier
                and "uint32_t    metric;" in carrier
                and "FUNC_IOCTL(AWDL_ELECTION_METRIC, apple80211_awdl_election_metric)" in hpp
                and "if (request_type != SIOCGA80211 && request_type != SIOCSA80211)" in virtual_request
                and "IOCTL(request_type, AWDL_ELECTION_METRIC, apple80211_awdl_election_metric);" in dispatch
                and "if (REQ_TYPE == SIOCGA80211)" in ioct_macro
                and "ret = get##REQ" in ioct_macro
                and "ret = set##REQ" in ioct_macro
            ),
            "setter_is_unread_and_fails_closed": (
                "(void)object;" in setter
                and "(void)data;" in setter
                and "return kIOReturnUnsupported;" in setter
                and "return kIOReturnSuccess;" not in setter
                and "data->" not in setter
                and "object->" not in setter
                and "memcpy" not in setter
                and "memset" not in setter
            ),
            "paired_getter_and_adjacent_awdl_state_remain_separate": (
                "return kIOReturnError;" in getter
                and "setAWDL_ELECTION_ID" in cpp
                and "awdlElectionId = data->election_id;" in cpp
                and "awdlElectionMetric" not in cpp
            ),
            "legacy_source_phase_only": (
                project.count("AirportVirtualIOCTL.cpp in Sources") >= 6
                and "AirportVirtualIOCTL.cpp" not in tahoe_phase
                and "AirportItlwmSkywalkInterface.cpp in Sources" in tahoe_phase
            ),
            "no_named_local_metric_owner_or_backend": all(
                not code_contains(token)
                for token in (
                    "awdlElectionMetric",
                    "setAWDLElectionMetric",
                    "AWDLElectionMetricOwner",
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
    failed = [name for name, passed in value["checks"].items() if not passed]
    if failed:
        raise ValueError("AWDL election-metric checks failed: " + ", ".join(failed))
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
        print(f"AWDL election-metric validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
