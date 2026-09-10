#!/bin/bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source_file="$repo_root/itlwm/hal_iwm/mac80211.cpp"

python3 - "$source_file" <<'PY'
from pathlib import Path
import re
import sys

source = Path(sys.argv[1]).read_text()
delivery = Path(sys.argv[1]).with_name("ItlIwm.cpp").read_text()

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
    "nstate == IEEE80211_S_ASSOC &&",
    "that->getMainWorkLoop()->inGate())",
    "that->prepareStateTransition(nstate, arg, &request)",
    "return that->postStateTransitionCommit(request, 0);",
)
for token in required:
    if token not in body:
        raise SystemExit(f"FAIL: missing AUTH -> ASSOC commit token: {token}")

direct = body.index("return that->postStateTransitionCommit(request, 0);")
queued = body.index("that->enqueueStateTransition(request)")
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

post = re.search(r"postStateTransitionCommit\(.*?\n\}", delivery, re.S).group(0)
drain = re.search(r"drainStateTransitionCommit\(IOInterruptEventSource.*?\n\}", delivery, re.S).group(0)
assert post.index("getMainWorkLoop()->inGate()") < post.index("drainStateTransitionCommit(source)")
assert drain.index("stateTransitionCurrent(request)") < drain.index("com.sc_newstate(")
assert "runAction" not in post + drain
print("PASS: IWM AUTH -> ASSOC retains the exact request and recursive main-workloop TX edge")
PY
