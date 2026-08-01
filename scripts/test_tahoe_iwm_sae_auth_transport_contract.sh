#!/bin/bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

python3 - "$ROOT" <<'PY'
from pathlib import Path
import re
import sys

root = Path(sys.argv[1])
cpp = (root / "itlwm/hal_iwm/ItlIwm.cpp").read_text()
hpp = (root / "itlwm/hal_iwm/ItlIwm.hpp").read_text()
var = (root / "itlwm/hal_iwm/if_iwmvar.h").read_text()
mac = (root / "itlwm/hal_iwm/mac80211.cpp").read_text()
tx = (root / "itlwm/hal_iwm/tx.cpp").read_text()
hw = (root / "itlwm/hal_iwm/hw.cpp").read_text()

def fail(message):
    raise SystemExit(f"FAIL: {message}")

def require(text, token, message):
    if token not in text:
        fail(message)

def body(text, name):
    match = re.search(rf"\b{re.escape(name)}\s*\([^;]*?\)\s*\{{", text, re.S)
    if not match:
        fail(f"missing body: {name}")
    start = match.end()
    depth = 1
    index = start
    while index < len(text) and depth:
        depth += (text[index] == "{") - (text[index] == "}")
        index += 1
    if depth:
        fail(f"unterminated body: {name}")
    return text[start:index - 1]

for token in (
    "submitSaeAuthFrame(", "cancelSaeAuthFrame(uint64_t ticket) override",
    "IOCommandGate *fSaeTxGate", "iwm_sae_tx_commit_doorbell",
    "iwm_sae_tx_snapshot_reset", "iwm_sae_tx_detach_begin",
):
    require(hpp, token, f"IWM HAL surface lacks {token}")

for token in (
    "sae_association_epoch", "sae_relay_generation", "sae_ticket",
    "sae_lifecycle_generation", "sc_sae_tx_lifecycle_lock",
    "sc_sae_tx_active_generation", "sc_sae_tx_direct_cancel_through",
    "IWM_SAE_TX_EVENTQ_LEN 4",
):
    require(var, token, f"descriptor/lifecycle owner lacks {token}")

submit = body(cpp, "submitSaeAuthFrame")
for token in (
    "itl_sae_auth_transport_request_is_well_formed",
    "iwm_sae_tx_lifecycle_enter", "fSaeTxGate", "gate->retain()",
    "attemptAction", "iwm_sae_tx_retire_unsubmitted",
):
    require(submit, token, f"submit admission lacks {token}")

leaf = body(cpp, "iwm_sae_tx_submit_on_gate")
for token in (
    "ieee80211_sae_auth_frame_build", "IEEE80211_S_AUTH",
    "ieee80211_pae_assoc_epoch_current", "ni->ni_bssid",
    "ic->ic_myaddr", "iwm_tx(sc, m, ni, EDCA_AC_BE, request)",
):
    require(leaf, token, f"selected-BSS TX leaf lacks {token}")

doorbell = body(cpp, "iwm_sae_tx_commit_doorbell")
for token in (
    "IOLockLock(sc->sc_sae_tx_lifecycle_lock)",
    "IOSimpleLockLock(sc->sc_sae_tx_lock)",
    "sc->sc_sae_tx_active_generation == sc->sc_sae_tx_generation",
    "iwm_update_sched", "sc->sc_sae_tx_doorbelled = true",
    "IWM_WRITE(sc, IWM_HBUS_TARG_WRPTR",
):
    require(doorbell, token, f"atomic scheduler/doorbell fence lacks {token}")
if doorbell.index("iwm_update_sched") > doorbell.index("IWM_WRITE(sc, IWM_HBUS_TARG_WRPTR"):
    fail("scheduler publication must precede the physical doorbell")

tx_body = body(mac, "iwm_tx")
for token in (
    "IEEE80211_AUTH_ALG_SAE", "sae_request->wire_transaction",
    "sae_request->auth_status", "sae_request->body_len",
    "iwm_sae_tx_data_clear(data)", "data->sae_ticket",
    "iwm_sae_tx_commit_doorbell", "memset(desc, 0, sizeof(*desc))",
):
    require(tx_body, token, f"IWM Algorithm-3 descriptor path lacks {token}")
if tx_body.index("data->sae_ticket") > tx_body.index("iwm_sae_tx_commit_doorbell"):
    fail("ticket identity must be captured before the doorbell")

completion = body(mac, "iwm_rx_tx_cmd_single")
require(completion, "if (txd->sae_active)", "firmware TX response ignores SAE descriptor")
require(completion, "iwm_sae_tx_report_terminal", "firmware TX response lacks terminal publication")
report_at = completion.index("iwm_sae_tx_report_terminal")
reclaim_at = completion.find("iwm_txd_done(sc, txd)", report_at)
if reclaim_at < 0 or report_at > reclaim_at:
    fail("terminal identity is published after descriptor reclaim")

done = body(mac, "iwm_txd_done")
require(done, "iwm_sae_tx_report_terminal(sc, txd, EIO)",
        "non-response reclaim is not fail-closed")
for text, name in ((tx, "iwm_reset_tx_ring"), (tx, "iwm_free_tx_ring")):
    reset = body(text, name)
    require(reset, "if (data->sae_active)", f"{name} ignores SAE ownership")
    require(reset, "iwm_sae_tx_report_terminal(sc, data, EIO)",
            f"{name} lacks fail-closed terminal")
    require(reset, "ieee80211_release_node", f"{name} leaks retained SAE node")

stop = body(hw, "iwm_stop_device")
for token in (
    "iwm_sae_tx_stop_begin", "iwm_sae_tx_snapshot_reset",
    "iwm_sae_tx_cancel_all", "iwm_reset_tx_ring",
    "iwm_sae_tx_purge", "iwm_sae_tx_emit_reset_event",
):
    require(stop, token, f"hardware reset boundary lacks {token}")
if not (stop.index("iwm_sae_tx_snapshot_reset") <
        stop.index("iwm_reset_tx_ring") < stop.index("iwm_sae_tx_purge") <
        stop.index("iwm_sae_tx_emit_reset_event")):
    fail("reset snapshot/reclaim/purge/publication order is unsafe")

detach = body(cpp, "iwm_sae_tx_detach_begin")
for token in (
    "iwm_sae_tx_lifecycle_close(sc, true)", "task_del",
    "taskq_barrier", "iwm_sae_tx_lifecycle_drain", "removeEventSource",
):
    require(detach, token, f"detach lifetime fence lacks {token}")

print("PASS: IWM one-ticket SAE auth TX-completion static contract")
PY
