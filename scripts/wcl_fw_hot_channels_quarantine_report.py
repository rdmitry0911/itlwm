#!/usr/bin/env python3
"""Generate and verify WCL FW hot channels no-producer evidence."""

import argparse
import hashlib
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/wcl_fw_hot_channels_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-498-wcl-fw-hot-channels-no-producer-quarantine-20260715.md"
RAW = ROOT / "docs/reference/artifacts/wcl-fw-hot-channels-25c56/raw.txt"
RAW_MANIFEST = RAW.with_name("SHA256SUMS.txt")
SIGNAL_AUDIT = ROOT / "docs/tahoe_signal_chain_audit.md"
CPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
HPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.hpp"
V2 = ROOT / "AirportItlwm/AirportItlwmV2.cpp"
INFRA = ROOT / "include/Airport/IO80211InfraProtocol.h"
IOCTL = ROOT / "include/Airport/apple80211_ioctl.h"
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
            if not path.is_file() or path.suffix not in {".c", ".cc", ".cpp", ".h", ".hpp"}:
                continue
            if token in path.read_text(encoding="utf-8", errors="ignore"):
                return True
    return False


def report():
    cpp = CPP.read_text(encoding="utf-8")
    hpp = HPP.read_text(encoding="utf-8")
    v2 = V2.read_text(encoding="utf-8")
    infra = INFRA.read_text(encoding="utf-8")
    ioctl = IOCTL.read_text(encoding="utf-8")
    note = " ".join(NOTE.read_text(encoding="utf-8").split())
    raw = RAW.read_text(encoding="utf-8")
    manifest = RAW_MANIFEST.read_text(encoding="utf-8")
    signal_audit = SIGNAL_AUDIT.read_text(encoding="utf-8")
    getter = section(
        cpp,
        "getWCL_FW_HOT_CHANNELS(apple80211_fw_hot_channels *data)",
        "getWCL_TRAFFIC_COUNTERS(apple80211_wcl_traffic_counters *data)",
    )
    closed_zone = section(signal_audit, "Closed in this zone:", "Recovered Apple behavior")
    bucket_zone = section(
        signal_audit,
        "Recovered Apple behavior splits into three public buckets:",
        "This batch intentionally stops",
    )
    raw_digest = hashlib.sha256(RAW.read_bytes()).hexdigest()

    return {
        "schema": "itlwm-wcl-fw-hot-channels-no-producer-quarantine-v1",
        "source_base_revision": "3089859c57c8fb116aa3b8539ade2027bfa474ab",
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "image_uuid_x86_64": "149C0AD1-A92F-35BC-AA69-5C8815C5421E",
            "infra_slot": 524,
            "infra_wrapper": "0x100017b74",
            "core_vtable_offset": "0x770",
            "core_vtable_cell": "0x1003A1858",
            "core_getter": "0x100140c84",
            "net_adapter_offset": "0x48+0x15e0",
            "net_adapter_getter": "0x100012db0",
            "firmware_iovar": "roam_channels_in_hotlist",
            "maximum_channels": 7,
        },
        "local": {
            "net_adapter_backend": False,
            "synthetic_zero_success": False,
            "numeric_ioc_route": False,
            "null_return_is_apple_parity": False,
            "valid_input_return_is_apple_parity": False,
            "runtime_selector_invocation": False,
        },
        "checks": {
            "reference_raw_manifest_matches": manifest == f"{raw_digest}  raw.txt\n",
            "reference_raw_has_routing_transport_and_bounded_output": all(
                token in raw
                for token in (
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "149C0AD1-A92F-35BC-AA69-5C8815C5421E",
                    "0x100017b74",
                    "0x770(%rax)",
                    "0x1003A1858         rebase  0x100140C84",
                    "0x100140c84",
                    "0x15e0(%rax)",
                    "0x100140c94  jmp    AppleBCMWLANNetAdapter::getFWHotChannels",
                    "getFWHotChannels",
                    "0x100012db0",
                    "0x100012dc2  movq   %rsi, %r14",
                    "roam_channels_in_hotlist",
                    "runIOVarGet",
                    "0x100012e2e  movl   %eax, %r15d",
                    "0x100012e31  testl  %eax, %eax",
                    "0x100012e33  je     0x100012e60",
                    "0x100012e3f  je     0x100012eca",
                    "0x100012e5e  jmp    0x100012eca",
                    "0x100012e66  cmpl   $0x7, %eax",
                    "0x100012e6f  cmovbl %eax, %r8d",
                    "0x100012e73  movl   %r8d, 0x10(%r14)",
                    "getAppleChannelSpec",
                    "0x100012eb9  movw   %ax, (%r14,%r12,2)",
                    "0x100012eda  movl   %r15d, %eax",
                )
            ),
            "reference_note_has_scope_and_nonclaim": all(
                token in note
                for token in (
                    "slot `[524]`",
                    "0x100017b74",
                    "0x100140c84",
                    "roam_channels_in_hotlist",
                    "not Apple null-input, valid-input return-code, full-carrier, channel-value, NetAdapter-owner, firmware, or runtime-selector parity",
                    "no numeric `APPLE80211_IOC_WCL_FW_HOT_CHANNELS`",
                )
            ),
            "protocol_slot_remains_without_invented_numeric_ioc": (
                "// [524]" in hpp
                and "getWCL_FW_HOT_CHANNELS" in hpp
                and "NetAdapter hot-channel state" in hpp
                and "fail closed" in hpp
                and "// [524]" in infra
                and "getWCL_FW_HOT_CHANNELS" in infra
                and "APPLE80211_IOC_WCL_FW_HOT_CHANNELS" not in ioctl
                and "APPLE80211_IOC_WCL_FW_HOT_CHANNELS" not in cpp
                and "fNetIf = new AirportItlwmSkywalkInterface;" in v2
            ),
            "local_null_guard_is_retained_as_safety_boundary": (
                "if (data == nullptr)" in getter
                and "return kIOReturnBadArgumentTahoe;" in getter
            ),
            "nonnull_getter_fails_closed_without_output": (
                "(void)data;" in getter
                and "return kIOReturnUnsupported;" in getter
                and all(
                    token not in getter
                    for token in (
                        "memset",
                        "memcpy",
                        "reinterpret_cast",
                        "kIOReturnSuccess",
                    )
                )
            ),
            "no_local_net_adapter_hot_channel_backend_is_introduced": all(
                not source_contains(token)
                for token in (
                    "getFWHotChannels(",
                    "roam_channels_in_hotlist",
                )
            ),
            "mimo_section_delimiter_is_preserved": (
                "getWCL_FW_HOT_CHANNELS(apple80211_fw_hot_channels *data)" in cpp
                and "getWCL_FW_HOT_CHANNELS" in
                (ROOT / "scripts/mimo_status_quarantine_report.py").read_text(encoding="utf-8")
            ),
            "historical_state_backed_classification_is_superseded": (
                "### 2026-07-15 correction: `WCL_FW_HOT_CHANNELS` is a NetAdapter no-producer quarantine"
                in signal_audit
                and "state-backed telemetry classification for `getWCL_FW_HOT_CHANNELS` is\n"
                "superseded" in signal_audit
                and "getWCL_FW_HOT_CHANNELS(...)" not in closed_zone
                and "getWCL_FW_HOT_CHANNELS" not in bucket_zone
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
        raise ValueError("WCL FW hot channels no-producer checks failed: " + ", ".join(failed))
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
        print(f"WCL FW hot channels no-producer validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
