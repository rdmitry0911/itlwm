# Analysis report — 2026-07-11

## ANOMALY

- id: `CR-480-IOPM-STATE-WORD-PARITY`
- status: `CONFIRMED_DEVIATION`
- symptom: repeated radio OFF/ON previously reached a kernel panic in
  `WCLNetManager::ipStatusInWaitForIp` through the system-awake replay chain.
- first visible manifestation: the second radio ON delivered
  `DRIVER_AVAILABLE` while WCL replayed a stored system-awake state into
  `WAITING_FOR_IP`.
- expected system behavior: radio power and IOPM system power are independent
  producers. IOPM executes synchronously on the controller command gate,
  preserves a shared atomic state word, removes `IO80211WokeSystem` before a
  real OFF transition, emits the ordinary unavailable `0x37` carrier but no
  selector `1` on OFF, and emits the ordinary available `0x37` carrier followed
  by one selector `1` after ON resume. A private reporting owner is updated
  after the gated action only when that owner exists.
- actual behavior: the historical local IOPM implementation deferred ON/OFF to
  two `thread_call`s, manually acknowledged completion, initialized its state
  to ON, and emitted selector `1` on both edges. The first rejected alignment
  candidate removed those deviations but reduced the reference state word to
  a standalone ordinal, omitted its `0x30 == 0x20` gate and atomic semantics,
  omitted `IO80211WokeSystem` removal, and did not account for the conditional
  post-gate reporting owner.
- divergence point: local `AirportItlwm::setPowerState`,
  `setPowerStateGated`, `registerWithPolicyMaker`, and
  `performTahoeBootChipImage`, versus 25C56
  `AppleBCMWLANCore::setPowerState` at `0xffffff80015ec10c`,
  `setPowerStateGated` at `0xffffff80015ec346`, `bootChipImage` at
  `0xffffff8001573712`, `reportInitFailure` at `0xffffff8001564846`,
  `loadAndSetup` at `0xffffff8001575946`, and `watchdogFailed` at
  `0xffffff80015ecfcc`.
- evidence:
  - panic logs: three pre-fix panics share
    `ipStatusInWaitForIp <- systemAwakeNotificationHandler <-`
    `handleReplayNotification <- driverAvailableEventHandler`.
  - runtime logs: FBT proved radio transitions did not enter the IOPM methods.
    A preliminary combined candidate survived four radio cycles and a
    240-second traffic run, but that trace recorded zero actual IOPM OFF/ON
    calls and predates structural approval; it is corroboration only, not
    Stage 2 evidence.
  - decomp: the full 25C56 PM bodies and all writers touching the relevant
    `+0x2890` masks establish the exact state and side-effect contracts below.
- candidate causes:
  - the non-reference IOPM producer remains correlated with the replayed
    system-awake state;
  - sole causality is not claimed because the pre-fix system-power seeding
    lifecycle was not traced completely.
- rejected causes:
  - GUI login state: direct/private Apple80211 and XPC results are identical
    with a logged-in graphical session and at `loginwindow`;
  - radio-local selector `1`: direct FBT excludes it after the radio producer
    correction.
- confirmed deviation: local IOPM execution, state ownership, OFF cleanup,
  and message scope differ from the recovered reference contract.
- root cause: not established for this request; the patch is intentionally
  limited to a confirmed reference deviation.
- fix: implemented as the reference-aligned PM state-word and system-carrier
  lifecycle described below.
- verification: complete on clean-build UUID `8AFE24EC-4859-33BD-9E12-452F4DC24A90`.

## FIX_CANDIDATE

- anomaly_id: `CR-480-IOPM-STATE-WORD-PARITY`
- symptom: the driver has a confirmed non-reference system-PM producer adjacent
  to the WCL system-awake replay panic, but sole root causality is not proven.
- expected system behavior:
  - `setPowerState` synchronously invokes `setPowerStateGated` through the
    controller command gate and returns that result;
  - after the gate, reference conditionally calls
    `AppleBCMWLANIOReportingCore::reportSystemPowerState(...)` only when its
    private reporter owner exists;
  - the gated action reads one shared 32-bit state word, skips all transitions
    when `(word & 0x30) == 0x20`, and preserves every unrelated bit while
    atomically setting or clearing bit `0`;
  - a real OFF removes `IO80211WokeSystem` before clearing bit `0` and calling
    the system-off helper;
  - a real ON sets bit `0` before calling the system-on helper;
  - boot bit `0x10` is atomically held across `bootChipImage` and cleared on
    success and failure; `watchdogFailed` is the only recovered owner that sets
    permanent failure bit `0x20` together with `0x40`.
- actual behavior: the rejected local candidate uses a scalar 0/1 member and
  ordinary assignments, has no `0x30` gate, does not remove the wake property,
  does not project the already-existing local Tahoe boot owner into bit `0x10`,
  and leaves the reference post-gate reporting branch unexplained.
- exact divergence point:
  - local `AirportItlwmV2.cpp` PM and Tahoe boot functions named above;
  - reference instruction ranges `0xffffff80015ec10c..0xffffff80015ec6ef`,
    `0xffffff8001573712..0xffffff8001573cbf`,
    `0xffffff8001564846..0xffffff8001564a61`,
    `0xffffff8001575946..0xffffff8001575f5d`, and
    `0xffffff80015ecfcc..0xffffff80015ed05f`.
- evidence from runtime: the rejected candidate exposed the missing system
  `0x37` brackets as a reproducible post-wake radio-cycle panic. The corrected
  build then completed a real framework sleep/wake, four separate post-wake
  radio cycles, `240/240` ping, and 240 seconds of concurrent iperf3 without
  a fault; the exact FBT trace is retained in the PM runtime capture directory.
- evidence from decomp:
  - `setPowerState` calls command-gate `runAction` at
    `0xffffff80015ec1ad` and conditionally reports through the owner at core
    expansion `+0x1580` at `0xffffff80015ec1d6..0xffffff80015ec202`;
  - `setPowerStateGated` checks `(state & 0x30) == 0x20`, uses
    `OSBitOrAtomic(1, ...)` and `OSBitAndAtomic(0xfffffffe, ...)`, and calls
    `removePropertyHelper(this, "IO80211WokeSystem")` before OFF. The BootKC
    export identifies `0xffffff800226c009` exactly as
    `removePropertyHelper(IOService*, char const*)`;
  - `bootChipImage` atomically sets `0x10` and clears it at both success and
    failure exits; `reportInitFailure` also clears `0x10`;
  - `watchdogFailed` atomically sets `0x40` and then `0x20`, signals readiness,
    halts, and returns `0xe00002bc`; `loadAndSetup` clears `0x40` on success.
    Exhaustive atomic-AND writer review finds no clear of `0x20`, so it is the
    persistent transition-blocking state.
- exact semantic mismatch between reference and our code: a scalar ordinal
  loses shared-word gating, boot ownership, atomic ordering, and preserved-bit
  semantics; the OFF edge omits a reference registry cleanup; and the
  conditional reporter branch was not classified.
- fix justification path: `REFERENCE_ALIGNMENT_FIX`
- why this is root cause and not just correlation: it is not claimed as the
  proven sole root cause. The protocol permits a patch at
  `CONFIRMED_DEVIATION` scope; the change removes only directly recovered
  producer mismatches and does not alter the WCL consumer.
- why proposed fix is 1:1 with reference architecture and semantics:
  - replace the scalar PM ordinal with a volatile 32-bit flag word;
  - atomically own bit `0` in the gated action while preserving all other bits;
  - implement the exact `0x30 == 0x20` transition block;
  - project the existing local `performTahoeBootChipImage` owner into atomic
    bit `0x10` and clear it on both exits;
  - remove `IO80211WokeSystem` before OFF;
  - retain system-ON-only selector `1` and synchronous command-gate execution;
  - do not synthesize bits `0x20/0x40`: the local Intel path has no equivalent
    Broadcom `powerOnSystem` retry-exhaustion/`watchdogFailed` owner. The word
    and gate preserve those bits if an exact owner is implemented later;
  - do not invent an IOReporting substitute. `AirportItlwm` has generic
    inherited IOReport entry points but no
    `AppleBCMWLANIOReportingCore`-equivalent owner or field. Therefore the
    reference `owner == NULL` branch is the applicable local path.
- implemented files/functions:
  - `AirportItlwm/AirportItlwmV2.hpp`: replace the scalar with the shared flag
    word and name its recovered masks;
  - `AirportItlwm/AirportItlwmV2.cpp`: align boot-bit ownership,
    `setPowerState`, `setPowerStateGated`, PM snapshot/output checks,
    wake-property removal, and initial state;
  - `scripts/state_machine_closure_report.py`: require the exact gate,
    atomics, property cleanup, boot-bit lifecycle, and reporter-owner
    non-applicability witness;
  - tracked reference and discrepancy documentation: record the recovered
    state-word owner map and the intentionally narrow claim.
- forbidden alternative fixes considered and rejected:
  - delay, retry, replay, or reorder around `DRIVER_AVAILABLE`;
  - WCL-side suppression or forced `WAITING_FOR_IP` state correction;
  - a fake reporting callback or generic IOReporter substituted for the absent
    private Broadcom owner;
  - synthetic permanent-failure bits without a matching local
    `watchdogFailed` lifecycle;
  - deriving system PM state from association, GUI login, or IP state.
- completed verification:
  - the clean Tahoe build resolved `959/959` BootKC symbols, was installed by
    AuxKC rebuild and rebooted as UUID `8AFE24EC-4859-33BD-9E12-452F4DC24A90`;
  - a real `pmset sleepnow`/QEMU wake recorded synchronous gated OFF/ON entries,
    OFF property removal, system unavailable/available `0x37` carriers, zero
    selector `1` posts on OFF, and exactly one after ON;
  - four radio cycles remained separate from IOPM, and the fresh concurrent
    stress reached `240/240` ping plus 240-second iperf3 (`572 MBytes` at
    `20.0 Mbits/sec`) without a panic or stall.

## Preliminary pre-Stage-1 corroboration — not final verification

The rejected combined candidate built and ran before structural approval. It
loaded as UUID `66639ECC-6348-3910-8752-2462A991FDA1`, survived four radio
OFF/ON cycles, restored `10.77.0.47` each time, and completed a 240-second
traffic run with ping `240/240`, zero loss, and iperf3 at `20.0 Mbit/s`.
Those facts show that removing the historical asynchronous/symmetric PM shape
did not destabilize the radio-only path. They do not exercise the modified
IOPM state machine and cannot satisfy Stage 2 for a future approved diff.
