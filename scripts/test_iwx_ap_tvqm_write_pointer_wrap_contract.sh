#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
source_file="$repo_root/itlwm/hal_iwx/ItlIwx.cpp"

python3 - "$source_file" <<'PY'
from pathlib import Path
import re
import sys

source = Path(sys.argv[1]).read_text()
match = re.search(
    r"int ItlIwx::\s*\niwx_ap_send_raw_frame\([^\n]+\)\s*\{(?P<body>.*?)"
    r"\n\}\n\nint ItlIwx::\s*\niwx_flush_sta_tids",
    source,
    re.S,
)
if match is None:
    raise SystemExit("FAIL: cannot locate IWX AP raw TX implementation")
body = match.group("body")

required = {
    "physical descriptor index wraps at the allocated carrier":
        "const int index = ring->cur & (ring->ring_count - 1);",
    "TVQM doorbell cursor advances in the hardware pointer domain":
        "ring->cur = (ring->cur + 1) % getTxQueueSize();",
    "doorbell publishes the hardware-domain cursor":
        "IWX_WRITE(sc, IWX_HBUS_TARG_WRPTR, ring->qid << 16 | ring->cur);",
}
for description, token in required.items():
    if token not in body:
        raise SystemExit(f"FAIL: {description}")

forbidden = "ring->cur = (ring->cur + 1) % ring->ring_count;"
if forbidden in body:
    raise SystemExit(
        "FAIL: AP TVQM write pointer still aliases the 256-slot carrier wrap"
    )

if body.index(required["physical descriptor index wraps at the allocated carrier"]) \
        > body.index(required["TVQM doorbell cursor advances in the hardware pointer domain"]):
    raise SystemExit("FAIL: descriptor selection must precede cursor publication")
if body.index(required["TVQM doorbell cursor advances in the hardware pointer domain"]) \
        > body.index(required["doorbell publishes the hardware-domain cursor"]):
    raise SystemExit("FAIL: cursor must advance before the TVQM doorbell")

# Model two laps of a 256-entry carrier behind AX210's 16-bit TVQM pointer.
ring_count = 256
hardware_count = 65536
cur = 0
seen = []
doorbells = []
for _ in range(ring_count + 16):
    seen.append(cur & (ring_count - 1))
    cur = (cur + 1) % hardware_count
    doorbells.append(cur)

if seen[:ring_count] != list(range(ring_count)) or seen[ring_count:] != list(range(16)):
    raise SystemExit("FAIL: descriptor carrier did not wrap independently")
if doorbells[ring_count - 1] != 256 or doorbells[ring_count] != 257:
    raise SystemExit("FAIL: TVQM pointer regressed to zero at the physical wrap")

print("PASS: IWX AP TVQM keeps the 16-bit doorbell cursor monotonic across the 256-slot carrier wrap")
PY
