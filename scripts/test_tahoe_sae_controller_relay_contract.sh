#!/usr/bin/env bash
# Static/local contract for the controller-owned SAE relay and its bounded
# outbound Algorithm-3 TX-completion handoff.
#
# This proves one credential-free frame can advance the relay only after the
# lower IWX backend reports terminal TX success.  It is deliberately not a
# WPA3 association implementation: join ownership, peer RX, cryptography,
# PMK/AKM selection, and PMF activation remain outside this layer.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

python3 - "$root" <<'PY'
from pathlib import Path
import re
import sys


root = Path(sys.argv[1])
relay = (root / "include/ClientKit/AirportItlwmSaeRelayV1.h").read_text()
v2 = (root / "AirportItlwm/AirportItlwmV2.cpp").read_text()
v2_hpp = (root / "AirportItlwm/AirportItlwmV2.hpp").read_text()
agent_h = (root / "AirportItlwmAgent/src/userclient.h").read_text()
agent_c = (root / "AirportItlwmAgent/src/userclient.c").read_text()
agent_main = (root / "AirportItlwmAgent/src/main.m").read_text()
skywalk = (root / "AirportItlwm/AirportItlwmSkywalkInterface.cpp").read_text()
input_c = (root / "itl80211/openbsd/net80211/ieee80211_input.c").read_text()
output_c = (root / "itl80211/openbsd/net80211/ieee80211_output.c").read_text()
proto_c = (root / "itl80211/openbsd/net80211/ieee80211_proto.c").read_text()
crypto_c = (root / "itl80211/openbsd/net80211/ieee80211_crypto.c").read_text()


def fail(message):
    raise SystemExit(f"SAE controller relay contract: {message}")


def require(text, needle, label):
    if needle not in text:
        fail(f"missing {label}: {needle}")


def forbid(text, needle, label):
    if needle in text:
        fail(f"unexpected {label}: {needle}")


def require_regex(text, pattern, label):
    if re.search(pattern, text, flags=re.S) is None:
        fail(f"missing {label}: /{pattern}/")


def body_after_opening(text, opening, label):
    depth = 0
    for index in range(opening, len(text)):
        char = text[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return text[opening + 1:index]
    fail(f"unterminated {label}")


def member_body(text, cls, name):
    match = re.search(r"\b" + re.escape(cls) + r"\s*::\s*" +
                      re.escape(name) + r"\s*\(", text)
    if match is None:
        fail(f"missing {cls}::{name} definition")
    opening = text.find("{", match.end())
    if opening < 0:
        fail(f"missing {cls}::{name} body")
    return body_after_opening(text, opening, f"{cls}::{name}")


def c_function_body(text, name):
    match = re.search(r"\b" + re.escape(name) + r"\s*\(", text)
    if match is None:
        fail(f"missing {name} definition")
    opening = text.find("{", match.end())
    if opening < 0:
        fail(f"missing {name} body")
    return body_after_opening(text, opening, name)


def strip_comments(text):
    return re.sub(r"/\*.*?\*/|//[^\n]*", "", text,
                  flags=re.S | re.M)


def split_top_level(text, delimiter=","):
    fields = []
    start = 0
    paren = bracket = brace = 0
    for index, char in enumerate(text):
        if char == "(":
            paren += 1
        elif char == ")":
            paren -= 1
        elif char == "[":
            bracket += 1
        elif char == "]":
            bracket -= 1
        elif char == "{":
            brace += 1
        elif char == "}":
            brace -= 1
        elif (char == delimiter and paren == 0 and bracket == 0 and
              brace == 0):
            fields.append(text[start:index].strip())
            start = index + 1
    tail = text[start:].strip()
    if tail:
        fields.append(tail)
    return fields


def table_entries(text):
    marker = "sAirportItlwmUserClientMethods"
    start = text.find(marker)
    if start < 0:
        fail("missing PLTI dispatch table")
    opening = text.find("{", start)
    if opening < 0:
        fail("missing PLTI dispatch table initializer")
    table = body_after_opening(text, opening, "PLTI dispatch table")
    entries = []
    index = 0
    while index < len(table):
        if table[index].isspace() or table[index] == ",":
            index += 1
            continue
        if table[index] != "{":
            fail("unexpected PLTI dispatch-table token")
        entry = body_after_opening(table, index, "PLTI dispatch entry")
        entries.append(entry)
        depth = 0
        end = None
        for cursor in range(index, len(table)):
            if table[cursor] == "{":
                depth += 1
            elif table[cursor] == "}":
                depth -= 1
                if depth == 0:
                    end = cursor
                    break
        if end is None:
            fail("unterminated PLTI dispatch entry")
        index = end + 1
    return entries


def compact(value):
    return re.sub(r"\s+", "", value)


def io_connect_call_args(body, label):
    marker = "IOConnectCallMethod("
    first = body.find(marker)
    if first < 0:
        fail(f"missing IOConnectCallMethod in {label}")
    if body.find(marker, first + len(marker)) >= 0:
        fail(f"multiple IOConnectCallMethod calls in {label}")
    opening = first + len(marker) - 1
    depth = 0
    closing = None
    for index in range(opening, len(body)):
        if body[index] == "(":
            depth += 1
        elif body[index] == ")":
            depth -= 1
            if depth == 0:
                closing = index
                break
    if closing is None:
        fail(f"unterminated IOConnectCallMethod in {label}")
    return [compact(field) for field in
            split_top_level(body[opening + 1:closing])]


def require_call_shape(body, label, selector, scalar_input, scalar_count,
                       struct_input, struct_input_size, scalar_output,
                       scalar_output_count, struct_output,
                       struct_output_size):
    args = io_connect_call_args(body, label)
    if len(args) != 10:
        fail(f"{label} must pass exactly ten IOConnectCallMethod arguments")
    expected = [
        "conn", selector, scalar_input, scalar_count, struct_input,
        struct_input_size, scalar_output, scalar_output_count, struct_output,
        struct_output_size,
    ]
    for index, (actual, want) in enumerate(zip(args, expected)):
        if actual != want:
            fail(f"{label} IOConnectCallMethod argument {index} is "
                 f"{actual}, expected {want}")


# The shared ABI, rather than private duplicate integers, remains the source
# of truth for the contiguous append-only selectors.
for token in (
    "kAirportItlwmSaeRelayWaitTargetSelector = 2",
    "kAirportItlwmSaeRelaySubmitReplySelector = 3",
    "kAirportItlwmSaeRelayWaitAuthEventSelector = 4",
    "kAirportItlwmSaeRelayCompleteSelector = 5",
    "kAirportItlwmSaeRelayAbortSelector = 6",
    "kAirportItlwmSaeRelaySelectorCount = 7",
):
    require(relay, token, "append-only SAE relay selector ABI")

# Legacy selectors stay byte-for-byte in their original slots.  The selector
# count is tied to the shared ABI, making a gap or a private eighth selector a
# static error before the kext can expose it.
for token in (
    "kAirportItlwmUserClientMethod_DeliverPMK = 0",
    "kAirportItlwmUserClientMethod_WaitAssociationTarget = 1",
):
    require(v2, token, "legacy PLTI selector")
require_regex(v2,
              r"kAirportItlwmUserClientMethod_NumMethods\s*=\s*"
              r"kAirportItlwmSaeRelaySelectorCount",
              "PLTI selector count bound to shared relay ABI")

entries = table_entries(strip_comments(v2))
if len(entries) != 7:
    fail(f"PLTI dispatch table must contain selectors 0..6 exactly, got "
         f"{len(entries)} entries")
expected_dispatch = (
    ("sExtDeliverPMK", "1", "sizeof(structapple80211_key)", "0", "0"),
    ("sExtWaitAssociationTarget", "1", "0", "0",
     "sizeof(structAirportItlwmAssociationTarget)"),
    ("sExtWaitSaeTarget", "0", "0", "0",
     "sizeof(structAirportItlwmSaeTargetV1)"),
    ("sExtSubmitSaeReply", "0", "sizeof(structAirportItlwmSaeAuthReplyV1)",
     "0", "0"),
    ("sExtWaitSaeAuthEvent", "1", "0", "0",
     "sizeof(structAirportItlwmSaeAuthEventV1)"),
    ("sExtCompleteSae", "0", "sizeof(structAirportItlwmSaeCompletionV1)",
     "0", "0"),
    ("sExtAbortSae", "0", "sizeof(structAirportItlwmSaeAbortV1)",
     "0", "0"),
)
for selector, (entry, expected) in enumerate(zip(entries, expected_dispatch)):
    fields = [compact(field) for field in split_top_level(entry)]
    if len(fields) != 5:
        fail(f"selector {selector} dispatch shape has {len(fields)} fields, "
             "expected five")
    action, scalar_in, struct_in, scalar_out, struct_out = fields
    action_name, want_scalar_in, want_struct_in, want_scalar_out, want_struct_out = expected
    if action_name not in action:
        fail(f"selector {selector} dispatch action is not {action_name}")
    actual_shape = (scalar_in, struct_in, scalar_out, struct_out)
    wanted_shape = (want_scalar_in, want_struct_in, want_scalar_out,
                    want_struct_out)
    if actual_shape != wanted_shape:
        fail(f"selector {selector} dispatch shape {actual_shape} does not "
             f"match {wanted_shape}")

external_method = member_body(v2, "AirportItlwmUserClient", "externalMethod")
require(external_method, "selector >= kAirportItlwmUserClientMethod_NumMethods",
        "append-only selector range gate")
require(external_method, "sAirportItlwmUserClientMethods[selector]",
        "selector-indexed dispatch")

# The controller owns the relay session and its lifetime nonce.  A UserClient
# owns a different, kernel-generated cookie; callers never submit a cookie as
# a scalar or choose one in an ABI structure.
require_regex(v2_hpp, r"\bAirportItlwmSaeRelayFsmV1\s+fSaeRelay\s*;",
              "controller-owned SAE relay state")
for token in (
    "fSaeControllerNonce",
    "fSaeRelayWaitingCookie",
    "fSaeRelayCancelEpoch",
    "fSaeRelayWaiterActive",
    "fSaeRelayTerminating",
    "waitSaeTarget",
    "submitSaeReply",
    "waitSaeAuthEvent",
    "completeSae",
    "abortSae",
    "beginSaeRelay",
    "cancelSaeRelay",
    "abortSaeRelayForClient",
):
    require(v2_hpp, token, "controller-owned SAE relay declaration")
for token in (
    "sExtWaitSaeTarget",
    "sExtSubmitSaeReply",
    "sExtWaitSaeAuthEvent",
    "sExtCompleteSae",
    "sExtAbortSae",
    "fSaeClientCookie",
):
    require(v2, token, "UserClient SAE relay ownership")

init_body = member_body(v2, "AirportItlwmUserClient", "initWithTask")
require(init_body, "fSaeClientCookie", "kernel-side SAE client cookie initialization")
require(init_body, "airportItlwmSaeFillNonZero(fSaeClientCookie)",
        "kernel-side SAE client cookie generator")
if (init_body.find("fProviderLock = nullptr") >
        init_body.find("IOUserClient::initWithTask")):
    fail("UserClient provider lock must be inert before base init can fail")
cookie_fill_body = c_function_body(v2, "airportItlwmSaeFillNonZero")
require(cookie_fill_body, "arc4random_buf",
        "kernel CSPRNG SAE client/controller cookie source")
require(cookie_fill_body, "AirportItlwmSaeRelayFsmV1BytesAllZero",
        "nonzero SAE client/controller cookie check")

for handler, controller_method in (
    ("sExtWaitSaeTarget", "waitSaeTarget"),
    ("sExtSubmitSaeReply", "submitSaeReply"),
    ("sExtWaitSaeAuthEvent", "waitSaeAuthEvent"),
    ("sExtCompleteSae", "completeSae"),
    ("sExtAbortSae", "abortSae"),
):
    body = member_body(v2, "AirportItlwmUserClient", handler)
    require(body, controller_method, f"{handler} controller route")
    require(body, "beginLifecycleOperation",
            f"{handler} lifecycle admission")
    require(body, "endLifecycleOperation", f"{handler} lifecycle release")
for handler in ("sExtWaitSaeTarget", "sExtWaitSaeAuthEvent"):
    body = member_body(v2, "AirportItlwmUserClient", handler)
    require(body, "copySaeClientCookie",
            f"{handler} per-client cookie binding")

for lifecycle_method in ("stop", "free"):
    body = member_body(v2, "AirportItlwmUserClient", lifecycle_method)
    require(body, "abortSaeRelayForClient",
            f"{lifecycle_method} SAE client cleanup")
    require(body, "copySaeClientCookie",
            f"{lifecycle_method} cookie-bound SAE cleanup")
client_close_body = member_body(v2, "AirportItlwmUserClient", "clientClose")
require(client_close_body, "terminate",
        "clientClose routes through cookie-bound stop cleanup")

for controller_method in ("waitSaeTarget", "submitSaeReply",
                          "waitSaeAuthEvent", "completeSae", "abortSae",
                          "beginSaeRelay", "cancelSaeRelay",
                          "abortSaeRelayForClient"):
    body = member_body(v2, "AirportItlwm", controller_method)
    require(body, "getCommandGate", f"{controller_method} command-gate route")
    require(body, "runAction", f"{controller_method} gated action")

for action_name in ("airportItlwmWaitSaeTargetAction",
                    "airportItlwmWaitSaeEventAction"):
    body = c_function_body(v2, action_name)
    require(body, "fSaeRelayTerminating",
            f"{action_name} teardown abort state")
    require(body, "kIOReturnAborted", f"{action_name} teardown result")
    require(body, "commandSleep", f"{action_name} command-gate wait")
    require(body, "fSaeRelayCancelEpoch",
            f"{action_name} replacement-session epoch fence")

wait_target_action = c_function_body(v2, "airportItlwmWaitSaeTargetAction")
for token in ("fSaeRelayWaiterActive", "fSaeRelayWaitingCookie",
              "airportItlwmClearSaeRelayWaiterLocked"):
    require(wait_target_action, token,
            "single-owner SAE target waiter cancellation fence")

cancel_body = member_body(v2, "AirportItlwm", "cancelSaeRelay")
require(cancel_body, "airportItlwmCancelSaeRelayAction",
        "SAE relay cancel gated action")
cancel_action = c_function_body(v2, "airportItlwmCancelSaeRelayAction")
for token in ("fSaeRelayTerminating", "airportItlwmClearSaeRelayLocked"):
    require(cancel_action, token, "SAE relay cancel terminal state/scrub")
clear_body = c_function_body(v2, "airportItlwmClearSaeRelayLocked")
for token in ("commandWakeup", "AirportItlwmSaeRelayFsmV1Clear",
              "airportItlwmAdvanceSaeRelayCancelEpochLocked",
              "fSaeRelayWaiterActive"):
    require(clear_body, token, "SAE relay wake/scrub helper")
begin_body = member_body(v2, "AirportItlwm", "beginSaeRelay")
require(begin_body, "airportItlwmBeginSaeRelayAction",
        "SAE relay start gated action")
begin_action = c_function_body(v2, "airportItlwmBeginSaeRelayAction")
for token in ("AirportItlwmSaeRelayFsmV1Begin", "fSaeRelay",
              "fSaeControllerNonce", "kAirportItlwmSaeRelayFsmIdle",
              "kIOReturnNotReady"):
    require(begin_action, token, "SAE relay start ownership")

# The controller owns one bounded outbound frame at a time.  It validates an
# Agent reply on a copy, reserves an exact transport ticket under the command
# gate, and only then calls the HAL outside that gate.  HAL admission is not
# an on-air success and therefore cannot advance the relay itself.
submit_api = member_body(v2, "AirportItlwm", "submitSaeReply")
for token in ("airportItlwmSubmitSaeReplyAction", "fHalService",
              "submitSaeAuthFrame", "airportItlwmSaeSubmitFailureAction",
              "cancelSaeAuthFrame"):
    require(submit_api, token, "controller-owned SAE TX submission")
forbid(submit_api, "AirportItlwmSaeRelayFsmV1AcceptReply",
       "HAL admission as an SAE FSM advance")

submit_action = c_function_body(v2, "airportItlwmSubmitSaeReplyAction")
for token in ("AirportItlwmSaeRelayFsmV1TargetBound",
              "AirportItlwmSaeRelayFsmV1IdentityMatches",
              "AirportItlwmSaeRelayFsmV1ValidateReply",
              "fSaePendingTxActive", "fSaeNextTxTicket",
              "itl_sae_auth_transport_request_is_well_formed",
              "fSaePendingTxRequest", "fSaePendingTxReply"):
    require(submit_action, token, "SAE reply-to-TX exact identity fence")
forbid(submit_action, "AirportItlwmSaeRelayFsmV1AcceptReply",
       "premature SAE reply acceptance")

# There is exactly one live reply acceptance in this translation unit.  It is
# reached only from a matching firmware-terminal success event; all other
# completion/reset/cancellation paths scrub rather than advance the FSM.
if v2.count("AirportItlwmSaeRelayFsmV1AcceptReply(") != 1:
    fail("there must be exactly one live SAE AcceptReply() call")
tx_completion_action = c_function_body(v2, "airportItlwmSaeTxCompletionAction")
for token in ("itl_sae_auth_transport_event_is_well_formed",
              "itl_sae_auth_transport_event_matches_request",
              "AirportItlwmSaeRelayFsmV1TargetBound",
              "a->event.result != 0",
              "AirportItlwmSaeRelayFsmV1AcceptReply",
              "fSaeLastTerminalTxEvent"):
    require(tx_completion_action, token,
            "firmware-terminal SAE completion fence")
if (tx_completion_action.find("itl_sae_auth_transport_event_matches_request") >
        tx_completion_action.find("AirportItlwmSaeRelayFsmV1AcceptReply") or
        tx_completion_action.find("a->event.result != 0") >
        tx_completion_action.find("AirportItlwmSaeRelayFsmV1AcceptReply")):
    fail("SAE AcceptReply() must follow identity and terminal-success checks")
for token in ("deliverExternalPMK", "ic_psk", "IEEE80211_F_PSK",
              "ieee80211_ioctl_setwpaparms", "IEEE80211_AKM_SAE",
              "IEEE80211_SEND_MGMT"):
    forbid(tx_completion_action, token,
           "SAE completion PMK/legacy-management side effect")

reset_action = c_function_body(v2, "airportItlwmSaeTxResetAction")
for token in ("fSaePendingTxActive", "fSaeLastTerminalTxEventValid",
              "airportItlwmSaeTerminalEventMatches",
              "airportItlwmClearSaeRelayLocked"):
    require(reset_action, token, "exact SAE reset invalidation fence")
for token in ("AirportItlwmSaeRelayFsmV1AcceptReply", "cancelSaeAuthFrame",
              "ieee80211_pae_assoc_epoch_current"):
    forbid(reset_action, token,
           "reset must neither accept nor re-enter a stale lower HAL")

forbid(v2, "AirportItlwmSaeRelayFsmV1AcceptCompletion",
       "premature SAE PMK completion acceptance")
require(v2, "AirportItlwmSaeRelayFsmV1AcceptAbort",
        "cookie-bound SAE relay abort")
complete_api = member_body(v2, "AirportItlwm", "completeSae")
require(complete_api, "kIOReturnNotReady",
        "SAE PMK completion remains fail-closed")
complete_action = c_function_body(v2, "airportItlwmCompleteSaeAction")
for token in ("AirportItlwmSaeRelayFsmV1TargetBound",
              "AirportItlwmSaeRelayFsmV1IdentityMatches",
              "kIOReturnNotReady"):
    require(complete_action, token,
            "SAE completion identity/fail-closed fence")
for action_name in ("airportItlwmSubmitSaeReplyAction",
                    "airportItlwmCompleteSaeAction"):
    body = c_function_body(v2, action_name)
    for token in ("deliverExternalPMK", "ic_psk", "IEEE80211_F_PSK",
                  "ieee80211_ioctl_setwpaparms", "IEEE80211_AKM_SAE",
                  "IEEE80211_SEND_MGMT"):
        forbid(body, token, f"{action_name} PSK/legacy-management side effect")
abort_client_action = c_function_body(v2, "airportItlwmAbortSaeClientAction")
for token in ("AirportItlwmSaeRelayFsmV1BytesEqual",
              "fSaeRelay.target.client_cookie",
              "fSaeRelayWaitingCookie",
              "airportItlwmClearSaeRelayLocked"):
    require(abort_client_action, token,
            "cookie-bound UserClient relay cancellation")

# The Agent wrappers use the same V1 records and shared selectors.  They are
# deliberately callable transport adapters only: no worker is started and no
# SAE request can fall into the legacy PBKDF2/DeliverPMK route.
require(agent_h, "<ClientKit/AirportItlwmSaeRelayV1.h>",
        "Agent shared SAE relay ABI include")
for token in ("AgentWaitSaeTarget", "AgentSubmitSaeReply",
              "AgentWaitSaeAuthEvent", "AgentCompleteSae", "AgentAbortSae",
              "struct AirportItlwmSaeTargetV1",
              "struct AirportItlwmSaeAuthReplyV1",
              "struct AirportItlwmSaeAuthEventV1",
              "struct AirportItlwmSaeCompletionV1",
              "struct AirportItlwmSaeAbortV1"):
    require(agent_h, token, "Agent SAE relay wrapper declaration")

agent_shapes = (
    ("AgentWaitSaeTarget", "kAirportItlwmSaeRelayWaitTargetSelector",
     "NULL", "0", "NULL", "0", "NULL", "NULL", "out_target",
     "&struct_out_sz"),
    ("AgentSubmitSaeReply", "kAirportItlwmSaeRelaySubmitReplySelector",
     "NULL", "0", "reply", "sizeof(*reply)", "NULL", "NULL", "NULL",
     "NULL"),
    ("AgentWaitSaeAuthEvent", "kAirportItlwmSaeRelayWaitAuthEventSelector",
     "scalar_in", "1", "NULL", "0", "NULL", "NULL", "out_event",
     "&struct_out_sz"),
    ("AgentCompleteSae", "kAirportItlwmSaeRelayCompleteSelector",
     "NULL", "0", "completion", "sizeof(*completion)", "NULL", "NULL",
     "NULL", "NULL"),
    ("AgentAbortSae", "kAirportItlwmSaeRelayAbortSelector",
     "NULL", "0", "abort_message", "sizeof(*abort_message)", "NULL",
     "NULL", "NULL", "NULL"),
)
for (wrapper, selector, scalar_input, scalar_count, struct_input,
     struct_input_size, scalar_output, scalar_output_count, struct_output,
     struct_output_size) in agent_shapes:
    body = c_function_body(agent_c, wrapper)
    require_call_shape(body, wrapper, selector, scalar_input, scalar_count,
                       struct_input, struct_input_size, scalar_output,
                       scalar_output_count, struct_output, struct_output_size)
    for token in ("AgentDeliverPMK", "AgentDerivePMK_PBKDF2", "PBKDF2",
                  "apple80211_key", "keychain", "passphrase"):
        forbid(body, token, f"{wrapper} legacy PSK side effect")

for wrapper in ("AgentWaitSaeTarget", "AgentWaitSaeAuthEvent"):
    body = c_function_body(agent_c, wrapper)
    require(body, "sizeof(*out_", f"{wrapper} exact output ABI size")
for wrapper, helper in (
    ("AgentWaitSaeTarget", "agent_sae_target_shape_is_valid"),
    ("AgentWaitSaeAuthEvent", "agent_sae_auth_event_shape_is_valid"),
    ("AgentSubmitSaeReply", "agent_sae_auth_reply_shape_is_valid"),
    ("AgentCompleteSae", "agent_sae_completion_shape_is_valid"),
    ("AgentAbortSae", "agent_sae_abort_shape_is_valid"),
):
    body = c_function_body(agent_c, wrapper)
    require(body, helper, f"{wrapper} shared-record validator")
    helper_body = c_function_body(agent_c, helper)
    require(helper_body, "agent_sae_record_header_is_valid",
            f"{helper} shared-record header route")
    require(helper_body, "sizeof(*", f"{helper} exact shared-record size check")
record_header_body = c_function_body(agent_c, "agent_sae_record_header_is_valid")
require(record_header_body, "kAirportItlwmSaeRelayV1Version",
        "Agent shared-record version check")

for token in ("AgentWaitSaeTarget", "AgentSubmitSaeReply",
              "AgentWaitSaeAuthEvent", "AgentCompleteSae", "AgentAbortSae"):
    forbid(agent_main, token, "premature Agent SAE worker")

# The bounded sender does not weaken the existing pure-SAE quarantine.  The
# generic net80211 state machine remains Open-System-only; exactly one
# separate builder writes Algorithm 3 and no association/PMK route is enabled.
require(skywalk, "requiresUnsupportedWpa3Auth",
        "pure-SAE association ingress reject")
require(input_c, "if (algo != IEEE80211_AUTH_ALG_OPEN)",
        "Open-System-only RX gate")
require(output_c, "LE_WRITE_2(frm, IEEE80211_AUTH_ALG_OPEN)",
        "Open-System-only TX builder")
require(proto_c, "IEEE80211_AUTH_OPEN_REQUEST",
        "Open-System auth request producer")
require(crypto_c, "ic->ic_rsnakms = IEEE80211_AKM_PSK;",
        "PSK-only active AKM configuration")
generic_auth = c_function_body(output_c, "ieee80211_get_auth")
forbid(generic_auth, "IEEE80211_AUTH_ALG_SAE",
       "generic Open-System auth builder Algorithm-3 use")
sae_builder = c_function_body(output_c, "ieee80211_sae_auth_frame_build")
for token in ("itl_sae_auth_transport_request_is_well_formed",
              "IEEE80211_S_AUTH", "IEEE80211_AUTH_ALG_SAE",
              "request->transaction", "request->auth_status"):
    require(sae_builder, token, "isolated SAE auth frame builder")
if output_c.count("IEEE80211_AUTH_ALG_SAE") != 1:
    fail("only the isolated SAE frame builder may write Algorithm 3")
for text, label in ((v2, "controller"), (input_c, "RX"),
                    (proto_c, "protocol")):
    forbid(text, "ieee80211_sae_auth_contract.h",
           "premature Algorithm-3 production include in " + label)
    forbid(text, "IEEE80211_AUTH_ALG_SAE",
           "premature Algorithm-3 production use in " + label)
forbid(crypto_c, "IEEE80211_AKM_SAE", "premature active SAE AKM")

print("PASS: SAE controller relay and bounded TX-completion contract")
PY
