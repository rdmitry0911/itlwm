#!/usr/bin/env bash
set -euo pipefail
PROJECT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
TEST_DIR="$(mktemp -d)"
trap 'rm -f "$TEST_DIR/production.inc" "$TEST_DIR/registers.inc" "$TEST_DIR/test"; rm -rf "$TEST_DIR/test.dSYM"; rmdir "$TEST_DIR"' EXIT
python3 - "$PROJECT_DIR" "$TEST_DIR" <<'PY'
from pathlib import Path
import os
import re
import subprocess
import sys

root, out = map(Path, sys.argv[1:])
current = (root / 'itlwm/hal_iwn/ItlIwn.cpp').read_text()
baseline = os.environ.get('IWN_AP_STOP_BASELINE')
old = subprocess.check_output(
    ['git', 'show', baseline + ':itlwm/hal_iwn/ItlIwn.cpp'],
    cwd=root, text=True) if baseline else current

def block(source, signature):
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

# New helpers are inert in the old stop method. The negative control compiles
# the unchanged old stop and scheduler methods, not a simulated failing branch.
methods = [block(current, sig) for sig in (
    'int ItlIwn::iwn_ap_stop_tx_queue_mask(',
    'int ItlIwn::iwn_retire_flushed_ap_tx(',
    'int ItlIwn::iwn_continue_ap_stop_after_flush(',
    'static bool iwn_ap_stop_tx_prepare_doorbell(',
    'void ItlIwn::iwn_note_ap_stop_tx_flush(',
)]
methods += [block(old, sig) for sig in (
    'bool ItlIwn::\niwn_ampdu_txq_can_advance(',
    'bool ItlIwn::\niwn_ampdu_txq_advance(',
    'void ItlIwn::iwn_ap_ampdu_tx_stop(',
    'IOReturn ItlIwn::stopAPMode(',
)]
(out / 'production.inc').write_text('\n\n'.join(methods))
registers = (root / 'itlwm/hal_iwn/if_iwnreg.h').read_text()
selected = {
    'IWN_TX_RING_COUNT', 'IWN_AGG_SSN_TO_TXQ_IDX', 'IWN_NUM_AMPDU_TID',
    'IWN_SCHED_BASE', 'IWN_HBUS_TARG_WRPTR', 'IWN_HW_REV_TYPE_4965',
    'IWN4965_SCHED_QUEUE_STATUS', 'IWN5000_SCHED_QUEUE_STATUS',
    'IWN4965_SCHED_QUEUE_RDPTR', 'IWN5000_SCHED_QUEUE_RDPTR',
    'IWN4965_SCHED_INTR_MASK', 'IWN5000_SCHED_AGGR_SEL',
    'IWN4965_TXQ_STATUS_CHGACT', 'IWN5000_TXQ_STATUS_CHGACT',
    'IWN4965_TXQ_STATUS_INACTIVE', 'IWN5000_TXQ_STATUS_INACTIVE',
    'IWN_IPAN_CMD_QUEUE', 'IWN_IPAN_MCAST_QUEUE', 'IWN_IPAN_AUX_QUEUE',
    'IWN_IPAN_FIRST_AGG_QUEUE', 'IWN_CMD_TXFIFO_FLUSH',
    'IWN_TXFIFO_FLUSH_DROP_ALL', 'IWN_CMD_WIPAN_RXON',
    'IWN_MODE_P2P',
    'IWN5000_SCHED_TX_STATUS_OFFSET',
}
defines = [line for line in registers.splitlines()
           if line.startswith('#define ') and
           line.split()[1].split('(')[0] in selected]
assert len(defines) == len(selected), selected - {
    line.split()[1].split('(')[0] for line in defines}
stage_start = current.index('enum {\n    IWN_AP_STAGE_IDLE')
stage_end = current.index('};', stage_start) + 2
wire = block(registers, 'struct iwn_txfifo_flush_cmd {') + ' __attribute__((packed));'
(out / 'registers.inc').write_text(
    '\n'.join(defines) + '\n' + current[stage_start:stage_end] + '\n' + wire)

if not baseline:
    assert current.index('void iwn_mem_set_region_4(struct iwn_softc *, uint32_t, uint32_t, int);') < current.index('void ItlIwn::iwn_ap_ampdu_tx_stop(')
    # Completion is decoded from the submitted command-ring owner before its
    # storage is released. Notification type mismatch cannot admit retirement.
    interrupt = block(current, 'iwn_notif_intr(struct iwn_softc *sc)')
    assert interrupt.index('iwn_note_ap_stop_tx_flush(') < interrupt.index('iwn_cmd_done(sc, desc)')
    assert 'desc->type != IWN_CMD_TXFIFO_FLUSH' in interrupt
    assert '(desc->qid & 0xf) == sc->command_queue' in interrupt
    reset = block(current, 'void ItlIwn::iwn_reset_ap_runtime_state()')
    assert 'apStopTxFlushIndex = 0;' in reset and 'apStopTxQueueMask = 0;' in reset
    single = block(current, 'int ItlIwn::iwn_set_ap_client_tx_ba(')
    assert 'iwn_ampdu_txq_advance(' not in single
    assert single.index('iwn_nic_lock(&com)') < single.index('iwn_ap_ampdu_tx_stop(')
PY
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I "$TEST_DIR" "$PROJECT_DIR/tests/iwn_ap_stop_tx_retirement_test.cpp" \
    -o "$TEST_DIR/test"
"$TEST_DIR/test" "$@"
