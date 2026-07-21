#!/usr/bin/env bash
# Verify the committed, sanitized runtime evidence for the WCL PMK-resume
# candidate.  This is a record-integrity check, not a substitute for the
# isolated QEMU experiment that produced it.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

python3 - "$root" <<'PY'
import json
from pathlib import Path
import re
import sys


root = Path(sys.argv[1])
evidence_path = root / "evidence/runtime/tahoe_wcl_plti_scan_resume_48c6e31.json"
record_path = root / "analysis/TAHOE_WCL_PLTI_SCAN_RESUME_RUNTIME_48C6E31_2026-07-21.md"
evidence = json.loads(evidence_path.read_text())
record = record_path.read_text()


def require(condition, message):
    if not condition:
        raise SystemExit(f"WCL PLTI runtime evidence contract: {message}")


require(evidence["schema"] == "itlwm-tahoe-wcl-plti-scan-resume-runtime/v1",
        "unexpected evidence schema")
candidate = evidence["candidate"]
require(candidate["commit"] == "48c6e31a0e19c766e81db29a78f7eaabf244765e",
        "candidate commit is not exact")
require(candidate["release_tag"] == "v2.4.0-alpha-48c6e31",
        "candidate release tag is not exact")
for key in ("release_prerelease", "archive_required_members_verified",
            "bundle_identifier_verified", "iobuiltin_false_verified"):
    require(candidate[key] is True, f"candidate verification is missing: {key}")
for key in ("release_archive_sha256", "binary_sha256"):
    require(re.fullmatch(r"[0-9a-f]{64}", candidate[key]) is not None,
            f"candidate digest is malformed: {key}")
require(re.fullmatch(r"[0-9A-F]{8}(?:-[0-9A-F]{4}){3}-[0-9A-F]{12}",
                     candidate["macho_uuid"]) is not None,
        "candidate UUID is malformed")

activation = evidence["activation"]
for key in ("private_auxkc_preflight_pass", "transactional_activation_ready",
            "guest_rebooted_only", "post_boot_installed_identity_matches_release",
            "post_a2df_loaded_identity_matches_release"):
    require(activation[key] is True, f"activation assertion is missing: {key}")
require(activation["canonical_mutation_during_preflight"] == "none",
        "preflight canonical mutation was not none")
require(activation["approved_auxkc_member_count"] == 5,
        "AuxKC member count is not exact")
require(activation["physical_validation_host_touched"] is False and
        activation["physical_host_rebooted"] is False,
        "record incorrectly attributes work to a physical host")

a2df = evidence["a2df_baseline_control"]
require(a2df["cycle_count"] == 4 and a2df["cycle_pass_count"] == 4,
        "A2DF does not retain four passing cycles")
require(a2df["four_cycle_result"] == "PASS",
        "A2DF four-cycle verdict is not PASS")
require(a2df["ipconfig_getpacket_observation"] == "COMPLETE",
        "DHCP observation is not complete")
require(a2df["route_and_address_invariants"] == "PASS",
        "route/address invariants did not pass")
for key in ("explicit_route_command", "explicit_address_command",
            "explicit_dhcp_state_mutating_command"):
    require(a2df[key] == "none", f"unexpected network mutation claim: {key}")
require(a2df["connection_trigger"] == "saved_profile_autojoin_only",
        "runtime trigger exceeded saved-profile autojoin")
require(a2df["kernel_panic_marker_count_after_run"] == 0,
        "post-run kernel panic marker was observed")

branch = evidence["branch_observation"]
require(branch["wcl_pmk_ready_scan_resume_marker_observed"] is False,
        "record must not claim that the new branch executed")
require("does not prove" in branch["interpretation"],
        "branch observation lacks its non-claim")
require("pure SAE or PMF functionality" in evidence["non_claims"],
        "pure-SAE non-claim is missing")

raw = evidence["local_only_raw_artifacts"]
require(raw["retained_local_only"] is True and
        re.fullmatch(r"[0-9a-f]{64}", raw["runner_log_sha256"]) is not None,
        "local-only raw evidence provenance is malformed")

for needle in (
        "four radio OFF/ON cycles passed",
        "five-packet laboratory data-plane check",
        "does not prove that the newly added WCL PMK-ready resume branch was entered",
        "No pure SAE/PMF claim is made",
):
    require(needle in record, f"runtime record is missing: {needle}")

# The committed record must remain shareable.  It may say that identifiers are
# redacted, but it must not carry a literal IPv4 address or MAC address.
combined = evidence_path.read_text() + "\n" + record
require(re.search(r"\b(?:[0-9]{1,3}\.){3}[0-9]{1,3}\b", combined) is None,
        "literal IPv4 address escaped into committed evidence")
require(re.search(r"(?i)\b(?:[0-9a-f]{2}:){5}[0-9a-f]{2}\b", combined) is None,
        "literal MAC address escaped into committed evidence")

print("WCL PLTI scan-resume runtime evidence contract ok")
PY
