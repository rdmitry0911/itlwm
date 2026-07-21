#!/usr/bin/env bash
# Admission contract for the Tahoe WCL external-PMK -> SCAN resume edge.
#
# This is intentionally a source and pure-unit test only.  It proves that the
# repair resumes the ordinary net80211 scan pipeline after its paired PLTI
# wait observes PMK readiness; it neither claims nor exercises pure SAE/PMF
# support.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

bash "$root/scripts/test_payload_builders.sh"

python3 - "$root" <<'PY'
from pathlib import Path
import sys


root = Path(sys.argv[1])
contracts = (root / "AirportItlwm/TahoeExternalPmkScanResumeContracts.hpp").read_text()
auth = (root / "AirportItlwm/TahoeAssociationAuthContracts.hpp").read_text()
sky = (root / "AirportItlwm/AirportItlwmSkywalkInterface.cpp").read_text()
sky_header = (root / "AirportItlwm/AirportItlwmSkywalkInterface.hpp").read_text()
iwx = (root / "itlwm/hal_iwx/ItlIwx.cpp").read_text()
node = (root / "itl80211/openbsd/net80211/ieee80211_node.c").read_text()
layer_runner = (root / "scripts/run_tahoe_sae_quarantine_layer.sh").read_text()
sae_gate = (root / "scripts/test_tahoe_sae_quarantine_contract.sh").read_text()


def fail(message):
    raise SystemExit(f"WCL PLTI scan-resume contract: {message}")


def require(text, needle, label):
    if needle not in text:
        fail(f"missing {label}: {needle}")


def forbid(text, needle, label):
    if needle in text:
        fail(f"unexpected {label}: {needle}")


def ordered(text, label, *needles):
    cursor = 0
    for needle in needles:
        pos = text.find(needle, cursor)
        if pos < 0:
            fail(f"{label} missing ordered token: {needle}")
        cursor = pos + len(needle)


def body(text, marker, label):
    start = text.find(marker)
    if start < 0:
        fail(f"missing {label}")
    opening = text.find("{", start)
    if opening < 0:
        fail(f"missing body for {label}")
    depth = 0
    for pos in range(opening, len(text)):
        if text[pos] == "{":
            depth += 1
        elif text[pos] == "}":
            depth -= 1
            if depth == 0:
                return text[opening + 1:pos]
    fail(f"unterminated {label}")


predicate = body(
    contracts, "constexpr bool shouldResumeScanAfterExternalPmk",
    "pure scan-resume predicate")
for token in (
        "facts.pskPmkPolicyAllowed",
        "facts.associationAccepted",
        "facts.observedExternalPmkReady",
        "facts.stateIsScan",
        "facts.pskFlagSet",
        "!facts.externalPmkOwner",
):
    require(predicate, token, "complete fail-closed predicate")
forbid(predicate, "IEEE80211_S_AUTH", "direct authentication transition")
forbid(predicate, "ieee80211_node_join_bss", "direct BSS selection")
forbid(predicate, "IEEE80211_SEND_MGMT", "direct management-frame send")

require(sky_header, "bool *externalPmkReadyObserved",
        "optional caller-local PMK-ready output")
associate = body(
    sky, "IOReturn AirportItlwmSkywalkInterface::associateSSID",
    "Skywalk associateSSID")
ordered(associate, "current PMK readiness handoff",
        "*externalPmkReadyObserved = false;", "waitForExternalPmkReady",
        "*externalPmkReadyObserved = pmkReady;")
require(associate, "Control-flow result only: never expose PMK material",
        "non-secret PMK-ready output boundary")

hidden_assoc = body(
    sky, "IOReturn AirportItlwmSkywalkInterface::\nsetWCL_ASSOCIATEImpl",
    "WCL association ingress")
ordered(hidden_assoc, "WCL PMK scan-resume ordering",
        "requiresUnsupportedWpa3Auth", "return kIOReturnUnsupported;",
        "bool externalPmkReadyObserved = false;",
        "&externalPmkReadyObserved);",
        "TahoeExternalPmkScanResumeContracts::Facts scanResumeFacts",
        "shouldResumeScanAfterExternalPmk(scanResumeFacts)",
        "ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);")
require(hidden_assoc, "TahoeAssociationAuthContracts::mayUseLocalPskPmk(auth_upper)",
        "exact existing PLTI PSK policy at scan-resume edge")
resume_start = hidden_assoc.find(
    "const TahoeExternalPmkScanResumeContracts::Facts scanResumeFacts")
resume_end = hidden_assoc.find("airportItlwmRegDiagRecordAssoc", resume_start)
if resume_start < 0 or resume_end < 0:
    fail("missing bounded WCL scan-resume block")
resume = hidden_assoc[resume_start:resume_end]
for token in (
        "ieee80211_node_choose_bss",
        "ieee80211_node_join_bss",
        "IEEE80211_S_AUTH",
        "IEEE80211_SEND_MGMT",
        "iwx_auth(",
):
    forbid(resume, token, "unsafe shortcut in WCL scan-resume block")
if resume.count("ieee80211_new_state(") != 1:
    fail("WCL scan-resume block must contain exactly one normal state request")
require(resume, "PMK_READY_SCAN_RESUME", "credential-safe local progress marker")

# Preserve the two lower-layer semantics that make SCAN->SCAN safe: IWX
# coalesces an active scan and restarts an inactive one; net80211 still holds
# an empty AUTO_JOIN scan for airportd instead of selecting a random BSS.
iwx_newstate = body(iwx, "void ItlIwx::\niwx_newstate_task(void *psc)", "IWX newstate task")
ordered(iwx_newstate, "IWX SCAN->SCAN preservation",
        "if (ostate == IEEE80211_S_SCAN)",
        "if (nstate == ostate)",
        "IWX_FLAG_SCANNING", "goto next_scan", "iwx_scan(sc)")
end_scan = body(node, "void\nieee80211_end_scan", "net80211 end_scan")
ordered(end_scan, "Apple AUTO_JOIN empty-ESS hold",
        "IEEE80211_F_AUTO_JOIN", "ic->ic_des_esslen == 0", "return;",
        "ieee80211_node_choose_bss")

# Pure SAE remains a reject-only path.  The resume predicate only consumes
# the pre-existing exact PLTI policy, so it cannot reopen an SAE carrier.
require(auth, "kAuthWpa3Sae | kAuthWpa2Psk",
        "sole audited WPA3 transition selector")
require(auth, "return (authtypeUpper & kWpa3OnlyAuthMask) != 0 &&",
        "pure WPA3 rejection predicate")
require(auth, "return isAuditedWpa3PskTransition(authtypeUpper) ||",
        "exact existing PMK allow-list")
require(sae_gate, "test_tahoe_wcl_plti_scan_resume_contract.sh",
        "SAE/quarantine aggregate includes WCL resume contract")
require(layer_runner, "test_tahoe_sae_quarantine_contract.sh",
        "isolated Tahoe layer runner includes aggregate contract")

print("WCL PLTI scan-resume contract ok")
PY
