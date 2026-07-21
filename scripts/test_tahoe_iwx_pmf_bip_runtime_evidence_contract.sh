#!/usr/bin/env bash
# Validate the aggregate-only evidence emitted by the bounded IWX PMF/BIP gate.
#
# --self-test validates only an in-memory synthetic aggregate.  It never
# manufactures a runtime claim and never contacts the guest or laboratory AP.
set -euo pipefail

MODE=self-test
EVIDENCE=""

usage() {
    cat >&2 <<'EOF'
usage: test_tahoe_iwx_pmf_bip_runtime_evidence_contract.sh [--self-test]
       test_tahoe_iwx_pmf_bip_runtime_evidence_contract.sh --evidence SAFE_AGGREGATE.json
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
        printf 'FAIL: evidence file missing or symlinked\n' >&2
        exit 2
    }
fi

python3 - "$MODE" "$EVIDENCE" <<'PY'
import json
import re
import sys
from pathlib import Path


def fail(message: str) -> None:
    raise SystemExit(f"FAIL: IWX PMF/BIP runtime evidence contract: {message}")


def require(condition: bool, message: str) -> None:
    if not condition:
        fail(message)


def boolean_fields(mapping: dict, *keys: str) -> None:
    for key in keys:
        require(isinstance(mapping.get(key), bool), f"boolean missing: {key}")


def validate(document: dict) -> None:
    require(document.get("schema") == "itlwm-tahoe-iwx-pmf-bip-runtime/v1",
            "unexpected schema")

    candidate = document.get("candidate")
    require(isinstance(candidate, dict), "candidate section missing")
    require(re.fullmatch(r"[0-9a-f]{40}", str(candidate.get("source_commit", ""))) is not None,
            "source commit is not exact")
    require(re.fullmatch(r"[0-9a-f]{64}", str(candidate.get("source_identity_sha256", ""))) is not None,
            "source identity is not exact")
    require(re.fullmatch(r"v[0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9]+(?:\.[A-Za-z0-9]+)*)?",
                         str(candidate.get("release_tag", ""))) is not None,
            "release tag is not a semantic version")
    require(candidate.get("release_publication_model") ==
            "single_mutable_release_per_semantic_version",
            "release policy changed")
    for key in ("archive_sha256", "binary_sha256"):
        require(re.fullmatch(r"[0-9a-f]{64}", str(candidate.get(key, ""))) is not None,
                f"candidate {key} is malformed")
    require(re.fullmatch(r"[0-9A-F]{8}(?:-[0-9A-F]{4}){3}-[0-9A-F]{12}",
                         str(candidate.get("macho_uuid", ""))) is not None,
            "candidate Mach-O UUID is malformed")
    boolean_fields(candidate, "identity_before_bound", "identity_after_bound")

    scope = document.get("scope")
    require(isinstance(scope, dict), "scope section missing")
    require(scope.get("environment") ==
            "pinned_disposable_qemu_guest_with_pinned_lab_ap",
            "scope is not the pinned disposable guest and lab AP")
    require(scope.get("guest_rebooted_by_runner") is False,
            "runner must not reboot the guest")
    require(scope.get("physical_validation_host_touched") is False,
            "physical validation host is out of scope")
    require(scope.get("host_ip_nat_forwarding_route_mutated") is False,
            "host network mutation claim is wrong")
    boolean_fields(scope, "host_ap_process_touched")

    saved = document.get("saved_profile")
    require(isinstance(saved, dict), "saved-profile section missing")
    require(saved.get("preexisting_keychain_or_known_network_required") is True,
            "saved-profile precondition changed")
    require(saved.get("explicit_join_command") is False,
            "explicit join command is not permitted")
    require(saved.get("password_carrier") == "none",
            "password carrier is not absent")
    boolean_fields(saved, "preflight_observed")

    network = document.get("network_invariants")
    require(isinstance(network, dict), "network-invariants section missing")
    for key in ("explicit_route_command", "explicit_address_command",
                "explicit_dhcp_state_mutating_command"):
        require(network.get(key) is False, f"network mutation claim is wrong: {key}")
    boolean_fields(network, "default_route_management_interface_preserved",
                   "direct_lab_route_preserved", "preexisting_lab_address_preserved")

    ap = document.get("ap_switchover")
    require(isinstance(ap, dict), "AP switchover section missing")
    require(ap.get("host_ip_nat_forwarding_route_mutated") is False,
            "AP helper claims host network mutation")
    boolean_fields(ap, "optional_pmf_preflight_passed", "required_pmf_was_active",
                   "optional_pmf_rollback_verified")

    pmf_bip = document.get("pmf_bip")
    require(isinstance(pmf_bip, dict), "PMF/BIP section missing")
    boolean_fields(pmf_bip, "initial_active_prefix_observed_before_rekey",
                   "bounded_traffic_probe_succeeded_before_rekey",
                   "bounded_group_rekey_requested",
                   "sealed_cross_slot_rekey_observed")

    trace = document.get("trace")
    require(isinstance(trace, dict), "trace section missing")
    require(trace.get("backend") in {"iwx", "unsupported", "unknown"},
            "trace backend category is invalid")
    require(trace.get("integrity") in {"ok", "inconclusive"},
            "trace integrity category is invalid")
    for key in ("reset_control_sequence", "capture_generation", "entry_count",
                "episode_count", "dropped_entries"):
        require(isinstance(trace.get(key), int) and trace[key] >= 0,
                f"trace scalar malformed: {key}")
    boolean_fields(trace, "reset_ack_generation_synchronized",
                   "initial_snapshot_buffer_generation_synchronized",
                   "seal_control_acknowledged", "final_control_disabled",
                   "double_read_stable")
    require(trace.get("verdict") in {
        "INITIAL_PMF_BIP_OBSERVED", "CROSS_SLOT_REKEY_OBSERVED",
        "PMF_RX_NOT_OBSERVED", "Q0_DOORBELL_NOT_OBSERVED",
        "Q0_COMPLETION_NOT_OBSERVED", "IGTK_PUBLICATION_NOT_OBSERVED",
        "ACTIVE_SLOT_NOT_OBSERVED", "PORT_VALID_NOT_OBSERVED",
        "BACKEND_UNSUPPORTED", "BRANCH_NOT_OBSERVED", "INTEGRITY_INCONCLUSIVE",
    }, "trace verdict is invalid")
    require(trace.get("first_missing_stage") in {
        "none", "capture-seal", "pmf-rx", "q0-doorbell", "q0-completion",
        "igtk-publication", "active-slot", "port-valid", "cross-slot-rekey",
        "unknown",
    }, "trace missing-stage category is invalid")

    radio = document.get("radio_cycle")
    require(isinstance(radio, dict), "radio-cycle section missing")
    require(radio.get("requested_cycles") == 1,
            "exactly one bounded radio cycle is required")
    require(radio.get("connection_trigger") == "saved_profile_autojoin_only",
            "connection trigger exceeded saved-profile autojoin")
    boolean_fields(radio, "radio_off_observed", "radio_on_observed",
                   "radio_recovery_attempted")

    local_only = document.get("local_only_raw_artifacts")
    require(local_only == {
        "trace_interface_route_and_hostapd_output_retained_local_only": True,
        "raw_output_committed": False,
    }, "raw artifacts are not kept local-only")
    safety = document.get("commit_safety")
    require(safety == {
        "wireless_identity_committed": False,
        "ip_or_route_committed": False,
        "secret_material_committed": False,
        "raw_capture_committed": False,
    }, "evidence commit-safety boundary changed")

    require(set(document.get("non_claims", [])) >= {
        "pure SAE functionality", "generic Internet reachability",
        "physical-host validation",
        "proof beyond the bounded PMF/BIP and traffic gate",
    }, "required non-claims are missing")

    result = document.get("result")
    require(result in {"PASS", "INCONCLUSIVE"}, "result is invalid")
    if result == "PASS":
        require(document.get("failure_phase") == "none", "PASS retains a failure phase")
        require(candidate["identity_before_bound"] is True and
                candidate["identity_after_bound"] is True,
                "PASS lacks before/after identity binding")
        require(scope["host_ap_process_touched"] is True,
                "PASS lacks a required-PMF AP switchover")
        require(saved["preflight_observed"] is True,
                "PASS lacks the saved-profile preflight")
        require(all(network[key] is True for key in (
            "default_route_management_interface_preserved",
            "direct_lab_route_preserved", "preexisting_lab_address_preserved")),
            "PASS lacks network-invariant preservation")
        require(all(ap[key] is True for key in (
            "optional_pmf_preflight_passed", "required_pmf_was_active",
            "optional_pmf_rollback_verified")),
            "PASS lacks AP preflight, activation, or rollback")
        require(all(pmf_bip[key] is True for key in (
            "initial_active_prefix_observed_before_rekey",
            "bounded_traffic_probe_succeeded_before_rekey",
            "bounded_group_rekey_requested", "sealed_cross_slot_rekey_observed")),
            "PASS lacks the ordered initial/traffic/rekey chain")
        require(trace["backend"] == "iwx" and trace["integrity"] == "ok" and
                trace["dropped_entries"] == 0,
                "PASS lacks an intact IWX trace")
        require(trace["verdict"] == "CROSS_SLOT_REKEY_OBSERVED" and
                trace["first_missing_stage"] == "none",
                "PASS lacks the sealed cross-slot verdict")
        require(trace["episode_count"] == 1 and trace["entry_count"] > 0 and
                trace["reset_control_sequence"] > 0 and trace["capture_generation"] > 0,
                "PASS lacks one nonempty bound capture episode")
        require(all(trace[key] is True for key in (
            "reset_ack_generation_synchronized",
            "initial_snapshot_buffer_generation_synchronized",
            "seal_control_acknowledged", "final_control_disabled",
            "double_read_stable")), "PASS lacks trace lifecycle acknowledgement")
        require(radio["radio_off_observed"] is True and radio["radio_on_observed"] is True,
                "PASS lacks observed radio OFF/ON")
    else:
        require(isinstance(document.get("failure_phase"), str) and
                document["failure_phase"] not in {"", "none"},
                "INCONCLUSIVE result lacks a failure phase")

    serialized = json.dumps(document, sort_keys=True)
    require(re.search(r"\b(?:[0-9]{1,3}\.){3}[0-9]{1,3}\b", serialized) is None,
            "literal IPv4 address escaped into evidence")
    require(re.search(r"(?i)\b(?:[0-9a-f]{2}:){5}[0-9a-f]{2}\b", serialized) is None,
            "literal MAC address escaped into evidence")
    require(re.search(r"(?i)\b(?:ssid|bssid)\b", serialized) is None,
            "wireless identity label escaped into evidence")
    require("AIAMlab" not in serialized and "wpa_passphrase" not in serialized,
            "wireless identity or credential field escaped into evidence")
    require(re.search(r"\b(?:en[0-9]+|wlp[0-9][A-Za-z0-9_-]*)\b", serialized) is None,
            "interface identity escaped into evidence")


def fixture() -> dict:
    return {
        "schema": "itlwm-tahoe-iwx-pmf-bip-runtime/v1",
        "candidate": {
            "source_commit": "a" * 40,
            "source_identity_sha256": "b" * 64,
            "release_tag": "v2.4.0-alpha",
            "release_publication_model": "single_mutable_release_per_semantic_version",
            "archive_sha256": "c" * 64,
            "binary_sha256": "d" * 64,
            "macho_uuid": "01234567-89AB-CDEF-0123-456789ABCDEF",
            "identity_before_bound": True,
            "identity_after_bound": True,
        },
        "scope": {
            "environment": "pinned_disposable_qemu_guest_with_pinned_lab_ap",
            "guest_rebooted_by_runner": False,
            "physical_validation_host_touched": False,
            "host_ap_process_touched": True,
            "host_ip_nat_forwarding_route_mutated": False,
        },
        "saved_profile": {
            "preexisting_keychain_or_known_network_required": True,
            "preflight_observed": True,
            "explicit_join_command": False,
            "password_carrier": "none",
        },
        "network_invariants": {
            "default_route_management_interface_preserved": True,
            "direct_lab_route_preserved": True,
            "preexisting_lab_address_preserved": True,
            "explicit_route_command": False,
            "explicit_address_command": False,
            "explicit_dhcp_state_mutating_command": False,
        },
        "ap_switchover": {
            "optional_pmf_preflight_passed": True,
            "required_pmf_was_active": True,
            "optional_pmf_rollback_verified": True,
            "host_ip_nat_forwarding_route_mutated": False,
        },
        "pmf_bip": {
            "initial_active_prefix_observed_before_rekey": True,
            "bounded_traffic_probe_succeeded_before_rekey": True,
            "bounded_group_rekey_requested": True,
            "sealed_cross_slot_rekey_observed": True,
        },
        "trace": {
            "backend": "iwx",
            "reset_control_sequence": 1,
            "capture_generation": 7,
            "reset_ack_generation_synchronized": True,
            "initial_snapshot_buffer_generation_synchronized": True,
            "seal_control_acknowledged": True,
            "final_control_disabled": True,
            "double_read_stable": True,
            "integrity": "ok",
            "entry_count": 12,
            "episode_count": 1,
            "dropped_entries": 0,
            "verdict": "CROSS_SLOT_REKEY_OBSERVED",
            "first_missing_stage": "none",
        },
        "radio_cycle": {
            "requested_cycles": 1,
            "connection_trigger": "saved_profile_autojoin_only",
            "radio_off_observed": True,
            "radio_on_observed": True,
            "radio_recovery_attempted": False,
        },
        "local_only_raw_artifacts": {
            "trace_interface_route_and_hostapd_output_retained_local_only": True,
            "raw_output_committed": False,
        },
        "commit_safety": {
            "wireless_identity_committed": False,
            "ip_or_route_committed": False,
            "secret_material_committed": False,
            "raw_capture_committed": False,
        },
        "result": "PASS",
        "failure_phase": "none",
        "non_claims": [
            "pure SAE functionality",
            "generic Internet reachability",
            "physical-host validation",
            "proof beyond the bounded PMF/BIP and traffic gate",
        ],
    }


mode, evidence_path = sys.argv[1:]
if mode == "self-test":
    document = fixture()
    validate(document)
    document["pmf_bip"]["bounded_group_rekey_requested"] = False
    try:
        validate(document)
    except SystemExit:
        pass
    else:
        fail("self-test accepted a PASS without a bounded group rekey")
    document["result"] = "INCONCLUSIVE"
    document["failure_phase"] = "bounded-group-rekey"
    validate(document)
    document["pmf_bip"]["bounded_group_rekey_requested"] = True
    document["trace"]["verdict"] = "INITIAL_PMF_BIP_OBSERVED"
    try:
        document["result"] = "PASS"
        document["failure_phase"] = "none"
        validate(document)
    except SystemExit:
        pass
    else:
        fail("self-test accepted an initial-only trace as PASS")
    print("PASS: IWX PMF/BIP runtime evidence contract skeleton")
else:
    validate(json.loads(Path(evidence_path).read_text(encoding="utf-8")))
    print("PASS: IWX PMF/BIP runtime evidence contract")
PY
