# IWM security admission before the first ready consumer

IWM now reopens software PMF, management TX and driver resident SAE after
hardware initialization but before `IFF_RUNNING`, monitor RUN or the first
SCAN can expose a ready consumer. Each reopen belongs to the exact live
firmware init generation. This addresses fast saved network joins and
recovery paths that previously reached AUTH while security admission was
still closed. Successful on air behavior remains a separate qualification.

## Reproduced ordering and ownership failures

The complete historical IWM init from `046bf991` compiles, then fails the
security before ready assertion with exit 134. The ready producer and
security operations in this fixture are explicit doubles; the complete init
body establishes their actual caller ordering, not hardware behavior.

The separate security fixture executes complete production PMF and TX reopen,
engine reopen and stop, callback lease operations, hook publication and radio
init and stop ownership helpers. Six historical controls pass: ordinary
reopen, detach, cancelled owner retirement, disabled runtime, stop during
callback drain and stop after the first engine claim. Thirteen historical
negative controls compile, then fail their intended assertions with exit 134:
shutdown, stale generation and missing init owner for each of PMF, TX and SAE;
shutdown before publication and between its two token checks; stop before
the engine claim; and publication without the radio lifecycle lock.
These historical controls and negatives reproduce on both Linux and macOS,
as does the complete historical init ordering failure.

Stop ownership is acquired through the real `iwm_radio_stop_begin`, rather
than setting a test flag. The shutdown controls represent stop waiting for
init to leave before the device and security teardown have reached their
later close operations. A reopen must reject that interval too.

## Lower driver ownership change

`iwm_init` opens PMF, TX and SAE with its saved hardware generation before
the first ready producer can run, then revalidates the init owner before
publishing RUN or SCAN. Monitor uses the same boundary. The old late reopen
calls after the first scan wait are removed.

The locked owner predicate requires primary UP, no detach or SHUTDOWN, one
init reference, no stop reference and an exact hardware generation. PMF and
TX reopen hold the sleeping lifecycle lock before their security leaves.
Engine reopen makes the same atomic claim before clearing stopping, after
callback drain and at its final owner snapshot. Drain remains outside all
locks. Final hook publication retains the lifecycle lock across the generic
writer leaf, closing the gap between the final claim and publication; no
callback or sleep occurs under those leaves.

Hook publication also rejects SHUTDOWN at both existing token checks. A
disabled crypto runtime remains a normal unavailable SAE path, not a new
failure of ordinary open or WPA2 initialization. Cancelled active owners
retain their actual AUTH tombstone and schedule retirement without exposing
the complete live hook set. No runtime capability, RFKILL or ready result is
fabricated.

The Tahoe reference places SAE support, PMK delivery and reassociation in
the lower owner, as described in
`docs/reference/TAHOE_IWM_DRIVER_RESIDENT_SAE_SOFTWARE_PMF_20260801.md`.
This change repairs Intel lifecycle admission; it does not copy BCM private
selectors into an Intel firmware ABI or move security drains into scan workers.

## Executable verification scope

All nineteen security controls pass ASan and UBSan on Linux and macOS.
Kernel locks, runtime capability, cancellation payload, task scheduling and
callback addresses are explicit doubles. Hook publication itself is real:
all eight hook fields are stored atomically under the generic writer leaf.
Callbacks, firmware, DMA and packet transmission do not execute in this
fixture. Lock doubles reject sleeping lock acquisition under a security leaf.

The full init and ready fixtures pass thirteen controls for each of IWM and
IWX on both platforms, including security before ready. Their owner and
security doubles remain separate from the real security helper fixture.
The seven full IWM init and stop controls also pass on both platforms.

The source PMF contract now follows the complete actual init body, its
generation arguments and `ieee80211_begin_scan` producer, and checks the
atomic PMF owner claim. Its first outdated signature and explicit SCAN token
failures are retained in logs rather than counted as behavioral negatives.
The final Linux payload aggregate and physical scan, standard scan, power
off link down, resident IWM SAE owner, unexpected AP reset replay and software
PMF contracts pass. The broader q0 producer surface remains separate.

## Loaded candidate and real sleep regression

Production `a1c778a3da770cc5bfdaba0f32c72a91d9304a70` is pushed, built,
transactionally installed and loaded in the existing IWM 9260 guest on SSH
3338. Source identity is `3909dd8568a9`; all 1088 BootKC imports resolve.
Private AuxKC admission and exact build and installed bundle equality pass.
Four companion kexts and rollback material remain under
`/private/var/tmp/aiam-iwn-activation-iwm9260-securitya1c778a3-20261010`.
Activation is `activation-20261010T073827Z`.

- Loaded UUID: `3562DB7F-EC82-3BB6-8317-DDFC43BC3426`.
- Mach-O SHA256: `a058ded6acf83bd1bc10b751886d9333bf6bf82551b3993c43376a0fe59e7cfb`.
- Boot: `FC32DD4D-3B6C-4EF3-8263-17C7BB4FFEE4`.

The same private monitor and overlay remain in use. The guarded guest reboot
disconnects SSH with exit 255 as expected; subsequent observation retains
temporary banner timeouts before verifying the new boot and exact image.
No QEMU restart or physical host `.22` access occurs.

Actual S3 is independently confirmed by `paused (suspended)` and serial
`ACPI SLEEP`. The power history records one 38 second sleep in this boot,
with WakeTime 1.331 seconds. Private `system_wakeup` restores the same boot,
UUID, installed hash, bundle equality and management default through en2.
The earlier sleep history in the same log belongs to earlier boots.

The bounded FBT trace starts with a ready marker before the sleep request
and exits normally. It observes real Off, stop ownership, drain with self
counts 0 and 1, device erase and successful Off completion in about 660 ms.
After wake, four native Off and On controls retain Off readbacks and en2
management. Exact inner POWER probes show On refusal `0xe00002d8` in 1.952,
1.961, 1.812 and 1.864 ms. Native process exit zero is not radio success.
RFKILL prevents successful init ownership and all three security reopen
entries during this hardware trace; the executable helper results above
must not be represented as hardware execution.

Evidence in the current laboratory root uses `security-reopen-*`: final
Linux aggregate and adjacent logs, WIP and candidate macOS checks, build,
activation, loaded image, sleep monitor, wake image, native controls, trace,
power history and installed archive verification. Initial static token
failures and transient reboot and wake observation failures remain retained.

## Separate LAB release artifact

Release `v2.4.0-alpha` has additional asset `627345255`,
`AirportItlwm-Tahoe-Iwm-SecurityReady-a1c778a3.kext.zip`, 15,713,572 bytes,
SHA256 `77788e71fbb42a8095a5fcf8664c9081a5c7d2355f7a6e00027a40a2ce327669`.
The archive is the exact installed unsigned Debug bundle; extraction matches
the complete installed bundle and loaded Mach-O. SCP completed before the
local size and hash guard and upload, without the earlier transfer race.

An independent API read verifies the new asset and all eight older assets
unchanged, including labels, digests, sizes and immutable metadata. A further
independent read verifies the new bounded notes and the complete preceding
notes byte for byte as the suffix. The default archive remains unchanged.
The new label and notes retain LAB ONLY, RFKILL and missing on air proof.

## Operational qualification boundary

The 9260 is hardware RFKILL blocked, so the loaded image, real S3 and bounded
native refusal regression do not establish a successful security reopen,
GUI join, open or WPA2 or WPA3 association, DHCP, traffic or operational AP.

The highest operational priority remains the repeated GUI and saved network
matrix, including recovery without an Off and On workaround, once hardware
radio admission is available. IWM dequeued workers and command producers,
deferred SAE hook retirement and remaining IWX enqueue paths retain separate
qualification; this change is not full q0 or driver equivalence.
