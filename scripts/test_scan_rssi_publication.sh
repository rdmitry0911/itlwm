#!/usr/bin/env bash
set -euo pipefail
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_root=$(mktemp -d)
trap 'test ! -f "$test_root/test" || unlink "$test_root/test"; rmdir "$test_root"' EXIT
python3 - "$root" <<'PY' | "${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=undefined,address -I"$root" -x c++ - -o "$test_root/test"
from pathlib import Path
import os
import subprocess
import sys
root = Path(sys.argv[1])
def read(path):
    return (root / path).read_text()
def function(text, marker):
    start = text.index(marker)
    opening = text.index('{', start)
    while ';' in text[start:opening]:
        start = text.index(marker, start + len(marker))
        opening = text.index('{', start)
    depth = 0
    for end in range(opening, len(text)):
        depth += (text[end] == '{') - (text[end] == '}')
        if depth == 0:
            return text[start:end + 1]
    raise AssertionError('unterminated ' + marker)
v2_path = 'AirportItlwm/AirportItlwmV2.cpp'
sky_path = 'AirportItlwm/AirportItlwmSkywalkInterface.cpp'
v2, sky = read(v2_path), read(sky_path)
rx = read('itl80211/openbsd/net80211/ieee80211_input.c')
node = read('itl80211/openbsd/net80211/ieee80211_node.c')
common = read('itl80211/openbsd/net80211/ieee80211.c')
common_h = read('itl80211/openbsd/net80211/ieee80211_var.h')
builders_v2, builders_sky = v2, sky
# A negative control can compile the exact old producers with the new RX
# fixture. It must fail at the missing measured-RSSI metadata assertion.
old = os.environ.get('SCAN_RSSI_BUILDERS_REF')
if old:
    builders_v2, builders_sky = [subprocess.check_output(
        ['git', '-C', str(root), 'show', old + ':' + p], text=True)
        for p in (v2_path, sky_path)]
receive = function(rx, 'void\nieee80211_recv_probe_resp(')
assert receive.index('ic->ic_stats.is_rx_chanmismatch++') < receive.index(
    'ieee80211_record_scan_rssi(ic, ni, rxi, chan);')
assert receive.index('IEEE80211_ADDR_COPY(ni->ni_bssid, wh->i_addr3);') < receive.index(
    'ieee80211_record_scan_rssi(ic, ni, rxi, chan);')
for path in ('itlwm/hal_iwn/ItlIwn.cpp', 'itlwm/hal_iwm/rx.cpp',
             'itlwm/hal_iwx/ItlIwx.cpp'):
    assert 'rxi.rxi_chan =' in read(path), path
for marker in ('postWclScanResultsGated(OSObject *target',
               'postWclPhysicalScanCompletionGated(OSObject *target'):
    publication = function(v2, marker)
    assert publication.index('prepareTahoeWclScanRssiPublication(') < publication.index(
        'APPLE80211_M_WCL_SCAN_RESULT') < publication.index(
        'recordTahoeWclScanRssiPublication(')
physical = function(v2, 'postWclPhysicalScanCompletionGated(OSObject *target')
assert physical.index('if (!suppressResults)') < physical.index(
    'if (!that->ownsWclPhysicalScanCompletion(') < physical.index(
    'prepareTahoeWclScanRssiPublication(')
collector = function(v2, 'static void collectTahoeWclScanResultSnapshot(')
assert 'recordTahoeWclScanRssiPublication' not in collector
old_collector = os.environ.get('SCAN_CENSUS_COLLECTOR_REF')
if old_collector:
    collector = function(subprocess.check_output(['git', '-C', str(root),
        'show', old_collector + ':' + v2_path], text=True),
        'static void collectTahoeWclScanResultSnapshot(')
reserve = function(v2, 'IOReturn AirportItlwm::reserveWclPhysicalScan(')
assert 'lifecycle.resultObservationFloorUs = observationFloor;' in reserve
assert 'lifecycle.resultObservationStarted = false;' in reserve
terminal = function(v2, 'static bool snapshotWclPhysicalScanTerminal(')
assert 'plan.generation == generation' in terminal
assert 'collector.observationFloorUs = state.resultObservationFloorUs;' in terminal
assert 'collector.plan = &plan;' in terminal
assert 'started.backend_generation, true)' in v2
ibss = function(node, 'void\nieee80211_create_ibss(')
for field in ('ni_scan_observation_stamp', 'ni_scan_rssi_stamp', 'ni_scan_rssi_published_stamp',
              'ni_scan_rssi', 'ni_scan_rssi_chan'):
    assert field + ' = 0;' in ibss
chunks = [
    function(common_h, 'struct ieee80211_wcl_scan_plan {') + ';',
    function(common, 'int\nieee80211_wcl_scan_plan_channel_allowed('),
    function(v2, 'static uint64_t tahoeScanObservationTime('),
    function(v2, 'TahoeWclPhysicalScanContracts::StartDisposition\nAirportItlwm::activateWclPhysicalScan('),
    function(rx, 'static void\nieee80211_record_scan_rssi('),
    function(node, 'int\nieee80211_scan_rssi_publication('),
    function(v2, 'static uint16_t buildTahoePrimaryChanSpec('),
    function(builders_v2, 'static bool buildTahoeWclScanResultPayload('),
    v2[v2.index('struct TahoeWclScanResultSnapshot {'):
       v2.index('/* net80211 holds splnet while it invokes this callback.')],
    collector,
    function(builders_sky, 'static bool buildTahoeCurrentBssPayload(\n    ItlHalService *hal,\n    TahoeBssManagerContracts::BeaconPayload *payload)\n{'),
]
print(read('tests/scan_rssi_publication_test.cpp').replace(
    '// PRODUCTION_FUNCTIONS', '\n\n'.join(chunks)))
PY
"$test_root/test"
