#!/usr/bin/env bash
# Static contract for the IWX-owned, one-ticket SAE Authentication TX spine.
#
# This checks source-level ownership all the way from the controller's
# credential-free request to a real IWX descriptor/doorbell and back through
# a deferred terminal completion.  It deliberately does not claim an SAE or
# WPA3 association: there is still no selected-BSS join owner, inbound
# Algorithm-3 RX, cryptographic backend, PMK/AKM activation, or PMF enable.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

python3 - "$root" <<'PY'
from pathlib import Path
import re
import sys


root = Path(sys.argv[1])
transport = (root / "include/HAL/ItlSaeAuthTransportV1.h").read_text()
hal = (root / "include/HAL/ItlHalService.hpp").read_text()
iwx = (root / "itlwm/hal_iwx/ItlIwx.cpp").read_text()
iwx_hpp = (root / "itlwm/hal_iwx/ItlIwx.hpp").read_text()
iwx_var = (root / "itlwm/hal_iwx/if_iwxvar.h").read_text()
output = (root / "itl80211/openbsd/net80211/ieee80211_output.c").read_text()
input_c = (root / "itl80211/openbsd/net80211/ieee80211_input.c").read_text()
proto = (root / "itl80211/openbsd/net80211/ieee80211_proto.c").read_text()
crypto = (root / "itl80211/openbsd/net80211/ieee80211_crypto.c").read_text()
var_h = (root / "itl80211/openbsd/net80211/ieee80211_var.h").read_text()
v2 = (root / "AirportItlwm/AirportItlwmV2.cpp").read_text()
v2_hpp = (root / "AirportItlwm/AirportItlwmV2.hpp").read_text()
skywalk = (root / "AirportItlwm/AirportItlwmSkywalkInterface.cpp").read_text()


def fail(message):
    raise SystemExit(f"IWX SAE auth transport contract: {message}")


def require(text, needle, label):
    if needle not in text:
        fail(f"missing {label}: {needle}")


def forbid(text, needle, label):
    if needle in text:
        fail(f"unexpected {label}: {needle}")


def body_after(source, opening, label):
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
    fail(f"unterminated {label}")


def function_body(source, name):
    # The target functions have ordinary C/C++ parameter lists. Requiring the
    # opening brace here avoids matching prototypes or prose references.
    pattern = re.compile(r"\b" + re.escape(name) +
                         r"\s*\([^;{}]*\)\s*\{", re.S)
    match = pattern.search(source)
    if match is None:
        fail(f"missing function {name}()")
    opening = source.rfind("{", match.start(), match.end())
    return body_after(source, opening, name)


def iwx_method(name):
    pattern = re.compile(r"\bItlIwx\s*::\s*" + re.escape(name) +
                         r"\s*\([^;{}]*\)\s*\{", re.S)
    match = pattern.search(iwx)
    if match is None:
        fail(f"missing ItlIwx::{name}()")
    opening = iwx.rfind("{", match.start(), match.end())
    return body_after(iwx, opening, f"ItlIwx::{name}")


def airport_method(name):
    pattern = re.compile(r"\bAirportItlwm\s*::\s*" + re.escape(name) +
                         r"\s*\([^;{}]*\)\s*\{", re.S)
    match = pattern.search(v2)
    if match is None:
        fail(f"missing AirportItlwm::{name}()")
    opening = v2.rfind("{", match.start(), match.end())
    return body_after(v2, opening, f"AirportItlwm::{name}")


def ordered(text, label, *needles):
    cursor = 0
    for needle in needles:
        position = text.find(needle, cursor)
        if position < 0:
            fail(f"{label} missing ordered token: {needle}")
        cursor = position + len(needle)


# The internal ABI is credential-free, bounded, and values-only. Its default
# HAL implementation stays fail-closed; IWX is the sole backend override.
for token in (
        "kItlSaeAuthTransportV1MaxBodyLength 768u",
        "struct ItlSaeAuthTxRequestV1",
        "struct ItlSaeAuthTransportEventV1",
        "itl_sae_auth_transport_request_is_well_formed",
        "itl_sae_auth_transport_event_is_well_formed",
        "itl_sae_auth_transport_event_matches_request",
        "ticket", "association_epoch", "relay_generation"):
    require(transport, token, "bounded transport ABI")
for secret in ("password[", "pwe[", "kck[", "pmk[", "pmkid[",
               "rsnxe_payload", "client_cookie"):
    forbid(transport, secret, "credential/cookie transport field")
require(hal, "virtual IOReturn submitSaeAuthFrame(",
        "HAL SAE submission contract")
require(hal, "return kIOReturnUnsupported;", "fail-closed non-IWX HAL")
for token in ("submitSaeAuthFrame(", "cancelSaeAuthFrame(",
              "IOCommandGate *fSaeTxGate", "iwx_sae_tx_gate_action",
              "iwx_sae_tx_task_dispatch", "iwx_sae_tx_commit_doorbell"):
    require(iwx_hpp, token, "IWX SAE TX declaration")
for obsolete in ("sc_tx_lock", "sae_tx_start_task", "iwx_sae_tx_start_task"):
    forbid(iwx_var + iwx_hpp + iwx, obsolete, "obsolete parallel SAE TX path")

# One caller reserves a ticket, keeps an outer task-gate lease while it owns a
# retained private gate, then attempts (rather than waits for) the
# driver-owned workloop action. This is intentionally distinct from
# AirportItlwm's policy command gate and cannot deadlock power-off draining.
submit = iwx_method("submitSaeAuthFrame")
for token in ("itl_sae_auth_transport_request_is_well_formed",
              "sc_task_gate_lock", "sc_sae_tx_lock", "sc_sae_tx_active",
              "fSaeTxGate", "gate->retain", "sc_task_gate_active++",
              "gate->attemptAction(&ItlIwx::iwx_sae_tx_gate_action, &args)",
              "kIOReturnCannotLock", "kIOReturnNotReady",
              "iwx_sae_tx_retire_unsubmitted", "iwx_task_gate_leave"):
    require(submit, token, "one-ticket IWX submission")
ordered(submit, "IWX submit lifetime",
        "gate = fSaeTxGate", "gate->retain", "sc_task_gate_active++",
        "gate->attemptAction(&ItlIwx::iwx_sae_tx_gate_action, &args)",
        "gate->release", "iwx_task_gate_leave")
gate_action = iwx_method("iwx_sae_tx_gate_action")
for token in ("iwx_task_gate_enter", "iwx_sae_tx_submit_on_gate",
              "iwx_task_gate_leave"):
    require(gate_action, token, "IWX workloop gate action")
forbid(gate_action, "getCommandGate", "controller gate inside IWX TX action")
leaf = iwx_method("iwx_sae_tx_submit_on_gate")
for token in ("ieee80211_sae_auth_frame_build", "iwx_sae_tx_request_is_live",
              "IEEE80211_S_AUTH", "iwx_tx(sc, m, ni, EDCA_AC_BE, request)"):
    require(leaf, token, "IWX SAE TX leaf")
forbid(leaf, "IEEE80211_SEND_MGMT", "generic management send bypass")

# The private gate is registered on the IWX workloop and removed only after
# close + task barriers/drain cover both outer and inner leases.
attach = iwx_method("iwx_attach")
for token in ("fSaeTxGate = IOCommandGate::commandGate(this)",
              "pa->workloop->addEventSource(fSaeTxGate)",
              "task_set(&sc->sae_tx_task, iwx_sae_tx_task_dispatch"):
    require(attach, token, "IWX SAE gate setup")
detach = iwx_method("detach")
ordered(detach, "IWX SAE gate teardown",
        "iwx_task_gate_close", "sae_tx_gate = fSaeTxGate",
        "fSaeTxGate = NULL", "iwx_task_gate_drain",
        "removeEventSource(sae_tx_gate)",
        "sae_tx_gate->release")
forbid(detach, "sae_tx_gate->disable()",
       "out-of-gate IOCommandGate disable during detach")

# The isolated net80211 builder creates a genuine management Authentication
# frame. The legacy builder remains Open-System-only and no generic sender
# owns Algorithm 3.
generic_auth = function_body(output, "ieee80211_get_auth")
require(generic_auth, "IEEE80211_AUTH_ALG_OPEN",
        "generic Open-System auth algorithm")
forbid(generic_auth, "IEEE80211_AUTH_ALG_SAE",
       "generic Algorithm-3 auth builder")
sae_builder = function_body(output, "ieee80211_sae_auth_frame_build")
for token in ("itl_sae_auth_transport_request_is_well_formed",
              "ic->ic_opmode != IEEE80211_M_STA",
              "ic->ic_state != IEEE80211_S_AUTH", "ic->ic_bss != ni",
              "ieee80211_pae_assoc_epoch_current(ic)",
              "IEEE80211_FC0_SUBTYPE_AUTH", "IEEE80211_AUTH_ALG_SAE",
              "request->transaction", "request->auth_status"):
    require(sae_builder, token, "isolated Algorithm-3 builder")
if output.count("IEEE80211_AUTH_ALG_SAE") != 1:
    fail("only the isolated builder may use IEEE80211_AUTH_ALG_SAE")
for source, label in ((input_c, "RX"), (proto, "protocol")):
    forbid(source, "IEEE80211_AUTH_ALG_SAE",
           f"generic {label} Algorithm-3 production route")

# The actual IWX TX ring revalidates raw bytes before trim, records identity
# only after DMA mapping, prepares the normal descriptor/TB/byte table, then
# does the final liveness check and physical doorbell in one small lock span.
tx = iwx_method("iwx_tx")
for token in ("if (sae_request != NULL)",
              "mbuf_len(m) < hdrlen + 6 + sae_request->body_len",
              "mbuf_pkthdr_len(m) != hdrlen + 6 + sae_request->body_len",
              "LE_READ_2(auth) != IEEE80211_AUTH_ALG_SAE",
              "getPhysicalSegmentsWithCoalesce", "data->sae_active = true",
              "iwx_tx_update_byte_tbl", "iwx_sae_tx_commit_doorbell",
              "IWX_HBUS_TARG_WRPTR"):
    require(tx, token, "real IWX descriptor/doorbell TX path")
ordered(tx, "post-DMA SAE descriptor identity",
        "getPhysicalSegmentsWithCoalesce", "data->m = m", "data->in = in",
        "iwx_sae_tx_data_clear(data)", "data->sae_active = true",
        "iwx_tx_update_byte_tbl", "iwx_sae_tx_commit_doorbell")
commit = iwx_method("iwx_sae_tx_commit_doorbell")
ordered(commit, "atomic final SAE doorbell",
        "IOLockLock(sc->sc_task_gate_lock)",
        "IOSimpleLockLock(sc->sc_sae_tx_lock)",
        "sc->sc_sae_tx_doorbelled = true",
        "IWX_WRITE(sc, IWX_HBUS_TARG_WRPTR", "IOSimpleLockUnlock",
        "IOLockUnlock")

# Firmware completion, BA reclaim, and ring reset all collapse into one
# bounded deferred terminal record. A success result is available only for a
# single TX response marked success; there is no early controller callback.
rx_single = iwx_method("iwx_rx_tx_cmd_single")
for token in ("IWX_TX_STATUS_SUCCESS", "txd->sae_active",
              "iwx_sae_tx_report_terminal", "txfail ?"):
    require(rx_single, token, "firmware terminal TX result")
tx_done = iwx_method("iwx_txd_done")
require(tx_done, "iwx_sae_tx_report_terminal(sc, txd, EIO)",
        "BA/reclaim terminal failure")
reset_ring = iwx_method("iwx_reset_tx_ring")
require(reset_ring, "iwx_sae_tx_report_terminal(sc, data, EIO)",
        "ring-reset terminal failure")
queue_terminal = iwx_method("iwx_sae_tx_queue_terminal")
for token in ("sc_sae_tx_active", "sc_sae_tx_doorbelled",
              "sc_sae_tx_active_ticket != event->ticket",
              "sc_sae_tx_eventq", "iwx_add_task(sc, sc->sc_nswq",
              "sae_tx_task"):
    require(queue_terminal, token, "one-ticket terminal FIFO")
deferred = iwx_method("iwx_sae_tx_task_dispatch")
for token in ("sc_sae_tx_eventq", "sc_sae_tx_cancel_through",
              "iwx_task_gate_leave", "IEEE80211_EVT_SAE_AUTH_TRANSPORT",
              "ic->ic_event_handler"):
    require(deferred, token, "deferred controller terminal delivery")
if deferred.find("iwx_task_gate_leave") > deferred.find("ic->ic_event_handler"):
    fail("IWX deferred terminal worker must leave its task gate before callback")
forbid(deferred, "getCommandGate", "IWX deferred worker controller-gate wait")
require(var_h, "IEEE80211_EVT_SAE_AUTH_TRANSPORT        8",
        "normal IWX SAE terminal event")
require(var_h, "IEEE80211_EVT_SAE_AUTH_TRANSPORT_RESET  9",
        "IWX SAE reset invalidation event")

# Stop snapshots exact identity after queued work has left but before the ring
# is purged, then emits reset only after rearm/splx. This cannot turn reset
# into a synthetic successful completion.
stop = iwx_method("iwx_stop_internal")
ordered(stop, "IWX reset invalidation order",
        "taskq_barrier(sc->sc_nswq)", "iwx_task_gate_drain",
        "iwx_sae_tx_snapshot_reset", "iwx_sae_tx_cancel_all",
        "iwx_stop_device(sc)", "iwx_sae_tx_purge", "splx(s)",
        "iwx_sae_tx_emit_reset_event")
snapshot = iwx_method("iwx_sae_tx_snapshot_reset")
for token in ("snapshot->result = EIO", "sc_sae_tx_active_event",
              "sc_sae_tx_last_event"):
    require(snapshot, token, "exact terminal/reset identity snapshot")

# The controller callback is deliberately a value-copy mailbox. It never
# waits on AirportItlwm's command gate from IWX nswq; the later source action
# drains all records and only then recursively enters that gate.
for token in ("AirportItlwmSaeTransportMailboxLifecycle",
              "fSaeTransportMailbox", "handleSaeAuthTransportEvent"):
    require(v2_hpp, token, "controller SAE mailbox ownership")
event_handler = airport_method("eventHandler")
ordered(event_handler, "nonblocking controller SAE event routing",
        "IEEE80211_EVT_SAE_AUTH_TRANSPORT", "handleSaeAuthTransportEvent",
        "return;", "IOCommandGate *gate")
handler = function_body(v2, "handleSaeAuthTransportEvent")
require(handler, "queueSaeTransportMailbox", "SAE event mailbox intake")
forbid(handler, "getCommandGate", "mailbox intake controller-gate wait")
forbid(handler, "runAction", "mailbox intake controller-gate wait")
mailbox_action = function_body(v2, "saeTransportMailboxInterruptAction")
for token in ("for (;;) ", "state.entries[state.head]",
              "state.count--", "dispatchSaeTransportMailboxEvent"):
    require(mailbox_action, token, "mailbox full-FIFO drain")
mailbox_dispatch = function_body(v2, "dispatchSaeTransportMailboxEvent")
for token in ("gate->runAction(&airportItlwmSaeTxCompletionAction",
              "gate->runAction(&airportItlwmSaeTxResetAction",
              "hal->cancelSaeAuthFrame(cancel_ticket)"):
    require(mailbox_dispatch, token, "mailbox controller action route")
reset_action = function_body(v2, "airportItlwmSaeTxResetAction")
for token in ("fSaeLastTerminalTxEventValid",
              "airportItlwmSaeTerminalEventMatches",
              "airportItlwmClearSaeRelayLocked"):
    require(reset_action, token, "exact controller reset invalidation")
forbid(reset_action, "AirportItlwmSaeRelayFsmV1AcceptReply",
       "reset as FSM success")
forbid(reset_action, "cancelSaeAuthFrame", "reset lower-HAL reentry")
disable = airport_method("disableAdapterCore")
ordered(disable, "power-off SAE cancellation", "cancelSaeRelay",
        "fHalService->disable")

# The usable-product boundary remains fail-closed for WPA3 despite the
# isolated physical sender: ingress rejects it, active AKM remains PSK-only,
# generic auth/RX are Open-only, and Agent completion cannot install a PMK.
require(skywalk, "requiresUnsupportedWpa3Auth",
        "WPA3 association ingress quarantine")
require(input_c, "if (algo != IEEE80211_AUTH_ALG_OPEN)",
        "Open-System-only normal RX")
require(proto, "IEEE80211_AUTH_OPEN_REQUEST",
        "Open-System-only protocol producer")
require(crypto, "ic->ic_rsnakms = IEEE80211_AKM_PSK;",
        "active PSK-only AKM")
forbid(crypto, "IEEE80211_AKM_SAE", "active SAE AKM")
complete = airport_method("completeSae")
require(complete, "kIOReturnNotReady", "SAE PMK completion fail-closed")
complete_action = function_body(v2, "airportItlwmCompleteSaeAction")
for token in ("deliverExternalPMK", "ic_psk", "IEEE80211_F_PSK",
              "ieee80211_ioctl_setwpaparms", "IEEE80211_AKM_SAE"):
    forbid(complete_action, token, "SAE PMK completion side effect")

print("PASS: IWX one-ticket SAE auth TX-completion static contract")
PY
