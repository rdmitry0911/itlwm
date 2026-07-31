#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
tx_cpp="$repo_root/itlwm/hal_iwm/mac80211.cpp"
var_h="$repo_root/itlwm/hal_iwm/if_iwmvar.h"

python3 - "$tx_cpp" "$var_h" <<'PY'
from pathlib import Path
import re
import sys

tx = Path(sys.argv[1]).read_text()
var = Path(sys.argv[2]).read_text()

def require(text: str, needle: str, message: str) -> None:
    if needle not in text:
        raise SystemExit(message)

require(var, "uint8_t sta_id;", "iwm_tx_data does not retain its firmware station owner")
require(tx, "data->sta_id = tx->sta_id;", "TX submission does not snapshot the firmware station owner")
require(tx, "bc_ent = htole16(len | (sta_id << 12));",
        "scheduler byte-count entry still ignores the caller's station id")

body = re.search(
    r"void ItlIwm::\s*\niwm_update_sched\([^\)]*uint8_t sta_id[^\)]*\)\s*\{(?P<body>.*?)\n\}",
    tx,
    re.S,
)
if body is None:
    raise SystemExit("cannot locate iwm_update_sched body")
if "IWM_STATION_ID << 12" in body.group("body"):
    raise SystemExit("iwm_update_sched still hard-codes station zero")

completion_resets = tx.count(
    "iwm_reset_sched(sc, ring->qid, ring->tail, txd->sta_id);"
)
if completion_resets < 2:
    raise SystemExit("not every IWM completion path releases the submitted station owner")

print("PASS: IWM scheduler byte-count ownership follows each submitted firmware station")
PY
