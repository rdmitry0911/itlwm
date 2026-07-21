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


def validate(document: dict, record: str | None = None) -> None:
    require(document.get("schema") == "itlwm-tahoe-post-plti-trace-runtime/v1",
            "unexpected schema")
    candidate = document.get("candidate")
    require(isinstance(candidate, dict), "candidate section missing")
    require(re.fullmatch(r"[0-9a-f]{40}", str(candidate.get("source_commit", ""))) is not None,
            "source commit is not exact")
    require(re.fullmatch(r"[A-Za-z0-9._-]+", str(candidate.get("release_tag", ""))) is not None,
            "release tag is malformed")
    require(candidate["release_tag"].endswith("-" + candidate["source_commit"][:7]),
            "release tag does not bind the source-commit short SHA")
    for key in ("archive_sha256", "binary_sha256"):
        require(re.fullmatch(r"[0-9a-f]{64}", str(candidate.get(key, ""))) is not None,
                f"candidate {key} is malformed")
    require(re.fullmatch(r"[0-9A-F]{8}(?:-[0-9A-F]{4}){3}-[0-9A-F]{12}",
                         str(candidate.get("macho_uuid", ""))) is not None,
            "candidate Mach-O UUID is malformed")
    require(candidate.get("identity_binding_precondition") == "PASS",
            "exact loaded-candidate identity was not a precondition")

    scope = document.get("scope")
    require(scope == {
        "environment": "pinned_disposable_qemu_guest",
        "physical_host_touched": False,
        "physical_host_rebooted": False,
        "guest_rebooted_by_runner": False,
        "guest_network_identity_collected": False,
    }, "scope is not the isolated no-identity QEMU scope")

    radio = document.get("radio_cycle")
    require(isinstance(radio, dict), "radio section missing")
    require(radio.get("requested_cycles") == 1, "not exactly one bounded radio cycle")
    require(radio.get("connection_trigger") == "saved_profile_autojoin_only",
            "connection trigger exceeded saved-profile autojoin")
    require(radio.get("saved_profile_precondition") == "external",
            "runner must not claim to enumerate a profile")
    for key in ("explicit_join_command", "explicit_route_command",
                "explicit_address_command", "explicit_dhcp_state_mutating_command"):
        require(radio.get(key) is False, f"network mutation/non-autojoin claim is wrong: {key}")

    trace = document.get("trace")
    require(isinstance(trace, dict), "trace section missing")
    require(trace.get("backend") in {"iwn", "unsupported", "unknown"},
            "trace backend category is invalid")
    for key in ("reset_ack_generation_synchronized",
                "initial_snapshot_buffer_generation_synchronized",
                "double_read_stable", "final_control_disabled"):
        require(isinstance(trace.get(key), bool), f"trace boolean missing: {key}")
    for key in ("reset_control_sequence", "reset_ack_generation", "capture_generation",
                "entry_count", "episode_count", "dropped_entries"):
        require(isinstance(trace.get(key), int) and trace[key] >= 0,
                f"trace scalar malformed: {key}")
    require(trace.get("integrity") in {"ok", "inconclusive"}, "unknown trace integrity")
    allowed_verdicts = {
        "KERNEL_CHAIN_OBSERVED", "BRANCH_NOT_OBSERVED", "RESUME_NO_SCAN",
        "RESUME_NO_SELECTION", "AUTH_NOT_DRAINED", "TX_NO_COMPLETION",
        "NO_EAPOL", "BACKEND_UNSUPPORTED", "INTEGRITY_INCONCLUSIVE",
    }
    require(trace.get("verdict") in allowed_verdicts, "unknown trace verdict")

    local_only = document.get("local_only_raw_artifacts")
    require(local_only == {
        "client_output_retained_local_only": True,
        "raw_output_committed": False,
    }, "raw trace handling is not local-only")
    safety = document.get("commit_safety")
    require(safety == {
        "wireless_identity_committed": False,
        "ip_or_route_committed": False,
        "secret_material_committed": False,
        "raw_capture_committed": False,
    }, "committed evidence is not fully sanitized")
    require("pure SAE or PMF functionality" in document.get("non_claims", []),
            "pure-SAE non-claim is missing")

    result = document.get("result")
    require(result in {"PASS", "INCONCLUSIVE"}, "result is invalid")
    if result == "PASS":
        require(trace.get("backend") == "iwn", "PASS is not on the supported legacy IWN backend")
        require(radio.get("radio_off_observed") is True and radio.get("radio_on_observed") is True,
                "PASS lacks observed radio OFF/ON")
        require(trace.get("reset_ack_generation_synchronized") is True and
                trace.get("initial_snapshot_buffer_generation_synchronized") is True and
                trace.get("double_read_stable") is True and
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
        "schema": "itlwm-tahoe-post-plti-trace-runtime/v1",
        "candidate": {
            "source_commit": "a" * 40,
            "release_tag": "v2.4.0-alpha-aaaaaaa",
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
            "explicit_join_command": False,
            "explicit_route_command": False,
            "explicit_address_command": False,
            "explicit_dhcp_state_mutating_command": False,
        },
        "trace": {
            "reset_control_sequence": 1,
            "reset_ack_generation": 7,
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
    print("PASS: post-PLTI runtime evidence contract skeleton")
else:
    evidence = json.loads(Path(evidence_path).read_text(encoding="utf-8"))
    record = Path(record_path).read_text(encoding="utf-8") if record_path else None
    validate(evidence, record)
    print("PASS: post-PLTI runtime evidence contract")
PY
