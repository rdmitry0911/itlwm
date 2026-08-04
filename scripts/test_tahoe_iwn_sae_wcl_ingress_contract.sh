#!/usr/bin/env bash
# Static contract for the product exact-SAE WCL ingress.  WCL parsing stays
# separate from the shared kernel direct-SAE transaction; a stricter build
# gate lets the isolated diagnostic UserClient reuse that lower half.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

python3 - "$root" <<'PY'
from pathlib import Path
import re
import sys

root = Path(sys.argv[1])
sky = (root / "AirportItlwm/AirportItlwmSkywalkInterface.cpp").read_text()
gate = (root / "AirportItlwm/IwnDirectSaeLabGate.hpp").read_text()
contracts = (root / "AirportItlwm/TahoeAssociationContracts.hpp").read_text()
owner_registry = (root / "AirportItlwm/TahoeOwnerRegistry.hpp").read_text()
iwn = (root / "itlwm/hal_iwn/ItlIwn.cpp").read_text()
iwn_var = (root / "itlwm/hal_iwn/if_iwnvar.h").read_text()
proto = (root / "itl80211/openbsd/net80211/ieee80211_proto.c").read_text()
net_var = (root / "itl80211/openbsd/net80211/ieee80211_var.h").read_text()


def fail(message):
    raise SystemExit(f"IWN exact-SAE WCL ingress contract: {message}")


def require(text, needle, label):
    if needle not in text:
        fail(f"missing {label}: {needle}")


def forbid(text, needle, label):
    if needle in text:
        fail(f"unexpected {label}: {needle}")


def body(source, marker, label):
    start = source.find(marker)
    if start < 0:
        fail(f"missing {label}")
    opening = source.find("{", start)
    if opening < 0:
        fail(f"missing body for {label}")
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
    fail(f"unterminated {label}")


def brace_body(source, opening, label):
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
    fail(f"unterminated {label}")


def ordered(text, label, *needles):
    cursor = 0
    for needle in needles:
        position = text.find(needle, cursor)
        if position < 0:
            fail(f"{label} missing ordered token: {needle}")
        cursor = position + len(needle)


for token in (
        "#include <HAL/ItlSaeDriverTarget.h>",
        "ITL_SAE_DRIVER_CRYPTO_AVAILABLE",
        "#define AIRPORT_ITLWM_IWN_SAE_WCL_INGRESS 1",
        "defined(IWN_SOFTWARE_PMF_LAB_BUILD)",
        "#define AIRPORT_ITLWM_IWN_DIRECT_SAE_LAB_STIMULUS 1",
        "AIRPORT_ITLWM_IWN_SAE_WCL_INGRESS",
):
    require(gate, token, "compile-time physical gate")
require(sky, '#include "IwnDirectSaeLabGate.hpp"', "shared gate inclusion")
require(sky, "#if AIRPORT_ITLWM_IWN_SAE_WCL_INGRESS",
        "WCL physical gate use")

for token in (
        "kWclAssociateIoucSelector = 0x1ba",
        "kWclAssociatePayloadLength = 0x6fc",
        "kWclKeyCipherTypeOffset = 0x48",
        "kWclKeyPasswordOffset = 0x50",
        "kFirstCandidateBssidOffset = 0x220",
        "kMaximumCandidateCount == 69",
):
    require(contracts, token, "direct WCL carrier identity")

association = body(sky,
    "IOReturn AirportItlwmSkywalkInterface::\nsetWCL_ASSOCIATEImpl",
    "WCL association ingress")
active_owner = body(sky,
    "tahoeHasActiveWclAssociationOwner(",
    "active JoinAdapter owner fence")
for token in (
        "ic->ic_state == IEEE80211_S_AUTH",
        "ic->ic_state == IEEE80211_S_ASSOC",
        "ic->ic_state == IEEE80211_S_RUN",
        "owner.hasCarrier", "!owner.publicCarrier",
        "owner.authAssocCompletionArmed",
        "!owner.joinTerminalObserved",
):
    require(active_owner, token, "active completion lease")
forbid(active_owner, "ieee80211_sae_wcl_request_bound_current",
       "active JoinAdapter lease must outlive SAE credential generation")
ordered(association, "active JoinAdapter fence before WCL owner replacement",
        "preserveActiveWclCompletionOwner = instance != nullptr",
        "tahoeHasActiveWclAssociationOwner(",
        "if (instance != nullptr && !preserveActiveWclCompletionOwner)",
        "if (preserveActiveWclCompletionOwner)",
        "ACTIVE_JOIN_RETAINED",
        "return kIOReturnNotReady;")
public_association = body(sky,
    "setASSOCIATE(struct apple80211_assoc_data *ad)",
    "public association ingress")
ordered(public_association,
        "public companion carrier cannot revoke active WCL owner",
        "tahoeHasActiveWclAssociationOwner(",
        "if (!preserveActiveWclCompletionOwner)",
        "if (preserveActiveWclCompletionOwner)",
        "public_assoc ACTIVE_WCL_JOIN_RETAINED",
        "return kIOReturnNotReady;",
        "ic->ic_pae_mfp_requested = 0")
branch_marker = "if (directSaeWclPassword) {"
branch_start = association.find(branch_marker)
if branch_start < 0:
    fail("missing exact-SAE WCL branch")
direct = brace_body(association, association.find("{", branch_start),
                    "exact-SAE WCL branch")
direct_code = re.sub(r"/\*.*?\*/|//[^\n]*", "", direct, flags=re.S)
for token in ("const bool directSaeWclPassword =",
              "wcl_key_cipher == APPLE80211_CIPHER_PWD",
              "mayUseDirectSaeWclCredential("):
    require(association, token, "exact CIPHER_PWD selector")
for token in (
        "ap_mode != APPLE80211_AP_MODE_INFRA",
        "candidate_count == 0",
        "candidate_count > TahoeAssociationContracts::kMaximumCandidateCount",
        "wcl_key_len < kItlSaeWclCredentialV1PassphraseMinLength",
        "wcl_key_len > kItlSaeWclCredentialV1PassphraseMaxLength",
        "itl_sae_wcl_credential_bssid_is_unicast_nonzero(saeBssid)",
        "AirportItlwmIwnDirectSaeCredentialProvenance::WclCandidate",
        "directRequest.password =",
        "startIwnDirectSaeCredential(&directRequest, nullptr,",
        "explicit_bzero(&directRequest",
        "explicit_bzero(&owner",
):
    require(direct, token, "WCL parse/fence")
for token in (
        "struct ItlSaeWclCredentialV1 saeCredential",
        "memcpy(saeCredential.password",
        "associateSSID(",
        "publishPendingAssocTarget",
        "ieee80211_new_state(",
        "IEEE80211_C_MFP",
):
    forbid(direct_code, token, "WCL wrapper bypass")
ordered(direct, "WCL validation before common call",
        "wcl_key_len > kItlSaeWclCredentialV1PassphraseMaxLength",
        "memcpy(saeBssid,",
        "itl_sae_wcl_credential_bssid_is_unicast_nonzero(saeBssid)",
        "airportItlwmRegDiagShouldBlock(",
        "directRequest.password =",
        "startIwnDirectSaeCredential(&directRequest, nullptr,")

common = body(sky,
    "IOReturn AirportItlwmSkywalkInterface::\nstartIwnDirectSaeCredential(",
    "shared direct-SAE transaction")
common_code = re.sub(r"/\*.*?\*/|//[^\n]*", "", common, flags=re.S)
ordered(common, "common policy/stage/resume order",
        "kIwnWclReplacementAdmissionDrainAttempts",
        "reserveSaeWclCredentialAdmission()",
        "IOSleep(1)",
        "clearExternalPmkEligibilityLocked(",
        "generation = ieee80211_sae_wcl_request_begin(",
        "credential.request_generation = generation;",
        "memcpy(credential.password, request->password",
        "fHalService->stageSaeWclCredential(&credential)",
        "setAUTH_TYPE(&authType)",
        "AirportItlwmPostPltiTraceBeginDirectSaeEpisode(ic)",
        "ieee80211_sae_wcl_request_resume_scan(ic, generation)")
ordered(common, "reference direct WCL candidate handoff",
        "AirportItlwmPostPltiTraceBeginDirectSaeEpisode(ic)",
        "AirportItlwmIwnDirectSaeCredentialProvenance::WclCandidate",
        "instance->associationScanOwnersIdle()",
        "tahoeFindJoinableCachedWclCandidate(",
        "ieee80211_sae_wcl_request_admit_cached_wcl_candidate(",
        "tahoeJoinCachedWclCandidate(",
        "ieee80211_sae_wcl_request_bound_current(ic, ic->ic_bss)",
        "CACHED_CANDIDATE_REFRESH_SCAN",
        "ieee80211_sae_wcl_request_resume_scan(ic, generation)")
ordered(common, "exact generation failure cleanup",
        "ieee80211_sae_wcl_request_clear_if_generation(ic, generation)",
        "fHalService->cancelSaeWclCredential(generation)",
        "explicit_bzero(&credential")
ordered(common, "deferred physical scan remains an accepted WCL carrier",
        "scanResume != IEEE80211_SAE_WCL_REQUEST_RESUME_STARTED &&",
        "scanResume != IEEE80211_SAE_WCL_REQUEST_RESUME_DEFERRED",
        "result = kIOReturnSuccess")

require(net_var, "IEEE80211_SAE_WCL_REQUEST_RESUME_DEFERRED = 3",
        "deferred resume result")
require(iwn_var, "sc_scan_lease_replay_sae_generation",
        "exact deferred generation slot")
require(iwn_var, "sc_sae_wcl_admission_requires_fresh_scan",
        "lower-live-scan admission identity")
reserve = body(iwn, "reserveSaeWclCredentialAdmission()",
               "IWN pre-secret lower admission")
ordered(reserve, "active lower scan remains a deferable exact admission",
        "lower_scan_deferable =",
        "iwn_scan_lease_live_locked(sc)",
        "!sc->sc_scan_lease.hardware_invalidated",
        "!sc->sc_scan_lease.terminal_claimed",
        "ic->ic_state == IEEE80211_S_SCAN",
        "IWN_FLAG_SCANNING",
        "lower_scan_deferable",
        "sc->sc_sae_wcl_admission_reserved = true",
        "sc->sc_sae_wcl_admission_requires_fresh_scan =")
require(common, "iwnHal->saeWclCredentialAdmissionRequiresFreshScan()",
        "cached-join lower radio owner fence")
newstate = body(iwn, "iwn_newstate(struct ieee80211com *ic,",
                "IWN state transition")
ordered(newstate, "active-scan direct SAE abort/replay",
        "direct_sae_scan_generation != 0",
        "ieee80211_sae_wcl_request_scan_deferred(",
        "iwn_scan_lease_defer_scan(",
        "direct_sae_scan_generation, &serial",
        "IWN_CMD_SCAN_ABORT",
        "CACHED_CANDIDATE_REFRESH_DEFERRED",
        "return EAGAIN;")
replay = body(iwn, "iwn_scan_lease_replay_task(void *arg)",
              "IWN scan replay task")
ordered(replay, "generation-fenced deferred fresh scan",
        "direct_sae_generation =",
        "sc->sc_scan_lease_replay_sae_generation",
        "sc->sc_scan_lease_replay_sae_generation = 0",
        "CACHED_CANDIDATE_REFRESH_REPLAY",
        "resume_result = ieee80211_sae_wcl_request_resume_scan(",
        "ic, direct_sae_generation)",
        "IEEE80211_SAE_WCL_REQUEST_RESUME_STARTED",
        "IEEE80211_SAE_WCL_REQUEST_RESUME_DEFERRED",
        "releaseSaeWclCredentialAdmission()")
deferred = body(proto,
    "ieee80211_sae_wcl_request_scan_deferred(struct ieee80211com *ic,",
    "net80211 deferred request transfer")
ordered(deferred, "STARTING to selection-held PENDING transfer",
        "ieee80211_sae_wcl_request_scan_starting_locked(ic, generation)",
        "ieee80211_sae_wcl_request_scan_policy_matches_locked(ic,",
        "IEEE80211_SAE_WCL_REQUEST_PENDING")
for token in (
        "associateSSID(", "publishPendingAssocTarget", "DeliverPMK",
        "storeAssocRsnIeOverride", "ieee80211_new_state(",
        "IEEE80211_C_MFP", "ic->ic_pae_mfp_requested",
):
    forbid(common_code, token, "shared direct-SAE bypass")
require(common, "request->provenance ==\n        AirportItlwmIwnDirectSaeCredentialProvenance::LabStimulus",
        "lab off-gate context fence")
require(common, "workloop->onThread()", "lab workloop owner fence")
require(common, "!workloop->onThread()", "lab workloop-thread provenance fence")
forbid(common, "workloop->inGate()", "impossible event-source off-gate fence")

owner = body(owner_registry, "struct AssociationOwner", "public owner")
for token in ("password", "pmk", "psk", "kck", "pwe"):
    forbid(owner.lower(), token, "secret owner field")
for token in ("owner.hasCarrier = true", "owner.authAssocCompletionArmed = true",
              "owner.selectedBssid", "owner.candidateBssid"):
    require(common, token, "WCL-only public owner publication")
require(common, "request->wclOwner != nullptr", "provenance owner branch")

ordinary = association[association.find("#endif", association.find(
    "#if AIRPORT_ITLWM_IWN_SAE_WCL_INGRESS")):]
require(ordinary, "requiresUnsupportedWpa3Auth(",
        "non-CIPHER_PWD WPA3 fallback remains fail-closed")

print("PASS: product WCL CIPHER_PWD reaches the IWN direct-SAE transaction while diagnostic stimulus remains separately gated")
PY
