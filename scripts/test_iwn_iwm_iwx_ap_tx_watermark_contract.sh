#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

python3 - "$PROJECT_DIR" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
controller = (root / "AirportItlwm/AirportItlwmV2.cpp").read_text()
iwn = (root / "itlwm/hal_iwn/ItlIwn.cpp").read_text()
iwm_hal = (root / "itlwm/hal_iwm/ItlIwm.cpp").read_text()
iwm = (root / "itlwm/hal_iwm/mac80211.cpp").read_text()
iwx = (root / "itlwm/hal_iwx/ItlIwx.cpp").read_text()


def function(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
    raise AssertionError(f"unterminated function: {signature}")


def require_all(scope: str, text: str, needles: tuple[str, ...]) -> None:
    for needle in needles:
        assert needle in text, f"{scope}: missing {needle}"


tx_action = function(controller, "skywalkTxAction(OSObject *owner")
require_all("Skywalk AP dequeue", tx_action, (
    "airportItlwmQueryAPTxFreeSpace(that->fHalService)",
    "const UInt32 dequeueLimit =",
    "apFreeSpace < count ? apFreeSpace : count",
    "apstaQueue && outRet == kIOReturnNoResources",
    "skywalkTxUnstageLastCompletionPacket(",
    "consumed--;",
))

iwn_send = function(iwn, "int ItlIwn::iwn_send_ap_data_frame(")
require_all("IWN AP admission", iwn_send, (
    "com.qfullmsk & (1U << ring->qid)",
    "ring->queued > IWN_TX_RING_HIMARK",
    "ring->queued >= IWN_TX_RING_COUNT - 1",
    "if (++ring->queued > IWN_TX_RING_HIMARK)",
    "com.qfullmsk |= 1 << ring->qid",
    "return ENOBUFS;",
))
iwn_free = function(iwn, "uint32_t ItlIwn::getAPTxFreeSpace() const")
assert iwn_free.count("IWN_TX_RING_HIMARK") >= 2, \
    "IWN AP free-space query must gate PAN and aggregate rings at high-water"
assert iwn_free.count("com.qfullmsk &") >= 2, \
    "IWN AP free-space query must preserve high/low-water hysteresis"
iwn_reopen = function(
    iwn, "iwn_clear_oactive(struct iwn_softc *sc, struct iwn_tx_ring *ring)")
require_all("IWN AP low-water reopen", iwn_reopen, (
    "apQueueWasFull",
    "ring->queued < IWN_TX_RING_LOMARK",
    "sc->qfullmsk &= ~(1 << ring->qid)",
    "airportItlwmRequestAPTxDequeue(that->getController())",
))

iwm_send = function(iwm, "iwm_ap_send_raw_frame(struct iwm_softc *sc,")
require_all("IWM AP admission", iwm_send, (
    "sc->qfullmsk & (1U << ring->qid)",
    "ring->queued > IWM_TX_RING_HIMARK",
    "ring->queued >= IWM_TX_RING_COUNT - 1",
    "if (++ring->queued > IWM_TX_RING_HIMARK)",
    "sc->qfullmsk |= 1U << ring->qid",
    "return ENOBUFS;",
))
iwm_free = function(iwm_hal, "getAPTxFreeSpace() const")
assert iwm_free.count("IWM_TX_RING_HIMARK") >= 2, \
    "IWM AP free-space query must gate multicast and client rings at high-water"
assert iwm_free.count("com.qfullmsk &") >= 2, \
    "IWM AP free-space query must preserve high/low-water hysteresis"
iwm_reopen = function(
    iwm, "iwm_clear_oactive(struct iwm_softc *sc, struct iwm_tx_ring *ring)")
require_all("IWM low-water state", iwm_reopen, (
    "ring->queued < IWM_TX_RING_LOMARK",
    "sc->qfullmsk &= ~(1 << ring->qid)",
))
iwm_ba = function(iwm, "iwm_rx_tx_ba_notif(")
iwm_single = function(iwm, "iwm_rx_tx_cmd_single(")
for scope, completion in (
    ("IWM AP BA completion", iwm_ba),
    ("IWM AP single completion", iwm_single),
):
    clear = completion.index("iwm_clear_oactive(sc, ring)")
    wake = completion.index("airportItlwmRequestAPTxDequeue", clear)
    assert clear < wake, f"{scope}: low-water state must update before wake"

iwx_send = function(iwx, "iwx_ap_send_raw_frame(struct iwx_softc *sc,")
require_all("IWX AP admission", iwx_send, (
    "ring->ap_queue_full",
    "ring->queued > ring->hi_mark",
    "ring->queued >= ring->ring_count - 1",
    "if (++ring->queued > ring->hi_mark)",
    "ring->ap_queue_full = true",
    "return ENOBUFS;",
))
iwx_free = function(iwx, "getAPTxFreeSpace() const")
assert iwx_free.count("->hi_mark") >= 2, \
    "IWX AP free-space query must gate multicast and client rings at high-water"
assert iwx_free.count("ap_queue_full") >= 2, \
    "IWX AP free-space query must preserve high/low-water hysteresis"
iwx_reopen = function(
    iwx, "iwx_clear_oactive(struct iwx_softc *sc, struct iwx_tx_ring *ring)")
require_all("IWX low-water state", iwx_reopen, (
    "ring->queued < ring->low_mark",
    "ring->ap_queue_full = false",
    "sizeof(sc->qfullmsk) * NBBY",
))
iwx_ba = function(iwx, "iwx_rx_tx_ba_notif(")
iwx_single = function(iwx, "iwx_rx_tx_cmd(")
for scope, completion in (
    ("IWX AP BA completion", iwx_ba),
    ("IWX AP single completion", iwx_single),
):
    clear = completion.index("iwx_clear_oactive(sc, ring)")
    wake = completion.index("airportItlwmRequestAPTxDequeue", clear)
    assert clear < wake, f"{scope}: low-water state must update before wake"

print("PASS: IWN/IWM/IWX AP TX uses reference high/low-water backpressure")
PY
