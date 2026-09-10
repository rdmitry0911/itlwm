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
current = (root / 'itlwm/hal_iwn/ItlIwn.cpp').read_text()
baseline = os.environ.get('IWN_TOPOLOGY_BASELINE')
source = subprocess.check_output(
    ['git', 'show', baseline + ':itlwm/hal_iwn/ItlIwn.cpp'],
    cwd=root, text=True) if baseline else current

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

# The current topology producer is also the fixture for an unchanged old
# complete scheduler method; this negative control checks that consumer,
# not a claim that the old init path called the newly introduced producer.
parts = [function(current, 'static void iwn_configure_tx_queue_topology(')]
parts += [function(source, signature) for signature in (
    'int ItlIwn::\niwn_ampdu_tx_start(',
    'void ItlIwn::\niwn5000_ampdu_tx_start(',
)]
(out / 'production.inc').write_text('\n\n'.join(parts))
registers = (root / 'itlwm/hal_iwn/if_iwnreg.h').read_text() + '\n' + (
    root / 'itlwm/hal_iwn/if_iwnvar.h').read_text()
selected = {
    'IWN_HW_REV_TYPE_4965', 'IWN_HW_REV_TYPE_6005',
    'IWN_IPAN_CMD_QUEUE', 'IWN_DEFAULT_CMD_QUEUE',
    'IWN_IPAN_AUX_QUEUE', 'IWN_IPAN_FIRST_AGG_QUEUE',
    'IWN4965_FIRST_AGG_TXQUEUE', 'IWN5000_FIRST_AGG_TXQUEUE',
    'IWN_NUM_AMPDU_TID', 'IWN_AMPDU_MAX', 'IWN_SCHED_WINSZ',
    'IWN_NODE_UPDATE', 'IWN_FLAG_SET_DISABLE_TID',
    'IWN_AGG_SSN_TO_TXQ_IDX', 'IWN_HBUS_TARG_WRPTR', 'IWN_SCHED_BASE',
    'IWN_TX_RING_COUNT',
    'IWN5000_SCHED_QUEUE_STATUS', 'IWN5000_TXQ_STATUS_CHGACT',
    'IWN5000_TXQ_STATUS_ACTIVE', 'IWN5000_SCHED_TRANS_TBL',
    'IWN5000_SCHED_QCHAIN_SEL', 'IWN5000_SCHED_AGGR_SEL',
    'IWN5000_SCHED_QUEUE_RDPTR', 'IWN5000_SCHED_QUEUE_OFFSET',
    'IWN5000_SCHED_INTR_MASK',
}
lines = [line for line in registers.splitlines()
         if line.startswith('#define ') and
         line.split()[1].split('(')[0] in selected]
assert len(lines) == len(selected), (len(lines), len(selected))
pan_flag = next(line for line in registers.splitlines()
                if line.strip().startswith('IWN_UCODE_TLV_FLAGS_PAN ') and '=' in line)
(out / 'registers.inc').write_text('\n'.join(lines) + '\nenum {\n' + pan_flag + '\n};\n')
if not baseline:
    init = function(current, 'int ItlIwn::\niwn_init(')
    assert init.index('iwn_read_firmware(sc)') < init.index(
        'iwn_configure_tx_queue_topology(sc)') < init.index('iwn_hw_init(sc)')
    assert init.index('memset(sc->sc_tx_ba, 0, sizeof(sc->sc_tx_ba))') < init.index(
        'iwn_configure_tx_queue_topology(sc)')
    assert 'IWN5000_FIRST_AGG_TXQUEUE + tid' not in current
    for signature, marker in (
        ('int ItlIwn::\niwn_ampdu_tx_start(', 'sc->first_agg_txq + tid'),
        ('void ItlIwn::\niwn_ampdu_tx_stop(', 'sc->first_agg_txq + tid'),
        ('int ItlIwn::\niwn_tx(', 'sc->first_agg_txq + tid'),
        ('void ItlIwn::\niwn_ampdu_tx_done(', 'desc->qid - sc->first_agg_txq'),
        ('void ItlIwn::\niwn_rx_compressed_ba(', 'sc->first_agg_txq + cba->tid'),
        ('int ItlIwn::iwn_ap_stop_tx_queue_mask(', 'com.first_agg_txq + staTid'),
    ):
        assert marker in function(current, signature), signature
PY
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -g \
    -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
    -I "$TEST_DIR" "$PROJECT_DIR/tests/iwn_tx_queue_topology_test.cpp" \
    -o "$TEST_DIR/test"
"$TEST_DIR/test"
