#!/usr/bin/env bash
# Validate a sanitized aggregate-only post-PLTI runtime attestation.
#
# With no arguments (or --self-test) this checks the validator using a local
# fixture.  With --evidence FILE [--record FILE] it validates a future runtime
# result.  This deliberately does not manufacture a success record before an
# isolated QEMU experiment has actually completed.
set -euo pipefail

MODE=self-test
EVIDENCE=""
RECORD=""

usage() {
    cat >&2 <<'EOF'
usage: test_tahoe_post_plti_trace_runtime_evidence_contract.sh [--self-test]
       test_tahoe_post_plti_trace_runtime_evidence_contract.sh \
           --evidence SAFE_AGGREGATE.json [--record RUNTIME_RECORD.md]
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --self-test)
            MODE=self-test
            shift
            ;;
        --evidence)
            [ "$#" -ge 2 ] || { usage; exit 2; }
            MODE=validate
            EVIDENCE="$2"
            shift 2
            ;;
        --record)
            [ "$#" -ge 2 ] || { usage; exit 2; }
            RECORD="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            usage
            exit 2
            ;;
    esac
done

if [ "$MODE" = validate ]; then
    [ -f "$EVIDENCE" ] || { printf 'FAIL: evidence file missing\n' >&2; exit 2; }
    [ -z "$RECORD" ] || [ -f "$RECORD" ] || { printf 'FAIL: record file missing\n' >&2; exit 2; }
fi

python3 - "$MODE" "$EVIDENCE" "$RECORD" <<'PY'
import json
import re
import sys
from pathlib import Path


def fail(message: str) -> None:
    raise SystemExit(f"FAIL: post-PLTI runtime evidence contract: {message}")


def require(condition: bool, message: str) -> None:
    if not condition:
        fail(message)


def require_exact_mapping(actual: object, expected: dict, label: str) -> None:
    require(isinstance(actual, dict) and set(actual) == set(expected),
            f"{label} shape malformed")
    for key, wanted in expected.items():
        observed = actual[key]
        require(type(observed) is type(wanted) and observed == wanted,
                f"{label}.{key} malformed")


def require_u32(section: dict, key: str) -> None:
    value = section.get(key)
    require(type(value) is int and 0 <= value <= 4294967295,
            f"trace scalar malformed: {key}")


def reject_duplicate_object_keys(pairs):
    document = {}
    for key, value in pairs:
        if key in document:
            raise ValueError("duplicate JSON key")
        document[key] = value
    return document


def parse_evidence(text: str) -> dict:
    return json.loads(text, object_pairs_hook=reject_duplicate_object_keys)


FAILURE_PHASES = {
    "none", "preflight", "identity-attestation", "hostkey-pin",
    "guest-build-pin", "trace-client-preflight", "radio-precondition-on",
    "trace-preflight-reset-request", "trace-preflight-reset-sequence",
    "trace-preflight-reset-ack", "trace-preflight-final-off",
    "radio-off-request", "radio-off-observation", "trace-reset-request",
    "radio-off-before-final-reset", "radio-off-after-final-reset-ack",
    "radio-off-after-final-reset-sync",
    "trace-reset-sequence", "trace-backend-iwx-ordered-unsupported",
    "trace-backend-unsupported", "trace-reset-ack",
    "trace-reset-snapshot-buffer-sync", "radio-on-request",
    "radio-on-observation", "trace-pre-seal-observation", "trace-seal",
    "trace-first-read", "trace-second-read", "trace-double-read-unstable",
    "trace-final-off", "trace-success-invariants", "trace-verdict-diagnostic",
}
NON_CLAIMS = [
    "pure SAE or PMF functionality",
    "generic Internet reachability",
    "physical-host validation",
    "proof beyond the categorical post-PLTI trace verdict",
]


def validate(document: dict, record=None) -> None:
    require(isinstance(document, dict), "document malformed")
    require(set(document) == {
        "schema", "candidate", "scope", "radio_cycle", "trace", "result",
        "failure_phase", "local_only_raw_artifacts", "commit_safety", "non_claims",
    }, "unexpected top-level evidence field")
    require(document.get("schema") == "itlwm-tahoe-post-plti-trace-runtime/v3",
            "unexpected schema")
    candidate = document.get("candidate")
    require(isinstance(candidate, dict), "candidate section missing")
    require(set(candidate) == {
        "source_commit", "source_identity_sha256", "release_tag",
        "release_publication_model", "archive_sha256", "binary_sha256",
        "macho_uuid", "identity_binding_precondition",
    }, "unexpected candidate evidence field")
    require(re.fullmatch(r"[0-9a-f]{40}", str(candidate.get("source_commit", ""))) is not None,
            "identity-bound source commit is not exact")
    require(re.fullmatch(r"[0-9a-f]{64}", str(candidate.get("source_identity_sha256", ""))) is not None,
            "identity-bound source identity is not exact")
    require(re.fullmatch(r"v[0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9]+(?:\.[A-Za-z0-9]+)*)?",
                         str(candidate.get("release_tag", ""))) is not None,
            "release tag is not a semantic version")
    require(candidate.get("release_publication_model") ==
            "single_mutable_release_per_semantic_version",
            "release policy is not the one-release-per-version model")
    for key in ("archive_sha256", "binary_sha256"):
        require(re.fullmatch(r"[0-9a-f]{64}", str(candidate.get(key, ""))) is not None,
                f"candidate {key} is malformed")
    require(re.fullmatch(r"[0-9A-F]{8}(?:-[0-9A-F]{4}){3}-[0-9A-F]{12}",
                         str(candidate.get("macho_uuid", ""))) is not None,
            "candidate Mach-O UUID is malformed")
    require(candidate.get("identity_binding_precondition") == "PASS",
            "exact loaded-candidate identity was not a precondition")

    require_exact_mapping(document.get("scope"), {
        "environment": "pinned_disposable_qemu_guest",
        "physical_host_touched": False,
        "physical_host_rebooted": False,
        "guest_rebooted_by_runner": False,
        "guest_network_identity_collected": False,
    }, "scope")

    radio = document.get("radio_cycle")
    require(isinstance(radio, dict) and set(radio) == {
        "requested_cycles", "connection_trigger", "saved_profile_precondition",
        "radio_off_observed", "radio_on_observed", "radio_recovery_attempted",
        "trace_armed_while_radio_off", "explicit_join_command",
        "explicit_route_command", "explicit_address_command",
        "explicit_dhcp_state_mutating_command",
    }, "radio cycle shape malformed")
    require(type(radio["requested_cycles"]) is int and radio["requested_cycles"] == 1,
            "not exactly one bounded radio cycle")
    require(radio["connection_trigger"] == "saved_profile_autojoin_only",
            "connection trigger exceeded saved-profile autojoin")
    require(radio["saved_profile_precondition"] == "external",
            "runner must not claim to enumerate a profile")
    for key in ("radio_off_observed", "radio_on_observed",
                "radio_recovery_attempted", "trace_armed_while_radio_off",
                "explicit_join_command", "explicit_route_command",
                "explicit_address_command", "explicit_dhcp_state_mutating_command"):
        require(type(radio[key]) is bool, f"radio boolean malformed: {key}")
    for key in ("explicit_join_command", "explicit_route_command",
                "explicit_address_command", "explicit_dhcp_state_mutating_command"):
        require(radio[key] is False, f"network mutation/non-autojoin claim is wrong: {key}")

    trace = document.get("trace")
    require(isinstance(trace, dict), "trace section missing")
    require(set(trace) == {
        "reset_control_sequence", "reset_ack_generation", "backend_preflight_iwn",
        "backend", "reset_ack_generation_synchronized",
        "initial_snapshot_buffer_generation_synchronized", "double_read_stable",
        "capture_generation", "integrity", "entry_count", "episode_count",
        "dropped_entries", "verdict", "first_missing_stage",
        "seal_control_acknowledged", "final_control_disabled",
    }, "unexpected trace evidence field")
    require(trace.get("backend") in {"iwn", "iwx", "unsupported", "unknown"},
            "trace backend category is invalid")
    for key in ("reset_ack_generation_synchronized",
                "initial_snapshot_buffer_generation_synchronized",
                "double_read_stable", "seal_control_acknowledged",
                "final_control_disabled", "backend_preflight_iwn"):
        require(type(trace.get(key)) is bool, f"trace boolean missing: {key}")
    for key in ("reset_control_sequence", "reset_ack_generation", "capture_generation",
                "entry_count", "episode_count", "dropped_entries"):
        require_u32(trace, key)
    require(trace.get("integrity") in {"ok", "inconclusive"}, "unknown trace integrity")
    allowed_verdicts = {
        "KERNEL_CHAIN_OBSERVED", "BRANCH_NOT_OBSERVED", "RESUME_NO_SCAN",
        "RESUME_NO_STATE_REQUEST", "RESUME_NO_IWN_DISPATCH",
        "SCAN_COMMAND_REJECTED", "SCAN_INCOMPLETE", "SCAN_NO_CANDIDATE",
        "RESUME_NO_SELECTION", "AUTH_NOT_DRAINED", "TX_NO_COMPLETION",
        "NO_EAPOL", "BACKEND_UNSUPPORTED", "INTEGRITY_INCONCLUSIVE",
    }
    require(trace.get("verdict") in allowed_verdicts, "unknown trace verdict")
    require(trace.get("first_missing_stage") in {
        "none", "state-scan-self-request", "iwn-scan-state",
        "iwn-scan-command", "scan-completion", "bss-selection",
        "join-bss", "auth-state", "auth-enqueue", "auth-dequeue",
        "auth-firmware-submit", "auth-exchange", "assoc-state",
        "assoc-enqueue", "assoc-dequeue", "assoc-firmware-submit",
        "assoc-exchange", "run-state", "eapol-decapped",
        "eapol-kernel-pae", "eapol-enqueue", "port-valid", "unknown",
    }, "unknown categorical first missing stage")

    require_exact_mapping(document.get("local_only_raw_artifacts"), {
        "client_output_retained_local_only": True,
        "raw_output_committed": False,
    }, "local raw artifacts")
    require_exact_mapping(document.get("commit_safety"), {
        "wireless_identity_committed": False,
        "ip_or_route_committed": False,
        "secret_material_committed": False,
        "raw_capture_committed": False,
    }, "commit safety")
    require(document.get("non_claims") == NON_CLAIMS,
            "fixed non-claims malformed")

    result = document.get("result")
    require(result in {"PASS", "INCONCLUSIVE"}, "result is invalid")
    require(document.get("failure_phase") in FAILURE_PHASES,
            "failure phase is invalid")
    if result == "PASS":
        require(document.get("failure_phase") == "none", "PASS retains a failure phase")
    else:
        require(document.get("failure_phase") != "none",
                "INCONCLUSIVE lacks a failure phase")
    if trace.get("backend") == "iwx":
        require(result == "INCONCLUSIVE",
                "the IWN-only runner cannot pass an IWX ordered trace")
        require(trace.get("verdict") == "BACKEND_UNSUPPORTED",
                "IWX ordered trace lacks an explicit unsupported verdict")
        require(document.get("failure_phase") ==
                "trace-backend-iwx-ordered-unsupported",
                "IWX ordered trace lacks its explicit boundary")
        if trace.get("backend_preflight_iwn") is False:
            require(radio.get("radio_off_observed") is False and
                    radio.get("radio_on_observed") is False,
                    "IWX preflight mismatch must stop before the radio cycle")
    if result == "PASS":
        require(trace.get("backend") == "iwn", "PASS is not on the supported legacy IWN backend")
        require(radio.get("radio_off_observed") is True and radio.get("radio_on_observed") is True,
                "PASS lacks observed radio OFF/ON")
        require(trace.get("reset_ack_generation_synchronized") is True and
                trace.get("initial_snapshot_buffer_generation_synchronized") is True and
                trace.get("double_read_stable") is True and
                trace.get("seal_control_acknowledged") is True and
                trace.get("final_control_disabled") is True,
                "PASS lacks a complete trace control lifecycle")
        require(trace.get("integrity") == "ok" and trace.get("dropped_entries") == 0,
                "PASS has incomplete trace integrity")
        require(trace.get("episode_count") == 1 and trace.get("entry_count") > 0,
                "PASS lacks one nonempty closed categorical episode")
        require(trace.get("reset_control_sequence") > 0 and
                trace.get("reset_ack_generation") > 0 and
                trace.get("capture_generation") > 0 and
                trace.get("reset_ack_generation") == trace.get("capture_generation"),
                "PASS lacks one exact reset/capture generation binding")
        require(trace.get("verdict") == "KERNEL_CHAIN_OBSERVED",
                "PASS overclaims a partial categorical trace")
        require(trace.get("first_missing_stage") == "none",
                "PASS retains a missing categorical stage")
        require(document.get("failure_phase") == "none", "PASS retains a failure phase")

    serialized = json.dumps(document, sort_keys=True)
    require(re.search(r"\b(?:[0-9]{1,3}\.){3}[0-9]{1,3}\b", serialized) is None,
            "literal IPv4 address escaped into evidence")
    require(re.search(r"(?i)\b(?:[0-9a-f]{2}:){5}[0-9a-f]{2}\b", serialized) is None,
            "literal MAC address escaped into evidence")
    require("AIAMlab" not in serialized and "BSSID" not in serialized and "SSID" not in serialized,
            "wireless identity escaped into evidence")

    if record is not None:
        require("one radio OFF/ON" in record, "record omits bounded radio scenario")
        require("does not prove pure SAE" in record, "record omits pure-SAE non-claim")
        require("local-only" in record, "record omits local-only raw-artifact boundary")
        require(re.search(r"\b(?:[0-9]{1,3}\.){3}[0-9]{1,3}\b", record) is None,
                "record contains a literal IPv4 address")
        require(re.search(r"(?i)\b(?:[0-9a-f]{2}:){5}[0-9a-f]{2}\b", record) is None,
                "record contains a literal MAC address")


mode, evidence_path, record_path = sys.argv[1:]
if mode == "self-test":
    fixture = {
        "schema": "itlwm-tahoe-post-plti-trace-runtime/v3",
        "candidate": {
            "source_commit": "a" * 40,
            "source_identity_sha256": "d" * 64,
            "release_tag": "v2.4.0-alpha",
            "release_publication_model": "single_mutable_release_per_semantic_version",
            "archive_sha256": "b" * 64,
            "binary_sha256": "c" * 64,
            "macho_uuid": "01234567-89AB-CDEF-0123-456789ABCDEF",
            "identity_binding_precondition": "PASS",
        },
        "scope": {
            "environment": "pinned_disposable_qemu_guest",
            "physical_host_touched": False,
            "physical_host_rebooted": False,
            "guest_rebooted_by_runner": False,
            "guest_network_identity_collected": False,
        },
        "radio_cycle": {
            "requested_cycles": 1,
            "connection_trigger": "saved_profile_autojoin_only",
            "saved_profile_precondition": "external",
            "radio_off_observed": True,
            "radio_on_observed": True,
            "radio_recovery_attempted": False,
            "trace_armed_while_radio_off": False,
            "explicit_join_command": False,
            "explicit_route_command": False,
            "explicit_address_command": False,
            "explicit_dhcp_state_mutating_command": False,
        },
        "trace": {
            "reset_control_sequence": 1,
            "reset_ack_generation": 7,
            "backend_preflight_iwn": False,
            "backend": "iwn",
            "reset_ack_generation_synchronized": True,
            "initial_snapshot_buffer_generation_synchronized": True,
            "double_read_stable": True,
            "capture_generation": 7,
            "integrity": "ok",
            "entry_count": 18,
            "episode_count": 1,
            "dropped_entries": 0,
            "verdict": "KERNEL_CHAIN_OBSERVED",
            "first_missing_stage": "none",
            "seal_control_acknowledged": True,
            "final_control_disabled": True,
        },
        "result": "PASS",
        "failure_phase": "none",
        "local_only_raw_artifacts": {
            "client_output_retained_local_only": True,
            "raw_output_committed": False,
        },
        "commit_safety": {
            "wireless_identity_committed": False,
            "ip_or_route_committed": False,
            "secret_material_committed": False,
            "raw_capture_committed": False,
        },
        "non_claims": [
            "pure SAE or PMF functionality",
            "generic Internet reachability",
            "physical-host validation",
            "proof beyond the categorical post-PLTI trace verdict",
        ],
    }
    validate(fixture, "one radio OFF/ON; does not prove pure SAE; local-only")
    fixture["trace"]["verdict"] = "BRANCH_NOT_OBSERVED"
    try:
        validate(fixture)
    except SystemExit:
        pass
    else:
        fail("self-test accepted partial trace as PASS")
    fixture["result"] = "INCONCLUSIVE"
    fixture["failure_phase"] = "trace-backend-iwx-ordered-unsupported"
    fixture["radio_cycle"]["radio_off_observed"] = False
    fixture["radio_cycle"]["radio_on_observed"] = False
    fixture["trace"].update({
        "backend": "iwx",
        "reset_ack_generation_synchronized": False,
        "initial_snapshot_buffer_generation_synchronized": False,
        "double_read_stable": False,
        "integrity": "inconclusive",
        "entry_count": 0,
        "episode_count": 0,
        "dropped_entries": 0,
        "verdict": "BACKEND_UNSUPPORTED",
        "first_missing_stage": "unknown",
        "seal_control_acknowledged": False,
        "final_control_disabled": True,
    })
    validate(fixture)
    fixture["result"] = "PASS"
    try:
        validate(fixture)
    except SystemExit:
        pass
    else:
        fail("self-test accepted IWX ordered trace as PASS")

    forged = json.loads(json.dumps(fixture))
    forged["result"] = "INCONCLUSIVE"
    forged["failure_phase"] = "trace-verdict-diagnostic"
    forged["radio_cycle"]["requested_cycles"] = True
    try:
        validate(forged)
    except SystemExit:
        pass
    else:
        fail("self-test accepted boolean requested cycle")
    try:
        parse_evidence('{"schema":"first","schema":"second"}')
    except ValueError:
        pass
    else:
        fail("self-test accepted a duplicate JSON key")

    print("PASS: post-PLTI runtime evidence contract skeleton")
else:
    evidence = parse_evidence(Path(evidence_path).read_text(encoding="utf-8"))
    record = Path(record_path).read_text(encoding="utf-8") if record_path else None
    validate(evidence, record)
    print("PASS: post-PLTI runtime evidence contract")
PY
