#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
PROJECT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
TEST_DIR="$(mktemp -d)"
trap 'rm -f "$TEST_DIR/definitions.inc" "$TEST_DIR/production.inc" "$TEST_DIR/test"; rm -rf "$TEST_DIR/test.dSYM"; rmdir "$TEST_DIR"' EXIT
python3 - "$PROJECT_DIR" "$TEST_DIR" <<'PY'
from pathlib import Path
import os
import re
import subprocess
import sys

root, out = map(Path, sys.argv[1:])
base = root / 'itl80211/openbsd/net80211'
revision = os.environ.get('PMF_LEAVE_KEY_BASELINE')
def source(name):
    path = 'itl80211/openbsd/net80211/' + name
    return subprocess.check_output(['git', 'show', revision + ':' + path],
        cwd=root, text=True) if revision else (root / path).read_text()
def function(text, signature):
    start = text.index(signature)
    opening = text.index('{', start)
    depth = 0
    for end in range(opening, len(text)):
        depth += (text[end] == '{') - (text[end] == '}')
        if depth == 0:
            return text[start:end + 1]
    raise AssertionError(signature)

names = '''IEEE80211_F_RSNON IEEE80211_F_PSK IEEE80211_F_MFPR
IEEE80211_F_DESBSSID IEEE80211_NODE_MFP IEEE80211_NODE_TXMGMTPROT
IEEE80211_NODE_PMK IEEE80211_NODE_PMKID IEEE80211_FC0_TYPE_MASK
IEEE80211_FC0_TYPE_MGT IEEE80211_FC0_TYPE_DATA IEEE80211_FC0_SUBTYPE_MASK
IEEE80211_FC0_SUBTYPE_DEAUTH IEEE80211_FC0_SUBTYPE_DISASSOC
IEEE80211_FC0_SUBTYPE_ACTION IEEE80211_FC1_PROTECTED
IEEE80211_KEY_SWCRYPTO'''.split()
headers = '\n'.join((base / name).read_text() for name in
    ('ieee80211.h', 'ieee80211_node.h', 'ieee80211_var.h', 'ieee80211_crypto.h'))
definitions = []
for name in names:
    definitions.append(re.search(r'^#define\s+' + name + r'\s+.*$',
        headers, re.M).group())
definitions.append(re.search(r'enum ieee80211_cipher\s*\{[^}]*\};', headers).group())
(out / 'definitions.inc').write_text('\n'.join(definitions) + '\n')
crypto = source('ieee80211_crypto.c')
parts = [function(source('ieee80211_proto.c'),
    'static void\nieee80211_sae_wcl_request_policy_clear_locked(')]
parts += [function(crypto, signature) for signature in
    ('struct ieee80211_key *\nieee80211_get_txkey(',
     'mbuf_t\nieee80211_encrypt(')]
(out / 'production.inc').write_text('\n\n'.join(parts) + '\n')
PY
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -g \
    -DUSE_APPLE_SUPPLICANT -fsanitize=address,undefined \
    -fno-sanitize-recover=all -fno-omit-frame-pointer \
    -I "$TEST_DIR" "$PROJECT_DIR/tests/pmf_leave_tx_key_test.cpp" \
    -o "$TEST_DIR/test"
"$TEST_DIR/test"
