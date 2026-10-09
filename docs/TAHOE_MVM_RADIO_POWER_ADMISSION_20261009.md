# IWM and IWX radio power admission 20261009

Public radio-on requests now check the current hardware RF_KILL level before
changing the logical radio state or arming a readiness wait. An asserted
switch returns `kIOReturnNotReady` immediately. The next request reads the
physical CSR again, so a stale cached flag cannot prevent activation after
the switch is released. This does not enable a physically blocked radio.

## Failure and readiness boundaries

The baseline IWM 9260 runtime recorded an actual `setPOWER(On)` call waiting
15.005 seconds and returning `0xe00002d6`. Lower initialization had already
been skipped with `fatal=2` (RF_KILL). GET exposed tentative On during that
wait, delaying repeated native Off/On controls; after timeout it returned Off.
This was not a permanent radio-off failure. The raw FBT trace and commands
are in `power-control-trace{,-commands}.log` under the evidence root below.

The 25C56 reference `handlePowerStateChange` at `0xffffff800157af02` restores
the old logical state after a real powerOn error. The powerOn range listing
also contains a synchronous NotReady return before its successful availability
tail. Reference material read on `10.7.6.112`:

- `~/Projects/ghidra_output/aiam_power_lifecycle_exact2_25C56_20260711.c`
- `~/Projects/ghidra_output/aiam_poweron_retry_exact_25C56_20260802/powerOn.full_range.txt`

The Intel preflight uses each backend's existing `check_rfkill` implementation,
which reads `CSR_GP_CNTRL` bit 27 and updates its cached flag. It applies only
to transitions that start the lower radio: Off to On, Standby to On, and Off
to Standby. Off, invalid requests and same-state requests do not acquire this
new hardware dependency. Controller lifecycle admission and the command gate
continue to fence the HAL access. A shutdown backend refuses without reading
MMIO.

Bootstrap firmware discovery remains separate. Temporary RF_KILL must not
convert the accepted boot discovery into a permanent BootFailure. An accepted
public activation still waits for its real tagged post-init REOPENED event;
the preflight publishes no DRIVER_AVAILABLE or POWER_CHANGED carrier and
does not shorten the normal 15000 ms timeout. Existing five-attempt lower
recovery is unchanged. `enableAdapter` also preserves synchronous HAL errors
before enabling Skywalk queues or arming its watchdog.

## Executed software verification

`scripts/test_mvm_radio_power_admission.sh iwm|iwx` executes the complete
production CSR reader, new HAL admission, `enableAdapter` and radio POWER
transition bodies with ASan and UBSan. Physical register values, IOKit queues
and asynchronous ready sleep are explicit doubles, not on-air observations.
It covers repeated blocked requests, fresh unblock/reblock, Off/Standby,
lower enable errors, timeout rollback, absent HAL, shutdown and bootstrap.

Both families pass. Substituting the full historical source through
`MVM_POWER_NEGATIVE_REF=f4f99486` fails the blocked-request behavioral assertion
(exit 134). Replacing only historical `enableAdapter` through
`MVM_POWER_NEGATIVE_ENABLE_REF=f4f99486` fails the lower-error assertion
(exit 134). These controls compile successfully.

The full payload aggregate and physical-scan lifecycle gate pass. Standard
scan and power-off link-down contracts pass. The separate q0 serialization
static gate still expects `sc->sc_task_gate_closed` in `epoch_live`, although
the baseline production function deliberately keeps the init lease valid
after its closed-to-open transition; that unchanged expectation is not a
regression introduced by radio admission.

Evidence root:
`/home/dima/Projects/aiam/scratch/iwm-9260-runtime-20261009.mo5CXe/`.
Current software logs are `power-admission-linux.log`,
`power-admission-negative-iwm.log` and `power-admission-negative-enable.log`.
Committed-source macOS build, installation, loaded identity and the repeated
real native power trace remain required before claiming this runtime layer
closed.

## Remaining user paths

RF_KILL asserted after admission or during initialization, fatal init skips
and exhausted asynchronous lower retries still need an owned failure signal
to the waiting controller. This preflight closes the already-known physical
block only. IWN keeps its previous admission path. The repeated GUI
open/WPA2/WPA3, saved-network, DHCP/traffic and sleep/wake matrix remains
unqualified while the assigned 9260 cannot transmit or scan. Physical host
`10.90.10.22` is outside this laboratory cycle.
