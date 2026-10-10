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

## Exact committed build and loaded regression

Production `b277d8bcdce9dacee5bf652acc0fa6909630f614` is pushed and independently
matched to the remote branch. Exact committed Linux and Mac payload aggregates
pass. The Tahoe 25C56 kernel build succeeds and all 1088 imports resolve against
the running guest's `/System/Library/KernelCollections/BootKernelExtensions.kc`.
The first bundle fetch names a branch absent from that HEAD-only bundle and
returns 128 before checkout or build; importing its verified HEAD reconciles
that artifact error. The original dirty guest checkout and physical host
10.90.10.22 remain untouched.

The new private AuxKC transaction preserves four companion members and rollback,
returns zero and reaches READY. A readiness lookup initially names `summary.txt`
instead of `activation-summary.txt` and stops before reboot. The resolved exact
READY guard and complete installed bundle equality pass before one guest reboot.
Neither installation nor reboot is replayed. The first postboot SSH observation
has a banner timeout; the second verifies the new boot and exact loaded image.

- Source identity: `04ae339d80a8`.
- Boot: `51F5933F-FA18-4D7E-833A-1CD96C9AA698`.
- Mach-O UUID: `F171BB53-E77E-3D73-B327-D0267671046B`.
- Mach-O SHA256: `cd53ad2c5d85f2e96477ca2863874598e93dc7b8515ef7bbc217fa49530ada7e`.

Two private monitor observations confirm paused suspended state, and serial
records ACPI SLEEP and S3 WAKE. Power history records 112 seconds of real sleep
and WakeTime 1.359 seconds. Private system_wakeup restores the same boot, image,
complete installed bundle and en2 management without VM restart or overlay change.
The first wake SSH observation returns 255; the second completes with zero.

The S3 SSH trace observer times out and returns 255. An independent postwake
process inspection finds that diagnostic no longer running; its initial trace
records successful IWM disable in 700.558 milliseconds, actual scan reset
invalidation in 6.257 microseconds and queue stops in 12.613 and 26.082
microseconds. This is not a complete successful S3 trace.

A separate postwake trace logs inside the guest and completes with its COMPLETE
marker, remote DTrace status zero and observer exit zero. Four native Off and On
controls complete with zero, retain Off readbacks and en2, and return NotReady
for On in 1.954, 1.855, 1.950 and 1.791 milliseconds. Utility exit zero is not
radio success. No actual scan abort wait or BTM receive occurs under RFKILL;
this regression does not qualify the changed physical scan or BTM path.

## Additional laboratory release

The archive is the exact installed and loaded bundle, without rebuilding it.
Build, installed and extracted bundles compare equal. Package and SCP complete
with zero before independent local SHA256 and size validation.

- Asset: [AirportItlwm-Tahoe-ScanAbort-b277d8bc.kext.zip](https://github.com/rdmitry0911/itlwm/releases/download/v2.4.0-alpha/AirportItlwm-Tahoe-ScanAbort-b277d8bc.kext.zip).
- Asset ID: `627798740`; size: 15,717,473 bytes.
- ZIP SHA256: `dca761084dc11cffd42b6ec6acfd3d3c3c3ac2c8f901996b26eac434b620449a`.

A fresh final API read verifies all thirteen older assets and their immutable
metadata unchanged, the default unchanged and complete previous notes preserved
as an exact suffix. This additional unsigned Debug LAB artifact explicitly
retains the RFKILL and lack of physical scan, BTM and IWX qualification limits.
