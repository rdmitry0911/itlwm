# IWM and IWX scan abort waits and deferred BTM dispatch

IWM and IWX now wait for scan abort completion without holding the recursive
controller gate needed by the physical terminal. The shared BTM RX path also
defers both scan start and abort to its ordinary timer callout, so it cannot
block the IRQ thread that must deliver their ACKs and terminals. These changes
address scan replacement during user joins and roaming. Successful GUI joins,
DHCP, traffic and on air WPA3 remain separate hardware qualification.

## Reproduced failures

The complete abort waiter and terminal owners from `281f2d51` reproduce two
failures in each MVM family on Linux and macOS. A gated caller times out while
the same workloop terminal cannot acquire its gate. An ordinary caller also
times out immediately after one spurious wake rather than waiting for its real
terminal. Both retain a live quarantined scan. The four historical controls
compile and fail their intended assertions with exit 134 on each platform.

The complete historical BTM RX parser and dispatch bodies reproduce a separate
scheduling failure. With no old census, RX directly submits a synchronous
start. With an old census, RX directly submits its synchronous abort. The IRQ
thread double cannot deliver the response while executing those calls. Both
historical controls compile and fail the no blocking lower command assertion
with exit 134 on Linux and macOS. Current request helpers remain in these two
dispatch controls; they do not substitute a firmware result.

## Retained terminal and cancellation

The abort waiter rechecks its exact serial, hardware generation, admission and
SHUTDOWN after every wake against one absolute one second deadline. Gated
callers release the wait mutex before public commandSleep, then reacquire it
after full recursive gate restoration. Ten millisecond slices are capped at
the hard deadline. A wake, abort status or slice timeout never counts as a
physical terminal. Ordinary callers retain the same wait mutex registration.

Exact terminal retirement publishes ordinary and public command gate wakeup
after releasing the scan leaf. Reset invalidation takes the same wait mutex
before the leaf, invalidates the real owners and clears the exact pending
waiter before both wakes. Upper callbacks run after both locks are released.
Reset and shutdown return ENXIO; an actual terminal retained at the deadline
returns success. A real timeout quarantines the old serial until device reset,
without clearing another scan's waiter or reusing an unretired UMAC UID.

The existing reference contracts keep command submission distinct from
physical scan retirement:
[25C56 scan to AP handoff](reference/IWX_APSTA_BOUNDED_FOREGROUND_SCAN_HANDOFF_RUNTIME_25C56_20260805.md)
and [25C56 scan to SAE handoff](reference/IWN_SAE_WCL_SCAN_HANDOFF_RUNTIME_25C56_20260803.md).
The latter's completed lower owner may enter association before its upper
callback returns; successful command submission alone does not authorize that
handoff.

## RX and callout ownership

Releasing a gate does not let a blocked workloop thread execute another IRQ.
RX therefore records the protected BTM target, marks the previous census
ineligible and schedules the existing retry timer before returning. CTimeout
uses the default IOTimerEventSource factory, whose kernel callout acquires the
workloop gate from an external thread, rather than workloop thread delivery.
The public implementation distinguishes those timer options in
[IOTimerEventSource](https://raw.githubusercontent.com/apple-oss-distributions/xnu/main/iokit/Kernel/IOTimerEventSource.cpp).

The callout aborts an existing background scan before admitting its replacement.
IWN's asynchronous abort acceptance leaves BGSCAN live, so it retries only
after physical retirement. MVM's exact terminal wait may clear that ownership
before the call returns. Busy admission retains the existing bounded retry
policy; errors reject only the still current BTM request.

Each accepted BTM gets a monotonically allocated nonzero host request
generation under the selected BSS leaf. It is not a firmware or wire token.
After a lower command releases and reacquires the gate, old work cannot clear,
publish or charge retries to a same target replacement. Generic BGSCAN and
fresh result preparation precede the new command; an early terminal cannot
be followed by stale BGSCAN publication or deletion of newly observed nodes.
Failed admission restores the prior scan flags and current retry owner.

## Software qualification

Linux and macOS ASan and UBSan gates pass with complete extracted production
bodies. The full payload aggregate and shared WNM source contract also pass.

- Forty three abort deadline and reset scenarios per MVM family cover early
  terminal, spurious and dropped wakes, deadline races, wrong physical kind,
  UMAC UID mismatch, reset, shutdown, generation change and another serial.
- Twenty four complete RX parser and timer dispatch scenarios cover idle and
  busy census, synchronous and asynchronous abort, lower failures, retry
  exhaustion, timer failure, cancellation, same target replacement, generation
  wrap, malformed admission and early terminal publication.

Firmware results, DMA, kernel gate and IRQ scheduling remain explicit doubles.
The abort controls publish through the actual scan lease instead of treating
an invented abort status as terminal evidence. LMAC has no UMAC UID, so UID
mismatch rejection is tested only for UMAC. These fixtures do not qualify
physical firmware scan cancellation or an on air BTM transition.

The Mac WIP source archive is
`d3594ba48ae8241d826212de72ed789f7d0d7c54ccbbdfc739bf16efbef51cc4`.
Evidence resides under `scratch/iwm-9260-runtime-20261009.mo5CXe` in the aiam
workspace, with the `scan-abort-*` prefix. The current passthrough IWM 9260
reports hardware RFKILL; neither software tests nor a loaded kext bypass it.
