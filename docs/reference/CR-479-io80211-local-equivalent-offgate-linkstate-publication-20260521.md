# CR-479 IO80211 local-equivalent off-gate link-state publication

work_item_id: mission_io80211_offgate_linkstate_publication_fix_20260521
epic_id: cr479_io80211glue_threadowner_deferral
anomaly_id: CR-479-stage1-iwx-auth-ack-boundary-after-pmk-delivery-20260518 (run-side link-up panic branch)
change_class: SAFETY_FIX (sendIOUCToWcl panic-prevention guard; off-gate publication attempted but runtime-proven not achievable from itlwm)
stage: STAGE_1_STRUCTURAL
status: LOCAL_EQUIVALENT_EXHAUSTED — after-fix runtime proved the itlwm-owned off-gate dispatch runs in-gate (inGate==1); the change stands as a safety-only panic-prevention guard, and the off-gate publication is routed to an OpenCore boot-time reference-patch attempt/rule-out mission (see Stage 2 after-fix runtime finding below)

## Recovered observable contract (reused, reference decomp exhausted)

`IO80211Glue::sendIOUCToWcl` (`0xffffff8002117f38`) validates, before it schedules
the IOUC publication block, the work-queue serial owner at `[[this+0x18]+0x38]`:
slot `+0x138` (onThread) must be true and slot `+0x130` (inGate) must be false,
else it branches to the null-owner panic tail `0xffffff80021181bf`
("trying to send on thread panic" @IO80211Glue.cpp:419). Recovered in
`IO80211GLUE_THREADOWNER_DEFERRAL_REF_EVIDENCE_20260520.md`
(sha256 `ff0ff7768ec01520835949957f2ec4bbb440af1f80a8c604326b34e4f5dd458f`) and the
upstream-owner / negative-producer lanes. The `+0x38` owner is the IO80211
work-queue (`IO80211WorkQueue`), the same family as itlwm's `_fWorkloop`.

itlwm publishes run-side link state from `iwn_newstate` (on the `_fWorkloop`
thread, holding the work-loop gate) via
`AirportItlwm::setLinkStatus` -> `getCommandGate()->runAction(setLinkStateGated,...)`
-> `setLinkStateGated` -> `IO80211InfraInterface::setLinkState(...)`. Because
`getCommandGate()` is an `IOCommandGate` event source on `_fWorkloop` and
`IOWorkLoop::runAction` runs the action under the recursive work-loop gate
(`MacKernelSDK/Headers/IOKit/IOWorkLoop.h:287..308`), the inherited publication
reaches `sendIOUCToWcl` with `inGate()==true`, taking the panic branch during
association-complete link-up (Kernel-2026-05-20-005748.panic).

## Diagnostic baseline (committed, reused — not re-derived)

The behavior-neutral predicate probe in `AirportItlwmV2.cpp setLinkStateGated`,
immediately before the inherited
`((IO80211InfraInterface *)that->fNetIf)->setLinkState(linkState, setLinkCode, false, 0, 0)`
publication, reads and logs the recovered predicates `getWorkLoop()->onThread()`,
`getWorkLoop()->inGate()`, and `IO80211InfraInterface::onDispatchQueue()`. It was
committed (commit `5f39f9a2`) and runtime-captured `onThread=1 inGate=1
onDispatchQueue=1` followed by the `sendIOUCToWcl` panic at the command-gated
site (`04_serial_linkup_predicate_panic.txt` sha256
`136d146bd6f787ba92e5eeceff7f532d3f0d7f68630443e88766b1c933ff6346`). That probe
is reused unchanged at the new publication context to validate the fix.

## Change (this diff): off-gate dispatch attempt + safety-only precondition guard

NOTE (Stage 2 outcome): the off-gate dispatch below was ATTEMPTED but the after-fix
runtime proved it does NOT reach `inGate()==0` (the IO80211WorkQueue dispatches the
itlwm event-source action in-gate). The diff therefore does not achieve the off-gate
publication; what it does deliver is the precondition GUARD, which prevents the
`sendIOUCToWcl` kernel panic by skipping publication when the precondition is unmet.
See "Stage 2 after-fix runtime finding" below. The description of the dispatch
mechanism is retained for accuracy of what the diff contains.

The publication dispatch is moved off the command gate onto the same
`_fWorkloop` (`IO80211WorkQueue`) serial owner via an itlwm-owned software
`IOInterruptEventSource`. The whole change is contained in
`AirportItlwm/AirportItlwmV2.cpp` (file-scope publication layer; `_fWorkloop` and
`_fCommandGate` are already file-scope globals in that translation unit, and
`AirportItlwm::setLinkStateGated` is a public static, so no header change is
required):

- a file-scope software `IOInterruptEventSource _fLinkStatePublishSource` is
  created, added to `_fWorkloop`, and enabled alongside `scanSource` in the
  start path. Teardown is a single idempotent helper
  `teardownLinkStatePublishSource()` that disables the source, calls
  `_fWorkloop->removeEventSource(...)` (which drains any in-flight action on the
  work-queue thread), clears the pending record, releases the source, and frees
  the `IOSimpleLock`. `AirportItlwm::stop()` calls it immediately after the
  link-down-queuing `setLinkStatus(kIONetworkLinkValid)` and BEFORE
  `detachInterface(fNetIf)`/`OSSafeReleaseNULL(fNetIf)`, so a deferred action can
  never reach the publication worker after `fNetIf`'s lifetime ends.
  `releaseAll()` calls the same helper (idempotent), covering the init-failure
  paths where `fNetIf` was never attached;
- `AirportItlwm::setLinkStatus` no longer calls
  `getCommandGate()->runAction(setLinkStateGated, ...)`. For both the link-up and
  link-down transitions it records the transition into a single coalesced pending
  record (`{linkState, rawCode, valid}`, protected by an `IOSimpleLock`) and
  triggers `_fLinkStatePublishSource->interruptOccurred(0, 0, 0)`;
- `publishLinkStateInterruptAction` runs on the `_fWorkloop` serial owner. It
  takes the lock, reads-and-clears the pending record (latest accepted state
  wins → coalescing), releases the lock, and — only when a transition is pending
  — calls the existing `AirportItlwm::setLinkStateGated` worker directly (not via
  the command gate);
- `setLinkStateGated` evaluates the recovered predicates (`getWorkLoop()->onThread()`,
  `getWorkLoop()->inGate()`, `onDispatchQueue()`) as a precondition GUARD at the
  TOP of the Tahoe publication path — before ANY publication side effect
  (`postTahoeWclLinkUpInd`, `reportLinkStatus`,
  `setLinkState`, `setRunningState`, the connect-complete event, and `postMessage`).
  The publication proceeds only when `onThread()==1 && inGate()==0` (the recovered
  `sendIOUCToWcl` precondition). When the precondition is not satisfied — i.e. the
  chosen route did not reach the publication site off-gate — the worker returns
  `kIOReturnNotReady` immediately and performs NO link-state publication at all
  (not merely skipping the inherited `setLinkState`). Reaching the inherited
  publication therefore means the precondition already holds, so the inherited call
  is unconditional at that point. The captured predicate line is the runtime
  discriminator; the panic is avoided in both outcomes (publish when the
  precondition holds, otherwise no publication).

Object lifetime / coalescing contract:

- `this` (the `AirportItlwm` provider) owns the event source and outlives it. The
  worker validates `that` (the `OSDynamicCast` result) and, in the Tahoe path,
  `that->fNetIf` non-NULL before any dereference, returning `kIOReturnNotReady` if
  either is NULL. `fNetIf` is released in `stop()` only after
  `teardownLinkStatePublishSource()` has drained and removed the source (see the
  teardown bullet above), so the worker never observes a freed `fNetIf`. Only the
  small link-state inputs (`linkState`, `rawCode`) cross the deferral boundary, by
  value, in the pending record — no pointers to transient state are captured.
- Duplicate or stale link-state transitions coalesce to the latest accepted value
  (the record is overwritten, not queued); the action publishes exactly once per
  coalesced transition. This is a value-latest lifecycle rule, not
  retry/replay/masking/forced-success/timing-heuristic/guessed-state correction —
  if no transition is pending when the action runs, it publishes nothing.
- The dispatch is asynchronous; `setLinkStatus` already returns the
  `super::setLinkStatus` result and no caller depends on the publication
  completing synchronously, so deferral does not change observable return
  semantics. The synchronous link-down housekeeping (`ifq_flush`, `mq_purge`)
  stays in `setLinkStatus`.

Runtime discriminator outcome (RESOLVED by the Stage 2 after-fix run, see section
above): the itlwm-added event source serviced by `IO80211WorkQueue` is dispatched
WITH the gate held (`inGate()==1`, 83/83 readings). Standard `IOWorkLoop` services
event sources under the gate (`IOWorkLoop.h`), and this IO80211WorkQueue behaves the
same; the recovered "native serial work runs off-gate" path is not reachable by an
itlwm-added event source. The recovered predicate probe was the discriminator and it
resolved to `inGate()==1`, so this local equivalent is EXHAUSTED
(`local_equivalent_exhausted=YES`) and the off-gate publication is routed to an
OpenCore boot-time reference-patch attempt/rule-out mission (not operator). Reference
decomp for the exact servicing semantics remains exhausted/closed
(`reference_decomp_exhausted=YES`).

## Stage 2 after-fix runtime finding (local-equivalent EXHAUSTED; safety-only guard validated)

The exact reviewed diff was built (symbol-checked: all 932 undefined symbols resolve
against BootKC), installed (kext UUID 868AFA9D / e0ffdb7c via AuxKC rebuild + reboot),
and exercised on a controlled join that reached association-complete RUN (ic_state=4)
with a post-link-up stability window. At the link-up publication site the reused probe
read `onThread=1 inGate=1 onDispatchQueue=1` on all 83 readings (zero `inGate=0`): the
itlwm-owned `IOInterruptEventSource` serviced by `IO80211WorkQueue` is dispatched WITH
the work-loop gate held, exactly like a standard `IOWorkLoop` services event sources.

Result:
- off-gate `inGate()==0`: NOT achieved (measured `inGate()==1`) => `local_equivalent_exhausted = YES`;
- `sendIOUCToWcl` panic (`@IO80211Glue.cpp:419`): ABSENT across 82 link-state transitions
  and RUN (the precondition guard skipped publication); the diagnostic baseline panicked
  on the FIRST association-complete link-up, so the guard is a validated safety fix;
- functional link-state publication to IO80211: NOT achieved (the guard correctly skips
  publication when the precondition is unmet), so this diff is a SAFETY-ONLY no-crash
  change, not a functional link-up fix.

Routing: the off-gate publication cannot be achieved from itlwm (every itlwm-callable
dispatch reaches the publication site in-gate). It is routed to an OpenCore boot-time
reference-patch attempt/rule-out mission (planner-owned, not operator). Evidence:
commit-approval/runtime_evidence/CR-479-stage1-io80211-offgate-linkstate-publication-fix-rev3-20260521/40_join/.

## Rejected routes

- Direct command-gated `getCommandGate()->runAction(setLinkStateGated,...)` as the
  final publication route: rejected — `inGate()==true`, the panic precondition.
- `_fWorkloop->runAction` / `runActionBlock`: rejected — run the action under the
  recursive work-loop gate (`inGate()==true`); the symbols recover only as
  indirect-dispatch thunks (read-only + write-mode passes), no non-gated body.
- `IO80211Glue::runActionBlockOnSerial` as context repair: rejected — downstream
  of the already-checked `sendIOUCToWcl` precondition.
- Calling `sendIOUCToWcl` / `receiveMessageInternal` / a CommonGlue producer from
  itlwm: rejected — IO80211Family-internal, not an itlwm-callable publication API.
- `openGate()/closeGate()` bracket around the publication: rejected as unsafe —
  the work-loop gate is recursive and the iwn interrupt/newstate frame already
  holds it, so a single `openGate()` cannot reach `inGate()==false` and a full
  release drops the outer frame's mutual exclusion.

## Verification performed (after-fix runtime, COMPLETED)

The exact reviewed diff was built with the Tahoe BootKC symbol check (all 932
undefined symbols resolve; BUILD SUCCEEDED), installed (kext 868AFA9D / e0ffdb7c via
AuxKC rebuild eliding AppleSunrise + reboot; loaded UUID verified), and exercised on a
controlled join to the lab AP that reached association-complete RUN (ic_state=4):

- reused `linkstate-publish-predicate onThread/inGate/onDispatchQueue` at the
  publication site: `onThread=1 inGate=1` on all 83 readings (off-gate `inGate=0` NOT
  achieved);
- `IO80211Glue::sendIOUCToWcl` null-owner panic (`@IO80211Glue.cpp:419`): ABSENT across
  82 link-state transitions and RUN (the precondition guard skipped publication);
- functional link-state publication: NOT achieved (guard skips when precondition unmet);
  this is a safety-only no-crash change;
- duplicate/stale transitions coalesced to the latest value; no
  retry/replay/masking/forced-success; guest stable throughout (no panic/hang).

Determination: `local_equivalent_exhausted=YES`; the off-gate publication is routed to
an OpenCore boot-time reference-patch attempt/rule-out mission. Evidence:
commit-approval/runtime_evidence/CR-479-stage1-io80211-offgate-linkstate-publication-fix-rev3-20260521/40_join/.

## Scope note

The reviewed diff for this request is exactly `AirportItlwm/AirportItlwmV2.cpp`
(predicate capture) plus this document. A pre-existing, unrelated staged diff on
`itlwm/hal_iwn/ItlIwn.cpp` (the CR-479 H.2 iwn post-RXON IOReg carrier, a
different correlation) is present in the working tree and is NOT part of this
request's reviewed scope.
