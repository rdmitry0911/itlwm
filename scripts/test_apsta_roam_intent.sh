#!/usr/bin/env bash
set -euo pipefail
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_root=$(mktemp -d)
trap 'test ! -f "$test_root/test" || unlink "$test_root/test"; rmdir "$test_root"' EXIT

python3 - "$root" <<'PY' | "${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=undefined -x c++ - -o "$test_root/test"
from pathlib import Path
import sys
root = Path(sys.argv[1])

def function(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 0
    for end in range(opening, len(source)):
        depth += (source[end] == '{') - (source[end] == '}')
        if depth == 0:
            return source[start:end + 1]
    raise AssertionError(signature)

sky = (root / 'AirportItlwm/AirportItlwmSkywalkInterface.cpp').read_text()
owner = (root / 'AirportItlwm/AirportItlwmAPSTAOwner.cpp').read_text()
controller = (root / 'AirportItlwm/AirportItlwmV2.cpp').read_text()
functions = function(owner, 'bool AirportItlwmAPSTAOwner::hasHostAPIntent() const')
functions += '\n' + function(controller, 'uint16_t AirportItlwm::getAPSTAPrimaryRoamSharedChannel() const')
functions += '\n' + function(sky, 'IOReturn AirportItlwmSkywalkInterface::\nsetWCL_REASSOC(')
core = (root / 'itl80211/openbsd/net80211/ieee80211.c').read_text()
functions += '\n' + function(core, 'static u_int8_t\nieee80211_wcl_reassoc_primary_channel(')
functions += '\n' + function(core, 'int\nieee80211_wcl_reassoc_candidate_disposition(')
node = (root / 'itl80211/openbsd/net80211/ieee80211_node.c').read_text()
functions += '\n' + function(node, 'int\nieee80211_match_bss(')
carriers = sky[sky.index('struct apple80211_reassoc_candidate\n'):sky.index('struct apple80211_pm_mode\n')]
common = (root / 'itl80211/openbsd/net80211/ieee80211_var.h').read_text()
request_start = common.index('#define IEEE80211_WCL_REASSOC_MAX_CHANSPECS')
request_end = common.index('/*', common.index('struct ieee80211_wcl_reassoc_request {', request_start))
request = common[request_start:request_end]
fixture = (root / 'tests/apsta_roam_intent_test.cpp').read_text()
print(fixture.replace('// PRODUCTION_CARRIERS', carriers)
      .replace('// PRODUCTION_REQUEST', request)
      .replace('// PRODUCTION_FUNCTIONS', functions))
PY
"$test_root/test"
