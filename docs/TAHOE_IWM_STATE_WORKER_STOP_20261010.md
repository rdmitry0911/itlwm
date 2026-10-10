# IWM state workers retire before radio resource erasure

IWM radio Off now cancels command waits and drains an already dequeued state
worker before resetting its firmware resources. This repairs a lifetime race
in repeated activation, association and recovery. Successful GUI joins,
DHCP and on air WPA3 remain separate hardware qualification.

## Reproduced resource lifetime failure

The complete historical dispatcher, state worker and stop bodies from
`685f3880` compile, then fail the intended assertion on Linux and macOS with
exit 134. Off returns while AUTH still holds its lifecycle lease; device
erasure occurs with that lease live, and AUTH observes the erased device.
The actual state request generation fence suppresses a stale generic commit
but cannot protect firmware storage still used by the body. Release and join
precede the assertion. Normal and cancelled state controls pass on both
platforms.

The complete historical command sender separately reproduces cancellation
between descriptor publication and wait registration. Even the new wake
helper in the explicit stop double cannot rescue the old unconditional
sleep: it reports ETIMEDOUT with one wait instead of ENXIO with none. This
also compiles and fails its intended assertion with exit 134 on both
platforms; the historical DMA rejection control passes.

## State ownership and command cancellation

The state dispatcher acquires a dedicated radio state reference under the
existing sleeping lifecycle lock. Admission requires UP and RUNNING, no
detach and no SHUTDOWN. The reference also participates in the existing
detach lifetime. Initial SCAN does not require SAE admission. The copied
state request generation and identity remain the separate readiness fence.

Stop first closes SHUTDOWN and advances the generation, invalidates the
copied state and scan owners, then wakes command descriptors and scan abort
waiters under the firmware wait mutex. It drains init, other stop and state
references before erasing the device and response slots. The wake helper
does not free responses or DMA. The init caller can still retain its own
single init reference on a timeout; generic SAE callback references are not
mistaken for state bodies that radio Off must synchronously drain.

The synchronous command sender takes the same wait mutex and rechecks
generation and SHUTDOWN before registering its sleep. A cancelled command
returns ENXIO without requiring an IRQ on the main workloop. Existing
generation checks still prevent a stale response from becoming a success.
No response owner, firmware command, RFKILL bypass or ready event is invented.

## Recursive controller gate waiting

Native POWER can hold the same main gate needed by firmware IRQs and generic
scan callbacks. A lifecycle IOLock sleep alone cannot release that gate.
An in gate drain instead releases the lifecycle lock and sleeps the whole
recursive workloop gate, then reacquires the lifecycle lock to check the
actual reference counts. The public command gate sleep and wake methods
wrap the protected workloop operations. Off gate callers retain the ordinary
IOLock wait.

Retirement wakes the workloop without acquiring its gate. Because its
predicate is protected by a different lock, a 10 millisecond deadline bounds
the wake before registration gap. A timeout only rechecks the predicate;
it never permits erasure of an owned device. The full recursive gate depth
is restored before the caller resumes. These API semantics follow the
[Apple workloop implementation](https://raw.githubusercontent.com/apple-oss-distributions/xnu/main/iokit/Kernel/IOWorkLoop.cpp)
and [command gate implementation](https://raw.githubusercontent.com/apple-oss-distributions/xnu/main/iokit/Kernel/IOCommandGate.cpp).
These are IOKit API sources, not a claim about private BCM firmware commands.

The lower retirement principle is consistent with
`reference/AppleBCMWLAN_IWM_SAE_TX_COMPLETION_25C56_20260801.md` and the
existing IWX cancellation before drain model. The prepared exact BCM
powerOff decompile is truncated; it does not establish this Intel specific
wake algorithm.

## Executable verification scope

All sixteen complete IWM lifecycle controls pass ASan and UBSan on Linux
and in the macOS WIP fixture. The two gated controls execute a callback
double which needs the same gate while Off owns it twice; one deliberately
drops wakeups. Both retain live storage until the state lease retires,
restore recursion and suppress the stale commit. The callback double is
not a real generic scan or kernel workloop execution.

The complete sender cancellation controls pass before wait, during wait
for inline and DMA payloads, and with an absent or invalid ring. The wait
double explicitly releases and reacquires the wait mutex. Responses and
published DMA stay owned until sender retirement and reset. Actual kernel
IRQ, DMA and firmware execution are outside these fixtures.

Linux payload aggregate and adjacent scan, Off link, resident SAE, AP reset,
software PMF and IWX init epoch contracts pass. Existing sender controls
cover 37 IWM and 62 IWX scenarios. Nineteen IWM security controls and fifteen
IWX lifecycle controls also pass in the macOS WIP. State request fixtures
retain explicit admission doubles; full radio state admission is exercised
by the lifecycle fixture above.

## Candidate hardware qualification

The new source has not yet been built, installed or released. The laboratory
still loads `685f3880`. Exact committed macOS tests, build, installation,
real sleep regression and publication must be recorded before treating this
candidate as delivered.

The first candidate build rejects direct calls to protected workloop methods.
The source now uses the public command gate wrappers; fixture workloop
methods have matching protected access. The failed build did not install
or activate a candidate and is retained as an integration failure.

Normal fast command completion before wait registration, full IWM command
ring serialization, raw BA and AP task lifetime, and direct device reset
callers are not closed by this state dispatcher change. Physical RFKILL on
the laboratory 9260 still prevents successful open, WPA2, WPA3, saved network,
DHCP, traffic and AP qualification; passing software fixtures is not on air
proof.
