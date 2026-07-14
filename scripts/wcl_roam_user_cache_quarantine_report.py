#!/usr/bin/env python3
"""Generate and verify WCL Roam User Cache false-success quarantine evidence."""

import argparse
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/wcl_roam_user_cache_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-479-wcl-roam-user-cache-quarantine-20260713.md"
SIGNAL_AUDIT = ROOT / "docs/tahoe_signal_chain_audit.md"
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
    note = NOTE.read_text(encoding="utf-8")
    signal_audit = SIGNAL_AUDIT.read_text(encoding="utf-8")
    setter = section(
        cpp,
        "setWCL_ROAM_USER_CACHE(apple80211_user_roam_cache *data)",
        "setWCL_REASSOC",
    )
    correction_heading = "## Q13 correction: WCL Roam User Cache is RoamAdapter-backed"
    return {
        "schema": "itlwm-wcl-roam-user-cache-quarantine-v1",
        "source_base_revision": "a9d72a587c93d7976ddd0ca0d6ca0fd89da6a9cb",
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "infra_wrapper": "0x100018ca0",
            "infra_virtual_offset": "0x6e0",
            "core_setter": "0x100141e52",
            "roam_adapter_offset": "0x15c0",
            "adapter_command": "0x10001c916",
            "adapter_state_bytes": "0x78",
            "request_validator": "0x10001ca9a",
            "clear_channels": "0x10001cb3a",
            "add_channels": "0x10001cc16",
            "set_override": "0x10001cd78",
            "channel_entry_stride": "0x0c",
            "channel_count_offset": "0x78",
            "override_offset": "0x7a",
        },
        "local": {
            "matching_roam_adapter_backend_implemented": False,
            "request_false_success": False,
            "full_carrier_layout_proven": False,
            "valid_input_or_error_is_apple_parity": False,
        },
        "checks": {
            "reference_note": all(
                token in note
                for token in (
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "0x100018ca0",
                    "`+0x6e0`",
                    "0x100141e52",
                    "`+0x15c0`",
                    "0x10001c916",
                    "0x78 bytes",
                    "0x10001ca9a",
                    "0x10001cb3a",
                    "0x10001cc16",
                    "0x10001cd78",
                    "0x0c strides",
                    "`+0x78`",
                    "`+0x7a`",
                    "complete public carrier layout",
                )
            ),
            "setter_quarantines_nonnull": all(
                token in setter
                for token in (
                    "if (data == nullptr)",
                    "return kIOReturnBadArgumentTahoe;",
                    "return kIOReturnUnsupported;",
                )
            )
            and all(
                token not in setter
                for token in (
                    "cachedUserRoamCache",
                    "hasCachedUserRoamCache",
                    "data->",
                    "return kIOReturnSuccess;",
                )
            ),
            "pseudo_state_and_layout_removed": all(
                token not in cpp and token not in hpp
                for token in (
                    "cachedUserRoamCache",
                    "hasCachedUserRoamCache",
                    "struct apple80211_user_roam_cache",
                )
            ),
            "scoped_roam_adapter_backend_absent": all(
                not source_contains(token)
                for token in (
                    "cmdROAM_USER_CACHE(",
                    "isAdaptiveRoamRequestValid(",
                    "clearChannelsFromUserRoamCache(",
                    "addChannelsToUserRoamCache(",
                    "setOverrideStateFromUserRoamCache(",
                    "runIOVarSet(",
                )
            ),
            "stale_q13_claim_corrected": correction_heading in signal_audit
            and "user-cache recovery demonstrates a RoamAdapter lifecycle and is reclassified"
            in signal_audit
            and "preserve the recovered caller-visible carrier state locally" not in signal_audit,
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
        raise ValueError("WCL Roam User Cache quarantine checks failed: " + ", ".join(failed))
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
        print(f"WCL Roam User Cache quarantine validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
