#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

python3 - "$PROJECT_DIR" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
hal = (root / "include/HAL/ItlHalService.hpp").read_text()
iwn_hpp = (root / "itlwm/hal_iwn/ItlIwn.hpp").read_text()
iwn = (root / "itlwm/hal_iwn/ItlIwn.cpp").read_text()
owner = (root / "AirportItlwm/AirportItlwmAPSTAOwner.cpp").read_text()

assert "requiresAPSTASharedChannel() const { return false; }" in hal
assert "bool requiresAPSTASharedChannel() const override;" in iwn_hpp

capability = iwn[
    iwn.index("bool ItlIwn::requiresAPSTASharedChannel() const"):
    iwn.index("uint16_t ItlIwn::getAPSTARequiredSharedChannel() const")
]
assert "num_different_channels == 1" in capability
assert "return true;" in capability

resume = owner[
    owner.index("IOReturn AirportItlwmAPSTAOwner::resumeAfterRadioReset()"):
    owner.index("void AirportItlwmAPSTAOwner::teardown()")
]
boundary = resume.index("APSTA radio-reset primary STA boundary")
query = resume.index("requiresAPSTASharedChannel()", boundary)
run = resume.index("ic->ic_state == IEEE80211_S_RUN", query)
channel = resume.index("ieee80211_chan2ieee(ic, ic->ic_bss->ni_chan)", run)
retarget = resume.index("apChannel = static_cast<uint16_t>(primaryChannel)", channel)
replay = resume.index("startLowerIfReady()", retarget)
assert boundary < query < run < channel < retarget < replay
assert "APSTA radio-reset shared channel follows primary" in resume

print("PASS: Tahoe IWN APSTA wake replay follows the recovered primary channel")
PY
