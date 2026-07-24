#!/usr/bin/env bash
# Contract for IWN's PMF reconnect owner.  This is intentionally source-level:
# the physical lab run remains the separate proof that protected MPDUs pass on
# the radio, while this catches the lifecycle races that would otherwise make
# that run non-repeatable.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

python3 - "$root" <<'PY'
from pathlib import Path
import sys


root = Path(sys.argv[1])
cpp = (root / "itlwm/hal_iwn/ItlIwn.cpp").read_text()
var = (root / "itlwm/hal_iwn/if_iwnvar.h").read_text()


def fail(message: str) -> None:
    raise SystemExit(f"IWN PMF reconnect contract: {message}")


def require(text: str, token: str, label: str) -> None:
    if token not in text:
        fail(f"missing {label}: {token}")


def forbid(text: str, token: str, label: str) -> None:
    if token in text:
        fail(f"unexpected {label}: {token}")


def order(text: str, label: str, *tokens: str) -> None:
    cursor = 0
    for token in tokens:
        position = text.find(token, cursor)
        if position < 0:
            fail(f"{label} missing ordered token: {token}")
        cursor = position + len(token)


def body(marker: str, label: str) -> str:
    start = cpp.find(marker)
    if start < 0:
        fail(f"missing {label}")
    opening = cpp.find("{", start)
    if opening < 0:
        fail(f"missing body for {label}")
    depth = 0
    for index in range(opening, len(cpp)):
        if cpp[index] == "{":
            depth += 1
        elif cpp[index] == "}":
            depth -= 1
            if depth == 0:
                return cpp[opening + 1:index]
    fail(f"unterminated {label}")


for token in (
    "struct iwn_mfp_pae_txn sc_mfp_pae_txn;",
    "struct iwn_mfp_pae_txn sc_mfp_pae_successor;",
    "u_int32_t          sc_mfp_pae_lifecycle_generation;",
    "volatile u_int32_t sc_mfp_pae_callback_state;",
    "bool                sc_mfp_pae_stopping;",
    "u_int32_t               lifecycle_generation;",
):
    require(var, token, "two-slot lifecycle storage")

enter = body("iwn_mfp_pae_callback_enter(struct iwn_softc *sc)",
             "callback enter")
for token in (
    "IWN_MFP_PAE_CALLBACK_CLOSED",
    "IWN_MFP_PAE_CALLBACK_COUNT_MASK",
    "__atomic_compare_exchange_n",
):
    require(enter, token, "atomic callback admission")
close = body("iwn_mfp_pae_callback_close(struct iwn_softc *sc)",
             "callback close")
require(close, "__atomic_compare_exchange_n", "CAS close")
leave = body("iwn_mfp_pae_callback_leave(struct iwn_softc *sc)",
             "callback leave")
require(leave, "__atomic_compare_exchange_n", "nonblocking callback release")
forbid(leave, "IOLockLock", "sleepable callback release")
drain = body("iwn_mfp_pae_callback_drain(struct iwn_softc *sc)",
             "callback drain")
for token in (
    "IWN_MFP_PAE_CALLBACK_COUNT_MASK",
    "IOSleep(1);",
):
    require(drain, token, "closed detach recheck")
forbid(drain, "IOLockSleep", "lost-wakeup-prone detach wait")

submit = body("iwn_pae_mfp_txn_submit(", "PMF submit")
order(submit, "callback admission before owner state",
      "sc = (struct iwn_softc *)ic->ic_softc;",
      "iwn_mfp_pae_callback_enter(sc)", "iwn_mfp_runtime_enabled(sc)")
order(submit, "pre-registration exact generic fence",
      "IOSimpleLockLockDisableInterrupt(bss_lock);",
      "IOSimpleLockLock(sc->sc_mfp_pae_lock);",
      "iwn_mfp_pae_generic_stage_live_locked")
for token in (
    "sc->sc_mfp_pae_successor",
    "iwn_mfp_pae_take_record_locked(successor",
    "target->lifecycle_generation",
    "iwn_mfp_pae_callback_leave(sc);",
):
    require(submit, token, "successor submit semantics")
forbid(submit, "error = EBUSY", "backend-local busy rejection")

task = body("iwn_mfp_pae_task(void *arg)", "PMF task")
for token in (
    "generation == sc->sc_mfp_pae_lifecycle_generation",
    "iwn_mfp_pae_generic_stage_live_locked",
    "iwn_mfp_pae_take_record_locked(txn, &retired_main)",
    "iwn_mfp_pae_promote_successor_locked(sc,",
    "iwn_mfp_pae_dispose_record(ic, &retired_main);",
):
    require(task, token, "old-worker lifecycle fence")
order(task, "promotion only after old record retirement",
      "iwn_mfp_pae_take_record_locked(txn, &retired_main)",
      "iwn_mfp_pae_promote_successor_locked(sc,",
      "(void)task_add(systq, &sc->mfp_pae_task);")

cancel = body("iwn_pae_mfp_txn_cancel(", "PMF cancel")
for token in (
    "iwn_mfp_pae_callback_enter(sc)",
    "sc->sc_mfp_pae_txn.active",
    "sc->sc_mfp_pae_successor.active",
    "iwn_mfp_pae_callback_leave(sc);",
):
    require(cancel, token, "exact cancellation across both slots")

abort_all = body("iwn_mfp_pae_abort_all(struct iwn_softc *sc)",
                 "PMF abort all")
order(abort_all, "stop cancels rather than promotes",
      "sc->sc_mfp_pae_stopping = true;",
      "iwn_mfp_pae_generation_advance_locked(sc);",
      "iwn_mfp_pae_cancel_record_locked(&sc->sc_mfp_pae_txn,",
      "iwn_mfp_pae_cancel_record_locked(&sc->sc_mfp_pae_successor,")
forbid(abort_all, "iwn_mfp_pae_promote_successor_locked", "stop promotion")

detach = body("iwn_mfp_pae_detach_begin(struct iwn_softc *sc)",
              "PMF detach")
order(detach, "detach producer quiescence",
      "iwn_mfp_pae_callback_close(sc);",
      "sc->sc_mfp_pae_detaching = true;",
      "ieee80211_pae_mfp_txn_abort(ic);",
      "iwn_mfp_pae_publish_hooks(sc, false);",
      "iwn_mfp_pae_abort_all(sc);",
      "iwn_mfp_pae_callback_drain(sc);",
      "task_del(systq, &sc->mfp_pae_task);",
      "taskq_barrier(systq);")

init = body("iwn_init(struct _ifnet *ifp)", "IWN init")
order(init, "restart only reopens after hardware config",
      "iwn_config(sc)", "iwn_mfp_pae_reopen(sc);", "ieee80211_begin_scan(ifp);")
stop = body("iwn_stop(struct _ifnet *ifp)", "IWN stop")
order(stop, "stop fences worker before generic abort",
      "iwn_mfp_pae_abort_all(sc);", "ieee80211_pae_mfp_txn_abort(ic);")

print("PASS: IWN PMF reconnect owner fences stale callbacks, workers, and stop/restart")
PY
