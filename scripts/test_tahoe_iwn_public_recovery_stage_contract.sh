#!/usr/bin/env bash
# Static and fixture gate for the public-recovery sidecar staging bridge.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
tool="$root/scripts/prepare_tahoe_iwn_public_recovery_stage.sh"

fail() {
    printf 'FAIL: public recovery stage contract: %s\n' "$*" >&2
    exit 1
}

[ -x "$tool" ] || fail 'stage bridge missing or not executable'
bash -n "$tool"
"$tool" --self-test
"$tool" --help >/dev/null 2>&1
bash "$root/scripts/test_tahoe_iwn_lab_public_recovery_receipt_contract.sh"

fixture="$(/usr/bin/mktemp /tmp/aiam-public-recovery-stage-report.XXXXXX)"
cleanup_fixture() {
    [ -f "$fixture" ] && [ ! -L "$fixture" ] && /usr/bin/unlink -- "$fixture"
}
trap cleanup_fixture EXIT HUP INT TERM

/usr/bin/python3 - "$fixture" <<'PY'
import json
import sys

document = {
    "schema": "itlwm-tahoe-iwn-public-recovery-stage-attestation/v1",
    "candidate_receipt_sha256": "a" * 64,
    "public_recovery_receipt_sha256": "b" * 64,
    "gate_build_dir_token": "Gate_9",
    "guest_dir_token": "Guest-7",
    "source": {
        "commit": "c" * 40,
        "identity_sha256": "d" * 64,
        "identity_paths_count": 7,
    },
    "helper": {
        "sha256": "e" * 64,
        "macho_uuid": "01234567-89AB-CDEF-0123-456789ABCDEF",
    },
    "validation": {
        "pinned_guest_host_key": True,
        "pinned_guest_build": True,
        "fresh_restricted_guest_directory": True,
        "helper_hash_matches_local_sidecar_receipt": True,
        "helper_macho_uuid_matches_local_sidecar_receipt": True,
        "guest_rehash_matches_local_bytes": True,
        "guest_macho_uuid_matches_local_bytes": True,
    },
    "non_claims": {
        "helper_invoked": False,
        "target_identity_collected": False,
        "credential_collected": False,
        "remote_output_collected": False,
        "association_tested": False,
        "rebooted": False,
        "runtime_experiment_performed": False,
    },
}
with open(sys.argv[1], "w", encoding="utf-8") as output:
    json.dump(document, output, sort_keys=True)
PY

[ "$("$tool" --validate-stage-report "$fixture")" = 'STAGE_REPORT_VALID' ] || fail 'valid stage report was rejected'

/usr/bin/python3 - "$fixture" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as source:
    document = json.load(source)
document["helper"]["macho_uuid"] = "malformed"
with open(sys.argv[1], "w", encoding="utf-8") as output:
    json.dump(document, output, sort_keys=True)
PY

if "$tool" --validate-stage-report "$fixture" >/dev/null 2>&1; then
    fail 'malformed stage report was accepted'
fi

/usr/bin/python3 - "$tool" <<'PY'
from pathlib import Path
import sys


text = Path(sys.argv[1]).read_text(encoding="utf-8")


def require(token: str, label: str) -> None:
    if token not in text:
        raise SystemExit(f"FAIL: public recovery stage lacks {label}: {token}")


def forbid(token: str, label: str) -> None:
    if token.lower() in text.lower():
        raise SystemExit(f"FAIL: public recovery stage exposes {label}: {token}")


for token, label in (
    ("capture_tahoe_iwn_lab_public_recovery_receipt.py", "sidecar receipt dependency"),
    ("read_typed_candidate_receipt", "typed v2 candidate receipt validation"),
    ("validate_public_recovery_receipt_document", "sidecar receipt validator"),
    ("parse_json_document", "sidecar receipt exact-byte parser"),
    ("helper_source_identity", "committed helper-source identity check"),
    ("macho_uuid", "local Mach-O UUID check"),
    ("GATE_DIR_PREFIX", "safe existing gate prefix"),
    ("GUEST_DIR_PREFIX", "fresh restricted guest prefix"),
    ("safe_token()", "safe token parser"),
    ("parse_gate_build_dir", "gate-token parser"),
    ("parse_guest_dir", "guest-token parser"),
    ("git -C \"$ROOT\" worktree add --detach", "detached clean worktree"),
    ("GIT_CONFIG_KEY_0=core.excludesFile", "private build-artifact exclusion"),
    ("StrictHostKeyChecking=yes", "strict host-key checking"),
    ("GlobalKnownHostsFile=/dev/null", "isolated known-hosts policy"),
    ("UpdateHostKeys=no", "host-key rotation refusal"),
    ("/usr/bin/ssh-keygen", "host-key fingerprint verification"),
    ("PINNED_GUEST_BUILD", "pinned guest build gate"),
    ("/usr/bin/scp", "strict artifact copy transport"),
    ("remote-gate-helper-copy-failed", "strict gate helper copy failure"),
    ("$PINNED_GUEST:$GUEST_DIR/$HELPER_NAME", "canonical remote helper name"),
    ("$PINNED_GUEST:$GUEST_DIR/$PUBLIC_RECEIPT_NAME", "canonical remote receipt name"),
    ("REMOTE_PUBLIC_RECOVERY_STAGE", "aggregate-only guest verification"),
    ("0xFEEDFACF", "remote thin Mach-O verification"),
    ("0x1B", "remote LC_UUID verification"),
    ("helper_bytes != regular", "remote artifact double read"),
    ("os.O_EXCL", "new report output gate"),
    ("O_NOFOLLOW", "no-follow report output gate"),
    ("0o600", "private report mode"),
    ("itlwm-tahoe-iwn-public-recovery-collection/v1", "collection report schema"),
    ("itlwm-tahoe-iwn-public-recovery-stage-attestation/v1", "stage report schema"),
    ("validate_stage_report()", "strict stage-report parser"),
    ("--validate-stage-report", "stage-report validator CLI"),
    ("stat.S_IMODE(metadata.st_mode) != 0o600", "private stage-report parser gate"),
    ("object_pairs_hook=reject_duplicate_object_keys", "duplicate JSON-key rejection"),
    ("parse_constant=reject_nonfinite_json_constant", "nonfinite JSON rejection"),
    ("target_identity_collected\": False", "target-identity non-claim"),
    ("credential_collected\": False", "credential non-claim"),
    ("remote_output_collected\": False", "remote-output non-claim"),
    ("runtime_experiment_performed\": False", "runtime non-claim"),
):
    require(token, label)

for token, label in (
    ("networksetup", "network configuration control"),
    ("CoreWLAN", "wireless framework invocation"),
    ("ssid", "raw service-set identity"),
    ("bssid", "raw BSS identity"),
    ("password", "credential carrier"),
    ("passphrase", "credential carrier"),
    ("StrictHostKeyChecking=no", "permissive host-key mode"),
    ("UserKnownHostsFile=/dev/null", "unpinned known-hosts mode"),
    ("UpdateHostKeys=yes", "host-key update mode"),
    ("ssh-keyscan", "untrusted host-key discovery"),
    ("rm -f", "implicit removal"),
    ("find -delete", "recursive cleanup"),
    ("worktree remove", "forced worktree cleanup"),
    ("/bin/cat", "raw remote binary streaming"),
):
    forbid(token, label)

root_gate = text[text.find("require_clean_committed_source() {"):text.find("prepare_guest_transport() {")]
if "--untracked-files" in root_gate or "status --porcelain" in root_gate:
    raise SystemExit("FAIL: root identity gate rejects unrelated untracked files")
for token in ("git -C \"$ROOT\" diff --quiet", "git -C \"$ROOT\" diff --cached --quiet", "rev-parse --verify HEAD"):
    if token not in root_gate:
        raise SystemExit(f"FAIL: root identity gate lacks tracked-change protection: {token}")

stage = text[text.find("stage_public_recovery() {"):text.find("self_test() {")]
ordered = ("validate_stage_inputs", "require_clean_committed_source", "prepare_guest_transport", "create_remote_stage_directory", "copy_stage_artifacts", "verify_remote_stage", "write_stage_report")
position = -1
for token in ordered:
    next_position = stage.find(token, position + 1)
    if next_position < 0:
        raise SystemExit(f"FAIL: stage flow lacks {token}")
    position = next_position
if "require_clean_head" in stage:
    raise SystemExit("FAIL: stage flow rejects unrelated untracked files through receipt clean-head helper")
if "--dry-run" not in text or "STAGE_DRY_RUN_READY" not in text or "COLLECT_DRY_RUN_READY" not in text:
    raise SystemExit("FAIL: bridge lacks non-transport dry-run surface")

print("PASS: Tahoe IWN public recovery stage static contract")
PY
