#!/usr/bin/env bash
# Static contract for the Tahoe link-handoff diagnostic and premature-active fix.
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
EVALUATOR="$ROOT/scripts/evaluate_tahoe_link_handoff.py"

[ -f "$EVALUATOR" ] || {
    echo "FAIL: missing Tahoe link-handoff evaluator" >&2
    exit 1
}
python3 "$EVALUATOR" --self-test

python3 - "$ROOT" <<'PY'
from __future__ import annotations

import json
import sys
from pathlib import Path


root = Path(sys.argv[1])
header = (root / "include/ClientKit/AirportItlwmRegDiag.h").read_text()
infra = (root / "include/Airport/IO80211InfraInterface.h").read_text()
diag_hpp = (root / "AirportItlwm/AirportItlwmRegDiag.hpp").read_text()
v2 = (root / "AirportItlwm/AirportItlwmV2.cpp").read_text()
skywalk = (root / "AirportItlwm/AirportItlwmSkywalkInterface.cpp").read_text()
client = (root / "AirportItlwmRegDiag/airport_itlwm_regdiag.c").read_text()
evaluator = (root / "scripts/evaluate_tahoe_link_handoff.py").read_text()
plan = (root / "analysis/TAHOE_LINK_HANDOFF_DIAGNOSTIC_PLAN_2026-07-20.md").read_text()
parent_result_doc = (root / "analysis/"
                     "TAHOE_LINK_STATE_PARENT_RESULT_NORMALIZATION_2026-07-20.md").read_text()
release_582_doc = (root / "analysis/"
                   "TAHOE_RELEASE_5827179_KEXT_VERIFICATION_2026-07-20.md").read_text()


def fail(message: str) -> None:
    raise SystemExit(f"Tahoe link-handoff contract: {message}")


def require(text: str, token: str, label: str) -> None:
    if token not in text:
        fail(f"missing {label}: {token}")


def forbid(text: str, token: str, label: str) -> None:
    if token in text:
        fail(f"forbidden {label}: {token}")


def ordered(text: str, label: str, *tokens: str) -> None:
    cursor = 0
    for token in tokens:
        position = text.find(token, cursor)
        if position < 0:
            fail(f"missing ordered {label}: {token}")
        cursor = position + len(token)


def body(text: str, marker: str, end: str) -> str:
    start = text.find(marker)
    if start < 0:
        fail(f"missing method marker: {marker}")
    finish = text.find(end, start + len(marker))
    if finish < 0:
        fail(f"missing method end marker: {end}")
    return text[start:finish]


for token in (
    "kAirportItlwmRegDiagTraceLinkStatus",
    "kAirportItlwmRegDiagTraceLinkPublish",
    "kAirportItlwmRegDiagTraceWclJoinAbort",
    "kAirportItlwmRegDiagLinkStatusSame",
    "kAirportItlwmRegDiagLinkStatusApplied",
    "kAirportItlwmRegDiagLinkPublishQueued",
    "kAirportItlwmRegDiagLinkPublishOffGateRejected",
    "kAirportItlwmRegDiagJoinAbortEnter",
    "kAirportItlwmRegDiagJoinAbortExit",
    "AIRPORT_ITLWM_REGDIAG_LINK_STATE_PARENT_ACCEPTED_UNAVAILABLE",
):
    require(header, token, "redaction-safe link diagnostic ABI")
for token in (
    "airportItlwmRegDiagRecordLinkStatus",
    "airportItlwmRegDiagRecordLinkPublish",
    "airportItlwmRegDiagRecordJoinAbort",
):
    require(diag_hpp, token, "link diagnostic declaration")
    require(v2, token, "link diagnostic implementation/callsite")

link_status = body(v2, "bool AirportItlwm::\nsetLinkStatus", "IOReturn AirportItlwm::\nsetLinkStateGated")
for token in (
    "const UInt32 previousStatus = currentStatus",
    "kAirportItlwmRegDiagLinkStatusSame",
    "kAirportItlwmRegDiagLinkStatusLifecycleRejected",
    "kAirportItlwmRegDiagLinkStatusApplied",
    "queueOffGateLinkStatePublish(this, kIO80211NetworkLinkUp, 0)",
):
    require(link_status, token, "controller link-status branch coverage")

queue = body(v2, "static void queueOffGateLinkStatePublish", "// Drain and release the off-gate")
for token in (
    "kAirportItlwmRegDiagLinkPublishSourceUnavailable",
    "kAirportItlwmRegDiagLinkPublishQueued",
    "source->interruptOccurred(0, 0, 0)",
):
    require(queue, token, "off-gate publication producer")
gated = body(v2, "IOReturn AirportItlwm::\nsetLinkStateGated", "#if defined(__PRIVATE_SPI__) && __IO80211_TARGET < __MAC_26_0")
for token in (
    "kAirportItlwmRegDiagLinkPublishActionUnavailable",
    "kAirportItlwmRegDiagLinkPublishOffGateRejected",
    "kAirportItlwmRegDiagLinkPublishPublished",
    "onThreadPred == 1 && inGatePred == 0",
):
    require(gated, token, "off-gate publication consumer")

# Tahoe's parent entry point is a bool ABI.  The gate callback itself remains
# IOReturn, so accepted=true must normalize to success=0 and rejected=false to
# a non-zero IOReturn without altering the legacy target branch or forcing a
# parent-false runtime transition.
require(infra, "virtual bool setLinkState(IO80211LinkState,UInt,bool,UInt,UInt);",
        "Tahoe parent bool setLinkState ABI")
tahoe_gated = gated.split("#if __IO80211_TARGET >= __MAC_26_0", 1)[1].split("#else", 1)[0]
ordered(tahoe_gated, "Tahoe bool-to-IOReturn normalization",
        "const bool linkTransitionAccepted =",
        "((IO80211InfraInterface *)that->fNetIf)->setLinkState(",
        "const IOReturn ret = linkTransitionAccepted ? kIOReturnSuccess",
        ": kIOReturnError;",
        "kAirportItlwmRegDiagLinkPublishPublished")
for token in (
    "linkTransitionAccepted ? 1U : 0U",
    "sRegDiag.snapshot.lastLinkStateResult = static_cast<int32_t>(ret)",
    "AIRPORT_ITLWM_REGDIAG_LINK_STATE_PARENT_ACCEPTED_UNAVAILABLE",
):
    require(gated, token, "normalized Tahoe link-state diagnostic")
forbid(tahoe_gated,
       "IOReturn ret = ((IO80211InfraInterface *)that->fNetIf)->setLinkState(",
       "Tahoe bool assigned directly to IOReturn")
forbid(tahoe_gated, "if (ret == kIOReturnSuccess) RT_SET(14);",
       "Tahoe accepted-transition runtime bit derived from IOReturn")
require(gated, "if (linkTransitionAccepted) RT_SET(14);",
        "Tahoe accepted-transition runtime bit")
for token in (
    'parent_accepted=%" PRIu64',
    "parent_accepted=n/a",
    "AIRPORT_ITLWM_REGDIAG_LINK_STATE_PARENT_ACCEPTED_UNAVAILABLE",
):
    require(client, token, "RegDiag target-honest parent-bool rendering")
parent_result_evidence = json.loads((root / "evidence/runtime/"
                                    "tahoe_link_state_parent_result_eb5fdb5.json").read_text())
if parent_result_evidence.get("schema_version") != "itlwm-tahoe-link-state-parent-result/v1":
    fail("parent-result evidence schema mismatch")
if parent_result_evidence.get("provenance") != {
    "bootkc_sha256": "eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d",
    "guest_os_build": "25C56",
    "source_commit": "eb5fdb530ef6493ecb2e0fa35efd5f7d88ecbd96",
}:
    fail("parent-result evidence provenance mismatch")
for key in (
    "parent_true_maps_to_ioreturn_success",
    "parent_false_maps_to_ioreturn_error",
    "rt14_follows_raw_parent_bool",
    "pre_tahoe_parent_accepted_is_na",
):
    if parent_result_evidence.get("static_semantics", {}).get(key) is not True:
        fail(f"parent-result evidence missing static semantic: {key}")
for key, value in parent_result_evidence.get("runtime_observation", {}).items():
    if value is not False:
        fail(f"parent-result evidence overclaims runtime observation: {key}")
for key, value in parent_result_evidence.get("non_claims", {}).items():
    if value is not False:
        fail(f"parent-result evidence broadens execution scope: {key}")
if parent_result_evidence.get("build_admission", {}).get("all_undefined_symbols_resolved") != 958:
    fail("parent-result evidence unresolved-symbol count mismatch")
for token in (
    "Successful, bounded scenario",
    "not a parent-false runtime observation",
    "This record is not a Wi-Fi, SAE, PMF",
    "credential-free record",
):
    require(parent_result_doc, token, "parent-result evidence document")
for token in (
    "v2.4.0-alpha-5827179",
    "29786593288",
    "AirportItlwm.kext/Contents/Info.plist",
    "AirportItlwm.kext/Contents/MacOS/AirportItlwm",
    "no functional Wi-Fi claim",
):
    require(release_582_doc, token, "release kext verification document")

join_abort = body(skywalk, "setWCL_JOIN_ABORT(apple80211_wcl_abort_join *data)", "IOReturn AirportItlwmSkywalkInterface::\nsetWCL_QOS_PARAMS")
for token in (
    "kAirportItlwmRegDiagJoinAbortEnter",
    "kAirportItlwmRegDiagJoinAbortExit",
    "ieee80211_new_state(ic, IEEE80211_S_SCAN, -1)",
):
    require(join_abort, token, "join-abort timeline")

enable = body(skywalk, "setInterfaceEnable(bool enable)", "#else\nbool AirportItlwmSkywalkInterface::\ninit")
require(enable, "IO80211InfraInterface::setInterfaceEnable(enable)", "base interface enable")
require(enable, "reserve the visible link-up edge for", "aliased low-latency boundary")
forbid(enable, "(void)reportLinkStatus(3", "premature active low-latency alias")
forbid(enable, "(void)IO80211InfraInterface::setLinkState(", "premature infra link-up alias")

for token in (
    'return "PREMATURE_ACTIVE_SHORT_CIRCUIT"',
    'return "LINK_PUBLICATION_PROGRESS"',
    'return "LINK_PUBLICATION_INCOMPLETE"',
    'return "DIAGNOSTIC_INCOMPLETE"',
    "SSID", "BSSID", "pointer", "key data",
):
    require(evaluator, token, "structural link-handoff evaluator")
for token in (
    'return "link-status"', 'return "link-publish"', 'return "join-abort"',
    "link_status_decision_name", "link_publish_decision_name",
    "join_abort_phase_name",
):
    require(client, token, "RegDiag client rendering")
for token in (
    "PREMATURE_ACTIVE_SHORT_CIRCUIT",
    "LINK_PUBLICATION_PROGRESS",
    "None is an association, authentication, EAPOL, DHCP, ping, or traffic PASS",
    "Only after that candidate is released",
):
    require(plan, token, "versioned link-handoff plan boundary")

print("PASS: Tahoe link-handoff diagnostic source contract")
PY
