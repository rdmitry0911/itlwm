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
APSTA publication and coalesced ready/census ordering. Existing admission, activation-failure, standard scan
and APSTA publication checks pass.

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

The new ready-success path has not yet been observed on hardware. The real
IWM 9260 still reports RF_KILL, so its new receipt producer cannot reach the
successful SCAN boundary. Full build, loaded-image checks and blocked-radio
sleep/wake regression are separate qualifications and do not replace that
missing observation. WPA3/SAE, GUI repeated open/WPA2/WPA3, saved networks,
DHCP/traffic, AP and successful Wi-Fi sleep/wake remain unqualified on this
card. Physical host `10.90.10.22` is outside this lab cycle.
