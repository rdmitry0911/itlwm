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
# budget.  The 45-second switcher read covers FIFO+host-pipe bounds plus margin.
for token in (
    "CREDENTIAL_READ_TIMEOUT_SECONDS=45",
    "SETUP_DEADLINE_SECONDS=180",
    "LABAP_BSS_CREDENTIAL_READY=1",
    "IFS= read -r -s -t \"$CREDENTIAL_READ_TIMEOUT_SECONDS\" passphrase",
    "write_state armed",
    "write_marker",
):
    require(switcher, token, "switcher")
activate = switcher[switcher.find("do_activate() {"):switcher.find("do_withdraw() {")]
ready = activate.find("LABAP_BSS_CREDENTIAL_READY=1")
read = activate.find('read -r -s -t "$CREDENTIAL_READ_TIMEOUT_SECONDS"')
state = activate.find("write_state armed")
marker = activate.find("write_marker")
if not 0 <= ready < read < state < marker:
    fail("READY is not before every state/marker mutation")
assert 45 >= 15 + 15 + 15
assert runner.HOST_CREDENTIAL_READY_TIMEOUT_SECONDS == 90

# Host-fed timing begins only after the C broker successfully sends HOST_FED.
for token in (
    "kCredentialDeadlineMilliseconds = 15000u",
    "kPipeWriteDeadlineMilliseconds = 15000u",
    "kSwitcherSetupBoundMilliseconds = 180000u",
    "kPostCredentialSetupMarginMilliseconds = 15000u",
    "kControllerStatusAndStartMarginMilliseconds = 45000u",
    "kHostFedToStartDeadlineMilliseconds =",
    "send_status(control_fd, host_fed_status",
    "deadline_after(kHostFedToStartDeadlineMilliseconds,",
):
    require(broker, token, "broker")
assert 180 + 15 + 45 == 240
assert runner.BROKER_HOST_FED_CAP_SECONDS == 240
assert runner.HOST_FED_TO_START_BUDGET_SECONDS == 235
assert runner.HOST_FED_TO_START_BUDGET_SECONDS + runner.BROKER_START_SAFETY_SECONDS <= \
    runner.BROKER_HOST_FED_CAP_SECONDS
assert runner.HOST_ACTIVATION_TIMEOUT_SECONDS == 195
assert runner.HOST_ACTIVATION_TIMEOUT_SECONDS + runner.HOST_STATUS_TIMEOUT_SECONDS + \
    runner.BROKER_START_CONTROL_TIMEOUT_SECONDS + runner.BROKER_START_SAFETY_SECONDS <= \
    runner.HOST_FED_TO_START_BUDGET_SECONDS
assert runner.HOST_FED_TIMEOUT_SECONDS == 60
assert runner.HOST_FED_TIMEOUT_SECONDS >= 15 + 15 + 15 + 15
if broker.find("send_status(control_fd, host_fed_status") > \
        broker.find("deadline_after(kHostFedToStartDeadlineMilliseconds,"):
    fail("broker starts HOST_FED deadline before acknowledgement")

# The broker's ARMED clock starts with the ARMED acknowledgement; the runner
# starts its corresponding control clock before waiting for withdraw-armed.
for token in (
    "kStartedToArmDeadlineMilliseconds = 145000u",
    "kArmedToReleaseDeadlineMilliseconds = 90000u",
    "kPostStartSessionDeadlineMilliseconds = 270000u",
):
    require(broker, token, "broker")
require(helper, "kControlInputDeadlineMilliseconds = 90000u", "public helper")
assert runner.BROKER_STARTED_TO_ARM_CAP_SECONDS == 145
assert runner.BROKER_ARMED_TO_RELEASE_CAP_SECONDS == 90
assert runner.BROKER_POST_START_CAP_SECONDS == 270
assert runner.START_TO_ARM_TIMEOUT_SECONDS + runner.BROKER_CONTROL_TIMEOUT_SECONDS < 145
assert runner.ARM_TO_WITHDRAW_TIMEOUT_SECONDS + runner.POST_ARM_RELEASE_PATH_SECONDS + \
    runner.ARMED_PHASE_SAFETY_SECONDS < 90
assert runner.HELPER_WITHDRAW_CONTROL_TIMEOUT_SECONDS == 90
assert 145 + 90 < 270

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
