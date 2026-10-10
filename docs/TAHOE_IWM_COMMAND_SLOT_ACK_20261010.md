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

## Runtime qualification pending

The candidate has not yet been built, installed or loaded. The laboratory
still runs production `e3e09a24`, with the previously qualified real S3 and
RFKILL refusal regression. Source fixtures alone do not close native GUI
joins, DHCP, traffic, WPA3 or AP on the newly installed 9260.

Evidence uses `command-slot-*` under the existing laboratory root. The first
historical adapter compile error was an unused mock clock warning, not a
behavioral negative. The first aggregate adapter error lacked the command stop
declaration in a data queue only retirement fixture; that fixture now rejects
any accidental command queue execution instead of supplying fake drain success.
