#!/usr/bin/env bash
# Contract for the selected API-68 IWX driver-resident SAE owner. This proves
# the in-kext credential -> crypto -> native TX/RX -> PMK -> ASSOC doorbell
# route; physical AX210/AX211/AX411 runtime remains a separate requirement.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

bash "$root/scripts/test_tahoe_ax211_api68_pmf_transaction_owner_contract.sh"
bash "$root/scripts/test_net80211_sae_eapol_key_descriptor_contract.sh"

python3 - "$root" <<'PY'
from pathlib import Path
import re
import sys

root = Path(sys.argv[1])
cpp = (root / "itlwm/hal_iwx/ItlIwx.cpp").read_text()
engine = (root / "itlwm/hal_iwx/IwxSaeEngine.inc").read_text()
var = (root / "itlwm/hal_iwx/if_iwxvar.h").read_text()
hpp = (root / "itlwm/hal_iwx/ItlIwx.hpp").read_text()
hal = (root / "include/HAL/ItlHalService.hpp").read_text()
skywalk = (root / "AirportItlwm/AirportItlwmSkywalkInterface.cpp").read_text()
v2 = (root / "AirportItlwm/AirportItlwmV2.cpp").read_text()
sta = (root / "AirportItlwm/AirportSTAIOCTL.cpp").read_text()


def fail(message: str) -> None:
    raise SystemExit(f"IWX driver-resident SAE owner contract: {message}")


def require(text: str, needle: str, label: str) -> None:
    if needle not in text:
        fail(f"missing {label}: {needle}")


def forbid(text: str, needle: str, label: str) -> None:
    if needle in text:
        fail(f"unexpected {label}: {needle}")


def block_after(source: str, opening: int, label: str) -> str:
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
    fail(f"unterminated {label}")


def body(source: str, name: str, label: str) -> str:
    pattern = re.compile(r"\b" + re.escape(name) +
                         r"\s*\([^;{}]*\)\s*\{", re.S)
    match = pattern.search(source)
    if match is None:
        fail(f"missing {label}")
    opening = source.rfind("{", match.start(), match.end())
    return block_after(source, opening, label)


def method(source: str, name: str) -> str:
    pattern = re.compile(r"\bItlIwx\s*::\s*" + re.escape(name) +
                         r"\s*\([^;{}]*\)\s*\{", re.S)
    match = pattern.search(source)
    if match is None:
        fail(f"missing ItlIwx::{name}()")
    opening = source.rfind("{", match.start(), match.end())
    return block_after(source, opening, f"ItlIwx::{name}")


def ordered(text: str, label: str, *needles: str) -> None:
    cursor = 0
    for needle in needles:
        index = text.find(needle, cursor)
        if index < 0:
            fail(f"{label} missing ordered token: {needle}")
        cursor = index + len(needle)


for token in (
        "struct iwx_sae_engine_owner", "completion_claimed",
        "assoc_tx_pending", "assoc_tx_accepted",
        "sc_sae_engine_lock", "sc_sae_wcl_credential_lock",
        "sc_sae_tx_direct_cancel_through"):
    require(var, token, "private IWX owner state")
for token in (
        "stageSaeWclCredential", "iwx_sae_auth_hold",
        "iwx_sae_engine_peer_event", "iwx_sae_engine_task",
        "iwx_sae_wnm_roam_start", "iwx_sae_wcl_roam_start",
        "supportsDriverResidentSae"):
    require(hpp, token, "IWX HAL declaration")

runtime = body(engine, "iwx_sae_engine_runtime_enabled", "runtime gate")
for token in ("ITL_SAE_DRIVER_CRYPTO_AVAILABLE", "iwx_mfp_runtime_enabled(sc)",
              "sc_task_gate_lock", "sc_nswq",
              "ic_pae_selected_bss_lock"):
    require(runtime, token, "complete API-68 runtime gate")

ticket = body(cpp, "iwx_sae_tx_ticket_cancelled_locked",
              "ticket-domain cancellation")
for token in ("iwx_sae_tx_ticket_is_direct", "IWX_SAE_ENGINE_TICKET_COUNTER_MASK",
              "sc_sae_tx_direct_cancel_through", "sc_sae_tx_cancel_through"):
    require(ticket, token, "independent direct ticket domain")
dispatch = method(cpp, "iwx_sae_tx_task_dispatch")
ordered(dispatch, "native terminal routing",
        "iwx_sae_engine_queue_terminal", "engine_consumed",
        "iwx_task_gate_leave", "IEEE80211_EVT_SAE_AUTH_TRANSPORT")

stage = method(engine, "stageSaeWclCredential")
for token in ("itl_sae_wcl_credential_is_well_formed",
              "iwx_sae_wcl_credential_cancelled_locked",
              "sc_sae_wcl_credential = copy", "explicit_bzero(&copy"):
    require(stage, token, "bounded credential staging")
start = body(engine, "iwx_sae_engine_start", "engine start")
ordered(start, "credential-to-crypto path",
        "ieee80211_sae_wcl_request_copyout_bound_current",
        "iwx_sae_wcl_credential_take_bound", "ieee80211_sae_engine_begin",
        "explicit_bzero(&credential", "iwx_sae_engine_submit_prepared")
submit = body(engine, "iwx_sae_engine_submit_prepared", "engine submit")
for token in ("IWX_SAE_ENGINE_TICKET_DIRECT_BIT",
              "ieee80211_sae_engine_prepare_tx",
              "that->submitSaeAuthFrame(&request)",
              "ieee80211_sae_engine_tx_rollback_unsubmitted"):
    require(submit, token, "driver-owned native TX")

worker = method(engine, "iwx_sae_engine_task")
ordered(worker, "verified Confirm to PMK claim",
        "IEEE80211_SAE_ENGINE_PEER_COMPLETE",
        "itl_sae_pmk_continuation_is_well_formed",
        "IOSimpleLockLockDisableInterrupt(bss_lock)",
        "IOSimpleLockLock(sc->sc_sae_engine_lock)",
        "ieee80211_sae_wcl_request_pmk_claim_locked",
        "owner->completion_claimed = true",
        "owner->assoc_tx_pending = true",
        "ieee80211_sae_wcl_request_pmk_continue_assoc")
for token in ("installExternalPmkLocked", "programPMK", "0x10c", "Agent"):
    forbid(worker, token, "BCM/controller PMK detour")

preflight = body(engine, "iwx_sae_engine_assoc_tx_preflight",
                 "ASSOC preflight")
for token in ("IWX_SAE_ASSOC_TX_REJECTED", "completion_claimed",
              "ieee80211_sae_wcl_request_pmk_claim_assoc_current_locked"):
    require(preflight, token, "selected-BSS ASSOC preflight")
commit = body(engine, "iwx_sae_engine_assoc_tx_commit", "ASSOC commit")
ordered(commit, "final IWX descriptor doorbell",
        "IOSimpleLockLockDisableInterrupt(bss_lock)",
        "IOSimpleLockLock(sc->sc_sae_engine_lock)",
        "ieee80211_sae_wcl_request_pmk_claim_assoc_current_locked",
        "ring->cur = next_cur", "IWX_WRITE(sc, IWX_HBUS_TARG_WRPTR",
        "ring->qid << 16 | ring->cur",
        "owner->assoc_tx_pending = false",
        "owner->assoc_tx_accepted = true")
tx = method(cpp, "iwx_tx")
ordered(tx, "pre-trim/final ASSOC ownership",
        "iwx_sae_engine_assoc_tx_preflight", "mbuf_adj(m, hdrlen)",
        "iwx_sae_engine_assoc_tx_commit")

targeted = method(engine, "iwx_sae_targeted_roam_start")
ordered(targeted, "active-ESS SAE retarget",
        "sc->sc_sae_wcl_credential_active",
        "credential = sc->sc_sae_wcl_credential",
        "source_generation = credential.request_generation",
        "ieee80211_sae_wcl_request_retarget_run",
        "ieee80211_match_bss(ic, candidate, 0)",
        "that->stageSaeWclCredential(&credential)",
        "LOWER_RETARGET_ACCEPTED",
        "ieee80211_node_join_bss",
        "ieee80211_sae_wcl_request_bound_current")
forbid(targeted, "IEEE80211_NEWSTATE_ARG_WNM_RECONNECT_HOLD",
       "asynchronous destructive RUN-to-SCAN retarget")
for token in (
        "ic->ic_sae_wnm_roam_start = ItlIwx::iwx_sae_wnm_roam_start",
        "ic->ic_sae_wcl_roam_start = ItlIwx::iwx_sae_wcl_roam_start",
        "ic->ic_sae_wnm_roam_start = NULL",
        "ic->ic_sae_wcl_roam_start = NULL"):
    require(engine, token, "targeted roam hook lifetime")

for token in ("iwx_sae_engine_stop_begin(sc)",
              "iwx_sae_wcl_stop_begin(sc)", "iwx_task_gate_close"):
    require(cpp, token, "stop lifecycle")
if cpp.find("iwx_sae_engine_stop_begin(sc)") > cpp.find(
        "iwx_task_gate_close(sc, false"):
    fail("engine stop must precede ordinary task-gate close")
for token in ("iwx_sae_engine_detach_begin(sc)",
              "iwx_sae_wcl_detach_begin(sc)",
              "iwx_task_gate_close(sc, true"):
    require(cpp, token, "detach lifecycle")
if cpp.find("iwx_sae_engine_detach_begin(sc)") > cpp.find(
        "iwx_task_gate_close(sc, true"):
    fail("engine detach must precede permanent task-gate close")

require(hal, "virtual bool supportsDriverResidentSae() { return false; }",
        "fail-closed HAL feature")
for source, label in ((v2, "Tahoe controller capability"),
                      (sta, "legacy capability shadow")):
    require(source, "fHalService->supportsDriverResidentSae()", label)
start_upper = body(skywalk, "startIwnDirectSaeCredential",
                   "backend-neutral upper WCL SAE start")
require(start_upper, "!fHalService->supportsDriverResidentSae()",
        "backend-neutral WCL admission")
require(start_upper, "OSDynamicCast(ItlIwn, fHalService) == nullptr",
        "IWN-only diagnostic stimulus")

print("PASS: selected API-68 IWX owns SAE credential, crypto, PMK, native ASSOC doorbell and multi-AP retarget")
PY
