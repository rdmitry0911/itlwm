# IWM retains command completion and drains senders before reset

IWM command acknowledgements now belong to an exact command slot until the
synchronous sender consumes completion. This repairs a lost wake that can
turn an acknowledged firmware command into a timeout during association,
activation or recovery. The queue also quarantines timed out slots and drains
senders before response, descriptor or DMA reclamation. On air joins and the
native GUI matrix remain separate hardware qualification.

## Reproduced early acknowledgement failure

The complete sender and ACK bodies from `e3e09a24` reproduce the failure for
inline and DMA commands on Linux and macOS. ACK runs before wait registration,
frees published DMA and decrements the queue, but the sender then sleeps and
returns ETIMEDOUT. Linux reports result 110; macOS reports 60. Both compile
and fail the intended result assertion with exit 134. IRQ delivery and timing
are explicit doubles; neither a response status nor a radio success is invented.

## Slot ownership and publication

The command leaf protects the physical ring tail, queue epoch, monotonically
increasing host serial and slot state. Publication requires a free slot, no
retained response or DMA, and current queue and radio admission. The complete
receipt remains serialized through descriptor construction and the MMIO
doorbell. Preparation uses local response and mbuf allocations and a temporary
DMA cursor before reserving ring storage. A nested producer during mapping
cannot overwrite its predecessor or share a permanent slot map.

The lock order is wait mutex, command leaf, selected BSS leaf, then scan leaf.
Allocation, mapping, NIC wake acquisition, sleep, free and callbacks stay outside
the command leaf. The wait mutex pairs terminal transitions with ordinary
msleep registration and serializes the 7000 family queue NIC wake transition.
Data queues retain their existing retirement model.

ACK validates queue, index, active slot epoch and the visible command code.
It changes a synchronous slot to COMPLETED before wakeup. The sender checks
its exact serial and epoch and consumes that retained state rather than treating
a wake or an empty queue as success. DMA detaches under the leaf and frees
outside it. Duplicate ACKs for already terminal slots, invalid indices and
different opcodes cannot decrement or free another command.

The firmware header does not carry the host serial. A same opcode duplicate
after physical slot reuse cannot be distinguished from the current response
by inventing a host token. Cross reset fencing therefore still requires the
real device stop and DMA reset boundary; serials protect host ownership, not
an unsupported on wire protocol extension.

## Timeout and reset

Without ACK, the sender marks its slot TIMED_OUT and leaves response and DMA
owned until a real late ACK or reset. Ring wrap cannot reuse that slot even
when other commands complete. Response copy checks slot identity and bounds
under the command leaf. A failed or malformed response retires its allocation;
the header ACK is not converted into a successful firmware status payload.

Stop closes admission, advances the queue epoch, marks outstanding slots
ABORTED and wakes waiters before draining every sender. Responses free only
after the sender count reaches zero. Device stop, command ring reset, command
ring free, partial attach failure and detach all participate. The queue lock
outlives rings and is destroyed after the IRQ event source is removed.

Firmware loading selects either DQA queue 0 or legacy queue 9 while stopped.
Admission opens only after the actual post alive setup and current radio owner
validation. Attach NVM bootstrap requires no UP or RUNNING interface and no
runtime init reference; runtime admission requires its actual init owner.
Neither a missing lock nor a stop race manufactures firmware readiness.

## Waiting with the recursive controller gate

A synchronous caller holding the main workloop gate releases its entire
recursive gate while waiting through the public command gate sleep method.
The wait mutex is released before gate sleep and reacquired only after the gate
returns. The retained slot predicate remains authoritative. A 10 millisecond
recheck bounds the registration gap, with one absolute two second command
deadline; a wake or an intermediate deadline cannot complete a command.

Queue drain uses the same gate aware wait so an Off caller cannot prevent a
sender from reacquiring the gate and retiring. These are public
[IOCommandGate API semantics](https://raw.githubusercontent.com/apple-oss-distributions/xnu/main/iokit/Kernel/IOCommandGate.cpp)
and [workloop sleep semantics](https://raw.githubusercontent.com/apple-oss-distributions/xnu/main/iokit/Kernel/IOWorkLoop.cpp),
not a recovered private BCM firmware queue algorithm. The separate lower TX
retirement principle is recorded in
`reference/AppleBCMWLAN_IWM_SAE_TX_COMPLETION_25C56_20260801.md`.

## Executable software qualification

The final source passes 31 full command slot scenarios and 16 real threaded
sender, ACK and stop scenarios on Linux and macOS with ASan and UBSan. Coverage
includes retained early ACK, normal ACK, spurious wake, wrong queue, index and
opcode, malformed response, timeout, late ACK, ring wrap, retained COMPLETED
storage, nested preparation, cursor allocation failure, legacy queue 9,
bootstrap and runtime admission, recursive gate restoration and bounded
no ACK timeout.

The threaded controls pause the complete sender after actual cancellation wake
or during local allocation and mapping. The complete stop body cannot reclaim
responses while that sender is live and finishes only after its actual leave.
Threads, mutexes and condition waits are real host primitives; gate scheduling,
IRQ delivery, DMA and firmware are still explicit doubles rather than kernel
or on air execution. Existing 37 IWM sender, cancellation, 62 IWX sender and
scan and state lifecycle controls remain green. The complete payload aggregate
passes on both Linux and macOS. The final macOS run also compiles the actual
historical sender and ACK and reaches the intended result assertion with exit
134; the aggregate wrapper completes with exit zero after checking that result.

## Loaded candidate and sleep regression

Production `eb91aee60e31d48f22bca36b0174d4240cea763a` is pushed and independently
verified on the branch remote. The exact committed macOS checkout passes the
complete payload aggregate and builds with all 1088 imports resolved against
the running 25C56 BootKC. Its tracked files are clean and source identity is
`fb332ca33f77`. The original dirty checkout and physical host `.22` stay untouched.

Private AuxKC admission, four preserved companions and both canonical and
candidate member sets pass. Activation `activation-20261010T100209Z` under
`/private/var/tmp/aiam-iwn-activation-iwm9260-cmdslot-eb91aee6-20261010` returns
zero and preserves rollback. An independent read verifies READY and full
installed bundle equality before one guarded guest reboot. The first postboot
SSH observation returns 255; the second verifies the new boot and exact image.
The same QEMU remains at PID 517226 without VM restart or overlay change.

- Loaded boot: `03D25669-683A-41E4-A4F9-C2299C42AC6C`.
- Mach-O UUID: `5F84C9FF-747A-3029-9502-B5C32CD5C50F`.
- Mach-O SHA256: `8aa54c41edca99b1774c323feee743f876ba138a6e06e9959ccab2b2b49285f2`.

Real S3 is independently confirmed by `paused (suspended)` and serial
`ACPI SLEEP`. Power history records 62 seconds of sleep and WakeTime 1.222
seconds in this boot. The WindowServer sleep notification timeout of 30 seconds
remains recorded. Private `system_wakeup` restores the same boot, exact loaded
image, full bundle equality and en2 management, with no VM restart.

The FBT trace finishes normally with exit zero and its COMPLETE marker. Initial
native Off executes device stop and command queue closure and returns success
in 663.646 milliseconds. Both observed queue stops return, in 16.392 and 13.024
microseconds. Four native Off and On controls after wake preserve Off readbacks
and en2. On returns `0xe00002d8` in 2.072, 2.218, 5.522 and 1.909 milliseconds.
The native utility exit zero is not radio success. RFKILL prevents command
sender or ACK execution in this runtime trace; the empty queue regression is
not qualification of a busy firmware ACK race or successful on air connection.

## Additional laboratory release

The archive contains the exact installed and loaded bundle without a packaging
rebuild. Build, installed and extracted bundles compare equal. SCP completes
with exit zero before the local independent size and SHA256 guard and upload.

- Asset: [AirportItlwm-Tahoe-Iwm-CommandSlots-eb91aee6.kext.zip](https://github.com/rdmitry0911/itlwm/releases/download/v2.4.0-alpha/AirportItlwm-Tahoe-Iwm-CommandSlots-eb91aee6.kext.zip).
- Asset ID: `627630660`; size: 15,716,627 bytes.
- ZIP SHA256: `6e3c893c68682ee86e2e44918c4abd95dac018ed5ae69b94795e8ee424dd3113`.

An independent final API read verifies all eleven older assets and their
immutable metadata unchanged, the full previous notes as an exact suffix,
and the default asset unchanged. This is an additional unsigned Debug LAB
asset, not a replacement default or a claim of native GUI joins, DHCP,
traffic, WPA3 or AP qualification on the 9260.

Evidence uses `command-slot-*` under the existing laboratory root. The first
historical adapter compile error was an unused mock clock warning, not a
behavioral negative. The first aggregate adapter error lacked the command stop
declaration in a data queue only retirement fixture; that fixture now rejects
any accidental command queue execution instead of supplying fake drain success.
Two preboot observation errors remain retained: an unprivileged shell could
not expand the protected activation glob, then a guard used the wrong summary
key. Neither replays activation; explicit paths and the actual activation_state
key independently reconcile the applied transaction before reboot.
