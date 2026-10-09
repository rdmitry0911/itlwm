# IWX repeated radio controls during firmware init and stop

IWX now sends every radio Off through its existing task-gated stop, including
firmware init before `IFF_RUNNING`. A second external Off waits for the first
stop to finish erasing hardware, and enable refuses admission while that stop
owns `SHUTDOWN`. This closes three reproduced lifecycle failures in the lower
driver. The laboratory currently has an IWM 9260, not an attached IWX device;
software qualification and IWM whole-image regression are separate scopes.

## Reproduced failures

The full historical bodies from `e973008e` compile and pass normal init,
missing-ready timeout, hardware failure and monitor controls. They then fail
three intended assertions, each with exit 134:

- Early Off followed by On returns before init finishes. The old init reports
  success with `premature=1 init=0 live=1 stops=0 readyEpoch=84`, although it
  began under request 42. Off skipped stop because `IFF_RUNNING` was not set.
- A second Off returns while the first stop is paused inside hardware erase.
  The old close-failure branch returns without draining the stop owner.
- On during that erase returns success and calls resume and prepare once each,
  despite the live stop owner. The old enable has no shutdown refusal.

The firmware and ready producer are explicit doubles. These are executable
lower-driver race reproductions, not measurements of an on-air IWX failure.

## Lower ownership change

`DVACT_QUIESCE` calls `iwx_stop` unconditionally. The existing stop already
closes task admission, advances generation, aborts q0, wakes synchronous
senders, fences dequeued work, drains init owners and erases hardware before
rearming. None of that can protect an init when the Off caller skips stop.

When another stop already owns the gate, an external Off now drains active,
init and stop references before returning. A caller executing inside init
instead yields its self-reference to the winning stop, avoiding a self-wait.
Enable returns NotReady before changing `IFF_UP`, resuming or preparing the
device while `SHUTDOWN` is set. No firmware capability, RFKILL bypass or forced
ready is introduced.

The reference contract is the powerOff quiesce boundary and separate system
and radio availability carriers in
`docs/reference/CR-480-system-pm-state-word-20260711.md`. Existing Intel gate
ownership supplies the implementation; Broadcom private offsets are not
mapped onto Intel lifetime counters.

## Executable verification

`scripts/test_iwx_radio_init_stop.sh` executes complete production enable,
disable, activate, init and stop bodies; the actual task-gate owner helpers;
the actual q0 start, enter, leave and stop helpers; and actual scan-lease and
ready validators. IOKit primitives, task barriers, firmware, net80211 soft
state and security hooks are explicit doubles. The wrapper uses the real
task-gate enter and leave, not the complete `iwx_init_task` callback.

The paused hardware-init double enters the real q0 sender lifetime and
installs an outstanding submitted slot and response allocation. Actual q0
stop aborts that slot, clears its cookie, wakes the sender, drains its ref and
frees the response. Assertions require zero remaining gate owners and no
hardware/scan/ready resurrection after early Off. A fresh replacement On
then completes under its own request epoch.

Eleven ASan/UBSan controls cover normal, timeout, hardware failure, monitor,
early Off, early Off/On, early Off with primary already down, overlapping Off,
On during stop, and both self-task and self-init-epoch collisions with an
external stop. The separate four-control caller fixture executes complete
disable/activate with a stop spy and does not replace the full lifecycle test.
Both fixtures are part of the Linux payload aggregate. Linux lifecycle,
payload, physical/standard scan, Off/link-down, resident IWM SAE ownership and
unexpected AP reset replay checks pass.

The same eleven lifecycle and four caller controls pass on macOS. Four
historical baseline controls pass there, then all three historical race
controls compile and fail their intended assertion with exit 134. The initial
Darwin fixture compile failure was the unavailable userland `explicit_bzero`;
its explicit primitive double was added before the authoritative rerun.

The q0 static contract had required the init gate to remain closed even after
opening task admission before SCAN. That assertion now requires the actual
generation/init/stop/shutdown fences and forbids a closed-gate requirement in
executable code. The existing runtime-init contract already expresses the
same rule. The larger q0 static script still fails its older raw `task_add`
count; this is not relabelled as a full q0/task-producer pass.

## Qualification limits

Complete q0 command submission, interrupts, firmware and DMA are not executed
by these fixtures. No IWX hardware runtime is claimed while the attached card
is IWM. The IWM 9260 remains hardware RFKILL-blocked, so GUI open/WPA2/WPA3,
saved networks, DHCP/traffic, operational AP and radio recovery after sleep
remain unqualified on that card. Physical host `.22` is untouched.

The next IWM layer is security hook reopening before the first ready consumer,
with real init-generation ownership rather than resurrection during stop.
The existing security-reopen WIP is not part of this change. IWM dequeued
workers/q0 and the remaining IWX enqueue paths also retain separate scope.
