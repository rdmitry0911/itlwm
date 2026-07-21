#!/usr/bin/env bash
# Verify the committed, aggregate-only runtime record for the bb7366b release.
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
IDENTITY="$ROOT/evidence/runtime/tahoe_lab_kext_identity_bb7366b.json"
RUNTIME="$ROOT/evidence/runtime/tahoe_lab_bb7366b_overlay_runtime.json"
APPSTORE_NOTE="$ROOT/analysis/TAHOE_APPSTORE_AMS_EN0_CONTROLLER_2026-07-20.md"
RUNTIME_NOTE="$ROOT/analysis/TAHOE_LAB_BB7366B_OVERLAY_RUNTIME_2026-07-21.md"

python3 - "$IDENTITY" "$RUNTIME" "$APPSTORE_NOTE" "$RUNTIME_NOTE" <<'PY'
import json
import pathlib
import re
import sys

identity_path, runtime_path, appstore_path, runtime_note_path = map(pathlib.Path, sys.argv[1:])
identity = json.loads(identity_path.read_text(encoding="utf-8"))
runtime = json.loads(runtime_path.read_text(encoding="utf-8"))

def require(condition, message):
    if not condition:
        raise SystemExit("FAIL: " + message)

candidate = runtime["candidate"]
expected = identity["expected_release"]
require(candidate["source_commit"] == "bb7366b5a99cadbb3b831b74acc2902f90b6a53a",
        "runtime record must name bb7366b")
require(candidate["release_tag"] == "v2.4.0-alpha-bb7366b",
        "runtime record must name exact prerelease")
for key in ("archive_sha256", "binary_sha256", "macho_uuid"):
    require(candidate[key] == expected[key], "identity and runtime candidate differ for " + key)
require(identity["candidate_binding"]["candidate_kext_bound"] is True,
        "loaded candidate binding must pass")
require(all(identity["candidate_binding"]["checks"].values()),
        "every loaded candidate identity check must pass")

a2df = runtime["a2df_radio_baseline"]
require(a2df["result"] == "PASS", "four-cycle baseline must pass")
require(a2df["completed_cycles"] == 4, "baseline must retain four cycles")
require(a2df["radio_off_observed_cycles"] == 4 and a2df["radio_on_observed_cycles"] == 4,
        "all radio transitions must be observed")
require(a2df["fresh_association_epochs"] == 4 and a2df["stable_ap_authorization_cycles"] == 4,
        "all association/authorization observations must pass")
require(a2df["total_ping_packets_transmitted"] == 20 and
        a2df["total_ping_packets_received"] == 20,
        "bounded packet result must be lossless")
require(a2df["dhcp_textual_observation"] == "COMPLETE", "DHCP observation must be complete")
require(runtime["prebaseline_public_radio_recovery"] == {
    "explicit_join_command": False,
    "radio_off_command_succeeded": True,
    "radio_on_command_succeeded": True,
    "saved_profile_address_ready": True,
}, "successful recovery must be recorded without a join claim")

topology = runtime["appstore_controller_topology"]
require(topology["topology_probe_result"] == "PASS", "topology probe must pass")
require(topology["controller_nodes"] == 1, "controller topology must be singular")
require(topology["controller_iobuiltin_false"] is True,
        "controller must remain non-built-in")
require(topology["controller_iomacaddress_length"] == 6,
        "record must retain corrected controller MAC observation")
require(topology["direct_parent_is_iopciedevicewrapper"] is True and
        topology["direct_parent_iomacaddress_absent"] is True,
        "record must distinguish the wrapper/provider from the controller")
require(topology["airport_itlwm_skywalk_six_byte_mac_nodes"] == 1,
        "Skywalk MAC observation must be retained")
require(topology["app_store_smoke_tested"] is False,
        "topology must not be misreported as an App Store smoke")

link = runtime["link_context"]
require(link["bounded_trace_window"]["result"] == "PASS", "bounded window must pass")
require(link["bounded_trace_window"]["completed_cycles"] == 1,
        "trace window must remain distinct from the baseline")
require(link["trace_dropped"] == 0 and link["trace_count"] == 38,
        "trace must be fresh and untruncated")
bridge = link["bridge_argument_order_runtime"]
require(bridge == {
    "net80211_bridge_events": 2,
    "nonzero_epoch_events": 2,
    "success_result_events": 2,
    "verdict": "PASS",
}, "bridge runtime observation must prove epoch/result encoding")
require(link["census"]["verdict"] == "OWNER_CONTEXT_GATE_HELD",
        "gate-held result must remain fail-closed")
require(link["parent_attestation"]["verdict"] == "TAHOE_PARENT_LINK_UP_NOT_OBSERVED",
        "parent result must not be inflated")
require(link["final_control"]["enabled"] is False and
        link["final_control"]["link_context_enabled"] is False,
        "diagnostic must be inert after capture")

non_claims = runtime["non_claims"]
require(not any(non_claims.values()), "all listed non-claims must stay explicit")
require(runtime["commit_safety"] == {
    "ip_or_route_committed": False,
    "raw_capture_committed": False,
    "secret_material_committed": False,
    "ssid_or_bssid_committed": False,
}, "runtime evidence must remain sanitized")

for path in (identity_path, runtime_path):
    text = path.read_text(encoding="utf-8")
    require(re.search(r"\b(?:[0-9]{1,3}\.){3}[0-9]{1,3}\b", text) is None,
            "sanitized evidence contains an IPv4 rendering")
    require("AIAMlab" not in text,
            "sanitized evidence contains a laboratory wireless identifier")
    require(re.search(r"\b(?:wlp[0-9][A-Za-z0-9_-]*|en[0-9]+)\b", text) is None,
            "sanitized evidence contains an interface identifier")

appstore_note = appstore_path.read_text(encoding="utf-8")
require("controller itself has a six-byte `IOMACAddress`" in appstore_note,
        "App Store note must correct the controller-MAC claim")
require("provider does not" in appstore_note,
        "App Store note must preserve the provider distinction")
require("controller path therefore lacks" not in appstore_note,
        "obsolete controller-MAC claim must be absent")
runtime_note = runtime_note_path.read_text(encoding="utf-8")
require("OWNER_CONTEXT_GATE_HELD" in runtime_note and
        "TAHOE_PARENT_LINK_UP_NOT_OBSERVED" in runtime_note,
        "runtime note must retain negative LinkContext results")
print("PASS: bb7366b sanitized overlay runtime evidence contract")
PY
