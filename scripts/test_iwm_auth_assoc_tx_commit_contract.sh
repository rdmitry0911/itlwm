#!/bin/bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source_file="$repo_root/itlwm/hal_iwm/mac80211.cpp"

python3 - "$source_file" <<'PY'
from pathlib import Path
import re
import sys

source = Path(sys.argv[1]).read_text()

match = re.search(
    r"int ItlIwm::\s*\n"
    r"iwm_newstate\(.*?\n\}\n\nvoid ItlIwm::\s*\n"
    r"iwm_endscan",
    source,
    re.S,
)
if match is None:
    raise SystemExit("FAIL: cannot isolate ItlIwm::iwm_newstate")

body = match.group(0)
required = (
    "if (ic->ic_state == IEEE80211_S_AUTH &&",
    "nstate == IEEE80211_S_ASSOC)",
    "sc->ns_nstate = nstate;",
    "sc->ns_arg = arg;",
    "return sc->sc_newstate(ic, nstate, arg);",
)
for token in required:
    if token not in body:
        raise SystemExit(f"FAIL: missing AUTH -> ASSOC commit token: {token}")

direct = body.index("return sc->sc_newstate(ic, nstate, arg);")
queued = body.index(
    "that->iwm_add_task(sc, sc->sc_nswq, &sc->newstate_task);"
)
if direct >= queued:
    raise SystemExit(
        "FAIL: AUTH -> ASSOC must commit before the generic async queue"
    )

guard = body[body.index("if (ic->ic_state == IEEE80211_S_AUTH &&"):direct]
if "IEEE80211_S_ASSOC" not in guard:
    raise SystemExit("FAIL: direct commit is not restricted to S_ASSOC")

if "getMainCommandGate()->runAction" in body:
    raise SystemExit(
        "FAIL: IWM state transition must not introduce a blocking gate call"
    )

print("PASS: IWM AUTH -> ASSOC commits before the lossy async TX edge")
PY
