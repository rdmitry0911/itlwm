#!/bin/bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source_file="$repo_root/itlwm/hal_iwx/ItlIwx.cpp"

python3 - "$source_file" <<'PY'
from pathlib import Path
import re
import sys

source = Path(sys.argv[1]).read_text()

match = re.search(
    r"int ItlIwx::\s*\n"
    r"iwx_newstate\(.*?\n\}\n\nvoid ItlIwx::\s*\n"
    r"iwx_endscan",
    source,
    re.S,
)
if match is None:
    raise SystemExit("FAIL: cannot isolate ItlIwx::iwx_newstate")

body = match.group(0)
required = (
    "if (ic->ic_state == IEEE80211_S_AUTH &&",
    "nstate == IEEE80211_S_ASSOC)",
    "err = that->iwx_rs_init(sc, (iwx_node *)ni, false);",
    "sc->ns_nstate = nstate;",
    "sc->ns_arg = arg;",
    "return sc->sc_newstate(ic, nstate, arg);",
)
for token in required:
    if token not in body:
        raise SystemExit(f"FAIL: missing AUTH -> ASSOC commit token: {token}")

rate_init = body.index(
    "err = that->iwx_rs_init(sc, (iwx_node *)ni, false);"
)
direct = body.index("return sc->sc_newstate(ic, nstate, arg);")
queued = body.index(
    "that->iwx_add_task(sc, sc->sc_nswq, &sc->newstate_task);"
)
if not rate_init < direct < queued:
    raise SystemExit(
        "FAIL: TLC init and AUTH -> ASSOC commit must precede async queueing"
    )

guard = body[body.index("if (ic->ic_state == IEEE80211_S_AUTH &&"):direct]
if "IEEE80211_S_ASSOC" not in guard:
    raise SystemExit("FAIL: direct commit is not restricted to S_ASSOC")
if "if (err)" not in guard or "&sc->init_task" not in guard:
    raise SystemExit("FAIL: inline TLC failure does not rearm initialization")

task_match = re.search(
    r"void ItlIwx::\s*\n"
    r"iwx_newstate_task\(.*?\n\}\n\nint ItlIwx::\s*\n"
    r"iwx_newstate",
    source,
    re.S,
)
if task_match is None:
    raise SystemExit("FAIL: cannot isolate ItlIwx::iwx_newstate_task")
task_body = task_match.group(0)
if "case IEEE80211_S_ASSOC:" not in task_body:
    raise SystemExit("FAIL: generic queued ASSOC lower work was removed")
if "iwx_rs_init(sc, (iwx_node *)ic->ic_bss, false)" not in task_body:
    raise SystemExit("FAIL: non-AUTH ASSOC transitions lost TLC initialization")

if "getMainCommandGate()->runAction" in body:
    raise SystemExit(
        "FAIL: IWX state transition must not introduce a blocking gate call"
    )

print("PASS: IWX AUTH -> ASSOC preserves TLC order on the RX workloop")
PY
