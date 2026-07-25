#!/usr/bin/env bash
# Validate the aggregate-only receipt-bound IWN WCL physical-scan evidence.
#
# Raw trace/client files stay in the private evidence directory.  This parser
# deliberately admits only the fixed categorical fields emitted by the runner.
set -euo pipefail

MODE=self-test
EVIDENCE=""

usage() {
    cat >&2 <<'EOF'
usage: test_tahoe_wcl_physical_scan_runtime_evidence_contract.sh [--self-test]
       test_tahoe_wcl_physical_scan_runtime_evidence_contract.sh \
         --evidence SAFE_AGGREGATE.json
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
    [ -f "$EVIDENCE" ] && [ ! -L "$EVIDENCE" ] || {
        printf 'FAIL: evidence file missing or unsafe\n' >&2
        exit 2
    }
fi

python3 - "$MODE" "$EVIDENCE" <<'PY'
import json
import re
import sys
from pathlib import Path


SCHEMA = "itlwm-tahoe-iwn-wcl-physical-scan-runtime/v3"
HASH64 = re.compile(r"[0-9a-f]{64}")
COMMIT40 = re.compile(r"[0-9a-f]{40}")
UUID = re.compile(r"[0-9A-F]{8}(?:-[0-9A-F]{4}){3}-[0-9A-F]{12}")
OUTCOMES = {
    "not-run", "ok", "client-unavailable", "interface-unavailable",
    "scan-failed", "count-overflow", "airport-itlwm-bsd-unresolved",
}
ENDPOINT_BINDINGS = {"unresolved", "airport-itlwm-bsd"}
VERDICTS = {
    "INTEGRITY_INCONCLUSIVE", "BACKEND_UNSUPPORTED", "BRANCH_NOT_OBSERVED",
    "LOWER_LEASE_NOT_OBSERVED", "TERMINAL_NOT_OBSERVED", "TERMINAL_ABORTED",
    "DONE_PUBLICATION_NOT_OBSERVED", "IWN_WCL_PHYSICAL_SCAN_OBSERVED",
}
STAGES = {"none", "request", "lower-lease", "terminal", "done-publication", "unknown"}
FAILURES = {
    "none", "preflight", "hostkey-pin", "guest-build-pin",
    "candidate-identity-before", "candidate-identity-after",
    "trace-client-preflight", "trace-client-postflight",
    "trace-reset-request", "trace-reset-sequence", "trace-reset-ack",
    "trace-reset-snapshot-sync", "scan-stimulus-parse", "scan-stimulus-exit",
    "trace-seal", "trace-seal-sequence", "trace-seal-ack",
    "wcl-state-first-read", "wcl-state-second-read",
    "wcl-state-double-read-unstable", "trace-verdict-diagnostic",
}
NON_CLAIMS = [
    "association, authentication, or SAE functionality",
    "roaming, reconnect, or multi-AP behavior",
    "data-plane or Internet reachability",
    "physical-host validation",
    "proof beyond one public scan invocation and its bounded IWN WCL physical-scan lifecycles",
]


def fail(message):
    raise SystemExit(f"FAIL: WCL physical-scan runtime evidence contract: {message}")


def require(condition, message):
    if not condition:
        fail(message)


def reject_duplicate_keys(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate key")
        result[key] = value
    return result


def reject_nonfinite(value):
    raise ValueError(f"non-finite JSON constant: {value}")


def parse(text):
    return json.loads(
        text, object_pairs_hook=reject_duplicate_keys,
        parse_constant=reject_nonfinite,
    )


def exact_mapping(value, keys, label):
    require(isinstance(value, dict) and set(value) == set(keys),
            f"{label} shape malformed")


def boolean(value, label):
    require(type(value) is bool, f"{label} malformed")


def u32(value, label):
    require(type(value) is int and 0 <= value <= 0xFFFFFFFF,
            f"{label} malformed")


def string(value, wanted, label):
    require(value == wanted, f"{label} malformed")


def validate_candidate(candidate):
    keys = {
        "kind", "source_commit", "source_identity_sha256",
        "source_identity_paths_count", "profile", "staged_kext_repo_path",
        "archive_sha256", "info_plist_sha256", "binary_sha256",
        "bundle_tree_sha256", "macho_uuid", "bundle_id", "trace_client_sha256",
        "identity_binding_precondition", "trace_client_receipt_binding_precondition",
    }
    exact_mapping(candidate, keys, "candidate")
    string(candidate["kind"], "local-unpublished-iwn-lab-candidate", "candidate.kind")
    require(isinstance(candidate["source_commit"], str) and
            COMMIT40.fullmatch(candidate["source_commit"]) is not None,
            "candidate.source_commit malformed")
    for key in (
            "source_identity_sha256", "archive_sha256", "info_plist_sha256",
            "binary_sha256", "bundle_tree_sha256", "trace_client_sha256"):
        require(isinstance(candidate[key], str) and HASH64.fullmatch(candidate[key]) is not None,
                f"candidate.{key} malformed")
    u32(candidate["source_identity_paths_count"], "candidate.source_identity_paths_count")
    require(candidate["source_identity_paths_count"] >= 1,
            "candidate.source_identity_paths_count is zero")
    string(candidate["profile"], "iwn-software-pmf-lab", "candidate.profile")
    string(candidate["staged_kext_repo_path"],
           "Build/Debug/Tahoe-IwnSoftwarePmfLab/AirportItlwm.kext",
           "candidate.staged_kext_repo_path")
    require(isinstance(candidate["macho_uuid"], str) and
            UUID.fullmatch(candidate["macho_uuid"]) is not None,
            "candidate.macho_uuid malformed")
    string(candidate["bundle_id"], "com.zxystd.AirportItlwm", "candidate.bundle_id")
    require(candidate["identity_binding_precondition"] in {"PASS", "INCONCLUSIVE"},
            "candidate.identity_binding_precondition malformed")
    require(candidate["trace_client_receipt_binding_precondition"] in {"PASS", "INCONCLUSIVE"},
            "candidate.trace_client_receipt_binding_precondition malformed")


def validate(document):
    exact_mapping(document, {
        "schema", "candidate", "scope", "physical_scan_stimulus",
        "trace_lifecycle", "iwn_wcl_physical_scan_trace", "result",
        "failure_phase", "local_only_raw_artifacts", "commit_safety", "non_claims",
    }, "document")
    string(document["schema"], SCHEMA, "schema")
    validate_candidate(document["candidate"])

    scope = document["scope"]
    exact_mapping(scope, {
        "environment", "physical_host_touched", "guest_rebooted_by_runner",
        "radio_power_changed", "association_or_profile_changed",
        "guest_network_identity_collected", "fixed_undirected_scan_requested",
    }, "scope")
    string(scope["environment"], "pinned_disposable_qemu_guest", "scope.environment")
    for key in (
            "physical_host_touched", "guest_rebooted_by_runner", "radio_power_changed",
            "association_or_profile_changed", "guest_network_identity_collected",
            "fixed_undirected_scan_requested"):
        boolean(scope[key], f"scope.{key}")
    require(all(scope[key] is False for key in (
        "physical_host_touched", "guest_rebooted_by_runner", "radio_power_changed",
        "association_or_profile_changed", "guest_network_identity_collected",
    )), "scope includes an unsafe mutation or collection claim")

    stimulus = document["physical_scan_stimulus"]
    exact_mapping(stimulus, {
        "command", "invocation_count", "client_exit_zero", "outcome", "total",
        "endpoint_binding", "band_2ghz", "band_5ghz", "band_6ghz", "band_other",
        "aggregate_sum_valid",
    }, "physical_scan_stimulus")
    string(stimulus["command"], "scan-wcl-physical", "physical_scan_stimulus.command")
    u32(stimulus["invocation_count"], "physical_scan_stimulus.invocation_count")
    boolean(stimulus["client_exit_zero"], "physical_scan_stimulus.client_exit_zero")
    require(stimulus["outcome"] in OUTCOMES, "physical_scan_stimulus.outcome malformed")
    require(stimulus["endpoint_binding"] in ENDPOINT_BINDINGS,
            "physical_scan_stimulus.endpoint_binding malformed")
    if stimulus["outcome"] in {"not-run", "airport-itlwm-bsd-unresolved"}:
        require(stimulus["endpoint_binding"] == "unresolved",
                "physical_scan_stimulus unresolved endpoint malformed")
    else:
        require(stimulus["endpoint_binding"] == "airport-itlwm-bsd",
                "physical_scan_stimulus controller endpoint binding malformed")
    for key in ("total", "band_2ghz", "band_5ghz", "band_6ghz", "band_other"):
        u32(stimulus[key], f"physical_scan_stimulus.{key}")
    boolean(stimulus["aggregate_sum_valid"], "physical_scan_stimulus.aggregate_sum_valid")
    require(stimulus["total"] == sum(stimulus[key] for key in (
        "band_2ghz", "band_5ghz", "band_6ghz", "band_other",
    )), "physical_scan_stimulus band sum malformed")
    require(scope["fixed_undirected_scan_requested"] ==
            (stimulus["invocation_count"] == 1),
            "scope scan request binding malformed")

    lifecycle = document["trace_lifecycle"]
    exact_mapping(lifecycle, {
        "capture_generation", "backend", "reset_control_acknowledged",
        "initial_snapshot_synchronized", "seal_control_acknowledged",
        "final_control_disabled", "double_read_stable",
    }, "trace_lifecycle")
    u32(lifecycle["capture_generation"], "trace_lifecycle.capture_generation")
    require(lifecycle["backend"] in {"UNKNOWN", "IWN"},
            "trace_lifecycle.backend malformed")
    for key in (
            "reset_control_acknowledged", "initial_snapshot_synchronized",
            "seal_control_acknowledged", "final_control_disabled", "double_read_stable"):
        boolean(lifecycle[key], f"trace_lifecycle.{key}")

    trace = document["iwn_wcl_physical_scan_trace"]
    exact_mapping(trace, {
        "entry_count", "integrity", "episode_count", "active_episode", "verdict",
        "first_missing_stage", "result_publication_issued",
    }, "iwn_wcl_physical_scan_trace")
    for key in ("entry_count", "episode_count", "active_episode"):
        u32(trace[key], f"iwn_wcl_physical_scan_trace.{key}")
    require(trace["integrity"] in {"ok", "inconclusive"},
            "iwn_wcl_physical_scan_trace.integrity malformed")
    require(trace["verdict"] in VERDICTS,
            "iwn_wcl_physical_scan_trace.verdict malformed")
    require(trace["first_missing_stage"] in STAGES,
            "iwn_wcl_physical_scan_trace.first_missing_stage malformed")
    boolean(trace["result_publication_issued"],
            "iwn_wcl_physical_scan_trace.result_publication_issued")

    exact_mapping(document["local_only_raw_artifacts"], {
        "client_output_retained_local_only", "raw_output_committed",
    }, "local_only_raw_artifacts")
    require(document["local_only_raw_artifacts"] == {
        "client_output_retained_local_only": True,
        "raw_output_committed": False,
    }, "local_only_raw_artifacts malformed")
    exact_mapping(document["commit_safety"], {
        "wireless_identity_committed", "ip_or_route_committed",
        "secret_material_committed", "raw_capture_committed",
    }, "commit_safety")
    require(document["commit_safety"] == {
        "wireless_identity_committed": False,
        "ip_or_route_committed": False,
        "secret_material_committed": False,
        "raw_capture_committed": False,
    }, "commit_safety malformed")
    require(document["non_claims"] == NON_CLAIMS, "non_claims malformed")
    require(document["result"] in {"PASS", "INCONCLUSIVE"}, "result malformed")
    require(document["failure_phase"] in FAILURES, "failure_phase malformed")

    positive = (
        document["candidate"]["identity_binding_precondition"] == "PASS" and
        document["candidate"]["trace_client_receipt_binding_precondition"] == "PASS" and
        stimulus["invocation_count"] == 1 and stimulus["client_exit_zero"] and
        stimulus["outcome"] == "ok" and
        stimulus["endpoint_binding"] == "airport-itlwm-bsd" and
        stimulus["aggregate_sum_valid"] and
        lifecycle["capture_generation"] > 0 and lifecycle["backend"] == "IWN" and
        all(lifecycle[key] for key in (
            "reset_control_acknowledged", "initial_snapshot_synchronized",
            "seal_control_acknowledged", "final_control_disabled", "double_read_stable",
        )) and
        1 <= trace["episode_count"] <= 2 and
        trace["entry_count"] >= 4 * trace["episode_count"] and
        trace["entry_count"] <= 128 and trace["integrity"] == "ok" and
        trace["active_episode"] == 0 and
        trace["verdict"] == "IWN_WCL_PHYSICAL_SCAN_OBSERVED" and
        trace["first_missing_stage"] == "none"
    )
    if document["result"] == "PASS":
        require(positive and document["failure_phase"] == "none",
                "PASS predicate malformed")
    else:
        require(not positive and document["failure_phase"] != "none",
                "INCONCLUSIVE predicate malformed")


def fixture():
    h = "a" * 64
    return {
        "schema": SCHEMA,
        "candidate": {
            "kind": "local-unpublished-iwn-lab-candidate",
            "source_commit": "b" * 40,
            "source_identity_sha256": h,
            "source_identity_paths_count": 1,
            "profile": "iwn-software-pmf-lab",
            "staged_kext_repo_path": "Build/Debug/Tahoe-IwnSoftwarePmfLab/AirportItlwm.kext",
            "archive_sha256": h,
            "info_plist_sha256": h,
            "binary_sha256": h,
            "bundle_tree_sha256": h,
            "macho_uuid": "01234567-89AB-CDEF-0123-456789ABCDEF",
            "bundle_id": "com.zxystd.AirportItlwm",
            "trace_client_sha256": h,
            "identity_binding_precondition": "PASS",
            "trace_client_receipt_binding_precondition": "PASS",
        },
        "scope": {
            "environment": "pinned_disposable_qemu_guest",
            "physical_host_touched": False,
            "guest_rebooted_by_runner": False,
            "radio_power_changed": False,
            "association_or_profile_changed": False,
            "guest_network_identity_collected": False,
            "fixed_undirected_scan_requested": True,
        },
        "physical_scan_stimulus": {
            "command": "scan-wcl-physical", "invocation_count": 1,
            "client_exit_zero": True, "outcome": "ok", "total": 0,
            "endpoint_binding": "airport-itlwm-bsd", "band_2ghz": 0,
            "band_5ghz": 0, "band_6ghz": 0,
            "band_other": 0, "aggregate_sum_valid": True,
        },
        "trace_lifecycle": {
            "capture_generation": 1, "backend": "IWN",
            "reset_control_acknowledged": True, "initial_snapshot_synchronized": True,
            "seal_control_acknowledged": True, "final_control_disabled": True,
            "double_read_stable": True,
        },
        "iwn_wcl_physical_scan_trace": {
            "entry_count": 8, "integrity": "ok", "episode_count": 2,
            "active_episode": 0, "verdict": "IWN_WCL_PHYSICAL_SCAN_OBSERVED",
            "first_missing_stage": "none", "result_publication_issued": False,
        },
        "result": "PASS", "failure_phase": "none",
        "local_only_raw_artifacts": {
            "client_output_retained_local_only": True, "raw_output_committed": False,
        },
        "commit_safety": {
            "wireless_identity_committed": False, "ip_or_route_committed": False,
            "secret_material_committed": False, "raw_capture_committed": False,
        },
        "non_claims": NON_CLAIMS,
    }


try:
    if sys.argv[1] == "validate":
        validate(parse(Path(sys.argv[2]).read_text(encoding="utf-8")))
    else:
        sample = fixture()
        validate(sample)
        sample["physical_scan_stimulus"]["unexpected_identity"] = "forbidden"
        try:
            validate(sample)
        except SystemExit:
            pass
        else:
            fail("self-test did not reject an unexpected identity-shaped field")
        sample = fixture()
        sample["physical_scan_stimulus"]["endpoint_binding"] = "en1"
        sample["result"] = "INCONCLUSIVE"
        sample["failure_phase"] = "trace-verdict-diagnostic"
        try:
            validate(sample)
        except SystemExit:
            pass
        else:
            fail("self-test did not reject a raw endpoint value")
        sample = fixture()
        sample["iwn_wcl_physical_scan_trace"]["episode_count"] = 3
        sample["iwn_wcl_physical_scan_trace"]["entry_count"] = 12
        sample["result"] = "PASS"
        try:
            validate(sample)
        except SystemExit:
            pass
        else:
            fail("self-test did not reject an unbounded PASS episode count")
except (OSError, ValueError, json.JSONDecodeError) as error:
    fail(str(error))

print("WCL physical-scan runtime evidence contract OK")
PY
