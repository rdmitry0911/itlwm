#!/usr/bin/env bash
# Static contract for Tahoe's passive end-to-end link owner-context census.
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
EVALUATOR="$ROOT/scripts/evaluate_tahoe_link_context_census.py"
PARENT_EVALUATOR="$ROOT/scripts/evaluate_tahoe_link_parent_attestation.py"

[ -f "$EVALUATOR" ] || {
    echo "FAIL: missing Tahoe link-context census evaluator" >&2
    exit 1
}
python3 "$EVALUATOR" --self-test
[ -f "$PARENT_EVALUATOR" ] || {
    echo "FAIL: missing Tahoe parent link-state attestation evaluator" >&2
    exit 1
}
python3 "$PARENT_EVALUATOR" --self-test

python3 - "$ROOT" <<'PY'
from __future__ import annotations

import json
import sys
from pathlib import Path


root = Path(sys.argv[1])
header = (root / "include/ClientKit/AirportItlwmRegDiag.h").read_text()
bridge = (root / "include/ClientKit/AirportItlwmRegDiagBridge.h").read_text()
proto = (root / "itl80211/openbsd/net80211/ieee80211_proto.c").read_text()
v2 = (root / "AirportItlwm/AirportItlwmV2.cpp").read_text()
v2_hpp = (root / "AirportItlwm/AirportItlwmV2.hpp").read_text()
skywalk = (root / "AirportItlwm/AirportItlwmSkywalkInterface.cpp").read_text()
diag_hpp = (root / "AirportItlwm/AirportItlwmRegDiag.hpp").read_text()
client = (root / "AirportItlwmRegDiag/airport_itlwm_regdiag.c").read_text()
evaluator = (root / "scripts/evaluate_tahoe_link_context_census.py").read_text()
parent_evaluator = (root / "scripts/evaluate_tahoe_link_parent_attestation.py").read_text()
evidence = json.loads((root / "evidence/runtime/"
                       "tahoe_link_context_census_ad3646a.json").read_text())
evidence_doc = (root / "analysis/"
                "TAHOE_LINK_CONTEXT_CENSUS_BUILD_2026-07-21.md").read_text()
release_doc = (root / "analysis/"
               "TAHOE_RELEASE_FE398B8_KEXT_VERIFICATION_2026-07-21.md").read_text()


def fail(message: str) -> None:
    raise SystemExit(f"Tahoe link-context census contract: {message}")


def require(text: str, token: str, label: str) -> None:
    if token not in text:
        fail(f"missing {label}: {token}")


def forbid(text: str, token: str, label: str) -> None:
    if token in text:
        fail(f"forbidden {label}: {token}")


def body(text: str, marker: str, end: str) -> str:
    start = text.find(marker)
    if start < 0:
        fail(f"missing method marker: {marker}")
    finish = text.find(end, start + len(marker))
    if finish < 0:
        fail(f"missing method end marker: {end}")
    return text[start:finish]


for token in (
    "kAirportItlwmRegDiagModeLinkContext",
    "kAirportItlwmRegDiagTraceLinkContext",
    "kAirportItlwmRegDiagLinkContextNet80211Bridge",
    "kAirportItlwmRegDiagLinkContextControllerStatus",
    "kAirportItlwmRegDiagLinkContextPublishQueue",
    "kAirportItlwmRegDiagLinkContextPublishAction",
    "kAirportItlwmRegDiagLinkContextGate",
    "kAirportItlwmRegDiagLinkContextSkywalkParent",
    "kAirportItlwmRegDiagLinkContextWclUpdate",
    "AIRPORT_ITLWM_REGDIAG_LINK_CONTEXT_EPOCH_CURRENT",
    "AIRPORT_ITLWM_REGDIAG_LINK_CONTEXT_ON_DISPATCH_MASK",
):
    require(header, token, "ABI-compatible passive census declaration")
for token in (
    "AirportItlwmRegDiagNet80211LinkContext",
    "must never publish a link state",
):
    require(bridge, token, "C bridge contract")
for forbidden in ("SSID", "BSSID", "setLinkState", "setLinkStatus"):
    forbid(bridge, forbidden, "sensitive or behavioral C bridge surface")

link_bridge = body(proto, "void\nieee80211_set_link_state", "//\tif (nstate != ifp->if_link_state)")
for token in (
    "#include <ClientKit/AirportItlwmRegDiagBridge.h>",
    "AirportItlwmRegDiagNet80211LinkContext(",
    "ieee80211_pae_assoc_epoch_current(ic)",
    "ifp->controller->setLinkStatus",
):
    require(proto if token.startswith("#include") else link_bridge, token,
            "net80211 bridge marker")
if link_bridge.index("AirportItlwmRegDiagNet80211LinkContext(") > \
        link_bridge.index("ifp->controller->setLinkStatus"):
    fail("net80211 marker must precede the unchanged controller bridge")

for token in (
    "currentTahoeAssociationEpoch",
    "recordTahoeLinkContext",
    "airportItlwmRegDiagRecordLinkContext",
    "AirportItlwmRegDiagNet80211LinkContext",
):
    require(v2_hpp if token in {"currentTahoeAssociationEpoch", "recordTahoeLinkContext"}
            else v2, token, "controller census helper")
record = body(v2, "void\nAirportItlwm::recordTahoeLinkContext",
              "extern \"C\" void\nAirportItlwmRegDiagNet80211LinkContext")
for token in (
    "if (!airportItlwmRegDiagShouldRecordLinkContext())",
    "currentTahoeAssociationEpoch()",
    "workLoop->onThread()",
    "workLoop->inGate()",
):
    require(record, token, "opt-in owner snapshot")
for forbidden in ("setLinkState(", "setLinkStatus(", "runAction(",
                  "interruptOccurred(", "retain("):
    forbid(record, forbidden, "behavioral recorder action")
c_bridge = body(v2, "extern \"C\" void\nAirportItlwmRegDiagNet80211LinkContext",
                "#endif\n\nvoid\nairportItlwmRegDiagRecordJoinAbort")
for forbidden in ("setLinkState(", "setLinkStatus(", "runAction(",
                  "interruptOccurred(", "retain(", "OSDynamicCast(",
                  "getWorkLoop(", "get80211Controller("):
    forbid(c_bridge, forbidden, "behavioral net80211 bridge action")
bridge_tail = """kAirportItlwmRegDiagLinkContextLifecycleUnknown, assocEpoch,
        -1, -1, -1, kIOReturnSuccess);"""
require(c_bridge, bridge_tail, "net80211 bridge recorder argument order")
bridge_call = c_bridge[c_bridge.index("airportItlwmRegDiagRecordLinkContext("):]
if bridge_call.index("assocEpoch") > bridge_call.index("kIOReturnSuccess"):
    fail("net80211 bridge must pass sampled epoch before recorder result")

for marker in (
    "kAirportItlwmRegDiagLinkContextControllerStatus",
    "kAirportItlwmRegDiagLinkContextPublishQueue",
    "kAirportItlwmRegDiagLinkContextPublishAction",
    "kAirportItlwmRegDiagLinkContextGate",
):
    require(v2, marker, "mandatory controller-chain site")
for marker in (
    "kAirportItlwmRegDiagLinkContextSkywalkParent",
    "kAirportItlwmRegDiagLinkContextWclUpdate",
    "AIRPORT_ITLWM_REQUIRE_INTERNAL_BOOL_OPERATION();\n#if __IO80211_TARGET >= __MAC_26_0\n    const bool traceLinkContext",
    "AIRPORT_ITLWM_REQUIRE_INTERNAL_OPERATION();\n#if __IO80211_TARGET >= __MAC_26_0\n    const bool traceLinkContext",
):
    require(skywalk, marker, "Skywalk passive contrast site")

for token in (
    "link_context_route_name",
    "link_context_stage_name",
    "link_context_predicate_name",
    'return "link-context"',
    "link-context-on",
    '"context", (char *)"1"',
):
    require(client, token, "credential-safe client decoder/control")
for token in (
    "OWNER_CONTEXT_MAIN_CHAIN_SAFE",
    "OWNER_CONTEXT_GATE_HELD",
    "OWNER_CONTEXT_CENSUS_INCOMPLETE",
    "OWNER_CONTEXT_CENSUS_TRACE_TRUNCATED",
    "trace_dropped",
    "BSD_LINK_STATE_UP",
    "IO80211_LINK_STATE_UP",
    "scope=execution-context only; no functional network claim",
    "SSIDs, BSSIDs, pointers, packets, or keys",
):
    require(evaluator, token, "census evaluator boundary")
for token in (
    "TAHOE_PARENT_LINK_UP_ACCEPTED",
    "TAHOE_PARENT_LINK_UP_REJECTED",
    "TAHOE_PARENT_LINK_UP_UNATTESTED",
    "TAHOE_PARENT_LINK_UP_NOT_OBSERVED",
    "TAHOE_PARENT_LINK_UP_TRACE_TRUNCATED",
    "TAHOE_PARENT_LINK_UP_CAPTURE_INCOMPLETE",
    "parent_accepted",
    "scope=parent bool only; no functional network claim",
):
    require(parent_evaluator, token, "independent parent attestation evaluator")

if evidence.get("schema_version") != "itlwm-tahoe-link-context-census/v1":
    fail("link-context evidence schema mismatch")
if evidence.get("provenance") != {
    "bootkc_sha256": "eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d",
    "guest_os_build": "25C56",
    "source_commit": "ad3646ab09316b2c6d26f8f63d956705aefe0bb3",
}:
    fail("link-context evidence provenance mismatch")
if evidence.get("build_admission", {}).get("all_undefined_symbols_resolved") != 959:
    fail("link-context evidence unresolved-symbol count mismatch")
for key in (
    "agent_clean_build_passed",
    "appstore_iobuiltin_contract_passed",
    "kext_debug_build_passed",
    "link_context_contract_passed",
    "regdiag_build_passed",
    "sae_quarantine_contract_passed",
    "thread_call_cancel_wait_absent",
):
    if evidence.get("build_admission", {}).get(key) is not True:
        fail(f"link-context evidence missing build admission: {key}")
for section in ("runtime_observation", "non_claims", "commit_safety"):
    for key, value in evidence.get(section, {}).items():
        if value is not False:
            fail(f"link-context evidence overclaims {section}: {key}")
for key, value in evidence.get("static_semantics", {}).items():
    if value is not True:
        fail(f"link-context evidence missing static semantic: {key}")
for key, value in evidence.get("verdict", {}).items():
    if value is not True:
        fail(f"link-context evidence missing verdict: {key}")
for token in (
    "Successful, bounded scenario",
    "all 959 undefined symbols",
    "does not claim that context tracing was enabled at runtime",
    "raw trace",
    "route snapshots are not committed",
):
    require(evidence_doc, token, "link-context successful scenario document")
for token in (
    "v2.4.0-alpha-fe398b8",
    "29787781829",
    "28d8cc4487a16a3bc7bc05002241e7fb4feb95207beb42e195b305a976d4ab08",
    "AirportItlwm.kext/Contents/Info.plist",
    "AirportItlwm.kext/Contents/MacOS/AirportItlwm",
    "functional Wi-Fi scenario was run",
):
    require(release_doc, token, "fe398b8 kext release verification document")

print("PASS: Tahoe passive link owner-context census source contract")
PY
