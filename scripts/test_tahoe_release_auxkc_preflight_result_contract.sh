#!/usr/bin/env bash
# Contract for the credential-free private AuxKC preflight of release 8fefad9.
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
EVIDENCE="$ROOT/evidence/runtime/tahoe_lab_8fefad9_auxkc_preflight.json"
DOCUMENT="$ROOT/analysis/TAHOE_LAB_8FEFAD9_AUXKC_PREFLIGHT_2026-07-20.md"
HELPER="$ROOT/scripts/tahoe_auxkc_admission_preflight.sh"

for path in "$EVIDENCE" "$DOCUMENT" "$HELPER"; do
    [ -f "$path" ] || {
        echo "FAIL: missing release AuxKC preflight input: $path" >&2
        exit 1
    }
done

python3 - "$EVIDENCE" "$DOCUMENT" "$HELPER" <<'PY'
from __future__ import annotations

import hashlib
import json
import sys
from pathlib import Path

evidence_path = Path(sys.argv[1])
document_path = Path(sys.argv[2])
helper_path = Path(sys.argv[3])
evidence = json.loads(evidence_path.read_text())
document = document_path.read_text()


def fail(message: str) -> None:
    raise SystemExit(f"Tahoe release AuxKC preflight contract: {message}")


def require(text: str, token: str, label: str) -> None:
    if token not in text:
        fail(f"missing {label}: {token}")


if evidence.get("schema_version") != "itlwm-tahoe-release-auxkc-preflight/v1":
    fail("unexpected evidence schema")

candidate = evidence.get("candidate", {})
expected_candidate = {
    "release_tag": "v2.4.0-alpha-8fefad9",
    "source_commit": "8fefad9fc07a65f1cea21f901a1336913f1fa99e",
    "archive_sha256": "1d810192b993d289416102f8243056fdb965e4187276efd15e45ab3c124c4801",
    "binary_sha256": "dd7acfc45a534fc22333f1811a359c8e0f6ec2bf7b583bc349c0fbd6617543b9",
    "macho_uuid": "EC0C3E24-FA7A-354C-BC28-B4A005073309",
}
for key, value in expected_candidate.items():
    if candidate.get(key) != value:
        fail(f"candidate {key} differs from the exact release result")
if candidate.get("archive_members") != [
        "AirportItlwm.kext/Contents/Info.plist",
        "AirportItlwm.kext/Contents/MacOS/AirportItlwm",
]:
    fail("release asset is not recorded as a complete kext bundle")

preflight = evidence.get("preflight", {})
if preflight.get("helper_sha256") != hashlib.sha256(helper_path.read_bytes()).hexdigest():
    fail("evidence is not bound to the current private-preflight helper")
if {key: preflight.get(key) for key in (
        "kmutil_create_exit", "private_auxkc_inspect_exit",
        "private_auxkc_bootkc_inspect_exit", "auxkc_members")} != {
        "kmutil_create_exit": 0, "private_auxkc_inspect_exit": 0,
        "private_auxkc_bootkc_inspect_exit": 0, "auxkc_members": 5}:
    fail("private materialization did not preserve the exact admission result")
if preflight.get("private_admission_result") != "PASS" or \
        preflight.get("canonical_postflight") != "PASS" or \
        preflight.get("canonical_mutation") != "none":
    fail("private preflight does not prove an unchanged canonical state")
if preflight.get("candidate_copy_sha256_matches_release") is not True or \
        preflight.get("non_airport_members_unchanged") is not True:
    fail("candidate copy or companion-member identity is not preserved")
if preflight.get("candidate_source_codesign_verify_exit") != 1 or \
        preflight.get("candidate_private_codesign_verify_exit") != 1:
    fail("record lost the non-claiming debug codesign observation")

scope = evidence.get("scope", {})
if scope.get("lab_guest_only") is not True:
    fail("test scope is not lab-only")
if any(scope.get(key) is not False for key in (
        "lab_guest_rebooted", "physical_host_rebooted", "remote_10_90_10_22_touched")):
    fail("record claims an out-of-scope reboot or host action")
for key, value in evidence.get("non_claims", {}).items():
    if value is not False:
        fail(f"functional or activation claim escaped boundary: {key}")
for key in ("raw_capture_committed", "secret_material_committed", "ssid_or_bssid_committed"):
    if evidence.get("commit_safety", {}).get(key) is not False:
        fail(f"unsafe committed material flag: {key}")
verdict = evidence.get("verdict", {})
if verdict != {
        "exact_release_bundle_materialized_privately": True,
        "private_auxkc_admission_passed": True,
        "release_contains_kext": True}:
    fail("preflight verdict is incomplete or overclaims functionality")

for token in (
        expected_candidate["release_tag"], expected_candidate["archive_sha256"],
        expected_candidate["binary_sha256"], expected_candidate["macho_uuid"],
        preflight["helper_sha256"], "not a Wi-Fi functionality",
        "No kext was installed, loaded", "code-signing"):
    require(document, token, "versioned preflight document")
for forbidden in ("passphrase", "192.168."):
    if forbidden.lower() in evidence_path.read_text().lower() or \
            forbidden.lower() in document.lower():
        fail(f"credential or network identifier leaked: {forbidden}")

print("PASS: Tahoe release AuxKC preflight result contract")
PY
