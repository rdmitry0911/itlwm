#!/bin/bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

python3 - \
    "$repo_root/itlwm/hal_iwm/mac80211.cpp" \
    "$repo_root/itlwm/hal_iwx/ItlIwx.cpp" <<'PY'
from pathlib import Path
import re
import sys

cases = (
    ("IWM", Path(sys.argv[1]), "ItlIwm", "iwm"),
    ("IWX", Path(sys.argv[2]), "ItlIwx", "iwx"),
)

for label, path, cls, prefix in cases:
    source = path.read_text()
    match = re.search(
        rf"int {cls}::\s*\n"
        rf"{prefix}_newstate\(.*?\n\}}\n\nvoid {cls}::\s*\n"
        rf"{prefix}_endscan",
        source,
        re.S,
    )
    if match is None:
        raise SystemExit(f"FAIL: cannot isolate {label} newstate callback")

    body = match.group(0)
    duplicate = re.search(r"if \(admission != 0\)\s*return admission == EALREADY \? 0 : admission;", body)
    if duplicate is None:
        raise SystemExit(
            f"FAIL: {label} does not reject duplicate queued states"
        )

    run_cleanup = body.find("if (ic->ic_state == IEEE80211_S_RUN)")
    queue = body.find("that->enqueueStateTransition(request)")
    if run_cleanup < 0 or queue < 0:
        raise SystemExit(f"FAIL: cannot find {label} cleanup/queue boundary")
    if not duplicate.end() < run_cleanup < queue:
        raise SystemExit(
            f"FAIL: {label} duplicate return does not precede cleanup and queue"
        )

    admission_source = path.with_name(f"{cls}.cpp").read_text()
    admission = re.search(r"prepareStateTransition\(int state,.*?\n\}", admission_source, re.S).group(0)
    assert "state != IEEE80211_S_SCAN && state != IEEE80211_S_AUTH" in admission
    assert "stateTransition.duplicate(com.sc_generation, state, argument, identity)" in admission
    assert "error = EALREADY" in admission
    assert "ItlScanCommandPolicy::identityLocked(&com.sc_ic)" in admission
    assert admission.index("IOSimpleLockLockDisableInterrupt(ownerLock)") < admission.index(
        "ItlScanCommandPolicy::identityLocked(&com.sc_ic)") < admission.index(
        "stateTransition.duplicate(com.sc_generation, state, argument, identity)")
    assert "sc->ns_nstate == nstate" not in body

print("PASS: IWM/IWX deduplicate only exact state/argument/join/epoch requests before cleanup")
PY
