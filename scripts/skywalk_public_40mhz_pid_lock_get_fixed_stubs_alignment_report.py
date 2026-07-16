#!/usr/bin/env python3
"""Generate and verify Skywalk public 40MHz/PID-lock GET fixed-stub evidence."""

import argparse
import hashlib
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/skywalk_public_40mhz_pid_lock_get_fixed_stubs_alignment_report.json"
NOTE = ROOT / "docs/reference/CR-561-skywalk-public-40mhz-pid-lock-get-fixed-stubs-alignment-20260716.md"
RAW = ROOT / "docs/reference/artifacts/skywalk-40mhz-pid-lock-get-public-fixed-stubs-bootkc-current/raw.txt"
RAW_MANIFEST = RAW.with_name("SHA256SUMS.txt")
CPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
V2 = ROOT / "AirportItlwm/AirportItlwmV2.cpp"
V1 = ROOT / "AirportItlwm/AirportSTAIOCTL.cpp"
VIRTUAL = ROOT / "AirportItlwm/AirportVirtualIOCTL.cpp"
ROUTES = ROOT / "AirportItlwm/TahoeSkywalkIoctlRoutes.hpp"
IOCTL = ROOT / "include/Airport/apple80211_ioctl.h"
PROJECT = ROOT / "itlwm.xcodeproj/project.pbxproj"

SELECTORS = (
    ("40MHZ_INTOLERANT", "APPLE80211_IOC_40MHZ_INTOLERANT", 71, 7642,
     "0xffffff80021bf08f", "0x20bf08f",
     "__Z29apple80211get40MHZ_INTOLERANTP23IO80211SkywalkInterfacePv", 7145,
     "__Z21apple80211getPID_LOCKP23IO80211SkywalkInterfaceP21apple80211_state_data"),
    ("PID_LOCK", "APPLE80211_IOC_PID_LOCK", 72, 7145,
     "0xffffff80021bf09a", "0x20bf09a",
     "__Z21apple80211getPID_LOCKP23IO80211SkywalkInterfaceP21apple80211_state_data", 7316,
     "__Z24apple80211getSTA_IE_LISTP23IO80211SkywalkInterfaceP22apple80211_sta_ie_data"),
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
    tahoe_block = section(dispatcher, "#if __IO80211_TARGET >= __MAC_26_0\n        case APPLE80211_IOC_40MHZ_INTOLERANT:", "        case APPLE80211_IOC_POWERSAVE:")
    group = section(dispatcher, "case APPLE80211_IOC_40MHZ_INTOLERANT:", "        case APPLE80211_IOC_POWERSAVE:")
    pre26_dispatcher = dispatcher.replace(tahoe_block, "")
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
        "batch_symbol_vmaddr_start=0xffffff80021bf08f",
        "batch_symbol_vmaddr_end=0xffffff80021bf0a5",
        "batch_symbol_fileoff_start=0x20bf08f",
        "batch_symbol_fileoff_end=0x20bf0a5",
        "batch_symbol_bytes=0x16",
        "batch_body_sha256=d10aa56f7dc33c2d99918578125f1c133d33dc0eaf3ecbd23caa44b6790369ef",
        "raw=55 48 89 e5 b8 0e 28 82 e0 5d c3",
        "body_sha256=9e4580f0175946d7624b2451e6ffda84a93e91e3e2d852d7a2c7998ee2d78576",
        "reads neither public argument",
    )
    for name, macro, value, nlist, vmaddr, fileoff, symbol, next_nlist, next_symbol in SELECTORS:
        raw_tokens += (
            f"selector={name}", f"symbol={symbol}", f"nlist_index={nlist}",
            f"symbol_vmaddr={vmaddr}", f"symbol_fileoff={fileoff}",
            "symbol_bytes=0x0b", f"next_nlist_index={next_nlist}", f"next_symbol={next_symbol}",
        )
    return {
        "schema": "itlwm-skywalk-public-40mhz-pid-lock-get-fixed-stubs-alignment-v1",
        "source_base_revision": "9caad3685ca0f8580270ed4efc601028b73a98b4",
        "reference": {
            "bootkc_sha256": "eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d",
            "bootkc_uuid": "F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5",
            "embedded_kext_uuid": "8FB4B7F0-D656-3539-B8D6-C1327A50377C",
            "fixed_status": "0xe082280e",
            "body_sha256": "9e4580f0175946d7624b2451e6ffda84a93e91e3e2d852d7a2c7998ee2d78576",
            "batch_body_sha256": "d10aa56f7dc33c2d99918578125f1c133d33dc0eaf3ecbd23caa44b6790369ef",
            "selectors": [
                {"name": name, "ioc": value, "nlist_index": nlist, "public_wrapper": vmaddr, "fileoff": fileoff, "symbol": symbol, "next_nlist_index": next_nlist, "next_symbol": next_symbol}
                for name, macro, value, nlist, vmaddr, fileoff, symbol, next_nlist, next_symbol in SELECTORS
            ],
        },
        "scope": {
            "public_nonnull_request_object_tahoe_bsd_get_only": True,
            "carrier_is_not_observed": True,
            "set_modified": False,
            "outer_null_dispatch_modified": False,
            "pre26_route_modified": False,
            "card_specific_route_modified": False,
            "legacy_v1_modified": False,
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
                "IOC 71", "IOC 72", "0xffffff80021bf08f..0xffffff80021bf0a5", "0xe082280e", "compile-time Tahoe-only case",
                "selectors remain absent from the pre-26 switch", "No local carrier contract is inferred for either member",
                "card-specific route has no 40MHz/PID-lock entry",
                "does not claim outer-null dispatch behavior, a carrier contract, SET behavior, 40MHz-intolerant behavior, PID-lock behavior, V1, Virtual IOCTL, card-specific behavior, firmware, runtime-execution, radio, association, traffic, or broader Tahoe behavior parity",
                "No private carrier or selector is constructed or invoked",
            )),
            "public_tahoe_get_returns_exact_unread_status_for_every_selector": (
                all(dispatcher.count(f"case {macro}:") == 1 for macro in macros)
                and group.count("if (cmd == SIOCGA80211)") == 1
                and group.count("return static_cast<IOReturn>(0xe082280e);") == 1
                and "req->req_data" not in group
                and "return kIOReturnSuccess;" not in group
            ),
            "tahoe_nonget_and_pre26_boundaries_remain_explicit": (
                "return kIOReturnUnsupported;" in group
                and "SIOCSA80211" not in group
                and "#endif // __IO80211_TARGET >= __MAC_26_0" in group
                and all(f"case {macro}:" not in pre26_dispatcher for macro in macros)
            ),
            "outer_null_and_bsd_boundaries_remain_explicit": (
                "if (req == NULL)\n        return kIOReturnUnsupported;" in dispatcher
                and dispatcher.index("if (req == NULL)\n        return kIOReturnUnsupported;") < dispatcher.index("case APPLE80211_IOC_40MHZ_INTOLERANT:")
                and "data != NULL" in bsd
                and "IOReturn ret = processApple80211Ioctl(normalizedCmd, req);" in bsd
                and "if (ret != kIOReturnUnsupported)\n            return ret;" in bsd
            ),
            "legacy_v1_virtual_and_card_boundaries_remain_explicit": (
                all(macro not in v1 and macro not in virtual and macro not in routes for macro in macros)
                and "default:\n            return false;" in routes
                and "routeTahoeSkywalkIoctl(interface, &req," in card
            ),
            "selector_and_carrier_boundaries_are_explicit": (
                all(f"#define {macro}" in ioctl and f"{value}" in ioctl[ioctl.index(f"#define {macro}"):ioctl.index("\n", ioctl.index(f"#define {macro}"))] for name, macro, value, nlist, vmaddr, fileoff, symbol, next_nlist, next_symbol in SELECTORS)
                and "req->req_data" not in group
            ),
            "all_active_skywalk_source_phases_and_target_guard_are_explicit": (
                all(marker in project for marker in markers)
                and all(f"case {macro}:" in tahoe_block for macro in macros)
                and "#endif // __IO80211_TARGET >= __MAC_26_0" in tahoe_block
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
        raise ValueError("Skywalk public 40MHz/PID-lock GET alignment validation failed: " + ", ".join(failed))
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
        print(f"Skywalk public 40MHz/PID-lock GET alignment validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
