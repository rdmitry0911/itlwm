#!/usr/bin/env python3
"""Generate and verify ranging-authentication null-owner evidence."""

import argparse
import json
import sys
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT = PROJECT_ROOT / "evidence/state/ranging_auth_quarantine_report.json"
REFERENCE_NOTE = (
    PROJECT_ROOT / "docs/reference/CR-479-ranging-authentication-quarantine-20260712.md"
)
OWNERS = PROJECT_ROOT / "AirportItlwm/TahoeOwners.hpp"
COMMANDER = PROJECT_ROOT / "AirportItlwm/TahoeCommander.hpp"
COMMANDER_V2 = PROJECT_ROOT / "AirportItlwm/TahoeCommanderV2.hpp"
SKYWALK = PROJECT_ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
SOURCE_ROOTS = (PROJECT_ROOT / "AirportItlwm", PROJECT_ROOT / "itlwm")
PROXIMITY_TRANSPORT_TOKENS = ('"proxd"', '"wsec"', '"ptk_start"')


def section(source, begin, end):
    start = source.index(begin)
    finish = source.index(end, start)
    return source[start:finish]


def source_contains(token):
    return any(
        token in path.read_text(encoding="utf-8", errors="ignore")
        for root in SOURCE_ROOTS
        for path in root.rglob("*")
        if path.is_file()
    )


def build_report():
    owners = OWNERS.read_text(encoding="utf-8")
    commander = COMMANDER.read_text(encoding="utf-8")
    commander_v2 = COMMANDER_V2.read_text(encoding="utf-8")
    skywalk = SKYWALK.read_text(encoding="utf-8")
    note = REFERENCE_NOTE.read_text(encoding="utf-8")

    owner = section(owners, "class TahoeRangingOwner", "#endif")
    generic = section(commander, "IOReturn runSetRangingAuthenticate", "private:")
    v2 = section(commander_v2, "IOReturn runSetRangingAuthenticate", "private:")
    skywalk_setter = section(
        skywalk,
        "setRANGING_AUTHENTICATE(apple80211_ranging_authenticate_request_t *data)",
        "setPM_MODE",
    )

    return {
        "schema": "itlwm-ranging-auth-quarantine-v1",
        "source_base_revision": "758a35b586d52085f7a9011988baaa49d68511f2",
        "reference": {
            "core_preflight": "FUN_ffffff80015eaf92",
            "core_fileset": "AppleBCMWLANCoreMac",
            "missing_owner_return": "0xe0000001",
            "owner_offset": "0x2c28",
            "transports": [token.strip('"') for token in PROXIMITY_TRANSPORT_TOKENS],
        },
        "local": {
            "backend_proximity_transport": False,
            "fabricated_proximity_owner": False,
            "generic_false_success": False,
            "owner_false_success": False,
            "selector": 567,
        },
        "checks": {
            "reference_note": all(
                token in note
                for token in (
                    "FUN_ffffff80015eaf92",
                    "AppleBCMWLANCoreMac",
                    "0xe0000001",
                    "+0x2c28",
                    "proxd",
                    "wsec",
                    "ptk_start",
                )
            ),
            "owner_fails_without_mutation": (
                "buildRangingAuthenticate(data, 0, &payload)" in owner
                and "return TahoeErrorMap::kAppleRangingInvalid;" in owner
                and "registry->ranging." not in owner
                and "completeSync(" not in owner
                and "asyncContext" not in owner
            ),
            "generic_fails_without_mutation": (
                "buildRangingAuthenticate(data, 0, &payload)" in generic
                and "return TahoeErrorMap::kAppleRangingInvalid;" in generic
                and "registry->ranging." not in generic
                and "asyncContext->completed" not in generic
                and "asyncContext" not in generic
            ),
            "v2_only_propagates_owner_result": (
                "return rangingOwner.apply(data, proximityOwnerId, asyncContext);" in v2
                and "dispatchIOVarSet(567" not in v2
                and "dispatchVirtualIOCtlSet(567" not in v2
                and "dispatchIssueCommand(567" not in v2
                and "dispatchHiddenCallback(567" not in v2
                and "registry->ranging" not in v2
            ),
            "skywalk_uses_no_fabricated_owner": (
                "kLocalTahoeProximityOwnerId" not in skywalk_setter
                and "data, 0, &asyncContext" in skywalk_setter
            ),
            "intel_source_has_no_proximity_transport": not any(
                source_contains(token) for token in PROXIMITY_TRANSPORT_TOKENS
            ),
        },
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--write", action="store_true", help="write the generated report")
    parser.add_argument("--check", action="store_true", help="compare against the checked-in report")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()
    if args.write == args.check:
        parser.error("select exactly one of --write or --check")

    report = build_report()
    failed = [name for name, passed in report["checks"].items() if not passed]
    if failed:
        raise ValueError("ranging-auth quarantine checks failed: " + ", ".join(failed))
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
        print(f"ranging-auth quarantine validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
