#!/usr/bin/env bash
# Source contract for IWM's password -> SAE -> PMK -> ASSOC owner.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
bash "$root/scripts/test_net80211_sae_eapol_key_descriptor_contract.sh"

python3 - "$root" <<'PY'
from pathlib import Path
import re, sys

root = Path(sys.argv[1])
cpp = (root / "itlwm/hal_iwm/ItlIwm.cpp").read_text()
engine = (root / "itlwm/hal_iwm/IwmSaeEngine.inc").read_text()
mac = (root / "itlwm/hal_iwm/mac80211.cpp").read_text()
hw = (root / "itlwm/hal_iwm/hw.cpp").read_text()
var = (root / "itlwm/hal_iwm/if_iwmvar.h").read_text()
hpp = (root / "itlwm/hal_iwm/ItlIwm.hpp").read_text()
skywalk = (root / "AirportItlwm/AirportItlwmSkywalkInterface.cpp").read_text()

def fail(msg): raise SystemExit(f"IWM driver-resident SAE owner: {msg}")
def need(text, token, label):
    if token not in text: fail(f"missing {label}: {token}")
def body(text, name):
    m = re.search(r"\b" + re.escape(name) + r"\s*\([^;{}]*\)\s*\{", text, re.S)
    if not m: fail(f"missing function {name}")
    start = text.rfind("{", m.start(), m.end()); depth = 0
    for i in range(start, len(text)):
        depth += text[i] == "{"; depth -= text[i] == "}"
        if depth == 0: return text[start + 1:i]
    fail(f"unterminated function {name}")
def ordered(text, label, *tokens):
    pos = 0
    for token in tokens:
        pos = text.find(token, pos)
        if pos < 0: fail(f"{label} missing ordered token: {token}")
        pos += len(token)

for token in ("struct iwm_sae_engine_owner", "completion_claimed",
              "assoc_tx_pending", "assoc_tx_accepted",
              "sc_sae_engine_lock", "sc_sae_wcl_credential_lock",
              "sc_sae_tx_direct_cancel_through"):
    need(var, token, "private owner state")
for token in ("stageSaeWclCredential", "iwm_sae_auth_hold",
              "iwm_sae_engine_peer_event", "iwm_sae_engine_task",
              "iwm_sae_wnm_roam_start", "iwm_sae_wcl_roam_start",
              "supportsDriverResidentSae"):
    need(hpp, token, "HAL declaration")

runtime = body(engine, "iwm_sae_engine_runtime_enabled")
for token in ("ITL_SAE_DRIVER_CRYPTO_AVAILABLE", "iwm_mfp_runtime_enabled(sc)",
              "sc_sae_tx_lifecycle_lock", "systq != NULL",
              "ic_pae_selected_bss_lock"):
    need(runtime, token, "runtime gate")

terminal = body(cpp, "iwm_sae_tx_task")
ordered(terminal, "native terminal routing", "iwm_sae_engine_queue_terminal",
        "iwm_sae_tx_ticket_is_direct", "engine_consumed",
        "IEEE80211_EVT_SAE_AUTH_TRANSPORT")

stage = body(engine, "stageSaeWclCredential")
for token in ("itl_sae_wcl_credential_is_well_formed",
              "iwm_sae_wcl_credential_cancelled_locked",
              "sc->sc_sae_wcl_credential = copy", "explicit_bzero(&copy"):
    need(stage, token, "bounded credential ingress")
start = body(engine, "iwm_sae_engine_start")
ordered(start, "credential to crypto",
        "ieee80211_sae_wcl_request_copyout_bound_current",
        "iwm_sae_wcl_credential_take_bound", "ieee80211_sae_engine_begin",
        "explicit_bzero(&credential", "iwm_sae_engine_submit_prepared")
worker = body(engine, "iwm_sae_engine_task")
ordered(worker, "verified confirm to PMK",
        "IEEE80211_SAE_ENGINE_PEER_COMPLETE",
        "itl_sae_pmk_continuation_is_well_formed",
        "ieee80211_sae_wcl_request_pmk_claim_locked",
        "owner->completion_claimed = true", "owner->assoc_tx_pending = true",
        "ieee80211_sae_wcl_request_pmk_continue_assoc")

commit = body(engine, "iwm_sae_engine_assoc_tx_commit")
ordered(commit, "ASSOC descriptor fence",
        "ieee80211_sae_wcl_request_pmk_claim_assoc_current_locked",
        "that->iwm_update_sched", "ring->cur = next_cur",
        "IWM_WRITE(sc, IWM_HBUS_TARG_WRPTR", "ring->qid << 8 | ring->cur",
        "owner->assoc_tx_pending = false", "owner->assoc_tx_accepted = true")
tx = body(mac, "iwm_tx")
ordered(tx, "pre-trim/final ASSOC ownership",
        "iwm_sae_engine_assoc_tx_preflight", "mbuf_adj(m, hdrlen)",
        "iwm_sae_engine_assoc_tx_commit")

roam = body(engine, "iwm_sae_targeted_roam_start")
ordered(roam, "active ESS retarget", "sc->sc_sae_wcl_credential_active",
        "credential = sc->sc_sae_wcl_credential",
        "IEEE80211_NEWSTATE_ARG_WNM_RECONNECT_HOLD",
        "ieee80211_sae_wcl_request_begin", "stageSaeWclCredential(&credential)",
        "ieee80211_node_join_bss", "ieee80211_sae_wcl_request_bound_current")
for token in ("ic->ic_sae_wnm_roam_start = ItlIwm::iwm_sae_wnm_roam_start",
              "ic->ic_sae_wcl_roam_start = ItlIwm::iwm_sae_wcl_roam_start"):
    need(engine, token, "multi-AP hook")

ordered(hw, "hardware stop", "iwm_sae_engine_stop_begin(sc)",
        "iwm_sae_tx_stop_begin(sc)", "iwm_sae_wcl_stop_begin(sc)",
        "iwm_mfp_pae_abort_all(sc)", "ieee80211_pae_mfp_txn_abort")
ordered(cpp, "detach", "iwm_sae_engine_detach_begin(sc)",
        "iwm_sae_tx_detach_begin(sc)", "iwm_sae_wcl_detach_begin(sc)",
        "iwm_mfp_pae_detach_begin(sc)", "ieee80211_ifdetach(ifp)")
need(skywalk, "!fHalService->supportsDriverResidentSae()",
     "backend-neutral upper admission")
need(skywalk, "OSDynamicCast(ItlIwn, fHalService) == nullptr",
     "IWN-only diagnostic stimulus")
print("PASS: IWM owns SAE credential, crypto, PMK, native ASSOC and multi-AP replay")
PY
