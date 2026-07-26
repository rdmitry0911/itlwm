#!/usr/bin/env bash
# Cross-layer timing/model contract for the public recovery credential bridge.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)"
runner="$root/scripts/run_tahoe_iwn_public_recovery.py"
switcher="$root/scripts/tahoe_labap_bss_switcher.sh"
broker="$root/AirportItlwmLabPublicRecovery/airport_itlwm_lab_credential_broker.c"
helper="$root/AirportItlwmLabPublicRecovery/airport_itlwm_lab_public_recovery.m"

bash -n "$switcher"
python3 -m py_compile "$runner"

python3 - "$runner" "$switcher" "$broker" "$helper" <<'PY'
from pathlib import Path
import importlib.util
import sys


runner_path = Path(sys.argv[1])
switcher = Path(sys.argv[2]).read_text()
broker = Path(sys.argv[3]).read_text()
helper = Path(sys.argv[4]).read_text()


def fail(message: str) -> None:
    raise SystemExit(f"public recovery timing contract: {message}")


def require(text: str, token: str, layer: str) -> None:
    if token not in text:
        fail(f"{layer} missing {token}")


spec = importlib.util.spec_from_file_location("public_recovery_timing_runner", runner_path)
if spec is None or spec.loader is None:
    fail("cannot import runner")
runner = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = runner
spec.loader.exec_module(runner)

# Before the secret exists, the host may spend longer on passive RF admission.
# Once it emits READY, every secret-bearing stage has a finite, cross-checked
# budget.  The exact SETUP_STARTED acknowledgement follows credential
# consumption, durable rollback ownership, and the v4 setup-deadline origin.
for token in (
    "CREDENTIAL_READ_TIMEOUT_SECONDS=45",
    "SETUP_DEADLINE_SECONDS=180",
    "LABAP_BSS_CREDENTIAL_READY=1",
    "LABAP_BSS_SETUP_STARTED=1",
    "IFS= read -r -s -t \"$CREDENTIAL_READ_TIMEOUT_SECONDS\" passphrase",
    "write_state armed",
    "write_marker",
    "start_watchdog",
):
    require(switcher, token, "switcher")
activate = switcher[switcher.find("do_activate() {"):switcher.find("do_withdraw() {")]
ready = activate.find("LABAP_BSS_CREDENTIAL_READY=1")
read = activate.find('read -r -s -t "$CREDENTIAL_READ_TIMEOUT_SECONDS"')
setup_deadline = activate.find("setup_deadline=$((setup_now + SETUP_DEADLINE_SECONDS))")
state = activate.find("write_state armed")
marker = activate.find("write_marker")
watchdog = activate.find("record_activation_phase watchdog-ready")
setup_started = activate.find("LABAP_BSS_SETUP_STARTED=1")
live_stop = activate.find("record_activation_phase pre-live-stop")
if not 0 <= ready < read < setup_deadline < state < marker < watchdog < setup_started < live_stop:
    fail("READY/SETUP_STARTED activation origins are unsafe")
assert 45 >= 15 + 15 + 15
assert runner.HOST_CREDENTIAL_READY_TIMEOUT_SECONDS == 90
assert runner.HOST_SETUP_STARTED_TIMEOUT_SECONDS == 60
assert runner.SETUP_STARTED_TO_START_BUDGET_SECONDS == 220

# HOST_FED begins the native outer cap.  The public runner waits at most sixty
# seconds for the post-credential SETUP_STARTED acknowledgement, then rebases
# its 185-second ACTIVE/status/START path from that exact line.
for token in (
    "kCredentialDeadlineMilliseconds = 15000u",
    "kPipeWriteDeadlineMilliseconds = 15000u",
    "kSwitcherCredentialReadBoundMilliseconds = 45000u",
    "kPostCredentialSetupMarginMilliseconds = 15000u",
    "kHostFedToSetupStartedBoundMilliseconds =",
    "kSwitcherSetupBoundMilliseconds = 180000u",
    "kPostSetupSchedulerMarginMilliseconds = 5000u",
    "kControllerStatusDeadlineMilliseconds = 10000u",
    "kControllerStartDeadlineMilliseconds = 20000u",
    "kControllerStartSafetyMilliseconds = 5000u",
    "kSetupStartedToStartDeadlineMilliseconds =",
    "kHostFedControllerHandoffMarginMilliseconds = 10000u",
    "kHostFedToStartDeadlineMilliseconds =",
    "send_status(control_fd, host_fed_status",
    "deadline_after(kHostFedToStartDeadlineMilliseconds,",
):
    require(broker, token, "broker")
assert 45 + 15 == 60
assert 180 + 5 + 10 + 20 + 5 == 220
assert 60 + 220 + 10 == 290
assert runner.BROKER_HOST_FED_CAP_SECONDS == 290
assert runner.HOST_FED_TO_START_BUDGET_SECONDS == 280
assert runner.HOST_SETUP_STARTED_TIMEOUT_SECONDS + \
    runner.SETUP_STARTED_TO_START_BUDGET_SECONDS == runner.HOST_FED_TO_START_BUDGET_SECONDS
assert runner.HOST_FED_TO_START_BUDGET_SECONDS + runner.BROKER_HOST_FED_HANDOFF_MARGIN_SECONDS <= \
    runner.BROKER_HOST_FED_CAP_SECONDS
assert runner.HOST_ACTIVATION_TIMEOUT_SECONDS == 185
assert runner.HOST_ACTIVATION_TIMEOUT_SECONDS + runner.HOST_STATUS_TIMEOUT_SECONDS + \
    runner.BROKER_START_CONTROL_TIMEOUT_SECONDS + runner.BROKER_START_SAFETY_SECONDS <= \
    runner.SETUP_STARTED_TO_START_BUDGET_SECONDS
assert runner.HOST_FED_TIMEOUT_SECONDS == 60
assert runner.HOST_FED_TIMEOUT_SECONDS >= 15 + 15 + 15 + 15
if broker.find("send_status(control_fd, host_fed_status") > \
        broker.find("deadline_after(kHostFedToStartDeadlineMilliseconds,"):
    fail("broker starts HOST_FED deadline before acknowledgement")

# The helper/runner clock starts after receiving ARMED; the native clock starts
# before the receive.  Its 110-second cap therefore contains a fixed 20-second
# handoff fence around the unchanged 90-second helper/controller budget.
for token in (
    "kStartedToArmDeadlineMilliseconds = 145000u",
    "kArmedToReleaseDeadlineMilliseconds = 110000u",
    "kPostStartSessionDeadlineMilliseconds = 270000u",
):
    require(broker, token, "broker")
require(helper, "kControlInputDeadlineMilliseconds = 90000u", "public helper")
assert runner.BROKER_STARTED_TO_ARM_CAP_SECONDS == 145
assert runner.BROKER_ARMED_TO_RELEASE_CAP_SECONDS == 110
assert runner.BROKER_POST_START_CAP_SECONDS == 270
assert runner.START_TO_ARM_TIMEOUT_SECONDS + runner.BROKER_CONTROL_TIMEOUT_SECONDS < 145
assert runner.ARM_TO_WITHDRAW_TIMEOUT_SECONDS + runner.POST_ARM_RELEASE_PATH_SECONDS + \
    runner.ARMED_PHASE_SAFETY_SECONDS < runner.HELPER_WITHDRAW_CONTROL_TIMEOUT_SECONDS
assert runner.HELPER_WITHDRAW_CONTROL_TIMEOUT_SECONDS == 90
assert runner.HELPER_WITHDRAW_CONTROL_TIMEOUT_SECONDS + \
    runner.ARMED_NATIVE_ACK_HANDOFF_MARGIN_SECONDS <= runner.BROKER_ARMED_TO_RELEASE_CAP_SECONDS
assert 145 + 110 < 270

sequence = runner_path.read_text()
armed = sequence.find("self.broker.arm()")
deadline = sequence.find("self._withdraw_control_deadline = (", armed)
withdraw_armed = sequence.find('next_expected("withdraw-armed"', armed)
renew = sequence.find("self._renew_for_withdraw_once()", withdraw_armed)
status = sequence.find('self._read_host_status("host-status-before-withdraw")', renew)
withdraw = sequence.find("self._withdraw_once()", status)
release = sequence.find("self.broker.release()", withdraw)
if min(armed, deadline, withdraw_armed, renew, status, withdraw, release) < 0 or not (
        armed < deadline < withdraw_armed < renew < status < withdraw < release):
    fail("ARMED control/renew/status/withdraw/release ordering changed")

host_fed = sequence.find("self.broker.wait_host_fed()")
outer_deadline = sequence.find("self._host_fed_deadline = time.monotonic()", host_fed)
setup_wait = sequence.find("wait_host_setup_started(", host_fed)
setup_deadline = sequence.find("self._setup_started_deadline = (", setup_wait)
active_wait = sequence.find("active_output = wait_host_activation(", setup_deadline)
if min(host_fed, outer_deadline, setup_wait, setup_deadline, active_wait) < 0 or not (
        host_fed < outer_deadline < setup_wait < setup_deadline < active_wait):
    fail("HOST_FED/SETUP_STARTED/ACTIVE timing origins changed")

# Both observed v4 leases are deliberately fresh.  The one-shot intermediate
# state makes the old deadline unable to authorize an accidental second renew.
assert runner.MINIMUM_INITIAL_LEASE_REMAINING_SECONDS == 285
assert runner.MINIMUM_RENEWED_LEASE_REMAINING_SECONDS == 285


class LeaseModel:
    def __init__(self) -> None:
        self.state = "labap-active"
        self.deadline = 300

    def renew(self, now: int) -> None:
        if self.state != "labap-active" or now >= self.deadline:
            raise ValueError("renew")
        self.state = "labap-withdraw-armed"
        self.deadline = now + 300

    def statusable(self, now: int) -> bool:
        return self.state in {"labap-active", "labap-withdraw-armed"} and now < self.deadline

    def watchdog_may_restore(self, now: int) -> bool:
        return now >= self.deadline


model = LeaseModel()
model.renew(299)
assert model.statusable(300)
assert not model.watchdog_may_restore(300)
try:
    model.renew(300)
except ValueError:
    pass
else:
    raise AssertionError("one-shot renewal was repeated")

assert runner.WATCHDOG_RETIRE_PROOF_TIMEOUT_SECONDS >= \
    runner.LEASE_SECONDS + runner.ROLLBACK_RESTORE_TIMEOUT_SECONDS
print("PASS: Tahoe IWN public recovery cross-layer timing contract")
PY
