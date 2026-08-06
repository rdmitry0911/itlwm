#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
source_file="$repo_root/itlwm/hal_iwx/ItlIwx.cpp"

python3 - "$source_file" <<'PY'
from pathlib import Path
import re
import sys

source = Path(sys.argv[1]).read_text()


def function_body(start_pattern: str, end_pattern: str) -> str:
    match = re.search(start_pattern + r"(?P<body>.*?)" + end_pattern,
                      source, re.S)
    if match is None:
        raise SystemExit(f"FAIL: cannot locate function matching {start_pattern}")
    return match.group("body")


exchange = function_body(
    r"static int\s+iwx_ap_exchange_tx_ring_carrier\([^)]*\)\s*\{",
    r"\n\}\n\nstatic bool\s+iwx_sae_wcl_credential_runtime_opted_in",
)
for token in (
    "IOSimpleLockLock(sc->sc_txq_locks[queueId]);",
    "memcpy(displaced, published, sizeof(*displaced));",
    "published->qid = IWX_INVALID_QUEUE;",
    "IOSimpleLockUnlock(sc->sc_txq_locks[queueId]);",
):
    if token not in exchange:
        raise SystemExit(f"FAIL: carrier exchange is missing {token}")
for forbidden in (
    "iwx_reset_tx_ring",
    "iwx_free_tx_ring",
    "iwx_dma_contig_free",
    "IOFree(",
    "->release(",
):
    if forbidden in exchange:
        raise SystemExit(
            f"FAIL: preemption-disabled carrier exchange performs {forbidden}"
        )

enable = function_body(
    r"int ItlIwx::\s*\niwx_tvqm_enable_txq_for_sta\([^)]*\)\s*\{",
    r"\n\}\n\nvoid ItlIwx::\s*\niwx_post_alive",
)
remove = function_body(
    r"int ItlIwx::\s*\niwx_ap_remove_internal_sta\([^)]*\)\s*\{",
    r"\n\}\n\nint ItlIwx::\s*\niwx_ap_add_client_sta",
)

for name, body, carrier in (
    ("TVQM install", enable, "displaced"),
    ("AP station removal", remove, "detached"),
):
    if "iwx_ap_exchange_tx_ring_carrier" not in body:
        raise SystemExit(f"FAIL: {name} does not publish through carrier exchange")
    if "IOSimpleLockLock" in body or "IOSimpleLockUnlock" in body:
        raise SystemExit(f"FAIL: {name} still owns a raw spinlock interval")
    reset = f"iwx_reset_tx_ring(sc, {carrier});"
    release = f"iwx_free_tx_ring(sc, {carrier});"
    if reset not in body or release not in body:
        raise SystemExit(f"FAIL: {name} does not reclaim detached carrier")
    if body.index("iwx_ap_exchange_tx_ring_carrier") > body.index(reset):
        raise SystemExit(f"FAIL: {name} resets carrier before detaching it")
    if body.index(reset) > body.index(release):
        raise SystemExit(f"FAIL: {name} frees carrier before resetting it")

print("PASS: IWX AP TVQM DMA carriers are released outside preemption-disabled locks")
PY
