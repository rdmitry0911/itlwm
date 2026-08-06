#!/usr/bin/env bash
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

python3 - "$root" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
header = (root / "AirportItlwm/AirportItlwmSkywalkInterface.hpp").read_text()
source = (root / "AirportItlwm/AirportItlwmSkywalkInterface.cpp").read_text()
controller_h = (root / "AirportItlwm/AirportItlwmV2.hpp").read_text()
controller = (root / "AirportItlwm/AirportItlwmV2.cpp").read_text()
assert "IOReturn enable(UInt) override;" in header
assert "IOReturn disable(UInt) override;" in header

def body(signature: str, next_signature: str) -> str:
    start = source.index(signature)
    return source[start:source.index(next_signature, start)]

enable = body(
    "IOReturn AirportItlwmAPSTASkywalkInterface::enable(UInt options)",
    "IOReturn AirportItlwmAPSTASkywalkInterface::disable(UInt options)",
)
for needle in (
    "getAssocState() == 0",
    "kAirportItlwmAPSTAEnableNotRunningReturn",
    "IO80211VirtualInterface::enable(options)",
    "result == kIOReturnSuccess && controller != nullptr",
    "controller->noteAPSTAInterfaceEnableDuringPendingHostAPStart();",
    "enableDatapath();",
):
    assert needle in enable, f"missing APSTA enable contract: {needle}"
assert enable.index("getAssocState() == 0") < enable.index(
    "IO80211VirtualInterface::enable(options)"
)
assert enable.index("IO80211VirtualInterface::enable(options)") < enable.index(
    "controller->noteAPSTAInterfaceEnableDuringPendingHostAPStart();"
)
assert enable.index(
    "controller->noteAPSTAInterfaceEnableDuringPendingHostAPStart();"
) < enable.index(
    "enableDatapath();"
)

assert "void noteAPSTAInterfaceEnableDuringPendingHostAPStart();" in controller_h
bridge_start = controller.index(
    "void AirportItlwm::noteAPSTAInterfaceEnableDuringPendingHostAPStart()"
)
bridge_end = controller.index(
    "void AirportItlwm::teardownAPSTAInterface()", bridge_start
)
bridge = controller[bridge_start:bridge_end]
assert "fAPSTAOwner != nullptr" in bridge
assert "fAPSTAOwner->noteInterfaceEnableDuringPendingHostAPStart();" in bridge

disable = body(
    "IOReturn AirportItlwmAPSTASkywalkInterface::disable(UInt options)",
    "bool AirportItlwmAPSTASkywalkInterface::isCommandProhibited",
)
assert disable.index("disableDatapath();") < disable.index(
    "IO80211SkywalkInterface::disable(options)"
)

print("PASS: APSTA interface enable/disable follows the reference lifecycle")
PY
