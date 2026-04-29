# CR-213 - End-to-end passive pool branch instrumentation (DIAGNOSTIC_INSTRUMENTATION)

- date: 2026-04-29
- stage: STAGE_1_STRUCTURAL
- justification class: DIAGNOSTIC_INSTRUMENTATION
- branch: master
- HEAD: `d3a07c2abccac863e1909aa562051a6ee5687245`
- requested by: Executor+Committer (per `docs/WORKFLOW_ITLWM.md`)
- supersedes: CR-212. CR-212 was too narrow because it stopped at
  `initWithName` object-state logging and did not instrument every local
  branch to a final point. CR-213 keeps CR-212's passive object-state
  reads and adds end-to-end passive branch coverage for the STEP 8b pool
  failure path and its immediate successful handoff boundaries.

does_this_fix_proven_current_root_cause: NO
This is diagnostic-only. It does not change the pool contract, retry,
fallback, packet type, PoolOptions, ownership, queue construction, or
registration behavior. It only adds passive logs that make every local
branch of the STEP 8b pool-creation error observable through its final
point.

## REQUIRED COVERAGE CRITERION

The criterion for this request is:

```
end-to-end instrumentation to final points across all branches of this error
```

CR-213 claims to satisfy that criterion for the locally controlled and
observable branches of the STEP 8b pool-creation error:

- factory branch `NEW_NULL` -> final return `NULL`;
- factory branch `INIT_FALSE` -> final release and return `NULL`;
- factory branch `INIT_TRUE` -> final return pool pointer;
- controller branch `TX_ONLY` -> final cleanup and `return false`;
- controller branch `RX_ONLY` -> final cleanup and `return false`;
- controller branch `TX_RX` -> final cleanup and `return false`;
- controller branch `BOTH_OK` -> final handoff to STEP 8c;
- downstream branch `DOWNSTREAM_QUEUE_FAIL` after `BOTH_OK` -> final
  cleanup and `return false`;
- downstream branch `QUEUES_OK` -> handoff to workloop attach;
- downstream branch `DOWNSTREAM_WORKLOOP_FAIL` -> final cleanup and
  `return false`;
- downstream branch `WORKLOOPS_OK` -> handoff to STEP 8d registration,
  proving the pool-creation error is no longer the active failure.

Branches inside the closed-source framework
`IOSkywalkPacketBufferPool::initWithName` / `_kern_pbufpool_create` are
not directly instrumented because doing so would require active probes or
framework modification. The request instruments the nearest legal passive
boundaries before and after that closed-source call.

## SYMPTOM

Same Stage 2 boot symptom as CR-208 / CR-209: STEP 8b pool creation
fails. CR-209 showed `new AirportItlwmIO80211PacketPool` succeeds and
the existing `pool->initWithName(...)` returns false for the production
TX/RX pools. CR-211 was rejected because direct pbufpool probes had
system-facing side effects. CR-212 was passive but incomplete for the
full branch-to-final-point coverage criterion.

## DIVERGENCE

- exact divergence point: the existing
  `IOSkywalkPacketBufferPool::initWithName` call reached through
  `AirportItlwmIO80211PacketPool::withName`, plus the controller-level
  TX/RX aggregation of those factory results in STEP 8b.
- confirmed deviation: `initWithName` returns false after `new`
  succeeds in current runtime evidence.
- confirmed root cause: TO_BE_DETERMINED by passive runtime evidence.
- exact confirmed deviation removed: NONE in this CR.
- exact semantic mismatch removed: NONE in this CR.

## CLAIM SCOPE

- exact claim scope:
  1. keep production `packetType = kIOSkywalkPacketTypeNetwork`;
  2. keep production PoolOptions unchanged (`poolFlags=1`);
  3. preserve CR-212 object-state reads (`poolVtable`, `ownerVtable`,
     `slot18`, `slot20`);
  4. add factory FINAL logs for `NEW_NULL`, `INIT_FALSE`, and
     `INIT_TRUE`;
  5. add STEP 8b controller branch logs for `BEGIN`, `AFTER_TX`,
     `AFTER_RX`, `TX_ONLY`, `RX_ONLY`, `TX_RX`, and `BOTH_OK`;
  6. add immediate downstream handoff logs for `DOWNSTREAM_QUEUE_FAIL`,
     `QUEUES_OK`, `DOWNSTREAM_WORKLOOP_FAIL`, and `WORKLOOPS_OK`.
- non_claims:
  - This request does NOT claim a fix.
  - This request does NOT instrument private closed-source framework
    branches directly.
  - This request does NOT call `kern_pbufpool_create` or
    `kern_pbufpool_destroy` directly.
  - This request does NOT change any system-facing argument, return
    value, state transition, ownership, ordering, callback, queue, or
    registration contract.
  - This request does NOT add retry, fallback, replay, polling, delay,
    forced state, or forced success.

## JUSTIFICATION PATH

justification path: DIAGNOSTIC_INSTRUMENTATION

- exact hypotheses being disambiguated:
  1. The factory fails before object allocation: `NEW_NULL`.
  2. The factory allocates but framework init fails before internal
     state publication: `INIT_FALSE slot18=0 slot20=0`.
  3. The factory allocates and framework init fails after partial
     internal state publication: `INIT_FALSE` with non-NULL `slot18`
     or `slot20`.
  4. Only the TX pool fails: controller branch `TX_ONLY`.
  5. Only the RX pool fails: controller branch `RX_ONLY`.
  6. Both pools fail symmetrically: controller branch `TX_RX`.
  7. Both pools succeed and the failure moves downstream:
     `BOTH_OK` followed by `DOWNSTREAM_QUEUE_FAIL` or
     `DOWNSTREAM_WORKLOOP_FAIL`.
  8. Both pools succeed and immediate downstream boundaries are clean:
     `WORKLOOPS_OK handoff=STEP8d`, proving this specific STEP 8b pool
     error is no longer active.

- exact probe points:
  - `AirportItlwm/AirportItlwmV2.cpp` -
    `AirportItlwmIO80211PacketPool::withName`, immediately after
    `new`, immediately after `initWithName`, and immediately before
    every factory return.
  - `AirportItlwm/AirportItlwmV2.cpp` -
    `AirportItlwm::start` STEP 8b, before TX creation, after TX
    creation, after RX creation, before every STEP 8b failure return,
    and before the successful handoff to STEP 8c.
  - `AirportItlwm/AirportItlwmV2.cpp` -
    STEP 8c queue creation and STEP 8c workloop attach, so the
    successful pool branch is followed to its immediate next final
    outcomes.

- why instrumentation is behavior-neutral:
  - The same factory calls run in the same order.
  - The same `initWithName(name, owner, packetType, options)` call runs
    with identical arguments.
  - The same queue constructors and workloop `addEventSource` calls run
    with identical arguments.
  - The same failure cleanup paths run: `super::stop`, `releaseAll`,
    `DISARM_PANIC_TIMER`, `return false`.
  - The same success handoffs run.
  - Only `XYLog` calls and diagnostic `sRT.startStep` updates were
    added.

## EXPECTED RUNTIME EVIDENCE

Factory branch evidence per TX/RX:

```
itlwm: PACKETPOOL[AirportItlwm-TX] new=<P> poolVtable=<VT> ...
itlwm: PACKETPOOL[AirportItlwm-TX] initWithName=<0|1>
      (type=1 slot18=<A> slot20=<B>)
itlwm: PACKETPOOL[AirportItlwm-TX] FINAL branch=<NEW_NULL|INIT_FALSE|INIT_TRUE> ...
```

Controller STEP 8b evidence:

```
itlwm: POOLTRACE[STEP8b] BEGIN ...
itlwm: POOLTRACE[STEP8b] AFTER_TX tx=<TX> rx=<RX-before>
itlwm: POOLTRACE[STEP8b] AFTER_RX tx=<TX> rx=<RX>
itlwm: POOLTRACE[STEP8b] FINAL branch=<TX_ONLY|RX_ONLY|TX_RX|BOTH_OK> ...
```

Downstream successful-pool branch evidence:

```
itlwm: POOLTRACE[STEP8c] FINAL branch=DOWNSTREAM_QUEUE_FAIL ...
itlwm: POOLTRACE[STEP8c] boundary=QUEUES_OK ...
itlwm: POOLTRACE[STEP8c-wl] FINAL branch=DOWNSTREAM_WORKLOOP_FAIL ...
itlwm: POOLTRACE[STEP8c-wl] boundary=WORKLOOPS_OK poolResult=BOTH_OK handoff=STEP8d
```

The runtime log must contain one factory FINAL line for TX, one factory
FINAL line for RX, and exactly one controller-level STEP 8b FINAL branch
line. If that branch is `BOTH_OK`, the log must continue to either a
downstream FINAL failure branch or `WORKLOOPS_OK handoff=STEP8d`.

## CHANGED FILES

changed files (CR-213-specific):
- `AirportItlwm/AirportItlwmV2.cpp`
  - Extend CR-212 passive logs into final branch logs in
    `AirportItlwmIO80211PacketPool::withName`.
  - Add STEP 8b begin/after/final branch logs.
  - Add immediate downstream STEP 8c/8c-wl final/handoff logs.
- `commit-approval/build_evidence/CR-213-build-end-to-end-pool-branch-instrumentation.txt`

## VERIFICATION PERFORMED

- build:
  - `scripts/build_tahoe.sh`: `BUILD SUCCEEDED`
  - BootKC undef-symbol verification:
    `OK: all 884 undefined symbols resolve against BootKC`
  - kext path:
    `Build/Debug/Tahoe/AirportItlwm.kext/Contents/MacOS/AirportItlwm`
  - kext sha256:
    `9723ec2d7ef333a8e8f4fa1df2080875d26935a508d7c3c3fffeba8a0b2eaaa1`
  - kext UUID:
    `3AC188DA-A10C-3541-97FD-861969956A44`
  - kext size: `16289520`
  - Build evidence file:
    `commit-approval/build_evidence/CR-213-build-end-to-end-pool-branch-instrumentation.txt`
- static checks:
  - `git diff --check HEAD`: PASS.
  - No direct active pbufpool probe call remains.
  - No `kIOSkywalkPacketTypeGeneric` differential path remains.
- before reproduction result: CR-209 evidence says pool factory reaches
  `initWithName=0`.
- after reproduction result: PENDING.

## STAGE RULES

- request_stage = STAGE_1_STRUCTURAL.
- after evidence: PENDING_HOST_REBOOT.
- after reproduction result: PENDING.
- request goal: reviewer structural approval to proceed with
  Stage 2 after-fix runtime collection on the exact reviewed HEAD
  and exact reviewed CR-213 cumulative artifact.
- commit is NOT requested at Stage 1. Commit may only be requested
  after successful after-fix runtime evidence.

## FORBIDDEN ALTERNATIVES CONSIDERED AND REJECTED

- active direct pbufpool probes: rejected; CR-211 showed this is not
  behavior-neutral under `DIAGNOSTIC_INSTRUMENTATION`.
- heuristic timing: not added.
- fallback path: not added.
- masking/suppression: not added.
- force callback / state / success: not added.
- forced sync / flush / barrier: not added.
- retry / reorder / poll loop: not added.
- alternate packet type A/B: not added.
- why rejected: this request must observe existing production branches
  through final points, not perturb kernel resource/accounting/timing
  state.

## PROPOSED COMMIT MESSAGE

```
AirportItlwm: trace STEP 8b pool branches to final points

Extend the passive pool diagnostic from CR-212 into full local
branch-to-final-point coverage for the STEP 8b pool creation failure.

The factory now logs final NEW_NULL, INIT_FALSE, and INIT_TRUE return
branches for each TX/RX pool. The controller logs STEP 8b begin,
after-TX, after-RX, and final TX_ONLY/RX_ONLY/TX_RX/BOTH_OK branches.
If both pools succeed, the trace follows the next boundaries through
queue creation and workloop attach so runtime evidence can prove whether
the pool error is closed or has moved downstream.

No contract behavior changes: Network packet type, PoolOptions,
ordering, ownership, cleanup, queue construction, and registration
handoffs are unchanged. No active pbufpool probes.

CR-213 / Stage 1.
```

## PATCH ARTIFACT

exact patch artifact:
`commit-approval/artifacts/CR-213-end-to-end-pool-branch-instrumentation.diff`

The artifact captures the cumulative staged/live diff at submission. It
is left **untracked** so it does not appear in `git diff --binary HEAD`
and matches that diff byte-for-byte under `cmp`.

The auditor may re-verify identity by:

```
git rev-parse HEAD
shasum -a 256 commit-approval/artifacts/CR-213-end-to-end-pool-branch-instrumentation.diff
wc -l           commit-approval/artifacts/CR-213-end-to-end-pool-branch-instrumentation.diff
git diff --binary HEAD | shasum -a 256
git diff --binary HEAD | wc -l
cmp -s <(git diff --binary HEAD) commit-approval/artifacts/CR-213-end-to-end-pool-branch-instrumentation.diff && echo OK
git diff --check HEAD
```

The two `shasum -a 256` outputs (artifact file and live diff) MUST be
identical. `cmp` is the binding identity gate.

## SUPERSEDES

supersedes (formal):
- CR-212 (insufficient branch-to-final-point coverage).
- CR-211 (rejected active pbufpool probe matrix).

implicitly invalidates:
- CR-210 rejection remains final; its Generic differential path is not
  restored.
- CR-209 Stage 1 approval is superseded by the extended diagnostic
  diff. CR-209 logging is preserved and extended.
- CR-208 Stage 1 approval remains superseded; its `poolFlags=1` source
  change is preserved on the production path.
