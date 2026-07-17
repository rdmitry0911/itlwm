#!/usr/bin/env python3
"""Generate and verify the LQM slow-WiFi producer quarantine evidence."""

import argparse
import json
import sys
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT = PROJECT_ROOT / "evidence/state/lqm_slow_wifi_producer_report.json"
REFERENCE_NOTE = PROJECT_ROOT / "docs/reference/CR-479-lqm-slow-wifi-producer-closure-20260711.md"
CONTRACTS = PROJECT_ROOT / "AirportItlwm/TahoeQosDynsarContracts.hpp"
REGISTRY = PROJECT_ROOT / "AirportItlwm/TahoeOwnerRegistry.hpp"
SKYWALK = PROJECT_ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
TESTS = PROJECT_ROOT / "tests/tahoe_payload_builders_test.cpp"

REFERENCE_TOKENS = [
    "0xffffff80015638e1",
    "0x1010001",
    "0xffffff8001616a46",
    "0xffffff8001616ad0",
    "SHR DL,0x2",
    "0xffffff8001522d90 -> 0xffffff8001616a46",
    "EACC3D4B-4773-30D1-83FF-CB21BE2A7DF8",
    "17DA2F0A-6A4D-33C6-BAD4-3E368575F3C8",
    "239/240",
    "type-3 `0xe00002c7`",
    "3758097095 (0xe00002c7)",
    "cap+0xb36 & 0x08",
]
SOURCE_TOKENS = {
    "contracts": ["kSlowWifiFeatureEnabledOffset = 0x7569"],
    "registry": [
        "uint8_t slowWifiFeatureEnabled = 0",
        "bool isSlowWifiFeatureEnabled() const",
    ],
    "skywalk": [
        "setOS_FEATURE_FLAGS(apple80211_feature_flags *data)",
        "constexpr uint64_t kSlowWifiFeatureFlag = 1ULL << 2",
        "if ((flags & kSlowWifiFeatureFlag) != 0)",
        "return kIOReturnUnsupported;",
        "cachedOSFeatureFlags = flags",
        "getSLOW_WIFI_FEATURE_ENABLED(apple80211_slow_wifi_feature_enabled *data)",
        "isSlowWifiFeatureEnabled()",
    ],
    "tests": [
        "slow-wifi enabled carrier is core-private +0x7569",
        "QoS/DynSAR owner starts with slow-wifi disabled",
        "QoS/DynSAR owner reset restores retained local zero carriers",
    ],
}
FORBIDDEN_LOCAL_TOKENS = [
    "syncSlowWifiFeatureEnabledFromOSFeatureFlags",
    "slowWifiFeatureEnabledFromOSFeatureFlags",
    "kSlowWifiOSFeatureFlagsBit",
    "kSlowWifiInitialEnabled",
]


def missing_tokens(path, tokens):
    text = path.read_text(encoding="utf-8")
    return [token for token in tokens if token not in text]


def present_tokens(paths, tokens):
    matches = []
    for path in paths:
        text = path.read_text(encoding="utf-8")
        matches.extend(token for token in tokens if token in text)
    return sorted(set(matches))


def build_report():
    missing = {
        "reference": missing_tokens(REFERENCE_NOTE, REFERENCE_TOKENS),
        "contracts": missing_tokens(CONTRACTS, SOURCE_TOKENS["contracts"]),
        "registry": missing_tokens(REGISTRY, SOURCE_TOKENS["registry"]),
        "skywalk": missing_tokens(SKYWALK, SOURCE_TOKENS["skywalk"]),
        "tests": missing_tokens(TESTS, SOURCE_TOKENS["tests"]),
    }
    forbidden = present_tokens([CONTRACTS, REGISTRY, SKYWALK], FORBIDDEN_LOCAL_TOKENS)
    return {
        "schema": "itlwm-lqm-slow-wifi-producer-quarantine-v2",
        "source_base_revision": "b27f276eb98be416dfd5b445a785d9d2fc98ff4f",
        "reference": {
            "build": "macOS 26.2 (25C56)",
            "initial_store": {
                "address": "0xffffff80015638e1",
                "value": "0x01010001",
                "slow_wifi_low_byte": 1,
            },
            "runtime_producer": {
                "entry": "0xffffff8001616a46",
                "store": "0xffffff8001616ad0",
                "input_bit": 2,
                "caller_thunk": "0xffffff8001522d90",
            },
        },
        "runtime_falsification": {
            "startup_copy_uuid": "EACC3D4B-4773-30D1-83FF-CB21BE2A7DF8",
            "producer_map_uuid": "17DA2F0A-6A4D-33C6-BAD4-3E368575F3C8",
            "queue_error": "IO80211QueueCall type 3 / 0xe00002c7",
            "ping_result": "239/240",
            "fbt_first_gate": {
                "function": "IO80211InfraInterface::createLinkQualityMonitor",
                "enabled_return": "0xe00002c7",
                "peer_monitor_entered": False,
                "lqm_init_entered": False,
            },
        },
        "local": {
            "raw_feature_word_cached_when_slow_wifi_bit_clear": True,
            "slow_wifi_bit2_quarantined_before_cache": True,
            "slow_wifi_null_owner_initial_enabled": 0,
            "producer_to_getter_mapping_implemented": False,
            "reason": "dependent LQM capability and PeerMonitor owners are unrecovered; bit 2 is rejected before cache",
        },
        "checks": {
            **{key: not value for key, value in missing.items()},
            "no_unsafe_local_mapping": not forbidden,
        },
        "missing_tokens": missing,
        "forbidden_local_tokens": forbidden,
    }


def validate(report):
    missing = [
        f"{scope}: {token}"
        for scope, tokens in report["missing_tokens"].items()
        for token in tokens
    ]
    if missing:
        raise ValueError("missing quarantine tokens: " + "; ".join(missing))
    if report["forbidden_local_tokens"]:
        raise ValueError(
            "unsafe slow-WiFi producer mapping present: "
            + ", ".join(report["forbidden_local_tokens"])
        )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--write", action="store_true", help="write the generated report")
    parser.add_argument("--check", action="store_true", help="compare against the checked-in report")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()
    if args.write == args.check:
        parser.error("select exactly one of --write or --check")

    report = build_report()
    validate(report)
    rendered = json.dumps(report, indent=2, sort_keys=True) + "\n"

    if args.write:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered, encoding="utf-8")
    else:
        if not args.output.exists():
            raise ValueError(f"missing checked-in report: {args.output}")
        if args.output.read_text(encoding="utf-8") != rendered:
            raise ValueError("checked-in report differs; rerun with --write")

    print(rendered, end="")


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"LQM slow-WiFi producer quarantine validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
