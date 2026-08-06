#!/usr/bin/env bash
# Prove that HostAP NULL remains authoritative until the asynchronous IWX
# firmware teardown reaches a terminal, while Tahoe's immediate replacement
# carrier is retained and serialized behind that terminal.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

python3 - "$root" <<'PY'
from pathlib import Path
import re
import sys


root = Path(sys.argv[1])


def body(text: str, signature: str) -> str:
    match = re.search(
        r"^" + re.escape(signature) + r"\s*\([^;{}]*\)\s*(?:const\s*)?\{",
        text,
        re.M | re.S,
    )
    if match is None:
        raise AssertionError(f"missing function: {signature}")
    opening = text.rfind("{", match.start(), match.end())
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[opening + 1:index]
    raise AssertionError(f"unterminated function: {signature}")


def ordered(text: str, *tokens: str) -> None:
    cursor = 0
    for token in tokens:
        position = text.find(token, cursor)
        if position < 0:
            raise AssertionError(f"missing ordered token: {token}")
        cursor = position + len(token)


owner = (root / "AirportItlwm/AirportItlwmAPSTAOwner.cpp").read_text()
owner_h = (root / "AirportItlwm/AirportItlwmAPSTAOwner.hpp").read_text()
iwx = (root / "itlwm/hal_iwx/ItlIwx.cpp").read_text()
reference = (
    root / "docs/reference/AppleBCMWLAN_APSTA_hostap_control_power_2026_04_27.md"
).read_text()

assert "bool lowerStopPending;" in owner_h
assert "bool confirmedHostAPStartPending;" in owner_h
assert "call `setHostApModeInternal(NULL)`" in reference
assert "return the internal call result" in reference

stop_lower = body(owner, "IOReturn AirportItlwmAPSTAOwner::stopLower")
ordered(
    stop_lower,
    "setAPSTADatapathEnabled(false)",
    "resetRuntimeState()",
    "lowerStopPending = true",
    "state.hostApTransitionState270 = 1",
    "driveLowerStopToTerminal()",
)

terminal = body(
    owner, "IOReturn AirportItlwmAPSTAOwner::driveLowerStopToTerminal"
)
ordered(
    terminal,
    "owner->fHalService->stopAPMode()",
    "if (result != kIOReturnSuccess)",
    "return result",
    "lowerStopPending = false",
    "state.hostApTransitionState270 = 0",
    "kAirportItlwmAPSTAOwnerTerminal",
)

resume = body(owner, "IOReturn AirportItlwmAPSTAOwner::resumeAfterRadioReset")
ordered(
    resume,
    "if (lowerStopPending)",
    "const IOReturn stopResult = driveLowerStopToTerminal()",
    "if (stopResult != kIOReturnSuccess)",
    "if (!confirmedHostAPStartPending)",
    "APSTA confirmed HostAP replacement crossed lower stop",
    "if (!radioResetResumePending)",
)

hostap = body(owner, "IOReturn AirportItlwmAPSTAOwner::setHostAPMode")
assert "!isApRunning() && !radioResetResumePending &&" in hostap
assert "!lowerStopPending" in hostap
ordered(
    hostap,
    "const IOReturn stopResult = stopLower()",
    "apsta_lower_stop_pending(stopResult)",
    "accepted asynchronous HostAP stop pending lower",
    "state.softapSsidLength274 = in->ssidLength1c",
    "if (lowerStopPending)",
    "confirmedHostAPStartPending = true",
    "const IOReturn stopResult = driveLowerStopToTerminal()",
    "queued confirmed HostAP replacement behind",
    "const IOReturn result = startLowerIfReady()",
)

stop_api = body(iwx, "IOReturn ItlIwx::\nstopAPMode")
assert stop_api.count("return kIOReturnNotReady;") >= 3
ordered(
    stop_api,
    "apStopRequested = true",
    "if (apStartPending)",
    "return kIOReturnNotReady",
    "if (!apLowerRunning)",
    "if (apStopPending)",
    "return kIOReturnNotReady",
    "apStopPending = true",
    "iwx_ap_schedule_task(this, &com.ap_stop_task)",
    "IWX AP lower stop queued outside upper command gate",
    "return kIOReturnNotReady",
)

dispatch = body(iwx, "static void\niwx_ap_stop_task_dispatch")
ordered(
    dispatch,
    "if (!that->iwx_task_gate_enter(sc, false))",
    "that->apStopPending = false",
    "IWX AP lower stop task deferred by closed epoch gate",
    "iwx_ap_stop_task(argument)",
)
closed_gate = dispatch[:dispatch.index("iwx_ap_stop_task(argument)")]
assert "apLowerRunning = false" not in closed_gate
assert "apStopRequested = false" not in closed_gate

worker = body(iwx, "static void\niwx_ap_stop_task")
ordered(
    worker,
    "stopLower = that->apLowerRunning",
    "that->iwx_stop_ap_mode(sc, &that->apRuntime)",
    "that->apStopPending = false",
    "that->apLowerRunning = false",
    "IWX AP lower stop worker complete",
)

lower_stop = body(iwx, "int ItlIwx::\niwx_stop_ap_mode")
ordered(
    lower_stop,
    "runtime->stage = kItlApFirmwareResourceStopping",
    "iwx_del_task(sc, sc->sc_nswq, &sc->ap_client_task)",
    "for (size_t index = 0; index < kItlApFirmwareMaxClients; index++)",
    "iwx_ap_update_quotas(sc, runtime, false)",
    "IWX_FW_CTXT_ACTION_REMOVE",
    "itl_ap_firmware_runtime_reset(runtime)",
)
assert "taskq_barrier(sc->sc_nswq)" not in lower_stop, (
    "the sc_nswq AP-stop worker must not barrier on its own task queue"
)

print("PASS: IWX HostAP stop/restart is acknowledged then serialized at the lower terminal")
PY
