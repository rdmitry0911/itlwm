#!/usr/bin/env bash
# Contract for the read-only untagged Tahoe IWN lab loaded-identity gate.
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
CAPTURE="$ROOT/scripts/capture_tahoe_iwn_lab_loaded_identity.py"
RECEIPT="$ROOT/scripts/capture_tahoe_iwn_lab_candidate_receipt.py"

fail() {
    printf 'FAIL: Tahoe IWN lab loaded identity contract: %s\n' "$*" >&2
    exit 1
}

require_literal() {
    local needle="$1" label="$2"
    grep -Fq -- "$needle" "$CAPTURE" || fail "missing $label"
}

forbid_literal() {
    local needle="$1" label="$2"
    ! grep -Fq -- "$needle" "$CAPTURE" || fail "forbidden $label"
}

[ -x "$CAPTURE" ] || fail 'loaded identity capture missing or not executable'
[ -x "$RECEIPT" ] || fail 'typed lab candidate receipt helper missing or not executable'

python3 "$CAPTURE" --self-test

for needle in \
    'itlwm-tahoe-iwn-lab-loaded-identity/v1' \
    'local-unpublished-iwn-lab-loaded-candidate' \
    'itlwm-tahoe-iwn-lab-candidate-receipt/v2' \
    'load_direct_runtime_candidate_receipt' \
    'typed IWN lab v2 receipt helper is unavailable' \
    '--candidate-receipt' \
    'require_regular_file(path, "candidate receipt")' \
    'PINNED_QEMU_GUEST = "devops@127.0.0.1"' \
    'PINNED_QEMU_PORT = 3322' \
    'PINNED_QEMU_BUILD = "25C56"' \
    'PINNED_QEMU_HOST_KEY_SHA256' \
    'DOUBLE_READ_DELAY_SECONDS = 1' \
    'StrictHostKeyChecking=yes' \
    'GlobalKnownHostsFile=/dev/null' \
    '"sudo",' \
    '"-n",' \
    '"/bin/bash",' \
    'reduce_guest_probe' \
    'double_read_facts' \
    'sanitized_identity_facts' \
    'pinned_qemu_guest_double_probe_succeeded' \
    'pinned_qemu_build_matches' \
    'sanitized_installed_loaded_identity_stable' \
    '"first_probe": first_command' \
    '"second_probe": second_command' \
    '"inter_probe_delay_seconds": DOUBLE_READ_DELAY_SECONDS' \
    'installed_info_plist_sha256_matches_candidate' \
    'installed_binary_sha256_matches_candidate' \
    'loaded_uuid_matches_installed' \
    'loaded_uuid_matches_candidate' \
    'candidate_kext_bound' \
    'ready_for_exact_local_lab_candidate_runtime_experiment' \
    'raw_guest_stdout_retained": False' \
    'raw_guest_stderr_retained": False' \
    'candidate_kext_installed_by_capture": False' \
    'candidate_kext_loaded_by_capture": False' \
    'kext_unloaded_by_capture": False' \
    'host_or_guest_rebooted": False' \
    'network_configuration_changed": False' \
    'association_tested": False' \
    'data_transfer_tested": False' \
    'loaded identity output must be outside the source repository'; do
    require_literal "$needle" "required safety token: $needle"
done

# This is an untagged local-lab lane.  It must not grow a release provenance
# dependency or an activation/control surface.
for needle in \
    'capture_tahoe_lab_kext_identity' \
    'create_tahoe_candidate_provenance' \
    '--expected-release-zip' \
    '--candidate-provenance' \
    '--release-tag' \
    'kmutil load' \
    'kextload' \
    'kextutil' \
    'sudo -n kmutil load' \
    'sudo -n kextload' \
    'sudo -n kextutil' \
    'networksetup' \
    'route add' \
    'route delete' \
    'route change' \
    'ipconfig ' \
    'ifconfig ' \
    'shutdown -r' \
    '/sbin/reboot' \
    'scp ' \
    'rsync ' \
    'curl ' \
    'parser.add_argument("--guest"' \
    'parser.add_argument("--port"'; do
    forbid_literal "$needle" "capability: $needle"
done

python3 - "$ROOT/scripts" "$CAPTURE" <<'PY'
import importlib.util
import sys
import tempfile
from pathlib import Path


scripts = Path(sys.argv[1])
capture_path = Path(sys.argv[2])
sys.path.insert(0, str(scripts))
spec = importlib.util.spec_from_file_location("iwn_loaded_identity_contract", capture_path)
if spec is None or spec.loader is None:
    raise SystemExit("FAIL: could not load IWN lab identity module")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

candidate = module.canonical_direct_candidate(module.fixture_candidate())
expected_keys = {
    "source_commit",
    "source_identity_sha256",
    "source_identity_paths_count",
    "profile",
    "staged_kext_repo_path",
    "archive_sha256",
    "info_plist_sha256",
    "bundle_tree_sha256",
    "binary_sha256",
    "macho_uuid",
    "bundle_id",
    "trace_client_sha256",
}
if set(candidate) != expected_keys:
    raise SystemExit("FAIL: canonical local candidate key set changed")

uuid = candidate["macho_uuid"]
valid_output = f"""guest_build={module.PINNED_QEMU_BUILD}
installed_bundle_present=true
installed_bundle_id={candidate["bundle_id"]}
installed_bundle_version=fixture
installed_short_version=fixture
installed_info_plist_sha256={candidate["info_plist_sha256"]}
installed_binary_sha256={candidate["binary_sha256"]}
__INSTALLED_UUID_BEGIN__
UUID: {uuid} (x86_64) AirportItlwm
__INSTALLED_UUID_END__
__LOADED_BEGIN__
123 0x0 0x0 {candidate["bundle_id"]} ({uuid})
__LOADED_END__
"""

with tempfile.TemporaryDirectory(prefix="aiam-iwn-loaded-identity-contract-") as temp:
    receipt = Path(temp) / "candidate-receipt.json"
    receipt.write_text("{}\n", encoding="utf-8")

    def loader(path: Path):
        if path != receipt.resolve(strict=True):
            raise SystemExit("FAIL: capture changed receipt path before typed loader")
        return module.fixture_candidate()

    events = []
    outputs = [valid_output, valid_output]

    def runner(timeout_seconds: int):
        if timeout_seconds != 9:
            raise SystemExit("FAIL: capture changed bounded timeout")
        if not outputs:
            raise SystemExit("FAIL: capture did not stop after two probes")
        events.append("probe")
        return {
            "returncode": 0,
            "stdout": outputs.pop(0),
            "duration_seconds": 0.01,
            "stderr_line_count": 0,
        }

    def sleeper(delay_seconds: float):
        if delay_seconds != module.DOUBLE_READ_DELAY_SECONDS:
            raise SystemExit("FAIL: capture did not use the fixed bounded delay")
        events.append("delay")

    document = module.capture(receipt, 9, loader, runner, sleeper)

if document.get("schema_version") != module.SCHEMA_VERSION:
    raise SystemExit("FAIL: loaded identity schema changed")
if document.get("capture_kind") != module.CAPTURE_KIND:
    raise SystemExit("FAIL: untagged lab capture kind changed")
if document.get("candidate_receipt_schema") != module.RECEIPT_SCHEMA_VERSION:
    raise SystemExit("FAIL: v2 candidate receipt boundary changed")
if document.get("expected_local_lab_candidate") != candidate:
    raise SystemExit("FAIL: expected local candidate was not copied exactly")
if events != ["probe", "delay", "probe"]:
    raise SystemExit("FAIL: two read-only probes were not delay-separated")
binding = document.get("candidate_binding")
if not isinstance(binding, dict) or binding.get("candidate_kext_bound") is not True:
    raise SystemExit("FAIL: matching installed and loaded candidate was not bound")
checks = binding.get("checks")
if not isinstance(checks, dict) or not checks or not all(value is True for value in checks.values()):
    raise SystemExit("FAIL: matching binding checks were not all true")
if binding.get("failure_reasons") != []:
    raise SystemExit("FAIL: matching binding retained false failure reasons")
double_read = document.get("guest_double_read")
if not isinstance(double_read, dict):
    raise SystemExit("FAIL: categorical double-read result is missing")
for key in (
    "both_pinned_qemu_guest_queries_succeeded",
    "both_sanitized_guest_observations_parsed",
    "both_pinned_qemu_builds_match",
    "sanitized_installed_loaded_identity_stable",
):
    if double_read.get(key) is not True:
        raise SystemExit(f"FAIL: matching double-read lacks {key}")
if double_read.get("probe_count") != 2:
    raise SystemExit("FAIL: double-read probe count changed")
if double_read.get("inter_probe_delay_seconds") != module.DOUBLE_READ_DELAY_SECONDS:
    raise SystemExit("FAIL: double-read delay evidence changed")
if document.get("verdict", {}).get(
    "ready_for_exact_local_lab_candidate_runtime_experiment"
) is not True:
    raise SystemExit("FAIL: matching local candidate is not runtime-ready")
command = document.get("command_result", {})
if command.get("raw_guest_stdout_retained") is not False:
    raise SystemExit("FAIL: raw guest stdout retention changed")
if command.get("raw_guest_stderr_retained") is not False:
    raise SystemExit("FAIL: raw guest stderr retention changed")
if any(value is not False for value in document.get("non_claims", {}).values()):
    raise SystemExit("FAIL: non-claim changed")

guest = module.parse_guest_observation(valid_output)
guest["loaded_uuids_observed"] = []
command = {"ssh_returncode": 0, "duration_seconds": 0.01, "stderr_line_count": 0}
failed = module.binding_result(
    candidate, guest, module.double_read_facts(guest, command, guest, command)
)
if failed.get("candidate_kext_bound") is not False:
    raise SystemExit("FAIL: missing loaded UUID was accepted")
if "loaded_uuid_matches_candidate" not in failed.get("failure_reasons", []):
    raise SystemExit("FAIL: missing loaded UUID reason was lost")

wrong_build_output = valid_output.replace(
    f"guest_build={module.PINNED_QEMU_BUILD}", "guest_build=wrong-build"
)
wrong_build = module.parse_guest_observation(wrong_build_output)
wrong_build_result = module.binding_result(
    candidate,
    wrong_build,
    module.double_read_facts(wrong_build, command, wrong_build, command),
)
if wrong_build_result.get("candidate_kext_bound") is not False:
    raise SystemExit("FAIL: wrong pinned QEMU build was accepted")
if "pinned_qemu_build_matches" not in wrong_build_result.get("failure_reasons", []):
    raise SystemExit("FAIL: wrong QEMU build reason was lost")

changed_loaded_output = valid_output.replace(
    f"({uuid})", "(FEDCBA98-7654-3210-FEDC-BA9876543210)"
)
changed_loaded = module.parse_guest_observation(changed_loaded_output)
unstable = module.double_read_facts(guest, command, changed_loaded, command)
if unstable.get("sanitized_installed_loaded_identity_stable") is not False:
    raise SystemExit("FAIL: changed loaded identity was accepted as stable")

print("PASS: Tahoe IWN lab loaded identity contract")
PY
