#!/usr/bin/env bash
# Static and fixture gate for the path-free public recovery helper receipt.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
tool="$root/scripts/capture_tahoe_iwn_lab_public_recovery_receipt.py"

fail() {
    printf 'FAIL: public recovery receipt contract: %s\n' "$*" >&2
    exit 1
}

[ -x "$tool" ] || fail 'receipt tool missing or not executable'
python3 -m py_compile "$tool"
python3 "$tool" --self-test

help_output="$(python3 "$tool" --help)"
case "$help_output" in
    *--candidate-receipt*--helper*--gate-token*) ;;
    *) fail 'receipt tool does not expose the typed candidate, helper, and gate inputs' ;;
esac

python3 - "$tool" <<'PY'
from pathlib import Path
import sys


text = Path(sys.argv[1]).read_text(encoding="utf-8")


def require(token: str, label: str) -> None:
    if token not in text:
        raise SystemExit(f"FAIL: public recovery receipt lacks {label}: {token}")


def forbid(token: str, label: str) -> None:
    if token.lower() in text.lower():
        raise SystemExit(f"FAIL: public recovery receipt exposes {label}: {token}")


for token, label in (
    ("itlwm-tahoe-iwn-lab-public-recovery-receipt/v1", "typed receipt schema"),
    ("local-unpublished-iwn-lab-public-recovery-helper", "non-release receipt kind"),
    ("itlwm-tahoe-iwn-lab-candidate-receipt/v2", "typed v2 candidate input"),
    ("direct_runtime_candidate_from_receipt", "typed candidate reader"),
    ("public recovery source worktree is not clean", "clean current source gate"),
    ("source_identity(root, \"HEAD\")", "current Tahoe source identity"),
    ("HELPER_SOURCE_IDENTITY_DOMAIN", "sidecar source identity domain"),
    ("AirportItlwmLabPublicRecovery/airport_itlwm_lab_public_recovery.m", "fixed helper source"),
    ("scripts/build_tahoe_lab_public_recovery.sh", "fixed helper build recipe"),
    ("Build/Debug/Tahoe/airport_itlwm_lab_public_recovery", "fixed helper build output"),
    ("GATE_TOKEN_RE", "safe gate-token grammar"),
    ("candidate_receipt_matches_current_source", "candidate/current-source equality"),
    ("candidate receipt changed during receipt capture", "candidate TOCTOU rejection"),
    ("public recovery helper changed during receipt capture", "helper TOCTOU rejection"),
    ("MACHO_64_LE_MAGIC", "thin Mach-O parser"),
    ("LC_UUID", "Mach-O UUID parser"),
    ("MH_EXECUTE", "executable Mach-O gate"),
    ("CPU_TYPE_X86_64", "x86_64 helper architecture gate"),
    ("CPU_TYPE_ARM64", "arm64 helper architecture gate"),
    ("object_pairs_hook=reject_duplicate_object_keys", "duplicate JSON-key rejection"),
    ("parse_constant=reject_nonfinite_json_constant", "nonfinite JSON rejection"),
    ("os.O_EXCL", "new-output gate"),
    ("O_NOFOLLOW", "no-follow output gate"),
    ("0o600", "private receipt mode"),
    ("validate_public_recovery_receipt_document", "typed receipt validator"),
    ("load_public_recovery_receipt", "typed receipt loader"),
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
    ("passphrase", "credential carrier"),
    ("password", "credential carrier"),
    ("/usr/bin/ssh", "guest transport"),
    ("scp ", "guest copy"),
    ("subprocess.run([\"ssh\"", "path-resolved guest transport"),
    ("networksetup -set", "network mutation"),
):
    forbid(token, label)

capture = text[text.find("def make_receipt("):text.find("def require_digest(")]
if '"candidate_receipt_path"' in capture or '"helper_path"' in capture:
    raise SystemExit("FAIL: public recovery receipt serializes a local input path")
if 'candidate_receipt_sha256' not in capture:
    raise SystemExit("FAIL: public recovery receipt does not bind candidate receipt bytes")
if '"gate_build_dir_token": gate_token' not in capture:
    raise SystemExit("FAIL: public recovery receipt does not bind the safe gate token")
if '"sha256": helper_after["sha256"]' not in capture:
    raise SystemExit("FAIL: public recovery receipt does not bind helper bytes")
if '"macho_uuid": helper_after["macho_uuid"]' not in capture:
    raise SystemExit("FAIL: public recovery receipt does not bind helper Mach-O UUID")
main = text[text.find("def main() -> int:"):]
if 'str(error)' in main:
    raise SystemExit("FAIL: public recovery receipt renders exception details")

validator = text[text.find("def validate_public_recovery_receipt_document("):text.find("def load_public_recovery_receipt(")]
for token in (
    "candidate and source identities differ",
    "require_gate_token(document.get(\"gate_build_dir_token\"))",
    "helper.get(\"repo_output_path\") != HELPER_REPO_PATH",
    "UUID_RE.fullmatch(helper_uuid)",
    "any(validation.get(key) is not True for key in VALIDATION_KEYS)",
):
    if token not in validator:
        raise SystemExit(f"FAIL: public recovery receipt validator lacks {token}")

print("PASS: Tahoe IWN public recovery receipt static contract")
PY
