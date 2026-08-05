#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
cpp="$repo_root/itlwm/hal_iwx/ItlIwx.cpp"

python3 - "$cpp" <<'PY'
import pathlib
import re
import sys

source = pathlib.Path(sys.argv[1]).read_text()


def method(name: str) -> str:
    match = re.search(
        rf"\n{name}\([^\n]*\)\n\{{(?P<body>.*?)\n\}}",
        source,
        re.S,
    )
    if not match:
        raise SystemExit(f"FAIL: missing {name}")
    return match.group("body")


live = method("iwx_task_gate_epoch_live")
live_code = re.sub(r"/\*.*?\*/|//[^\n]*", "", live, flags=re.S)
opened = method("iwx_task_gate_open")
init = method("iwx_init_internal")

for required in (
    "!(sc->sc_flags & IWX_FLAG_SHUTDOWN)",
    "!sc->sc_task_gate_detaching",
    "sc->sc_task_gate_init_refs == 1",
    "sc->sc_task_gate_stop_refs == 0",
    "sc->sc_generation == generation",
):
    if required not in live_code:
        raise SystemExit(f"FAIL: live epoch lost stop fence: {required}")

if "sc_task_gate_closed" in live_code:
    raise SystemExit(
        "FAIL: live init epoch cannot require the gate to remain closed after open"
    )

if "sc->sc_task_gate_closed = false;" not in opened:
    raise SystemExit("FAIL: open transition no longer publishes task admission")

open_at = init.find("iwx_task_gate_open(sc, generation)")
scan_at = init.find("ieee80211_begin_scan(ifp)")
live_after_open = init.find("iwx_task_gate_epoch_live(sc, generation)", scan_at)
if min(open_at, scan_at, live_after_open) < 0 or not open_at < scan_at < live_after_open:
    raise SystemExit("FAIL: init no longer validates the same epoch after opening SCAN")

print("PASS: IWX runtime init epoch survives gate open and remains stop-fenced")
PY
