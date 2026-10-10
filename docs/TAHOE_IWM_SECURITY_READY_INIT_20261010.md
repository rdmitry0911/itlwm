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

## Laboratory qualification boundary

The laboratory is the existing IWM 9260 guest on SSH 3338, using the private
monitor and unchanged overlay. Physical host `.22` remains out of scope.
The 9260 is hardware RFKILL blocked, so loaded image, real S3 and bounded
native refusal regression do not establish a successful security reopen,
GUI join, open or WPA2 or WPA3 association, DHCP, traffic or operational AP.
The new source candidate has not yet been built or loaded at this checkpoint.

The highest operational priority remains the repeated GUI and saved network
matrix, including recovery without an Off and On workaround, once hardware
radio admission is available. IWM dequeued workers and command producers,
deferred SAE hook retirement and remaining IWX enqueue paths retain separate
qualification; this change is not full q0 or driver equivalence.
