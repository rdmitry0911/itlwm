#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

python3 - "$repo_root" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
owner = (root / "AirportItlwm/AirportItlwmAPSTAOwner.cpp").read_text()
iwm = (root / "itlwm/hal_iwm/ItlIwm.cpp").read_text()
iwx = (root / "itlwm/hal_iwx/ItlIwx.cpp").read_text()
runtime = (root / "include/HAL/ItlApFirmwareRuntime.hpp").read_text()


def body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise SystemExit(f"FAIL: missing function: {signature}")
    opening = source.find("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening:index + 1]
    raise SystemExit(f"FAIL: unterminated function: {signature}")


prepare = body(owner, "void AirportItlwmAPSTAOwner::prepareForRadioReset()")
empty = body(owner,
    "void AirportItlwmAPSTAOwner::prepareEmptyAPForRadioReset()")
retained = body(owner,
    "void AirportItlwmAPSTAOwner::prepareRetainedLowerReset(")
assert "prepareRetainedLowerReset(lowerChannel);" in prepare
assert "owner->setAPSTADatapathEnabled(false);" in retained
assert "radioResetResumePending = true;" in retained
assert "prepareEmptyAPForRadioReset();" in prepare
assert "stopLower()" not in prepare, \
    "PM owner must not submit asynchronous lower work before radio reset"
assert "(void)stopLower();" not in empty
assert "setAPSTADatapathEnabled(false)" in empty
assert "resetRuntimeState();" in empty
assert "kAirportItlwmAPSTAOwnerTerminal" in empty
assert "delegated to imminent radio reset" in empty

resume = body(owner, "IOReturn AirportItlwmAPSTAOwner::resumeAfterRadioReset()")
assert resume.index("ic->ic_state != IEEE80211_S_RUN") < \
       resume.index("startLowerIfReady()"), \
       "retained GO must re-arm after the primary STA boundary"
assert "radioResetResumePending = false;" in resume

iwm_disable = body(iwm, "disable(IONetworkInterface *netif)")
iwm_lower_stop = "iwm_stop_ap_resources(&com, &apRuntime)"
assert "apRuntime.stage != kItlApFirmwareResourceIdle" in iwm_disable, \
    "IWM must notice retained GO resources"
assert iwm_lower_stop in iwm_disable, "IWM must retire GO before radio stop"
assert iwm_disable.index(iwm_lower_stop) < iwm_disable.index("DVACT_QUIESCE"), \
    "IWM GO teardown must precede generic firmware teardown"
assert iwm_disable.index(iwm_lower_stop) < iwm_disable.index(
    "already !IFF_UP"), \
    "IWM cleanup must survive an already-lowered primary ifnet"
iwm_start = body(iwm, "startAPMode(const struct ItlHalApConfig *config)")
assert "apRuntime.stage != kItlApFirmwareResourceIdle" in iwm_start
assert "kIOReturnBusy" in iwm_start, \
    "IWM stale pre-sleep state must remain fail-closed"

# IWX firmware command completions share the device workloop with the upper
# power command.  Therefore disable must not enqueue an explicit AP worker
# immediately before waiting for generic device teardown.  The generic stop
# is the authoritative erasure edge and clears the serialized AP lifecycle
# only after every previously admitted AP worker has been cancelled/drained.
iwx_disable = body(iwx, "disable(IONetworkInterface *netif)")
iwx_stop_request = "(void)stopAPMode();"
assert iwx_stop_request not in iwx_disable, \
    "IWX PM path must not queue a firmware worker from the power workloop"
assert "continuing radio quiesce" in iwx_disable
already_down = iwx_disable.index("if (!(ifp->if_flags & IFF_UP))")
quiesce = iwx_disable.index("DVACT_QUIESCE")
assert already_down < quiesce
assert "return kIOReturnSuccess;" not in iwx_disable[already_down:quiesce], \
    "an already-lowered primary ifnet must not bypass AP firmware reset"

iwx_device_stop = body(iwx, "iwx_stop_internal(struct _ifnet *ifp")
assert "iwx_del_task(sc, sc->sc_nswq, &sc->ap_start_task)" in iwx_device_stop
assert "iwx_del_task(sc, sc->sc_nswq, &sc->ap_stop_task)" in iwx_device_stop
assert iwx_device_stop.index("taskq_barrier(sc->sc_nswq)") < \
       iwx_device_stop.index("iwx_stop_device(sc)")
assert iwx_device_stop.index("iwx_stop_device(sc)") < \
       iwx_device_stop.index("iwx_ap_lifecycle_reset(that, false)")

iwx_lifecycle_reset = body(iwx, "iwx_ap_lifecycle_reset(ItlIwx *that")
for reset in (
    "apStartPending = false",
    "apStopPending = false",
    "apLowerRunning = false",
    "apStopRequested = false",
    "itl_ap_firmware_runtime_reset(&that->apRuntime)",
):
    assert reset in iwx_lifecycle_reset, \
        f"IWX reset must clear stale AP lifecycle: {reset}"

iwx_start = body(iwx, "startAPMode(const struct ItlHalApConfig *config)")
assert "if (apLowerRunning)" in iwx_start
assert "apStartPending || apStopPending || apStopRequested" in iwx_start
assert "return kIOReturnNotReady;" in iwx_start, \
    "IWX stale async start/stop state must remain fail-closed"

crypto_reset = body(runtime,
    "itl_ap_firmware_client_crypto_reset(")
for forbidden in ("groupKeyInstalled = true", "clientAuthorized = true"):
    assert forbidden not in crypto_reset
for required in (
    "clientAuthorized = false",
    "clientPairwiseKeyInstalled = false",
    "explicit_bzero(client->clientPairwiseKey",
    "explicit_bzero(client->clientRxPn",
):
    assert required in crypto_reset, \
        f"radio reset must close client security state: {required}"

print("PASS: paired IWM/IWX AP resources re-arm after radio reset")
PY
