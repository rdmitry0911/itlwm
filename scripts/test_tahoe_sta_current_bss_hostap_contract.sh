#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

python3 - "$PROJECT_DIR" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
sky = (root / "AirportItlwm/AirportItlwmSkywalkInterface.cpp").read_text()

start = sky.index("bool AirportItlwmSkywalkInterface::\nsetLinkStateInternal(")
end = sky.index("void AirportItlwmSkywalkInterface::\nsetCurrentApAddress", start)
link = sky[start:end]

# Tahoe's parent-accepted STA link-up is the common path for a saved network.
# It must populate the same BssManager association predicate that Apple's
# HostAP implementation observes, without waiting for an optional later WCL
# LINK_STATE_UPDATE carrier.
accepted = link.index("if (ret &&")
up = link.index("if (!isLinkDown)", accepted)
payload = link.index("TahoeBssManagerContracts::BeaconPayload currentBss{}", up)
build = link.index("buildTahoeCurrentBssPayload(fHalService, &currentBss)", payload)
publish = link.index("instance->setTahoeCurrentBss(currentBss.meta, currentBss.ie)", build)
rates = link.index("updateDriverBssManagerRateAndMcs();", publish)
identity = link.index("instance->publishTahoeAcceptedJoinIdentityEvents(", rates)
down = link.index("instance->clearTahoeCurrentBss();", identity)

assert accepted < up < payload < build < publish < rates < identity < down
assert "without necessarily receiving WCL's later" in link
assert "isAssociated() is true" in link
assert "instance->stopTahoeLqmStatsTimer();" in link
assert "reference HostAP" in link

print("PASS: accepted STA link transitions keep Tahoe current-BSS ownership "
      "coherent for the reference HostAP association predicate")
PY
