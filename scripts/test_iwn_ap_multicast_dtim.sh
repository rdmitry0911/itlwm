#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
TEST_DIR="$(mktemp -d)"
trap 'rm -f "$TEST_DIR/production.inc" "$TEST_DIR/test"; rmdir "$TEST_DIR"' EXIT
python3 - "$PROJECT_DIR" "$TEST_DIR/production.inc" <<'PY'
from pathlib import Path
import os
import subprocess
import sys

root = Path(sys.argv[1])
source = (root / 'itlwm/hal_iwn/ItlIwn.cpp').read_text()
baseline = os.environ.get('IWN_AP_MCAST_BASELINE')
old = subprocess.check_output(
    ['git', 'show', baseline + ':itlwm/hal_iwn/ItlIwn.cpp'],
    cwd=root, text=True) if baseline else source

def function(text, signature):
    start = text.index(signature)
    brace = text.index('{', start)
    depth = 0
    for end in range(brace, len(text)):
        if text[end] == '{':
            depth += 1
        elif text[end] == '}':
            depth -= 1
            if depth == 0:
                return text[start:end + 1]
    raise AssertionError(signature)

tx = function(source, 'int ItlIwn::iwn_send_ap_data_frame(')
alloc = function(source, 'iwn_alloc_tx_ring(struct iwn_softc *sc,')
reopen = function(source, 'iwn_clear_oactive(struct iwn_softc *sc,')
for needle in ('iwn_ap_data_queue(multicast,',
               'const bool qosData = !multicast && apClientQos;',
               'multicastPowerSave |= candidate->powerSave;',
               'if (moreData || multicastPowerSave)',
               'if (!multicast && apClientQos)'):
    assert needle in tx, needle
assert alloc.count('qid == IWN_IPAN_MCAST_QUEUE') == 2
assert 'ring->qid == IWN_IPAN_MCAST_QUEUE' in reopen
assert 'airportItlwmRequestAPTxDequeue' in reopen

definitions = (root / 'itlwm/hal_iwn/if_iwnreg.h').read_text()
names = ('IWN_IPAN_BE_QUEUE', 'IWN_IPAN_MCAST_QUEUE', 'IWN_IPAN_CMD_QUEUE',
         'IWN_TX_RING_COUNT', 'IWN_TX_RING_HIMARK', 'IWN_TX_RING_LOMARK')
lines = [line for line in definitions.splitlines()
         if line.startswith('#define ') and line.split()[1] in names]
assert len(lines) == len(names)
Path(sys.argv[2]).write_text('\n'.join(lines) + '\n' +
    function(source, 'static int iwn_ap_data_queue(') + '\n' +
    function(old, 'uint32_t ItlIwn::getAPTxFreeSpace() const') + '\n')
PY
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I "$TEST_DIR" "$PROJECT_DIR/tests/iwn_ap_multicast_dtim_test.cpp" \
    -o "$TEST_DIR/test"
"$TEST_DIR/test"
