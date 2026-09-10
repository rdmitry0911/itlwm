#!/usr/bin/env bash
set -euo pipefail
PROJECT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
TEST_DIR="$(mktemp -d)"
trap 'rm -f "$TEST_DIR/production.inc" "$TEST_DIR/test"; rm -rf "$TEST_DIR/test.dSYM"; rmdir "$TEST_DIR"' EXIT
python3 - "$PROJECT_DIR" "$TEST_DIR" <<'PY'
from pathlib import Path
import os
import subprocess
import sys

root, out = map(Path, sys.argv[1:])
baseline = os.environ.get('AP_TX_DEQUEUE_BASELINE')
def source(path):
    return subprocess.check_output(
        ['git', 'show', baseline + ':' + path], cwd=root, text=True
    ) if baseline else (root / path).read_text()

def function(text, signature):
    start = text.index(signature)
    opening = text.index('{', start)
    depth = 0
    for end in range(opening, len(text)):
        if text[end] == '{':
            depth += 1
        elif text[end] == '}':
            depth -= 1
            if depth == 0:
                return text[start:end + 1]
    raise AssertionError(signature)

controller = source('AirportItlwm/AirportItlwmV2.cpp')
iwn = source('itlwm/hal_iwn/ItlIwn.cpp')
parts = [function(controller, 'void AirportItlwm::requestAPTxDequeue()')]
parts += [function(iwn, signature) for signature in (
    'IOReturn ItlIwn::iwn_ap_data_tx_action(',
    'IOReturn ItlIwn::transmitAPData(',
)]
(out / 'production.inc').write_text('\n\n'.join(parts))
PY
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -g \
    -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
    -I "$TEST_DIR" "$PROJECT_DIR/tests/apsta_async_tx_dequeue_test.cpp" \
    -o "$TEST_DIR/test"
"$TEST_DIR/test" "${AP_TX_DEQUEUE_TEST_MODE:-all}"
