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

## Exact build and loaded regression

Production `c9f1d745922aeea6eb20a61c10ba2a92ad30ae0d` is committed, pushed and
independently matched to the remote branch. Exact committed Linux and macOS
payload gates and source contracts pass. The ordinary Tahoe build succeeds
and all 1088 imports resolve against the running guest BootKernelExtensions.kc.
The isolated guest checkout is clean; build and DerivedData complete bundles
compare equal.

- Source identity: `c89ac518508d`.
- Mach-O UUID: `8A5BF340-BFC5-3C2E-8FF8-E27D52B48868`.
- Mach-O SHA256: `495ed1f4174112a4572ac7b35bf797d21651a10e20063b6bebdead93e7b81f82`.
- Loaded boot: `CA1DAC07-E228-41F6-A8E3-7488217F4D1E`.

Private activation `activation-20261010T130244Z` preserves all four companion
kexts and rollback, reaches READY and verifies the installed complete bundle.
One guarded guest reboot loads the exact new UUID. The first observation is
reset and the second times out during banner exchange; the next verifies the
new boot, loaded UUID, full installed bundle and independent en2 management.
No QEMU restart or host side replacement of the base or overlay occurs. Guest
installation writes its existing overlay. The original dirty guest checkout
and physical host 10.90.10.22 are untouched.

The new consume and confirmed admission entry probes are present in the
loaded image. The bounded DTrace observer completes with remote status zero.
Before sleep, native Off and On calls return success and public power reads
On, but actual lower init records hardware RFKILL fatal 2 without IFF_RUNNING.
Those initial public results are not successful radio admission.

Two independent private monitor observations show paused suspended state;
serial records System Sleep and ACPI SLEEP. One exact private monitor wake
resumes the same boot. Power history records a 51 second sleep interval and
WakeTime 1.252 seconds; that interval is not a measurement of the full physical
paused duration. The first postwake SSH observation times out during banner
exchange, and the next verifies exact image, complete bundle and en2 route.

The actual IWM disable returns in 668.248 milliseconds. The power log retains
the driver slow acknowledgement and WindowServer 30000 millisecond timeout.
Four native postwake Off and On controls preserve management and power Off.
The real driver On calls return NotReady at 2.100, 1.795, 1.906 and
2.020 milliseconds. Utility exit zero does not mean successful radio startup.
The guest ends with Wi-Fi Off.

No actual BTM consume or confirmed admission runs under RFKILL. The loaded
regression therefore does not qualify on air BTM, WPA3, restored Wi-Fi traffic,
GUI combinations, AP service or new IWN and IWX hardware.

## Additional laboratory release

The additional unsigned Debug asset is
[AirportItlwm-Tahoe-BtmGeneration-c9f1d745.kext.zip](https://github.com/rdmitry0911/itlwm/releases/download/v2.4.0-alpha/AirportItlwm-Tahoe-BtmGeneration-c9f1d745.kext.zip).
Build, installed and extracted complete bundles compare equal, without a
packaging rebuild.

- Asset ID: `628008501`; size: 15,719,194 bytes.
- ZIP SHA256: `e511daa110749e6c097c4fd0ccccd0dd33dd97dcff2b04481c03fa58d2fd2042`.

Fresh API reads verify all sixteen previous assets and the default unchanged.
The complete previous notes remain the exact suffix below the new LAB entry.
An independent release download compares byte for byte with the validated
local archive. The label and notes retain RFKILL and missing on air limits.

## Remaining operational qualification

Repeated native GUI open, WPA2 and WPA3 transitions among saved networks,
automatic recovery without an Off and On workaround, DHCP and traffic after
sleep remain the highest operational priority. Those current IWM cells, AP
service and new IWN or IWX hardware are not qualified by these fixtures.
