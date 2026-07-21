#!/usr/bin/env bash
# Validate the aggregate-only, provenance-bound 5e2f70a guest runtime record.
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
EVIDENCE="$ROOT/evidence/runtime/tahoe_a2df_5e2f70a_four_cycle.json"
RECORD="$ROOT/docs/TAHOE_RUNTIME_5E2F70A.md"

python3 - "$EVIDENCE" "$RECORD" <<'PY'
import json
import re
import sys
from pathlib import Path


def fail(message):
    raise SystemExit("FAIL: 5e2f70a runtime evidence contract: " + message)


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
        "d69bd5f028f46c8a76968d30d0a7a157f094136953a77dd9edc4a12027cbed6e",
    "macho_uuid": "034BABDD-BFDF-316F-B547-122C74246607",
    "post_boot_installed_to_candidate_match": True,
    "post_boot_loaded_to_candidate_match": True,
    "source_candidate_sha256":
        "d69bd5f028f46c8a76968d30d0a7a157f094136953a77dd9edc4a12027cbed6e",
    "source_commit": "5e2f70a52eaa53b6c2633e95af76452dfe3e3774",
    "source_identity_sha256":
        "05b089d66b36efaea28c4cd3f33ca10fde1437a5f139feb77fe6e4e9b9c73b4b",
    "source_to_private_preflight_match": True,
}, "candidate identity binding is incomplete")
require(re.fullmatch(r"[0-9a-f]{40}", candidate["source_commit"]) is not None,
        "source commit format")
for key in ("source_candidate_sha256", "activated_candidate_sha256",
            "source_identity_sha256"):
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

require(document.get("a2df_baseline_control") == {
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
}, "A2DF aggregate changed")
require(document.get("runtime_surface") == {
    "fenced_raw_bsd_setters_runtime_invoked": False,
    "normal_saved_profile_a2df_regression": "PASS",
    "originating_panic_reproduced": False,
    "prior_panic_fix_proven": False,
    "static_no_backend_set_fence_contract": "PASS",
}, "BSD-fence runtime scope overclaims or changed")
require(document.get("non_claims") == {
    "eapol_exchange_verified": False,
    "generic_internet_reachability_verified": False,
    "igtk_installation_verified": False,
    "physical_host_validation": False,
    "pmf_q0_completion_verified": False,
    "pmf_required_association_verified": False,
    "pure_sae_verified": False,
    "raw_bsd_setter_apple_parity_verified": False,
    "raw_bsd_user_pointer_rejection_verified": False,
    "remote_validation_host_validation": False,
    "wcl_link_publication_verified": False,
    "wpa3_sae_verified": False,
}, "non-claims changed")
require(document.get("commit_safety") == {
    "ip_or_route_committed": False,
    "raw_capture_committed": False,
    "secret_material_committed": False,
    "wireless_identity_committed": False,
}, "committed aggregate is not sanitized")
require(document.get("local_only_raw_artifacts") == {
    "retained_local_only": True,
    "summary_sha256":
        "82eb0a958e3a00a0335165a605a7cf51d3ef7466e085e89193b3ff4ff98aef34",
}, "local-only raw-artifact boundary changed")
require(document.get("release") == {
    "asset_or_tag_created": False,
    "semantic_release_mutated": False,
}, "private runtime record mutated a semantic release")

for needle in (
        "No semantic release tag",
        "or asset was created or changed.",
        "This runtime experiment did not execute either raw",
        "does not establish a fix for a prior panic.",
        "This is only a saved-profile A2DF regression result.",
):
    require(needle in record, "runtime record omitted boundary: " + needle)

for name, text in (("evidence", json.dumps(document, sort_keys=True)),
                   ("record", record)):
    require(re.search(r"\b(?:\d{1,3}\.){3}\d{1,3}\b", text) is None,
            name + " contains a literal IPv4 address")
    require(re.search(r"(?i)\b(?:[0-9a-f]{2}:){5}[0-9a-f]{2}\b", text) is None,
            name + " contains a literal MAC address")

print("PASS: 5e2f70a aggregate-only BSD-fence runtime evidence")
PY
