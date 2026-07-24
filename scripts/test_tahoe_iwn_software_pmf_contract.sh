#!/usr/bin/env bash
# Source-level contract for the lab-gated IWN software-PMF backend.  This is
# intentionally an owner/lifetime test, not a claim that an on-air WPA3 join
# has passed: IWN still lacks the selected-BSS SAE state owner and association
# bridge, and physical protected-MPDU delivery needs a separately opted-in
# radio run.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

python3 - "$root" <<'PY'
from pathlib import Path
import sys


root = Path(sys.argv[1])
cpp = (root / "itlwm/hal_iwn/ItlIwn.cpp").read_text()
hpp = (root / "itlwm/hal_iwn/ItlIwn.hpp").read_text()
var = (root / "itlwm/hal_iwn/if_iwnvar.h").read_text()


def fail(message: str) -> None:
    raise SystemExit(f"IWN software-PMF contract: {message}")


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


# All owner state is explicit and the normal production binary cannot turn it
# on: only a separately built lab artifact reaches the capability publisher.
for token in (
    "struct iwn_mfp_pae_txn",
    "struct task        mfp_pae_task;",
    "IOSimpleLock       *sc_mfp_pae_lock;",
    "struct iwn_mfp_pae_txn sc_mfp_pae_successor;",
    "u_int32_t          sc_mfp_pae_lifecycle_generation;",
    "volatile u_int32_t sc_mfp_pae_callback_state;",
    "bool                sc_mfp_pae_detaching;",
    "bool                sc_mfp_pae_stopping;",
    "bool                sc_mfp_pae_task_ready;",
    "bool                sc_mfp_pae_lab_enabled;",
):
    require(var, token, "IWN PMF owner storage")
for token in (
    "iwn_mfp_pae_task(void *)",
    "iwn_pae_mfp_txn_submit",
    "iwn_pae_mfp_txn_cancel",
    "iwn_pae_mfp_txn_finish",
    "iwn_mfp_pae_detach_begin",
):
    require(hpp, token, "IWN PMF owner declaration")

require(cpp, "#ifndef IWN_SOFTWARE_PMF_LAB_BUILD", "lab-build default gate")
require(cpp, "#define IWN_SOFTWARE_PMF_LAB_BUILD 0", "disabled lab-build default")
lab = body("iwn_mfp_pae_lab_opted_in(void)", "lab opt-in")
order(lab, "lab-build opt-in", "#if IWN_SOFTWARE_PMF_LAB_BUILD",
      "return true;", "#else", "return false;")
runtime = body("iwn_mfp_runtime_enabled(", "runtime capability gate")
require(runtime, "sc->sc_mfp_pae_lab_enabled", "latched lab gate")
attach = body("iwn_attach(struct iwn_softc *sc", "IWN attach")
order(attach, "latch before publication",
      "sc->sc_mfp_pae_lab_enabled = iwn_mfp_pae_lab_opted_in();",
      "task_set(&sc->mfp_pae_task", "iwn_publish_mfp_capability(sc);")
publish = body("iwn_publish_mfp_capability(", "MFP capability publication")
require(publish, "iwn_mfp_pae_callback_open(sc)", "callback admission open")
hooks = body("iwn_mfp_pae_publish_hooks(", "MFP hook publication")
require(hooks, "ic->ic_caps |= IEEE80211_C_MFP;", "MFP capability bit")
for token in (
    "ic->ic_pae_mfp_txn_submit = ItlIwn::iwn_pae_mfp_txn_submit;",
    "ic->ic_pae_mfp_txn_cancel = ItlIwn::iwn_pae_mfp_txn_cancel;",
    "ic->ic_pae_mfp_txn_finish = ItlIwn::iwn_pae_mfp_txn_finish;",
):
    require(hooks, token, "MFP owner callback")
forbid(cpp, "ic_sae_engine_peer_event =", "IWN SAE capability publication")

# Context allocation cannot run in the RX action.  The serial systq worker
# owns it, while the generic PAE transaction remains the only publisher.
task = body("iwn_mfp_pae_task(void *arg)", "PMF task")
require(task, "ieee80211_set_key(ic, ni, &key);", "software CCMP preparation")
forbid(task, "iwn_set_key(ic, ni, &key)", "firmware key submission from PMF task")
submit = body("iwn_pae_mfp_txn_submit(", "PMF submit")
order(submit, "callback lease and exact generic registration",
      "iwn_mfp_pae_callback_enter(sc)", "held_ni = ieee80211_ref_node(ni);",
      "IOSimpleLockLockDisableInterrupt(bss_lock);",
      "iwn_mfp_pae_generic_stage_live_locked", "(void)task_add(systq, &sc->mfp_pae_task);")
require(submit, "iwn_mfp_pae_callback_leave(sc);", "submit callback release")
require(submit, "sc->sc_mfp_pae_successor", "successor registration")
finish = body("iwn_pae_mfp_txn_finish(", "PMF finish")
order(finish, "software context before atomic generic publish",
      "generic->ptk_key.k_priv = txn->ptk_key.k_priv;",
      "generic->ptk_key.k_flags |= IEEE80211_KEY_SWCRYPTO;",
      "ieee80211_pae_mfp_txn_finish_publish_locked(ic, txn_id);")
require(finish, "generic->gtk_key.k_flags |= IEEE80211_KEY_SWCRYPTO;",
        "GTK software-crypto transfer")
require(finish, "iwn_mfp_pae_callback_leave(sc);", "finish callback release")

# PMF requires one complete software pairwise CCMP lifetime: do not let data
# and robust-management frames share a key while firmware and software own
# separate packet-number/replay state.
rx = body("iwn_rx_done(struct iwn_softc *sc", "IWN RX")
for token in (
    "(ni->ni_pairwise_key.k_flags & IEEE80211_KEY_SWCRYPTO) == 0",
    "(ni->ni_flags & IEEE80211_NODE_MFP) == 0",
):
    require(rx, token, "hardware-decrypt bypass")
tx = body("iwn_tx(struct iwn_softc *sc", "IWN TX")
for token in (
    "(k->k_flags & IEEE80211_KEY_SWCRYPTO)",
    "(ni->ni_flags & IEEE80211_NODE_MFP)",
    "ieee80211_encrypt(ic, m, k)",
):
    require(tx, token, "software encrypt path")
set_key = body("iwn_set_key(struct ieee80211com *ic", "IWN set key")
order(set_key, "software-key bypass", "IEEE80211_KEY_SWCRYPTO",
      "return ieee80211_set_key(ic, ni, k);", "return ops->add_node(sc, &node, 1);")
delete_key = body("iwn_delete_key(struct ieee80211com *ic", "IWN delete key")
order(delete_key, "software-key teardown", "IEEE80211_KEY_SWCRYPTO",
      "ieee80211_delete_key(ic, ni, k);", "(void)ops->add_node(sc, &node, 1);")
run = body("iwn_run(struct iwn_softc *sc", "IWN RUN")
order(run, "raw protected RX policy", "IEEE80211_NODE_MFP",
      "IWN_FILTER_NODECRYPT", "IWN_FILTER_NODECRYPT")
newstate = body("iwn_newstate(struct ieee80211com *ic", "IWN state transition")
require(newstate, "sc->rxon.filter &= ~htole32(IWN_FILTER_NODECRYPT);",
        "RUN-exit decrypt restoration")

# Detach closes admission, removes queued work, and waits for a copied worker
# before either lock is freed.
detach_begin = body("iwn_mfp_pae_detach_begin(struct iwn_softc *sc)",
                    "IWN PMF detach")
order(detach_begin, "detach task drain", "iwn_mfp_pae_callback_close(sc);",
      "sc->sc_mfp_pae_detaching = true;", "ieee80211_pae_mfp_txn_abort(ic);",
      "iwn_mfp_pae_publish_hooks(sc, false);", "iwn_mfp_pae_abort_all(sc);",
      "iwn_mfp_pae_callback_drain(sc);", "task_del(systq, &sc->mfp_pae_task);",
      "taskq_barrier(systq);")
forbid(detach_begin, "systq->", "taskq private-structure access")

stop = body("iwn_stop(struct _ifnet *ifp)", "IWN stop")
order(stop, "stop closes local PMF before generic cancellation",
      "iwn_mfp_pae_abort_all(sc);", "ieee80211_pae_mfp_txn_abort(ic);")
init = body("iwn_init(struct _ifnet *ifp)", "IWN init")
order(init, "fresh PMF generation before scan", "iwn_mfp_pae_reopen(sc);",
      "ieee80211_begin_scan(ifp);")

print("PASS: IWN software-PMF owner is lab-gated and keeps full CCMP lifetime in software")
PY
