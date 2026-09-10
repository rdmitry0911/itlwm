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
    "nstate == IEEE80211_S_ASSOC &&",
    "that->getMainWorkLoop()->inGate())",
    "that->prepareStateTransition(nstate, arg, &request)",
    "const int error = that->iwx_rs_init(sc, (iwx_node *)ic->ic_bss, false);",
    "return that->postStateTransitionCommit(request, error);",
)
for token in required:
    if token not in body:
        raise SystemExit(f"FAIL: missing AUTH -> ASSOC commit token: {token}")

rate_init = body.index(
    "const int error = that->iwx_rs_init(sc, (iwx_node *)ic->ic_bss, false);"
)
direct = body.index("return that->postStateTransitionCommit(request, error);")
queued = body.index("that->enqueueStateTransition(request)")
if not rate_init < direct < queued:
    raise SystemExit(
        "FAIL: TLC init and AUTH -> ASSOC commit must precede async queueing"
    )

guard = body[body.index("if (ic->ic_state == IEEE80211_S_AUTH &&"):direct]
if "IEEE80211_S_ASSOC" not in guard:
    raise SystemExit("FAIL: direct commit is not restricted to S_ASSOC")
drain = re.search(r"drainStateTransitionCommit\(IOInterruptEventSource.*?\n\}", source, re.S).group(0)
recover = re.search(r"recoverStateTransition\(const ItlStateTransitionRequest.*?\n\}", source, re.S).group(0)
assert "if (error != 0)" in drain and "recoverStateTransition(request)" in drain
assert recover.index("stateTransitionCurrent(request)") < recover.index("scanCommand.open = false")
assert recover.index("scanCommand.open = false") < recover.index("&com.init_task")

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

post = re.search(r"postStateTransitionCommit\(const ItlStateTransitionRequest.*?\n\}", source, re.S).group(0)
assert post.index("getMainWorkLoop()->inGate()") < post.index("drainStateTransitionCommit(source)")
assert drain.index("stateTransitionCurrent(request)") < drain.index("com.sc_newstate(")
assert "runAction" not in post + drain
print("PASS: IWX AUTH -> ASSOC preserves TLC ordering, exact identity and main-workloop commit")
PY
