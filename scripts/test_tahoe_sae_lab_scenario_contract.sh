#!/usr/bin/env bash
# Static and fixture gate for the credential-safe Tahoe SAE/PMF lab matrix.
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

for path in \
    "$ROOT/scripts/run_tahoe_sae_lab_profiles.sh" \
    "$ROOT/scripts/test_tahoe_sae_lab_profiles_runner_fixture.sh" \
    "$ROOT/scripts/capture_tahoe_sae_layer.sh" \
    "$ROOT/scripts/evaluate_tahoe_sae_capture.py" \
    "$ROOT/scripts/capture_tahoe_lab_ap_visibility.py" \
    "$ROOT/scripts/capture_tahoe_lab_kext_identity.py" \
    "$ROOT/scripts/test_tahoe_lab_kext_identity_contract.sh" \
    "$ROOT/analysis/TAHOE_SAE_PMF_LAB_SCENARIO_MATRIX_2026-07-20.md" \
    "$ROOT/analysis/TAHOE_LAB_AP_READINESS_2026-07-20.md" \
    "$ROOT/analysis/TAHOE_LAB_CANDIDATE_IDENTITY_BINDING_2026-07-20.md" \
    "$ROOT/analysis/TAHOE_LAB_FF7B960_PROFILE_AND_SHA256_BASELINE_2026-07-20.md" \
    "$ROOT/evidence/runtime/tahoe_lab_ap_visibility_readiness.json" \
    "$ROOT/evidence/runtime/tahoe_lab_kext_identity_a4d803c.json" \
    "$ROOT/evidence/runtime/tahoe_lab_kext_identity_ff7b960.json" \
    "$ROOT/evidence/runtime/tahoe_lab_ff7b960_profile_and_sha256_baseline.json"; do
    [ -f "$path" ] || {
        echo "FAIL: missing lab-scenario contract input: $path" >&2
        exit 1
    }
done

bash -n "$ROOT/scripts/run_tahoe_sae_lab_profiles.sh"
bash -n "$ROOT/scripts/capture_tahoe_sae_layer.sh"
python3 "$ROOT/scripts/evaluate_tahoe_sae_capture.py" --self-test
bash "$ROOT/scripts/test_tahoe_sae_lab_profiles_runner_fixture.sh"
bash "$ROOT/scripts/test_tahoe_lab_kext_identity_contract.sh"

python3 - "$ROOT" <<'PY'
from __future__ import annotations

import hashlib
import json
import sys
from pathlib import Path


root = Path(sys.argv[1])
runner = (root / "scripts/run_tahoe_sae_lab_profiles.sh").read_text()
capture = (root / "scripts/capture_tahoe_sae_layer.sh").read_text()
evaluator = (root / "scripts/evaluate_tahoe_sae_capture.py").read_text()
visibility = (root / "scripts/capture_tahoe_lab_ap_visibility.py").read_text()
identity = (root / "scripts/capture_tahoe_lab_kext_identity.py").read_text()
matrix = (root / "analysis/TAHOE_SAE_PMF_LAB_SCENARIO_MATRIX_2026-07-20.md").read_text()
readiness = (root / "analysis/TAHOE_LAB_AP_READINESS_2026-07-20.md").read_text()
binding_doc = (root / "analysis/TAHOE_LAB_CANDIDATE_IDENTITY_BINDING_2026-07-20.md").read_text()
runtime_doc = (root / "analysis/TAHOE_LAB_FF7B960_PROFILE_AND_SHA256_BASELINE_2026-07-20.md").read_text()
gitignore = (root / ".gitignore").read_text()
evidence_path = root / "evidence/runtime/tahoe_lab_ap_visibility_readiness.json"
evidence = json.loads(evidence_path.read_text())
identity_evidence_path = root / "evidence/runtime/tahoe_lab_kext_identity_a4d803c.json"
identity_evidence = json.loads(identity_evidence_path.read_text())
bound_identity_evidence_path = root / "evidence/runtime/tahoe_lab_kext_identity_ff7b960.json"
bound_identity_evidence = json.loads(bound_identity_evidence_path.read_text())
runtime_evidence_path = root / "evidence/runtime/tahoe_lab_ff7b960_profile_and_sha256_baseline.json"
runtime_evidence = json.loads(runtime_evidence_path.read_text())


def fail(message: str) -> None:
    raise SystemExit(f"Tahoe SAE/PMF lab contract: {message}")


def require(text: str, needle: str, label: str) -> None:
    if needle not in text:
        fail(f"missing {label}: {needle}")


def forbid(text: str, needle: str, label: str) -> None:
    if needle in text:
        fail(f"forbidden {label}: {needle}")


def ordered(text: str, label: str, *needles: str) -> None:
    cursor = 0
    for needle in needles:
        found = text.find(needle, cursor)
        if found < 0:
            fail(f"{label} missing ordered token: {needle}")
        cursor = found + len(needle)


# The runner is a fixed four-epoch evidence protocol, not a generic join
# helper.  Exact carriers prevent a mixed-mode result from being mislabeled.
for needle in (
    "schema_version=itlwm-tahoe-sae-pmf-four-epoch/v1",
    "schema_version=itlwm-tahoe-sae-pmf-committable-attestation/v1",
    "schema_version=itlwm-tahoe-sae-pmf-profile-preflight/v1",
    "PREFLIGHT_ONLY=0",
    "--preflight",
    "credential_lookup_or_join=not-attempted",
    "PROFILE_READINESS_INCOMPLETE",
    "-listpreferredwirelessnetworks",
    "password_carrier=keychain-only",
    "profile_identifiers=sha256-only",
    "raw_capture_material=local-only-not-versioned",
    "raw_capture_material=not-versioned",
    "pure_sae_pmf_success_claim=false",
    "explicit_route_address_dhcp_mutation=false",
    "umask 077",
    "default_route_signature",
    "DEFAULT_ROUTE_BASELINE",
    "default_route_preserved",
    "profile_identifier_sha256",
    "capture_manifest_sha256",
    "capture_snapshot_sha256",
    "capture_trace_sha256",
    "capture_verdict_sha256",
    "[ \"$verdict\" = PASS ] || rc=1",
    "CAPTURE_TOOL=\"${AIAM_SAE_LAB_CAPTURE_TOOL:-$ROOT/scripts/capture_tahoe_sae_layer.sh}\"",
    "NETWORKSETUP_TOOL=\"${AIAM_SAE_LAB_NETWORKSETUP_TOOL:-/usr/sbin/networksetup}\"",
    "ROUTE_TOOL=\"${AIAM_SAE_LAB_ROUTE_TOOL:-/sbin/route}\"",
    "[ \"$ROUTE_TOOL\" != /sbin/route ]",
    "[ \"${AIAM_SAE_LAB_TEST_MODE:-}\" = 1 ]",
    "-- \"$NETWORKSETUP_TOOL\" -setairportnetwork",
):
    require(runner, needle, "four-epoch evidence runner")
ordered(runner, "fixed SAE/PMF profile order",
        "run_epoch wpa2-psk-baseline wpa2-psk",
        "run_epoch pure-sae-required-pmf-reject pure-sae-required-pmf-reject",
        "run_epoch sae-transition-psk sae-transition-psk",
        "run_epoch wpa2-psk-recovery wpa2-psk")
ordered(runner, "saved-profile preflight before join-capable epoch body",
        "profile_is_known()", "if [ \"$PREFLIGHT_ONLY\" -eq 1 ]",
        "run_epoch wpa2-psk-baseline wpa2-psk")
for needle in (
    "PASSWORD=",
    "--password",
    "ITLWM_ALLOWED_AP_PSK",
    "route add",
    "route delete",
    "route change",
    "ipconfig ",
    "ifconfig ",
    "kmutil",
    "kextload",
    "kextutil",
    "/sbin/shutdown",
    "shutdown -r",
    "/sbin/reboot",
    "/EFI",
    "sudo ",
    "airport -s",
    "wdutil scan",
    "-setairportpower",
):
    forbid(runner, needle, "unsafe runner capability")

# One capture records source provenance and accepts only strict profile names;
# its trigger output remains deliberately discarded to prevent credential logs.
for needle in (
    "schema_version=itlwm-tahoe-sae-pmk-capture/v2",
    "runner_git_head",
    "runner_tree_state",
    "wpa2-sha256-psk",
    "sae-transition-psk",
    "pure-sae-required-pmf-reject",
    "\"$@\" >/dev/null 2>&1",
    "routing_mutation=none",
    "ip_address_mutation=none",
):
    require(capture, needle, "per-epoch capture safety")
for needle in ("route add", "route delete", "route change", "ipconfig set",
               "ipconfig renew", "kmutil", "kextload", "/sbin/shutdown",
               "shutdown -r", "/sbin/reboot"):
    forbid(capture, needle, "capture mutation")

# Exact evaluator predicates distinguish baseline, transition, and the
# deliberate pure-SAE quarantine.  The self-test above executes the fixture
# matrix for both direct PMK and PLTI success plus all negative cases.
for needle in (
    "POLICY_AUDITED_WPA3_TRANSITION = 0x08",
    "WPA2_PSK = 0x0008",
    "WPA2_SHA256_PSK = 0x0400",
    "AUDITED_WPA3_PSK_TRANSITION = PURE_SAE | WPA2_PSK",
    "event.auth_upper == WPA2_PSK",
    "event.auth_upper == WPA2_SHA256_PSK",
    "evaluate_wpa2_sha256_psk",
    "event.auth_upper == AUDITED_WPA3_PSK_TRANSITION",
    "event.path == \"hidden-assoc\"",
    "event.pmf == 1",
    "event.policy == POLICY_REJECT_WPA3",
    "pure SAE carried PMK/PLTI/EAPOL/link activity",
    "transition-auth",
    "transition-policy",
    "pure-pmf",
    "pure-pmk",
    "pure-eapol",
    "pure-link",
    "wpa2-sha256-plti",
    "wpa2-sha256-auth",
):
    require(evaluator, needle, "exact evaluator fixture/predicate")

# Visibility is a committed readiness success, never a substitute for a
# candidate-runtime result.  The v2 schema explicitly prevents that claim.
for needle in (
    "itlwm-tahoe-lab-ap-visibility-readiness/v2",
    "loaded_kext_bound_to_checkout",
    "candidate_functional_verdict",
    "ready_for_candidate_runtime_experiment",
):
    require(visibility, needle, "visibility readiness boundary")
forbid(visibility, "candidate_fix", "obsolete unbound candidate claim")
require(gitignore, "runtime-captures/", "raw capture ignore rule")

# An exact candidate runtime result is only meaningful after the released
# archive, installed bundle, and already loaded bundle are proven identical.
# This gate must itself remain strictly read-only and host-key pinned.
for needle in (
    "itlwm-tahoe-lab-kext-identity-binding/v1",
    "StrictHostKeyChecking=yes",
    "PINNED_HOST_KEY_SHA256",
    "installed_binary_sha256_matches_release",
    "installed_macho_uuid_matches_release",
    "loaded_uuid_matches_release",
    "candidate_kext_bound",
    "candidate_kext_loaded_by_capture\": False",
    "association_tested\": False",
):
    require(identity, needle, "exact candidate identity gate")
for needle in (
    "StrictHostKeyChecking=no",
    "kmutil load",
    "kextload",
    "kextutil",
    "sudo ",
    "networksetup -set",
    "route add",
    "route delete",
    "route change",
    "ipconfig ",
    "ifconfig ",
):
    forbid(identity, needle, "identity-gate mutation or weak host-key capability")
for needle in (
    "candidate_kext_bound=true",
    "read-only",
    "not an association or traffic PASS",
    "identity precondition",
    "successful identity-quarantine",
):
    require(binding_doc, needle, "versioned identity-gate evidence document")

for needle in (
    "`wpa2-psk-baseline`",
    "`pure-sae-required-pmf-reject`",
    "`sae-transition-psk`",
    "`wpa2-psk-recovery`",
    "auth=0x1008",
    "policy=0xe",
    "not an SAE or PMF success claim",
    "Raw `trace.txt`, snapshots, reports, route dumps,",
):
    require(matrix, needle, "versioned scenario matrix")
for needle in (
    "actual successful exact-candidate identity scenario",
    "PROFILE_READINESS_INCOMPLETE",
    "auth=0x400",
    "INCONCLUSIVE_OR_FAIL",
    "no successful EAPOL TX/RX pair",
    "no successful link-up publication",
):
    require(runtime_doc, needle, "bound-candidate runtime record")
for needle in (
    "successful **scan readiness** record",
    "not a successful association",
    "candidate functional verdict was tested",
):
    require(readiness, needle, "versioned readiness boundary")

if evidence.get("schema_version") != "itlwm-tahoe-lab-ap-visibility-readiness/v2":
    fail("readiness evidence schema is not v2")
if evidence.get("candidate_binding", {}).get("loaded_kext_bound_to_checkout") is not False:
    fail("readiness evidence claims a bound checkout")
if evidence.get("candidate_binding", {}).get("candidate_functional_verdict") != "not-tested":
    fail("readiness evidence claims a candidate functional result")
if evidence.get("verdict", {}).get("ready_for_candidate_runtime_experiment") is not True:
    fail("readiness evidence lacks a successful directed-scan precondition")
if evidence.get("scan", {}).get("allowed_ap_visible") is not True:
    fail("readiness evidence lacks allowed-AP visibility")
if evidence.get("driver", {}).get("kext_loaded") is not True:
    fail("readiness evidence lacks a loaded-kext precondition")
lab = evidence.get("lab", {})
if any(lab.get(key) is not False for key in
       ("secret_material_committed", "ssid_committed", "psk_committed")):
    fail("readiness evidence does not declare secret/identifier redaction")
if any(value is not False for value in evidence.get("non_claims", {}).values()):
    fail("readiness evidence contains an unbounded functional claim")
identifier = evidence.get("runtime_environment", {}).get("allowed_ap_identifier", "")
if not identifier.startswith("operator-env-ssid-sha256:"):
    fail("readiness evidence stores a non-redacted AP identifier")
corewlan = evidence.get("scan", {}).get("corewlan", {})
if corewlan.get("raw_allowed_ssid_echoed_by_api") is not False:
    fail("readiness evidence contains a raw allowed SSID")
for observation in evidence.get("scan", {}).get("allowed_ap_observations", []):
    if observation.get("ssid_redacted") is not True or observation.get("bssid_redacted") is not True:
        fail("readiness observation lacks identifier redaction")
    if observation.get("directed_result_summary", {}).get("bssid_fields_present") != 0:
        fail("readiness observation leaked a BSSID field")

evidence_hash = hashlib.sha256(evidence_path.read_bytes()).hexdigest()
if evidence_hash not in readiness:
    fail("readiness document does not bind the committed evidence hash")

if identity_evidence.get("schema_version") != "itlwm-tahoe-lab-kext-identity-binding/v1":
    fail("identity evidence schema is not v1")
binding = identity_evidence.get("candidate_binding", {})
if binding.get("candidate_kext_bound") is not False:
    fail("identity evidence incorrectly claims a bound release kext")
checks = binding.get("checks", {})
for check in (
    "pinned_guest_query_succeeded",
    "wifi_interface_present",
    "installed_bundle_present",
    "installed_bundle_id_matches_release",
    "kext_reported_loaded",
    "loaded_uuid_matches_installed",
):
    if checks.get(check) is not True:
        fail(f"identity evidence lacks successful quarantine precondition: {check}")
for check in (
    "installed_binary_sha256_matches_release",
    "installed_macho_uuid_matches_release",
    "loaded_uuid_matches_release",
):
    if checks.get(check) is not False:
        fail(f"identity evidence lacks the expected stale-kext mismatch: {check}")
if identity_evidence.get("verdict", {}).get("candidate_runtime_test_performed") is not False:
    fail("identity evidence claims a candidate runtime test")
if any(value is not False for value in identity_evidence.get("non_claims", {}).values()):
    fail("identity evidence contains an unbounded functional claim")
if identity_evidence.get("command_result", {}).get("raw_guest_stdout_retained") is not False:
    fail("identity evidence retained raw guest stdout")
identity_evidence_hash = hashlib.sha256(identity_evidence_path.read_bytes()).hexdigest()
if identity_evidence_hash not in binding_doc:
    fail("identity document does not bind the committed identity evidence hash")

# The later record is deliberately the inverse of the stale-a4d803c
# quarantine: all identities match, but it still keeps the identity-only and
# incomplete-handshake boundaries explicit.  This turns successful laboratory
# prerequisites into reviewable, source-bound evidence without overstating
# Wi-Fi functionality.
if bound_identity_evidence.get("schema_version") != "itlwm-tahoe-lab-kext-identity-binding/v1":
    fail("bound identity evidence schema is not v1")
bound_binding = bound_identity_evidence.get("candidate_binding", {})
if bound_binding.get("candidate_kext_bound") is not True:
    fail("bound identity evidence does not prove the released kext identity")
for check in (
    "pinned_guest_query_succeeded",
    "wifi_interface_present",
    "installed_bundle_present",
    "installed_bundle_id_matches_release",
    "installed_binary_sha256_matches_release",
    "installed_macho_uuid_matches_release",
    "kext_reported_loaded",
    "loaded_uuid_matches_installed",
    "loaded_uuid_matches_release",
):
    if bound_binding.get("checks", {}).get(check) is not True:
        fail(f"bound identity evidence lacks matching check: {check}")
if bound_binding.get("failure_reasons") != []:
    fail("bound identity evidence retains an unexplained mismatch")
if bound_identity_evidence.get("verdict", {}).get("ready_for_exact_candidate_runtime_experiment") is not True:
    fail("bound identity evidence does not permit the exact-candidate experiment")
if any(value is not False for value in bound_identity_evidence.get("non_claims", {}).values()):
    fail("bound identity evidence contains an unbounded functional claim")

if runtime_evidence.get("schema_version") != "itlwm-tahoe-sae-pmf-bound-candidate-runtime-record/v1":
    fail("bound runtime evidence schema is not v1")
candidate = runtime_evidence.get("candidate", {})
bound_identity_hash = hashlib.sha256(bound_identity_evidence_path.read_bytes()).hexdigest()
if candidate.get("identity_evidence_sha256") != bound_identity_hash:
    fail("runtime record is not bound to the committed exact-candidate identity record")
if candidate.get("release_tag") != "v2.4.0-alpha-ff7b960":
    fail("runtime record names an unexpected release")
if candidate.get("binary_sha256") != "8cc606410deb27d57ce9a0144eb894b5375c60563ff58deb69b4a7a31723d21a":
    fail("runtime record names an unexpected released binary")
if candidate.get("macho_uuid") != "25D5548C-5DF3-3383-9544-967CD3AE0C6F":
    fail("runtime record names an unexpected released Mach-O UUID")
for key in ("raw_capture_committed", "secret_material_committed", "ssid_or_bssid_committed"):
    if runtime_evidence.get("commit_safety", {}).get(key) is not False:
        fail(f"runtime record violates redaction boundary: {key}")
profile = runtime_evidence.get("profile_preflight", {})
if profile.get("schema_version") != "itlwm-tahoe-sae-pmf-profile-preflight/v1":
    fail("profile preflight record has an unexpected schema")
if profile.get("credential_lookup_or_join") != "not-attempted":
    fail("profile preflight claims a credential lookup or join")
if profile.get("default_route_observable") is not True:
    fail("profile preflight did not observe the default route")
if profile.get("explicit_route_address_dhcp_mutation") is not False:
    fail("profile preflight made an unsafe network mutation claim")
if profile.get("verdict") != "PROFILE_READINESS_INCOMPLETE":
    fail("profile preflight does not retain its incomplete-matrix verdict")
profiles = profile.get("profiles", {})
if profiles.get("wpa2", {}).get("known") is not True:
    fail("profile preflight lacks the known WPA2 saved-profile observation")
for name in ("pure_sae", "sae_transition"):
    item = profiles.get(name, {})
    if item.get("known") is not False or len(item.get("identifier_sha256", "")) != 64:
        fail(f"profile preflight does not retain the redacted missing-profile boundary: {name}")
for script_name, evidence_key in (("scripts/run_tahoe_sae_lab_profiles.sh", "runner_script_sha256"),
                                  ("scripts/evaluate_tahoe_sae_capture.py", "evaluator_script_sha256")):
    expected_hash = hashlib.sha256((root / script_name).read_bytes()).hexdigest()
    record = profile if evidence_key == "runner_script_sha256" else runtime_evidence.get("sha256_psk_baseline", {})
    if record.get(evidence_key) != expected_hash:
        fail(f"runtime record is not bound to the committed {script_name} source")
sha256_psk = runtime_evidence.get("sha256_psk_baseline", {})
if sha256_psk.get("strict_verdict") != "INCONCLUSIVE_OR_FAIL":
    fail("SHA256-PSK diagnostic record is incorrectly marked successful")
if sha256_psk.get("strict_verdict_reasons") != [
        "missing successful EAPOL TX/RX pair",
        "no successful link-up publication",
]:
    fail("SHA256-PSK diagnostic record does not retain the exact failure boundary")
control = sha256_psk.get("control", {})
if (control.get("abi"), control.get("mode"), control.get("block"),
        control.get("trace_count"), control.get("trace_dropped")) != (2, "0x35", "0x0", 10, 0):
    fail("SHA256-PSK diagnostic record has an unexpected RegDiag epoch")
carrier = sha256_psk.get("expected_carrier", {})
if carrier != {"auth_upper": "0x400", "policy": "0x6", "pmf": 0}:
    fail("SHA256-PSK diagnostic record has an unexpected exact carrier")
ingress = sha256_psk.get("ingress", {})
if (ingress.get("association_ingress_status"), ingress.get("auth_policy_events"),
        ingress.get("accepted_plti_publications"), ingress.get("accepted_plti_deliveries"),
        ingress.get("join_abort_observed")) != (0, 2, 2, 2, True):
    fail("SHA256-PSK diagnostic record loses the accepted-PMK boundary")
if sha256_psk.get("stages") != {
        "accepted_direct_pmk": 0, "eapol_rx": 0, "eapol_tx": 0, "link_up": 0}:
    fail("SHA256-PSK diagnostic record has unexpected handshake counters")
if sha256_psk.get("route_guard", {}).get("default_route_preserved") is not True:
    fail("SHA256-PSK diagnostic record lacks the route-preservation observation")
if any(value is not False for value in sha256_psk.get("non_claims", {}).values()):
    fail("SHA256-PSK diagnostic record contains an unbounded functional claim")
runtime_evidence_hash = hashlib.sha256(runtime_evidence_path.read_bytes()).hexdigest()
if runtime_evidence_hash not in runtime_doc or bound_identity_hash not in runtime_doc:
    fail("runtime document does not bind its committed evidence")

print("PASS: Tahoe SAE/PMF lab scenario contract")
PY
