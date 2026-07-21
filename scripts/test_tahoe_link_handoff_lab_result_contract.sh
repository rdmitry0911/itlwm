#!/usr/bin/env bash
# Contract for the release-bound Tahoe link-handoff lab record.
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
EVIDENCE="$ROOT/evidence/runtime/tahoe_lab_9b5d064_link_handoff.json"
IDENTITY="$ROOT/evidence/runtime/tahoe_lab_kext_identity_9b5d064.json"
DOCUMENT="$ROOT/analysis/TAHOE_LAB_9B5D064_LINK_HANDOFF_2026-07-20.md"

for path in "$EVIDENCE" "$IDENTITY" "$DOCUMENT" \
            "$ROOT/scripts/capture_tahoe_sae_layer.sh" \
            "$ROOT/scripts/evaluate_tahoe_sae_capture.py" \
            "$ROOT/scripts/evaluate_tahoe_link_handoff.py" \
            "$ROOT/scripts/tahoe_auxkc_admission_preflight.sh"; do
    [ -f "$path" ] || {
        echo "FAIL: missing release-bound link-handoff input: $path" >&2
        exit 1
    }
done

python3 "$ROOT/scripts/evaluate_tahoe_sae_capture.py" --self-test
python3 "$ROOT/scripts/evaluate_tahoe_link_handoff.py" --self-test

python3 - "$ROOT" <<'PY'
from __future__ import annotations

import hashlib
import json
import sys
from pathlib import Path

root = Path(sys.argv[1])
evidence_path = root / "evidence/runtime/tahoe_lab_9b5d064_link_handoff.json"
identity_path = root / "evidence/runtime/tahoe_lab_kext_identity_9b5d064.json"
document = (root / "analysis/TAHOE_LAB_9B5D064_LINK_HANDOFF_2026-07-20.md").read_text()
evidence = json.loads(evidence_path.read_text())
identity = json.loads(identity_path.read_text())

def fail(message: str) -> None:
    raise SystemExit(f"Tahoe release-bound link-handoff contract: {message}")

def require(text: str, token: str, label: str) -> None:
    if token not in text:
        fail(f"missing {label}: {token}")

if evidence.get("schema_version") != "itlwm-tahoe-link-handoff-runtime-record/v1":
    fail("unexpected runtime evidence schema")
candidate = evidence.get("candidate", {})
expected_candidate = {
    "release_tag": "v2.4.0-alpha-9b5d064",
    "archive_sha256": "654ab9c57d7546871c0c313ff7f09e77f4b1c59c49f468905e98a4f1fe602811",
    "binary_sha256": "93d7f53dd54608ae76e5c99d8dd61de1437059280ab72ab53c3db42914f37317",
    "macho_uuid": "ACCF10BE-552A-34D3-8908-7706514B2E3C",
    "source_commit": "9b5d064287541feadee23d5ee542fc423356660d",
}
for key, value in expected_candidate.items():
    if candidate.get(key) != value:
        fail(f"candidate {key} differs from the release-bound result")
if candidate.get("identity_evidence_sha256") != hashlib.sha256(identity_path.read_bytes()).hexdigest():
    fail("runtime record is not bound to exact-candidate identity evidence")
if identity.get("candidate_binding", {}).get("candidate_kext_bound") is not True:
    fail("identity record does not prove archive/installed/loaded equality")
if identity.get("candidate_binding", {}).get("failure_reasons") != []:
    fail("identity record retains an unexplained mismatch")

for key in ("raw_capture_committed", "secret_material_committed", "ssid_or_bssid_committed"):
    if evidence.get("commit_safety", {}).get(key) is not False:
        fail(f"unsafe committed material flag: {key}")
activation = evidence.get("activation", {})
if activation.get("scope", {}).get("lab_guest_only") is not True:
    fail("activation scope is not restricted to the lab guest")
if any(activation.get("scope", {}).get(key) is not False
       for key in ("physical_host_rebooted", "remote_10_90_10_22_touched")):
    fail("activation record claims an out-of-scope host action")
private = activation.get("private_auxkc_admission", {})
if private.get("canonical_postflight") != "PASS" or private.get("private_admission_result") != "PASS":
    fail("private AuxKC admission is not a complete pass")
if private.get("exact_member_count") != 5 or private.get("canonical_mutation") != "none":
    fail("private AuxKC membership or witness changed")
full = activation.get("full_auxkc", {})
if full.get("exact_member_count") != 5 or full.get("non_airport_members_unchanged") is not True:
    fail("full AuxKC membership changed")
for key in ("atomic_swap", "candidate_uuid_present", "rebuild_passed",
            "rollback_auxkc_present", "rollback_bundle_present"):
    if full.get(key) is not True:
        fail(f"full AuxKC activation lacks {key}")

epoch = evidence.get("controlled_epoch", {})
scenario = epoch.get("scenario", {})
if (scenario.get("expect"), scenario.get("join_invoked"), scenario.get("trigger_exit"),
        scenario.get("settle_seconds")) != ("wpa2-sha256-psk", True, 0, 15):
    fail("controlled profile epoch is not the recorded one-shot scenario")
credential = epoch.get("credential_handling", {})
if credential.get("profile_selected_by_sha256") is not True or credential.get("passphrase_recorded") is not False:
    fail("credential handling does not remain redacted")
if len(credential.get("saved_profile_identifier_sha256", "")) != 64:
    fail("saved profile is not represented by SHA-256 only")
regdiag = epoch.get("regdiag", {})
if (regdiag.get("abi"), regdiag.get("control_mode"), regdiag.get("control_block"),
        regdiag.get("trace_count"), regdiag.get("trace_dropped")) != (2, "0x35", "0x0", 14, 0):
    fail("RegDiag epoch is not coherent")
handshake = epoch.get("handshake", {})
if handshake.get("auth_policy_carrier") != {
        "auth_upper": "0x400", "events": 1, "pmf": 0,
        "policy": "0x6", "result": "0x0"}:
    fail("runtime evidence loses the exact SHA256-PSK carrier")
if handshake.get("strict_verdict") != "INCONCLUSIVE_OR_FAIL":
    fail("incomplete handshake is incorrectly labeled successful")
if handshake.get("strict_verdict_reasons") != [
        "missing successful EAPOL TX/RX pair", "no successful link-up publication"]:
    fail("incomplete handshake boundary changed")
if {key: handshake.get(key) for key in ("pmk_direct", "plti_publish", "plti_deliver", "eapol_tx", "eapol_rx", "link_up")} != {
        "pmk_direct": 0, "plti_publish": 1, "plti_deliver": 1,
        "eapol_tx": 0, "eapol_rx": 0, "link_up": 0}:
    fail("handshake stage counters changed")

handoff = epoch.get("link_handoff", {})
if handoff.get("verdict") != "LINK_PUBLICATION_INCOMPLETE":
    fail("link-handoff diagnosis is not incomplete")
if handoff.get("verdict_reasons") != [
        "pre-trigger controller status was active, but a later active transition was applied",
        "off-gate link publication was queued but not accepted"]:
    fail("link-handoff diagnosis does not preserve the resolved-controller boundary")
if handoff.get("pre") != {"controller_status": "0x3", "net80211_state": 4} or handoff.get("post") != {"controller_status": "0x3", "net80211_state": 4}:
    fail("redacted state snapshots changed")
if handoff.get("link_status") != {"applied": 2, "events": 4, "same": 2}:
    fail("link-status timeline changed")
if handoff.get("link_publish") != {
        "events": 4, "off_gate_rejected": 2, "published": 0, "queued": 2,
        "result_not_ready": 2, "result_success": 2}:
    fail("link-publication timeline changed")
if handoff.get("real_active_transition") != {
        "applied": True, "previous_status": "0x1", "requested_status": "0x3",
        "sequence": 10}:
    fail("the real active controller transition was not preserved")
if handoff.get("off_gate_predicate_at_rejection") != {"in_gate": True, "on_thread": True, "raw_code": 3}:
    fail("off-gate predicate finding changed")
if handoff.get("join_abort_events") != 0:
    fail("unexpected join-abort count")
environment = epoch.get("environment", {})
if environment.get("routes_changed_observed") is not True or environment.get("interface_changed") is not False:
    fail("route/interface observation changed")
if any(environment.get(key) != "none" for key in ("explicit_route_command", "explicit_address_command", "explicit_dhcp_command")):
    fail("capture claims an explicit network mutation")
if epoch.get("post_epoch_pinned_guest_query_succeeded") is not True:
    fail("post-epoch pinned guest reachability witness is missing")

expected_scripts = {
    "capture_sha256": "scripts/capture_tahoe_sae_layer.sh",
    "link_handoff_evaluator_sha256": "scripts/evaluate_tahoe_link_handoff.py",
    "auxkc_preflight_sha256": "scripts/tahoe_auxkc_admission_preflight.sh",
}
for evidence_key, source_path in expected_scripts.items():
    expected_hash = hashlib.sha256((root / source_path).read_bytes()).hexdigest()
    if evidence.get("script_identity", {}).get(evidence_key) != expected_hash:
        fail(f"runtime record is not bound to {source_path}")
# The SAE evaluator record is historical evidence from the captured epoch.
# A later evaluator may become stricter without retroactively changing which
# source produced this bounded, non-success result.
historical_sae_evaluator_hash = "63cc6b68ab96e0cbf4f390d23c419b08a2888f7cf4c02d12c82c46e0a85067a8"
if evidence.get("script_identity", {}).get("sae_capture_evaluator_sha256") != historical_sae_evaluator_hash:
    fail("runtime record is not bound to its historical SAE evaluator source")
if not all(len(value) == 64 for value in evidence.get("artifact_hashes", {}).values()):
    fail("raw-artifact witnesses are not SHA-256 values")
for key, value in evidence.get("non_claims", {}).items():
    if value is not False:
        fail(f"functional claim escaped its non-claim boundary: {key}")
verdict = evidence.get("verdict", {})
if verdict.get("candidate_identity_bound") is not True or verdict.get("controlled_link_handoff_epoch_completed") is not True:
    fail("successful candidate and controlled-epoch prerequisites are missing")
if verdict.get("functional_connection_claimed") is not False or verdict.get("safety_guard_relaxed") is not False:
    fail("record makes an unsafe functional claim or relaxes the guard")

identity_hash = hashlib.sha256(identity_path.read_bytes()).hexdigest()
evidence_hash = hashlib.sha256(evidence_path.read_bytes()).hexdigest()
for token in (
    "v2.4.0-alpha-9b5d064", identity_hash, evidence_hash,
    "LINK_PUBLICATION_INCOMPLETE", "inGate=1", "not a Wi-Fi association claim",
    "no connectivity, DHCP, ping, or traffic claim",
):
    require(document, token, "versioned lab result document")

print("PASS: Tahoe release-bound link-handoff lab result contract")
PY
