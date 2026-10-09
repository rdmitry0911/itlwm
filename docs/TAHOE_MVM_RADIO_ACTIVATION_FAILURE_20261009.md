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

## Qualification still required

The first full macOS build of `92207a86` caught an ABI-double mismatch:
SDK IOReturn is signed int, while the fixtures had used uint32_t. The packet
stores the status bit pattern as uint32_t, so both producers and the receiver
now convert explicitly, and both fixtures use the SDK's signed type. The
failed build was not installed; its log is retained. A preceding command
also used the wrong power-off test filename and exited before building; the
correct WCL contract passed on retry.

This software checkpoint is not yet a loaded-image or real asynchronous
failure observation. The lab's 9260 currently reports hardware RF_KILL in
both Linux and macOS. No radio block is bypassed. A real guest sleep/wake
attempt will be evaluated separately; native blocked Off/On regression can
exercise the previous admission fix but cannot by itself qualify this new
post-admission terminal. IWX hardware, GUI repeated open/WPA2/WPA3, saved
networks, DHCP/traffic, AP and successful sleep/wake are not qualified here.
The existing untagged REOPENED readiness identity remains a separate audit
item; this new failure does not borrow the current pending epoch as its
producer identity. Physical host `10.90.10.22` is untouched.
