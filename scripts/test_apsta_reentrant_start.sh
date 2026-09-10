#!/bin/bash
set -euo pipefail
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
fixture="$repo/tests/apsta_reentrant_start_test.cpp"
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/aiam-apsta-reentrant-test.XXXXXX")
trap 'test ! -f "$test_dir/test" || unlink "$test_dir/test"; rmdir "$test_dir"' EXIT
# An optional immutable source revision is a compilable negative control.
python3 - "$repo" "$fixture" "${1:-}" <<'PY' | "${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -I "$repo" -x c++ - -o "$test_dir/test"
from pathlib import Path
import subprocess
import sys
repo = Path(sys.argv[1])
current = (repo / 'AirportItlwm/AirportItlwmAPSTAOwner.cpp').read_text()
source = subprocess.check_output(
    ['git', '-C', str(repo), 'show',
     sys.argv[3] + ':AirportItlwm/AirportItlwmAPSTAOwner.cpp'], text=True
) if sys.argv[3] else current
header = (repo / 'AirportItlwm/AirportItlwmAPSTAOwner.hpp').read_text()
hal = (repo / 'include/HAL/ItlHalService.hpp').read_text()
def block(text, signature):
    start = text.index(signature)
    opening = text.index('{', start)
    depth = 0
    for end in range(opening, len(text)):
        depth += (text[end] == '{') - (text[end] == '}')
        if not depth:
            return text[start:end + 1]
    raise AssertionError(signature)
types = block(header, 'enum AirportItlwmAPSTAOwnerLifecycleState') + ';\n'
types += block(hal, 'struct ItlHalApConfig {') + ';\n'
types += block(source, 'enum {\n    kAirportItlwmAPSTAAuthUpperOpen') + ';\n'
types += block(current, 'class APSTALowerCallScope {') + ';\n'
functions = block(source, 'static uint64_t apsta_primary_association_epoch(')
functions += '\n' + block(source, 'static bool apsta_lower_start_retryable(')
functions += '\n' + block(source, 'static bool apsta_lower_stop_pending(')
# The pre-fix methods never call this newly added helper. Supplying its
# declaration's body lets the unchanged old control flow compile and fail
# the runtime invariant, rather than failing on a missing test dependency.
functions += '\n' + block(current,
    'void AirportItlwmAPSTAOwner::advanceHostAPRequestGeneration()')
for signature in ('IOReturn AirportItlwmAPSTAOwner::startLowerIfReady()',
                  'IOReturn AirportItlwmAPSTAOwner::stopLower()',
                  'IOReturn AirportItlwmAPSTAOwner::driveLowerStopToTerminal()',
                  'void AirportItlwmAPSTAOwner::resetRuntimeState()',
                  'void AirportItlwmAPSTAOwner::prepareRetainedLowerReset(',
                  'void AirportItlwmAPSTAOwner::prepareEmptyAPForRadioReset()',
                  'IOReturn AirportItlwmAPSTAOwner::resumeAfterRadioReset()',
                  'IOReturn AirportItlwmAPSTAOwner::setHostAPMode('):
    functions += '\n' + block(source, signature)
print(Path(sys.argv[2]).read_text().replace('// PRODUCTION_TYPES', types)
      .replace('// PRODUCTION_FUNCTIONS', functions))
PY
"$test_dir/test"
