# BTM request ownership across target admission and retirement

A copied BSS Transition target now carries its local request generation
through the WCL bridge and the IWN, IWM and IWX SAE paths. An old callback
cannot retire or authorize a newer BTM request merely because its SSID and
target BSSID are identical. This correction concerns logical request
ownership; firmware commands and physical descriptor completion retain their
existing owners. The RFKILL blocked laboratory IWM 9260 still cannot qualify
these transitions in the air.

## Reproduced successor failures

Three controls execute the complete production functions from `d80d6fc3` on
both Linux and macOS with ASan and UBSan. Actual arm, confirm, fence submission
and two terminal functions establish the completed leave. A second actual
request then selects the same SSID and target with a different generation.

The old consume clears successor request 2. The RUN admission accepts the old
selection as the successor and advances SAE policy from 42 to 43. The SCAN
admission likewise accepts the old selection. All three controls compile and
fail their intended assertions with exit 134 on both operating systems.

The frozen macOS baseline remains
`/private/var/tmp/btm-consume-audit-d80d6fc3.HGZsbK/source`. The updated baseline
test archive SHA256 is
`5a6434bf28f4c2867f82b858a536270f6c30536caa54286b6ed1b3f6747ba929`.
Kernel scheduling, SAE registration, credential staging and lower callbacks
are explicit fixture boundaries, not observed GUI or on air authentication.

## Exact request identity

Copying the target and generation occurs under the same selected BSS leaf.
Rejected copies clear both outputs. Consume requires that copied nonzero
generation as well as the existing SSID and target checks; malformed inputs
cannot clear a request. Retirement leaves the monotonic request and fence
counters untouched and does not imply physical TX retirement.

Both confirmed SAE admission leaves require the copied BTM generation.
Ordinary WCL retarget uses its existing WCL owner with a zero BTM generation.
An inconsistent BTM flag and generation pair is rejected before admission.
The native WCL credential request is private driver state; no Apple carrier
layout or wire token changes.

The real two descriptor terminal already captures request generation and
source epoch. Node reconnect now passes them into each HAL wrapper. The
wrapper rechecks that source identity, copies the target and generation, and
rejects a different copied generation. The later admission leaf and logical
consume independently validate the captured owner after yielding work.

The saved 25C56 `WCLRoamManager::linkDown` export at `ffffff8002105ae4` clears
logical roam state and invokes policy and timer cleanup. That reference
behavior is not a physical descriptor completion receipt. The existing
[TX completion contract](reference/AppleBCMWLAN_IWM_SAE_TX_COMPLETION_25C56_20260801.md)
keeps those boundaries distinct; the new local generation does not invent an
equivalent firmware ABI.

## Software qualification

The Linux and macOS full payload aggregates and the WNM and direct SAE source
contracts pass. The carrier fixture runs 83 new copy, consume, admission and three HAL
wrapper cases alongside the three repaired successor controls, 60 existing
BTM handoff cases, 145 carrier cases and 30 post target cancellation edges.
The RX and timer fixture retains 92 passing cases. The macOS WIP aggregate
completes with status zero in
`/private/var/tmp/btm-generation-wip-20261010.tYLOlq/source`.

The new cases cover invalid and missing outputs, zero and wrong generations,
duplicate consumption, cancellation and independent association replacement,
pending and completed successors, an atomic target and generation copy across
unlock replacement, unchanged ordinary WCL admission, and replacement at both
unlocks inside each complete production HAL wrapper. The lower staging double
calls the complete production admission leaf; it does not simulate firmware
or successful SAE negotiation.

## Loaded image and next qualification

The currently loaded image is still production `d80d6fc3`, source identity
`946d41bce15d`, UUID `F11831F7-5BCE-3411-AEB5-90E6E5500E50`, boot
`7A93D037-E8BD-4E60-B64F-FD2A8E2E3178`. The generation correction is not yet
committed, built, installed or released. Exact committed tests, build and
import validation, private activation, loaded image verification, real S3
regression and an additional LAB asset remain the next cycle steps.

Repeated native GUI open, WPA2 and WPA3 transitions among saved networks,
automatic recovery without an Off and On workaround, DHCP and traffic after
sleep remain the highest operational priority. Those current IWM cells, AP
service and new IWN or IWX hardware are not qualified by these fixtures.
