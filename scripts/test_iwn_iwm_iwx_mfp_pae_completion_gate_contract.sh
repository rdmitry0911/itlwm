#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

python3 - "$PROJECT_DIR" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
pae_output = (root / "itl80211/openbsd/net80211/ieee80211_pae_output.c").read_text()
iwn = (root / "itlwm/hal_iwn/ItlIwn.cpp").read_text()
iwn_h = (root / "itlwm/hal_iwn/ItlIwn.hpp").read_text()
iwm = (root / "itlwm/hal_iwm/IwmMfpPae.inc").read_text()
iwm_start = (root / "itlwm/hal_iwm/mac80211.cpp").read_text()
iwm_h = (root / "itlwm/hal_iwm/ItlIwm.hpp").read_text()
iwx = (root / "itlwm/hal_iwx/ItlIwx.cpp").read_text()
iwx_h = (root / "itlwm/hal_iwx/ItlIwx.hpp").read_text()

send = pae_output[
    pae_output.index("ieee80211_send_eapol_key(struct ieee80211com"):
    pae_output.index("ieee80211_send_eapol_key(struct ieee80211com") + 5000
]
assert "ifq_enqueue(&ifp->if_snd, m);" in send
assert "(*ifp->if_start)(ifp);" in send

backends = (
    ("IWN", iwn, iwn_h, "iwn", "void ItlIwn::\niwn_mfp_pae_task", iwn),
    ("IWM", iwm, iwm_h, "iwm", "void ItlIwm::\niwm_mfp_pae_task", iwm_start),
    ("IWX", iwx, iwx_h, "iwx", "void ItlIwx::\niwx_mfp_pae_task", iwx),
)

for label, source, header, prefix, task_token, start_source in backends:
    action = f"{prefix}_mfp_pae_complete_action"
    assert action in header, f"{label}: missing completion action declaration"
    action_start = source.index(f"{prefix}_mfp_pae_complete_action(")
    action_body = source[action_start:action_start + 1800]
    assert "ieee80211_pae_mfp_txn_complete(" in action_body, (
        f"{label}: main-gate action does not own generic completion"
    )

    task_start = source.index(task_token)
    task_body = source[task_start:task_start + 12000]
    gate = task_body.index("getMainCommandGate()")
    run = task_body.index(f"gate->runAction({action}", gate)
    fallback = task_body.index("ieee80211_pae_mfp_txn_complete(", run)
    assert gate < run < fallback, f"{label}: completion gate/fallback order"
    assert "EIO" in task_body[run:fallback + 300], (
        f"{label}: missing fail-closed completion result"
    )

    start_token = f"void Itl{label.title()}::\n{prefix}_start(struct _ifnet *ifp)"
    start = start_source.index(start_token)
    start_body = start_source[start:start + 900]
    assert "getMainCommandGate()->attemptAction(" in start_body, (
        f"{label}: test precondition no longer uses a non-blocking TX kick"
    )

print("PASS: IWN/IWM/IWX MFP completion preserves the sole EAPOL TX kick")
PY
