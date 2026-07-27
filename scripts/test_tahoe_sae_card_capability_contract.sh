#!/usr/bin/env bash
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
tmp="$(mktemp -d "${TMPDIR:-/tmp}/tahoe-sae-card-capability.XXXXXX")"
trap 'rm -rf "$tmp"' EXIT

c++ -x c++ -std=c++17 -Wall -Wextra -Werror -I"$root" \
    -o "$tmp/test" - <<'EOF'
#include "AirportItlwm/TahoeCapabilityContracts.hpp"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

int main()
{
    uint8_t baseline[24] = {};
    uint8_t sae[24] = {};

    TahoeCapabilityContracts::applyAppleConsistentCardCapabilityCluster(
        baseline);
    TahoeCapabilityContracts::applyAppleConsistentCardCapabilityCluster(sae);
    TahoeCapabilityContracts::applySaeCardCapability(sae);

    assert(baseline[TahoeCapabilityContracts::kCardCapabilitySaeByte] ==
           TahoeCapabilityContracts::kCardCapabilityByte9);
    assert(sae[TahoeCapabilityContracts::kCardCapabilitySaeByte] ==
           (TahoeCapabilityContracts::kCardCapabilityByte9 |
            TahoeCapabilityContracts::kCardCapabilitySaeMask));
    for (size_t index = 0; index < sizeof(sae); ++index) {
        if (index != TahoeCapabilityContracts::kCardCapabilitySaeByte)
            assert(sae[index] == baseline[index]);
    }
    return 0;
}
EOF

"$tmp/test"

python3 - "$root" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
v2 = (root / "AirportItlwm/AirportItlwmV2.cpp").read_text()
legacy = (root / "AirportItlwm/AirportSTAIOCTL.cpp").read_text()

for name, source in (("Tahoe controller", v2), ("legacy shadow", legacy)):
    gate = "#if AIRPORT_ITLWM_IWN_SAE_WCL_INGRESS"
    call = "TahoeCapabilityContracts::applySaeCardCapability(cd->capabilities);"
    if gate not in source or call not in source:
        raise SystemExit(f"{name} does not gate the SAE card capability")
PY

printf '%s\n' 'PASS: Tahoe SAE card capability contract'
