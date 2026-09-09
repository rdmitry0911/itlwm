#!/usr/bin/env bash
set -euo pipefail
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_root=$(mktemp -d)
trap 'test ! -f "$test_root/test" || unlink "$test_root/test"; rmdir "$test_root"' EXIT

python3 - "$root" <<'PY' | "${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=undefined -x c++ - -o "$test_root/test"
from pathlib import Path
import sys

root = Path(sys.argv[1])
source = (root / 'AirportItlwm/AirportItlwmAPSTAOwner.cpp').read_text()

def function(signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 0
    for end in range(opening, len(source)):
        depth += (source[end] == '{') - (source[end] == '}')
        if depth == 0:
            return source[start:end + 1]
    raise AssertionError(signature)

functions = function('static uint64_t apsta_primary_association_epoch(')
for name in ('armPrimaryStaHandoffScan', 'consumePrimaryStaHandoffScan',
             'shouldRetainPrimaryStaCarrier', 'consumePrimaryStaCarrierHold',
             'consumePrimaryStaPostStopWclAssociation'):
    functions += '\n' + function('bool AirportItlwmAPSTAOwner::' + name + '(')

stop = function('IOReturn AirportItlwmAPSTAOwner::stopLower()')
assert 'apsta_primary_association_epoch(primary)' in stop
assert 'primaryStaCarrierHoldPending = primaryStaHandoffAssociationEpoch != 0;' in stop
terminal = function('IOReturn AirportItlwmAPSTAOwner::driveLowerStopToTerminal()')
assert 'apsta_primary_association_epoch(retainedPrimary)' in terminal
assert terminal.index('primaryStaPostStopAssociationEpoch =') < terminal.index(
    'restoreRetainedPrimaryStaLinkAfterStop();')
for name in ('bool AirportItlwmAPSTAOwner::initWithController(',
             'void AirportItlwmAPSTAOwner::free()'):
    lifecycle = function(name)
    assert 'primaryStaHandoffAssociationEpoch = 0;' in lifecycle
    assert 'primaryStaPostStopAssociationEpoch = 0;' in lifecycle

fixture = (root / 'tests/apsta_retained_association_epoch_test.cpp').read_text()
print(fixture.replace('// PRODUCTION_FUNCTIONS', functions))
PY
"$test_root/test"
