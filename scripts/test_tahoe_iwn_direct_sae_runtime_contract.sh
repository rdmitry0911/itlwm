#!/usr/bin/env bash
# Narrow static contract for the credential-safe direct-IWN-SAE runtime runner.
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
RUNNER="$ROOT/scripts/run_tahoe_iwn_direct_sae_runtime.sh"
GENERIC_RUNTIME_CONTRACT="$ROOT/scripts/test_tahoe_post_plti_trace_runtime_contract.sh"
LOADED_IDENTITY_CONTRACT="$ROOT/scripts/test_tahoe_iwn_lab_loaded_identity_contract.sh"
DIRECT_EVIDENCE_CONTRACT="$ROOT/scripts/test_tahoe_iwn_direct_sae_runtime_evidence_contract.sh"
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
[ -f "$LOADED_IDENTITY_CONTRACT" ] || fail 'loaded-candidate identity contract missing'
[ -f "$DIRECT_EVIDENCE_CONTRACT" ] || fail 'direct runtime evidence contract missing'
[ -f "$DIRECT_CONTRACT" ] || fail 'direct-SAE evaluator contract missing'
[ -f "$TRACE_CLIENT" ] || fail 'direct-SAE trace client missing'
bash -n "$RUNNER"
bash "$GENERIC_RUNTIME_CONTRACT"
bash "$LOADED_IDENTITY_CONTRACT"
bash "$DIRECT_EVIDENCE_CONTRACT"

for needle in \
    'POST_PLTI_RUNNER=' \
    'PINNED_GUEST="devops@127.0.0.1"' \
    'PINNED_PORT=3322' \
    'PINNED_GUEST_BUILD="25C56"' \
    'PINNED_GUEST_HOSTKEY_SHA256' \
    'StrictHostKeyChecking=yes' \
    'IDENTITY_CAPTURE=' \
    '--candidate-receipt' \
    'load_direct_runtime_candidate_receipt' \
    'itlwm-tahoe-iwn-lab-loaded-identity/v1' \
    'expected_local_lab_candidate' \
    'capture_identity before' \
    'capture_identity after' \
    'IDENTITY_BEFORE_BOUND=1' \
    'IDENTITY_AFTER_BOUND=1' \
    '--lab-identity-evidence' \
    'itlwm-tahoe-post-plti-trace-runtime/v4' \
    'local-unpublished-iwn-lab-candidate' \
    'itlwm-tahoe-iwn-direct-sae-runtime/v2' \
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
    'expected[2] = int(expected[2])' \
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
    'read_candidate_receipt || {',
    'capture_identity before || fail_phase candidate-identity-before',
    'remote_trace_client_exists || fail_phase trace-client-preflight',
    '"$POST_PLTI_RUNNER" --trace-tool "$TRACE_TOOL"',
    '--lab-identity-evidence "$OUT_DIR/identity-before.json"',
    'read_generic_attestation || fail_phase delegated-runner-attestation',
    'capture_direct_report direct-sae-report-read-1',
    'capture_direct_report direct-sae-report-read-2',
    'capture_identity after || fail_phase candidate-identity-after',
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

# The direct runner must derive the guest executable digest solely from the
# receipt v2 it validates locally.  A caller-controlled digest or a release
# identity file would sever the exact-candidate/trace-interpreter binding.
forbidden_direct_inputs = (
    'IDENTITY_EVIDENCE=',
    'RELEASE_TAG=',
    '--identity-evidence) IDENTITY_EVIDENCE="$2"',
    '--trace-client-sha256) TRACE_CLIENT_SHA256="$2"',
)
for token in forbidden_direct_inputs:
    if token in text:
        raise SystemExit(f'FAIL: direct-SAE runner retains forbidden legacy/free input: {token}')
receipt_reader = text.find('read_candidate_receipt() {')
identity_capture = text.find('capture_identity() {')
generic_reader = text.find('read_generic_attestation() {')
if min(receipt_reader, identity_capture, generic_reader) < 0:
    raise SystemExit('FAIL: receipt, loaded identity, or generic attestation reader missing')
receipt_end = text.find('\n}\n\ncapture_identity()', receipt_reader)
identity_end = text.find('\n}\n\nextract_token()', identity_capture)
generic_end = text.find('\n}\n\ncapture_direct_report()', generic_reader)
if min(receipt_end, identity_end, generic_end) < 0:
    raise SystemExit('FAIL: direct runtime reader body is unterminated')
receipt_text = text[receipt_reader:receipt_end]
identity_text = text[identity_capture:identity_end]
generic_text = text[generic_reader:generic_end]
for token in (
    'load_direct_runtime_candidate_receipt',
    'trace_client_sha256',
    'valid_trace_client_sha256 "$TRACE_CLIENT_SHA256"',
):
    if token not in receipt_text:
        raise SystemExit(f'FAIL: direct receipt reader lacks {token}')
for token in (
    'itlwm-tahoe-iwn-lab-loaded-identity/v1',
    'expected_local_lab_candidate',
    'candidate_kext_bound',
    'all(value is True for value in checks.values())',
    'ready_for_exact_local_lab_candidate_runtime_experiment',
    'IDENTITY_BEFORE_BOUND=1',
    'IDENTITY_AFTER_BOUND=1',
):
    if token not in identity_text:
        raise SystemExit(f'FAIL: direct loaded-identity capture lacks {token}')
for token in (
    'itlwm-tahoe-post-plti-trace-runtime/v4',
    'local-unpublished-iwn-lab-candidate',
    'trace_client_receipt_binding_precondition',
    'trace_client_sha256',
    'candidate source identity path count',
    'expected[2] = int(expected[2])',
):
    if token not in generic_text:
        raise SystemExit(f'FAIL: direct generic v4 binding lacks {token}')
if 'release_tag' in generic_text:
    raise SystemExit('FAIL: direct generic v4 reader leaks release identity semantics')
trace_digest_assignment = text.find('TRACE_CLIENT_SHA256="${fields[11]}"', receipt_reader)
cli_parse = text.find('while [ "$#" -gt 0 ]; do')
ssh_setup = text.find('SSH=(')
if min(trace_digest_assignment, cli_parse, ssh_setup) < 0:
    raise SystemExit('FAIL: direct receipt digest binding/order is missing')
if not cli_parse < trace_digest_assignment < ssh_setup:
    raise SystemExit('FAIL: trace-client digest must be receipt-derived before guest contact')

after_capture = text.find('capture_identity after || fail_phase candidate-identity-after')
postflight = text.find('remote_trace_client_exists || fail_phase trace-client-postflight')
if after_capture < 0 or postflight < 0 or after_capture > postflight:
    raise SystemExit('FAIL: loaded candidate is not rebound before postflight completion')

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
start = text.find('candidate = {', text.find('write_safe_attestation() {'))
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
for required in (
    '"schema": "itlwm-tahoe-iwn-direct-sae-runtime/v2"',
    '"kind": "local-unpublished-iwn-lab-candidate"',
    '"identity_before_bound"',
    '"identity_after_bound"',
    '"trace_client_pre_bound"',
    '"trace_client_post_bound"',
):
    if required not in attestation:
        raise SystemExit(f'FAIL: direct-SAE attestation lacks receipt-bound candidate field: {required}')
for forbidden in ('"release_tag"', 'release_publication_model', 'identity_evidence_precondition'):
    if forbidden in attestation:
        raise SystemExit(f'FAIL: direct-SAE attestation retains release/legacy field: {forbidden}')
print('PASS: IWN direct-SAE runtime runner static safety contract')
PY
