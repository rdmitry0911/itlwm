# IWM and IWX radio ready ownership 20261009

IWM and IWX now deliver confirmed radio readiness as an immutable lower
receipt through a nonblocking controller mailbox. A delayed old receipt
cannot borrow a new POWER attempt's epoch, and a scan worker no longer has
to enter the upper command gate while Off may be draining that worker.
This follows the preceding accepted-activation failure correction.

## Producer and controller boundaries

Both `noteWclScanRadioReady` producers retain their real boundary: an accepted
firmware scan command and committed S_SCAN state. Under the existing lower
scan lock they capture the activation epoch, scan receipt serial and hardware
generation into the 32-byte `ItlRadioReadyV1`. Receipt identity remains valid
after the command completes and successful init cancels its failure owner;
reset or a replacement activation invalidates it. Hardware error, RF_KILL,
shutdown, changed generation or missing IFF_UP/IFF_RUNNING rejects validation.

The upper REOPENED callback value-copies this receipt and rings the retained
interrupt-source doorbell. It neither enters the controller gate nor mutates
ESS, scan admission or APSTA inventory. The gated action rechecks the exact
receipt, its captured availability epoch and the live lower owner before
performing those effects. Public DRIVER_AVAILABLE uses the receipt's actual
nonzero epoch, not the current pending epoch. A genuine epoch-zero bootstrap
receipt may reopen scan admission and publish firmware-supported APSTA
inventory, but cannot satisfy a later public On. Duplicates are ignored;
a newer receipt can replace a queued older one from a reset.

Ready and a fast scan terminal can share the same interrupt notification.
The action handles failure first, then ready, then scan-result publication,
preserving availability before census exposure. It does not invent a
successful hardware ready event or bypass a blocked radio. Legacy IWN still
emits NULL and retains its previous receiver; its ownership and gate/drain
correction are not included in this MVM checkpoint.

The 25C56 reference powerOn listing publishes the exact 0x37/0xf8 available
carrier at `0xffffff80015e0245` before its later return. The separate system
wake adds POWER_CHANGED after its own restoration boundary. The exact carrier
and domain distinction remain unchanged. Source:
`~/Projects/ghidra_output/aiam_poweron_retry_exact_25C56_20260802/powerOn.full_range.txt`
on `10.7.6.112`, and the repository reference
`docs/reference/CR-479-driver-availability-producers-20260711.md`.

## Executed software verification

`scripts/test_mvm_radio_ready.sh iwm|iwx` executes the complete production
ready producer, lower validator, upper mailbox, gated action, interrupt action
and availability action. The scan command and physical completion reducers
are the real production headers. Firmware state, IOKit services, APSTA
publication and external carrier/result delivery are explicit doubles.

Both families pass with ASan/UBSan. Cases include a callback while the upper
gate is held, completed scan and cancelled failure owner before delivery,
replacement between the lower claim and callback, old queued action versus
new On, bootstrap zero and duplicate receipts, newer reset receipts, hardware
and upper failures, lifecycle shutdown, malformed records, replacement during
APSTA publication and coalesced ready/census ordering. Existing admission,
activation-failure, standard scan and APSTA publication checks pass.

Full historical producers from `f6110188` compile but fail the missing-receipt
assertion (exit 134, both families). The complete historical readiness
consumer compiles but fails when it tries to enter the upper gate from the
lower callback (exit 134, both families). This is an explicit IOKit-double
ordering check, not a measured real hardware deadlock. The initial negative
runner assigned its mode inside a pipeline subshell, so its first upper
control accidentally ran positive mode; that runner was corrected before
the failing controls above and makes no production change.

Evidence root:
`/home/dima/Projects/aiam/scratch/iwm-9260-runtime-20261009.mo5CXe/`.
Logs: `radio-ready-negative-{producer,upper}-{iwm,iwx}.log` and
`radio-ready-verified-physical-linux.log`. The first static reset check matched
a callsite instead of the full method definition; its failing log is retained
as `radio-ready-final-physical-linux.log` and its selector was corrected.

## Hardware qualification

Production commit `6182a2b50357796ca2db335f67f283ee1960620a` was built in the
isolated guest checkout. The same complete fixtures and four historical
negative controls pass their expected outcomes on macOS. Full Tahoe build
passes all 1088 imports, private AuxKC validation passes, and transactional
activation retains four companions and rollback material. Built and installed
bundles compare identically. Activation root:
`/private/var/tmp/aiam-iwn-activation-iwm9260-ready6182a2b5-20261009`.

The disposable IWM 9260 guest was rebooted and verified to load the exact new
image, not merely have it installed:

- Boot `FDF7EADB-0029-4961-8B1C-DF0091297DD4`.
- Source identity `ef5d7d0021db`.
- Loaded UUID `67CD7ED7-FE5B-3C15-AE01-BD2194F34DC6`.
- Mach-O SHA256
  `cbd321d2e3bad0c85bafcb0a7d6c894ff1b2370771b12eb8685abe951a799111`.

Real S3 was then entered from the bootstrap logical-On state. QEMU reported
`paused (suspended)` and the serial console recorded `ACPI SLEEP`; the private
monitor's `system_wakeup` resumed the same boot. macOS reports a 26-second
sleep and WakeTime 1.262 seconds. Passive FBT recorded IOPM Off/On, activation
epoch 2, the exact lower RFKILL failure, upper mailbox and gated failure
doorbell. Lower failure was 1.562 ms and doorbell 6.112 ms after acceptance.
No ready producer, ready mailbox or ready doorbell was observed in that
bounded trace. Logical On after failed wake is policy state, not an available
radio. Management en2, its default route and SSH returned; no panic observed.
One SSH banner timeout during suspension is retained, not treated as a crash.

Four subsequent native Off/On cycles pass, all reads remain Off, with the same
boot and en2 route. Actual SET/CORE FBT returns `0xe00002d8` (NotReady), not
just a successful `networksetup` process exit: On duration 1.888–2.955 ms.
GUI observation remains the login screen. Logs are
`radio-ready-{macos-build,negative-macos,activation,loaded}.log`,
`radio-ready-{sleep-command,sleep-observe,sleep-trace,wake-monitor,wake-poll,postwake}.log`
and `radio-ready-repeat-{command,trace}.log` under the evidence root above.

The new ready-success path has **not** been observed on hardware. The real
9260 still reports RF_KILL, so its receipt producer cannot reach the successful
SCAN boundary. These loaded-image and blocked-radio regressions do not replace
that observation. WPA3/SAE, GUI repeated open/WPA2/WPA3, saved networks,
DHCP/traffic, AP and operational Wi-Fi after sleep remain unqualified on this
card. Physical host `10.90.10.22` was not modified.

## Published laboratory package

The exact installed unsigned bundle is an additional asset in
`rdmitry0911/itlwm` release `v2.4.0-alpha`:
`AirportItlwm-Tahoe-IwmIwx-RadioReady-6182a2b5.kext.zip`, asset `625856131`,
15,712,530 bytes. ZIP SHA256:
`f28a0e4951f75059635dfadd494b8b444941b670a71622a8299ba37a155f63e3`.
The archive's extracted Mach-O matches the loaded hash above. Its label and
notes explicitly say LAB ONLY, RFKILL and no on-air qualification. A separate
API read verified size/digest, all four earlier assets unchanged, and all
previous release notes byte-for-byte retained as the suffix. The default
September package was not replaced.
