#!/usr/bin/env bash
# Validate the sanitized, receipt-bound direct-IWN-SAE runtime attestation.
#
# The validator intentionally accepts only aggregate facts.  It does not read
# raw trace output and it rejects any attempt to carry a wireless/network
# identity or secret into the evidence document.
set -euo pipefail

MODE=self-test
EVIDENCE=""

usage() {
    cat >&2 <<'EOF'
usage: test_tahoe_iwn_direct_sae_runtime_evidence_contract.sh [--self-test]
       test_tahoe_iwn_direct_sae_runtime_evidence_contract.sh \
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


SCHEMA = "itlwm-tahoe-iwn-direct-sae-runtime/v2"
LAB_KIND = "local-unpublished-iwn-lab-candidate"
LAB_PROFILE = "iwn-software-pmf-lab"
LAB_STAGED_KEXT_REPO_PATH = (
    "Build/Debug/Tahoe-IwnSoftwarePmfLab/AirportItlwm.kext"
)
LAB_BUNDLE_ID = "com.zxystd.AirportItlwm"


def fail(message: str) -> None:
    raise SystemExit(f"FAIL: IWN direct-SAE runtime evidence contract: {message}")


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


def reject_nonfinite_json_constant(value: str):
    raise ValueError(f"non-finite JSON constant: {value}")


def parse_evidence(text: str) -> object:
    return json.loads(
        text,
        object_pairs_hook=reject_duplicate_object_keys,
        parse_constant=reject_nonfinite_json_constant,
    )


def is_hex(value: object, width: int) -> bool:
    return isinstance(value, str) and re.fullmatch(rf"[0-9a-f]{{{width}}}", value) is not None


def is_uuid(value: object) -> bool:
    return (isinstance(value, str) and re.fullmatch(
        r"[0-9A-F]{8}(?:-[0-9A-F]{4}){3}-[0-9A-F]{12}", value
    ) is not None)


def require_u32(section: dict, key: str, label: str) -> None:
    value = section.get(key)
    require(type(value) is int and 0 <= value <= 4294967295,
            f"{label}.{key} malformed")


def require_bool(section: dict, key: str, label: str) -> None:
    require(type(section.get(key)) is bool, f"{label}.{key} malformed")


GENERIC_VERDICTS = {
    "KERNEL_CHAIN_OBSERVED", "BRANCH_NOT_OBSERVED", "RESUME_NO_SCAN",
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
    "assoc-firmware-submit", "assoc-exchange", "run-state", "eapol-decapped",
    "eapol-kernel-pae", "eapol-enqueue", "port-valid", "unknown",
}
GENERIC_FAILURE_PHASES = {
    "none", "not-run", "preflight", "identity-attestation", "hostkey-pin",
    "guest-build-pin", "trace-client-identity-binding",
    "trace-client-preflight", "radio-precondition-on",
    "trace-preflight-reset-request", "trace-preflight-reset-sequence",
    "trace-preflight-reset-ack", "trace-preflight-final-off",
    "radio-off-request", "radio-off-observation", "trace-reset-request",
    "radio-off-before-final-reset", "radio-off-after-final-reset-ack",
    "radio-off-after-final-reset-sync", "trace-reset-sequence",
    "trace-backend-iwx-ordered-unsupported", "trace-backend-unsupported",
    "trace-reset-ack", "trace-reset-snapshot-buffer-sync", "radio-on-request",
    "radio-on-observation", "trace-pre-seal-observation", "trace-seal",
    "trace-first-read", "trace-second-read", "trace-double-read-unstable",
    "trace-final-off", "trace-success-invariants", "trace-verdict-diagnostic",
}
DIRECT_VERDICTS = {
    "DIRECT_SAE_4WAY_PORT_VALID", "BRANCH_NOT_OBSERVED",
    "FRESH_SCAN_NOT_OBSERVED", "REQUEST_NO_BSS_SELECTION",
    "JOIN_BSS_NOT_OBSERVED", "NODE_MFP_NOT_NEGOTIATED",
    "AUTH_STATE_NOT_OBSERVED", "COMMIT_TX_NOT_COMPLETE",
    "PEER_COMMIT_NOT_ACCEPTED", "CONFIRM_TX_NOT_COMPLETE",
    "PEER_CONFIRM_NOT_VALIDATED", "PMK_NOT_CLAIMED",
    "ASSOC_DESCRIPTOR_NOT_ACCEPTED", "ASSOC_EXCHANGE_NOT_COMPLETE",
    "FOUR_WAY_NOT_COMPLETE", "PMF_PTK_SOFTWARE_CCMP_NOT_OBSERVED",
    "PMF_GTK_SOFTWARE_CCMP_NOT_OBSERVED", "PMF_IGTK_STAGE_NOT_OBSERVED",
    "PMF_IGTK_PUBLICATION_NOT_OBSERVED",
    "PMF_KEYSET_PUBLICATION_NOT_OBSERVED", "BACKEND_UNSUPPORTED",
    "INTEGRITY_INCONCLUSIVE",
}
DIRECT_MISSING_STAGES = {
    "none", "capture-seal", "fresh-scan", "bss-selection", "join-bss",
    "node-mfp", "auth-state", "commit-tx", "peer-commit", "confirm-tx",
    "peer-confirm", "pmk-claim", "assoc-descriptor", "assoc-exchange",
    "four-way", "pmf-ptk-software-ccmp", "pmf-gtk-software-ccmp",
    "pmf-igtk-stage", "pmf-igtk-publication",
    "pmf-keyset-publication", "port-valid", "unknown",
}
RUNTIME_FAILURE_PHASES = {
    "none", "preflight", "candidate-receipt", "candidate-identity-before",
    "hostkey-pin", "guest-build-pin", "trace-client-preflight",
    "delegated-runner-attestation", "delegated-runner-failed",
    "iwn-direct-sae-report-first-read", "iwn-direct-sae-report-first-parse",
    "iwn-direct-sae-report-second-read", "iwn-direct-sae-report-second-parse",
    "iwn-direct-sae-report-double-read-unstable", "candidate-identity-after",
    "trace-client-postflight", "trace-verdict-diagnostic",
}
NON_CLAIMS = [
    "application or data-plane traffic verification",
    "group rekey, reconnect, roaming, or multi-AP replacement",
    "physical-host validation",
    "proof beyond one sealed direct-SAE four-way port-valid trace",
]


def validate_candidate(candidate: object) -> dict:
    require(isinstance(candidate, dict), "candidate section missing")
    expected = {
        "kind", "profile", "source_commit", "source_identity_sha256",
        "source_identity_paths_count", "staged_kext_repo_path", "archive_sha256",
        "info_plist_sha256", "bundle_tree_sha256", "binary_sha256", "macho_uuid",
        "bundle_id", "trace_client_sha256", "identity_before_bound",
        "identity_after_bound", "trace_client_pre_bound", "trace_client_post_bound",
    }
    require(set(candidate) == expected, "unexpected candidate field")
    require(candidate.get("kind") == LAB_KIND, "candidate kind malformed")
    require(candidate.get("profile") == LAB_PROFILE, "candidate profile malformed")
    require(candidate.get("staged_kext_repo_path") == LAB_STAGED_KEXT_REPO_PATH,
            "candidate staged kext path malformed")
    require(candidate.get("bundle_id") == LAB_BUNDLE_ID,
            "candidate bundle identifier malformed")
    require(is_hex(candidate.get("source_commit"), 40),
            "candidate source commit malformed")
    for key in (
        "source_identity_sha256", "archive_sha256", "info_plist_sha256",
        "bundle_tree_sha256", "binary_sha256", "trace_client_sha256",
    ):
        require(is_hex(candidate.get(key), 64), f"candidate {key} malformed")
    count = candidate.get("source_identity_paths_count")
    require(type(count) is int and 1 <= count <= 4294967295,
            "candidate source identity path count malformed")
    require(is_uuid(candidate.get("macho_uuid")), "candidate Mach-O UUID malformed")
    for key in (
        "identity_before_bound", "identity_after_bound", "trace_client_pre_bound",
        "trace_client_post_bound",
    ):
        require_bool(candidate, key, "candidate")
    return candidate


def validate_generic_trace(generic: object) -> dict:
    require(isinstance(generic, dict), "generic trace missing")
    expected = {
        "delegated_runner_exit", "result", "failure_phase",
        "reset_control_sequence", "capture_generation", "backend", "integrity",
        "entry_count", "episode_count", "dropped_entries", "verdict",
        "first_missing_stage", "radio_off_observed", "radio_on_observed",
        "reset_ack_generation_synchronized",
        "initial_snapshot_buffer_generation_synchronized",
        "seal_control_acknowledged", "final_control_disabled", "double_read_stable",
        "trace_armed_while_radio_off", "backend_preflight_iwn",
    }
    require(set(generic) == expected, "unexpected generic trace field")
    delegated_exit = generic.get("delegated_runner_exit")
    require(type(delegated_exit) is int and 0 <= delegated_exit <= 255,
            "generic delegated runner exit malformed")
    require(generic.get("result") in {"PASS", "INCONCLUSIVE"},
            "generic result malformed")
    require(generic.get("failure_phase") in GENERIC_FAILURE_PHASES,
            "generic failure phase malformed")
    for key in (
        "reset_control_sequence", "capture_generation", "entry_count",
        "episode_count", "dropped_entries",
    ):
        require_u32(generic, key, "generic")
    for key in (
        "radio_off_observed", "radio_on_observed",
        "reset_ack_generation_synchronized",
        "initial_snapshot_buffer_generation_synchronized",
        "seal_control_acknowledged", "final_control_disabled", "double_read_stable",
        "trace_armed_while_radio_off", "backend_preflight_iwn",
    ):
        require_bool(generic, key, "generic")
    require(generic.get("backend") in {"iwn", "iwx", "unsupported", "unknown"},
            "generic backend malformed")
    require(generic.get("integrity") in {"ok", "inconclusive"},
            "generic integrity malformed")
    require(generic.get("verdict") in GENERIC_VERDICTS,
            "generic verdict malformed")
    require(generic.get("first_missing_stage") in GENERIC_MISSING_STAGES,
            "generic first missing stage malformed")
    if generic["result"] == "PASS":
        require(generic["failure_phase"] == "none",
                "generic PASS retains a failure phase")
    else:
        require(generic["failure_phase"] != "none",
                "generic INCONCLUSIVE lacks a failure phase")
    return generic


def validate_direct_trace(direct: object) -> dict:
    require(isinstance(direct, dict), "direct SAE trace missing")
    expected = {
        "report_one_read", "report_two_read", "double_read_stable",
        "capture_generation", "backend", "entry_count", "integrity",
        "episode_count", "active_episode", "verdict", "first_missing_stage",
    }
    require(set(direct) == expected, "unexpected direct SAE trace field")
    for key in ("report_one_read", "report_two_read", "double_read_stable"):
        require_bool(direct, key, "direct trace")
    for key in (
        "capture_generation", "entry_count", "episode_count", "active_episode",
    ):
        require_u32(direct, key, "direct trace")
    require(direct.get("backend") in {"IWN", "unknown"},
            "direct trace backend malformed")
    require(direct.get("integrity") in {"ok", "inconclusive"},
            "direct trace integrity malformed")
    require(direct.get("verdict") in DIRECT_VERDICTS,
            "direct trace verdict malformed")
    require(direct.get("first_missing_stage") in DIRECT_MISSING_STAGES,
            "direct trace first missing stage malformed")
    return direct


def generic_sealed_lifecycle_is_complete(generic: dict) -> bool:
    result_and_diagnostic = (
        (generic["result"] == "PASS" and generic["failure_phase"] == "none" and
         generic["verdict"] == "KERNEL_CHAIN_OBSERVED" and
         generic["first_missing_stage"] == "none")
        or
        (generic["result"] == "INCONCLUSIVE" and
         generic["failure_phase"] == "trace-verdict-diagnostic")
    )
    return (
        generic["delegated_runner_exit"] == 0 and result_and_diagnostic and
        generic["backend"] == "iwn" and generic["integrity"] == "ok" and
        generic["reset_control_sequence"] > 0 and
        generic["capture_generation"] > 0 and generic["entry_count"] > 0 and
        generic["episode_count"] == 1 and generic["dropped_entries"] == 0 and
        all(generic[key] is True for key in (
            "radio_off_observed", "radio_on_observed",
            "reset_ack_generation_synchronized",
            "initial_snapshot_buffer_generation_synchronized",
            "seal_control_acknowledged", "final_control_disabled",
            "double_read_stable", "trace_armed_while_radio_off",
            "backend_preflight_iwn",
        ))
    )


def direct_chain_is_positive(generic: dict, direct: dict) -> bool:
    return (
        all(direct[key] is True for key in (
            "report_one_read", "report_two_read", "double_read_stable",
        )) and
        direct["capture_generation"] == generic["capture_generation"] and
        direct["backend"] == "IWN" and
        direct["entry_count"] == generic["entry_count"] and
        direct["entry_count"] > 0 and direct["integrity"] == "ok" and
        direct["episode_count"] == 1 and direct["active_episode"] == 0 and
        direct["verdict"] == "DIRECT_SAE_4WAY_PORT_VALID" and
        direct["first_missing_stage"] == "none"
    )


IPV4_RE = re.compile(r"(?<![0-9])(?:[0-9]{1,3}\.){3}[0-9]{1,3}(?![0-9])")
MAC_RE = re.compile(r"(?i)(?<![0-9a-f])(?:[0-9a-f]{2}:){5}[0-9a-f]{2}(?![0-9a-f])")
IPV6_RE = re.compile(
    r"(?i)(?<![0-9a-f])(?:[0-9a-f]{1,4}:){2,}[0-9a-f:]*[0-9a-f](?![0-9a-f])"
)
SENSITIVE_LABEL_RE = re.compile(
    r"(?i)(?:\bssid\b|\bbssid\b|\bpass(?:word|phrase)\b|\bcredential(?:s)?\b|"
    r"\bkeychain\b|\bpre[-_ ]?shared\b|\bpsk\b|\bsecret\b|"
    r"\bnetwork[-_ ]?(?:name|identifier)\b|\bwireless[-_ ]?(?:name|identifier)\b)"
)


def string_values(value: object):
    if isinstance(value, dict):
        for nested in value.values():
            yield from string_values(nested)
    elif isinstance(value, list):
        for nested in value:
            yield from string_values(nested)
    elif isinstance(value, str):
        yield value


def reject_sensitive_values(document: dict) -> None:
    for value in string_values(document):
        require(IPV4_RE.search(value) is None,
                "literal IPv4 address escaped into evidence")
        require(MAC_RE.search(value) is None,
                "literal MAC address escaped into evidence")
        require(IPV6_RE.search(value) is None,
                "literal IPv6 address escaped into evidence")
        require(SENSITIVE_LABEL_RE.search(value) is None,
                "wireless identity or secret escaped into evidence")


def validate(document: object) -> None:
    require(isinstance(document, dict), "document malformed")
    require(set(document) == {
        "schema", "candidate", "scope", "wcl_trigger", "generic_trace",
        "iwn_direct_sae_trace", "result", "failure_phase",
        "local_only_raw_artifacts", "commit_safety", "non_claims",
    }, "unexpected top-level evidence field")
    require(document.get("schema") == SCHEMA, "unexpected schema")
    candidate = validate_candidate(document.get("candidate"))
    require_exact_mapping(document.get("scope"), {
        "environment": "pinned_disposable_qemu_guest",
        "physical_host_touched": False,
        "physical_host_rebooted": False,
        "guest_rebooted_by_runner": False,
        "wireless_identity_collected": False,
        "network_secret_collected": False,
    }, "scope")
    require_exact_mapping(document.get("wcl_trigger"), {
        "requested_cycles": 1,
        "connection_trigger": "saved_profile_autojoin_only",
        "secret_argument": "none",
        "fresh_scan_state": "delegated_radio_off_on_trace_reset_while_off",
        "explicit_join_command": False,
        "explicit_scan_command": False,
        "explicit_profile_command": False,
        "explicit_route_command": False,
        "explicit_address_command": False,
        "explicit_dhcp_state_mutating_command": False,
    }, "WCL trigger")
    generic = validate_generic_trace(document.get("generic_trace"))
    direct = validate_direct_trace(document.get("iwn_direct_sae_trace"))
    require(document.get("result") in {"PASS", "INCONCLUSIVE"},
            "result malformed")
    require(document.get("failure_phase") in RUNTIME_FAILURE_PHASES,
            "runtime failure phase malformed")
    if document["result"] == "PASS":
        require(document["failure_phase"] == "none", "PASS retains a failure phase")
        require(all(candidate[key] is True for key in (
            "identity_before_bound", "identity_after_bound",
            "trace_client_pre_bound", "trace_client_post_bound",
        )), "PASS lacks exact candidate/client binding")
        require(generic_sealed_lifecycle_is_complete(generic),
                "PASS lacks sealed generic lifecycle")
        require(direct_chain_is_positive(generic, direct),
                "PASS lacks complete direct SAE chain")
    else:
        require(document["failure_phase"] != "none",
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
    require(document.get("non_claims") == NON_CLAIMS, "fixed non-claims malformed")
    reject_sensitive_values(document)


def fixture() -> dict:
    candidate = {
        "kind": LAB_KIND,
        "profile": LAB_PROFILE,
        "source_commit": "a" * 40,
        "source_identity_sha256": "b" * 64,
        "source_identity_paths_count": 7,
        "staged_kext_repo_path": LAB_STAGED_KEXT_REPO_PATH,
        "archive_sha256": "c" * 64,
        "info_plist_sha256": "d" * 64,
        "bundle_tree_sha256": "e" * 64,
        "binary_sha256": "f" * 64,
        "macho_uuid": "01234567-89AB-CDEF-0123-456789ABCDEF",
        "bundle_id": LAB_BUNDLE_ID,
        "trace_client_sha256": "1" * 64,
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
    direct = {
        "report_one_read": True,
        "report_two_read": True,
        "double_read_stable": True,
        "capture_generation": 7,
        "backend": "IWN",
        "entry_count": 21,
        "integrity": "ok",
        "episode_count": 1,
        "active_episode": 0,
        "verdict": "DIRECT_SAE_4WAY_PORT_VALID",
        "first_missing_stage": "none",
    }
    return {
        "schema": SCHEMA,
        "candidate": candidate,
        "scope": {
            "environment": "pinned_disposable_qemu_guest",
            "physical_host_touched": False,
            "physical_host_rebooted": False,
            "guest_rebooted_by_runner": False,
            "wireless_identity_collected": False,
            "network_secret_collected": False,
        },
        "wcl_trigger": {
            "requested_cycles": 1,
            "connection_trigger": "saved_profile_autojoin_only",
            "secret_argument": "none",
            "fresh_scan_state": "delegated_radio_off_on_trace_reset_while_off",
            "explicit_join_command": False,
            "explicit_scan_command": False,
            "explicit_profile_command": False,
            "explicit_route_command": False,
            "explicit_address_command": False,
            "explicit_dhcp_state_mutating_command": False,
        },
        "generic_trace": generic,
        "iwn_direct_sae_trace": direct,
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
        "non_claims": NON_CLAIMS,
    }


def expect_rejected(document: object, label: str) -> None:
    try:
        validate(document)
    except SystemExit:
        return
    fail(f"self-test accepted {label}")


mode, evidence_path = sys.argv[1:]
if mode == "self-test":
    valid = fixture()
    validate(valid)

    incomplete = fixture()
    incomplete["result"] = "INCONCLUSIVE"
    incomplete["failure_phase"] = "candidate-identity-before"
    incomplete["candidate"]["identity_before_bound"] = False
    incomplete["iwn_direct_sae_trace"].update({
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

    negative_cases = (
        (lambda value: value["candidate"].__setitem__("identity_after_bound", False),
         "unbound candidate accepted as PASS"),
        (lambda value: value["generic_trace"].__setitem__("trace_armed_while_radio_off", False),
         "unsealed generic lifecycle accepted as PASS"),
        (lambda value: value["iwn_direct_sae_trace"].__setitem__("verdict", "FOUR_WAY_NOT_COMPLETE"),
         "partial direct trace accepted as PASS"),
        (lambda value: value["iwn_direct_sae_trace"].__setitem__("first_missing_stage", "four-way"),
         "missing direct stage accepted as PASS"),
        (lambda value: value["candidate"].__setitem__("release_tag", "v0.0.0"),
         "release field accepted in lab candidate"),
        (lambda value: value["generic_trace"].__setitem__("entry_count", True),
         "boolean generic scalar"),
        (lambda value: value.__setitem__("failure_phase", "unsafe-token"),
         "unknown runtime failure phase"),
    )
    for mutate, label in negative_cases:
        forged = fixture()
        mutate(forged)
        expect_rejected(forged, label)
    try:
        reject_sensitive_values({"only_for_self_test": "SSID-safe"})
    except SystemExit:
        pass
    else:
        fail("self-test accepted a wireless identifier string")
    try:
        reject_sensitive_values({"only_for_self_test": "10.20.30.40"})
    except SystemExit:
        pass
    else:
        fail("self-test accepted a literal IPv4 string")
    try:
        reject_sensitive_values({"only_for_self_test": "password-safe"})
    except SystemExit:
        pass
    else:
        fail("self-test accepted a secret string")
    try:
        parse_evidence(
            '{"schema":"first","candidate":{"kind":"one","kind":"two"}}'
        )
    except ValueError:
        pass
    else:
        fail("self-test accepted a duplicate JSON key")
    try:
        parse_evidence('{"schema":NaN}')
    except ValueError:
        pass
    else:
        fail("self-test accepted a non-finite JSON value")
    print("PASS: IWN direct-SAE runtime evidence contract self-test")
else:
    try:
        document = parse_evidence(Path(evidence_path).read_text(encoding="utf-8"))
    except Exception as exc:
        fail(f"cannot parse evidence: {exc}")
    validate(document)
    print("PASS: IWN direct-SAE runtime evidence contract")
PY
