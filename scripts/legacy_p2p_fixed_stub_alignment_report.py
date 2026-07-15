#!/usr/bin/env python3
"""Generate and verify legacy P2P fixed-stub alignment evidence."""

import argparse
import hashlib
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/legacy_p2p_fixed_stub_alignment_report.json"
AWDL = ROOT / "AirportItlwm/AirportAWDL.cpp"
HEADER = ROOT / "AirportItlwm/AirportItlwm.hpp"
STA = ROOT / "AirportItlwm/AirportSTAIOCTL.cpp"
VIRTUAL = ROOT / "AirportItlwm/AirportVirtualIOCTL.cpp"
SKYWALK = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
PROJECT = ROOT / "itlwm.xcodeproj/project.pbxproj"
NOTE = ROOT / "docs/reference/CR-501-legacy-p2p-fixed-stub-alignment-20260715.md"
AUDIT = ROOT / "docs/tahoe_signal_chain_audit.md"
RAW = ROOT / "docs/reference/artifacts/p2p-public-fixed-stubs-bootkc-current/raw.txt"
RAW_MANIFEST = RAW.with_name("SHA256SUMS.txt")

BOOTKC_SHA256 = "eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d"
BOOTKC_UUID = "F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5"
KEXT_UUID = "8FB4B7F0-D656-3539-B8D6-C1327A50377C"
FIXED_STATUS = "0xe082280e"
FIXED_BODY = "55 48 89 e5 b8 0e 28 82 e0 5d c3"


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


def is_unread_fixed_stub(setter):
    forbidden = (
        "kIOReturnSuccess",
        "data->",
        "*reinterpret_cast",
        "memcpy",
        "memset",
        "APPLE80211_M_P2P_",
        "setSCAN_REQ",
        "IEEE80211_",
    )
    return (
        "(void)object;" in setter
        and "(void)data;" in setter
        and "return kP2PPublicFixedStubStatus;" in setter
        and all(token not in setter for token in forbidden)
    )


def build_report():
    awdl = AWDL.read_text(encoding="utf-8")
    header = HEADER.read_text(encoding="utf-8")
    sta = STA.read_text(encoding="utf-8")
    virtual = VIRTUAL.read_text(encoding="utf-8")
    skywalk = SKYWALK.read_text(encoding="utf-8")
    project = PROJECT.read_text(encoding="utf-8")
    note = NOTE.read_text(encoding="utf-8")
    audit = AUDIT.read_text(encoding="utf-8")
    raw = RAW.read_text(encoding="utf-8")

    scan_setter = section(
        awdl,
        "IOReturn AirportItlwm::\nsetP2P_SCAN(",
        "IOReturn AirportItlwm::\nsetP2P_LISTEN(",
    )
    listen_setter = section(
        awdl,
        "IOReturn AirportItlwm::\nsetP2P_LISTEN(",
        "IOReturn AirportItlwm::\nsetP2P_GO_CONF(",
    )
    go_conf_setter = awdl[awdl.index("IOReturn AirportItlwm::\nsetP2P_GO_CONF("):]
    sta_routes = section(
        sta,
        "        case APPLE80211_IOC_P2P_LISTEN:",
        "        default:\n        unhandled:",
    )
    virtual_routes = section(
        virtual,
        "        case APPLE80211_IOC_P2P_LISTEN:",
        "        default:\n        unhandled:",
    )
    scan_req = section(
        sta,
        "IOReturn AirportItlwm::\nsetSCAN_REQ(",
        "IOReturn AirportItlwm::\nsetSCAN_REQ_MULTIPLE(",
    )
    vif_create = section(
        sta,
        "IOReturn AirportItlwm::\nsetVIRTUAL_IF_CREATE(",
        "IOReturn AirportItlwm::\nsetVIRTUAL_IF_DELETE(",
    )
    vif_delete = section(
        sta,
        "IOReturn AirportItlwm::\nsetVIRTUAL_IF_DELETE(",
        "/*\n * V1 controller mirror",
    )
    tahoe_p2p_get = section(
        skywalk,
        "IOReturn AirportItlwmSkywalkInterface::\ngetP2P_DEVICE_CAPABILITY(",
        "IOReturn AirportItlwmSkywalkInterface::\ngetPRIVATE_MAC(",
    )
    tahoe_hp2p = section(
        skywalk,
        "IOReturn AirportItlwmSkywalkInterface::\nsetHP2P_CTRL(",
        "IOReturn AirportItlwmSkywalkInterface::\nsetSET_PROPERTY(",
    )
    tahoe_sources = section(
        project,
        "F8E94CA52B9ABFE20081A3C4 /* Sources */ = {",
        "/* End PBXSourcesBuildPhase section */",
    )

    route_tokens = (
        "IOCTL_SET(request_type, P2P_LISTEN, apple80211_p2p_listen_data);",
        "IOCTL_SET(request_type, P2P_SCAN, apple80211_scan_data);",
        "IOCTL_SET(request_type, P2P_GO_CONF, apple80211_p2p_go_conf_data);",
    )
    header_tokens = (
        "FUNC_IOCTL_SET(P2P_LISTEN, apple80211_p2p_listen_data)",
        "FUNC_IOCTL_SET(P2P_SCAN, apple80211_scan_data)",
        "FUNC_IOCTL_SET(P2P_GO_CONF, apple80211_p2p_go_conf_data)",
    )
    sibling_selectors = (
        "APPLE80211_IOC_P2P_ENABLE",
        "APPLE80211_IOC_P2P_NOA_LIST",
        "APPLE80211_IOC_P2P_OPP_PS",
        "APPLE80211_IOC_P2P_CT_WINDOW",
    )

    return {
        "schema": "itlwm-legacy-p2p-fixed-stub-alignment-v1",
        "source_base_revision": "c091ce97e72da5f6f7e18bdf8547f4213dee8a7b",
        "reference": {
            "kind": "current 25C56 BootKC embedded Wi-Fi KEXT exact fixed stubs",
            "bootkc_sha256": BOOTKC_SHA256,
            "bootkc_uuid": BOOTKC_UUID,
            "embedded_kext_uuid": KEXT_UUID,
            "fixed_status": FIXED_STATUS,
            "symbols": {
                "P2P_ENABLE": "0xffffff80021c40e4",
                "P2P_LISTEN": "0xffffff80021c40ef",
                "P2P_SCAN": "0xffffff80021c40fa",
                "P2P_GO_CONF": "0xffffff80021c417b",
            },
        },
        "scope": {
            "local_historical_setters": ["P2P_LISTEN", "P2P_SCAN", "P2P_GO_CONF"],
            "p2p_enable_local_implementation": False,
            "tahoe_source_modified_by_this_layer": False,
            "runtime_selector_invocation": False,
            "deployment": False,
            "radio_or_association": False,
            "traffic": False,
        },
        "checks": {
            "reference_raw_identity_and_symbol_records_present": all(
                token in raw
                for token in (
                    BOOTKC_SHA256,
                    BOOTKC_UUID,
                    KEXT_UUID,
                    "mh_kext_fileoff=0x1fe3000",
                    "lc_symtab_absolute_symoff=0x3498030",
                    "lc_symtab_nsyms=19760",
                    "lc_symtab_absolute_stroff=0x367e130",
                    "symbol=setP2P_ENABLE vmaddr=0xffffff80021c40e4",
                    "symbol=setP2P_LISTEN vmaddr=0xffffff80021c40ef",
                    "symbol=setP2P_SCAN vmaddr=0xffffff80021c40fa",
                    "symbol=setP2P_GO_CONF vmaddr=0xffffff80021c417b",
                )
            )
            and raw.count(FIXED_BODY) == 4
            and "reads neither public argument" in raw
            and "not be relabelled as kIOReturnUnsupported" in raw,
            "reference_raw_manifest_matches": manifest_matches_raw(),
            "local_header_keeps_only_the_three_typed_setters": (
                all(token in header for token in header_tokens)
                and "FUNC_IOCTL_SET(P2P_ENABLE," not in header
                and "FUNC_IOCTL_SET(P2P_NOA_LIST," not in header
                and "FUNC_IOCTL_SET(P2P_OPP_PS," not in header
                and "FUNC_IOCTL_SET(P2P_CT_WINDOW," not in header
            ),
            "both_historical_dispatchers_retain_set_only_routes": (
                all(token in sta_routes for token in route_tokens)
                and all(token in virtual_routes for token in route_tokens)
                and "#define IOCTL_SET(REQ_TYPE, REQ, DATA_TYPE)" in header
                and "if (REQ_TYPE == SIOCSA80211)" in header
                and "ret = set##REQ(interface, (struct DATA_TYPE* )data);" in header
            ),
            "three_setters_are_unread_exact_fixed_stubs": (
                "kP2PPublicFixedStubStatus" in awdl
                and FIXED_STATUS in awdl
                and is_unread_fixed_stub(scan_setter)
                and is_unread_fixed_stub(listen_setter)
                and is_unread_fixed_stub(go_conf_setter)
            ),
            "unimplemented_sibling_selectors_remain_unrouted": all(
                token not in sta and token not in virtual
                for token in sibling_selectors
            ),
            "normal_scan_and_virtual_interface_lifecycles_preserved": (
                all(
                    token in scan_req
                    for token in (
                        "ieee80211_begin_cache_bgscan",
                        "scanSource->enable();",
                        "return kIOReturnSuccess;",
                    )
                )
                and all(
                    token in vif_create
                    for token in (
                        "APPLE80211_VIF_P2P_DEVICE",
                        "APPLE80211_VIF_P2P_GO",
                        "new IO80211P2PInterface",
                        "ensureAPSTAOwner",
                    )
                )
                and all(
                    token in vif_delete
                    for token in (
                        "deleteAPSTAOwnerForBSDName",
                        "detachVirtualInterface",
                    )
                )
            ),
            "separate_tahoe_p2p_hp2p_surfaces_preserved": (
                "data->capability = 0;" in tahoe_p2p_get
                and "return kIOReturnSuccess;" in tahoe_p2p_get
                and "return kIOReturnUnsupported;" in tahoe_hp2p
            ),
            "awdl_remains_historical_and_absent_from_tahoe_phase": (
                project.count("AirportAWDL.cpp in Sources") >= 6
                and "AirportAWDL.cpp in Sources" not in tahoe_sources
                and "AirportItlwmSkywalkInterface.cpp in Sources" in tahoe_sources
            ),
            "correction_records_scope_and_nonclaims": all(
                token in note
                for token in (
                    "0xe082280e",
                    "not `kIOReturnUnsupported`",
                    "does not claim that Apple historical P2P implementation",
                    "No private selector/carrier is invoked.",
                )
            )
            and "legacy P2P fixed-stub alignment" in audit,
        },
    }


def render_report():
    value = build_report()
    failed = [name for name, passed in value["checks"].items() if not passed]
    if failed:
        raise ValueError("legacy P2P fixed-stub alignment checks failed: " + ", ".join(failed))
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
        print(f"legacy P2P fixed-stub alignment validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
