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
and exact committed macOS source `e3e09a24`. The two gated controls execute a callback
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
IWX lifecycle controls also pass on that committed macOS tree. Thirteen
full init and ready controls per family and both full failure suites pass.
State request fixtures
retain explicit admission doubles; full radio state admission is exercised
by the lifecycle fixture above.

## Loaded candidate and real sleep regression

Production `48cd5833` and its public API correction
`e3e09a246ec4dbddc8a1481c85107104b0a391fb` are pushed. The exact committed
macOS tree passes the tests above and builds with all 1088 undefined symbols
resolved against the running 25C56 BootKC. Source identity is
`71e8a8609892`. The isolated guest checkout is tracked clean; the original
dirty checkout and physical host `.22` remain untouched.

The first candidate build rejects direct calls to protected workloop methods.
The source now uses the public command gate wrappers; fixture workloop
methods have matching protected access. The failed build did not install
or activate a candidate and is retained as an integration failure.

Private AuxKC admission and both canonical and candidate member sets pass.
Four companion kexts and rollback material remain under
`/private/var/tmp/aiam-iwn-activation-iwm9260-statedraine3e09a24-20261010`.
Activation is `activation-20261010T091058Z`; the installer and independent
bundle reconciliation return zero. A guarded guest only reboot returns
SSH 255. The same QEMU survives three temporary banner timeouts before
observation verifies the new boot and exact image; no VM restart or overlay
change occurs.

- Loaded boot: `F8F77FB6-5654-43CC-9AB7-D7B54DD1D52F`.
- Mach-O UUID: `4466C18E-5470-3F5B-B6F2-F170F246F5A8`.
- Mach-O SHA256: `943e3b89d3c32b024e505a7df3b92d28d9e76ab4c971f046dc10ddfa7eaa28fb`.

Real S3 is confirmed independently by `paused (suspended)` and serial
`ACPI SLEEP`. Power history records one 68 second sleep in this boot and
WakeTime 1.409 seconds; the WindowServer sleep notification timeout of
30 seconds remains recorded. The suspend observation wrapper returns one
despite its suspended state line, so it is not counted as a clean process
pass. A second private monitor observation confirms suspension before
`system_wakeup`; the same boot, image, full installed bundle and en2 management
then verify independently. One post wake banner timeout is retained.

The FBT trace starts before native Off and S3 and finishes normally with
explicit remote DTrace and local SSH exit zero. Actual initial Off follows
stop ownership, command cancellation, drain with self counts 0 and 1,
device erasure and successful completion in about 680 milliseconds. Four
native Off and On controls after wake retain Off readbacks and en2. On
returns `0xe00002d8` in 2.014, 1.999, 1.835 and 1.855 milliseconds. Native
process exit zero is not radio success. RFKILL prevents any admitted state
body or full firmware init in this trace; the software race fix is not
relabeled as a hardware AUTH execution result.

Evidence in the laboratory root uses `state-worker-*`: historical and final
Linux and macOS fixtures, exact build, activation and loaded image,
native controls, serial S3 events, monitor wake, completed trace and package.
The original unused mock and state fixture adapter compile errors remain
integration errors, not behavioral negatives.

## Additional laboratory release

The archive is the exact installed and loaded bundle without a packaging
rebuild. Build, installed and extracted bundles compare equal. SCP finishes
before the independent local size and hash guard and upload.

- Asset: [AirportItlwm-Tahoe-Iwm-StateDrain-e3e09a24.kext.zip](https://github.com/rdmitry0911/itlwm/releases/download/v2.4.0-alpha/AirportItlwm-Tahoe-Iwm-StateDrain-e3e09a24.kext.zip).
- Asset ID: `627527708`; size: 15,714,244 bytes.
- ZIP SHA256: `ab7e8276dacfccd4946aca9a6571ab30ffca6cbfdf07c9d2df474009d77d88b5`.

An independent release API read verifies all ten older assets and their
immutable metadata unchanged, the complete previous notes as a byte equal
suffix, and the unchanged default artifact. The label and notes retain the
unsigned Debug LAB scope and the absence of on air qualification. IWX
production is unchanged; its fixtures are regression coverage, not a fresh
IWX hardware result.

## Remaining command and hardware scope

Normal fast command completion before wait registration, full IWM command
ring serialization, raw BA and AP task lifetime, and direct device reset
callers are not closed by this state dispatcher change. Physical RFKILL on
the laboratory 9260 still prevents successful open, WPA2, WPA3, saved network,
DHCP, traffic and AP qualification; passing software fixtures is not on air
proof.
