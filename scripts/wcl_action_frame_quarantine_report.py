#!/usr/bin/env python3
"""Generate and verify WCL action-frame false-success quarantine evidence."""

import argparse
import json
import sys
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT = PROJECT_ROOT / "evidence/state/wcl_action_frame_quarantine_report.json"
REFERENCE_NOTE = (
    PROJECT_ROOT / "docs/reference/CR-479-wcl-action-frame-quarantine-20260712.md"
)
OWNERS = PROJECT_ROOT / "AirportItlwm/TahoeOwners.hpp"
COMMANDER = PROJECT_ROOT / "AirportItlwm/TahoeCommander.hpp"
COMMANDER_V2 = PROJECT_ROOT / "AirportItlwm/TahoeCommanderV2.hpp"
SKYWALK = PROJECT_ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"


def section(source, begin, end):
    start = source.index(begin)
    finish = source.index(end, start)
    return source[start:finish]


def build_report():
    owners = OWNERS.read_text(encoding="utf-8")
    commander = COMMANDER.read_text(encoding="utf-8")
    commander_v2 = COMMANDER_V2.read_text(encoding="utf-8")
    skywalk = SKYWALK.read_text(encoding="utf-8")
    note = REFERENCE_NOTE.read_text(encoding="utf-8")

    owner = section(owners, "class TahoeActionFrameOwner", "class TahoeRangingOwner")
    generic = section(
        commander, "IOReturn runSetWCLActionFrame", "IOReturn runSetRangingAuthenticate"
    )
    v2 = section(
        commander_v2, "IOReturn runSetWCLActionFrame", "IOReturn runSetRangingAuthenticate"
    )
    skywalk_setter = section(
        skywalk,
        "setWCL_ACTION_FRAME(apple80211_wcl_action_frame *data)",
        "setWCL_ROAM_USER_CACHE",
    )

    return {
        "schema": "itlwm-wcl-action-frame-quarantine-v1",
        "source_base_revision": "e4ab286c593717a7874d9ad805a6d0135565d21d",
        "reference": {
            "core_owner": "AppleBCMWLANCore::setWCL_ACTION_FRAME",
            "v1": "AppleBCMWLANNetAdapter::sendActionFrame",
            "v2": "AppleBCMWLANNetAdapter::sendActionFrameV2",
            "transport": "actframe",
        },
        "local": {
            "backend_action_frame_injector": False,
            "selector": 620,
            "owner_false_success": False,
            "generic_false_success": False,
        },
        "checks": {
            "reference_note": all(
                token in note
                for token in (
                    "AppleBCMWLANCore::setWCL_ACTION_FRAME",
                    "AppleBCMWLANNetAdapter::sendActionFrame",
                    "AppleBCMWLANNetAdapter::sendActionFrameV2",
                    "actframe",
                    "TahoeErrorMap::kAppleUnsupported",
                )
            ),
            "owner_drops_without_mutation": (
                "buildActionFrame(data, firmwareGeneration, &payload)" in owner
                and "return TahoeErrorMap::kAppleUnsupported;" in owner
                and "registry->actionFrame." not in owner
                and "completeSync(" not in owner
            ),
            "generic_commander_drops_without_mutation": (
                "buildActionFrame(data, firmwareGeneration, &payload)" in generic
                and "return TahoeErrorMap::kAppleUnsupported;" in generic
                and "registry->actionFrame." not in generic
                and "asyncContext->completed" not in generic
            ),
            "active_v2_propagates_owner_failure": (
                "actionFrameOwner.apply(data, firmwareGeneration, asyncContext)" in v2
                and "if (rc != kIOReturnSuccess)" in v2
                and "return rc;" in v2
            ),
            "skywalk_propagates_before_cache": (
                "runSetWCLActionFrame(" in skywalk_setter
                and "if (rc != kIOReturnSuccess)" in skywalk_setter
                and skywalk_setter.index("if (rc != kIOReturnSuccess)")
                < skywalk_setter.index("cachedLastActionFrameCategory")
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
        raise ValueError("WCL action-frame quarantine checks failed: " + ", ".join(failed))
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
        print(f"WCL action-frame quarantine validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
