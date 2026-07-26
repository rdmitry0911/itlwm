#!/usr/bin/env bash
# Static safety contract for the one-shot direct-IWN-SAE lab helper.
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)"
CLIENT="$ROOT/AirportItlwmIwnDirectSaeLabClient/airport_itlwm_iwn_direct_sae_lab_client.c"
BUILD="$ROOT/scripts/build_tahoe_iwn_direct_sae_lab_client.sh"

fail() {
    printf 'FAIL: IWN direct-SAE lab client contract: %s\n' "$*" >&2
    exit 1
}

[ -f "$CLIENT" ] || fail 'client source missing'
[ -x "$BUILD" ] || fail 'client build script missing or not executable'
bash -n "$BUILD"

require() {
    grep -Fq -- "$1" "$CLIENT" || fail "missing $2"
}

forbid() {
    ! grep -Fq -- "$1" "$CLIENT" || fail "forbidden $2"
}

for token in \
    '#include <ClientKit/AirportItlwmIwnLabDirectSaeStimulusV1.h>' \
    'kAirportItlwmIwnLabDirectSaeStimulusUserClientType' \
    'kAirportItlwmIwnLabDirectSaeStimulusQueryReadySelector' \
    'kAirportItlwmIwnLabDirectSaeStimulusSubmitSelector' \
    'IOServiceOpen(' \
    'IOConnectCallStructMethod(' \
    'read(STDIN_FILENO,' \
    'AirportItlwmIwnLabDirectSaeStimulusRequestScrub(&request)' \
    'secure_bzero(&request, sizeof(request))' \
    'IOServiceClose(connection)' \
    'lab-client=not-ready' \
    'lab-client=accepted' \
    'lab-client=rejected' \
    'lab-client=open-unavailable' \
    '--query-ready' \
    '--submit-stdin' \
    '--hold-milliseconds'; do
    require "$token" "required client token: $token"
done

# No general association, scan, credential store, raw frame, or identity
# collection surface belongs in the direct UserClient helper.
for token in \
    'CoreWLAN' 'CWWiFi' 'Security/' 'Keychain' 'SecItem' \
    'networksetup' 'airport -' 'scanForNetworks' 'associateToNetwork' \
    'IORegistryEntryCreateCFProperty' 'IORegistryEntrySetCFProperty' \
    'getenv(' 'fopen(' 'open(' 'write(' 'printf("%s",' \
    'SSID' 'BSSID' 'passphrase' 'password=' 'PMK' 'PMKID' 'PWE' 'KCK' \
    'system('; do
    forbid "$token" "client capability or disclosure token: $token"
done

python3 - "$CLIENT" "$BUILD" <<'PY'
from pathlib import Path
import sys

client = Path(sys.argv[1]).read_text(encoding='utf-8')
build = Path(sys.argv[2]).read_text(encoding='utf-8')

def fail(message: str) -> None:
    raise SystemExit(f'FAIL: IWN direct-SAE lab client contract: {message}')

if client.count('IOConnectCallStructMethod(') != 1:
    fail('client must have exactly one Submit external method')
if client.count('IOConnectCallMethod(') != 1:
    fail('client must have exactly one readiness external method')
if 'if (strcmp(readiness, "ready") != 0) {' not in client:
    fail('Submit does not gate stdin reads on readiness')
ready_gate = client.index('if (strcmp(readiness, "ready") != 0) {')
read_request = client.index('read_exact_request(&request)')
if ready_gate > read_request:
    fail('stdin may be read before readiness')
scrub_after_read = client.index(
    'AirportItlwmIwnLabDirectSaeStimulusRequestScrub(&request);',
    read_request)
if scrub_after_read > client.index('IOServiceClose(connection)'):
    fail('request is not scrubbed before client close')
if 'while (offset < sizeof(*out))' not in client or 'if (count != 0)' not in client:
    fail('stdin framing is not exact and EOF-bound')
if 'kMaximumHoldMilliseconds = 60000u' not in client:
    fail('client hold is not bounded')
for token in ('--iwn-software-pmf-lab', '-framework IOKit',
              '-framework CoreFoundation', 'chmod 700 "$OUTPUT"'):
    if token not in build:
        fail(f'lab-only build script missing {token}')
if 'BUILD_IWN_SOFTWARE_PMF_LAB' in build:
    fail('lab helper must not be enabled by environment')
PY

printf '%s\n' 'PASS: direct-IWN-SAE lab client reads one exact stdin record and emits categories only'
