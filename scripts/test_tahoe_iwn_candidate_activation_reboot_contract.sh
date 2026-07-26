#!/usr/bin/env bash
# Contract for the one-way strict-pinned IWN candidate activation/reboot bridge.
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)"
BRIDGE="$ROOT/scripts/run_tahoe_iwn_candidate_activation_reboot.py"
WRAPPER="$ROOT/scripts/run_tahoe_iwn_candidate_activation_reboot.sh"

fail() {
    printf 'FAIL: Tahoe IWN activation/reboot bridge contract: %s\n' "$*" >&2
    exit 1
}

[ -x "$BRIDGE" ] || fail 'Python bridge is not executable'
[ -x "$WRAPPER" ] || fail 'shell bridge entry point is not executable'
head -n 1 "$BRIDGE" | grep -Fqx '#!/usr/bin/python3 -Es' ||
    fail 'Python bridge direct entry point is not environment-isolated'
python3 -m py_compile "$BRIDGE"
python3 "$BRIDGE" --self-test
"$BRIDGE" --self-test
bash -n "$WRAPPER"
bash "$WRAPPER" --help >/dev/null

python3 - "$BRIDGE" <<'PY'
import ast
from pathlib import Path
import sys

text = Path(sys.argv[1]).read_text(encoding="utf-8")
root = Path(sys.argv[1]).parent.parent


def fail(message: str) -> None:
    raise SystemExit("FAIL: Tahoe IWN activation/reboot bridge contract: " + message)


def require(needle: str, label: str) -> None:
    if needle not in text:
        fail("missing " + label + ": " + needle)


def forbid(needle: str, label: str) -> None:
    if needle in text:
        fail("forbidden " + label + ": " + needle)


def ordered(label: str, *needles: str) -> None:
    cursor = 0
    for needle in needles:
        position = text.find(needle, cursor)
        if position < 0:
            fail(label + " misses ordered token: " + needle)
        cursor = position + len(needle)


def ordered_in(subject: str, label: str, *needles: str) -> None:
    cursor = 0
    for needle in needles:
        position = subject.find(needle, cursor)
        if position < 0:
            fail(label + " misses ordered token: " + needle)
        cursor = position + len(needle)


for needle in (
    'SCHEMA_VERSION = "itlwm-tahoe-iwn-candidate-activation-reboot/v1"',
    'parser.add_argument("--activate-and-reboot", action="store_true")',
    '--activate-and-reboot is required for the guest-only destructive bridge',
    'snapshot_private_json(candidate_input, "candidate receipt")',
    'snapshot_private_json(stage_input, "private stage report")',
    'object_pairs_hook=reject_duplicate_keys',
    'parse_constant=reject_nonfinite_json_constant',
    'getattr(os, "O_NOFOLLOW", None)',
    'canonical_direct_candidate(candidate_snapshot.document)',
    'validate_stage_report(\n            stage_snapshot.document, candidate, candidate, receipt_digest)',
    'candidate.get("source_commit") != self.head',
    'candidate.get("source_identity_sha256") != identity',
    'candidate.get("source_identity_paths_count") != count',
    'SOURCE_IDENTITY_DOMAIN_V2',
    'SOURCE_IDENTITY_PATHS_V2',
    'GIT_CONFIG_NOSYSTEM',
    'GIT_CONFIG_GLOBAL',
    'GIT_OPTIONAL_LOCKS',
    '"-c", "core.excludesFile=/dev/null", "-C", str(root), *arguments]',
    'PREFLIGHT_RELATIVE = "scripts/tahoe_auxkc_admission_preflight.sh"',
    'ACTIVATION_RELATIVE = "scripts/tahoe_auxkc_activate_release.sh"',
    'ACTIVATION_PREFIX = "/private/var/tmp/aiam-iwn-activation-"',
    'activation_prefix = "/private/var/tmp/aiam-iwn-activation-"',
    'for parent in ("/private", "/private/tmp", "/private/var", "/private/var/tmp")',
    'TRUSTED_SOURCE_RELATIVES = (SELF_RELATIVE, PREFLIGHT_RELATIVE, ACTIVATION_RELATIVE)',
    'StrictHostKeyChecking=yes',
    'GlobalKnownHostsFile=/dev/null',
    'UpdateHostKeys=no',
    'UserKnownHostsFile=',
    'host_key_fingerprint() != PINNED_QEMU_HOST_KEY_SHA256',
    'stdin=subprocess.DEVNULL',
    'env=LOCAL_ENV',
    'close_fds=True',
    '[SSH_KEYGEN, "-lf", str(self.known_hosts), "-E", "sha256"]',
    'SSH, "-F", "/dev/null", "-T"',
    'import shlex',
    'remote_command = " ".join(shlex.quote(argument) for argument in args)',
    '"\\x00" in argument',
    'strict transport lost an empty remote argv element',
    'tempfile.mkdtemp(',
    'dir="/tmp"',
    'stat.S_IMODE(directory.st_mode) != 0o700',
    'FROZEN_CANDIDATE_VERIFIED',
    'validate_no_follow_tree(frozen_dir)',
    'scrub_untrusted_metadata(frozen_dir)',
    'harden_tree(frozen_dir, 0o700)',
    '["/usr/bin/ditto", "--norsrc", "--noacl", "--noextattr", "--noqtn", candidate_dir, frozen_dir]',
    'self.frozen_candidate',
    'strict_receipt_candidate(receipt) != expected_candidate_value',
    'AUXKC_PREFLIGHT_VERIFIED',
    'CANONICAL_BASELINE_CAPTURED',
    'CANONICAL_BASELINE_CURRENT',
    'preflight_airport, preflight_auxkc, preflight_companion = verify_preflight_summary()',
    'companion != preflight_companion',
    'ACTIVATION_REMOTE_VERIFIED',
    'ACTIVATION_ROLLBACK_VERIFIED',
    'self.private_candidate',
    'self.preflight_dir + "/AirportItlwm.kext"',
    '["/usr/bin/sudo", "-n", "/sbin/shutdown", "-r", "now"]',
    '["/usr/bin/sudo", "-n", "/bin/test", "-f", self.work + "/reboot-requested"]',
    'if dispatched.returncode == 0:',
    'elif dispatched.returncode == 255:',
    'self.reboot_dispatch_acknowledged = True',
    'self.reboot_dispatch_uncertain = True',
    'activation_completion_confirmed',
    'reboot_dispatch_uncertain',
    'class ActivationDisconnectTransport:',
    'class RebootDisconnectTransport:',
    'SSH-255 reboot witness failed',
    'canonical_baseline_current_immediately_before_activation',
    'guest_only_dispatch_acknowledged',
    'guest_only_dispatch_uncertain',
    'strict_transport_down_observed',
    'boot_session_changed',
    'ready_for_loaded_identity_capture',
    '"candidate_loaded_claimed": False',
    '"runtime_experiment_performed": False',
    '"raw_guest_output_retained": False',
    '"raw_identity_or_path_retained": False',
    '"qemu_control_used": False',
    '"association_tested": False',
    '"credential_collected": False',
):
    require(needle, "bridge safety token")

ordered(
    "candidate activation phase order",
    "remote.freeze_stage()",
    "source.assert_unchanged()",
    "remote.preflight()",
    "remote.baseline()",
    "remote.baseline_current()",
    "facts.activation_attempted = True",
    "remote.activate()",
    "facts.activation_ready = True",
    "remote.reboot_and_wait()",
)

forbidden = (
    ("ssh-keyscan", "host-key discovery"),
    ("StrictHostKeyChecking=no", "host-key bypass"),
    ("UserKnownHostsFile=/dev/null", "pinned-host-key bypass"),
    ("scp ", "unbound artifact copy"),
    ("kextload", "direct kext load"),
    ("kextunload", "direct kext unload"),
    ("kmutil load", "direct kext load"),
    ("kmutil unload", "direct kext unload"),
    ("networksetup", "network configuration mutation"),
    ("ipconfig set", "address mutation"),
    ("route add", "route mutation"),
    ("route delete", "route mutation"),
    ("qemu-system", "QEMU control"),
    ("qemu-img", "overlay manipulation"),
    ("/private/tmp/aiam-iwn-activation-", "boot-volatile activation root"),
    ("/usr/bin/test", "nonfunctional Tahoe marker probe"),
    ("return subprocess.run([*self.base, *args], **options)", "lossy SSH command flattening"),
    ("shell=True", "shell command expansion"),
)
for needle, label in forbidden:
    forbid(needle, label)

if text.count('["/usr/bin/sudo", "-n", "/sbin/shutdown", "-r", "now"]') != 1:
    fail("guest reboot command is not singular")
if "capture_tahoe_iwn_lab_loaded_identity.py" in text:
    fail("bridge must stop before separate loaded-identity capture")
if "load_direct_runtime_candidate_receipt" in text:
    fail("bridge must not import a mutable receipt parser before its source guard")
if "sha256_file(candidate" in text or "sha256_file(stage" in text:
    fail("bridge must not parse and hash input through separate pathname reads")
if '["/usr/bin/ditto", candidate_dir, frozen_dir]' in text:
    fail("bridge must not preserve staged ACLs or extended attributes into the root snapshot")
if '["/usr/bin/ditto", "--noacl", "--noextattr", candidate_dir, frozen_dir]' in text:
    fail("bridge root snapshot must strip resource forks and quarantine metadata too")
freeze_start = text.find('elif mode == "freeze":')
freeze_end = text.find('elif mode == "preflight":', freeze_start)
if freeze_start < 0 or freeze_end < 0:
    fail("remote frozen-snapshot verifier is missing")
ordered_in(text[freeze_start:freeze_end], "frozen snapshot validation before metadata cleanup",
           '["/usr/bin/ditto", "--norsrc", "--noacl", "--noextattr", "--noqtn", candidate_dir, frozen_dir]',
           'validate_no_follow_tree(frozen_dir)',
           'scrub_untrusted_metadata(frozen_dir)',
           'harden_tree(frozen_dir, 0o700)')
baseline_start = text.find('elif mode == "baseline":')
baseline_end = text.find('elif mode == "baseline-current":', baseline_start)
if baseline_start < 0 or baseline_end < 0:
    fail("remote canonical-baseline verifier is missing")
ordered_in(text[baseline_start:baseline_end], "preflight-bound baseline capture",
           'preflight_airport, preflight_auxkc, preflight_companion = verify_preflight_summary()',
           'airport, companion = member_rows()',
           'current_airport = digest(',
           'current_auxkc = digest(',
           'companion != preflight_companion',
           'document = {')
preflight_start = text.find("    def preflight(self) -> None:")
preflight_end = text.find("    def baseline(self)", preflight_start)
if preflight_start < 0 or preflight_end < 0:
    fail("bridge preflight method is missing")
if "self.stage.extracted_kext" in text[preflight_start:preflight_end]:
    fail("preflight must consume only the root-owned frozen candidate")
activation_start = text.find("    def activate(self) -> str:")
activation_end = text.find("    def _boot_token", activation_start)
if activation_start < 0 or activation_end < 0:
    fail("activation method is missing")
ordered_in(text[activation_start:activation_end], "activation completion uncertainty",
           'self.activation_completion_confirmed = False',
           'if result.returncode == 255:',
           'raise BridgeFailure("activation")',
           'self.activation_completion_confirmed = True')
reboot_start = text.find("    def reboot_and_wait(self)")
reboot_end = text.find("\ndef run_phases", reboot_start)
if reboot_start < 0 or reboot_end < 0:
    fail("reboot method is missing")
ordered_in(text[reboot_start:reboot_end], "SSH-255 reboot witness boundary",
           'if dispatched.returncode == 0:',
           'self.reboot_dispatch_acknowledged = True',
           'elif dispatched.returncode == 255:',
           'self.reboot_dispatch_uncertain = True',
           'if not down:',
           'if after == before:',
           'return True, True, True')
phase_start = text.find("def run_phases")
phase_end = text.find("\ndef report", phase_start)
if phase_start < 0 or phase_end < 0:
    fail("phase runner is missing")
ordered_in(text[phase_start:phase_end], "rollback only after known helper completion",
           'facts.activation_completion_confirmed = bool(',
           'if facts.activation_completion_confirmed:',
           'facts.activation_rollback_verified = remote.rollback_verified()')


def literals(path: Path) -> dict[str, object]:
    tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
    values: dict[str, object] = {}
    for node in tree.body:
        if isinstance(node, ast.Assign):
            for target in node.targets:
                if isinstance(target, ast.Name):
                    try:
                        values[target.id] = ast.literal_eval(node.value)
                    except ValueError:
                        pass
    return values


bridge_values = literals(Path(sys.argv[1]))
pin_values = literals(root / "scripts" / "capture_tahoe_iwn_lab_loaded_identity.py")
identity_values = literals(root / "scripts" / "tahoe_source_identity.py")
for name in (
    "PINNED_QEMU_GUEST", "PINNED_QEMU_PORT", "PINNED_QEMU_BUILD",
    "PINNED_QEMU_HOST_KEY", "PINNED_QEMU_HOST_KEY_SHA256",
):
    if bridge_values.get(name) != pin_values.get(name):
        fail("bridge pin literal drifted from loaded-identity authority: " + name)
if tuple(bridge_values.get("SOURCE_IDENTITY_PATHS_V2", ())) != tuple(identity_values.get("SOURCE_PATHS_V2", ())):
    fail("bridge v2 source-path literal drifted from source-identity authority")
if bridge_values.get("SOURCE_IDENTITY_DOMAIN_V2") != identity_values.get("IDENTITY_DOMAIN_V2"):
    fail("bridge v2 source-domain literal drifted from source-identity authority")

print("PASS: Tahoe IWN candidate activation/reboot bridge contract")
PY
