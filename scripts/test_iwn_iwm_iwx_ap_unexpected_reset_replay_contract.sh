#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

python3 - "$repo_root" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
owner = (root / "AirportItlwm/AirportItlwmAPSTAOwner.cpp").read_text()
iwn = (root / "itlwm/hal_iwn/ItlIwn.cpp").read_text()
iwm = (root / "itlwm/hal_iwm/mac80211.cpp").read_text()
iwx = (root / "itlwm/hal_iwx/ItlIwx.cpp").read_text()


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


retained = body(owner,
    "void AirportItlwmAPSTAOwner::prepareRetainedLowerReset(")
for required in (
    "owner->setAPSTADatapathEnabled(false);",
    "clearLowerAssociatedStations();",
    "state.softapAssociatedStaCount00 = 0;",
    "lifecycle = kAirportItlwmAPSTAOwnerLowerBlocked;",
    "radioResetResumePending = true;",
):
    assert required in retained, f"retained reset misses: {required}"
assert retained.index("owner->setAPSTADatapathEnabled(false);") < \
       retained.index("radioResetResumePending = true;"), \
    "stale datapath must close before replay is armed"

resume = body(owner,
    "IOReturn AirportItlwmAPSTAOwner::resumeAfterRadioReset()")
for required in (
    "const bool upperRunning = isApRunning();",
    "owner->fHalService->getAPCurrentChannel()",
    "if (upperRunning && lowerChannel == 0)",
    "APSTA unexpected lower reset detected",
    "prepareRetainedLowerReset(0);",
    "const IOReturn result = startLowerIfReady();",
):
    assert required in resume, f"unexpected reset replay misses: {required}"
assert resume.index("prepareRetainedLowerReset(0);") < \
       resume.index("const IOReturn result = startLowerIfReady();"), \
    "lower-loss census must arm the existing bounded replay path"
assert "if (upperRunning && lowerChannel != apChannel)" in resume, \
    "healthy census must preserve the committed CSA channel"

iwn_stop = body(iwn, "iwn_stop(struct _ifnet *ifp)")
assert "iwn_reset_ap_runtime_state();" in iwn_stop
assert iwn_stop.index("iwn_reset_ap_runtime_state();") < \
       iwn_stop.index("iwn_hw_stop(sc);"), \
    "IWN must publish lower AP loss before destroying firmware"

for family, source, signature, device_stop, runtime_reset in (
    ("IWM", iwm, "iwm_stop(struct _ifnet *ifp)", "iwm_stop_device(sc);",
     "itl_ap_firmware_runtime_reset(&that->apRuntime, true);"),
    ("IWX", iwx, "iwx_stop_internal(struct _ifnet *ifp,",
     "iwx_stop_device(sc);", "iwx_ap_lifecycle_reset(that, false);"),
):
    stop = body(source, signature)
    assert runtime_reset in stop, \
        f"{family} must retire its stale GO runtime on device reset"
    assert stop.index(device_stop) < stop.index(runtime_reset), \
        f"{family} software AP retirement must follow the device reset"

iwx_lifecycle_reset = body(iwx, "iwx_ap_lifecycle_reset(ItlIwx *that")
assert "itl_ap_firmware_runtime_reset(&that->apRuntime, !detached);" in \
       iwx_lifecycle_reset, \
    "IWX serialized lifecycle reset must select the PMKSA lifetime"

print("PASS: IWN/IWM/IWX unexpected lower reset replays retained HostAP")
PY
