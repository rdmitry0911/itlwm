#!/bin/bash
set -euo pipefail

repo_root=$(cd "$(dirname "$0")/.." && pwd)

python3 - "$repo_root" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
owner = (root / "AirportItlwm/AirportItlwmAPSTAOwner.cpp").read_text()
owner_hpp = (root / "AirportItlwm/AirportItlwmAPSTAOwner.hpp").read_text()
controller = (root / "AirportItlwm/AirportItlwmV2.cpp").read_text()
controller_hpp = (root / "AirportItlwm/AirportItlwmV2.hpp").read_text()

for field in (
    "bool radioResetResumePending;",
    "bool radioResetWaitForPrimaryStaRun;",
    "uint16_t radioResetResumeWaitTicks;",
):
    assert field in owner_hpp, f"missing retained AP wake field: {field}"

prepare = owner[
    owner.index("void AirportItlwmAPSTAOwner::prepareForRadioReset()"):
    owner.index("IOReturn AirportItlwmAPSTAOwner::resumeAfterRadioReset()")
]
resume = owner[
    owner.index("IOReturn AirportItlwmAPSTAOwner::resumeAfterRadioReset()"):
    owner.index("void AirportItlwmAPSTAOwner::teardown()")
]

assert "radioResetWaitForPrimaryStaRun ||" in prepare
assert "ic->ic_state == IEEE80211_S_RUN" in prepare, \
    "sleep must preserve the last live primary-BSS observation"
assert prepare.index("radioResetWaitForPrimaryStaRun =") < \
       prepare.index("radioResetResumePending = true;"), \
    "the primary-STA replay dependency must be captured before AP replay arms"

runtime_sample = resume[
    resume.index("if (!radioResetResumePending)"):
    resume.index("if (radioResetWaitForPrimaryStaRun)")
]
assert "isApRunning()" in runtime_sample
assert "ic->ic_state == IEEE80211_S_RUN" in runtime_sample
assert "radioResetWaitForPrimaryStaRun =" in runtime_sample, \
    "watchdog must retain a pre-transition primary-STA RUN observation"

assert "if (radioResetWaitForPrimaryStaRun)" in resume
assert "ic->ic_state != IEEE80211_S_RUN" in resume
assert "return kIOReturnNotReady;" in resume
assert resume.index("ic->ic_state != IEEE80211_S_RUN") < \
       resume.index("const IOReturn result = startLowerIfReady();"), \
    "PAN replay must not precede the post-reset primary BSS RUN boundary"
assert "kAirportItlwmAPSTARadioResetPrimaryStaWaitTicks" in resume, \
    "AP-only fallback must remain bounded"
assert "radioResetWaitForPrimaryStaRun = false;" in resume

watchdog = controller[
    controller.index("void AirportItlwm::watchdogAction("):
    controller.index("#if __IO80211_TARGET >= __MAC_26_0",
                     controller.index("void AirportItlwm::watchdogAction("))
]
assert "gate->runAction(resumeAPSTAAfterRadioResetGated)" in watchdog
assert "#define kWatchDogTimerPeriod 1000" in controller_hpp, \
    "bounded wait ticks rely on the one-second controller watchdog"

print("PASS: Tahoe APSTA wake replays primary BSS RXON before retained PAN data")
PY
