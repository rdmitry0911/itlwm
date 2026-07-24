#!/usr/bin/env bash
# Validate the aggregate-only IWN software-PMF runtime attestation.
set -euo pipefail

MODE=self-test
EVIDENCE=""

usage() {
    cat >&2 <<'EOF'
usage: test_tahoe_iwn_software_pmf_runtime_evidence_contract.sh [--self-test]
       test_tahoe_iwn_software_pmf_runtime_evidence_contract.sh \
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
    [ -f "$EVIDENCE" ] || { printf 'FAIL: evidence file missing\n' >&2; exit 2; }
fi

python3 - "$MODE" "$EVIDENCE" <<'PY'
import json
import re
import sys
from pathlib import Path


def fail(message: str) -> None:
    raise SystemExit(f"FAIL: IWN software-PMF runtime evidence contract: {message}")


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


def reject_duplicate_object_keys(pairs):
    document = {}
    for key, value in pairs:
        if key in document:
            raise ValueError("duplicate JSON key")
        document[key] = value
    return document


def parse_evidence(text: str) -> dict:
    return json.loads(text, object_pairs_hook=reject_duplicate_object_keys)


def is_hex(value: object, width: int) -> bool:
    return isinstance(value, str) and re.fullmatch(rf"[0-9a-f]{{{width}}}", value) is not None


def require_bool(section: dict, key: str) -> None:
    require(isinstance(section.get(key), bool), f"boolean missing: {key}")


def require_nonnegative_int(section: dict, key: str) -> None:
    value = section.get(key)
    require(type(value) is int and 0 <= value <= 4294967295,
            f"scalar malformed: {key}")


GENERIC_VERDICTS = {
    "KERNEL_CHAIN_OBSERVED", "BRANCH_NOT_OBSERVED",
    "RESUME_NO_STATE_REQUEST", "RESUME_NO_IWN_DISPATCH",
    "SCAN_COMMAND_REJECTED", "SCAN_INCOMPLETE", "SCAN_NO_CANDIDATE",
    "RESUME_NO_SELECTION", "AUTH_NOT_DRAINED", "TX_NO_COMPLETION",
    "NO_EAPOL", "BACKEND_UNSUPPORTED", "INTEGRITY_INCONCLUSIVE",
}
GENERIC_MISSING_STAGES = {
    "none", "state-scan-self-request", "iwn-scan-state",
    "iwn-scan-command", "scan-completion", "bss-selection", "join-bss",
    "auth-state", "auth-enqueue", "auth-dequeue", "auth-firmware-submit",
    "auth-exchange", "assoc-state", "assoc-enqueue", "assoc-dequeue",
    "assoc-firmware-submit", "assoc-exchange", "run-state",
    "eapol-decapped", "eapol-kernel-pae", "eapol-enqueue", "port-valid",
    "unknown",
}
GENERIC_FAILURE_PHASES = {
    "none", "not-run", "preflight", "identity-attestation", "hostkey-pin",
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
RUNTIME_FAILURE_PHASES = {
    "none", "preflight", "hostkey-pin", "guest-build-pin",
    "trace-client-preflight", "candidate-identity-before",
    "delegated-runner-attestation", "delegated-runner-failed",
    "iwn-software-pmf-report-first-read",
    "iwn-software-pmf-report-first-parse",
    "iwn-software-pmf-report-second-read",
    "iwn-software-pmf-report-second-parse",
    "iwn-software-pmf-double-read-unstable", "candidate-identity-after",
    "trace-client-postflight", "trace-verdict-diagnostic",
}
PMF_VERDICTS = {
    "INITIAL_SOFTWARE_PMF_OBSERVED", "BRANCH_NOT_OBSERVED",
    "PTK_SOFTWARE_CCMP_NOT_OBSERVED", "GTK_SOFTWARE_CCMP_NOT_OBSERVED",
    "IGTK_STAGE_NOT_OBSERVED", "IGTK_PUBLICATION_NOT_OBSERVED",
    "SOFTWARE_KEYSET_PUBLICATION_NOT_OBSERVED", "PORT_VALID_NOT_OBSERVED",
    "BACKEND_UNSUPPORTED", "INTEGRITY_INCONCLUSIVE",
}
PMF_MISSING_STAGES = {
    "none", "ptk-software-ccmp", "gtk-software-ccmp", "igtk-stage",
    "igtk-publication", "software-keyset-publication", "port-valid",
    "capture-seal", "unknown",
}
NON_CLAIMS = [
    "SAE or WPA3 functionality",
    "protected-MPDU interoperability or data-plane verification",
    "group rekey, reconnect, roaming, or multi-AP replacement",
    "physical-host validation",
    "proof beyond the local software PMF ownership trace verdict",
]


def validate(document: dict) -> None:
    require(isinstance(document, dict), "document malformed")
    require(set(document) == {
        "schema", "candidate", "scope", "radio_cycle", "generic_trace",
        "iwn_software_pmf_trace", "result", "failure_phase",
        "local_only_raw_artifacts", "commit_safety", "non_claims",
    }, "unexpected top-level evidence field")
    require(document.get("schema") == "itlwm-tahoe-iwn-software-pmf-runtime/v1",
            "unexpected schema")
    candidate = document.get("candidate")
    scope = document.get("scope")
    radio = document.get("radio_cycle")
    generic = document.get("generic_trace")
    pmf = document.get("iwn_software_pmf_trace")
    require(isinstance(candidate, dict), "candidate section missing")
    require(set(candidate) == {
        "source_commit", "source_identity_sha256", "release_tag",
        "release_publication_model", "archive_sha256", "binary_sha256",
        "macho_uuid", "trace_client_sha256", "identity_before_bound",
        "identity_after_bound", "trace_client_pre_bound",
        "trace_client_post_bound",
    }, "unexpected candidate field")
    require_exact_mapping(scope, {
        "environment": "pinned_disposable_qemu_guest",
        "physical_host_touched": False,
        "physical_host_rebooted": False,
        "guest_rebooted_by_runner": False,
        "guest_network_identity_collected": False,
    }, "scope")
    require(isinstance(radio, dict), "radio section missing")
    require_exact_mapping(radio, {
        "requested_cycles": 1,
        "connection_trigger": "saved_profile_autojoin_only",
        "saved_profile_precondition": "external",
        "explicit_join_command": False,
        "explicit_scan_command": False,
        "explicit_profile_command": False,
        "explicit_route_command": False,
        "explicit_address_command": False,
        "explicit_dhcp_state_mutating_command": False,
    }, "radio cycle")
    require(isinstance(generic, dict), "generic trace missing")
    require(isinstance(pmf, dict), "IWN PMF trace missing")
    require(set(generic) == {
        "delegated_runner_exit", "result", "failure_phase",
        "reset_control_sequence", "capture_generation", "backend", "integrity",
        "entry_count", "episode_count", "dropped_entries", "verdict",
        "first_missing_stage", "radio_off_observed", "radio_on_observed",
        "reset_ack_generation_synchronized",
        "initial_snapshot_buffer_generation_synchronized",
        "seal_control_acknowledged", "final_control_disabled",
        "double_read_stable", "trace_armed_while_radio_off",
        "backend_preflight_iwn",
    }, "unexpected generic trace field")
    require(set(pmf) == {
        "report_one_read", "report_two_read", "double_read_stable",
        "capture_generation", "backend", "entry_count", "integrity",
        "episode_count", "active_episode", "verdict", "first_missing_stage",
    }, "unexpected IWN PMF trace field")
    for key in (
        "identity_before_bound", "identity_after_bound", "trace_client_pre_bound",
        "trace_client_post_bound",
    ):
        require_bool(candidate, key)
    for key, width in (
        ("source_commit", 40), ("source_identity_sha256", 64),
        ("archive_sha256", 64), ("binary_sha256", 64),
        ("trace_client_sha256", 64),
    ):
        value = candidate.get(key)
        require(value == "" or is_hex(value, width), f"candidate {key} malformed")
    tag = candidate.get("release_tag")
    require(tag == "" or (isinstance(tag, str) and re.fullmatch(
        r"v[0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9]+(?:\.[A-Za-z0-9]+)*)?", tag)),
        "candidate release tag malformed")
    uuid = candidate.get("macho_uuid")
    require(uuid == "" or (isinstance(uuid, str) and re.fullmatch(
        r"[0-9A-F]{8}(?:-[0-9A-F]{4}){3}-[0-9A-F]{12}", uuid)),
        "candidate Mach-O UUID malformed")
    require(candidate.get("release_publication_model") ==
            "single_mutable_release_per_semantic_version", "release policy missing")

    delegated_exit = generic.get("delegated_runner_exit")
    require(type(delegated_exit) is int and 0 <= delegated_exit <= 255,
            "delegated runner exit malformed")
    for key in (
        "reset_control_sequence", "capture_generation",
        "entry_count", "episode_count", "dropped_entries",
    ):
        require_nonnegative_int(generic, key)
    for key in (
        "radio_off_observed", "radio_on_observed",
        "reset_ack_generation_synchronized",
        "initial_snapshot_buffer_generation_synchronized",
        "seal_control_acknowledged", "final_control_disabled", "double_read_stable",
        "trace_armed_while_radio_off", "backend_preflight_iwn",
    ):
        require_bool(generic, key)
    require(generic.get("result") in {"PASS", "INCONCLUSIVE"}, "generic result invalid")
    require(generic.get("failure_phase") in GENERIC_FAILURE_PHASES,
            "generic failure phase invalid")
    require(generic.get("backend") in {"iwn", "iwx", "unsupported", "unknown"},
            "generic backend invalid")
    require(generic.get("integrity") in {"ok", "inconclusive"}, "generic integrity invalid")
    require(generic.get("verdict") in GENERIC_VERDICTS and
            generic.get("first_missing_stage") in GENERIC_MISSING_STAGES,
            "generic categorical verdict malformed")
    if generic.get("result") == "PASS":
        require(generic.get("failure_phase") == "none",
                "generic PASS retains a failure phase")
    else:
        require(generic.get("failure_phase") != "none",
                "generic INCONCLUSIVE lacks a failure phase")

    for key in (
        "capture_generation", "entry_count", "episode_count", "active_episode",
    ):
        require_nonnegative_int(pmf, key)
    for key in ("report_one_read", "report_two_read", "double_read_stable"):
        require_bool(pmf, key)
    require(pmf.get("backend") in {"IWN", "unknown"}, "IWN PMF backend invalid")
    require(pmf.get("integrity") in {"ok", "inconclusive"}, "IWN PMF integrity invalid")
    require(pmf.get("verdict") in PMF_VERDICTS, "IWN PMF verdict invalid")
    require(pmf.get("first_missing_stage") in PMF_MISSING_STAGES,
            "IWN PMF first missing stage invalid")

    require(document.get("result") in {"PASS", "INCONCLUSIVE"}, "result invalid")
    require(document.get("failure_phase") in RUNTIME_FAILURE_PHASES,
            "failure phase invalid")
    if document.get("result") == "PASS":
        require(document.get("failure_phase") == "none", "PASS retains a failure phase")
    else:
        require(document.get("failure_phase") != "none",
                "INCONCLUSIVE lacks a failure phase")
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

    if document.get("result") == "PASS":
        require(all(candidate[key] is True for key in (
            "identity_before_bound", "identity_after_bound",
            "trace_client_pre_bound", "trace_client_post_bound",
        )), "PASS lacks exact candidate/client binding")
        for key, width in (
            ("source_commit", 40), ("source_identity_sha256", 64),
            ("archive_sha256", 64), ("binary_sha256", 64),
            ("trace_client_sha256", 64),
        ):
            require(is_hex(candidate[key], width), f"PASS candidate {key} absent")
        require(isinstance(candidate.get("release_tag"), str) and
                re.fullmatch(r"v[0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9]+(?:\.[A-Za-z0-9]+)*)?",
                             candidate["release_tag"]) is not None,
                "PASS candidate release tag absent")
        require(isinstance(candidate.get("macho_uuid"), str) and
                re.fullmatch(r"[0-9A-F]{8}(?:-[0-9A-F]{4}){3}-[0-9A-F]{12}",
                             candidate["macho_uuid"]) is not None,
                "PASS candidate Mach-O UUID absent")
        require(generic.get("delegated_runner_exit") == 0 and generic.get("result") == "PASS",
                "PASS lacks clean delegated runner")
        require(generic.get("backend") == "iwn" and generic.get("integrity") == "ok",
                "PASS generic trace is not a clean IWN chain")
        require(generic.get("verdict") == "KERNEL_CHAIN_OBSERVED" and
                generic.get("first_missing_stage") == "none", "PASS generic chain partial")
        require(generic.get("entry_count") > 0 and generic.get("episode_count") == 1 and
                generic.get("dropped_entries") == 0 and generic.get("capture_generation") > 0 and
                generic.get("reset_control_sequence") > 0, "PASS generic invariants incomplete")
        require(all(generic[key] is True for key in (
            "radio_off_observed", "radio_on_observed",
            "reset_ack_generation_synchronized",
            "initial_snapshot_buffer_generation_synchronized",
            "seal_control_acknowledged", "final_control_disabled", "double_read_stable",
            "trace_armed_while_radio_off", "backend_preflight_iwn",
        )), "PASS generic control lifecycle incomplete")
        require(all(pmf[key] is True for key in (
            "report_one_read", "report_two_read", "double_read_stable",
        )), "PASS lacks two stable PMF reads")
        require(pmf.get("backend") == "IWN" and pmf.get("integrity") == "ok" and
                pmf.get("capture_generation") == generic.get("capture_generation") and
                pmf.get("entry_count") == generic.get("entry_count") and
                pmf.get("entry_count") > 0 and pmf.get("episode_count") == 1 and
                pmf.get("active_episode") == 0, "PASS PMF snapshot is not one closed IWN episode")
        require(pmf.get("verdict") == "INITIAL_SOFTWARE_PMF_OBSERVED" and
                pmf.get("first_missing_stage") == "none", "PASS PMF chain partial")
        require(document.get("failure_phase") == "none", "PASS retains a failure phase")

    serialized = json.dumps(document, sort_keys=True)
    require(re.search(r"\b(?:[0-9]{1,3}\.){3}[0-9]{1,3}\b", serialized) is None,
            "literal IPv4 escaped into evidence")
    require(re.search(r"(?i)\b(?:[0-9a-f]{2}:){5}[0-9a-f]{2}\b", serialized) is None,
            "literal MAC escaped into evidence")
    require("SSID" not in serialized and "BSSID" not in serialized and "passphrase" not in serialized,
            "wireless identity or secret label escaped into evidence")


def fixture() -> dict:
    candidate = {
        "source_commit": "a" * 40,
        "source_identity_sha256": "b" * 64,
        "release_tag": "v2.4.0-alpha",
        "release_publication_model": "single_mutable_release_per_semantic_version",
        "archive_sha256": "c" * 64,
        "binary_sha256": "d" * 64,
        "macho_uuid": "01234567-89AB-CDEF-0123-456789ABCDEF",
        "trace_client_sha256": "e" * 64,
        "identity_before_bound": True,
        "identity_after_bound": True,
        "trace_client_pre_bound": True,
        "trace_client_post_bound": True,
    }
    generic = {
        "delegated_runner_exit": 0,
        "result": "PASS",
        "failure_phase": "none",
        "reset_control_sequence": 1,
        "capture_generation": 7,
        "backend": "iwn",
        "integrity": "ok",
        "entry_count": 21,
        "episode_count": 1,
        "dropped_entries": 0,
        "verdict": "KERNEL_CHAIN_OBSERVED",
        "first_missing_stage": "none",
        "radio_off_observed": True,
        "radio_on_observed": True,
        "reset_ack_generation_synchronized": True,
        "initial_snapshot_buffer_generation_synchronized": True,
        "seal_control_acknowledged": True,
        "final_control_disabled": True,
        "double_read_stable": True,
        "trace_armed_while_radio_off": True,
        "backend_preflight_iwn": True,
    }
    pmf = {
        "report_one_read": True,
        "report_two_read": True,
        "double_read_stable": True,
        "capture_generation": 7,
        "backend": "IWN",
        "entry_count": 21,
        "integrity": "ok",
        "episode_count": 1,
        "active_episode": 0,
        "verdict": "INITIAL_SOFTWARE_PMF_OBSERVED",
        "first_missing_stage": "none",
    }
    return {
        "schema": "itlwm-tahoe-iwn-software-pmf-runtime/v1",
        "candidate": candidate,
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
            "explicit_join_command": False,
            "explicit_scan_command": False,
            "explicit_profile_command": False,
            "explicit_route_command": False,
            "explicit_address_command": False,
            "explicit_dhcp_state_mutating_command": False,
        },
        "generic_trace": generic,
        "iwn_software_pmf_trace": pmf,
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
            "SAE or WPA3 functionality",
            "protected-MPDU interoperability or data-plane verification",
            "group rekey, reconnect, roaming, or multi-AP replacement",
            "physical-host validation",
            "proof beyond the local software PMF ownership trace verdict",
        ],
    }


mode, evidence_path = sys.argv[1:]
if mode == "self-test":
    document = fixture()
    validate(document)
    document["iwn_software_pmf_trace"]["capture_generation"] = 8
    try:
        validate(document)
    except SystemExit:
        pass
    else:
        fail("self-test accepted a cross-generation PMF PASS")
    incomplete = fixture()
    incomplete["result"] = "INCONCLUSIVE"
    incomplete["failure_phase"] = "candidate-identity-before"
    for key in ("source_commit", "source_identity_sha256", "release_tag",
                "archive_sha256", "binary_sha256", "macho_uuid",
                "trace_client_sha256"):
        incomplete["candidate"][key] = ""
    for key in ("identity_before_bound", "identity_after_bound",
                "trace_client_pre_bound", "trace_client_post_bound"):
        incomplete["candidate"][key] = False
    incomplete["generic_trace"].update({
        "delegated_runner_exit": 255,
        "result": "INCONCLUSIVE",
        "failure_phase": "not-run",
        "reset_control_sequence": 0,
        "capture_generation": 0,
        "backend": "unknown",
        "integrity": "inconclusive",
        "entry_count": 0,
        "episode_count": 0,
        "dropped_entries": 0,
        "verdict": "INTEGRITY_INCONCLUSIVE",
        "first_missing_stage": "unknown",
        "radio_off_observed": False,
        "radio_on_observed": False,
        "reset_ack_generation_synchronized": False,
        "initial_snapshot_buffer_generation_synchronized": False,
        "seal_control_acknowledged": False,
        "final_control_disabled": False,
        "double_read_stable": False,
        "trace_armed_while_radio_off": False,
        "backend_preflight_iwn": False,
    })
    incomplete["iwn_software_pmf_trace"].update({
        "report_one_read": False,
        "report_two_read": False,
        "double_read_stable": False,
        "capture_generation": 0,
        "backend": "unknown",
        "entry_count": 0,
        "integrity": "inconclusive",
        "episode_count": 0,
        "active_episode": 0,
        "verdict": "INTEGRITY_INCONCLUSIVE",
        "first_missing_stage": "unknown",
    })
    validate(incomplete)
    for mutate, label in (
        (lambda value: value["generic_trace"].__setitem__("entry_count", True),
         "boolean generic scalar"),
        (lambda value: value["iwn_software_pmf_trace"].__setitem__("active_episode", True),
         "boolean PMF scalar"),
        (lambda value: value["generic_trace"].__setitem__("entry_count", 2 ** 32),
         "out-of-range scalar"),
        (lambda value: value["candidate"].__setitem__("release_tag", ""),
         "empty PASS release tag"),
        (lambda value: value["candidate"].__setitem__("macho_uuid", ""),
         "empty PASS UUID"),
        (lambda value: value["generic_trace"].__setitem__("trace_armed_while_radio_off", False),
         "missing causal arm fact"),
        (lambda value: value.__setitem__("failure_phase", "unsafe-token"),
         "unknown top-level phase"),
        (lambda value: value["generic_trace"].__setitem__("failure_phase", "unsafe-token"),
         "unknown generic phase"),
        (lambda value: value.__setitem__("unexpected", "value"),
         "unexpected evidence field"),
    ):
        forged = fixture()
        mutate(forged)
        try:
            validate(forged)
        except SystemExit:
            pass
        else:
            fail(f"self-test accepted {label}")
    try:
        parse_evidence('{"schema":"first","schema":"second"}')
    except ValueError:
        pass
    else:
        fail("self-test accepted a duplicate JSON key")
    print("PASS: IWN software-PMF runtime evidence contract self-test")
else:
    try:
        document = parse_evidence(Path(evidence_path).read_text(encoding="utf-8"))
    except Exception as exc:
        fail(f"cannot parse evidence: {exc}")
    validate(document)
    print("PASS: IWN software-PMF runtime evidence contract")
PY
