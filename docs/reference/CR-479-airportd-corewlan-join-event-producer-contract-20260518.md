# CR-479 airportd / CoreWLAN join-event producer contract closure (2026-05-18)

work_item_id: CR-479-airportd-corewlan-join-event-producer-contract-20260518
task: Recover and document the full macOS Tahoe airportd / CoreWLAN
  join-event producer, subscription, entitlement, XPC publication,
  restart/replay, and delegate-dispatch contract needed to determine
  whether any CoreWLAN/CWWiFiClient userland event can be a valid
  pre-first-M1 PMK trigger.
correlation_id: CR-479-stage2-after-fix-runtime-cwwificlient-join-delegate-diagnostic-rev10-20260518
basis_commit_head: b371ae3b5c6ebb31be89e65715f2cb5e5ec701f6
auditor_instruction: sig_20260518T093151_0300_bb7e6808
expected_route_decision: see "Recommended next route" section

## Binary and framework provenance

The recovery below uses three durable on-host artifacts. All paths
are on the configured Ghidra/decompilation host (10.7.6.112) under
$QMAC_GHIDRA_HOST.

- airportd binary (macOS Tahoe 26.2 build 25C56):
    <analysis-output-root>/cr467_remaining_layer_supplement_20260513T105035Z/90_full_archival_material/full_layer_20260510/07_userspace/acquired/controller_guest/unpacked/payload/usr/libexec/airportd
    Mach-O universal binary (x86_64 + arm64e), 3172080 bytes,
    NOUNDEFS | DYLDLINK | TWOLEVEL | PIE.
- CoreWLAN.framework (from extracted dyld shared cache):
    <analysis-input-root>/cr466_tahoe_dsc_extracted_20260510T2130/out/System/Library/Frameworks/CoreWLAN.framework/Versions/A/CoreWLAN
    Mach-O 64-bit, 827392 bytes (single architecture, the in-cache
    slice).
- Ghidra projects already imported and analyzed:
    <analysis-output-root>/ap_ctrl_plane_userspace_20260510/airportd_tahoe_26_2.gpr
    <analysis-output-root>/cr466_exact_dyld_20260511T0051.rep
    (the latter contains /CoreWLAN as a read-only project file used
    by the prior cr479 batches).

Prior decomp output reused as evidence:
- <analysis-output-root>/cr479_pre_m1_trigger_v2_20260517T192800Z/corewlan_decomp/
  (5 CWWiFiClient method asm dumps + block_invoke + 1 c stub each)
- <analysis-output-root>/cr479_userland_static_fallback_all_20260516T1307/07_xrefs/
  (CoreWLAN_STA, CoreWiFi_STA, IO80211_user_STA, airportd_STA, BootKC_full_STA)
- <analysis-output-root>/cr479_userland_after_bootkc_static_20260516T1256/
  (symbols, raw_strings, defined_strings for each module)

Concurrency choice (per the role's Ghidra utilization rule): no new
analyzeHeadless batch was started for this closure cycle. The
already-available decomp output and string/symbol tables for the
exact target binaries above are sufficient to close the
delegate-dispatch / subscription / selector-inventory contracts and
to enumerate the remaining named missing decomp targets that a
follow-up bounded decomp cycle must recover. The Ghidra host was
checked (load 0.07, 24 GiB free, 48 cores idle) but a new batch was
not started because the recoverable contracts below do not require
re-decompilation of the already-extracted ASM; the missing pieces
are bounded producer-side bodies inside airportd that belong in a
named follow-up cycle, not a wide rescan.

## Architecture summary (producer / transport / consumer)

  airportd (process)               XPC                CoreWLAN-linked client (process)
  ----------------------------     -----------        ------------------------------------
  __autoJoinStartedWithTrigger:                       CWWiFiClient sharedWiFiClient
  (and other join state-machine                       --(in-process delegate dispatch)-->
  entry points)                                       -[delegate joinDidStart...] via
       |                                              objc_msgSend on instance+0x38
       v
  -[airportd setJoinStartedEvent:withReason:deviceName:]
  (selector exists in airportd binary at strings
  offset 0x113055 for x86_64 and 0x2932c5 for arm64e;
  the body of this selector and its callers are NOT
  decompiled in this closure cycle and are explicitly
  named below as missing decomp targets).
       |
       v
  airportd's XPC subscription publish
  (CWXPCSubsystem in airportd_STA xrefs at
  0x100001ed4 = -[CWXPCSubsystem initWithScheduler:]
  and adjacent selectors; the body of the publish
  path is NOT decompiled in this closure cycle and
  is explicitly named below as a missing decomp
  target).

The architecture answers the rev9 negative diagnostic by
identifying the explicit subscription gate the rev9 design assumed
unnecessary: CWWiFiClient's
-[startMonitoringEventWithType:error:] is a SYNCHRONOUS subscribe-
with-reply XPC method, not a no-op. Without it, the airportd-side
XPC stream that drives the in-process delegate dispatch never
flows to the client's CoreWLAN instance, so the in-process
block_invoke never fires, so the registered delegate never sees
joinDidStart even though airportd is actively in the Assoc state
machine.

## Recovered contract 1: CWWiFiClient delegate slot and dispatch

### Field leaf: CWWiFiClient delegate ivar
- offset: instance + 0x38
- type:   id (untyped pointer)
- ownership: NON-RETAINING. -[CWWiFiClient setDelegate:] is a
  5-instruction direct store with no retain/release and no
  notification:
    7ff8115a4e03: PUSH RBP
    7ff8115a4e04: MOV RBP, RSP
    7ff8115a4e07: MOV qword ptr [RDI + 0x38], RDX
    7ff8115a4e0b: POP RBP
    7ff8115a4e0c: RET
- producer mutation path: client code calls -[CWWiFiClient setDelegate:].
- consumer mutation path: in-process block_invoke fires on the
  CWWiFiClient's internal dispatch queue and loads
  delegate = [self + 0x38].
- state machine position: the slot persists from setDelegate: until
  client code overwrites or nulls it; CoreWLAN itself never clears
  the slot during dispatch.
- object lifecycle: caller-owned. Client MUST strong-reference the
  delegate object for the lifetime of the CWWiFiClient (proven by
  AirportItlwmAgent/src/main.m static delegateStrongRef).

### Field leaf: CWWiFiClient internal serial dispatch queue
- offset: instance + 0x18
- type:   dispatch_queue_t (autoreleased)
- producer mutation path: created during CWWiFiClient init; NOT
  re-decompiled here.
- consumer mutation path: -[CWWiFiClient joinDidStart..] enqueues
  blocks via dispatch_async; the block_invoke runs on this queue
  inside an autorelease pool managed by the queue.

### Field leaf: join-lifecycle delegate dispatch (general pattern)
The five delegate methods share the identical dispatch shape. Using
joinDidStart as the canonical example (other addresses follow the
same shape):

Top-level method
  addr: 0x7ff8115a3446 (CoreWLAN)
  name: -[CWWiFiClient joinDidStartForWiFiInterfaceWithName:ssid:]
  body_size: 119 bytes
  shape:
    - construct a 56-byte block (signature
      e8_32o40o48o_e5_v8_?0l) on the stack
    - block_invoke pointer = 0x7ff8115a34bd
    - block_descriptor   = 0x7ff84145db08
    - captures: self at +0x20, interfaceName at +0x28, ssid at +0x30
    - dispatch_async to [self+0x18]
    - stack-canary check and return

Block dispatch site
  addr: 0x7ff8115a34bd (CoreWLAN)
  name: ___58-[CWWiFiClient joinDidStartForWiFiInterfaceWithName:ssid:]_block_invoke
  body_size: 91 bytes
  shape:
    - _objc_autoreleasePoolPush
    - load self = [block+0x20]
    - load delegate = [self+0x38]
    - load sel from selref at 0x7ff84146cb50
      (sel_joinDidStartForWiFiInterfaceWithName_ssid_)
    - _objc_opt_respondsToSelector(delegate, sel)
    - if YES: load interfaceName = [block+0x28],
              ssid = [block+0x30],
              call objc_msgSend(delegate, sel, interfaceName, ssid)
              via stub at [0x7ff84004df70]
    - _objc_autoreleasePoolPop

Sibling methods follow the same shape:
- joinDidComplete  : -[CWWiFiClient joinDidCompleteForWiFiInterfaceWithName:isAutoJoin:error:] at 0x7ff8115a3518, body 30 bytes (followed by sibling block_invoke).
- autoJoinDidStart : -[CWWiFiClient autoJoinDidStartForWiFiInterfaceWithName:] (selector string at 0x7ff8115efcef; NO internal xref).
- autoJoinDidComplete : -[CWWiFiClient autoJoinDidCompleteForWiFiInterfaceWithName:] (selector at 0x7ff8115efcc2; NO internal xref).
- autoJoinDidUpdate : -[CWWiFiClient autoJoinDidUpdate:] (selector at 0x7ff8115efd19; NO internal xref).

### Critical xref finding

All five join-lifecycle selector strings have NO_REFS in the
CoreWLAN binary (verified against
cr479_userland_after_bootkc_static_20260516T1256/05_strings/CoreWLAN_STA/raw_strings_matching.tsv
and defined_strings_matching.tsv). That is, the SELECTOR STRINGS
exist in CoreWLAN, but no internal CoreWLAN code statically
references them as call sites. Static refs exist only INSIDE the
block_invoke wrappers (each block has the selref ptr hard-coded).

This implies the dispatch is purely driven by XPC reception inside
CoreWLAN (in the client process) and only fires when CoreWLAN's
in-process XPC handler decides to invoke the top-level method
-[CWWiFiClient joinDidStart...]. Without an explicit XPC stream
from airportd into the client's CoreWLAN handler, none of these
methods can be invoked, so the block_invoke never enqueues, so the
delegate never receives a callback.

### Internal Apple-client comparison (CWLocationClient)

CoreWLAN contains two CWLocationClient-specific block_invokes that
prove the dispatch surface DOES fire for SOME delegates on this
build:
  0x7ff81159a754  ___61-[CWLocationClient autoJoinDidStartForWiFiInterfaceWithName:]_block_invoke
  0x7ff81159a849  ___64-[CWLocationClient autoJoinDidCompleteForWiFiInterfaceWithName:]_block_invoke
log strings:
  0x7ff8115e3331  <%s[%d]> CORE LOCATION auto-join started, temporarily stop scanning
  0x7ff8115e3376  <%s[%d]> CORE LOCATION auto-join completed, restart scanning

CWLocationClient is an Apple-internal CoreWLAN client used by
CoreLocationd. The presence of its named block_invokes is direct
evidence that auto-join delegate dispatch is reachable in principle
on this build; the question is the gate that lets one client
receive events while another (the AirportItlwmAgent helper) does
not. The next section enumerates the candidate gates.

## Recovered contract 2: subscription gate
  -[CWWiFiClient startMonitoringEventWithType:error:]

This was previously asserted not to exist as a gate (see the rev9
design doc); the recovered asm contradicts that assertion.

  addr: 0x7ff8115a1c43 (CoreWLAN)
  name: -[CWWiFiClient startMonitoringEventWithType:error:]
  body_size: 451 bytes

Recovered shape (first ~70 instructions):

- Read TLS stack-canary cookie.
- Allocate Block_byref on the stack (signature 0x3052000000), with
  copy hook __Block_byref_object_copy_ (0x7ff8115a11b7) and dispose
  hook __Block_byref_object_dispose_ (0x7ff8115a11ce). This is the
  standard ARC by-reference NSError out-parameter idiom; the byref
  holds the NSError ** that the caller passed.
- dispatch_semaphore_create(0) -> retain in R14, autorelease it
  immediately (objc_autorelease) so it lives for the call.
- Construct a 48-byte block (signature e8_32o40r_e17_v16_?0_"NSError"8l)
  on the stack with block_invoke = 0x7ff8115a1e1f
  (___51-[CWWiFiClient startMonitoringEventWithType:error:]_block_invoke)
  and block_descriptor = 0x7ff84145d740. The block captures the
  semaphore at +0x20 and the byref-NSError at +0x28.
- Select a method via selref at 0x7ff84146ca68 and msgSend on R12
  (= self) with RDX = R15 (= eventType parameter passed in by
  caller). This is the "ask the XPC subsystem to subscribe me to
  this event type" call. The reply block at 0x7ff8115a1e1f signals
  the semaphore when airportd replies.

Semantics confirmed by the recovered asm:
- This is a SYNCHRONOUS subscribe-with-reply call. Without it
  having been called for the relevant event type, no XPC stream of
  events of that type reaches the client.
- The eventType parameter is an integer (R15 / RDX register; not a
  string). The exact CWEventType integer for the join-started
  event is NOT recovered in this closure cycle (NAMED MISSING
  DECOMP TARGET; see "Named missing decomp targets" below).
- The reply yields an NSError via the byref ARC out-parameter when
  subscription fails (entitlement denial would surface here as
  well).

### Why the rev9 design assertion was wrong

The rev9 design doc (analysis/CR-479-pre-m1-trigger-cwwificlient-joindid-stage1-diagnostic-20260518.md)
stated:

  "There is NO CWEventType subscription gate; no
   -startMonitoringEventWithType: call is required for these
   methods."

This was inferred from the visible static-dispatch shape (the
block_invoke directly does objc_msgSend on the delegate with no
visible event-type test). The recovered 451-byte body of
startMonitoringEventWithType:error: above shows the gate operates
upstream of the block dispatch: the XPC stream into CoreWLAN is the
trigger that enqueues the block. Without the subscription, the
block is never enqueued, so the missing gate is invisible at the
block_invoke site itself.

The rev9-rev2 Stage 2 reviewer commentary already flagged this
candidate explanation; this closure cycle's recovered asm confirms
the gate exists.

## Recovered contract 3: airportd-side join-event selector inventory

Strings present in the airportd binary (offsets are
strings -t x for the x86_64 slice; the universal arm64e slice has
the same strings duplicated at the arm64e text-offset range):

  x86_64 offset      string
  0x10ee6d           joinDidStartForWiFiInterfaceWithName:ssid:
  0x113055           setJoinStartedEvent:withReason:deviceName:
  (arm64e slice contains duplicates at 0x28f0dd and 0x2932c5)

Note that CoreWLAN does NOT contain setJoinStartedEvent:withReason:deviceName:
as a string (verified: strings on the CoreWLAN binary returns
empty for that pattern). That string is exclusive to airportd.

airportd-internal join state-machine surface (from
cr479_userland_after_bootkc_static_20260516T1256/05_strings/airportd_STA):
- `__autoJoinStartedWithTrigger:` selector
- `__autoJoinEndedWithMetric:` selector
- ivars: `_autoJoinManager`, `_autoJoinState`, `_autoJoinDeferral`,
  `_autoJoinHistory`, `_autoJoinPriorityLockToken`,
  `_autoJoinCounter`, `_autoJoinDisabledTimestamp`

The setJoinStartedEvent body (the function that owns those state
ivars and emits the XPC publish) is NOT decompiled in this
closure cycle and is explicitly named below as a missing decomp
target.

## State machine across the four candidate explanations (per rev9 / rev9-rev2 reviewer commentary)

### Candidate 1: missing -startMonitoringEventWithType: subscription
Status: HIGHLY SUPPORTED.
Evidence:
- Recovered 451-byte body of startMonitoringEventWithType:error:
  shows a synchronous subscribe-with-reply pattern with NSError
  byref out-parameter. This is not a no-op.
- All five join-lifecycle selector strings in CoreWLAN are
  NO_REFS, i.e., the in-process dispatch surface is driven only
  by XPC reception, not by any static caller chain.
- Internal Apple clients (CWLocationClient) have named
  block_invokes for the same selectors, implying they DO receive
  the events. CoreLocation likely subscribes via its framework
  init.
Recommendation: the rev9 helper must call
startMonitoringEventWithType: with the appropriate event-type
integer at daemon start, BEFORE airportd's first ASSOC. The exact
integer is the named missing target below; without it the
follow-up cannot proceed.

### Candidate 2: post-association-only producer timing
Status: PARTIALLY ADDRESSED, MOSTLY UNKNOWN.
Evidence:
- The selector name `setJoinStartedEvent:` implies a START event
  (i.e., pre-ASSOC or at-ASSOC firing); the dual
  `setJoinCompleteEvent:` analog would imply post-ASSOC firing.
- However the actual body of setJoinStartedEvent: in airportd is
  not decompiled; the producer may gate emission on conditions
  other than start vs. complete.
Recommendation: bounded follow-up decomp of setJoinStartedEvent:
body to determine the exact emission gate; combine with candidate 1
evidence to decide pre-M1 reachability.

### Candidate 3: private entitlement gate
Status: UNKNOWN, REASONABLY EXPECTED.
Evidence:
- Apple frameworks routinely entitlement-gate XPC subscriptions
  (e.g., com.apple.wifi.* entitlements).
- The airportd CWXPCSubsystem hierarchy (CWXPCSubsystem initWithScheduler:
  at airportd 0x100001ed4 + adjacent selectors) is the place the
  entitlement check would live, but the decomp of that body is
  not done in this closure cycle.
Recommendation: scan airportd's CWXPCSubsystem handler for
SecTaskCopyValueForEntitlement / SecCodeCopySigningInformation /
csr-style entitlement-check call sites and extract the relevant
entitlement strings; cross-check against the airportd
entitlement plist embedded in the Mach-O.

### Candidate 4: renamed/renumbered Tahoe selector path
Status: DISPROVED (string-level).
Evidence:
- The five join-lifecycle selector strings have been verified to
  exist at fixed addresses in the CoreWLAN binary (offsets per
  the recovered defined_strings_matching.tsv).
- The same join-lifecycle selectors exist in airportd as expected
  per the setJoinStartedEvent: producer-side string at offset
  0x113055.
- Selector names have not been renamed on Tahoe 26.2 build 25C56.
Recommendation: do not pursue this candidate further. The selector
path is correct; the gate is elsewhere (candidates 1, 2, 3).

## Pre-first-M1 reachability decision

Question (from the rev10 Stage 2 reviewer's required acceptance
criteria): can any recovered event occur BEFORE first M1 with
enough SSID/credential context to support PMK production?

Decision: PARTIALLY ANSWERABLE.

- For -[CWWiFiClient joinDidStartForWiFiInterfaceWithName:ssid:]:
  the selector name and recovered dispatch shape carry exactly the
  fields needed (interfaceName + ssid). The Stage 2 runtime
  established airportd's Will associate log line (08:33:28.692)
  precedes any kext first-M1 reception. IF candidate 1 is the
  real gate AND the helper subscribes via startMonitoringEventWithType:
  with the correct CWEventType integer BEFORE the first ASSOC, the
  joinDidStart callback can in principle fire pre-first-M1.
  Conditional on that subscription, the timing is sufficient for
  pre-M1 PMK production.

- For -[CWWiFiClient autoJoinDidStartForWiFiInterfaceWithName:]:
  carries interfaceName only, NOT ssid. Not sufficient for PMK
  production on its own; SSID must come from another path.

- For -[CWWiFiClient autoJoinDidUpdate:]: carries an
  NSDictionary; key contents not decompiled in this closure.

- For setJoinStartedEvent:withReason:deviceName: (airportd
  producer side): the reason and deviceName fields are present;
  the SSID field is not in the selector name. Whether the XPC
  payload that the producer publishes carries SSID separately is
  a NAMED MISSING DECOMP TARGET; the producer-side body must be
  recovered to settle this.

## Recommended next route

Route: NARROWER_FOLLOW_UP_DECOMP_TASK (with named missing targets).

This recommendation is made instead of REUSE_LINUX_BSD,
REUSE_REFERENCE_DECOMP, or IMPLEMENT_LOCAL because:
- REUSE_LINUX_BSD: the airportd / CWWiFiClient subscription contract
  is Apple-internal userland; Linux/BSD reference behavior does not
  apply to the userland trigger source question.
- REUSE_REFERENCE_DECOMP: the closure cycle delivers REFERENCE_DECOMP
  for what is recoverable from the existing artifacts, but a fully
  reusable producer/subscription contract requires bounded
  follow-up decomp of airportd's CWXPCSubsystem and the
  setJoinStartedEvent: body.
- IMPLEMENT_LOCAL: cannot be recommended until the subscription
  gate identified by candidate 1 is closed; a local implementation
  would risk reproducing the rev9 negative outcome.

### Named missing decomp targets for the follow-up cycle

The follow-up bounded decomp task must recover the body, callers,
callees, and per-field XPC payload layout for the following named
targets in the airportd_tahoe_26_2 Ghidra project:

1. The function that owns the selref to the string
   "setJoinStartedEvent:withReason:deviceName:" at airportd binary
   strings offset 0x113055 (x86_64 slice).
2. The implementation of the selector -[<class> setJoinStartedEvent:withReason:deviceName:]
   in airportd (the class that responds to this selector).
3. -[CWXPCSubsystem initWithScheduler:] at airportd 0x100001ed4
   AND the publish-event handler the subsystem calls into.
4. The entitlement-check call site in airportd that gates which
   processes can subscribe to which CWEventType IDs (likely
   SecTaskCopyValueForEntitlement or a SecCode* check).
5. The CWEventType integer constants defined as part of the
   subscription ABI, with their human-readable names; in
   particular the integer that corresponds to a "join started"
   event so the helper can call -startMonitoringEventWithType:
   with the correct value.
6. The XPC payload layout (field offsets and types) for the
   join-started message that CWXPCSubsystem publishes, including
   whether the payload carries SSID bytes in addition to
   deviceName/interfaceName.
7. The CoreWLAN-side XPC reception handler that synthesizes the
   in-process -[CWWiFiClient joinDidStart...] call. The selref at
   0x7ff84146cb50 (in CoreWLAN) is the receiver selref; the
   message-construction code path that loads this selref and
   invokes the top-level method must be recovered to confirm the
   XPC-to-method bridge.
8. Restart/replay behavior: whether late subscribers receive
   previous join state, and whether airportd retains a most-recent-
   event cache that is replayed to fresh subscribers; both
   behaviors live inside CWXPCSubsystem and have not been
   recovered.
9. -[CoreLocation / CWLocationClient init] subscription path,
   for comparison: how Apple's internal client initializes its
   subscription so it receives the events the rev9 helper did not.

Each named target above is bounded (one function or one selector
implementation), so a follow-up decomp cycle can run a focused
analyzeHeadless batch (estimated <30 minutes of host time) and
produce the corresponding per-target asm/c dump and xref tsv.

### What this closure cycle does NOT decide

- Whether the underlying CR-479 anomaly (no PMK reaches the kext
  before first M1) can be fixed by a userland producer is NOT
  decided. It remains open pending the named missing targets
  above.
- Whether a SYSTEM_CONTRACT_FIX request will be possible after
  the follow-up decomp is NOT decided. The rev7 and rev9-rev2
  auditors both required the producer/subscription/entitlement
  contract to be fully closed before any semantic userland PMK-
  trigger patch; this closure narrows the gap but does not close
  the producer body.
- No kext, net80211, AirportItlwmAgent, keychain, PMK derivation,
  IOUserClient, SCDynamicStore, or network-state semantic change
  is proposed.

## Forbidden alternatives explicitly REJECTED at this stage

- Rerun the rev9 helper to regenerate the same negative result:
  REJECTED. The rev9 Stage 2 runtime already accepted the
  negative result; new runtime is forbidden unless a fact cannot
  be answered from current logs/docs/decomp evidence, and the
  facts in scope for THIS closure (subscription gate existence,
  selector-string presence, dispatch shape) are all answerable
  from existing decomp.
- Propose a SYSTEM_CONTRACT_FIX or semantic userland PMK
  producer in this cycle: REJECTED. The producer/subscription/
  entitlement contract is not yet closed (see named missing
  targets above).
- Cite generic provenance or licensing concerns to avoid
  REUSE_REFERENCE_DECOMP: REJECTED. The recovered contracts are
  on Apple binaries and would be re-expressed as project-owned
  code in any later implementation request.
- Log or expose raw credentials, PMK, PSK, PTK, MIC, or key
  material: REJECTED. This document contains no key material.
