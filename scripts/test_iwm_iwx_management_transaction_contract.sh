#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

python3 - "$ROOT" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
iwm = (root / "itlwm/hal_iwm/mac80211.cpp").read_text(encoding="utf-8")
iwm_delivery = (root / "itlwm/hal_iwm/ItlIwm.cpp").read_text(encoding="utf-8")
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


for source, delivery, marker, label in (
    (iwm, iwm_delivery, "iwm_newstate_task(void *psc)", "IWM AUTH drain"),
    (iwx, iwx, "iwx_newstate_task(void *psc)", "IWX AUTH drain"),
):
    state = body(source, marker, label)
    require(state, "takeStateTransition(&request)", label)
    require(state, "postStateTransitionCommit(request, err)", label)
    if "sc->sc_newstate" in state or "runAction" in state:
        fail(f"{label} publishes outside the workloop or waits on its gate")
    post = body(delivery, "postStateTransitionCommit(const ItlStateTransitionRequest", label)
    ordered(post, ("stateTransitionCurrent(request)",
                  "stateTransition.publish(request, com.sc_generation, error)",
                  "source->retain()", "IOSimpleLockUnlockEnableInterrupt",
                  "getMainWorkLoop()->inGate()", "drainStateTransitionCommit(source)",
                  "source->interruptOccurred(NULL, NULL, 0)", "source->release()"), label)
    commit = body(delivery, "drainStateTransitionCommit(IOInterruptEventSource", label)
    ordered(commit, ("getMainWorkLoop()->inGate()", "stateTransition.takeCommit",
                    "IOSimpleLockUnlockEnableInterrupt", "stateTransitionCurrent(request)",
                    "com.sc_newstate("), label)
    if "runAction" in post + commit:
        fail(f"{label} reintroduces blocking state delivery")

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
    "cmd_ver == 0 || cmd_ver == IWX_FW_CMD_VER_UNKNOWN",
    "IWX_SCD_QUEUE_REMOVE",
    "iwx_send_cmd(sc, &hcmd)",
    "iwx_reset_tx_ring(sc, ring)",
    "sc->sc_tid_data[IWX_MAX_TID_COUNT].qid = IWX_INVALID_QUEUE",
):
    require(disable, token, "IWX TXQ disable")
ordered(disable, (
    "iwx_send_cmd(sc, &hcmd)",
    "pkt = hcmd.resp_pkt",
    "if (!pkt ||",
    "iwx_reset_tx_ring(sc, ring)",
), "IWX acknowledged TXQ teardown")
for forbidden in (
    "ring->queued == 0 && ring->tail == ring->cur",
    "return EBUSY",
    "IOSimpleLockLock(txq_lock)",
):
    if forbidden in disable:
        fail(f"TXQ removal must accept flush completion without read pointers: {forbidden}")

flush_tids = body(iwx, "iwx_flush_sta_tids(struct iwx_softc", "IWX flush response")
for token in (
    "tid == IWX_MGMT_TID ? IWX_MAX_TID_COUNT : tid",
    "nitems(sc->sc_tid_data)",
    "sc->sc_tid_data[tid_slot].qid != qid",
    "iwx_ampdu_txq_advance(sc, txq, IWX_AGG_SSN_TO_TXQ_IDX(read_after",
):
    require(flush_tids, token, "sparse management TID reclaim")

flush = body(iwx, "iwx_flush_sta(struct iwx_softc", "IWX flush fence")
ordered(flush, ("beginPrimaryStationCleanup(false, &receipt)",
                "iwx_flush_station(sc, receipt)",
                "finishPrimaryStationCleanup(receipt, error)"), "owned TX flush")
begin_cleanup = body(iwx, "beginPrimaryStationCleanup(bool", "station cleanup reservation")
finish_cleanup = body(iwx, "finishPrimaryStationCleanup(const", "station cleanup completion")
require(begin_cleanup, "primaryStationContext.begin(", "station cleanup reservation")
require(begin_cleanup, "com.sc_flags |= IWX_FLAG_TXFLUSH", "owned TX producer fence")
ordered(finish_cleanup, ("primaryStationContext.finish(", "if (error == 0)",
                        "com.sc_flags &= ~IWX_FLAG_TXFLUSH"), "confirmed fence retirement")

remove = body(iwx, "iwx_rm_sta(struct iwx_softc", "IWX STA removal")
ordered(remove, (
    "beginPrimaryStationCleanup(true, &receipt)",
    "iwx_flush_station(sc, receipt)",
    "iwx_disable_txq(sc, receipt.identity.station",
    "iwx_remove_station(sc, receipt)",
    "finishPrimaryStationCleanup(receipt, err)",
), "flush-disable-remove transaction")
for lower in (disable, flush_tids):
    require(lower, "hcmd.context_command = context", "physical cleanup submission owner")
    require(lower, "primaryStationCleanupCurrent(context->receipt)", "response retirement owner")

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
