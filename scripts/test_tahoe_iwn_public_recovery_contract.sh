#!/usr/bin/env bash
# Static and model contract for the bounded public recovery supervisor.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)"
runner="$root/scripts/run_tahoe_iwn_public_recovery.py"
entrypoint="$root/scripts/run_tahoe_iwn_public_recovery.sh"
stage="$root/scripts/prepare_tahoe_iwn_public_recovery_stage.sh"
broker_source="$root/AirportItlwmLabPublicRecovery/airport_itlwm_lab_credential_broker.c"
helper_source="$root/AirportItlwmLabPublicRecovery/airport_itlwm_lab_public_recovery.m"

bash -n "$entrypoint"
python3 -m py_compile "$runner"
python3 "$runner" --self-test
"$entrypoint" --self-test

python3 - "$runner" "$entrypoint" "$stage" "$broker_source" "$helper_source" <<'PY'
from pathlib import Path
import contextlib
import io
import importlib.util
import subprocess
import sys
import tempfile


runner_path = Path(sys.argv[1])
entrypoint = Path(sys.argv[2]).read_text()
stage_path = Path(sys.argv[3])
broker_source = Path(sys.argv[4]).read_text()
helper_source = Path(sys.argv[5]).read_text()
source = runner_path.read_text()


def fail(message: str) -> None:
    raise SystemExit(f"public recovery supervisor contract: {message}")


def require(token: str, label: str) -> None:
    if token not in source:
        fail(f"missing {label}: {token}")


def forbid(token: str, label: str) -> None:
    if token in source:
        fail(f"unexpected {label}: {token}")


def body(marker: str, label: str) -> str:
    start = source.find(marker)
    if start < 0:
        fail(f"missing {label}")
    end = len(source)
    for prefix in ("\ndef ", "\nclass ", "\n@dataclass", "\n    def "):
        candidate = source.find(prefix, start + len(marker))
        if candidate >= 0:
            end = min(end, candidate)
    return source[start:end]


for token in (
    "RUNTIME_SCHEMA = \"itlwm-tahoe-iwn-public-recovery-runtime/v1\"",
    "STAGE_SCHEMA = \"itlwm-tahoe-iwn-public-recovery-stage-attestation/v2\"",
    "require_fifo_stdin", "stat.S_ISFIFO(metadata.st_mode)", "os.dup(descriptor)",
    "socket.SOCK_SEQPACKET", "socket.SCM_RIGHTS", "b\"HOST_FED\"",
    "b\"STARTED\"", "b\"ARMED\"", "b\"RELEASED\"", "b\"ABORTED\"",
    "LEASE_SECONDS = 300", "MINIMUM_INITIAL_LEASE_REMAINING_SECONDS = 285",
    "MINIMUM_RENEWED_LEASE_REMAINING_SECONDS = 285",
    "MINIMUM_WITHDRAW_LEASE_REMAINING_SECONDS", "parse_hash_only_status",
    "LABAP_BSS_STATUS schema=tahoe-labap-bss-status/v1", "next_expected(\"initial-ready\"",
    "next_expected(\"withdraw-armed\"", "next_expected(\"recovered\"",
    "StrictHostKeyChecking=yes", "PINNED_QEMU_HOST_KEY_SHA256",
    "verify_staged_helper", "load_public_recovery_receipt",
    "load_direct_runtime_candidate_receipt", "bind_loaded_identity", "bind_stage_report",
    "capture_loaded_identity", "guest_probe_script", "fresh_loaded_identity_bound",
    "bind_current_candidate_source", "BROKER_SOURCE_RELATIVE", "BROKER_SOURCE_COPY_NAME",
    "write_private_bytes", "BROKER_BUILD_FLAGS",
    "BROKER_COMPILER", "BROKER_POST_START_CAP_SECONDS = 270",
    "HOST_CREDENTIAL_READY_TIMEOUT_SECONDS = 90",
    "HOST_SETUP_STARTED_TIMEOUT_SECONDS = 60", "HOST_SETUP_STARTED",
    "SETUP_STARTED_TO_START_BUDGET_SECONDS = 220",
    "HOST_ACTIVATION_TIMEOUT_SECONDS = 185", "HOST_FED_TO_START_BUDGET_SECONDS = 280",
    "HOST_FED_TIMEOUT_SECONDS = 60", "BROKER_STARTED_TO_ARM_CAP_SECONDS = 145",
    "BROKER_ARMED_TO_RELEASE_CAP_SECONDS = 110", "ARMED_NATIVE_ACK_HANDOFF_MARGIN_SECONDS = 20",
    "HELPER_RECOVERY_TIMEOUT_SECONDS = 125", "HELPER_GRACEFUL_CLEANUP_TIMEOUT_SECONDS",
    "HOST_CREDENTIAL_READY", "wait_host_credential_ready", "wait_host_setup_started", "ActivationOutput",
    "_set_timeout_until", "_receive_until",
    "deadline = time.monotonic() + BROKER_START_CONTROL_TIMEOUT_SECONDS",
    "deadline = time.monotonic() + BROKER_CONTROL_TIMEOUT_SECONDS",
    "--renew-for-withdraw", "LEASE_RENEWED_FOR_WITHDRAW",
    "_retire_after_watchdog_handoff", "ROLLBACK_RESTORE_TIMEOUT_SECONDS = 180",
    "WATCHDOG_RETIRE_PROOF_TIMEOUT_SECONDS", "--recovery-owner",
    "LABAP_BSS_RECOVERY_OWNER=WATCHDOG", "LABAP_BSS_RECOVERY_OWNER=NONE",
    "_recovery_owner", "_discard_proven_unarmed_state_dir",
    "os.execve(helper, [helper], os.environ)", "dir_fd=stage_fd", "O_NOFOLLOW",
    "quote_remote_shell_word", "guest_stage_path",
    "root_owned_nonwritable_guest_stage", "metadata.st_uid != 0",
    "stat.S_IMODE(stage_metadata.st_mode) != 0o555",
    "--withdraw", "--rollback", "--retire", "require_clean_eof",
):
    require(token, "bounded runtime bridge")

for token in (
    "sys.stdin.read", "sys.stdin.buffer", "shell=True", "networksetup", "dtrace",
    "Popen(\"", "subprocess.run(\"", "DEFAULT_BROKER", "config.broker", "--broker",
    "sys.path.insert", "communicate(", "os.execve(helper_fd",
    "os.execve not in os.supports_fd",
):
    forbid(token, "secret/network/shell escape")

if "exec /usr/bin/python3 -I" not in entrypoint or "run_tahoe_iwn_public_recovery.py" not in entrypoint:
    fail("thin shell entrypoint no longer delegates directly to Python")
for forbidden in ("ssh ", "scp ", "networksetup", "dtrace"):
    if forbidden in entrypoint:
        fail(f"entrypoint gained unrelated operation: {forbidden}")

sequence = body("def _run_helper_sequence", "helper sequence")
ordered = (
    "self.broker.start(self.host_target, guest_write)",
    "next_expected(\"initial-ready\"",
    "self.broker.arm()",
    "next_expected(\"withdraw-armed\"",
    "self._renew_for_withdraw_once()",
    "self._withdraw_once()",
    "self.broker.release()",
    "next_expected(\"recovered\"",
)
cursor = 0
for token in ordered:
    position = sequence.find(token, cursor)
    if position < 0:
        fail(f"positive protocol ordering missing: {token}")
    cursor = position + len(token)
withdraw = body("def _withdraw_once", "withdraw gate")
if "not self.state.helper_withdraw_armed" not in withdraw or \
        "not self.state.host_lease_renewed_after_helper_arm" not in withdraw:
    fail("withdraw is not gated by helper withdraw-armed proof")
activation = body("def _activate_and_start_broker", "credential-ready activation")
ready = activation.find("wait_host_credential_ready(self.activation_output)")
fifo = activation.find("credential_fd = require_fifo_stdin()")
broker_start = activation.find("BrokerSession(self.broker_binary, credential_fd, host_write)")
host_fed = activation.find("self.broker.wait_host_fed()")
host_fed_deadline = activation.find("self._host_fed_deadline = time.monotonic()")
setup_wait = activation.find("wait_host_setup_started(", host_fed)
setup_deadline = activation.find("self._setup_started_deadline = (", setup_wait)
active_wait = activation.find("active_output = wait_host_activation(", setup_deadline)
active_parse = activation.find("parse_multiband_result(active_output, ACTIVE_RE")
if min(ready, fifo, broker_start, host_fed, host_fed_deadline, setup_wait, setup_deadline,
       active_wait, active_parse) < 0 or not (
        ready < fifo < broker_start < host_fed < host_fed_deadline < setup_wait <
        setup_deadline < active_wait < active_parse):
    fail("READY/FIFO/broker/HOST_FED/SETUP_STARTED/ACTIVE order is unsafe")
if "communicate(" in activation:
    fail("activation stream parser regressed to communicate")
if activation.count("self._discard_exact_empty_state_dir(\"host-activation-cleanup\")") != 2:
    fail("fresh state directory is not retired on pipe/Popen startup failure")
activation_wait = body("def wait_host_activation", "incremental activation result parser")
for token in ("output.next_line", "output.require_clean_eof", "process.wait(timeout=remaining)"):
    if token not in activation_wait:
        fail(f"activation result is not bounded/incremental: {token}")
if "communicate(" in activation_wait:
    fail("activation result parser retains an unbounded communicate path")
if "MINIMUM_RENEWED_LEASE_REMAINING_SECONDS" not in sequence or \
        "self.state.host_renewed_lease_fresh = True" not in sequence:
    fail("post-renew status does not require a fresh lease receipt")
armed = sequence.find("self.broker.arm()")
armed_deadline = sequence.find("self._withdraw_control_deadline = (", armed)
withdraw_armed = sequence.find("next_expected(\"withdraw-armed\"", armed)
if min(armed, armed_deadline, withdraw_armed) < 0 or not armed < armed_deadline < withdraw_armed:
    fail("local ARMED control deadline does not begin with native ARM acknowledgement")
recovered = sequence.find("next_expected(\"recovered\"")
rollback_after_recovered = sequence.find("self._verified_rollback_and_retire()", recovered)
helper_eof = sequence.find("self.helper_output.require_clean_eof", recovered)
if min(recovered, rollback_after_recovered, helper_eof) < 0 or not recovered < rollback_after_recovered < helper_eof:
    fail("verified rollback is delayed behind helper EOF after recovered")
cleanup = body("def cleanup_after_failure", "failure cleanup")
for token in ("with mask_cleanup_signals()", "self.broker.abort()", "self._close_helper_after_abort()",
              "self._stop_activation()", "self._verified_rollback_and_retire()"):
    if token not in cleanup:
        fail(f"failure cleanup missing safe action: {token}")
if cleanup.find("self._stop_activation()") > cleanup.find("self._close_helper_after_abort()") or \
        cleanup.find("self._verified_rollback_and_retire()") > cleanup.find("self._close_helper_after_abort()"):
    fail("host rollback is delayed behind helper cleanup")
retire = body("def _verified_rollback_and_retire", "rollback and retire")
rollback_position = retire.find("LABAP_BSS_SWITCH=ORIGINAL_RESTORED")
direct_retire_position = retire.rfind("if not self._try_retire(PROCESS_CLEANUP_TIMEOUT_SECONDS):")
retire_probe = body("def _try_retire", "verified retirement probe")
if (rollback_position < 0 or direct_retire_position < 0 or
        rollback_position > direct_retire_position or
        "LABAP_BSS_SWITCH=RETIRED" not in retire_probe):
    fail("retire is not strictly after verified rollback")
if "host_watchdog_rollback_verified" not in retire or "LABAP_BSS_SWITCH=RETIRED" not in retire_probe:
    fail("watchdog-completed rollback cannot be safely retired")
first_retire = retire.find("if self._try_retire(PROCESS_CLEANUP_TIMEOUT_SECONDS):")
owner_query = retire.find("recovery_owner = self._recovery_owner()")
none_owner = retire.find('recovery_owner == b"LABAP_BSS_RECOVERY_OWNER=NONE')
watchdog_owner = retire.find('recovery_owner != b"LABAP_BSS_RECOVERY_OWNER=WATCHDOG')
ambiguous_race = retire.find("self._retire_after_completed_watchdog_window()")
if min(first_retire, owner_query, none_owner, watchdog_owner, ambiguous_race) < 0 or \
        not (first_retire < owner_query < none_owner < watchdog_owner):
    fail("failed rollback lacks retire/owner handoff ordering")
if not owner_query < ambiguous_race:
    fail("ambiguous owner does not receive a bounded completed-watchdog retire bridge")
completed_race = body("def _retire_after_completed_watchdog_window", "completed watchdog race bridge")
for token in ("COMPLETED_WATCHDOG_RETIRE_RACE_TIMEOUT_SECONDS", "self._try_retire(timeout)",
              "WATCHDOG_RETIRE_POLL_SECONDS"):
    if token not in completed_race:
        fail(f"completed watchdog race bridge is not bounded: {token}")
discard = body("def _discard_exact_empty_state_dir", "exact empty-state cleanup")
for token in ("metadata.st_uid != os.getuid()", "stat.S_IMODE(metadata.st_mode) != 0o700",
              "state_dir.rmdir()"):
    if token not in discard:
        fail(f"empty state cleanup is not ownership/emptiness bounded: {token}")

execute = body("def execute", "execution sequence")
if execute.find("self._capture_fresh_loaded_identity()") > execute.find("self._activate_and_start_broker()"):
    fail("fresh loaded-identity capture is not immediately before host activation")
launch = body("def start_helper", "sealed helper launch")
for token in ("REMOTE_EXEC_HELPER", "quote_remote_shell_word", '"-I", "-c"'):
    if token not in launch:
        fail(f"helper launch lacks sealed execution fence: {token}")
graceful_helper = body("def _finish_helper_gracefully", "graceful helper cleanup")
for token in ("drain_until_eof(HELPER_GRACEFUL_CLEANUP_TIMEOUT_SECONDS)",
              "process.terminate()", "process.kill()"):
    if token not in graceful_helper:
        fail(f"helper cleanup is not bounded and drain-first: {token}")

report = body("def report_document", "aggregate report")
for forbidden in ("sha256", "ssid", "bssid", "raw_host_output", "raw_guest_output"):
    if forbidden in report and forbidden not in ("raw_host_output", "raw_guest_output"):
        fail(f"aggregate report carries identity/hash field: {forbidden}")
for token in ("raw_host_output_retained\": False", "raw_guest_output_retained\": False",
              "credential_written_to_report\": False", "target_hashes_written_to_report\": False"):
    if token not in report:
        fail(f"aggregate report missing non-retention assertion: {token}")

if stage_path.exists():
    stage = stage_path.read_text()
    for token in (
        "itlwm-tahoe-iwn-public-recovery-stage-attestation/v2",
        '"candidate_receipt_sha256"', '"public_recovery_receipt_sha256"',
        '"guest_dir_token"', '"helper"', '"macho_uuid"',
    ):
        if token not in stage:
            fail(f"stage schema interface changed unexpectedly: {token}")

spec = importlib.util.spec_from_file_location("public_recovery_runner", runner_path)
if spec is None or spec.loader is None:
    fail("cannot import runner for model checks")
module = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = module
spec.loader.exec_module(module)

# Cross-layer public timing tuple.  The post-credential SETUP_STARTED
# acknowledgement rebases ACTIVE/status/START; the native HOST_FED retention
# cap conservatively covers the preceding 60-second read/preparation window.
assert module.HOST_CREDENTIAL_READY_TIMEOUT_SECONDS == 90
assert module.HOST_SETUP_STARTED_TIMEOUT_SECONDS == 60
assert module.SETUP_STARTED_TO_START_BUDGET_SECONDS == 220
assert module.HOST_ACTIVATION_TIMEOUT_SECONDS == 185
assert module.HOST_FED_TIMEOUT_SECONDS == 60
assert module.HOST_FED_TO_START_BUDGET_SECONDS == 280
assert module.BROKER_HOST_FED_CAP_SECONDS == 290
assert module.MINIMUM_INITIAL_LEASE_REMAINING_SECONDS == 285
assert module.MINIMUM_RENEWED_LEASE_REMAINING_SECONDS == 285
assert module.WATCHDOG_RETIRE_PROOF_TIMEOUT_SECONDS == 500
assert module.COMPLETED_WATCHDOG_RETIRE_RACE_TIMEOUT_SECONDS == 25
assert module.ARMED_PHASE_SAFETY_SECONDS == 5
assert module.HOST_SETUP_STARTED_TIMEOUT_SECONDS + \
    module.SETUP_STARTED_TO_START_BUDGET_SECONDS == module.HOST_FED_TO_START_BUDGET_SECONDS
assert 185 + module.HOST_STATUS_TIMEOUT_SECONDS + \
    module.BROKER_START_CONTROL_TIMEOUT_SECONDS + module.BROKER_START_SAFETY_SECONDS <= 220
assert 280 + module.BROKER_HOST_FED_HANDOFF_MARGIN_SECONDS <= 290
assert 60 >= 15 + 15 + 15 + 15
assert module.ARM_TO_WITHDRAW_TIMEOUT_SECONDS + module.POST_ARM_RELEASE_PATH_SECONDS + \
    module.ARMED_PHASE_SAFETY_SECONDS < module.HELPER_WITHDRAW_CONTROL_TIMEOUT_SECONDS
assert module.HELPER_WITHDRAW_CONTROL_TIMEOUT_SECONDS + \
    module.ARMED_NATIVE_ACK_HANDOFF_MARGIN_SECONDS <= module.BROKER_ARMED_TO_RELEASE_CAP_SECONDS
for token in (
    "kSwitcherCredentialReadBoundMilliseconds = 45000u",
    "kSwitcherSetupBoundMilliseconds = 180000u",
    "kPostCredentialSetupMarginMilliseconds = 15000u",
    "kHostFedToSetupStartedBoundMilliseconds =",
    "kSetupStartedToStartDeadlineMilliseconds =",
    "kHostFedControllerHandoffMarginMilliseconds = 10000u",
    "kHostFedToStartDeadlineMilliseconds =",
):
    if token not in broker_source:
        fail(f"native broker timing bridge changed: {token}")
assert 45 + 15 + 180 + 5 + 10 + 20 + 5 + 10 == 290


class DeadlineClock:
    def __init__(self) -> None:
        self.now = 0.0

    def monotonic(self) -> float:
        return self.now


class BrokerSocketFixture:
    def __init__(self, clock: DeadlineClock, status: bytes, send_cost: float) -> None:
        self.clock = clock
        self.status = status
        self.send_cost = send_cost
        self.timeouts: list[float] = []
        self.sent: list[bytes] = []
        self.receive_calls = 0

    def settimeout(self, value: float) -> None:
        assert value > 0
        self.timeouts.append(value)

    def send(self, payload: bytes) -> int:
        self.sent.append(payload)
        self.clock.now += self.send_cost
        return len(payload)

    def sendmsg(self, vectors: list[bytes], _ancillary: object) -> int:
        payload = b"".join(vectors)
        self.sent.append(payload)
        self.clock.now += self.send_cost
        return len(payload)

    def recvmsg(self, _capacity: int, _flags: int) -> tuple[bytes, list[object], int, object]:
        self.receive_calls += 1
        return self.status, [], 0, None


def broker_session_with(socket_fixture: BrokerSocketFixture) -> module.BrokerSession:
    session = object.__new__(module.BrokerSession)
    session._parent = socket_fixture
    return session


# The send and receive halves share one deadline.  A successful five-second
# send leaves only fifteen seconds for the ACK; a send that consumes all of
# the phase budget cannot begin a receive wait at all.
clock = DeadlineClock()
fixed_socket = BrokerSocketFixture(clock, b"ARMED", 5.0)
original_monotonic = module.time.monotonic
module.time.monotonic = clock.monotonic
try:
    broker_session_with(fixed_socket)._send_fixed(b"ARM", b"ARMED")
finally:
    module.time.monotonic = original_monotonic
assert fixed_socket.timeouts == [20.0, 15.0]

clock = DeadlineClock()
start_socket = BrokerSocketFixture(clock, b"STARTED", 6.0)
module.time.monotonic = clock.monotonic
try:
    broker_session_with(start_socket).start(
        module.HostTarget("a" * 64, "b" * 64, 300), 9
    )
finally:
    module.time.monotonic = original_monotonic
assert start_socket.timeouts == [20.0, 14.0]

clock = DeadlineClock()
stalled_socket = BrokerSocketFixture(clock, b"RELEASED", 20.0)
module.time.monotonic = clock.monotonic
try:
    try:
        broker_session_with(stalled_socket)._send_fixed(b"RELEASE", b"RELEASED")
    except module.RunnerError:
        pass
    else:
        raise AssertionError("control send consumed a second receive window")
finally:
    module.time.monotonic = original_monotonic
assert stalled_socket.receive_calls == 0


def activation_process(payload: bytes, exit_code: int = 0) -> subprocess.Popen[bytes]:
    program = (
        "import sys; "
        f"sys.stdout.buffer.write({payload!r}); sys.stdout.flush(); "
        f"raise SystemExit({exit_code})"
    )
    return subprocess.Popen(
        [sys.executable, "-c", program], stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
    )


def close_activation(process: subprocess.Popen[bytes], output: module.ActivationOutput) -> None:
    try:
        process.wait(timeout=1)
    finally:
        output.close()


setup_started = module.HOST_SETUP_STARTED
active = b"LABAP_BSS_SWITCH=ACTIVE external_bss_count=2 external_band_count=2\n"

# Deterministic local pipe/Popen grammar: the only accepted transcript is the
# nonsecret READY acknowledgement, exact SETUP_STARTED origin, then one exact
# ACTIVE line and EOF/0.
process = activation_process(module.HOST_CREDENTIAL_READY + setup_started + active)
output = module.ActivationOutput(process)
try:
    module.wait_host_credential_ready(output)
    module.wait_host_setup_started(output, 1)
    assert module.wait_host_activation(process, output, 1) == active
    module.parse_multiband_result(active, module.ACTIVE_RE, "activation-model")
finally:
    close_activation(process, output)

for payload, exit_code, label in (
    (active, 0, "ACTIVE-before-READY"),
    (module.HOST_CREDENTIAL_READY, 0, "EOF-before-SETUP_STARTED"),
    (module.HOST_CREDENTIAL_READY + active, 0, "ACTIVE-before-SETUP_STARTED"),
    (module.HOST_CREDENTIAL_READY + setup_started + active + b"extra\n", 0, "extra-after-ACTIVE"),
    (module.HOST_CREDENTIAL_READY + setup_started + active, 1, "nonzero-switcher"),
):
    process = activation_process(payload, exit_code)
    output = module.ActivationOutput(process)
    try:
        try:
            module.wait_host_credential_ready(output)
            module.wait_host_setup_started(output, 1)
            result = module.wait_host_activation(process, output, 1)
            module.parse_multiband_result(result, module.ACTIVE_RE, "activation-model")
        except module.RunnerError:
            pass
        else:
            raise AssertionError(f"{label} activation transcript was accepted")
    finally:
        close_activation(process, output)

# A second READY is not a SETUP_STARTED result, even if it is followed by
# clean EOF.
process = activation_process(module.HOST_CREDENTIAL_READY + module.HOST_CREDENTIAL_READY)
output = module.ActivationOutput(process)
try:
    module.wait_host_credential_ready(output)
    try:
        module.wait_host_setup_started(output, 1)
    except module.RunnerError:
        pass
    else:
        raise AssertionError("duplicate READY setup transcript was accepted")
finally:
    close_activation(process, output)


def recovery_supervisor() -> module.Supervisor:
    state = module.RuntimeState(activation_attempted=True)
    config = module.Config(
        Path("/tmp/candidate.json"), Path("/tmp/public.json"), Path("/tmp/loaded.json"),
        Path("/tmp/stage.json"), Path("/tmp/output.json"),
    )
    supervisor = module.Supervisor(config, state)
    supervisor.state_dir = Path("/tmp/aiam-labap-bss-switch.recovery-contract")
    return supervisor


def failed_rollback(*_args: object, **_kwargs: object) -> subprocess.CompletedProcess[bytes]:
    return subprocess.CompletedProcess(["switcher"], 1, b"")


def assert_verified_watchdog_recovery(supervisor: module.Supervisor) -> None:
    assert supervisor.state.host_rollback_attempted
    assert supervisor.state.host_retire_attempted
    assert supervisor.state.host_rollback_verified
    assert supervisor.state.host_watchdog_rollback_verified
    assert supervisor.state.host_retired_verified
    assert supervisor.state_dir is None
    assert not supervisor.state.host_withdrawn_verified


# No-host recovery interleavings.  The first foreground rollback has failed;
# each fixture proves that only an exact retirement can complete a watchdog
# route, while an empty pre-marker route never enters the 500-second handoff.
original_run_switcher = module.run_switcher
module.run_switcher = failed_rollback
try:
    ambiguous = recovery_supervisor()
    ambiguous._try_retire = lambda _timeout: False
    ambiguous._recovery_owner = lambda: (_ for _ in ()).throw(module.RunnerError("ambiguous"))
    ambiguous._retire_after_completed_watchdog_window = lambda: True
    ambiguous._retire_after_watchdog_handoff = lambda: (_ for _ in ()).throw(AssertionError("long poll"))
    ambiguous._verified_rollback_and_retire()
    assert_verified_watchdog_recovery(ambiguous)

    none_retired = recovery_supervisor()
    retire_results = iter((False, True))
    none_retired._try_retire = lambda _timeout: next(retire_results)
    none_retired._recovery_owner = lambda: b"LABAP_BSS_RECOVERY_OWNER=NONE\n"
    none_retired._discard_proven_unarmed_state_dir = lambda: (_ for _ in ()).throw(AssertionError("discard"))
    none_retired._retire_after_completed_watchdog_window = lambda: (_ for _ in ()).throw(AssertionError("race"))
    none_retired._retire_after_watchdog_handoff = lambda: (_ for _ in ()).throw(AssertionError("long poll"))
    none_retired._verified_rollback_and_retire()
    assert_verified_watchdog_recovery(none_retired)

    watchdog = recovery_supervisor()
    watchdog._try_retire = lambda _timeout: False
    watchdog._recovery_owner = lambda: b"LABAP_BSS_RECOVERY_OWNER=WATCHDOG\n"
    watchdog._retire_after_completed_watchdog_window = lambda: (_ for _ in ()).throw(AssertionError("race"))
    watchdog._retire_after_watchdog_handoff = lambda: True
    watchdog._verified_rollback_and_retire()
    assert_verified_watchdog_recovery(watchdog)

    unarmed = recovery_supervisor()
    unarmed._try_retire = lambda _timeout: False
    unarmed._recovery_owner = lambda: b"LABAP_BSS_RECOVERY_OWNER=NONE\n"
    unarmed._discard_proven_unarmed_state_dir = lambda: setattr(unarmed, "state_dir", None)
    unarmed._retire_after_completed_watchdog_window = lambda: (_ for _ in ()).throw(AssertionError("race"))
    unarmed._retire_after_watchdog_handoff = lambda: (_ for _ in ()).throw(AssertionError("long poll"))
    unarmed._verified_rollback_and_retire()
    assert unarmed.state.host_rollback_attempted
    assert unarmed.state.host_retire_attempted
    assert not unarmed.state.host_rollback_verified
    assert not unarmed.state.host_watchdog_rollback_verified
    assert not unarmed.state.host_retired_verified
    assert unarmed.state_dir is None
finally:
    module.run_switcher = original_run_switcher

valid_status = (
    b"LABAP_BSS_STATUS schema=tahoe-labap-bss-status/v1 active=1 "
    b"target_ssid_sha256=" + b"a" * 64 + b" target_bssid_sha256=" + b"b" * 64 +
    b" lease_seconds=300 lease_remaining_seconds=285\n"
)
target = module.parse_hash_only_status(valid_status, "model")
assert target.remaining_seconds == 285
try:
    module.parse_hash_only_status(valid_status.replace(b"lease_seconds=300", b"lease_seconds=299"), "model")
except module.RunnerError:
    pass
else:
    raise AssertionError("non-300 lease was accepted")

for name, flags in (
    ("initial-ready", (0, 0, 0, 0, 0, 0)),
    ("withdraw-armed", (1, 1, 0, 0, 0, 0)),
    ("recovered", (1, 1, 1, 1, 1, 1)),
):
    module.HelperOutput._validate_line(module.make_helper_line(name, flags), name)
try:
    module.HelperOutput._validate_line(
        module.make_helper_line(
            "public-association-failed", (0, 0, 0, 0, 0, 0)
        ),
        "initial-ready",
    )
except module.RunnerError as error:
    assert error.phase == "helper-result-public-association-failed"
else:
    raise AssertionError("fixed helper failure result was not reduced categorically")
insufficient_alternates = module.make_helper_line(
    "initial-or-alternate-target-unavailable", (0, 0, 0, 0, 0, 0)
).replace(b"alternate_bss_count=2", b"alternate_bss_count=1").replace(
    b"alternate_ready=1", b"alternate_ready=0"
)
try:
    module.HelperOutput._validate_line(insufficient_alternates, "initial-ready")
except module.RunnerError as error:
    assert error.phase == "helper-result-alternate-bss-insufficient"
else:
    raise AssertionError("alternate BSS deficit was not reduced categorically")
missing_initial = module.make_helper_line(
    "initial-or-alternate-target-unavailable", (0, 0, 0, 0, 0, 0)
).replace(b"matching_records=1", b"matching_records=0")
try:
    module.HelperOutput._validate_line(missing_initial, "initial-ready")
except module.RunnerError as error:
    assert error.phase == (
        "helper-result-initial-bss-unavailable-same-ess-visible"
    )
else:
    raise AssertionError("missing initial BSS with visible ESS was not classified")
try:
    module.HelperOutput._validate_line(
        module.make_helper_line("recovered", (1, 1, 1, 1, 0, 1)), "recovered"
    )
except module.RunnerError:
    pass
else:
    raise AssertionError("invalid recovered vector was accepted")

native_failure_states = set(__import__("re").findall(
    r'result = "([a-z0-9-]+)"', helper_source
))
native_failure_states.discard("recovered")
assert native_failure_states == set(module.HELPER_FAILURE_STATES)

candidate = {
    "source_commit": "a" * 40,
    "source_identity_sha256": "b" * 64,
    "source_identity_paths_count": 1,
    "profile": "iwn-software-pmf-lab",
    "staged_kext_repo_path": "Build/Debug/Tahoe-IwnSoftwarePmfLab/AirportItlwm.kext",
    "archive_sha256": "c" * 64,
    "info_plist_sha256": "d" * 64,
    "bundle_tree_sha256": "e" * 64,
    "binary_sha256": "f" * 64,
    "macho_uuid": "01234567-89AB-CDEF-0123-456789ABCDEF",
    "bundle_id": "com.zxystd.AirportItlwm",
    "trace_client_sha256": "1" * 64,
}
raw_candidate = {
    **candidate,
    "bundle_version": "1",
    "short_version": "1",
}
with tempfile.TemporaryDirectory() as temporary:
    receipt = Path(temporary) / "candidate.json"
    receipt.write_bytes(b"fixture\n")
    original_loader = module.load_direct_runtime_candidate_receipt
    module.load_direct_runtime_candidate_receipt = lambda _path: raw_candidate
    try:
        canonical, _digest = module.canonical_candidate(receipt)
    finally:
        module.load_direct_runtime_candidate_receipt = original_loader
assert canonical == candidate

guest = {
    "os_build": module.PINNED_QEMU_BUILD,
    "installed_bundle_present": True,
    "installed_bundle_id": candidate["bundle_id"],
    "installed_bundle_version": "1",
    "installed_short_version": "1",
    "installed_info_plist_sha256": candidate["info_plist_sha256"],
    "installed_binary_sha256": candidate["binary_sha256"],
    "installed_macho_uuid": candidate["macho_uuid"],
    "installed_macho_uuid_unambiguous": True,
    "kext_reported_loaded": True,
    "loaded_uuids_observed": [candidate["macho_uuid"]],
    "loaded_driver_line_count": 1,
    "guest_observation_parsed": True,
}
loaded_document = {
    "schema_version": module.LOADED_IDENTITY_SCHEMA,
    "capture_kind": module.LOADED_IDENTITY_CAPTURE_KIND,
    "captured_at_utc": "2000-01-01T00:00:00+00:00",
    "capture_mode": "read-only-pinned-qemu-guest",
    "candidate_receipt_schema": module.CANDIDATE_RECEIPT_SCHEMA,
    "candidate_receipt_kind": module.CANDIDATE_RECEIPT_KIND,
    "expected_local_lab_candidate": dict(candidate),
    "guest_observation": guest,
    "guest_double_read": {
        "probe_count": 2,
        "inter_probe_delay_seconds": module.LOADED_IDENTITY_DOUBLE_READ_DELAY_SECONDS,
        **{key: True for key in module.LOADED_IDENTITY_DOUBLE_READ_KEYS
           if key not in {"probe_count", "inter_probe_delay_seconds"}},
    },
    "candidate_binding": {
        "checks": {key: True for key in module.LOADED_IDENTITY_CHECK_KEYS},
        "candidate_kext_bound": True,
        "failure_reasons": [],
    },
    "command_result": {
        "read_only_probe_count": 2,
        "inter_probe_delay_seconds": module.LOADED_IDENTITY_DOUBLE_READ_DELAY_SECONDS,
        "first_probe": {"ssh_returncode": 0, "duration_seconds": 0.0, "stderr_line_count": 0},
        "second_probe": {"ssh_returncode": 0, "duration_seconds": 0.0, "stderr_line_count": 0},
        "guest_host_key_fingerprint": module.PINNED_QEMU_HOST_KEY_SHA256,
        "guest_command": "two read-only installed-and-loaded-kext identity queries",
        "raw_guest_stdout_retained": False,
        "raw_guest_stderr_retained": False,
    },
    "non_claims": {key: False for key in module.LOADED_IDENTITY_NON_CLAIM_KEYS},
    "verdict": {
        "ready_for_exact_local_lab_candidate_runtime_experiment": True,
        "candidate_runtime_test_performed": False,
    },
}
module.bind_loaded_identity(loaded_document, candidate)
invalid_loaded_document = __import__("copy").deepcopy(loaded_document)
invalid_loaded_document["command_result"]["unexpected"] = False
try:
    module.bind_loaded_identity(invalid_loaded_document, candidate)
except module.RunnerError:
    pass
else:
    raise AssertionError("loaded-identity extra nested field was accepted")

assert module.quote_remote_shell_word("x'y") == "'x'\"'\"'y'"
bound_artifacts = module.BoundArtifacts(
    "a" * 64,
    "01234567-89AB-CDEF-0123-456789ABCDEF",
    "b" * 64,
    "stage-token",
)
assert bound_artifacts.guest_stage_path == (
    module.GUEST_STAGE_PREFIX + "stage-token"
)
assert module.HELPER_NAME not in bound_artifacts.guest_stage_path
assert "guest_helper_path" not in source
with contextlib.redirect_stderr(io.StringIO()):
    try:
        module.parse_arguments(["--broker", "/tmp/untrusted"])
    except SystemExit:
        pass
    else:
        raise AssertionError("untrusted broker override was accepted")

document = module.report_document(module.RuntimeState())
rendered = __import__("json").dumps(document, sort_keys=True)
assert "target_ssid_sha256" not in rendered
assert "target_bssid_sha256" not in rendered
assert '"sha256"' not in rendered
print("PASS: Tahoe IWN public recovery supervisor contract")
PY
