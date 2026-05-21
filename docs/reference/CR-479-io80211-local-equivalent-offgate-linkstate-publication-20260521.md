# CR-479 IO80211 local-equivalent off-gate link-state publication — Stage 1 predicate capture

work_item_id: mission_io80211_local_equivalent_offgate_linkstate_publication_20260521
epic_id: cr479_io80211glue_threadowner_deferral
anomaly_id: CR-479-stage1-iwx-auth-ack-boundary-after-pmk-delivery-20260518 (run-side link-up panic branch)
change_class: DIAGNOSTIC_INSTRUMENTATION
stage: STAGE_1_STRUCTURAL
status: FIX_IMPLEMENTED (Stage 1 candidate; build/runtime deferred per mission)

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

## This Stage 1 change (behavior-neutral predicate capture)

`AirportItlwmV2.cpp setLinkStateGated`, immediately before the inherited
`((IO80211InfraInterface *)that->fNetIf)->setLinkState(linkState, setLinkCode, false, 0, 0)`
publication, now reads and logs the recovered predicates:

- `getWorkLoop()->onThread()` (work-queue serial owner thread),
- `getWorkLoop()->inGate()` (work-loop gate held),
- `IO80211InfraInterface::onDispatchQueue()` (IO80211 dispatch-queue ownership).

The change is strictly behavior-neutral: it only reads predicates and logs them;
it does not alter ordering, payload, gating, return semantics, or ownership, and
it adds no fallback/retry/poll/replay/masking/forced-success/guessed-state logic.
It is the no-guessing first step of the IMPLEMENT_LOCAL route: it instruments the
exact predicate the off-gate publication must satisfy, at the exact pre-publication
point, so the candidate off-gate dispatch can be authored and validated against
measured predicate values rather than an unverified static assumption.

Expected runtime reading at this existing command-gated site (to be captured in
the deferred runtime window): `onThread==1, inGate==1` — i.e. the panic
precondition violation, confirming the diagnosis.

## Candidate local-equivalent off-gate dispatch (design for the follow-up fix, NOT implemented here)

The reference owner that satisfies the contract is the IO80211 work-queue serial
execution context (the apple80211 / WCL request+event handlers run on the
work-queue with `onThread==true, inGate==false`). The itlwm local equivalent
under evaluation is to dispatch the inherited publication
(`IO80211InfraInterface::setLinkState`) from an itlwm-owned event-source action
serviced by `_fWorkloop` (`IO80211WorkQueue`) outside the command gate, rather
than from `getCommandGate()->runAction`. Object-lifetime / coalescing rules for
that follow-up:

- retain `this` and `fNetIf` for the deferral window; capture only the required
  link-state inputs (linkState, setLinkCode/rawCode) in a single pending record;
- coalesce duplicate or stale pending link-state transitions to the latest value
  as a documented lifecycle rule (not retry/replay/masking/forced-success);
- the off-gate action publishes once per coalesced transition.

Residual uncertainty blocking the follow-up fix's authoring without this capture:
whether an itlwm-added event source serviced by `IO80211WorkQueue` runs with
`inGate()==false` is not statically determinable — standard `IOWorkLoop`
services event sources under the gate (`IOWorkLoop.h`), while the recovered
evidence shows the work-queue's native serial work runs off-gate; the discriminator
is the runtime predicate capture this change enables. Reference decomp for the
exact servicing semantics is exhausted/closed (`reference_decomp_exhausted=YES`).

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

## Verification plan (runtime deferred to a separately authorized Stage 1 window)

After Stage 1 approval and in an authorized runtime window:
capture the `postTahoeWclLinkUpInd` / `reportLinkStatus` / `setLinkState` sequence
and the new `linkstate-publish-predicate onThread/inGate/onDispatchQueue` line at
the existing command-gated site (expected `onThread=1 inGate=1`); then, in the
follow-up fix, capture the same predicates at the candidate off-gate site
(target `onThread=1 inGate=0`), confirm `sendIOUCToWcl` does not take the
null-owner panic branch during association-complete link-up, and observe a short
post-link-up stability window.

## Scope note

The reviewed diff for this request is exactly `AirportItlwm/AirportItlwmV2.cpp`
(predicate capture) plus this document. A pre-existing, unrelated staged diff on
`itlwm/hal_iwn/ItlIwn.cpp` (the CR-479 H.2 iwn post-RXON IOReg carrier, a
different correlation) is present in the working tree and is NOT part of this
request's reviewed scope.
