#!/usr/bin/env bash
# Narrow static contract for the credential-safe direct-IWN-SAE runtime runner.
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
RUNNER="$ROOT/scripts/run_tahoe_iwn_direct_sae_runtime.sh"
GENERIC_RUNTIME_CONTRACT="$ROOT/scripts/test_tahoe_post_plti_trace_runtime_contract.sh"
DIRECT_CONTRACT="$ROOT/include/ClientKit/AirportItlwmIwnDirectSaeTraceContracts.h"
TRACE_CLIENT="$ROOT/AirportItlwmPostPltiTrace/airport_itlwm_post_plti_trace.c"

fail() {
    printf 'FAIL: IWN direct-SAE runtime runner contract: %s\n' "$*" >&2
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

[ -x "$RUNNER" ] || fail 'runner missing or not executable'
[ -f "$GENERIC_RUNTIME_CONTRACT" ] || fail 'delegated runtime contract missing'
[ -f "$DIRECT_CONTRACT" ] || fail 'direct-SAE evaluator contract missing'
[ -f "$TRACE_CLIENT" ] || fail 'direct-SAE trace client missing'
bash -n "$RUNNER"
bash "$GENERIC_RUNTIME_CONTRACT"

for needle in \
    'POST_PLTI_RUNNER=' \
    'PINNED_GUEST="devops@127.0.0.1"' \
    'PINNED_PORT=3322' \
    'PINNED_GUEST_BUILD="25C56"' \
    'PINNED_GUEST_HOSTKEY_SHA256' \
    'StrictHostKeyChecking=yes' \
    '--identity-evidence' \
    '--trace-client-sha256' \
    '--arm-while-radio-off' \
    'get iwn-direct-sae-report' \
    'DIRECT_SAE_4WAY_PORT_VALID' \
    'DIRECT_REPORT_ONE_READ=1' \
    'DIRECT_REPORT_TWO_READ=1' \
    'DIRECT_DOUBLE_READ_STABLE=1' \
    'delegated_fresh_scan_lifecycle_is_complete' \
    'direct_chain_is_positive' \
    'DIRECT_CAPTURE_GENERATION" = "$GENERIC_CAPTURE_GENERATION' \
    'DIRECT_ENTRY_COUNT" = "$GENERIC_ENTRY_COUNT' \
    'trace_armed_while_radio_off' \
    'saved_profile_autojoin_only' \
    'fresh_scan_state' \
    'secret_argument": "none"' \
    'wireless_identity_collected": False' \
    'network_secret_collected": False' \
    'runtime-attestation.json' \
    'local_only_raw_artifacts' \
    'physical-host validation'; do
    require_literal "$needle" "required runtime token: $needle"
done

# This wrapper must not grow a second radio or association control surface.
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

python3 - "$RUNNER" "$DIRECT_CONTRACT" "$TRACE_CLIENT" <<'PY'
from pathlib import Path
import re
import sys


text = Path(sys.argv[1]).read_text(encoding="utf-8")
direct = Path(sys.argv[2]).read_text(encoding="utf-8")
client = Path(sys.argv[3]).read_text(encoding="utf-8")

ordered = (
    'remote_trace_client_exists || fail_phase trace-client-preflight',
    '"$POST_PLTI_RUNNER" --trace-tool "$TRACE_TOOL"',
    'read_generic_attestation || fail_phase delegated-runner-attestation',
    'capture_direct_report direct-sae-report-read-1',
    'capture_direct_report direct-sae-report-read-2',
    'remote_trace_client_exists || fail_phase trace-client-postflight',
    'if delegated_fresh_scan_lifecycle_is_complete && direct_chain_is_positive; then',
)
cursor = 0
for token in ordered:
    cursor = text.find(token, cursor)
    if cursor < 0:
        raise SystemExit(f"FAIL: direct-SAE runtime order missing {token}")
    cursor += len(token)

if text.count('capture_direct_report direct-sae-report-read-') != 2:
    raise SystemExit('FAIL: direct-SAE runner must make exactly two frozen direct-report reads')
if 'remote_radio_power' in text:
    raise SystemExit('FAIL: direct-SAE wrapper must delegate radio ownership')
if 'DIRECT_SAE_4WAY_PORT_VALID' not in text:
    raise SystemExit('FAIL: direct-SAE runner lacks positive four-way gate')
if 'DIRECT_FIRST_MISSING_STAGE" = none' not in text:
    raise SystemExit('FAIL: direct-SAE runner does not require a complete direct trace')
if 'DIRECT_CAPTURE_GENERATION" = "$GENERIC_CAPTURE_GENERATION' not in text:
    raise SystemExit('FAIL: direct report is not generation-bound to the reset trace')
if 'DIRECT_ENTRY_COUNT" = "$GENERIC_ENTRY_COUNT' not in text:
    raise SystemExit('FAIL: direct report is not bound to the sealed trace buffer')

# Each categorical direct evaluator outcome must have a stable client spelling
# and must be accepted by the bounded runner as a non-secret diagnostic result.
# Otherwise a newly useful negative result could be discarded before the
# attestation, masking the exact layer that failed in the driver.
whitelist_start = text.find('case "$DIRECT_VERDICT" in')
whitelist_end = text.find('esac', whitelist_start)
if whitelist_start < 0 or whitelist_end < 0:
    raise SystemExit('FAIL: direct-SAE runner verdict whitelist missing')
whitelist = text[whitelist_start:whitelist_end]
verdicts = {
    'kAirportItlwmIwnDirectSaeTraceVerdictIntegrityInconclusive': 'INTEGRITY_INCONCLUSIVE',
    'kAirportItlwmIwnDirectSaeTraceVerdictBackendUnsupported': 'BACKEND_UNSUPPORTED',
    'kAirportItlwmIwnDirectSaeTraceVerdictBranchNotObserved': 'BRANCH_NOT_OBSERVED',
    'kAirportItlwmIwnDirectSaeTraceVerdictFreshScanNotObserved': 'FRESH_SCAN_NOT_OBSERVED',
    'kAirportItlwmIwnDirectSaeTraceVerdictRequestNoBssSelection': 'REQUEST_NO_BSS_SELECTION',
    'kAirportItlwmIwnDirectSaeTraceVerdictJoinBssNotObserved': 'JOIN_BSS_NOT_OBSERVED',
    'kAirportItlwmIwnDirectSaeTraceVerdictNodeMfpNotNegotiated': 'NODE_MFP_NOT_NEGOTIATED',
    'kAirportItlwmIwnDirectSaeTraceVerdictAuthStateNotObserved': 'AUTH_STATE_NOT_OBSERVED',
    'kAirportItlwmIwnDirectSaeTraceVerdictCommitTxNotComplete': 'COMMIT_TX_NOT_COMPLETE',
    'kAirportItlwmIwnDirectSaeTraceVerdictPeerCommitNotAccepted': 'PEER_COMMIT_NOT_ACCEPTED',
    'kAirportItlwmIwnDirectSaeTraceVerdictConfirmTxNotComplete': 'CONFIRM_TX_NOT_COMPLETE',
    'kAirportItlwmIwnDirectSaeTraceVerdictPeerConfirmNotValidated': 'PEER_CONFIRM_NOT_VALIDATED',
    'kAirportItlwmIwnDirectSaeTraceVerdictPmkNotClaimed': 'PMK_NOT_CLAIMED',
    'kAirportItlwmIwnDirectSaeTraceVerdictAssocDescriptorNotAccepted': 'ASSOC_DESCRIPTOR_NOT_ACCEPTED',
    'kAirportItlwmIwnDirectSaeTraceVerdictAssocExchangeNotComplete': 'ASSOC_EXCHANGE_NOT_COMPLETE',
    'kAirportItlwmIwnDirectSaeTraceVerdictFourWayNotComplete': 'FOUR_WAY_NOT_COMPLETE',
    'kAirportItlwmIwnDirectSaeTraceVerdictPmfPtkSoftwareCcmpNotObserved': 'PMF_PTK_SOFTWARE_CCMP_NOT_OBSERVED',
    'kAirportItlwmIwnDirectSaeTraceVerdictPmfGtkSoftwareCcmpNotObserved': 'PMF_GTK_SOFTWARE_CCMP_NOT_OBSERVED',
    'kAirportItlwmIwnDirectSaeTraceVerdictPmfIgtkStageNotObserved': 'PMF_IGTK_STAGE_NOT_OBSERVED',
    'kAirportItlwmIwnDirectSaeTraceVerdictPmfIgtkPublicationNotObserved': 'PMF_IGTK_PUBLICATION_NOT_OBSERVED',
    'kAirportItlwmIwnDirectSaeTraceVerdictPmfKeysetPublicationNotObserved': 'PMF_KEYSET_PUBLICATION_NOT_OBSERVED',
    'kAirportItlwmIwnDirectSaeTraceVerdictDirectSae4WayPortValid': 'DIRECT_SAE_4WAY_PORT_VALID',
}
stages = {
    'kAirportItlwmIwnDirectSaeTraceMissingStageNone': 'none',
    'kAirportItlwmIwnDirectSaeTraceMissingStageCaptureSeal': 'capture-seal',
    'kAirportItlwmIwnDirectSaeTraceMissingStageFreshScan': 'fresh-scan',
    'kAirportItlwmIwnDirectSaeTraceMissingStageBssSelection': 'bss-selection',
    'kAirportItlwmIwnDirectSaeTraceMissingStageJoinBss': 'join-bss',
    'kAirportItlwmIwnDirectSaeTraceMissingStageNodeMfp': 'node-mfp',
    'kAirportItlwmIwnDirectSaeTraceMissingStageAuthState': 'auth-state',
    'kAirportItlwmIwnDirectSaeTraceMissingStageCommitTx': 'commit-tx',
    'kAirportItlwmIwnDirectSaeTraceMissingStagePeerCommit': 'peer-commit',
    'kAirportItlwmIwnDirectSaeTraceMissingStageConfirmTx': 'confirm-tx',
    'kAirportItlwmIwnDirectSaeTraceMissingStagePeerConfirm': 'peer-confirm',
    'kAirportItlwmIwnDirectSaeTraceMissingStagePmkClaim': 'pmk-claim',
    'kAirportItlwmIwnDirectSaeTraceMissingStageAssocDescriptor': 'assoc-descriptor',
    'kAirportItlwmIwnDirectSaeTraceMissingStageAssocExchange': 'assoc-exchange',
    'kAirportItlwmIwnDirectSaeTraceMissingStageFourWay': 'four-way',
    'kAirportItlwmIwnDirectSaeTraceMissingStagePmfPtkSoftwareCcmp': 'pmf-ptk-software-ccmp',
    'kAirportItlwmIwnDirectSaeTraceMissingStagePmfGtkSoftwareCcmp': 'pmf-gtk-software-ccmp',
    'kAirportItlwmIwnDirectSaeTraceMissingStagePmfIgtkStage': 'pmf-igtk-stage',
    'kAirportItlwmIwnDirectSaeTraceMissingStagePmfIgtkPublication': 'pmf-igtk-publication',
    'kAirportItlwmIwnDirectSaeTraceMissingStagePmfKeysetPublication': 'pmf-keyset-publication',
    'kAirportItlwmIwnDirectSaeTraceMissingStagePortValid': 'port-valid',
}
for enum_name, token in verdicts.items():
    if enum_name not in direct:
        raise SystemExit(f'FAIL: direct evaluator verdict missing: {enum_name}')
    if re.search(rf'case\s+{re.escape(enum_name)}:\s*return\s+"{re.escape(token)}";', client) is None:
        raise SystemExit(f'FAIL: direct client verdict mapping missing: {enum_name}')
    if token not in whitelist:
        raise SystemExit(f'FAIL: direct runner rejects mapped verdict: {token}')
for enum_name, token in stages.items():
    if enum_name not in direct:
        raise SystemExit(f'FAIL: direct evaluator stage missing: {enum_name}')
    if re.search(rf'case\s+{re.escape(enum_name)}:\s*return\s+"{re.escape(token)}";', client) is None:
        raise SystemExit(f'FAIL: direct client stage mapping missing: {enum_name}')

# The emitted aggregate must not have fields that could carry a network name,
# hardware address, or secret.  Candidate artifact digests are allowed because
# they are necessary to bind the test to the loaded lab build.
start = text.find('document = {', text.find('write_safe_attestation() {'))
end = text.find('\nPath(output).write_text', start)
if start < 0 or end < 0:
    raise SystemExit('FAIL: direct-SAE runtime attestation builder missing')
attestation = text[start:end]
for forbidden in ('ssid', 'bssid', 'passphrase', 'password', 'keychain', 'credential'):
    if re.search(forbidden, attestation, flags=re.IGNORECASE):
        raise SystemExit(f'FAIL: direct-SAE attestation carries forbidden field label: {forbidden}')
for required in ('"wireless_identity_collected": False', '"network_secret_collected": False'):
    if required not in attestation:
        raise SystemExit(f'FAIL: direct-SAE attestation lacks privacy assertion: {required}')
print('PASS: IWN direct-SAE runtime runner static safety contract')
PY
