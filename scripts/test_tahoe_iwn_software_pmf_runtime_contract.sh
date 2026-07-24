#!/usr/bin/env bash
# Static safety contract for the bounded IWN software-PMF runtime wrapper.
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
RUNNER="$ROOT/scripts/run_tahoe_iwn_software_pmf_runtime.sh"
EVIDENCE_CONTRACT="$ROOT/scripts/test_tahoe_iwn_software_pmf_runtime_evidence_contract.sh"
GENERIC_RUNTIME_CONTRACT="$ROOT/scripts/test_tahoe_post_plti_trace_runtime_contract.sh"

fail() {
    printf 'FAIL: IWN software-PMF runtime runner contract: %s\n' "$*" >&2
    exit 1
}

require_literal() {
    local needle="$1" label="$2"
    grep -Fq -- "$needle" "$RUNNER" || fail "missing $label"
}

forbid_literal() {
    local needle="$1" label="$2"
    ! grep -Fq -- "$needle" "$RUNNER" || fail "forbidden $label"
}

[ -f "$RUNNER" ] || fail 'runner missing'
[ -f "$EVIDENCE_CONTRACT" ] || fail 'evidence contract missing'
[ -f "$GENERIC_RUNTIME_CONTRACT" ] || fail 'delegated generic runtime contract missing'
bash -n "$RUNNER"
bash "$GENERIC_RUNTIME_CONTRACT"
bash "$EVIDENCE_CONTRACT" --self-test

for needle in \
    'POST_PLTI_RUNNER=' \
    'PINNED_GUEST="devops@127.0.0.1"' \
    'PINNED_PORT=3322' \
    'PINNED_GUEST_BUILD="25C56"' \
    'PINNED_GUEST_HOSTKEY_SHA256' \
    'StrictHostKeyChecking=yes' \
    '--release-zip' \
    '--candidate-provenance' \
    'trace_client_sha256_from_provenance' \
    'trace_client_binding' \
    'capture_identity before' \
    'capture_identity after' \
    '--identity-evidence' \
    '--trace-client-sha256 "$TRACE_CLIENT_SHA256"' \
    '--arm-while-radio-off' \
    'get iwn-software-pmf-report' \
    'PMF_REPORT_ONE_READ=1' \
    'PMF_REPORT_TWO_READ=1' \
    'PMF_DOUBLE_READ_STABLE=1' \
    'INITIAL_SOFTWARE_PMF_OBSERVED' \
    'KERNEL_CHAIN_OBSERVED' \
    'generic_chain_is_positive' \
    'pmf_chain_is_positive' \
    'PMF_ENTRY_COUNT" = "$GENERIC_ENTRY_COUNT' \
    'GENERIC_FAILURE_PHASE' \
    'GENERIC_ARMED_WHILE_RADIO_OFF' \
    'GENERIC_BACKEND_PREFLIGHT_IWN' \
    'trace_armed_while_radio_off' \
    'backend_preflight_iwn' \
    'episode_count' \
    'active_episode' \
    'dropped_entries' \
    'trace-client-postflight' \
    'runtime-attestation.json' \
    'saved_profile_autojoin_only' \
    'requested_cycles": 1' \
    'SAE or WPA3 functionality' \
    'protected-MPDU interoperability or data-plane verification'; do
    require_literal "$needle" "required runtime/provenance token: $needle"
done

# It is only a provenance and sealed-report companion.  The delegated runner
# owns the one radio cycle; this wrapper must not grow a second association or
# network-control surface of its own.
for needle in \
    '-setairportnetwork' \
    '-listpreferredwirelessnetworks' \
    'airport -s' \
    'wdutil scan' \
    'scanForNetworks' \
    'networksetup -setairportpower' \
    'ipconfig ' \
    'ifconfig ' \
    'route add' \
    'route delete' \
    'route change' \
    'kmutil ' \
    'kextload' \
    'kextutil' \
    'shutdown -r' \
    '/sbin/reboot' \
    'scp ' \
    'rsync ' \
    'curl '; do
    forbid_literal "$needle" "capability: $needle"
done

python3 - "$RUNNER" <<'PY'
from pathlib import Path
import sys

text = Path(sys.argv[1]).read_text(encoding="utf-8")
receipt_start = text.find('trace_client_sha256_from_provenance() {')
receipt_end = text.find('\n}\n\nTRACE_CLIENT_SHA256=', receipt_start)
if receipt_start < 0 or receipt_end < 0:
    raise SystemExit('FAIL: IWN runtime lacks provenance receipt helper')
receipt_helper = text[receipt_start:receipt_end]
if 'from pathlib import Path' not in receipt_helper:
    raise SystemExit('FAIL: IWN runtime provenance receipt helper lacks Path import')
if receipt_helper.count('trace_client_sha256_from_candidate_provenance(') != 1:
    raise SystemExit('FAIL: IWN runtime must make one typed provenance receipt lookup')
if 'trace_client_sha256_from_candidate_provenance(Path(sys.argv[2]))' not in receipt_helper:
    raise SystemExit('FAIL: IWN runtime provenance receipt lookup must pass Path')

ordered = (
    'remote_trace_client_exists || fail_phase trace-client-preflight',
    'capture_identity before || fail_phase candidate-identity-before',
    '"$POST_PLTI_RUNNER" --trace-tool "$TRACE_TOOL"',
    'read_generic_attestation || fail_phase delegated-runner-attestation',
    'capture_pmf_report pmf-read-1',
    'capture_pmf_report pmf-read-2',
    'capture_identity after || fail_phase candidate-identity-after',
    'remote_trace_client_exists || fail_phase trace-client-postflight',
    'generic_chain_is_positive && pmf_chain_is_positive',
)
cursor = 0
for token in ordered:
    cursor = text.find(token, cursor)
    if cursor < 0:
        raise SystemExit(f"FAIL: IWN runtime order missing {token}")
    cursor += len(token)

if text.count('capture_pmf_report pmf-read-') != 2:
    raise SystemExit('FAIL: IWN runtime must make exactly two frozen PMF reads')
if 'remote_radio_power' in text:
    raise SystemExit('FAIL: IWN wrapper must delegate rather than add another radio owner')
if 'INITIAL_SOFTWARE_PMF_OBSERVED' not in text or 'KERNEL_CHAIN_OBSERVED' not in text:
    raise SystemExit('FAIL: IWN runtime lacks dual-verdict gate')
if 'PMF_CAPTURE_GENERATION" = "$GENERIC_CAPTURE_GENERATION' not in text:
    raise SystemExit('FAIL: IWN PMF report is not generation-bound to generic trace')
print('PASS: IWN software-PMF runtime runner static safety contract')
PY
