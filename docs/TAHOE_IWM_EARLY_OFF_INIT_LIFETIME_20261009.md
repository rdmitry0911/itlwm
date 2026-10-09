# IWM Off during firmware init: lifetime and hardware quiescence

## Reproduced failure

An IWM firmware init starts hardware before setting `IFF_RUNNING`. The old
`disable` cleared `IFF_UP`, but `DVACT_QUIESCE` called `iwm_stop` only when
`IFF_RUNNING` was already set. Off could therefore return while that init
still owned live firmware/DMA. A replacement On restored `IFF_UP`; the old
init could reopen its unchanged scan lease and emit ready with the replacement
request epoch.

The full-function historical control from `9dab1748` reproduces both cases.
Early Off exits 134 because it returns before quiescence. Early Off/On exits
134 with `result=0 premature=1 hardwareLive=1 stops=0 scans=1 readyEpoch=84`:
the firmware attempt began under request 42, not request 84. Firmware and
ready production are explicit doubles; this is not a measured on-air race.
Initial fixture compile failures were retained, then corrected before either
authoritative behavioral negative was run.

## Lower ownership change

IWM now admits one firmware-init owner under its existing sleeping TX
lifecycle lock. That owner also holds an existing detach lease until every
init exit. Another init cannot reset mutable BA state or begin hardware while
an init or stop owns the lifetime.

Stop closes `SHUTDOWN` and advances the hardware generation before scan/state
invalidation. It cancels queued work, wakes the first-scan waiter and drains
the init owner before `iwm_stop_device` erases DMA/rings. Quiesce no longer
depends on `IFF_RUNNING`. Init rechecks its owner after hardware init and
before/after the ready wait, and leaves through one release tail. It cannot
reopen an old scan lease after Off or report success after stop wins.

A timeout originating inside init retains its one self-reference while
stopping, so it does not wait on itself. If an external stop already owns the
reset, the init caller exits instead of waiting for that stop. A second
external Off waits for the first stop's actual erase. New enable is refused
while shutdown owns admission.

This uses the existing IWX init/stop owner-count pattern, not Broadcom private
field offsets. The framework contract remains the reference powerOff quiesce
boundary and its separate system/radio availability carriers; see
`docs/reference/CR-480-system-pm-state-word-20260711.md`. No new carrier,
RFKILL bypass or forced ready is introduced. Firmware waits remain bounded by
their existing timeouts; this change does not invent a firmware-loader abort.

## Verification scope

`scripts/test_iwm_radio_init_stop.sh` executes the complete production
`iwm_init`, `iwm_stop`/`iwm_stop_internal`, `disable`, `enable`, `iwm_activate`,
all six new owner helpers, and the actual scan-reopen/ready validators.
`ItlScanCommandLease` is the real header. Hardware, task queues, net80211
soft-state and IOKit locks are explicit doubles. Two threaded controls pause
hardware init while actual Off runs; a third pauses hardware erase while a
second actual Off waits.

Linux ASan/UBSan passes early Off, early Off/On with a fresh replacement init,
normal, missing ready/timeout, hardware failure, overlapping Off and admission
controls. The old full init/stop/disable/activate bodies compile, then fail
both intended historical assertions. The existing full IWM/IWX ready-init
fixtures remain separate and retain their explicit ownership doubles.

The Linux payload aggregate, physical/standard scan, power-off/link-down,
unexpected AP reset replay and resident IWM SAE owner contracts pass. The
static stop checks now follow the real `iwm_stop_internal` body and separately
verify the external wrapper, rather than dropping the old cancellation check.

The same seven new controls and the existing complete IWM/IWX init-ready
fixtures pass on macOS. Both historical controls compile there, then exit 134
at the intended assertion; Darwin's missing-ready error is 35, Linux's is 11.

## Loaded candidate and real S3 regression

Production `e689972e3fcf4c44a032d71731825e9e45f4bb38` is committed/pushed,
built from the clean isolated guest checkout, transactionally installed and
loaded. Build source identity is `38fff67b1a04`; all 1088 undefined imports
resolve against the real BootKC. Private AuxKC admission and exact
build/installed bundle equality pass. Four companions and rollback are
retained under
`/private/var/tmp/aiam-iwn-activation-iwm9260-stope689972e-20261009`.

- Loaded UUID: `4ED4ECEA-0106-3F4D-9B90-18A96B049E21`.
- Mach-O SHA256: `2c65cdee33c887bc8637eff5825c26b08700fa5e67d2143a30632e498a4eedd8`.
- Boot: `3298EDC9-BB1E-422B-BF45-D65C10A9B674`.

Actual S3 is confirmed by QEMU `paused (suspended)` and serial `ACPI SLEEP`.
The same boot returned after 39 seconds; the framework reports WakeTime
1.280 seconds. FBT observes IOPM Off, lower disable, stop owner entry, init
drain with self-counts 0/1, device erase, stop owner end (generation 1), then
successful lower disable return. The stop interval is about 664 ms on this
RFKILL-blocked card. IOPM On subsequently produces the owned epoch-2 RFKILL
failure and its deferred upper failure dispatch. No actual firmware-init
owner entry or successful on-air ready was observed.

Four post-S3 native Off/On controls pass, readbacks remain Off and en2/default
route are intact. Three additional controls use an exact inner `setPOWER`
probe: real SET returns NotReady `0xe00002d8` in 2.553, 2.016 and 1.947 ms.
Their command-process exit status is not relabelled as radio success.

The first trace had an overbroad `setPOWER` wildcard producing nested timing
samples and printed a bool return as 32 bits. Those raw samples are retained
but excluded from timing/boolean claims. The corrected script uses the exact
inner function and an 8-bit bool return. An initial loaded-state check hit
SIGPIPE from `grep -q` under pipefail; the independently repeated complete
loaded-line check passes. Transient SSH failures during reboot/wake are
retained; same-boot management recovery is verified. GUI evidence is still
the login screen, not a connection matrix pass.

Evidence logs are in the current laboratory control root under `radio-stop-*`.

## Separate LAB release artifact

Release `v2.4.0-alpha` has additional asset `625987470`,
`AirportItlwm-Tahoe-Iwm-EarlyOff-e689972e.kext.zip`, 15,713,420 bytes,
SHA256 `42076839e3a2310ca1a837f36a6c8785111b8b8ce406436187186a65c32198d3`.
The archive is the installed bundle without a rebuild; its extracted Mach-O
matches the loaded hash. A fresh API read verifies the new size/digest, all
six older assets unchanged, and all preceding release notes byte-for-byte
preserved as a suffix. The default archive is unchanged. The label and notes
retain unsigned LAB ONLY, RFKILL and missing on-air qualification.

## Still open

This closes the init owner, not every IWM task producer or command-queue
serialization path. It does not claim that `task_del` drains an already
dequeued state worker. Nor does it move PMF/TX/SAE hook reopening before the
first ready consumer: the separate security-reopen fixtures remain WIP and
their SHUTDOWN negatives remain red.

The physical 9260 is still hardware RFKILL-blocked. Successful firmware init,
GUI repeated open/WPA2/WPA3, saved networks, DHCP/traffic, AP and operational
Wi-Fi after sleep remain unqualified on this card. Physical host `.22` is
untouched.
