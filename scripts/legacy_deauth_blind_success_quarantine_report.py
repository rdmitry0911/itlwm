#!/usr/bin/env python3
"""Generate and verify legacy DEAUTH blind-success quarantine evidence."""

import argparse
import hashlib
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/legacy_deauth_blind_success_quarantine_report.json"
LEGACY_CPP = ROOT / "AirportItlwm/AirportSTAIOCTL.cpp"
LEGACY_HPP = ROOT / "AirportItlwm/AirportItlwm.hpp"
SKYWALK = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
PROJECT = ROOT / "itlwm.xcodeproj/project.pbxproj"
NOTE = ROOT / "docs/reference/CR-500-legacy-deauth-blind-success-quarantine-20260715.md"
AUDIT = ROOT / "docs/tahoe_signal_chain_audit.md"
RAW = ROOT / "docs/reference/artifacts/deauth-selector-dispatch-bootkc-current/raw.txt"
RAW_MANIFEST = RAW.with_name("SHA256SUMS.txt")

BOOTKC_SHA256 = "eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d"
BOOTKC_UUID = "F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5"


def section(source, begin, end):
    start = source.index(begin)
    return source[start:source.index(end, start)]


def manifest_matches_raw():
    expected = hashlib.sha256(RAW.read_bytes()).hexdigest()
    lines = RAW_MANIFEST.read_text(encoding="utf-8").splitlines()
    return any(
        line == f"{expected}  raw.txt" or line == f"{expected} *raw.txt"
        for line in lines
    )


def build_report():
    legacy_cpp = LEGACY_CPP.read_text(encoding="utf-8")
    legacy_hpp = LEGACY_HPP.read_text(encoding="utf-8")
    skywalk = SKYWALK.read_text(encoding="utf-8")
    project = PROJECT.read_text(encoding="utf-8")
    note = NOTE.read_text(encoding="utf-8")
    audit = AUDIT.read_text(encoding="utf-8")
    raw = RAW.read_text(encoding="utf-8")

    legacy_route = section(
        legacy_cpp,
        "        case APPLE80211_IOC_DEAUTH:\n",
        "        case APPLE80211_IOC_TX_ANTENNA:",
    )
    legacy_setter = section(
        legacy_cpp,
        "IOReturn AirportItlwm::\nsetDEAUTH(OSObject *object,",
        "void AirportItlwm::\neventHandler",
    )
    legacy_getter = section(
        legacy_cpp,
        "IOReturn AirportItlwm::\ngetDEAUTH(OSObject *object,",
        "IOReturn AirportItlwm::\ngetASSOCIATION_STATUS",
    )
    legacy_disassociate = section(
        legacy_cpp,
        "IOReturn AirportItlwm::setDISASSOCIATE(OSObject *object)",
        "IOReturn AirportItlwm::\ngetSUPPORTED_CHANNELS",
    )
    skywalk_setter = section(
        skywalk,
        "IOReturn AirportItlwmSkywalkInterface::\nsetDEAUTH(",
        "IOReturn AirportItlwmSkywalkInterface::\ngetMCS(",
    )
    tahoe_sources = section(
        project,
        "F8E94CA52B9ABFE20081A3C4 /* Sources */ = {",
        "/* End PBXSourcesBuildPhase section */",
    )

    setter_forbidden = (
        "kIOReturnSuccess",
        "fHalService",
        "IEEE80211_SEND_MGMT",
        "ieee80211_new_state",
        "ic_deauth_reason",
    )
    return {
        "schema": "itlwm-legacy-deauth-blind-success-quarantine-v1",
        "source_base_revision": "51e9f90c4486918b7c97678c3dda007b123fcd92",
        "reference": {
            "kind": "current BootKC public selector topology only",
            "bootkc_sha256": BOOTKC_SHA256,
            "bootkc_uuid": BOOTKC_UUID,
            "wrapper": "apple80211setDEAUTH",
            "selector": "0x1d",
            "terminal_vtable_offset": "0x2e0",
        },
        "scope": {
            "legacy_controller_setter_only": True,
            "tahoe_skywalk_setter_modified_by_this_layer": False,
            "runtime_selector_invocation": False,
            "deployment": False,
            "radio_or_association": False,
            "traffic": False,
        },
        "checks": {
            "reference_raw_identity_and_topology_present": (
                BOOTKC_SHA256 in raw
                and BOOTKC_UUID in raw
                and "apple80211setDEAUTH" in raw
                and "0x1d" in raw
                and "0x2e0" in raw
            ),
            "reference_raw_manifest_matches": manifest_matches_raw(),
            "legacy_header_retains_typed_bidirectional_deauth": (
                "FUNC_IOCTL(DEAUTH, apple80211_deauth_data)" in legacy_hpp
            ),
            "legacy_ioc29_routes_through_typed_macro": (
                "IOCTL(request_type, DEAUTH, apple80211_deauth_data);" in legacy_route
            ),
            "legacy_ioctl_macro_still_selects_set_half": all(
                token in legacy_hpp
                for token in (
                    "#define IOCTL(REQ_TYPE, REQ, DATA_TYPE)",
                    "if (REQ_TYPE == SIOCGA80211)",
                    "ret = get##REQ(interface, (struct DATA_TYPE* )data);",
                    "ret = set##REQ(interface, (struct DATA_TYPE* )data);",
                )
            ),
            "legacy_setter_is_unread_fail_closed": (
                "(void)object;" in legacy_setter
                and "(void)da;" in legacy_setter
                and "return kIOReturnUnsupported;" in legacy_setter
                and all(token not in legacy_setter for token in setter_forbidden)
            ),
            "paired_legacy_getter_is_preserved": (
                "da->deauth_reason = ic->ic_deauth_reason;" in legacy_getter
                and "return kIOReturnSuccess;" in legacy_getter
            ),
            "legacy_disassociate_not_substituted": all(
                token in legacy_disassociate
                for token in (
                    "IEEE80211_SEND_MGMT",
                    "ieee80211_del_ess",
                    "ieee80211_new_state",
                )
            ),
            "separate_tahoe_skywalk_quarantine_retained": (
                "(void)da;" in skywalk_setter
                and "return kIOReturnUnsupported;" in skywalk_setter
                and "This public carrier is not the void DISASSOCIATE lifecycle." in skywalk_setter
            ),
            "legacy_source_remains_historical_and_absent_from_tahoe_phase": (
                project.count("AirportSTAIOCTL.cpp in Sources") >= 6
                and "AirportSTAIOCTL.cpp in Sources" not in tahoe_sources
                and "AirportItlwmSkywalkInterface.cpp in Sources" in tahoe_sources
            ),
            "correction_records_scope_and_nonclaims": all(
                token in note
                for token in (
                    "current Skywalk topology, not a recovered legacy AirportItlwm terminal",
                    "does not alter the Tahoe Skywalk bridge corrected by CR-499",
                    "not Apple legacy semantic parity",
                )
            )
            and "legacy IOC 29 DEAUTH blind-success quarantine" in audit,
        },
    }


def render_report():
    value = build_report()
    failed = [name for name, passed in value["checks"].items() if not passed]
    if failed:
        raise ValueError("legacy DEAUTH quarantine checks failed: " + ", ".join(failed))
    return json.dumps(value, indent=2, sort_keys=True) + "\n"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--write", action="store_true")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if args.write == args.check:
        parser.error("select exactly one of --write or --check")

    rendered = render_report()
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
        print(f"legacy DEAUTH quarantine validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
