# IWM and IWX initialization receipt 20261009

IWM and IWX initialization now completes from the exact hardware generation's
accepted scan-ready receipt, not from the connection's current SCAN state.
A fast cached or saved-network join may already have entered AUTH before the
init thread resumes; it must not turn a completed activation into a timeout
and stop the radio. The same receipt makes a wakeup delivered before the first
sleep harmless.

## Completion and cancellation

The lower `isRadioScanReady` copies the existing immutable readiness record
under the scan lock, then reuses the complete `isRadioReadyCurrent` validator.
It matches the init's saved hardware generation and accepts genuine bootstrap
epoch zero. Reset, replacement activation, changed hardware generation,
RFKILL, hardware error, shutdown or missing IFF_UP/IFF_RUNNING rejects it.
The record survives scan-command completion; no currently active command or
live association state is needed to prove that initialization reached SCAN.

Both full init routines check the receipt before sleeping and again after
waking, including a timeout return. IWM also rejects a shutdown owner before
and after sleep; IWX retains its task-gate generation, init/stop-owner and
shutdown fence. No receipt still means a real timeout and normal teardown.
An already replaced or shutting-down epoch exits without timeout teardown.

This does not move the public ready boundary or its carrier. The lower producer
still requires an accepted firmware scan and committed S_SCAN. The preceding
nonblocking mailbox and exact epoch publication remain unchanged. The 25C56
reference separates hardware initialization, available `0x37/0xf8` and later
system-wake completion. Reference ownership is recorded in
`docs/reference/CR-479-driver-availability-producers-20260711.md`; IWM's real
lower SAE/PMF owner is described in
`docs/reference/TAHOE_IWM_DRIVER_RESIDENT_SAE_SOFTWARE_PMF_20260801.md`.
BCM-specific lower selectors are not fabricated as Intel firmware commands.

## Executed production fixtures

`scripts/test_mvm_radio_init_ready.sh iwm|iwx` extracts the complete production
init routine, sticky getter and lower receipt validator. Firmware scan-ready,
sleep return, task gate and security-hook reopening are explicit doubles.
Both families pass with ASan/UBSan for readiness before sleep, readiness on a
timeout, immediate AUTH before init resumes, no receipt despite mutable SCAN,
hardware error, scan rejection, generation replacement, shutdown without a
generation increment, monitor mode and copied-receipt invalidation. The
existing full producer/controller readiness fixtures also pass.

All eight complete historical init controls from `8e953f22` compile then fail
the expected behavioral assertion (exit 134): lost wake, consumer entering
AUTH, accepted ready on timeout, and mutable SCAN without a receipt, for both
families. The current getter and validator are shared dependencies in these
controls; only the old init body is substituted. These are deterministic
ordering checks, not measured real hardware successful initialization.

Evidence root:
`/home/dima/Projects/aiam/scratch/iwm-9260-runtime-20261009.mo5CXe/`.
Logs `radio-init-positive-linux.log`, `radio-init-historical-linux.log` and
the eight `radio-init-historical-{iwm,iwx}-*.log` retain the executed results.
An initial aggregate invocation used the orchestration checkout instead of
the driver checkout; its missing-script log is retained separately from the
corrected verification.

The complete fixtures and all eight historical init controls also pass their
expected outcomes on macOS. Full Linux aggregate and physical/standard scan,
power-off, SAE reset-reconnect and IWM software-PMF/driver-owner checks pass.
Logs: `radio-init-aggregate-linux-verified.log`,
`radio-init-macos-build.log` and `radio-init-historical-macos.log`.

## Loaded IWM regression

Production commit `9dab1748264259520d42685e9203a246a383279f` was built from the
clean isolated guest source; the dirty original checkout remains untouched.
Full Tahoe build resolves all 1088 imports. Private AuxKC validation and
transactional activation pass with four companion members and rollback
retained. Built and installed bundles compare identically. Activation root:
`/private/var/tmp/aiam-iwn-activation-iwm9260-init9dab1748-20261009`.

The guest was rebooted and the exact loaded image verified:

- Source identity `f31dcc45e0cd`.
- Boot `0F6485C1-A17C-4E98-A7DD-46756080E6B9`.
- UUID `DFED1EE5-0378-3FE0-81ED-01155D30633D`.
- Mach-O SHA256
  `63230f18d61a1ac9f5eb0e7e7a87fa724ba352d962f77e1dce7519ffe09846a5`.

Real S3 reports QEMU `paused (suspended)` and serial `ACPI SLEEP`. The private
monitor wake resumes the same boot; macOS reports 21 seconds asleep and
WakeTime 1.444 seconds. FBT captures activation epoch 2, lower RFKILL failure
at 2.658 ms, upper failure mailbox and gated doorbell at 19.912 ms after lower
acceptance. No `iwm_init`, new sticky getter or ready-success event is observed
in this bounded trace: RFKILL prevents that path from executing. This is a
loaded blocked-radio regression, not hardware proof of the new successful
init logic. en2, its route and SSH return; no panic observed. One SSH banner
timeout during suspension is retained. Logical On after failed wake is policy
state, not an operational radio.

Four subsequent native Off/On cycles retain the boot and en2 route. Every
power read remains Off; real SET FBT returns NotReady `0xe00002d8` in
1.828–4.731 ms, independent of `networksetup` process exit. GUI remains the
login screen. Logs: `radio-init-{activation,loaded,sleep-command,sleep-monitor,
sleep-trace,wake-monitor,wake-poll,postwake}.log` and
`radio-init-repeat-{command,trace}.log` under the evidence root above.

## Published laboratory package

The exact installed unsigned bundle is an additional asset in
`rdmitry0911/itlwm` release `v2.4.0-alpha`:
`AirportItlwm-Tahoe-IwmIwx-InitReceipt-9dab1748.kext.zip`, asset `625910461`,
15,712,818 bytes. ZIP SHA256:
`c72672ee7ec88d30c5f3f0a08520965481fc990b2893031511638ebaad2fc50b`.
Its extracted Mach-O matches the loaded hash. A separate API read verifies
size/digest, all five preceding assets unchanged, and all previous notes
byte-for-byte preserved as the suffix. The default September archive is not
replaced. LAB ONLY notes retain the RFKILL and missing on-air qualification.

## Remaining security and hardware qualification

IWM still reopens PMF, SAE TX and SAE engine admission after the init waiter
observes ready. Its separate `security-before-ready` ordering control remains
red (exit 134), retained as `radio-init-iwm-security-remaining.log`. IWX already
opens the engine before scheduling its first scan and passes that control.
The current change removes the erroneous init timeout; it does not close
IWM's early SAE-consumer window or authorize moving blocking hook drains into
the scan worker.

The lab's real IWM 9260 reports RFKILL. Successful new init/ready, repeated GUI
open/WPA2/WPA3, saved networks, DHCP/traffic, AP and operational Wi-Fi after
sleep remain unqualified on this card. Loaded-image and blocked-radio S3
regression must be kept separate from those missing on-air observations.
Physical host `10.90.10.22` is not part of this cycle.
