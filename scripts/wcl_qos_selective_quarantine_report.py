#!/usr/bin/env python3
"""Generate and verify WCL QoS selective quarantine evidence."""

import argparse
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/wcl_qos_selective_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-479-wcl-qos-selective-quarantine-20260714.md"
SIGNAL_AUDIT = ROOT / "docs/tahoe_signal_chain_audit.md"
INVENTORY = ROOT / "docs/tahoe_discrepancy_inventory.md"
CPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
HPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.hpp"
SOURCE_ROOTS = (
    ROOT / "AirportItlwm",
    ROOT / "include",
    ROOT / "itl80211",
    ROOT / "itlwm",
)
MISSING_OWNER_FLAGS = 0x6D
LOCAL_OWNER_FLAGS = 0x12
UNKNOWN_NOOP_FLAG = 0x80


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


def local_disposition(flags):
    return "unsupported" if flags & MISSING_OWNER_FLAGS else "success"


def report():
    cpp = CPP.read_text(encoding="utf-8")
    hpp = HPP.read_text(encoding="utf-8")
    note = NOTE.read_text(encoding="utf-8")
    signal_audit = SIGNAL_AUDIT.read_text(encoding="utf-8")
    inventory = " ".join(INVENTORY.read_text(encoding="utf-8").split())
    setter = section(
        cpp,
        "setWCL_QOS_PARAMS(apple80211_wcl_qos_params *data)",
        "setWCL_LINK_UP_DONE",
    )
    return {
        "schema": "itlwm-wcl-qos-selective-quarantine-v1",
        "source_base_revision": "90574110c0f5ab7fdf05fae28e3647a48034ffac",
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "infra_wrapper": "0x100018dbc",
            "core_setter": "0x1001e867a",
            "net_adapter_offset": "0x15e0",
            "net_adapter_setter": "0x10019df72",
            "flag_offset": "0x17",
            "long_retry": "0x100012bb6",
            "long_retry_ioctl": "0x22",
            "lifetime": "0x100015f06",
            "realtime_policy": "0x10013a06a",
            "realtime_state_offset": "0x78f4",
            "mlo": "0x10004394a",
            "null_status": "0xe00002bc",
        },
        "local": {
            "missing_owner_flag_mask": "0x6d",
            "local_owner_flag_mask": "0x12",
            "unknown_noop_flag": "0x80",
            "request_false_success": False,
            "valid_input_return_is_apple_parity": False,
        },
        "checks": {
            "reference_note": all(
                token in note
                for token in (
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "0x100018dbc",
                    "0x1001e867a",
                    "`0xe00002bc`",
                    "`+0x15e0`",
                    "0x10019df72",
                    "`+0x17`",
                    "0x100012bb6",
                    "`0x22`",
                    "0x100015f06",
                    "0x10013a06a",
                    "`+0x78f4`",
                    "0x10004394a",
                    "`0x80`",
                    "complete public carrier",
                )
            ),
            "missing_owner_mask_quarantined_before_mutation": all(
                token in setter
                for token in (
                    "constexpr uint8_t kMissingQosOwnerFlags = 0x6d;",
                    "if (qos == nullptr)",
                    "return kIOReturnBadArgumentTahoe;",
                    "const uint8_t flags = qos->flags;",
                    "if ((flags & kMissingQosOwnerFlags) != 0)",
                    "return kIOReturnUnsupported;",
                )
            )
            and setter.index("return kIOReturnUnsupported;") < setter.index("ic->ic_rtsthreshold")
            and setter.index("return kIOReturnUnsupported;") < setter.index("setPOWERSAVE(&pd);"),
            "local_rts_and_pm_paths_preserved": all(
                token in setter
                for token in (
                    "if ((flags & 0x02) != 0)",
                    "qos->rts_threshold",
                    "if ((flags & 0x10) != 0)",
                    "setPOWERSAVE(&pd);",
                    "return kIOReturnSuccess;",
                )
            ),
            "unknown_noop_mask_preserved": MISSING_OWNER_FLAGS == 0x6D
            and LOCAL_OWNER_FLAGS == 0x12
            and UNKNOWN_NOOP_FLAG == 0x80
            and (UNKNOWN_NOOP_FLAG & MISSING_OWNER_FLAGS) == 0
            and local_disposition(UNKNOWN_NOOP_FLAG) == "success"
            and local_disposition(LOCAL_OWNER_FLAGS | UNKNOWN_NOOP_FLAG) == "success"
            and local_disposition(0x01) == "unsupported"
            and local_disposition(0x20) == "unsupported"
            and local_disposition(0x40) == "unsupported",
            "pseudo_cache_removed": all(
                token not in cpp and token not in hpp
                for token in (
                    "cachedQosLongRetryLimit",
                    "cachedQosRtsThreshold",
                    "cachedQosLifetimeAc3",
                    "cachedQosLifetimeAc2",
                    "cachedQosFlags",
                )
            ),
            "scoped_missing_backends_absent": all(
                not source_contains(token)
                for token in (
                    "configureLongRetryLimit(",
                    "configureLifeTime(",
                    "configureMloFeatures(",
                    "setReatimeAppPoliciesInternal(",
                )
            ),
            "historical_claims_corrected": "Q10 correction: WCL QoS has a selective owner boundary"
            in signal_audit
            and "aggregate missing-owner mask `0x6d`" in signal_audit
            and "QoS is explicitly selective" in inventory,
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
        raise ValueError("WCL QoS selective quarantine checks failed: " + ", ".join(failed))
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
        print(f"WCL QoS selective quarantine validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
