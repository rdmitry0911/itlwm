#!/usr/bin/env bash
# Static contract for the physically lab-only UserClient stimulus that reaches
# the real IWN direct-SAE lower half.  It intentionally proves no CoreWLAN/WCL
# credential transport and no association outcome.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

python3 - "$root" <<'PY'
from pathlib import Path
import re
import sys

root = Path(sys.argv[1])
abi = (root / "include/ClientKit/AirportItlwmIwnLabDirectSaeStimulusV1.h").read_text()
gate = (root / "AirportItlwm/IwnDirectSaeLabGate.hpp").read_text()
v2 = (root / "AirportItlwm/AirportItlwmV2.cpp").read_text()
v2h = (root / "AirportItlwm/AirportItlwmV2.hpp").read_text()
sky = (root / "AirportItlwm/AirportItlwmSkywalkInterface.cpp").read_text()
agent = (root / "AirportItlwmAgent/src/userclient.c").read_text()
relay = (root / "include/ClientKit/AirportItlwmSaeRelayV1.h").read_text()
hal = (root / "include/HAL/ItlHalService.hpp").read_text()
iwn_h = (root / "itlwm/hal_iwn/ItlIwn.hpp").read_text()
iwn_cpp = (root / "itlwm/hal_iwn/ItlIwn.cpp").read_text()
iwn_var = (root / "itlwm/hal_iwn/if_iwnvar.h").read_text()


def fail(message):
    raise SystemExit(f"IWN direct-SAE lab UserClient contract: {message}")


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


def ordered(text, label, *needles):
    cursor = 0
    for needle in needles:
        place = text.find(needle, cursor)
        if place < 0:
            fail(f"{label} missing ordered token: {needle}")
        cursor = place + len(needle)


def struct_body(source, name):
    marker = f"struct {name} {{"
    start = source.find(marker)
    if start < 0:
        fail(f"missing ABI record {name}")
    end = source.find("\n};", start)
    if end < 0:
        fail(f"unterminated ABI record {name}")
    return source[start:end]


for token in (
        "#define kAirportItlwmIwnLabDirectSaeStimulusUserClientType ('ISAE')",
        "kAirportItlwmIwnLabDirectSaeStimulusQueryReadySelector = 0",
        "kAirportItlwmIwnLabDirectSaeStimulusSubmitSelector = 1",
        "uint32_t profile;", "uint32_t ssid_len;",
        "uint32_t password_len;", "uint8_t bssid[",
        "uint8_t ssid[", "uint8_t password[",
        "RequestIsWellFormed", "RequestScrub",
        "request->reserved,", "request->ssid +",
        "request->password +",
):
    require(abi, token, "fixed lab ABI")
record = struct_body(abi, "AirportItlwmIwnLabDirectSaeStimulusRequestV1")
for token in ("generation", "pmk", "pmkid", "pwe", "kck", "rsn", "channel",
              "frame", "*"):
    forbid(record.lower(), token.lower(), "forbidden caller-controlled ABI field")

for token in (
        "defined(IWN_SOFTWARE_PMF_LAB_BUILD)",
        "ITL_SAE_DRIVER_CRYPTO_AVAILABLE",
        "#define AIRPORT_ITLWM_IWN_DIRECT_SAE_LAB_STIMULUS 1",
        "#define AIRPORT_ITLWM_IWN_DIRECT_SAE_LAB_STIMULUS 0",
):
    require(gate, token, "physical compile-time gate")

require(v2h, "AirportItlwmIwnDirectSaeLabStimulusLifecycle",
        "isolated one-slot lifecycle")
for token in ("pending", "dispatching", "active", "cancelRequested",
              "lowerAdmissionReserved", "ownerCookie", "activeGeneration",
              "requestId", "outcomeValid", "outcomeRequestId",
              "outcomeCookie"):
    require(v2h, token, "exact cancellation fence")
require(relay, "kAirportItlwmSaeRelaySelectorCount = 7",
        "unchanged PLTI selector ABI")
require(v2, "kAirportItlwmUserClientMethod_NumMethods =\n        kAirportItlwmSaeRelaySelectorCount",
        "unchanged PLTI dispatch count")
forbid(agent, "AirportItlwmIwnLabDirectSaeStimulus", "permanent Agent ingress")

for token in (
        "productPltiType", "labDirectSaeType",
        "kAirportItlwmIwnLabDirectSaeStimulusUserClientType",
        "clientHasPrivilege(", "beginLifecycleOperation()",
        "client->attach(this)", "client->start(this)",
):
    require(v2, token, "lab UserClient open fence")
require(v2, "if (fIwnDirectSaeLabClient)", "separate dispatch by type")
for token in ("sExtIwnDirectSaeLabQueryReady", "sExtIwnDirectSaeLabSubmit",
              "sExtIwnDirectSaeLabQueryOutcome",
              "sizeof(struct AirportItlwmIwnLabDirectSaeStimulusRequestV1)",
              "sizeof(struct AirportItlwmIwnLabDirectSaeStimulusReadyReplyV1)",
              "sizeof(struct AirportItlwmIwnLabDirectSaeStimulusOutcomeReplyV1)"):
    require(v2, token, "three-method lab dispatch")

query = body(v2, "IOReturn AirportItlwmUserClient::\nsExtIwnDirectSaeLabQueryReady(",
             "QueryReady handler")
forbid(query, "XYLog", "QueryReady input logging")
forbid(query, "queueIwnDirectSaeLabStimulus", "QueryReady stimulus")
forbid(query, "startIwnDirectSaeLabStimulus", "QueryReady association")
require(query, "queryIwnDirectSaeLabReady(&reply)", "identity-free readiness")
outcome_query = body(
    v2,
    "IOReturn AirportItlwmUserClient::\nsExtIwnDirectSaeLabQueryOutcome(",
    "QueryOutcome handler")
for token in ("copySaeClientCookie(clientCookie)",
              "queryIwnDirectSaeLabOutcome(clientCookie, &reply)",
              "explicit_bzero(&reply", "explicit_bzero(clientCookie"):
    require(outcome_query, token, "cookie-bound categorical outcome")
for token in ("XYLog", "request", "password", "bssid", "generation",
              "kIOReturnSuccess ?"):
    forbid(outcome_query.lower(), token.lower(), "outcome disclosure/bypass")
ready = body(v2, "airportItlwmIwnDirectSaeLabReadyGated(",
             "lower-backed readiness gate")
require(ready, "fHalService->isSaeWclCredentialAdmissionReady()",
        "lower IWN admission readiness")
require(hal, "virtual bool isSaeWclCredentialAdmissionReady() { return false; }",
        "fail-closed HAL readiness")
require(hal, "virtual bool reserveSaeWclCredentialAdmission() { return false; }",
        "fail-closed HAL reservation")
require(hal, "virtual void releaseSaeWclCredentialAdmission() {}",
        "fail-closed HAL reservation release")
require(iwn_h, "bool isSaeWclCredentialAdmissionReady() override;",
        "IWN readiness override")
for token in ("bool reserveSaeWclCredentialAdmission() override;",
              "void releaseSaeWclCredentialAdmission() override;"):
    require(iwn_h, token, "IWN reservation override")
require(iwn_var, "bool                sc_sae_wcl_admission_reserved;",
        "lower scan-leaf reservation owner")
require(iwn_var, "u_int64_t           sc_sae_join_scan_block_generation;",
        "exact SAE join scan-continuity owner")
iwn_ready = body(iwn_cpp, "isSaeWclCredentialAdmissionReady()",
                 "IWN readiness predicate")
for token in ("iwn_sae_engine_runtime_enabled(sc)",
              "iwn_sae_tx_lifecycle_enter(sc, false)",
              "iwn_sae_wcl_credential_stage_state_permitted(ic, ifp)",
              "!iwn_scan_lease_live_locked(sc)",
              "!sc->sc_wcl_initial_scan_pending.queued",
              "sc->sc_sae_join_scan_block_generation == 0",
              "(sc->sc_flags & IWN_FLAG_SCANNING) == 0",
              "!sc->sc_sae_engine_owner.active",
              "sc->sc_sae_engine == NULL",
              "!sc->sc_sae_wcl_credential_staged",
              "iwn_sae_tx_lifecycle_leave(sc)"):
    require(iwn_ready, token, "lower IWN readiness fence")
reserve = body(iwn_cpp, "reserveSaeWclCredentialAdmission()",
               "IWN lower reservation")
ordered(reserve, "lower reservation linearization",
        "iwn_sae_tx_lifecycle_enter(sc, false)",
        "IOLockLock(sc->sc_sae_tx_lifecycle_lock)",
        "IOSimpleLockLock(sc->sc_scan_lease_lock)",
        "lower_scan_deferable =",
        "iwn_scan_lease_live_locked(sc)",
        "!sc->sc_scan_lease.hardware_invalidated",
        "!sc->sc_scan_lease.terminal_claimed",
        "!sc->sc_wcl_initial_scan_pending.queued",
        "!sc->sc_sae_wcl_admission_reserved",
        "sc->sc_sae_join_scan_block_generation == 0",
        "!sc->sc_ap_transition_scan_blocked",
        "!iwn_scan_lease_live_locked(sc)",
        "(sc->sc_flags & IWN_FLAG_SCANNING) == 0",
        "lower_scan_deferable",
        "sc->sc_sae_wcl_admission_reserved = true;",
        "IOSimpleLockUnlock(sc->sc_scan_lease_lock)")
for token in ("!sc->sc_sae_engine_owner.active",
              "sc->sc_sae_engine == NULL",
              "!sc->sc_sae_wcl_credential_staged",
              "sc->sc_sae_wcl_admission_reserved = false;"):
    require(reserve, token, "reservation rollback fence")
release = body(iwn_cpp, "releaseSaeWclCredentialAdmission()",
               "IWN reservation release")
ordered(release, "reservation release lifecycle order",
        "iwn_sae_tx_lifecycle_enter(sc, true)",
        "IOLockLock(sc->sc_sae_tx_lifecycle_lock)",
        "IOSimpleLockLock(sc->sc_scan_lease_lock)",
        "sc->sc_sae_wcl_admission_reserved = false;",
        "IOSimpleLockUnlock(sc->sc_scan_lease_lock)",
        "IOLockUnlock(sc->sc_sae_tx_lifecycle_lock)",
        "iwn_sae_tx_lifecycle_leave(sc)")
submit = body(v2, "IOReturn AirportItlwmUserClient::\nsExtIwnDirectSaeLabSubmit(",
              "Submit handler")
ordered(submit, "Submit bounded copy/scrub",
        "memcpy(&request, args->structureInput, sizeof(request))",
        "RequestIsWellFormed(&request)",
        "queueIwnDirectSaeLabStimulus(&request, clientCookie)",
        "RequestScrub(&request)",
        "explicit_bzero(clientCookie")
for token in ("XYLog", "associateSSID(", "publishPendingAssocTarget", "DeliverPMK",
              "stageSaeWclCredential", "request->password"):
    forbid(submit, token, "UserClient bypass/log")

queue = body(v2, "IOReturn AirportItlwm::\nqueueIwnDirectSaeLabStimulus(",
             "mailbox queue")
ordered(queue, "no secret before readiness",
        "queryIwnDirectSaeLabReady(&ready)",
        "if (!readyForSecret)",
        "fHalService->reserveSaeWclCredentialAdmission()",
        "state.request = *request;",
        "state.lowerAdmissionReserved = true;",
        "signalIwnDirectSaeLabStimulus(state, admissionLock, source)")
for token in ("associateSSID(", "ieee80211_new_state(", "raw +", "stageSaeWclCredential"):
    forbid(queue, token, "mailbox bypass")

action = body(v2, "static void\niwnDirectSaeLabStimulusInterruptAction(",
              "workloop source action")
for token in ("startIwnDirectSaeLabStimulus(&request",
              "dispatchOutcome", "iwnDirectSaeLabPublishOutcomeLocked",
              "explicit_bzero(&request", "cancelIwnDirectSaeLabGeneration"):
    require(action, token, "source action work")
ordered(action, "reservation transfer through lower start",
        "lowerAdmissionOwned = state.lowerAdmissionReserved;",
        "state.lowerAdmissionReserved = false;",
        "startIwnDirectSaeLabStimulus(&request",
        "iwnDirectSaeLabReleaseLowerAdmission(that)")
forbid(action, "runAction(", "workloop source direct command-gate action")
forbid(action, "args->", "borrowed UserClient pointer in action")
require(v2, "teardownIwnDirectSaeLabStimulusSource(this, _fWorkloop)",
        "source teardown")
require(v2, "cancelIwnDirectSaeLabAll();", "drain cancellation")
for token in ("cancelIwnDirectSaeLabForClient(sae_cookie)",
              "iwnDirectSaeLabCookieEqual"):
    require(v2, token, "exact UserClient cancellation")
for token in ("releaseLowerAdmission =\n            iwnDirectSaeLabClearOwnershipLocked(state)",
              "if (releaseLowerAdmission)\n            iwnDirectSaeLabReleaseLowerAdmission(this)",
              "if (releaseLowerAdmission)\n        iwnDirectSaeLabReleaseLowerAdmission(that)"):
    require(v2, token, "pending/teardown reservation release")
require(sky, "ieee80211_sae_wcl_request_clear_if_generation", 
        "exact driver generation cancellation")

scan_reserve = body(iwn_cpp, "iwn_scan_lease_reserve(",
                    "physical scan lease reservation")
for token in ("(sc->sc_sae_wcl_admission_reserved && !direct_sae_scan)",
              "sc->sc_sae_join_scan_block_generation != 0",
              "(direct_sae_scan && !sc->sc_sae_wcl_admission_reserved)",
              "if (direct_sae_scan)",
              "sc->sc_sae_wcl_admission_reserved = false;",
              "sc->sc_sae_join_scan_block_generation =\n            direct_sae_scan_generation;"):
    require(scan_reserve, token, "atomic direct-SAE scan consumption")
require(iwn_cpp,
        "direct_sae_scan_generation)) != 0",
        "direct-SAE scan carries reservation-consume identity")
preflight = body(iwn_cpp, "iwn_newstate_preflight(",
                 "IWN state-machine scan preflight")
ordered(preflight, "active SAE join blocks ordinary S_SCAN",
        "arg != IEEE80211_NEWSTATE_ARG_SCAN_HOP",
        "iwn_sae_join_scan_blocked(sc)",
        "return 1;")
auth_hold = body(iwn_cpp, "iwn_sae_auth_hold(", "direct SAE auth hold")
ordered(auth_hold, "cached direct join promotes scan continuity",
        "ieee80211_sae_wcl_request_copyout_bound_current",
        "iwn_sae_join_scan_block_promote(sc, bound.generation)")
promote = body(iwn_cpp, "static bool\niwn_sae_join_scan_block_promote(",
               "direct SAE scan-continuity promotion")
for token in (
        "sc->sc_scan_lease.owner == IWN_SCAN_LEASE_GENERIC_BACKGROUND",
        "sc->sc_scan_lease.owner == IWN_SCAN_LEASE_WCL_BACKGROUND",
        "sc->sc_scan_lease.phase == IWN_SCAN_LEASE_DRAINING",
        "sc->sc_scan_lease.terminal_claimed",
        "!sc->sc_scan_lease.abort_requested",
        "!sc->sc_scan_lease.hardware_invalidated",
        "!sc->sc_scan_lease.publication_invalidated",
        "ic->ic_wcl_reassoc_owner_active",
        "IEEE80211_WCL_REASSOC_OWNER_LEAF_ROAM_STARTED",
        "(!iwn_scan_lease_live_locked(sc) || completing_wcl_roam)"):
    require(promote, token,
            "exact completed WCL scan to SAE join continuity transfer")
port_valid = body(iwn_cpp, "iwn_sae_roam_port_valid(",
                  "direct SAE port-valid terminal")
ordered(port_valid, "successful join releases exact scan continuity",
        "completed_generation =",
        "sc->sc_sae_wcl_credential.request_generation",
        "iwn_sae_join_scan_block_clear_generation(sc, completed_generation)")
cancel_credential = body(iwn_cpp, "cancelSaeWclCredential(",
                         "direct SAE failed terminal")
ordered(cancel_credential, "failed join releases exact scan continuity",
        "iwn_sae_join_scan_block_clear_generation(sc, request_generation)",
        "iwn_sae_wcl_credential_cancel_through_locked")
for token in (
        "else if (direct_sae_scan_generation != 0)",
        "ieee80211_node_cleanup_sae_wcl_scan_starting(",
        "ic, ic->ic_bss, direct_sae_scan_generation",
        "return EAGAIN;"):
    require(iwn_cpp, token, "exact direct-SAE scan node cleanup")

lab = body(sky, "startIwnDirectSaeLabStimulus(", "Skywalk lab entry")
for token in ("RequestIsWellFormed(request)", "LabStimulus",
              "APPLE80211_AUTHTYPE_WPA3_SAE", "wclOwner = nullptr",
              "startIwnDirectSaeCredential(&directRequest", "explicit_bzero(&directRequest"):
    require(lab, token, "strict lab entry")
for token in (
        "OutcomeRejectedPrecondition", "OutcomeRejectedRequestBegin",
        "OutcomeRejectedAssociationOwner", "OutcomeRejectedCredentialStage",
        "OutcomeRejectedAuthType", "OutcomeRejectedScanResume",
        "OutcomeStarted"):
    require(sky, token, "bounded lower dispatch outcome")
for token in ("associateSSID(", "setWCL_ASSOCIATE", "publishPendingAssocTarget",
              "ieee80211_new_state(", "raw +"):
    forbid(lab, token, "lab entry bypass")
common = body(sky, "startIwnDirectSaeCredential(",
              "shared direct-SAE transaction")
require(common, "!workloop->onThread()", "workloop-thread provenance fence")
forbid(common, "workloop->inGate()", "impossible event-source off-gate fence")

print("PASS: lab-only separate UserClient queues one exact IWN SAE stimulus on its workloop source and scrubs/cancels it by client-bound kernel state")
PY
