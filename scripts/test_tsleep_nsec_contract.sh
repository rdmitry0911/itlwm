#!/usr/bin/env bash
# Static contract for the BSD-compatible nanosecond sleep adapter.
set -euo pipefail

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
hpp="$root/include/HAL/ItlHalService.hpp"
cpp="$root/include/HAL/ItlHalService.cpp"
iwn="$root/itlwm/hal_iwn/ItlIwn.cpp"

grep -Fq 'uint64_t timo' "$hpp"
grep -Fq 'uint64_t timo' "$cpp"
if grep -Fq 'int tsleep_nsec(void *ident, int priority, const char *wmesg, int timo)' "$hpp"; then
    echo 'legacy narrowed tsleep_nsec signature remains' >&2
    exit 1
fi

grep -Fq 'if (timo != UINT64_MAX)' "$cpp"
grep -Fq 'ts.tv_sec = timo / 1000000000ULL;' "$cpp"
grep -Fq 'ts.tv_nsec = timo % 1000000000ULL;' "$cpp"
grep -Fq 'struct timespec *timeout = nullptr;' "$cpp"
grep -Fq 'msleep(ident, this->inner_lock, priority, wmesg, timeout)' "$cpp"

# The existing FH-DMA caller intentionally asks for five seconds; it must
# reach the widened adapter instead of truncating to a signed nanosecond int.
grep -Fq 'tsleep_nsec(sc, PCATCH, "iwninit", SEC_TO_NSEC(5))' "$iwn"

echo 'tsleep_nsec transport contract: PASS'
