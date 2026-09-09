#!/usr/bin/env bash
set -euo pipefail
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_root=$(mktemp -d)
trap 'test ! -f "$test_root/test" || unlink "$test_root/test"; rmdir "$test_root"' EXIT

python3 - "$root" <<'PY' | "${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=undefined -x c++ - -o "$test_root/test"
from pathlib import Path
import sys

root = Path(sys.argv[1])
bip = (root / 'itl80211/openbsd/net80211/ieee80211_crypto_bip.c').read_text()
proto = (root / 'itl80211/openbsd/net80211/ieee80211_proto.c').read_text()
pae = (root / 'itl80211/openbsd/net80211/ieee80211_pae_input.c').read_text()

def function(source, name):
    start = source.index('\n' + name + '(')
    opening = source.index('{', start)
    depth = 0
    for end in range(opening, len(source)):
        depth += (source[end] == '{') - (source[end] == '}')
        if depth == 0:
            return 'int' + source[start:end + 1]
    raise AssertionError(name)

finish = function(proto, 'ieee80211_pae_mfp_txn_finish_publish_locked')
assert finish.index('ieee80211_pae_mfp_txn_live_locked') < finish.index(
    'ieee80211_bip_key_rearm_locked') < finish.index('ni->ni_ptk = txn->ptk;')
begin = function(proto, 'ieee80211_pae_mfp_txn_begin')
assert '(have_igtk && retain_igtk)' in begin
assert 'txn->retain_igtk = !!retain_igtk;' in begin
assert 'if (have_igtk || retain_igtk)' in begin
for name in ('ieee80211_pae_mfp_msg3_begin', 'ieee80211_pae_mfp_group_begin'):
    builder = function(pae, name)
    assert 'if (bip_update)' in builder
    assert '} else\n            retain_igtk = 1;' in builder
    assert '&igtk_key, have_igtk, retain_igtk,' in builder
assert pae.count('ieee80211_bip_key_rearm(ic, ni, &bip_key)') == 2

functions = '\n'.join(function(bip, name) for name in (
    'ieee80211_bip_key_shape_valid', 'ieee80211_bip_key_is_slot',
    'ieee80211_bip_ctx_live_locked', 'ieee80211_bip_key_rearm_locked'))
fixture = (root / 'tests/net80211_retained_igtk_rearm_test.cpp').read_text()
print('#include <initializer_list>')
print(fixture.replace('// PRODUCTION_FUNCTIONS', functions))
PY
"$test_root/test"
