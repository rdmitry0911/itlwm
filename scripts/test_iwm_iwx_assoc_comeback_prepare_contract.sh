#!/usr/bin/env bash
# Lower-driver status-30 prepare-TX parity and lifecycle contract.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

python3 - "$root" <<'PY'
from pathlib import Path
import sys


root = Path(sys.argv[1])


def fail(message):
    raise SystemExit(f"IWM/IWX association prepare contract: {message}")


def body(text, marker, label):
    start = text.find(marker)
    if start < 0:
        fail(f"missing {label}")
    opening = text.find("{", start)
    if opening < 0:
        fail(f"missing body for {label}")
    depth = 0
    for position in range(opening, len(text)):
        if text[position] == "{":
            depth += 1
        elif text[position] == "}":
            depth -= 1
            if depth == 0:
                return text[opening + 1:position]
    fail(f"unterminated {label}")


def ordered(text, label, *tokens):
    cursor = 0
    for token in tokens:
        position = text.find(token, cursor)
        if position < 0:
            fail(f"{label} missing ordered token: {token}")
        cursor = position + len(token)


iwm_cpp = (root / "itlwm/hal_iwm/ItlIwm.cpp").read_text()
iwm_mac = (root / "itlwm/hal_iwm/mac80211.cpp").read_text()
iwm_hw = (root / "itlwm/hal_iwm/hw.cpp").read_text()
iwx_cpp = (root / "itlwm/hal_iwx/ItlIwx.cpp").read_text()

for family, source, callback_marker, task_marker, protection, drain in (
    (
        "IWM", iwm_cpp,
        "int ItlIwm::\niwm_assoc_comeback_retry(",
        "void ItlIwm::\niwm_assoc_comeback_task(",
        "that->iwm_protect_session(sc, in, duration_tu",
        "gate->runAction(_iwm_start_task",
    ),
    (
        "IWX", iwx_cpp,
        "int ItlIwx::\niwx_assoc_comeback_retry(",
        "void ItlIwx::\niwx_assoc_comeback_task_dispatch(",
        "that->iwx_schedule_protect_session(sc, in, duration_tu)",
        "gate->runAction(_iwx_start_task",
    ),
):
    callback = body(source, callback_marker, f"{family} retry callback")
    ordered(
        callback,
        f"{family} immutable publication",
        "sc->sc_assoc_comeback_retry = *retry",
        "sc->sc_assoc_comeback_generation = sc->sc_generation",
        "sc->sc_assoc_comeback_queued = true",
        "task_add(sc->sc_nswq, &sc->assoc_comeback_task)",
    )
    if "ic->ic_bss" in callback or "ieee80211_node *" in callback:
        fail(f"{family} callback resolves a mutable node before deferral")

    task = body(source, task_marker, f"{family} retry task")
    ordered(
        task,
        f"{family} prepare-before-publish ordering",
        "retry = sc->sc_assoc_comeback_retry",
        "generation == sc->sc_generation",
        "ieee80211_pae_assoc_epoch_current(ic) == retry.association_epoch",
        "IEEE80211_ADDR_EQ(ic->ic_bss->ni_bssid, retry.bssid)",
        "duration_tu = MAX(duration_tu, 900U)",
        protection,
        "ieee80211_assoc_comeback_retry_complete(ic, &retry)",
        drain,
    )

for token in (
    "task_set(&sc->assoc_comeback_task, iwm_assoc_comeback_task",
    "ic->ic_assoc_comeback_retry = iwm_assoc_comeback_retry",
    "iwm_del_task(sc, sc->sc_nswq, &sc->assoc_comeback_task)",
    "that->iwm_assoc_comeback_cancel(sc)",
):
    if token not in iwm_mac:
        fail(f"IWM lifecycle missing {token}")
if "that->iwm_assoc_comeback_cancel(sc)" not in iwm_hw:
    fail("IWM hardware reset does not revoke delayed retry work")

for token in (
    "task_set(&sc->assoc_comeback_task, iwx_assoc_comeback_task_dispatch",
    "ic->ic_assoc_comeback_retry = iwx_assoc_comeback_retry",
    "iwx_del_task(sc, sc->sc_nswq, &sc->assoc_comeback_task)",
    "iwx_task_gate_drain(sc",
):
    if token not in iwx_cpp:
        fail(f"IWX lifecycle missing {token}")

print("PASS: IWM/IWX renew firmware association protection before status-30 retry TX")
PY
