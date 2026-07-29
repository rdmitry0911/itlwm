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
    duplicate = re.search(
        r"if \(sc->ns_nstate == nstate && nstate != IEEE80211_S_SCAN &&\s*"
        r"nstate != IEEE80211_S_AUTH\)\s*"
        r"return 0;",
        body,
    )
    if duplicate is None:
        raise SystemExit(
            f"FAIL: {label} does not reject duplicate queued states"
        )

    run_cleanup = body.find("if (ic->ic_state == IEEE80211_S_RUN)")
    queue = body.find(
        f"that->{prefix}_add_task(sc, sc->sc_nswq, &sc->newstate_task);"
    )
    if run_cleanup < 0 or queue < 0:
        raise SystemExit(f"FAIL: cannot find {label} cleanup/queue boundary")
    if not duplicate.end() < run_cleanup < queue:
        raise SystemExit(
            f"FAIL: {label} duplicate return does not precede cleanup and queue"
        )

    guard = body[duplicate.start():duplicate.end()]
    if "IEEE80211_S_SCAN" not in guard or "IEEE80211_S_AUTH" not in guard:
        raise SystemExit(
            f"FAIL: {label} duplicate suppression lost SCAN/AUTH exceptions"
        )

print("PASS: IWM/IWX reject duplicate async state transitions before cleanup")
PY
