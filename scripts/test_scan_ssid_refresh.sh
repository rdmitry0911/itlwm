#!/usr/bin/env bash
set -euo pipefail
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_root=$(mktemp -d)
trap 'test ! -f "$test_root/test" || unlink "$test_root/test"; rmdir "$test_root"' EXIT
python3 - "$root" <<'PY' | "${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=undefined,address -x c++ - -o "$test_root/test"
from pathlib import Path
import sys
root = Path(sys.argv[1])
source = (root / 'itl80211/openbsd/net80211/ieee80211_input.c').read_text()
start = source.index('static void\nieee80211_refresh_scan_ssid(')
opening = source.index('{', start)
depth = 0
for end in range(opening, len(source)):
    depth += (source[end] == '{') - (source[end] == '}')
    if depth == 0:
        function = source[start:end + 1]
        break
else:
    raise AssertionError('unterminated production function')
receive = source[source.index('void\nieee80211_recv_probe_resp('):]
receive = receive[:receive.index('\n#ifndef IEEE80211_STA_ONLY\n/*-')]
assert 'ieee80211_refresh_scan_ssid(ic, ni, ssid);' in receive
assert "if (ssid[1] != 0 && ni->ni_essid[0] == '\\0')" not in receive
fixture = (root / 'tests/scan_ssid_refresh_test.cpp').read_text()
print(fixture.replace('// PRODUCTION_FUNCTION', function))
PY
"$test_root/test"
