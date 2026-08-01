#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

python3 - "$PROJECT_DIR" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
controller = (root / "AirportItlwm/AirportItlwmV2.cpp").read_text()
controller_hpp = (root / "AirportItlwm/AirportItlwmV2.hpp").read_text()
hal = (root / "include/HAL/ItlHalService.hpp").read_text()
iwn = (root / "itlwm/hal_iwn/ItlIwn.cpp").read_text()
iwn_hpp = (root / "itlwm/hal_iwn/ItlIwn.hpp").read_text()


def body(source: str, signature: str, next_signature: str) -> str:
    start = source.index(signature)
    end = source.index(next_signature, start)
    return source[start:end]


assert "virtual uint32_t getAPTxFreeSpace() const" not in hal, \
    "AP ring admission must not change the HAL vtable ABI"
assert "airportItlwmQueryAPTxFreeSpace(" in hal
assert "uint32_t getAPTxFreeSpace() const;" in iwn_hpp
free_space = body(
    iwn,
    "uint32_t ItlIwn::getAPTxFreeSpace() const",
    "bool ItlIwn::iwn_handle_ap_probe_req(",
)
for needle in (
    "IWN_IPAN_BE_QUEUE",
    "IWN_TX_RING_COUNT - 1",
    "usable - ring->queued",
):
    assert needle in free_space, f"missing shared PAN-ring admission: {needle}"
for needle in ("!apClientAssociated", "!apClientNodeInstalled"):
    assert needle in free_space, \
        f"AP admission must stay closed before firmware client readiness: {needle}"
assert "airportItlwmQueryAPTxFreeSpace(ItlHalService *service)" in iwn

association = body(
    iwn,
    "int ItlIwn::iwn_send_ap_assoc_success()",
    "int ItlIwn::iwn_send_ap_sensitivity()",
)
associated = association.index("apClientAssociated = true;")
wake = association.index(
    "airportItlwmRequestAPTxDequeue(getController());", associated
)
assert associated < wake, \
    "firmware client association must reopen AP Skywalk dequeue"

tx_action = body(
    controller,
    "skywalkTxAction(OSObject *owner",
    "void AirportItlwm::requestAPTxDequeue()",
)
for needle in (
    "const UInt32 dequeueLimit =",
    "airportItlwmQueryAPTxFreeSpace(that->fHalService)",
    "apFreeSpace < count ? apFreeSpace : count",
    "i < dequeueLimit",
):
    assert needle in tx_action, f"missing shared PAN dequeue limit: {needle}"
assert "skywalkTxAction, nullptr, 0" in controller
assert "skywalkAPTxQueryFreeSpace" not in controller, \
    "Tahoe does not export the query-handler withPool overload"
no_resources = tx_action.index(
    "apstaQueue && outRet == kIOReturnNoResources"
)
rollback = tx_action.index("skywalkTxUnstageLastCompletionPacket(", no_resources)
unconsume = tx_action.index("consumed--;", rollback)
stop_batch = tx_action.index("break;", unconsume)
assert no_resources < rollback < unconsume < stop_batch
assert "sRT.txPktDrop++;" not in tx_action[no_resources:unconsume], \
    "temporary PAN-ring pressure must not be counted as a packet drop"

clear_oactive = body(
    iwn,
    "iwn_clear_oactive(struct iwn_softc *sc, struct iwn_tx_ring *ring)",
    "bool ItlIwn::\niwn_tx_pending(",
)
for needle in (
    "apQueueWasFull",
    "ring->queued < IWN_TX_RING_LOMARK",
    "airportItlwmRequestAPTxDequeue(that->getController())",
):
    assert needle in clear_oactive, f"missing PAN low-water restart: {needle}"

assert "void requestAPTxDequeue();" in controller_hpp
restart = body(
    controller,
    "void AirportItlwm::requestAPTxDequeue()",
    'extern "C" void\nairportItlwmRequestAPTxDequeue(',
)
assert "fAPSTATxQueues[i]->requestDequeue(nullptr, 0)" in restart

print("PASS: Tahoe APSTA TX queues honor shared PAN-ring backpressure")
PY
