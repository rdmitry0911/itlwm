#!/usr/bin/env bash
# Contract for the local-only, non-release Tahoe IWN lab candidate receipt.
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
RECEIPT="$ROOT/scripts/capture_tahoe_iwn_lab_candidate_receipt.py"

[ -x "$RECEIPT" ] || {
    echo "FAIL: missing executable Tahoe IWN lab candidate receipt tool" >&2
    exit 1
}

python3 "$RECEIPT" --self-test

python3 - "$RECEIPT" <<'PY'
from pathlib import Path
import sys


text = Path(sys.argv[1]).read_text(encoding="utf-8")


def require(needle: str, label: str) -> None:
    if needle not in text:
        raise SystemExit(f"FAIL: candidate receipt lacks {label}: {needle}")


def forbid(needle: str, label: str) -> None:
    if needle in text:
        raise SystemExit(f"FAIL: candidate receipt contains {label}: {needle}")


for needle, label in (
    ("itlwm-tahoe-iwn-lab-candidate-receipt/v1", "typed local-lab schema"),
    ("local-unpublished-iwn-lab-candidate", "non-release receipt kind"),
    ("iwn-software-pmf-lab", "fixed IWN lab profile"),
    ("Build/Debug/Tahoe-IwnSoftwarePmfLab/AirportItlwm.kext", "fixed staged profile path"),
    ("candidate source worktree is not clean", "clean committed HEAD gate"),
    ("source_head_stable_during_capture", "stable HEAD check"),
    ("artifact_stable_during_capture", "artifact TOCTOU check"),
    ("archive_sha256", "archive digest"),
    ("macho_uuid", "Mach-O UUID"),
    ("bundle_tree_sha256", "whole-bundle logical tree digest"),
    ("archive contains duplicate kext member", "duplicate-member rejection"),
    ("candidate_from_receipt", "typed future-verifier reader"),
    ("load_candidate_receipt", "receipt loader"),
    ("receipt output must be outside the source repository", "clean-tree preserving output rule"),
    ("release_tag_claimed\": False", "explicit non-release claim"),
    ("candidate_kext_installed\": False", "install non-claim"),
    ("candidate_kext_loaded\": False", "load non-claim"),
    ("association_tested\": False", "association non-claim"),
    ("data_transfer_tested\": False", "traffic non-claim"),
    ("StrictHostKeyChecking", "absence marker"),
):
    if label == "absence marker":
        forbid(needle, "guest SSH or host-key behaviour")
    else:
        require(needle, label)

for needle, label in (
    ("--release-tag", "release-tag argument"),
    ("--candidate-provenance", "release-provenance argument"),
    ("kmutil load", "kext loading"),
    ("kextload", "legacy kext loading"),
    ("kextutil", "kext utility loading"),
    ("sudo ", "privilege escalation"),
    ("networksetup -set", "network mutation"),
    ("route add", "route mutation"),
    ("ssh ", "guest connection"),
):
    forbid(needle, label)

print("PASS: Tahoe IWN lab candidate receipt contract")
PY
