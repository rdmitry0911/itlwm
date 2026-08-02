#!/usr/bin/env bash
# Source contract for IWM's product software-PMF owner.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
python3 - "$root" <<'PY'
from pathlib import Path
import re, sys

root = Path(sys.argv[1])
cpp = (root / "itlwm/hal_iwm/ItlIwm.cpp").read_text()
pmf = (root / "itlwm/hal_iwm/IwmMfpPae.inc").read_text()
mac = (root / "itlwm/hal_iwm/mac80211.cpp").read_text()
hw = (root / "itlwm/hal_iwm/hw.cpp").read_text()
var = (root / "itlwm/hal_iwm/if_iwmvar.h").read_text()

def fail(msg): raise SystemExit(f"IWM software PMF: {msg}")
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

for token in ("struct iwm_mfp_pae_txn", "mfp_pae_task",
              "sc_mfp_pae_lock", "sc_mfp_pae_successor",
              "sc_mfp_pae_lifecycle_generation", "sc_mfp_pae_callback_state",
              "sc_mfp_pae_runtime_enabled"):
    need(var, token, "owner storage")

hooks = body(pmf, "iwm_mfp_pae_publish_hooks")
for token in ("ic->ic_caps |= IEEE80211_C_MFP;",
              "ic->ic_pae_mfp_txn_submit = ItlIwm::iwm_pae_mfp_txn_submit;",
              "ic->ic_pae_mfp_txn_cancel = ItlIwm::iwm_pae_mfp_txn_cancel;",
              "ic->ic_pae_mfp_txn_finish = ItlIwm::iwm_pae_mfp_txn_finish;"):
    need(hooks, token, "published PMF hook")

task = body(pmf, "iwm_mfp_pae_task")
need(task, "ieee80211_set_key(ic, ni, &key);", "sleepable software CCMP preparation")
if "iwm_set_key(ic, ni, &key)" in task: fail("PMF worker enters firmware key path")
submit = body(pmf, "iwm_pae_mfp_txn_submit")
ordered(submit, "exact staged transaction", "iwm_mfp_pae_callback_enter(sc)",
        "held_ni = ieee80211_ref_node(ni)",
        "IOSimpleLockLockDisableInterrupt(bss_lock)",
        "iwm_mfp_pae_generic_stage_live_locked", "task_add(systq, &sc->mfp_pae_task)")
finish = body(pmf, "iwm_pae_mfp_txn_finish")
ordered(finish, "atomic software key publication",
        "generic->ptk_key.k_priv = txn->ptk_key.k_priv",
        "generic->ptk_key.k_flags |= IEEE80211_KEY_SWCRYPTO",
        "generic->gtk_key.k_flags |= IEEE80211_KEY_SWCRYPTO",
        "ieee80211_pae_mfp_txn_finish_publish_locked")

tx = body(mac, "iwm_tx")
for token in ("k->k_flags & IEEE80211_KEY_SWCRYPTO",
              "ni->ni_flags & IEEE80211_NODE_MFP", "ieee80211_encrypt(ic, m, k)"):
    need(tx, token, "software TX lifetime")
rx = body(mac, "iwm_rx_hwdecrypt")
for token in ("ni->ni_pairwise_key.k_flags & IEEE80211_KEY_SWCRYPTO",
              "ni->ni_flags & IEEE80211_NODE_MFP"):
    need(rx, token, "hardware RX-decrypt bypass")
set_key = body(mac, "iwm_set_key")
ordered(set_key, "software set-key bypass", "IEEE80211_KEY_SWCRYPTO",
        "return ieee80211_set_key(ic, ni, k)", "IWM_ADD_STA_KEY")
delete_key = body(mac, "iwm_delete_key")
ordered(delete_key, "software delete-key bypass", "IEEE80211_KEY_SWCRYPTO",
        "ieee80211_delete_key(ic, ni, k)", "IWM_FLAG_STA_ACTIVE")

attach = mac[mac.find("iwm_attach(struct iwm_softc *sc"):
             mac.find("fail5:", mac.find("iwm_attach(struct iwm_softc *sc"))]
ordered(attach, "attach publication", "sc->sc_mfp_pae_runtime_enabled = true",
        "task_set(&sc->mfp_pae_task", "iwm_publish_mfp_capability(sc)")
init = mac[mac.find("iwm_init(struct _ifnet *ifp"):
           mac.find("_iwm_start_task", mac.find("iwm_init(struct _ifnet *ifp"))]
ordered(init, "fresh PMF generation", "iwm_mfp_pae_reopen(sc)",
        "iwm_sae_tx_reopen(sc)", "iwm_sae_engine_reopen(sc)")
ordered(hw, "stop cancellation", "iwm_mfp_pae_abort_all(sc)",
        "ieee80211_pae_mfp_txn_abort")
detach = body(cpp, "detach")
ordered(detach, "detach PMF drain", "iwm_mfp_pae_detach_begin(sc)",
        "ieee80211_ifdetach(ifp)", "iwm_mfp_pae_callback_destroy(sc)")
print("PASS: IWM software PMF owns one complete software CCMP/BIP lifetime")
PY
