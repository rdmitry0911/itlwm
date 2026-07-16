#!/usr/bin/env python3
"""Generate and verify Skywalk public STA/RSN GET fixed-stub evidence."""

import argparse
import hashlib
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/skywalk_public_sta_rsn_get_fixed_stubs_alignment_report.json"
NOTE = ROOT / "docs/reference/CR-562-skywalk-public-sta-rsn-get-fixed-stubs-alignment-20260716.md"
RAW = ROOT / "docs/reference/artifacts/skywalk-sta-rsn-get-public-fixed-stubs-bootkc-current/raw.txt"
RAW_MANIFEST = RAW.with_name("SHA256SUMS.txt")
CPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
V2 = ROOT / "AirportItlwm/AirportItlwmV2.cpp"
V1 = ROOT / "AirportItlwm/AirportSTAIOCTL.cpp"
VIRTUAL = ROOT / "AirportItlwm/AirportVirtualIOCTL.cpp"
ROUTES = ROOT / "AirportItlwm/TahoeSkywalkIoctlRoutes.hpp"
IOCTL = ROOT / "include/Airport/apple80211_ioctl.h"
PROJECT = ROOT / "itlwm.xcodeproj/project.pbxproj"

SELECTORS = (
    ("STA_AUTHORIZE", "APPLE80211_IOC_STA_AUTHORIZE", 74, 7450,
     "0xffffff80021bf0fa", "0x20bf0fa",
     "__Z26apple80211getSTA_AUTHORIZEP23IO80211SkywalkInterfaceP29apple80211_sta_authorize_data", 7665,
     "__Z29apple80211getSTA_DISASSOCIATEP23IO80211SkywalkInterfaceP28apple80211_sta_disassoc_data",
     "instance->setSTA_AUTHORIZE(", "AirportItlwmAPSTAStaAuthorizeInputLayout"),
    ("STA_DISASSOCIATE", "APPLE80211_IOC_STA_DISASSOCIATE", 75, 7665,
     "0xffffff80021bf105", "0x20bf105",
     "__Z29apple80211getSTA_DISASSOCIATEP23IO80211SkywalkInterfaceP28apple80211_sta_disassoc_data", 7250,
     "__Z23apple80211getSTA_DEAUTHP23IO80211SkywalkInterfaceP28apple80211_sta_disassoc_data",
     "instance->setSTA_DISASSOCIATE(", "AirportItlwmAPSTAStaDisassocInputLayout *)req->req_data, false"),
    ("STA_DEAUTH", "APPLE80211_IOC_STA_DEAUTH", 76, 7250,
     "0xffffff80021bf110", "0x20bf110",
     "__Z23apple80211getSTA_DEAUTHP23IO80211SkywalkInterfaceP28apple80211_sta_disassoc_data", 7148,
     "__Z21apple80211getRSN_CONFP23IO80211SkywalkInterfaceP24apple80211_rsn_conf_data",
     "instance->setSTA_DISASSOCIATE(", "AirportItlwmAPSTAStaDisassocInputLayout *)req->req_data, true"),
    ("RSN_CONF", "APPLE80211_IOC_RSN_CONF", 77, 7148,
     "0xffffff80021bf11b", "0x20bf11b",
     "__Z21apple80211getRSN_CONFP23IO80211SkywalkInterfaceP24apple80211_rsn_conf_data", 7096,
     "__Z20apple80211getKEY_RSCP23IO80211SkywalkInterfaceP14apple80211_key",
     "instance->setRSN_CONF(", "apple80211_rsn_conf_data *)req->req_data"),
)


def section(source, begin, end):
    start = source.index(begin)
    return source[start:source.index(end, start)]


def report():
    cpp, v2, v1, virtual, routes, ioctl, project = (
        path.read_text(encoding="utf-8")
        for path in (CPP, V2, V1, VIRTUAL, ROUTES, IOCTL, PROJECT)
    )
    note = " ".join(NOTE.read_text(encoding="utf-8").split())
    raw = RAW.read_text(encoding="utf-8")
    manifest = RAW_MANIFEST.read_text(encoding="utf-8")
    raw_digest = hashlib.sha256(RAW.read_bytes()).hexdigest()
    bsd = section(cpp, "processBSDCommand(ifnet_t interface, UInt cmd, void *data)", "processApple80211Ioctl(UInt cmd, apple80211req *req)")
    dispatcher = section(cpp, "processApple80211Ioctl(UInt cmd, apple80211req *req)", "IOReturn AirportItlwmSkywalkInterface::\ngetAUTH_TYPE")
    group = section(dispatcher, "case APPLE80211_IOC_STA_AUTHORIZE:", "        case APPLE80211_IOC_VHT_MCS_INDEX_SET:")
    pre26_dispatcher = dispatcher.replace(group, "")
    card = section(v2, "SInt32 AirportItlwm::handleCardSpecific(", "IOReturn AirportItlwm::enableAdapter")
    macros = tuple(item[1] for item in SELECTORS)
    markers = (
        "F8A028722A4A7FE100C6DE90 /* AirportItlwmSkywalkInterface.cpp in Sources */",
        "F8D94CD22B9ABFE20081A3C4 /* AirportItlwmSkywalkInterface.cpp in Sources */",
        "F8E94CD22B9ABFE20081A3C4 /* AirportItlwmSkywalkInterface.cpp in Sources */",
    )
    raw_tokens = (
        "eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d",
        "F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5",
        "8FB4B7F0-D656-3539-B8D6-C1327A50377C",
        "batch_symbol_vmaddr_start=0xffffff80021bf0fa",
        "batch_symbol_vmaddr_end=0xffffff80021bf126",
        "batch_symbol_fileoff_start=0x20bf0fa",
        "batch_symbol_fileoff_end=0x20bf126",
        "batch_symbol_bytes=0x2c",
        "batch_body_sha256=7cafec9b3e93326f5496eb094c961c255f27309b88a01744fb7a5aaccead46b2",
        "raw=55 48 89 e5 b8 0e 28 82 e0 5d c3",
        "body_sha256=9e4580f0175946d7624b2451e6ffda84a93e91e3e2d852d7a2c7998ee2d78576",
        "reads neither public argument",
    )
    for name, macro, value, nlist, vmaddr, fileoff, symbol, next_nlist, next_symbol, set_call, set_payload in SELECTORS:
        raw_tokens += (
            f"selector={name}", f"symbol={symbol}", f"nlist_index={nlist}",
            f"symbol_vmaddr={vmaddr}", f"symbol_fileoff={fileoff}",
            "symbol_bytes=0x0b", f"next_nlist_index={next_nlist}", f"next_symbol={next_symbol}",
        )
    blocks = []
    for index, item in enumerate(SELECTORS):
        macro = item[1]
        end = f"case {SELECTORS[index + 1][1]}:" if index + 1 < len(SELECTORS) else "        case APPLE80211_IOC_VHT_MCS_INDEX_SET:"
        blocks.append(section(dispatcher, f"case {macro}:", end))
    return {
        "schema": "itlwm-skywalk-public-sta-rsn-get-fixed-stubs-alignment-v1",
        "source_base_revision": "f9e56dd1904d8e1e8b2230e422486e24d91cce08",
        "reference": {
            "bootkc_sha256": "eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d",
            "bootkc_uuid": "F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5",
            "embedded_kext_uuid": "8FB4B7F0-D656-3539-B8D6-C1327A50377C",
            "fixed_status": "0xe082280e",
            "body_sha256": "9e4580f0175946d7624b2451e6ffda84a93e91e3e2d852d7a2c7998ee2d78576",
            "batch_body_sha256": "7cafec9b3e93326f5496eb094c961c255f27309b88a01744fb7a5aaccead46b2",
            "selectors": [
                {"name": name, "ioc": value, "nlist_index": nlist, "public_wrapper": vmaddr, "fileoff": fileoff, "symbol": symbol, "next_nlist_index": next_nlist, "next_symbol": next_symbol}
                for name, macro, value, nlist, vmaddr, fileoff, symbol, next_nlist, next_symbol, set_call, set_payload in SELECTORS
            ],
        },
        "scope": {
            "public_nonnull_request_object_tahoe_bsd_get_only": True,
            "carrier_is_not_observed_for_get": True,
            "set_behavior_changed": False,
            "outer_null_dispatch_modified": False,
            "pre26_route_modified": False,
            "card_specific_route_modified": False,
            "legacy_v1_get_modified": False,
            "virtual_ioctl_modified": False,
            "carrier_abi_added": False,
            "deployment": False,
            "radio_or_association": False,
            "runtime_selector_invocation": False,
            "traffic": False,
        },
        "checks": {
            "reference_raw_manifest_matches": manifest == f"{raw_digest}  raw.txt\n",
            "reference_raw_has_all_exact_unread_public_get_fixed_stubs": all(token in raw for token in raw_tokens),
            "note_records_scope_and_nonclaims": all(token in note for token in (
                "IOC 74", "IOC 75", "IOC 76", "IOC 77", "0xffffff80021bf0fa..0xffffff80021bf126", "0xe082280e",
                "compile-time Tahoe-only GET branch", "four public GET wrappers remain absent from the pre-26 route",
                "No local carrier contract is inferred for any GET member", "card-specific route has no STA/RSN group entry",
                "does not claim outer-null dispatch behavior, a carrier contract, STA authorization behavior, STA disassociation behavior, STA deauth behavior, RSN configuration behavior, SET behavior, V1 GET behavior, Virtual IOCTL, card-specific behavior, firmware, runtime-execution, radio, association, traffic, or broader Tahoe behavior parity",
                "No private carrier or selector is constructed or invoked",
            )),
            "public_tahoe_get_prefixes_return_exact_unread_status": (
                all(dispatcher.count(f"case {macro}:") == 1 for macro in macros)
                and all(block.count("if (cmd == SIOCGA80211)") == 1 for block in blocks)
                and all(block.count("return static_cast<IOReturn>(0xe082280e);") == 1 for block in blocks)
                and all("req->req_data" not in block[:block.index("if (instance == NULL)")] for block in blocks)
            ),
            "existing_set_producers_and_null_order_remain_explicit": all(
                "if (instance == NULL)" in block
                and block.index("if (cmd == SIOCGA80211)") < block.index("if (instance == NULL)")
                and "cmd == SIOCSA80211" in block
                and set_call in block
                and set_payload in block
                and "return kIOReturnNotReady;" in block
                and ": kIOReturnUnsupported;" in block
                for block, (_, _, _, _, _, _, _, _, _, set_call, set_payload) in zip(blocks, SELECTORS)
            ),
            "tahoe_and_pre26_boundaries_remain_explicit": (
                all("#if __IO80211_TARGET >= __MAC_26_0" in block and "#endif // __IO80211_TARGET >= __MAC_26_0" in block for block in blocks)
                and all(f"case {macro}:" not in pre26_dispatcher for macro in macros)
            ),
            "outer_null_and_bsd_boundaries_remain_explicit": (
                "if (req == NULL)\n        return kIOReturnUnsupported;" in dispatcher
                and dispatcher.index("if (req == NULL)\n        return kIOReturnUnsupported;") < dispatcher.index("case APPLE80211_IOC_STA_AUTHORIZE:")
                and "data != NULL" in bsd
                and "IOReturn ret = processApple80211Ioctl(normalizedCmd, req);" in bsd
                and "if (ret != kIOReturnUnsupported)\n            return ret;" in bsd
            ),
            "legacy_virtual_card_and_set_helper_boundaries_remain_explicit": (
                "case APPLE80211_IOC_RSN_CONF:" in v1
                and "IOCTL_SET(request_type, RSN_CONF, apple80211_rsn_conf_data);" in v1
                and all(macro not in virtual and macro not in routes for macro in macros)
                and "default:\n            return false;" in routes
                and "routeTahoeSkywalkIoctl(interface, &req," in card
                and "IOReturn AirportItlwm::setSTA_AUTHORIZE(" in v2
                and "IOReturn AirportItlwm::setSTA_DISASSOCIATE(" in v2
                and "IOReturn AirportItlwm::setRSN_CONF(" in v2
            ),
            "selector_definitions_and_active_source_phases_are_explicit": (
                all(f"#define {macro}" in ioctl and f"{value}" in ioctl[ioctl.index(f"#define {macro}"):ioctl.index("\n", ioctl.index(f"#define {macro}"))] for name, macro, value, nlist, vmaddr, fileoff, symbol, next_nlist, next_symbol, set_call, set_payload in SELECTORS)
                and all(marker in project for marker in markers)
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
        raise ValueError("Skywalk public STA/RSN GET alignment validation failed: " + ", ".join(failed))
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
        print(f"Skywalk public STA/RSN GET alignment validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
