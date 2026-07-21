#!/usr/bin/env bash
# Validate the aggregate-only private-candidate runtime record for 2ebe2d1.
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
EVIDENCE="$ROOT/evidence/runtime/tahoe_a2df_2ebe2d1_four_cycle.json"
RECORD="$ROOT/docs/TAHOE_RUNTIME_2EBE2D1.md"

python3 - "$EVIDENCE" "$RECORD" <<'PY'
import json
import re
import sys
from pathlib import Path


def fail(message):
    raise SystemExit("FAIL: 2ebe2d1 runtime evidence contract: " + message)


def require(condition, message):
    if not condition:
        fail(message)


evidence_path, record_path = map(Path, sys.argv[1:])
document = json.loads(evidence_path.read_text(encoding="utf-8"))
record = record_path.read_text(encoding="utf-8")

require(document.get("schema_version") ==
        "itlwm-tahoe-private-candidate-runtime/v1",
        "unexpected schema")

candidate = document.get("candidate")
require(candidate == {
    "activated_candidate_sha256":
        "0b4c89517e7ffb856290eed57e2e2af406e5f6562fbe0a7bcdb443d57601fd9d",
    "macho_uuid": "05CF8083-DBC4-3F82-BB6D-5AB203535348",
    "post_boot_installed_to_candidate_match": True,
    "post_boot_loaded_to_candidate_match": True,
    "source_candidate_sha256":
        "0b4c89517e7ffb856290eed57e2e2af406e5f6562fbe0a7bcdb443d57601fd9d",
    "source_commit": "2ebe2d1657936209a465cf3ff09c8e77db918210",
    "source_to_private_preflight_match": True,
}, "candidate identity binding is incomplete")
require(re.fullmatch(r"[0-9a-f]{40}", candidate["source_commit"]) is not None,
        "source commit format")
for key in ("source_candidate_sha256", "activated_candidate_sha256"):
    require(re.fullmatch(r"[0-9a-f]{64}", candidate[key]) is not None,
            "candidate digest format")
require(re.fullmatch(r"[0-9A-F]{8}(?:-[0-9A-F]{4}){3}-[0-9A-F]{12}",
                     candidate["macho_uuid"]) is not None,
        "candidate UUID format")

require(document.get("auxkc") == {
    "activation_state": "READY_FOR_GUEST_REBOOT",
    "approved_member_count": 5,
    "canonical_member_set_after_activation": "PASS",
    "canonical_preflight_mutation": "none",
    "direct_load_or_unload": False,
    "private_admission_result": "PASS",
}, "private AuxKC boundary changed")
require(document.get("scope") == {
    "environment": "pinned_disposable_qemu_guest",
    "guest_only_reboot": True,
    "physical_host_rebooted": False,
    "physical_host_touched": False,
    "remote_validation_host_touched": False,
}, "runtime scope changed")

baseline = document.get("a2df_baseline_control")
require(baseline == {
    "connection_trigger": "saved_profile_autojoin_only",
    "cycle_count": 4,
    "cycle_pass_count": 4,
    "dhcp_textual_observation": "COMPLETE",
    "explicit_address_command": False,
    "explicit_dhcp_state_mutating_command": False,
    "explicit_join_command": False,
    "explicit_route_command": False,
    "four_cycle_result": "PASS",
    "fresh_association_epochs": 4,
    "ping_packets_per_cycle": 5,
    "radio_off_observed_cycles": 4,
    "radio_on_observed_cycles": 4,
    "route_and_address_invariants": "PASS",
    "stable_authorization_cycles": 4,
    "total_ping_packets_received": 20,
    "total_ping_packets_transmitted": 20,
}, "four-cycle baseline is incomplete")

require(document.get("runtime_execution") == {
    "iwx_q0_observer_exercised": False,
    "iwn_ordered_trace_entries": 33,
    "iwn_ordered_trace_verdict": "KERNEL_CHAIN_OBSERVED",
    "iwn_trace_dropped_entries": 0,
    "post_plti_backend": "iwn",
}, "runtime backend/evidence boundary changed")
require(document.get("non_claims") == {
    "eapol_exchange_verified": False,
    "generic_internet_reachability_verified": False,
    "igtk_installation_verified": False,
    "iwx_q0_observer_runtime_verified": False,
    "physical_host_validation": False,
    "pmf_q0_completion_verified": False,
    "pmf_required_association_verified": False,
    "pure_sae_verified": False,
    "remote_validation_host_validation": False,
    "wcl_link_publication_verified": False,
    "wpa3_sae_verified": False,
}, "non-claims are incomplete")
require(document.get("release") == {
    "asset_or_tag_created": False,
    "semantic_release_mutated": False,
}, "intermediate candidate mutated a semantic release")
require(document.get("commit_safety") == {
    "ip_or_route_committed": False,
    "raw_capture_committed": False,
    "secret_material_committed": False,
    "wireless_identity_committed": False,
}, "committed aggregate is not sanitized")
raw = document.get("local_only_raw_artifacts")
require(raw == {
    "retained_local_only": True,
    "summary_sha256":
        "2d1de1b4d90568539be8aed338d13e9bc5feb55f5a0a823136ece4dbcc9c30b4",
}, "local-only raw-artifact boundary changed")

for needle in (
    "No semantic release tag or asset was",
    "created or changed.",
    "The execution backend was IWN",
    "not exercise the IWX q0 observer",
    "This is only a saved-profile A2DF regression result.",
):
    require(needle in record, "runtime record omitted boundary: " + needle)

for name, text in (("evidence", json.dumps(document, sort_keys=True)),
                   ("record", record)):
    require(re.search(r"\b(?:\d{1,3}\.){3}\d{1,3}\b", text) is None,
            name + " contains a literal IPv4 address")
    require(re.search(r"(?i)\b(?:[0-9a-f]{2}:){5}[0-9a-f]{2}\b", text) is None,
            name + " contains a literal MAC address")

print("PASS: 2ebe2d1 aggregate-only private-candidate runtime evidence")
PY
