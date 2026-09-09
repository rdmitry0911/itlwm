#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_root=$(mktemp -d)
trap 'test ! -f "$test_root/test" || unlink "$test_root/test"; rmdir "$test_root"' EXIT
python3 - "$root" <<'PY' | "${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=undefined -x c++ - -o "$test_root/test"
from pathlib import Path
import sys
root = Path(sys.argv[1])
source = (root / 'itlwm/hal_iwn/ItlIwn.cpp').read_text()

def function(signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 0
    for end in range(opening, len(source)):
        depth += (source[end] == '{') - (source[end] == '}')
        if depth == 0:
            return source[start:end + 1]
    raise AssertionError(signature)

functions = '\n'.join(function(signature) for signature in (
    'static bool\niwn_wcl_scan_plan_has_eligible_band(',
    'static int\niwn_wcl_scan_initial_band(',
    'void ItlIwn::\niwn_scan_lease_replay_task(void *arg)',
))
fixture = (root / 'tests/iwn_queued_scan_band_test.cpp').read_text()
print(fixture.replace('// PRODUCTION_FUNCTIONS', functions))
PY
"$test_root/test"
