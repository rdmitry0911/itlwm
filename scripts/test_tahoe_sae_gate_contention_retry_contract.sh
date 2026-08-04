#!/usr/bin/env bash
# Cross-family source contract for bounded SAE submission retries while the
# private workloop gate is shared with concurrent APSTA traffic.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

python3 - "$root" <<'PY'
from pathlib import Path
import re
import sys


root = Path(sys.argv[1])


def fail(message: str) -> None:
    raise SystemExit(f"SAE gate-contention retry contract: {message}")


def body(source: str, name: str, label: str) -> str:
    pattern = re.compile(r"\b" + re.escape(name) +
                         r"\s*\([^;{}]*\)\s*\{", re.S)
    match = pattern.search(source)
    if match is None:
        fail(f"missing {label}")
    opening = source.rfind("{", match.start(), match.end())
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
    fail(f"unterminated {label}")


def require(source: str, token: str, label: str) -> None:
    if token not in source:
        fail(f"{label} missing token: {token}")


def ordered(source: str, label: str, *tokens: str) -> None:
    cursor = 0
    for token in tokens:
        position = source.find(token, cursor)
        if position < 0:
            fail(f"{label} missing ordered token: {token}")
        cursor = position + len(token)


families = (
    ("IWN", "iwn", "itlwm/hal_iwn/ItlIwn.cpp", "itlwm/hal_iwn/ItlIwn.cpp"),
    ("IWM", "iwm", "itlwm/hal_iwm/IwmSaeEngine.inc", "itlwm/hal_iwm/ItlIwm.cpp"),
    ("IWX", "iwx", "itlwm/hal_iwx/IwxSaeEngine.inc", "itlwm/hal_iwx/ItlIwx.cpp"),
)

for upper, lower, engine_path, tx_path in families:
    engine = (root / engine_path).read_text()
    tx = (root / tx_path).read_text()
    limit = f"{upper}_SAE_ENGINE_SUBMIT_RETRY_LIMIT"
    max_shift = f"{upper}_SAE_ENGINE_SUBMIT_RETRY_MAX_SHIFT"
    delay_name = f"{lower}_sae_engine_submit_retry_delay_ms"

    require(engine, f"{limit} = 6", f"{upper} retry limit")
    require(engine, f"{max_shift} = 5", f"{upper} delay cap")
    delay = body(engine, delay_name, f"{upper} retry delay helper")
    ordered(delay, f"{upper} capped exponential delay",
            f"retry_count > {max_shift}",
            f"retry_count = {max_shift}", "return 1U << retry_count")

    submit = body(engine, f"{lower}_sae_engine_submit_prepared",
                  f"{upper} prepared submit")
    ordered(submit, f"{upper} successful-frame retry reset",
            "rc == kIOReturnSuccess", "owner->in_flight_ticket == ticket",
            "owner->submit_retry_count = 0")

    worker = body(engine, f"{lower}_sae_engine_task",
                  f"{upper} SAE worker")
    ordered(worker, f"{upper} deferred retry snapshot and delay",
            "owner->submit_retry_pending = false",
            "retry_count = owner->submit_retry_count",
            f"IOSleep({delay_name}(retry_count))")
    ordered(worker, f"{upper} bounded retry admission",
            "owner->in_flight_ticket == 0",
            f"owner->submit_retry_count <\n                {limit}",
            "owner->submit_retry_count++",
            "owner->submit_retry_pending = true")

    native_submit = body(tx, "submitSaeAuthFrame",
                         f"{upper} native SAE submit")
    require(native_submit, "attemptAction(",
            f"{upper} non-blocking private gate admission")

# retry_count is captured after increment, so retries 1..6 wait
# 2, 4, 8, 16, 32 and 32 ms: bounded to 94 ms per Commit or Confirm frame.
delays = [1 << min(count, 5) for count in range(1, 7)]
if delays != [2, 4, 8, 16, 32, 32] or sum(delays) != 94:
    fail(f"unexpected bounded delay model: {delays}, total={sum(delays)}")

print("PASS: IWN/IWM/IWX keep SAE gate retries non-blocking and bounded to 94 ms per frame")
PY
