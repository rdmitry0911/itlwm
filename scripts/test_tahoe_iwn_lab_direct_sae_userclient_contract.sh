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
              "ownerCookie", "activeGeneration", "requestId"):
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
              "sizeof(struct AirportItlwmIwnLabDirectSaeStimulusRequestV1)",
              "sizeof(struct AirportItlwmIwnLabDirectSaeStimulusReadyReplyV1)"):
    require(v2, token, "two-method lab dispatch")

query = body(v2, "IOReturn AirportItlwmUserClient::\nsExtIwnDirectSaeLabQueryReady(",
             "QueryReady handler")
forbid(query, "XYLog", "QueryReady input logging")
forbid(query, "queueIwnDirectSaeLabStimulus", "QueryReady stimulus")
forbid(query, "startIwnDirectSaeLabStimulus", "QueryReady association")
require(query, "queryIwnDirectSaeLabReady(&reply)", "identity-free readiness")
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
        "state.request = *request;",
        "signalIwnDirectSaeLabStimulus(state, admissionLock, source)")
for token in ("associateSSID(", "ieee80211_new_state(", "raw +", "stageSaeWclCredential"):
    forbid(queue, token, "mailbox bypass")

action = body(v2, "static void\niwnDirectSaeLabStimulusInterruptAction(",
              "off-gate source action")
for token in ("startIwnDirectSaeLabStimulus(&request",
              "explicit_bzero(&request", "cancelIwnDirectSaeLabGeneration"):
    require(action, token, "source action work")
forbid(action, "runAction(", "off-gate action command gate")
forbid(action, "args->", "borrowed UserClient pointer in action")
require(v2, "teardownIwnDirectSaeLabStimulusSource(this, _fWorkloop)",
        "source teardown")
require(v2, "cancelIwnDirectSaeLabAll();", "drain cancellation")
for token in ("cancelIwnDirectSaeLabForClient(sae_cookie)",
              "iwnDirectSaeLabCookieEqual"):
    require(v2, token, "exact UserClient cancellation")
require(sky, "ieee80211_sae_wcl_request_clear_if_generation", 
        "exact driver generation cancellation")

lab = body(sky, "startIwnDirectSaeLabStimulus(", "Skywalk lab entry")
for token in ("RequestIsWellFormed(request)", "LabStimulus",
              "APPLE80211_AUTHTYPE_WPA3_SAE", "wclOwner = nullptr",
              "startIwnDirectSaeCredential(&directRequest", "explicit_bzero(&directRequest"):
    require(lab, token, "strict lab entry")
for token in ("associateSSID(", "setWCL_ASSOCIATE", "publishPendingAssocTarget",
              "ieee80211_new_state(", "raw +"):
    forbid(lab, token, "lab entry bypass")

print("PASS: lab-only separate UserClient queues one exact IWN SAE stimulus off-gate and scrubs/cancels it by client-bound kernel state")
PY
