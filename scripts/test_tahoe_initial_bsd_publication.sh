#!/usr/bin/env bash
set -euo pipefail
PROJECT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
TEST_DIR="$(mktemp -d)"
trap 'rm -f "$TEST_DIR/production.inc" "$TEST_DIR/test"; rmdir "$TEST_DIR"' EXIT
python3 - "$PROJECT_DIR" "$TEST_DIR" <<'PY'
from pathlib import Path
import sys
root, out = map(Path, sys.argv[1:])
source = (root / 'AirportItlwm/AirportItlwmV2.cpp').read_text()
def method(signature):
    start = source.index(signature)
    depth = 0
    for end in range(source.index('{', start), len(source)):
        if source[end] == '{': depth += 1
        elif source[end] == '}':
            depth -= 1
            if depth == 0: return source[start:end + 1]
    raise AssertionError(signature)
(out / 'production.inc').write_text(method('IOReturn AirportItlwm::publishDefaultAPSTAInterface()') +
    '\n' + method('void AirportItlwm::publishInitialBSDInterfaces()'))
PY
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -Wno-unused-parameter -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer -I "$TEST_DIR" \
    "$PROJECT_DIR/tests/tahoe_initial_bsd_publication_test.cpp" -o "$TEST_DIR/test"
"$TEST_DIR/test"
bash "$PROJECT_DIR/scripts/test_tahoe_default_apsta_publication_contract.sh"
