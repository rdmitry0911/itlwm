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

## Loaded IWM whole image regression

Production `5d4cb4dc56dad38d27e7b9a7f107ca67bdc468aa` is committed and
pushed. The clean isolated guest checkout builds with all 1088 imports
resolved against the running 25C56 BootKC. Private AuxKC admission and
transactional activation pass; all four companions and rollback material
remain under
`/private/var/tmp/aiam-iwn-activation-iwm9260-iwxstop5d4cb4dc-20261009`.
Activation `activation-20261009T200234Z` installs a byte-equal bundle.

- Source identity: `fe4207595c53`.
- Loaded UUID: `A1927158-09E9-3A6B-941C-555066AD2304`.
- Mach-O SHA256: `c078d8fccb3772a7584e89879cb535def0b4ab2d01feac0b65d2319728012774`.
- Boot: `DFD9C8C1-A00A-4330-B62B-F09F8271B231`.

The first S3 test was interrupted by an executor sandbox failure. Its trace
is empty and is not treated as measurement. On 2026-10-10, the exact private
monitor independently confirmed `paused (suspended)` and serial retained
`ACPI SLEEP`. `system_wakeup` resumed that same guest without a QEMU restart,
reinstall or rebuild. Same boot, loaded UUID/hash, exact build/installed
equality and en2/default-route management are independently verified after
wake. Power history shows maintenance wake/sleep cycles during the
interruption, not one continuous overnight sleep. Its final sleep interval
is 248 seconds and WakeTime is 1.244 seconds; the history records 11 total
sleep/wakes for that boot, not 11 complete Wi-Fi qualification passes.

Four subsequent native Off/On controls pass their bounded refusal checks.
Every readback remains Off and en2/default10.0.6.2 remains available. The new
bounded FBT trace observes actual lower disable, stop ownership, drain with
self-counts 0/1, device erase, stop end generation 11 and successful Off
return. That stop takes about 664 ms. Exact inner POWER probes report the
four real On refusals `0xe00002d8` in 1.891, 1.827, 1.903 and 1.892 ms.
The native process exit zero is not radio success. No init-owner or ready
success entry occurs on the RFKILL-blocked IWM; none of these observations
executes the new IWX hardware branch. The trace exits normally and no dtrace
process remains at the package check.

Logs under the current laboratory evidence root include
`iwx-stop-{build-macos,activation,loaded}.log`,
`iwx-stop-resume-{wake-monitor,wake-poll,loaded,native-controls}.log`,
`iwx-stop-resumed-regression-trace.log`, `iwx-stop-pm-history.log` and
`iwx-stop-package-macos.log`. Earlier empty trace and failed observation
logs are retained separately.

## Separate LAB release artifact

Release `v2.4.0-alpha` has additional asset `627281202`,
`AirportItlwm-Tahoe-IwmIwx-EarlyOff-5d4cb4dc.kext.zip`, 15,713,418 bytes,
SHA256 `26245baee84fa7ef071aa0dadb5454ee504fb8417f99dd289fe3da6765832a4f`.
The archive is the exact installed unsigned Debug bundle; its extracted
Mach-O matches the loaded image without a packaging rebuild. The label and
notes retain IWM RFKILL, missing IWX hardware and missing on-air qualification.

The first upload began before SCP finished and was rejected with a content
length error. No asset existed under the new name afterward. The completed
archive was then size/hash-verified before upload; an independent API read
verifies its exact digest/size and all seven preceding assets unchanged.
A following independent API read verifies the newly prepended notes and all
previous notes byte-for-byte as the suffix. The default archive is unchanged.

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
