# IWM and IWX accepted radio activation failures 20261009

This checkpoint adds an exact request-owned failure path for a radio start
that was accepted before RF_KILL, a fatal hardware state or recovery
exhaustion became known. It also prevents an older sleeping POWER request
from rolling back or disabling a newer Off/On successor. Build, loaded-image
and runtime qualification are recorded separately below when executed.

## Production changes

The controller arms an availability epoch before calling the new optional
HAL `enableForRadioPowerOn` entry. IWM and IWX store that request identity
before scheduling initialization. Their complete init workers capture it
before inspecting fatal state; a one-shot compare/exchange claims only that
request for the internal event `IEEE80211_EVT_RADIO_POWER_ON_FAILED` (27).
The 32-byte versioned value carries epoch, IOReturn, reason and lower error,
but no node, controller, credential or retained pointer.

Known RF_KILL returns `kIOReturnNotReady`; fatal hardware and five failed
complete attempts return `kIOReturnIOError`. A successful init cancels only
its own failure owner. Admission resets the retry counter under the same
lock used to claim each retry, so an older attempt cannot consume a new
activation's budget. Exhaustion stops self-requeueing; it does not delete
the shared init task, which may already contain a successor's queued work.
Bootstrap with epoch zero keeps its existing discovery path and does not
produce this public activation failure. Legacy IWN retains its default HAL
entry and has no new lower failure producer in this checkpoint.

Lower event ingress only validates and copies the value under the short
admission lock, then rings the existing retained interrupt-source doorbell.
It does not enter the controller command gate or cancel/drain the backend:
an Off caller may own that gate while waiting for the init task to finish.
The gated action wakes the exact waiter. Failure prevents ready publication
and cancels pending success wake bulletins, without fabricating
DRIVER_AVAILABLE, POWER_CHANGED or a permanent firmware-failure carrier.

The public waiter returns the lower error promptly. It retires and disables
the failed lower activation only if its exact availability epoch still
belongs to it, clearing IFF_UP for a genuine fresh retry. If Off or a newer
On has already replaced that epoch, the old caller returns its error without
changing the successor's lower state or logical radio state. An asynchronous
IOPM wake has no sleeping caller; its failed epoch remains unavailable and
association fails closed. Automatic recovery from that system-wake failure
is not claimed here.

## Reference boundary

The 25C56 `AppleBCMWLANCore::handlePowerStateChange` at
`0xffffff800157af02` rolls back the old logical state after a real powerOn
error. Its full powerOn assembly range has the recovery counter at `+0x150c`,
compare against four and increment, and an early `0xe00002d5`
(`kIOReturnBusy`) return before the availability tail. These establish real
failure and bounded-recovery boundaries, not a requirement to translate a
temporary Intel RF_KILL into the reference's permanent-failure carrier.
The separate IOPM wake carriers remain separate from native radio toggles.

Read on `10.7.6.112`:

- `~/Projects/ghidra_output/aiam_power_lifecycle_exact2_25C56_20260711.c`
- `~/Projects/ghidra_output/aiam_poweron_retry_exact_25C56_20260802/powerOn.full_range.txt`

The combined export's truncated powerOn/powerOff decompiles are not used to
infer their missing tails. Numeric IOReturn names are checked against XNU's
`iokit/IOKit/IOReturn.h`.

## Executed software checks

`test_mvm_radio_power_failure.sh iwm|iwx` compiles and executes the complete
production init worker, request bridge, failure receiver, interrupt action
and waiter with ASan/UBSan. Firmware initialization, MMIO and IOKit lock,
sleep and interrupt services are explicit doubles. Tests cover fatal skips,
RF_KILL arriving during init, exactly five attempts, eventual success,
one-shot ownership, replacement during init or after the fifth retry claim,
stale queued doorbells, cancelled requests, malformed values, failed IOPM
wake and untagged bootstrap.

The admission fixture also executes the complete POWER transition and failed
activation retirement bodies. Its nested Off/new-On case proves the old
sleeping caller cannot roll back the new On, while an owned timeout disables
its lower attempt and allows the next enable to run.

Both families pass, as do the complete payload aggregate and physical-scan
lifecycle gate. Standard scan and power-off contracts pass. Historical full
workers from `87694fb6` compile but fail the missing-failure behavioral
assertion (exit 134, both families). The historical full controller compiles
but fails the successor-state assertion (exit 134). The unchanged q0 static
expectation described in the radio-admission checkpoint remains separate.

Evidence root:
`/home/dima/Projects/aiam/scratch/iwm-9260-runtime-20261009.mo5CXe/`.
Logs: `activation-failure-payloads-linux.log`,
`activation-failure-negative-{iwm,iwx}.log` and
`activation-supersession-negative-iwm.log`.

## Full build and loaded IWM runtime

The first full macOS build of `92207a86` caught an ABI-double mismatch:
SDK IOReturn is signed int, while the fixtures had used uint32_t. The packet
stores the status bit pattern as uint32_t, so both producers and the receiver
now convert explicitly, and both fixtures use the SDK's signed type. The
failed build was not installed; its log is retained. A preceding command
also used the wrong power-off test filename and exited before building; the
correct WCL contract passed on retry.

The corrected production commit `cfa8d39763dcfb89435ec1a586c2723b4e6bfd41`
is pushed and built from a clean committed guest tree. Both admission and
failure fixtures, standard scan and power-off contracts passed on macOS.
The complete Linux payload aggregate passed again with signed IOReturn.
All 1088 BootKC imports resolve, without `_thread_call_cancel_wait`.

- Source identity: `af147f2b6af2`.
- Built, installed and loaded UUID: `7DB624F0-37A5-368B-9286-0257CFEB967E`.
- Mach-O SHA256: `bf8a8e52baafa49ffe216537957b90ab131ad0807a7491e6dd533223da71508a`.
- Guest boot: `5897D32D-0B4C-477C-9C1F-4C1A3FE2559D`.
- Transaction root: `/private/var/tmp/aiam-iwn-activation-iwm9260-failcfa8d397-20261009`.

Private AuxKC admission passed without canonical mutation. Transactional
activation reached READY, preserved four companion members and retained
rollback copies. The installed bundle is byte-equal to the built bundle.
Only the disposable guest rebooted; no unload or physical-host reboot.

### Actual S3 failure qualification

With Wi-Fi logical On and the real RF_KILL retained, `pmset sleepnow` entered
S3. QEMU reported `paused (suspended)` and serial recorded ACPI SLEEP. A
`system_wakeup` command to this VM's private monitor resumed it. Guest power
history reports 44 seconds asleep and a 1.457-second wake; independent SSH
and en2/default10.0.6.2 returned in the same boot with the same loaded UUID.

Passive FBT records the actual new path, not an injected callback:

`IOPM_SYSTEM Off -> IOPM_SYSTEM On -> ENABLE_ADAPTER epoch=2 ->
IWM_ACCEPT epoch=2 -> IWM_FAILURE(epoch=2, status=0xe00002d8, reason=1,
lower=EPERM) -> UPPER_FAILURE epoch=2 -> FAILURE_DOORBELL epoch=2`.

Admission to the lower failure took 4.244 ms; admission to the gated failure
action took 9.418 ms. Serial records RADIO_POWER_ON_FAILED, and subsequent
IOC reads expose `isDriverAvailable=0`. Logical POWER remains On after the
failed IOPM wake; this is preserved policy state, not an operational radio.
The early SSH timeout during S3 is retained in the command log.

Four subsequent actual native Off/On cycles passed. Off remained readable
after each RF_KILL refusal; FBT recorded On returns `0xe00002d8` in 1.804,
1.897, 1.937 and 2.104 ms. Management and boot identity survived. This proves
the real IWM failed-wake terminal and bounded repeated blocked controls,
not a public commandSleep failure under real late RF_KILL (fixture-only).

Logs: `activation-failure-{signed-macos-build,activation,loaded}.log`,
`activation-failure-sleep-{trace,commands,qemu}.log`,
`activation-failure-wakeup-{qemu,reachability}.log`, and
`activation-failure-postwake-power-{trace,commands}.log`. Exact probes and
native sequence are `radio-failure-runtime.d` and `radio-failure-repeat.sh`.

The exact installed unsigned Debug bundle is the separate LAB release asset
`AirportItlwm-Tahoe-IwmIwx-RadioFailure-cfa8d397.kext.zip`, 15,711,181 bytes,
ZIP SHA256 `85c0e6ea388a4f93e6a6db1bd8eec75412902d6d04a1f06f7af5a5ec1e21fd34`.
GitHub asset `625769224` independently reports that digest and size. Older
assets, including the default qualified archive, are retained unchanged.

## Remaining qualification

The lab's 9260 still reports hardware RF_KILL in both Linux and macOS. No
radio block is bypassed. IWX hardware, GUI repeated open/WPA2/WPA3, saved
networks, DHCP/traffic, AP and successful Wi-Fi recovery after sleep are not
qualified here. Lower terminal ownership is software-tested for both MVM
families but hardware-qualified only for this IWM failed system-wake path.
The subsequent `TAHOE_MVM_RADIO_READY_OWNERSHIP_20261009.md` checkpoint
addresses MVM REOPENED readiness identity and the success callback's gate
entry. This failure fix does not borrow the current pending epoch as its
producer identity. Physical host `10.90.10.22` is untouched.
