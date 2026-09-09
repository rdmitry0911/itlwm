#!/usr/bin/env bash
set -euo pipefail
PROJECT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
TEST_DIR="$(mktemp -d)"
trap 'rm -f "$TEST_DIR/production.inc" "$TEST_DIR/registers.inc" "$TEST_DIR/test"; rmdir "$TEST_DIR"' EXIT
python3 - "$PROJECT_DIR" "$TEST_DIR" <<'PY'
from pathlib import Path
import os
import subprocess
import sys

root, out = map(Path, sys.argv[1:])
baseline = os.environ.get('IWN_STA_STOP_BASELINE')
source = subprocess.check_output(
    ['git', 'show', baseline + ':itlwm/hal_iwn/ItlIwn.cpp'],
    cwd=root, text=True) if baseline else (
        root / 'itlwm/hal_iwn/ItlIwn.cpp').read_text()

def function(signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 0
    for end in range(opening, len(source)):
        if source[end] == '{':
            depth += 1
        elif source[end] == '}':
            depth -= 1
            if depth == 0:
                return source[start:end + 1]
    raise AssertionError(signature)

names = (
    ('bool', 'iwn_ampdu_txq_can_advance'),
    ('bool', 'iwn_ampdu_txq_advance'),
    ('void', 'iwn_ampdu_tx_stop'),
    ('void', 'iwn4965_ampdu_tx_stop'),
    ('void', 'iwn5000_ampdu_tx_stop'),
)
(out / 'production.inc').write_text('\n\n'.join(
    function(kind + ' ItlIwn::\n' + name + '(') for kind, name in names))
registers = (root / 'itlwm/hal_iwn/if_iwnreg.h').read_text()
selected = {
    'IWN_TX_RING_COUNT', 'IWN_AGG_SSN_TO_TXQ_IDX',
    'IWN4965_FIRST_AGG_TXQUEUE', 'IWN5000_FIRST_AGG_TXQUEUE',
    'IWN_SCHED_BASE', 'IWN_HBUS_TARG_WRPTR',
    'IWN4965_SCHED_QUEUE_STATUS', 'IWN5000_SCHED_QUEUE_STATUS',
    'IWN4965_SCHED_QUEUE_RDPTR', 'IWN5000_SCHED_QUEUE_RDPTR',
    'IWN4965_SCHED_INTR_MASK', 'IWN5000_SCHED_INTR_MASK',
    'IWN5000_SCHED_AGGR_SEL',
    'IWN4965_TXQ_STATUS_CHGACT', 'IWN5000_TXQ_STATUS_CHGACT',
    'IWN4965_TXQ_STATUS_INACTIVE', 'IWN5000_TXQ_STATUS_INACTIVE',
    'IWN_NODE_UPDATE', 'IWN_FLAG_SET_DISABLE_TID',
}
lines = [line for line in registers.splitlines()
         if line.startswith('#define ') and
         line.split()[1].split('(')[0] in selected]
assert len(lines) == len(selected), (len(lines), len(selected))
(out / 'registers.inc').write_text('\n'.join(lines) + '\n')
PY
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I "$TEST_DIR" "$PROJECT_DIR/tests/iwn_sta_aggregate_stop_test.cpp" \
    -o "$TEST_DIR/test"
"$TEST_DIR/test"
