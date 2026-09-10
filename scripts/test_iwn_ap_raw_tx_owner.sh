#!/usr/bin/env bash
set -euo pipefail
PROJECT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
TEST_DIR="$(mktemp -d)"
trap 'rm -f "$TEST_DIR/production.inc" "$TEST_DIR/wire.inc" "$TEST_DIR/test"; rm -rf "$TEST_DIR/test.dSYM"; rmdir "$TEST_DIR"' EXIT
python3 - "$PROJECT_DIR" "$TEST_DIR" <<'PY'
from pathlib import Path
import os
import re
import subprocess
import sys

root, out = map(Path, sys.argv[1:])
current = (root / 'itlwm/hal_iwn/ItlIwn.cpp').read_text()
baseline = os.environ.get('IWN_AP_RAW_BASELINE')
source = subprocess.check_output(
    ['git', 'show', baseline + ':itlwm/hal_iwn/ItlIwn.cpp'],
    cwd=root, text=True) if baseline else current

def block(text, signature):
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

methods = '\n'.join(block(source, sig) for sig in (
    'int ItlIwn::iwn_send_ap_mgmt_frame(',
    'int ItlIwn::iwn_send_ap_raw_frame(',
))
(out / 'production.inc').write_text(methods)
ieee = (root / 'itl80211/openbsd/net80211/ieee80211.h').read_text()
headers = ieee + '\n' + (root / 'itlwm/hal_iwn/if_iwnvar.h').read_text()
headers += '\n' + (root / 'itl80211/openbsd/net80211/ieee80211_crypto.h').read_text()
selected = {
    'EDCA_NUM_AC',
    'IEEE80211_ADDR_LEN', 'IEEE80211_NWID_LEN', 'IEEE80211_TKIP_MICLEN',
    'IEEE80211_FC0_TYPE_MASK', 'IEEE80211_FC0_TYPE_MGT',
    'IEEE80211_FC0_TYPE_CTL', 'IEEE80211_FC0_TYPE_DATA',
    'IEEE80211_FC0_SUBTYPE_MASK', 'IEEE80211_FC0_SUBTYPE_BAR',
    'IEEE80211_FC0_SUBTYPE_AUTH', 'IEEE80211_FC0_SUBTYPE_PROBE_RESP',
    'IEEE80211_FC0_SUBTYPE_ASSOC_RESP', 'IEEE80211_FC0_SUBTYPE_ACTION',
    'IEEE80211_FC1_PROTECTED', 'IEEE80211_CCMP_HDRLEN',
    'IEEE80211_CCMP_MICLEN', 'IEEE80211_WEP_EXTIV',
    'IEEE80211_BA_TID_INFO_MASK', 'IEEE80211_BA_TID_INFO_SHIFT',
    'IWN_TX_FIRST_TB_SIZE', 'IWN_TX_FIRST_TB_STRIDE',
    'IWN_AP_MGMT_PAYLOAD_SIZE', 'IWN_AP_DATA_PAYLOAD_SIZE',
}
defines = [line for line in headers.splitlines()
           if line.startswith('#define') and len(line.split()) >= 3
           and line.split()[1] in selected]
assert len(defines) == len(selected), selected - {line.split()[1] for line in defines}
wire = '\n'.join(defines) + '\n'
wire += '\n'.join(block(ieee, 'struct ' + name + ' {') + ' __packed;'
                  for name in ('ieee80211_frame', 'ieee80211_frame_min'))
stage = current.index('enum {\n    IWN_AP_STAGE_IDLE')
wire += '\n' + current[stage:current.index('};', stage) + 2]
(out / 'wire.inc').write_text(wire)
PY
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I "$TEST_DIR" -I "$PROJECT_DIR" \
    "$PROJECT_DIR/tests/iwn_ap_raw_tx_owner_test.cpp" -o "$TEST_DIR/test"
"$TEST_DIR/test"
