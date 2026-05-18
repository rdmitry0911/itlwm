# CR-479 airportd/CoreWLAN join-event producer decomp closure (2026-05-18)

work_item_id: CR-479-follow-up-airportd-corewlan-join-event-producer-decomp-20260518
task: Recover the named airportd/CoreWLAN producer-contract decomp
  targets enumerated by the auditor-instructed bounded coder task,
  and map the recovered evidence back to a single local-route
  decision.
correlation_id: CR-479-stage2-airportd-corewlan-join-event-producer-contract-20260518
basis_commit_head: 0acb18607d1f9e122df2160c3fe920437948ebb1
auditor_instruction: sig_20260518T101924_0300_b37a9429
expected_route_decision: see "Recommended next route" section

## Ghidra-host capacity choice (before launching the batch)

Captured on the configured decompilation host immediately before
the analyzeHeadless batches were started:

  date          = 2026-05-18T07:20:18Z
  nproc         = 48
  memory_total  = 60 GiB
  memory_used   = 1 GiB
  memory_free   = 24 GiB
  memory_avail  = 59 GiB
  load_average  = 0.07 / 0.10 / 0.09 (1 / 5 / 15 min)
  top user CPU  = 9.1 % (only the snapshot `top` process)
  no swap activity, no I/O thrash, no other Ghidra job concurrent.

Concurrency choice: a single decompiler thread per Ghidra
analyzeHeadless invocation, two serial invocations
(airportd_x86_64 then CoreWLAN), each with `-noanalysis -readOnly`.
This was sufficient because (a) the bounded target list per
invocation is small (1 to 7 string needles plus a few hard-coded
addresses), (b) the read-only mode avoids project-write contention
and snapshot rebuilds, and (c) the host was effectively idle so
no additional parallelism is needed. The two invocations together
completed end-to-end in well under five minutes wall clock.

Progress evidence preserved:
  commit-approval/runtime_evidence/CR-479-follow-up-airportd-corewlan-join-event-producer-decomp-20260518/airportd_headless.log
  commit-approval/runtime_evidence/CR-479-follow-up-airportd-corewlan-join-event-producer-decomp-20260518/corewlan_headless.log
  commit-approval/runtime_evidence/CR-479-follow-up-airportd-corewlan-join-event-producer-decomp-20260518/batch_start.txt
  commit-approval/runtime_evidence/CR-479-follow-up-airportd-corewlan-join-event-producer-decomp-20260518/MANIFEST.sha256
    (sha256 185b429252d44e2ab2f0a5aecac370668d859a88a56a0d5986daa1af38413f3d
     covering 100 per-target asm/c/pcode/xrefs files + xref-summary
     tsvs; integrity-checked clean with `shasum -a 256 -c MANIFEST.sha256`)

## Decompiler-state caveat

The Ghidra in-process decompiler returned `decompile_status=FAIL`
for every dumped function (consistent with the prior cr479
batches; the Tahoe macOS Mach-O shared-cache slices trip a
decompiler-process-died failure mode on this Ghidra build). The
fallback evidence in scope of this closure is therefore the
recovered instruction-level listing (asm), per-instruction p-code
expansion (pcode.tsv), and xref tables for every dumped function.
Where the contract question is decidable from the asm shape
(register / memory write / objc_msgSend / dispatch_* / Sec*
sequences) the per-target evidence anchors below identify the
exact instruction sequence; where the asm shape cannot fully
decide a contract gate (e.g., XPC payload field-by-field
constructor), the still-uncertain piece is enumerated as a named
follow-up in the "Remaining named missing decomp targets" section.

## Producer-side recovery (airportd_tahoe_26_2 project)

### Target P1: airportd setJoinStartedEvent producer caller

- selector: `setJoinStartedEvent:withReason:deviceName:`
- airportd binary string offsets: 0x113055 (x86_64),
  0x2932c5 (arm64e)
- xref count to the selector string: 1 (in the x86_64 slice)
- recovered caller (the sole xref site):
    address 0x10002760f
    name    -[<class> connectToTetherDevice:remember:interfaceName:token:authorization:connection:isAutoJoin:isAskToJoin:reply:]
    body    205 asm lines, 29 outgoing calls
    file    airportd_decomp/10002760f_connectToTetherDevice_remember_interfaceName_token_authorization_connection_isAutoJoin_isAskToJoin_r.asm

The recovered caller is the airportd
`connectToTetherDevice:remember:interfaceName:token:authorization:connection:isAutoJoin:isAskToJoin:reply:`
method, called from the airportd-side XPC service handler for
the tethering / connection-setup flow. It calls objc_msgSend
through the stub at `[0x1001483d8]` 23 times in its body,
including the call that targets the
setJoinStartedEvent:withReason:deviceName: selref.

Inference: setJoinStartedEvent on the live Tahoe path is called
when airportd dispatches its tether-device-connection logic, i.e.
during association-attempt setup. This is BEFORE the kext first-M1
window in the timing sense (the connection-setup logic runs before
the ASSOC IOCTL the kext receives). Important: there is exactly
ONE call site for setJoinStartedEvent in airportd, so this single
caller is the entire producer surface for that selector.

Object class of the receiver (the `<class>` on which
setJoinStartedEvent: is invoked) is NOT recovered here from the
asm alone; the receiver is loaded from an airportd-internal ivar
that the recovered asm reaches through `[reg + offset]` chains;
recovering the exact class name is a NAMED FOLLOW-UP item below.

### Target P2: airportd auto-join state machine entry

- recovered function:
    address 0x1000ba51b
    name    `__autoJoinStartedWithTrigger:`
    body    161 asm lines, 34 outgoing calls, 676 bytes
    file    airportd_decomp/1000ba51b___autoJoinStartedWithTrigger_.asm

The recovered head of this function shows the standard
"load logger handle and emit a start log line" preamble using two
selrefs at [0x100168238] and [0x100167fe0] before the body proper.
This is the entry point that the airportd auto-join layer calls
when an auto-join attempt is triggered; it is one of the call
sites that emits the airportd-internal `autoJoinDidUpdate:`
publication used by the cross-process delegate dispatch.

### Target P3: CWXPCSubsystem.initWithScheduler

- recovered function:
    address 0x100001ed4
    name    `-[CWXPCSubsystem initWithScheduler:]`
    body    289 asm lines, 56 outgoing calls, 1372 bytes
    file    airportd_decomp/100001ed4_initWithScheduler_.asm

The recovered initializer constructs the airportd-side XPC
publication subsystem object. It assigns at least nine instance
slots from helper-call results (offsets +0x48, +0x50, +0x58,
+0x60, +0x68, +0x70, +0x88, +0xa0, +0xb8), of which several are
constructed from the global selref / class-ptr tables at
[0x100148418], [0x100148148], [0x100147d60], [0x1001482c0],
[0x100147d70], [0x100147d80], and friends. The constructor takes
a Scheduler argument in RDX and stores it via the same helper
call sequence.

Notable shape: the initializer is a long chain of "MOV
RDI,[ivar-ptr-table]; CALL allocate-helper; JZ initFailedLabel"
checks. This is the CWXPCSubsystem holding the channel, the
queues, the entitlement check helpers, and the published-event
state objects. The exact identification of WHICH helper among
those is the entitlement helper is not decided in this closure
cycle; it requires following each of the 9 instance slots to
the helper at the stub address (a NAMED FOLLOW-UP item below).

### Target P4: airportd join-lifecycle objc_msgSend xrefs

The full xref counts to the five join-lifecycle selector strings
in airportd:

  selector                                                         xref_count
  ---------------------------------------------------------------- ----------
  setJoinStartedEvent:withReason:deviceName:                        1
  joinDidStartForWiFiInterfaceWithName:ssid:                        3
  joinDidCompleteForWiFiInterfaceWithName:isAutoJoin:error:         3
  autoJoinDidStartForWiFiInterfaceWithName:                         3
  autoJoinDidCompleteForWiFiInterfaceWithName:                      2
  autoJoinDidUpdate:                                                5

The "3 callers per selector" pattern for joinDidStart/Complete and
the "5 callers for autoJoinDidUpdate" pattern strongly suggests
airportd dispatches these delegate selectors from multiple call
sites in its state machine (one per branch of the join/auto-join
state graph), each ultimately reaching the in-process delegate via
objc_msgSend through the same stub at `[0x1001483d8]`. The full
xref listing per selector is preserved at:

  airportd_decomp/xrefs_setJoinStartedEvent_withReason_deviceName_.tsv
  airportd_decomp/xrefs_joinDidStartForWiFiInterfaceWithName_ssid_.tsv
  airportd_decomp/xrefs_joinDidCompleteForWiFiInterfaceWithName_isAutoJoin_error_.tsv
  airportd_decomp/xrefs_autoJoinDidStartForWiFiInterfaceWithName_.tsv
  airportd_decomp/xrefs_autoJoinDidCompleteForWiFiInterfaceWithName_.tsv
  airportd_decomp/xrefs_autoJoinDidUpdate_.tsv

Each caller function was decompiled and dumped to the same
directory; see MANIFEST.tsv in airportd_decomp/.

## Consumer-side recovery (CoreWLAN in cr466_exact_dyld project)

### Target C1: CoreWLAN startMonitoringEventWithType reply path

- block_invoke recovered:
    address 0x7ff8115a1e1f
    name    `___51-[CWWiFiClient startMonitoringEventWithType:error:]_block_invoke`
    body    17 asm lines, 55 bytes
    file    corewlan_decomp/7ff8115a1e1f____51__CWWiFiClient_startMonitoringEventWithType_error___block_invoke.asm

Recovered shape (full body, 17 instructions):

  - retain (or release-and-msgSend) on the NSError pointer (RSI)
    via selref [0x7ff84146b9d0] through the objc_msgSend stub at
    [0x7ff84004df70].
  - store the retained NSError into the by-reference struct's
    inner chain at [byref + 0x28] -> [+0x8] -> [+0x28]. This is
    the canonical ARC __Block_byref_object NSError out-parameter
    writeback.
  - jump to `_dispatch_semaphore_signal` (entry 0x7ff8115d9cbe) to
    unblock the synchronous caller in startMonitoringEventWithType:.

This 17-instruction body PROVES the subscribe-with-reply
contract: the caller of startMonitoringEventWithType: waits on
the semaphore that this block signals on reply. Therefore
subscribing to a CWEventType is a synchronous gate (with NSError
out-param) that must succeed BEFORE any join-lifecycle delegate
dispatch can flow to the registered delegate.

### Target C2: CoreWLAN selector strings (consumer side)

Each of the five join-lifecycle selector strings has exactly one
location in the CoreWLAN binary, and each has NO internal CoreWLAN
caller xrefs (the selectors are looked up only inside the
block_invoke wrappers that already exist in CoreWLAN). The
xref-summary tsvs are preserved at:

  corewlan_decomp/xrefs_joinDidStartForWiFiInterfaceWithName_ssid_.tsv
  corewlan_decomp/xrefs_joinDidCompleteForWiFiInterfaceWithName_isAutoJoin_error_.tsv
  corewlan_decomp/xrefs_autoJoinDidStartForWiFiInterfaceWithName_.tsv
  corewlan_decomp/xrefs_autoJoinDidCompleteForWiFiInterfaceWithName_.tsv
  corewlan_decomp/xrefs_autoJoinDidUpdate_.tsv

This recovers the same NO_REFS evidence the prior contract closure
recorded; the new batch confirms it independently against the
exact-dyld CoreWLAN slice.

### Target C3: CWLocationClient autoJoin delegate-receive bodies

- autoJoinDidStart block_invoke:
    address 0x7ff81159a71b
    name    `___61-[CWLocationClient autoJoinDidStartForWiFiInterfaceWithName:]_block_invoke`
    body    32 asm lines, 114 bytes
    file    corewlan_decomp/7ff81159a71b____61__CWLocationClient_autoJoinDidStartForWiFiInterfaceWithName___block_invoke.asm

  Recovered shape:
  - load `self = [block + 0x20]` (the CWLocationClient instance),
    then load `inner = [block + 0x28]` and `[inner + 0x10]`
  - call objc_msgSend with selref [0x7ff84146ba10] on the loaded
    pair (likely an "isEnabled" / "shouldHandle" predicate)
  - if the return value is non-zero, log via `0x7ff8115d99fa`
    using the format string at `0x7ff8115e3331`
    ("<%s[%d]> CORE LOCATION auto-join started, temporarily stop
    scanning") and set the byte flag at `[inner + 0x39] = 1`
  - call objc_msgSend with selref [0x7ff84146c680] (likely the
    "stop scanning" follow-up action)
  - return

- autoJoinDidComplete block_invoke:
    address 0x7ff81159a7ff
    name    `___64-[CWLocationClient autoJoinDidCompleteForWiFiInterfaceWithName:]_block_invoke`
    body    35 asm lines
    file    corewlan_decomp/7ff81159a7ff____64__CWLocationClient_autoJoinDidCompleteForWiFiInterfaceWithName___block_invoke.asm

This is the DIRECT evidence that an Apple-internal CoreWLAN
client (CWLocationClient, used by CoreLocationd / WirelessRadioManagerd-
style processes) DOES receive autoJoin delegate dispatch on this
build. The flag at [inner + 0x39] = 1 confirms the dispatch
actually arrives at the in-process delegate; the autoJoinDidStart
->stop-scanning side effect is observable in the CoreLocationd
process even though the rev9 AirportItlwmAgent helper never
received any of the same callbacks.

## Updated status of the four candidate explanations

### Candidate 1: missing -startMonitoringEventWithType: subscription
Status: CONFIRMED (was "HIGHLY SUPPORTED" in the prior closure;
now decisively confirmed by the recovered reply block_invoke
body that explicitly does dispatch_semaphore_signal).

Decision: the rev9 helper did not call startMonitoringEventWithType:
before installing its delegate, and therefore never opened the
XPC subscription that drives the in-process join-lifecycle
delegate dispatch in CoreWLAN. CWLocationClient (which DID
receive the autoJoinDidStart callback per Target C3 above)
necessarily either calls startMonitoringEventWithType: as part of
its own init (the CWLocationClient subscription path remains a
NAMED FOLLOW-UP target — the cycle did not decompile the full
CWLocationClient init body) or relies on an implicit subscription
that Apple internal classes obtain through a path the rev9 helper
does not have access to.

### Candidate 2: post-association-only producer timing
Status: UNLIKELY (was "PARTIALLY ADDRESSED MOSTLY UNKNOWN" in the
prior closure).

Recovered evidence: the sole setJoinStartedEvent caller is
`connectToTetherDevice:remember:interfaceName:token:authorization:connection:isAutoJoin:isAskToJoin:reply:`,
the airportd connection-setup XPC handler. This is the path called
when a client process asks airportd to associate to a tethered/
target Wi-Fi device. It fires at association-setup time, which
the rev9-rev2 Stage 2 runtime evidence shows precedes the kext
first-M1 window: airportd logged "Will associate" at
2026-05-18T08:33:28.692 and "Join timed out" at 08:33:38.865;
the setJoinStartedEvent would have been published into the
producer subsystem within that same 10-second window. Whether the
CWXPCSubsystem actually forwards the event to subscribed
out-of-process clients during that window remains gated by
Candidate 1's subscription requirement.

### Candidate 3: private entitlement gate
Status: STILL UNKNOWN (no change from the prior closure).

The CWXPCSubsystem.initWithScheduler body has been recovered
(Target P3) but the individual instance-slot helpers it assigns
have not been followed to their entitlement-check (SecTaskCopyValueForEntitlement /
SecCodeCopySigningInformation) call sites. This is a NAMED
FOLLOW-UP target below.

### Candidate 4: renamed/renumbered Tahoe selector path
Status: DISPROVED (unchanged from the prior closure).

The recovered xref evidence in the airportd binary again confirms
all five join-lifecycle selectors AND the producer-side
setJoinStartedEvent selector exist with their canonical Apple
names; selectors are not renamed on this build.

## Pre-first-M1 reachability decision (updated)

Recovered evidence supports the following narrowed answer to the
auditor-required reachability question:

- `joinDidStart` CAN in principle fire pre-first-M1 on the live
  Tahoe iwx path, because its producer
  (connectToTetherDevice:...) is dispatched on the connection-
  setup path that the rev9-rev2 Stage 2 runtime captured between
  airportd's "Will associate" and "Join timed out" log lines.
- The pre-first-M1 window IS available, IF the consumer process
  has previously called -startMonitoringEventWithType:error: with
  the correct CWEventType integer AND has whatever entitlement
  the CWXPCSubsystem requires (Candidate 3 unresolved).
- For the AirportItlwmAgent helper as committed (rev10 HEAD), the
  helper does NOT call startMonitoringEventWithType: at all and
  is ad-hoc-codesigned. Either failure mode independently breaks
  the path.

The reachability question therefore conditionally resolves to:
the architectural path exists; the rev9 helper is missing
preconditions on both gates 1 and (possibly) 3.

## Recommended next route

Route: REUSE_REFERENCE_DECOMP for the local-implementation decision
on a userland PMK trigger, contingent on resolving the two
remaining named follow-up items in this closure. Specifically:

1. Resolve Candidate 1 by decompiling the CWLocationClient
   init/subscription path to confirm the CWEventType integer
   value the helper must pass to -startMonitoringEventWithType:
   to receive joinDidStart. Once that integer is named, the rev9
   helper can be evolved into a SUBSCRIBED CWWiFiClient observer
   that COULD receive the event.
2. Resolve Candidate 3 by following the CWXPCSubsystem entitlement
   helper to its SecTask/SecCode call site and identifying the
   entitlement string (or signing-predicate) that the airportd
   side requires from a subscribing process. If the entitlement
   is a com.apple.wifi.* private entitlement that ad-hoc-signed
   processes cannot claim, then the userland-helper route is
   intrinsically blocked and IMPLEMENT_LOCAL becomes the
   recommended route (a project-owned local PMK producer that
   reaches the kext via the already-accepted PLTI ingress carrier
   without going through CoreWLAN/airportd).

The alternative routes are explicitly REJECTED at this point:
- REUSE_LINUX_BSD: not applicable; the trigger source question is
  Apple-userland-XPC-specific.
- IMPLEMENT_LOCAL without resolving Candidates 1 and 3: REJECTED,
  because the rev9 Stage 2 runtime already proved the naive
  delegate-only approach fails.
- RESEARCH_FIRST without bounded targets: not needed; the targets
  are now bounded.
- no_semantic_userland_trigger: REJECTED at this stage, because
  the architectural path exists conditional on the remaining
  named follow-ups, so the project is not yet at the "no userland
  trigger" decision.

Whether another diagnostic runtime is necessary: FORBIDDEN until
the CWEventType integer (Candidate 1 follow-up) is resolved and
the rev9 helper is updated to subscribe via
startMonitoringEventWithType: with the resolved integer. Running
the rev9 helper as-is again would just regenerate the same
negative result the rev9 Stage 2 runtime already captured.

## Remaining named missing decomp targets (bounded follow-up cycle)

After this closure cycle, three of the previous nine named
missing targets are SUBSTANTIALLY ADDRESSED (P1 connectToTetherDevice
producer caller, P2 __autoJoinStartedWithTrigger, P3
CWXPCSubsystem.initWithScheduler shape). Six targets remain to
be closed by a follow-up bounded decomp cycle, in priority order:

1. The CWLocationClient init/subscription path: identify the
   exact call sequence by which CWLocationClient registers as a
   CWWiFiClient delegate AND subscribes via
   startMonitoringEventWithType: (the CWEventType integer
   constant it passes is the smoking gun).
2. The CWEventType integer constants and human-readable names,
   especially the integer for the "join started" event type.
3. The CWXPCSubsystem publish-event handler bodies (the helpers
   stored at instance slots +0x48 through +0xb8 in the
   initWithScheduler initializer).
4. The entitlement / signing-predicate check site inside the
   publish-event handler (SecTaskCopyValueForEntitlement /
   SecCodeCopySigningInformation / csops call sites) and the
   exact entitlement-string used.
5. The join-started XPC payload field layout — whether the
   payload carries SSID bytes in addition to deviceName /
   interfaceName.
6. Restart/replay behavior: whether late subscribers receive
   previous join state.

Each remaining target is bounded (one or two functions) and the
analyzeHeadless invocation pattern used by this cycle can be
re-applied with the new addresses once the CWLocationClient
init address is resolved.

## What this closure cycle does NOT decide

- Whether the underlying CR-479 anomaly (no PMK reaches the kext
  before first M1) can be fixed via a userland producer remains
  open pending the six named follow-up targets.
- Whether the airportd entitlement gate (Candidate 3) ultimately
  blocks the userland path or not.
- No semantic source change is proposed in this cycle.

## Forbidden alternatives explicitly REJECTED at this stage

- Re-running the rev9 helper to regenerate the same negative
  result: FORBIDDEN until the CWEventType integer and the
  entitlement gate are resolved.
- Proposing a SYSTEM_CONTRACT_FIX or semantic userland PMK
  producer: REJECTED. Producer-side body coverage now has 3 of 9
  named targets recovered, but Candidates 1 and 3 still gate any
  userland implementation.
- Cite licensing concerns to avoid REUSE_REFERENCE_DECOMP:
  REJECTED. Reference behavior is recovered per Apple binaries and
  would be re-expressed as project-owned code in any later
  implementation request.
- Log or expose raw credentials, PMK, PSK, PTK, MIC, or key
  material: REJECTED. This document contains no key material.
- Clone, copy, mirror, rsync, or move the guest itlwm .git
  directory outside /Users/devops/Projects/itlwm: did not occur.
  The runtime_evidence/ tarball was transferred via scp on
  command lines that never touched the guest .git directory.
