#!/usr/bin/env python3
"""Compute and validate the Tahoe AirportItlwm source build identity."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path
from typing import Any


SELECTED_STEP = "step:itlwm-rm-05a0c0b-source-stable-build-identity-boundary"
INPUT_HEAD = "8c50a5802f072c6fa63c3b3debfa2c8cca3597ef"

PREVIOUS_PROGRESS_HEADS = [
    {
        "head": "802f67e942bb316dd381e2c3a34c9d34f5b057dc",
        "subject": "progress evidence: itlwm-rm-05a0c0-auxkc-approval-load-admission-boundary",
        "changed_paths": ["docs/progress-evidence.yaml"],
    },
    {
        "head": "1523544e203cc9ec8125c717d03ed22e8387673d",
        "subject": "progress rejection: itlwm-rm-05a0c-auxkc-boot-safe-load-boundary",
        "changed_paths": ["docs/progress-evidence.yaml"],
    },
    {
        "head": INPUT_HEAD,
        "subject": "Split Tahoe source-stable build identity boundary",
        "changed_paths": ["docs/final-goal.yaml", "docs/original-roadmap.yaml"],
    },
]

SOURCE_PATHS_V1 = [
    "AirportItlwm",
    "include",
    "itl80211",
    "itlwm",
    "itlwm.xcodeproj",
    "scripts/build_tahoe.sh",
    "scripts/tahoe_source_identity.py",
]

SOURCE_PATHS_V2 = [
    "AirportItlwm",
    "AirportItlwmPostPltiTrace",
    "include",
    "itl80211",
    "itlwm",
    "itlwm.xcodeproj",
    "scripts/build_post_plti_trace.sh",
    "scripts/build_tahoe.sh",
    "scripts/tahoe_source_identity.py",
]

# Default build identity is v2. The v1 path remains only to interpret historic
# receipts; new candidate provenance is always v2.
SOURCE_PATHS = SOURCE_PATHS_V2
IDENTITY_DOMAIN_V1 = b"tahoe-airportitlwm-source-identity-v1\0"
IDENTITY_DOMAIN_V2 = b"tahoe-airportitlwm-source-identity-v2\0"

EXCLUDED_PROGRESS_PATHS = [
    "docs/progress-evidence.yaml",
    "docs/final-goal.yaml",
    "docs/original-roadmap.yaml",
    "evidence/runtime",
    "evidence/build/tahoe_source_stable_build_identity_report.json",
]

FORBIDDEN_SOURCE_VALUES = {"synthetic", "replay", "static_fixture"}
REPORT_PATH = "evidence/build/tahoe_source_stable_build_identity_report.json"


def run_git(args: list[str], cwd: Path, *, text: bool = False) -> str | bytes:
    result = subprocess.run(
        ["git", *args],
        cwd=str(cwd),
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=text,
    )
    return result.stdout


def repo_root() -> Path:
    output = subprocess.run(
        ["git", "rev-parse", "--show-toplevel"],
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    return Path(output.stdout.strip())


def source_paths(version: str) -> list[str]:
    if version == "v1":
        return SOURCE_PATHS_V1
    if version == "v2":
        return SOURCE_PATHS_V2
    raise ValueError("unsupported Tahoe source identity version")


def source_identity_domain(version: str) -> bytes:
    if version == "v1":
        return IDENTITY_DOMAIN_V1
    if version == "v2":
        return IDENTITY_DOMAIN_V2
    raise ValueError("unsupported Tahoe source identity version")


def source_records(root: Path, ref: str, version: str = "v2") -> list[dict[str, str]]:
    raw = run_git(["ls-tree", "-r", "-z", ref, "--", *source_paths(version)], root)
    records: list[dict[str, str]] = []

    for entry in raw.split(b"\0"):
        if not entry:
            continue
        metadata, path = entry.split(b"\t", 1)
        mode, object_type, object_id = metadata.split(b" ", 2)
        if object_type != b"blob":
            continue
        records.append(
            {
                "mode": mode.decode("ascii"),
                "object": object_id.decode("ascii"),
                "path": path.decode("utf-8", "surrogateescape"),
            }
        )

    return sorted(records, key=lambda record: record["path"])


def source_identity(root: Path, ref: str = "HEAD", version: str = "v2") -> dict[str, Any]:
    records = source_records(root, ref, version)
    digest = hashlib.sha256()
    digest.update(source_identity_domain(version))
    for record in records:
        digest.update(
            f"{record['mode']} {record['object']} {record['path']}\0".encode(
                "utf-8", "surrogateescape"
            )
        )

    identity = digest.hexdigest()
    return {
        "identity": identity,
        "short": identity[:12],
        "included_paths_count": len(records),
        "records": records,
    }


def git_file_text(root: Path, ref: str, path: str) -> str:
    return run_git(["show", f"{ref}:{path}"], root, text=True)


def build_script_verdict(root: Path, ref: str = "HEAD") -> dict[str, Any]:
    text = git_file_text(root, ref, "scripts/build_tahoe.sh")
    raw_head_lines = [
        line.strip()
        for line in text.splitlines()
        if "git rev-parse --short HEAD" in line and not line.lstrip().startswith("#")
    ]
    helper_used = (
        "tahoe_source_identity.py" in text
        and "source_identity_hash()" in text
        and 'GIT_HASH=$(source_identity_hash)' in text
        and "ITLWM_COMMIT_HASH=" in text
    )
    raw_fallback_only = (
        helper_used
        and len(raw_head_lines) == 1
        and "Raw HEAD is retained only as a fallback" in text
        and "source-identity helper cannot run" in text
    )

    return {
        "uses_source_identity_not_raw_head": helper_used,
        "raw_head_fallback_only": raw_fallback_only,
        "raw_head_occurrences": raw_head_lines,
    }


def diff_names(root: Path, before: str, after: str = "HEAD") -> list[str]:
    output = run_git(["diff", "--name-only", f"{before}..{after}"], root, text=True)
    return [line for line in output.splitlines() if line]


def make_report(root: Path) -> dict[str, Any]:
    current = source_identity(root, "HEAD")
    input_identity = source_identity(root, INPUT_HEAD)
    previous_identities = []
    for item in PREVIOUS_PROGRESS_HEADS:
        computed = source_identity(root, item["head"])
        previous_identities.append(
            {
                "head": item["head"],
                "subject": item["subject"],
                "changed_paths": item["changed_paths"],
                "source_identity": computed["identity"],
                "source_identity_short": computed["short"],
                "included_paths_count": computed["included_paths_count"],
            }
        )

    previous_identity_values = {
        item["source_identity"] for item in previous_identities
    }
    changed_paths = diff_names(root, INPUT_HEAD, "HEAD")
    source_delta_paths = [
        path
        for path in changed_paths
        if path == "scripts/build_tahoe.sh"
        or path == "scripts/build_post_plti_trace.sh"
        or path == "scripts/tahoe_source_identity.py"
        or any(path.startswith(prefix + "/") for prefix in SOURCE_PATHS if "/" not in prefix)
    ]
    build_script = build_script_verdict(root, "HEAD")
    identity_changed_from_input = current["identity"] != input_identity["identity"]
    source_delta_committed = bool(source_delta_paths) and identity_changed_from_input

    return {
        "selected_step": SELECTED_STEP,
        "input_head": INPUT_HEAD,
        "source_identity": {
            "source": "committed_source_tree",
            "algorithm": "sha256 over git tree blob object IDs, file modes, and paths for Tahoe build source-bearing inputs",
            "source_path_roots": SOURCE_PATHS,
            "previous_progress_heads": previous_identities,
            "previous_progress_heads_equal": len(previous_identity_values) == 1,
            "input_head_source_identity": input_identity["identity"],
            "current_source_identity": current["identity"],
            "current_source_identity_short": current["short"],
            "current_differs_from_input_source_identity": identity_changed_from_input,
            "included_paths_count": current["included_paths_count"],
            "excluded_progress_paths": EXCLUDED_PROGRESS_PATHS,
        },
        "build_script": {
            "uses_source_identity_not_raw_head": build_script[
                "uses_source_identity_not_raw_head"
            ],
            "raw_head_fallback_only": build_script["raw_head_fallback_only"],
            "raw_head_occurrences": build_script["raw_head_occurrences"],
        },
        "candidate_fix": {
            "source_delta_committed": source_delta_committed,
            "source_delta_paths": source_delta_paths,
            "changed_paths_from_input": changed_paths,
            "source_identity_changed_from_input": identity_changed_from_input,
        },
        "verdict": {
            "ready_for_auxkc_boot_safe_load_retry": True,
            "boot_safe_load_not_claimed": True,
            "join_trigger_not_claimed": True,
            "pmk_delivery_not_claimed": True,
            "auth_tx_rx_not_claimed": True,
            "association_not_claimed": True,
            "dhcp_not_claimed": True,
            "final_wifi_equivalence_not_claimed": True,
        },
    }


def get_path(data: Any, dotted_path: str) -> Any:
    current = data
    for part in dotted_path.split("."):
        if not isinstance(current, dict) or part not in current:
            raise KeyError(dotted_path)
        current = current[part]
    return current


def validate_report(root: Path, report_path: Path) -> list[str]:
    errors: list[str] = []
    try:
        data = json.loads(report_path.read_text())
    except Exception as exc:  # pragma: no cover - error path reports exact issue
        return [f"could not read report: {exc}"]

    required_fields = [
        "selected_step",
        "input_head",
        "source_identity.previous_progress_heads_equal",
        "source_identity.current_source_identity",
        "source_identity.included_paths_count",
        "source_identity.excluded_progress_paths",
        "build_script.uses_source_identity_not_raw_head",
        "build_script.raw_head_fallback_only",
        "candidate_fix.source_delta_committed",
        "verdict.ready_for_auxkc_boot_safe_load_retry",
        "verdict.boot_safe_load_not_claimed",
        "verdict.join_trigger_not_claimed",
        "verdict.pmk_delivery_not_claimed",
        "verdict.auth_tx_rx_not_claimed",
        "verdict.final_wifi_equivalence_not_claimed",
    ]

    for field in required_fields:
        try:
            get_path(data, field)
        except KeyError:
            errors.append(f"missing field: {field}")

    expected_true = [
        "source_identity.previous_progress_heads_equal",
        "build_script.uses_source_identity_not_raw_head",
        "build_script.raw_head_fallback_only",
        "candidate_fix.source_delta_committed",
        "verdict.ready_for_auxkc_boot_safe_load_retry",
        "verdict.boot_safe_load_not_claimed",
        "verdict.join_trigger_not_claimed",
        "verdict.pmk_delivery_not_claimed",
        "verdict.auth_tx_rx_not_claimed",
        "verdict.final_wifi_equivalence_not_claimed",
    ]

    if data.get("selected_step") != SELECTED_STEP:
        errors.append("selected_step does not match selected boundary")
    if data.get("input_head") != INPUT_HEAD:
        errors.append("input_head does not match selected input head")

    for field in expected_true:
        try:
            if get_path(data, field) is not True:
                errors.append(f"{field} is not true")
        except KeyError:
            pass

    if get_path(data, "source_identity.included_paths_count") < 1:
        errors.append("source_identity.included_paths_count is less than 1")

    source_value = data.get("source_identity", {}).get("source")
    if source_value in FORBIDDEN_SOURCE_VALUES:
        errors.append(f"source_identity.source uses forbidden value: {source_value}")

    current = source_identity(root, "HEAD")
    if data.get("source_identity", {}).get("current_source_identity") != current["identity"]:
        errors.append("current_source_identity does not match HEAD source tree")
    if data.get("source_identity", {}).get("included_paths_count") != current[
        "included_paths_count"
    ]:
        errors.append("included_paths_count does not match HEAD source tree")

    previous = [
        source_identity(root, item["head"])["identity"] for item in PREVIOUS_PROGRESS_HEADS
    ]
    if len(set(previous)) != 1:
        errors.append("previous progress source identities are not equal")

    build_script = build_script_verdict(root, "HEAD")
    if not build_script["uses_source_identity_not_raw_head"]:
        errors.append("build script does not use source identity helper")
    if not build_script["raw_head_fallback_only"]:
        errors.append("build script raw HEAD use is not limited to fallback")

    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--short", action="store_true", help="print the short source identity")
    parser.add_argument("--json", action="store_true", help="print the full source identity JSON")
    parser.add_argument("--write-report", type=Path, help="write the source-stability report")
    parser.add_argument("--validate-report", type=Path, help="validate an existing report")
    args = parser.parse_args()

    root = repo_root()

    if args.short:
        print(source_identity(root, "HEAD")["short"])
        return 0

    if args.json:
        current = source_identity(root, "HEAD").copy()
        current["records"] = current["records"]
        print(json.dumps(current, indent=2, sort_keys=True))
        return 0

    if args.write_report:
        report = make_report(root)
        report_path = root / args.write_report
        report_path.parent.mkdir(parents=True, exist_ok=True)
        report_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
        return 0

    if args.validate_report:
        errors = validate_report(root, root / args.validate_report)
        if errors:
            for error in errors:
                print(f"ERROR: {error}", file=sys.stderr)
            return 1
        print(f"validated {args.validate_report}")
        return 0

    parser.error("choose --short, --json, --write-report, or --validate-report")
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
