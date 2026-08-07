#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

python3 - "$ROOT" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
iwm = (root / "itlwm/hal_iwm/mac80211.cpp").read_text(encoding="utf-8")
iwx = (root / "itlwm/hal_iwx/ItlIwx.cpp").read_text(encoding="utf-8")
reg = (root / "itlwm/hal_iwx/if_iwxreg.h").read_text(encoding="utf-8")


def fail(message):
    raise SystemExit(f"IWM/IWX management transaction contract: {message}")


def body(text, marker, label):
    start = text.find(marker)
    if start < 0:
        fail(f"missing {label}")
    opening = text.find("{", start)
    if opening < 0:
        fail(f"missing body for {label}")
    depth = 0
    for pos in range(opening, len(text)):
        if text[pos] == "{":
            depth += 1
        elif text[pos] == "}":
            depth -= 1
            if depth == 0:
                return text[opening + 1:pos]
    fail(f"unterminated {label}")


def require(text, needle, label):
    if needle not in text:
        fail(f"missing {label}: {needle}")


def ordered(text, needles, label):
    cursor = 0
    for needle in needles:
        cursor = text.find(needle, cursor)
        if cursor < 0:
            fail(f"invalid {label} order at {needle}")
        cursor += len(needle)


for source, marker, start_task, label in (
    (iwm, "iwm_newstate_task(void *psc)", "_iwm_start_task", "IWM AUTH drain"),
    (iwx, "iwx_newstate_task(void *psc)", "_iwx_start_task", "IWX AUTH drain"),
):
    state = body(source, marker, label)
    for token in (
        "const int state",
        "sc->sc_newstate(ic, nstate, arg)",
        "nstate == IEEE80211_S_AUTH",
        "getMainCommandGate()",
        f"gate->runAction({start_task}, &ic->ic_ac.ac_if)",
        "kIOReturnNotReady",
        "could not drain AUTH management frame",
    ):
        require(state, token, label)
    ordered(state, (
        "sc->sc_newstate(ic, nstate, arg)",
        "nstate == IEEE80211_S_AUTH",
        f"gate->runAction({start_task}, &ic->ic_ac.ac_if)",
    ), label)

enable = body(iwx, "iwx_enable_txq(struct iwx_softc", "IWX TXQ enable")
for token in (
    "IWX_SCD_QUEUE_CONFIG_CMD",
    "cmd_ver == 0 || cmd_ver == IWX_FW_CMD_VER_UNKNOWN",
    "IWX_TX_QUEUE_CFG_ENABLE_QUEUE",
    "cmd_ver == 3",
    "IWX_SCD_QUEUE_ADD",
    "IWX_WIDE_ID(IWX_DATA_PATH_GROUP",
    "sc->sc_tid_data[IWX_MAX_TID_COUNT].qid = qid",
):
    require(enable, token, "IWX TXQ enable")

disable = body(iwx, "iwx_disable_txq(struct iwx_softc", "IWX TXQ disable")
for token in (
    "ring->queued == 0 && ring->tail == ring->cur",
    "cmd_ver == 0 || cmd_ver == IWX_FW_CMD_VER_UNKNOWN",
    "IWX_SCD_QUEUE_REMOVE",
    "iwx_send_cmd(sc, &hcmd)",
    "iwx_reset_tx_ring(sc, ring)",
    "sc->sc_tid_data[IWX_MAX_TID_COUNT].qid = IWX_INVALID_QUEUE",
):
    require(disable, token, "IWX TXQ disable")
ordered(disable, (
    "IOSimpleLockUnlock(txq_lock)",
    "iwx_send_cmd(sc, &hcmd)",
    "iwx_reset_tx_ring(sc, ring)",
), "IWX non-sleeping TXQ teardown")
reset_tail = disable[disable.find("iwx_reset_tx_ring(sc, ring)") - 80:]
if "IOSimpleLockLock(txq_lock)" in reset_tail:
    fail("TX ring reset must not run under the queue spinlock")

flush_tids = body(iwx, "iwx_flush_sta_tids(struct iwx_softc", "IWX flush response")
for token in (
    "tid == IWX_MGMT_TID ? IWX_MAX_TID_COUNT : tid",
    "nitems(sc->sc_tid_data)",
    "sc->sc_tid_data[tid_slot].qid != qid",
    "iwx_ampdu_txq_advance(sc, txq, IWX_AGG_SSN_TO_TXQ_IDX(read_after",
):
    require(flush_tids, token, "sparse management TID reclaim")

flush = body(iwx, "iwx_flush_sta(struct iwx_softc", "IWX flush fence")
for token in (
    "inherited_flush",
    "sc->sc_flags |= IWX_FLAG_TXFLUSH",
    "if (!inherited_flush)",
    "sc->sc_flags &= ~IWX_FLAG_TXFLUSH",
):
    require(flush, token, "nested TX flush fence")

remove = body(iwx, "iwx_rm_sta(struct iwx_softc", "IWX STA removal")
for token in (
    "sc->sc_flags |= IWX_FLAG_TXFLUSH",
    "iwx_flush_sta(sc, in)",
    "iwx_disable_txq(sc, IWX_STATION_ID",
    "iwx_rm_sta_cmd(sc, in)",
    "if (!inherited_flush)",
):
    require(remove, token, "IWX STA removal")
ordered(remove, (
    "sc->sc_flags |= IWX_FLAG_TXFLUSH",
    "iwx_flush_sta(sc, in)",
    "iwx_disable_txq(sc, IWX_STATION_ID",
    "iwx_rm_sta_cmd(sc, in)",
    "sc->sc_flags &= ~IWX_FLAG_TXFLUSH",
), "flush-disable-remove transaction")

for token in (
    "#define IWX_SCD_QUEUE_CONFIG_CMD 0x17",
    "#define IWX_SCD_QUEUE_ADD",
    "#define IWX_SCD_QUEUE_REMOVE",
    "struct iwx_scd_queue_cfg_cmd",
    "TX_QUEUE_CFG_CMD_API_S_VER_3",
):
    require(reg, token, "SCD queue ABI")

print("PASS: IWM/IWX management transaction contract")
PY
