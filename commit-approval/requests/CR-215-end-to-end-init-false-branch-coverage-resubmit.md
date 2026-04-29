# CR-215 - End-to-end branch coverage for initWithName=0 hypothesis (DIAGNOSTIC_INSTRUMENTATION) — resubmission of CR-214

- date: 2026-04-29
- stage: STAGE_1_STRUCTURAL
- justification class: DIAGNOSTIC_INSTRUMENTATION
- branch: master
- HEAD: `d3a07c2abccac863e1909aa562051a6ee5687245`
- requested by: Executor+Committer (per `docs/WORKFLOW_ITLWM.md`)
- supersedes: CR-214 (REJECTED on 2026-04-29 for missing CR-213
  Stage 2 runtime evidence; see
  `commit-approval/decisions/COMMIT_DECISION_CR-214.md` lines 91-95).
- preserves: CR-213 Stage 1 approval scope on its end-to-end
  controller-and-downstream branch coverage; CR-214's source-side
  instrumentation is carried over verbatim.

does_this_fix_proven_current_root_cause: NO
This is diagnostic-only. Same source-side instrumentation as CR-214
(narrowing every `INIT_FALSE_<stage>` branch inside the framework's
`IOSkywalkPacketBufferPool::initWithName` plus unredacting all
pointer outputs). The only delta vs CR-214 is the addition of the
CR-213 Stage 2 runtime evidence files that CR-214 was rejected for
not filing.

## REJECTION_REMEDIATION

CR-214 was REJECTED with three findings (per `COMMIT_DECISION_CR-214.md`):

1. `runtime_evidence: FAIL` — request claimed CR-213 Stage 2 reboot
   evidence at 2026-04-29 09:55 with kext sha `9723ec2d…`, but no
   corresponding runtime/stage2 evidence file existed.
2. `completeness: FAIL` — premise about `INIT_FALSE` for TX/RX with
   `%p` redaction was not independently reviewable.
3. `REJECTION_REASONS` — Stage 1 cannot approve after-fix runtime on
   request prose alone when the request's purpose is to refine a
   prior runtime result.

CR-215 remediates by adding three reviewable evidence files
(REQUIRED_CHANGES_BEFORE_RESUBMISSION lines 96-101 of CR-214 decision):

| evidence | path | content |
|---|---|---|
| boot log | `commit-approval/runtime_evidence/CR-213-stage2-boot-log-20260429.txt` | 35 raw lines from `sudo log show` between 09:55:00 and 09:55:30, covering both TX/RX factory invocations, controller STEP 8b BEGIN/AFTER_TX/AFTER_RX/FINAL, the existing pre-CR-213 `DEBUG start [STEP 8b]` lines, and `IO80211Controller::stop` cleanup |
| loaded-kext identity | `commit-approval/runtime_evidence/CR-213-stage2-loaded-kext-20260429.txt` | `kextstat` line at boot showing UUID `3AC188DA-A10C-3541-97FD-861969956A44` matching CR-213 Stage 1 build evidence |
| structured Stage 2 evidence | `commit-approval/stage2_evidence/CR-213-stage2-evidence-20260429.md` | full Stage 2 evidence document with claim recap, identity verification, branch coverage observed (`INIT_FALSE` × 2 + `TX_RX failMask=0x3`), and explicit description of the `%p` privacy redaction blocker that CR-215 addresses |

## REQUIRED COVERAGE CRITERION

Same as CR-214 (per `feedback_diagnostic_end_to_end_criterion`):

```
end-to-end instrumentation to final points across all branches of
this error/hypothesis
```

For the
`IOSkywalkPacketBufferPool::initWithName(name, owner, packetType=Network, options) -> false`
hypothesis, every internal stage at which the framework can give
up has a named `INIT_FALSE_<stage>` final-point marker driven by a
read of the slot that stage writes:

- `INIT_FALSE_PRE_THCALL` — `thread_call_allocate_with_options`
  failed (KDK 0x9c89). slot[0xb0]=0.
- `INIT_FALSE_PRE_SEGSTATS` — `IOMallocTypeImpl(kalloc_type_view_127)`
  failed (KDK 0x9ca2). slot[0xb0]!=0, slot[0x78]=0.
- `INIT_FALSE_PRE_LOCK1` — first `IORecursiveLockAlloc` failed
  (KDK 0x9cb7). slot[0x78]!=0, slot[0x80]=0.
- `INIT_FALSE_PRE_LOCK2` — second `IORecursiveLockAlloc` failed
  (KDK 0x9ccc). slot[0x80]!=0, slot[0x88]=0.
- `INIT_FALSE_PRE_OWNER_CACHE` — never reached the post-IOBSD
  type/owner/poolFlags writes (KDK 0x9cea-0x9cf6). slot[0x88]!=0
  AND (slot[0x20]=0 OR slot[0x3c]=0).
- `INIT_FALSE_KPBP_REJECT` — `kern_pbufpool_create` rejected the
  kpinit, or returned 0 but did not populate `this[0x18]` (KDK
  0x9e84/0x9eb5). slot[0x88]!=0, slot[0x20]!=0, slot[0x3c]!=0,
  slot[0x18]=0.
- `INIT_FALSE_OSARRAY_FIRST` — first
  `OSArray::withCapacity(packetCount)` failed (KDK 0x9f06).
  slot[0x18]!=0, slot[0x68]=0.
- `INIT_FALSE_OSARRAY_SECOND` — second
  `OSArray::withCapacity(...)` failed (KDK 0x9f27). slot[0x68]!=0,
  slot[0x60]=0.
- `INIT_FALSE_POST_OSARRAY` — packet-inventory loop or other late
  failure. slot[0x60]!=0.

`INIT_TRUE`, `NEW_NULL`, `TX_ONLY` / `RX_ONLY` / `TX_RX` /
`BOTH_OK`, `DOWNSTREAM_QUEUE_FAIL` / `QUEUES_OK`,
`DOWNSTREAM_WORKLOOP_FAIL` / `WORKLOOPS_OK handoff=STEP8d`
are preserved verbatim from CR-213 with `%p` → `0x%llx` rendering.

## SYMPTOM

Same Stage 2 boot symptom as CR-208 / CR-209 / CR-213. Now backed by
the filed CR-213 evidence file
`commit-approval/runtime_evidence/CR-213-stage2-boot-log-20260429.txt`,
which contains the literal lines:

```
POOLTRACE[STEP8b] BEGIN owner=<private> opts=<private>
   pktCount=256 bufCount=256 bufSize=2048 maxBPP=1 memSegSz=0 poolFlags=0x1
PACKETPOOL[AirportItlwm-TX] new=<private> ...
PACKETPOOL[AirportItlwm-TX] initWithName=0 (type=1 slot18=<private> slot20=<private>)
PACKETPOOL[AirportItlwm-TX] FINAL branch=INIT_FALSE preRelease ...
PACKETPOOL[AirportItlwm-TX] FAIL: initWithName returned false; pool released to NULL
PACKETPOOL[AirportItlwm-TX] FINAL branch=INIT_FALSE return=<private>
[symmetric pattern for AirportItlwm-RX]
POOLTRACE[STEP8b] FINAL branch=TX_RX failMask=0x3 tx=0x0 rx=0x0
   cleanup=super_stop_releaseAll_disarm_return_false
DEBUG start [STEP 8b] FAIL: pool creation (TX=0x0 RX=0x0)
IO80211Controller::stop[868]
```

`<private>` redaction blocks slot/pool numerics. CR-215 unredacts and
narrows the failure to a specific framework-internal stage.

## DIVERGENCE

- exact divergence point: still inside the framework
  `IOSkywalkPacketBufferPool::initWithName` flow (KDK
  `IOSkywalkFamily.kext` `0x9bf0`).
- confirmed deviation: `initWithName` returns false symmetrically
  for TX and RX after `new` succeeds (CR-209 evidence + CR-213
  Stage 2 evidence file now filed).
- confirmed root cause: TO_BE_DETERMINED by CR-215 runtime evidence.
- exact confirmed deviation removed: NONE in this CR.
- exact semantic mismatch removed: NONE in this CR.

## CLAIM SCOPE

- exact claim scope:
  1. keep production `packetType = kIOSkywalkPacketTypeNetwork`;
  2. keep production PoolOptions unchanged (`poolFlags=1`);
  3. read every slot the framework's initWithName writes
     (chronological per KDK 0x9c5f..0x9f24): name(0x98),
     thCall(0xb0), segStats(0x78), lock1(0x80), lock2(0x88),
     owner(0x20), pbufpool(0x18), arr1(0x68), arr2(0x60),
     typeCache(0x3c), flagsCache(0x48), singleSeg(0xb8),
     disposed(0xba);
  4. classify INIT_FALSE into the named INIT_FALSE_<stage>
     branches enumerated in REQUIRED COVERAGE CRITERION above;
  5. emit `0x%llx` (via `(uintptr_t)` cast) for every pointer in
     the factory PACKETPOOL[…] lines and in the controller
     POOLTRACE[STEP8b]/STEP8c lines, so os_log default-private
     redaction does not blind the evidence;
  6. preserve all CR-213 controller-level branch markers
     (`TX_ONLY`/`RX_ONLY`/`TX_RX`/`BOTH_OK`,
     `DOWNSTREAM_QUEUE_FAIL`/`QUEUES_OK`,
     `DOWNSTREAM_WORKLOOP_FAIL`/`WORKLOOPS_OK handoff=STEP8d`).
- non_claims:
  - This request does NOT claim a fix.
  - This request does NOT instrument private closed-source framework
    branches directly.
  - This request does NOT call `kern_pbufpool_create` /
    `kern_pbufpool_destroy` directly (CR-211 was rejected for that;
    CR-215 stays strictly passive).
  - This request does NOT change any system-facing argument, return
    value, state transition, ownership, ordering, callback, queue,
    or registration contract.
  - This request does NOT add retry, fallback, replay, polling,
    delay, forced state, or forced success.

## JUSTIFICATION PATH

justification path: DIAGNOSTIC_INSTRUMENTATION

- exact hypotheses being disambiguated (one-of-N selector, all
  branches covered): same as CR-214; see REQUIRED COVERAGE
  CRITERION list above.

- exact probe points:
  - `AirportItlwm/AirportItlwmV2.cpp` —
    `AirportItlwmIO80211PacketPool::withName`, immediately after
    `new` and immediately after `pool->initWithName(...)`. Reads
    are passive memory loads from our own subclass instance (we
    own the memory until `OSSafeReleaseNULL`).
  - `AirportItlwm/AirportItlwmV2.cpp` —
    `AirportItlwm::start` STEP 8b and STEP 8c boundaries:
    pointer-only format-string substitutions (`%p` → `0x%llx`).

- why instrumentation is behavior-neutral:
  - The same `new`, `initWithName(name, owner, packetType,
    options)`, queue constructors, workloop `addEventSource`
    calls, cleanup calls, and return values run in the same
    order with the same arguments.
  - The added slot reads are non-mutating loads from our own
    object's memory; they do not touch framework state.
  - The added `INIT_FALSE_<stage>` decision tree is a pure string
    select; it does not branch the production cleanup path.
  - Pointer rendering changes (`%p` → `0x%llx`) do not change any
    value passed to a system call; they only change the log
    output format.
  - No retry, fallback, masking, forced state, or workaround.
  - +0 BootKC undefined symbols.

- exact runtime evidence expected from this instrumentation:

  Factory branch evidence per TX/RX:
  ```
  itlwm: PACKETPOOL[AirportItlwm-TX] new=0x<P> poolVtable=0x<VT>
        (size=200 opts=0x<O> owner=0x<W> ownerVtable=0x<OV>
         pktCount=256 bufCount=256 bufSize=2048 maxBPP=1 memSegSz=0
         poolFlags=0x1 type=1)
  itlwm: PACKETPOOL[AirportItlwm-TX] initWithName=0 (type=1) slots:
        name=0x<...> thCall=0x<...> segStats=0x<...> lock1=0x<...>
        lock2=0x<...> owner=0x<...> pbufpool=0x<...>
        arr1=0x<...> arr2=0x<...> typeCache=<U> flagsCache=0x<X>
        singleSeg=<U> disposed=<U>
  itlwm: PACKETPOOL[AirportItlwm-TX] FINAL branch=INIT_FALSE_<STAGE>
        preRelease pool=0x<P> pbufpool=0x<...> owner=0x<...>
        arr1=0x<...> arr2=0x<...> disposed=<U>
  itlwm: PACKETPOOL[AirportItlwm-TX] FAIL: initWithName returned false;
        pool released to NULL
  itlwm: PACKETPOOL[AirportItlwm-TX] FINAL branch=INIT_FALSE_<STAGE>
        return=0x0
  ```

  Controller and downstream evidence: same lines as CR-213 with all
  pointer arguments now rendered as `0x%llx` rather than
  `<private>`-redacted `%p`.

## CHANGED FILES

changed files (CR-215-specific delta vs HEAD):
- `AirportItlwm/AirportItlwmV2.cpp` — same source-side change as
  CR-214 (carried verbatim).
- `commit-approval/build_evidence/CR-214-build-end-to-end-init-false.txt`
  — preserved (CR-214 build evidence; the build is identical for
  CR-215 because the source is unchanged).
- `commit-approval/runtime_evidence/CR-213-stage2-boot-log-20260429.txt`
  — NEW (the missing CR-213 runtime evidence the CR-214 rejection
  required).
- `commit-approval/runtime_evidence/CR-213-stage2-loaded-kext-20260429.txt`
  — NEW (CR-213 loaded-kext identity).
- `commit-approval/stage2_evidence/CR-213-stage2-evidence-20260429.md`
  — NEW (structured Stage 2 evidence document for CR-213).

The CR-215 cumulative artifact carries forward all previously staged
content (CR-201 / 203 / 204 / 206 / 208 / 209 / 213 / 214).

## DIFF SUMMARY

CR-215 source-side diff is byte-identical to CR-214's source-side
diff (verified by `diff` of the `AirportItlwm/AirportItlwmV2.cpp`
hunks). Only the evidence-file additions distinguish CR-215 from
CR-214.

## EVIDENCE FROM DECOMP

Same as CR-214; see `IOSkywalkPacketBufferPool::initWithName` slot
chronology mapping in EVIDENCE FROM DECOMP of
`commit-approval/requests/CR-214-end-to-end-init-false-branch-coverage.md`.
Anchor offsets: `0x9c5f`, `0x9c7f`, `0x9c89`, `0x9ca2`, `0x9cb7`,
`0x9ccc`, `0x9cea`, `0x9cee`, `0x9cf6`, `0x9d60`, `0x9e7b`, `0x9e84`,
`0x9eb5`, `0x9ef3`, `0x9f06`, `0x9f27`. Each maps to a named
`INIT_FALSE_<stage>` branch.

## EVIDENCE FROM RUNTIME

The CR-213 Stage 2 evidence is now FILED (the precise gap CR-214
was rejected for):

- `commit-approval/runtime_evidence/CR-213-stage2-boot-log-20260429.txt`
  — 35 lines covering TX and RX factory invocations, controller
  STEP 8b BEGIN/AFTER_TX/AFTER_RX/FINAL lines, the existing
  pre-CR-213 `DEBUG start [STEP 8b]` lines, and
  `IO80211Controller::stop` cleanup.
- `commit-approval/runtime_evidence/CR-213-stage2-loaded-kext-20260429.txt`
  — `kextstat` line confirming UUID
  `3AC188DA-A10C-3541-97FD-861969956A44` matches the Stage 1
  reviewed kext identity from
  `commit-approval/decisions/COMMIT_DECISION_CR-213.md` line 39.
- `commit-approval/stage2_evidence/CR-213-stage2-evidence-20260429.md`
  — structured Stage 2 evidence document with explicit branch
  coverage attestation (`INIT_FALSE` × 2 + `TX_RX failMask=0x3`)
  and a precise description of the `%p` privacy redaction
  blocker.

after-runtime evidence for CR-215 itself: PENDING — Stage 2 will
run after host boots the CR-215 kext (sha
`fb86f74f13543fac72ffa5cf8eb60e11abd4e7056b951df5036254d36de60f52`,
UUID `B2A2AA8A-02DA-31D7-98F6-7437B1F91EEA`; identical to CR-214
since the source-side diff is unchanged).

why this runtime evidence is semantically significant: the now-filed
CR-213 evidence anchors the premise that CR-215's source-side change
narrows. CR-215's own Stage 2 will identify exactly one
`INIT_FALSE_<stage>` branch with all pointer values readable.

why this is not trace-order / object-id noise: each CR-213 line is
deterministic per factory invocation; both TX and RX reproduce
symmetrically. CR-215's slot reads are independent loads with no
aliasing.

## CAUSALITY

- regression window: `8e05ddf` → `d3a07c2` (unchanged).
- pinpointed divergence path: TBD by CR-215 runtime.
- why this is root-cause-discovery rather than guesswork:
  - CR-208's `poolFlags=1` hypothesis was insufficient (Stage 2 fail).
  - CR-209 / CR-213 narrowed to the framework-internal initWithName
    failure boundary (now reviewable via filed CR-213 evidence).
  - Static disasm of the framework's initWithName + `_kern_pbufpool_create`
    + `_pp_create` enumerates ~9 mutually-exclusive failure points.
  - CR-215 reads the slot signature that uniquely identifies each
    failure point and emits a named branch label in one boot.

## VERIFICATION PERFORMED

- build: identical to CR-214 (no source-side change vs CR-214).
  - `scripts/build_tahoe.sh`: `BUILD SUCCEEDED`
  - BootKC undef-symbol verification:
    `OK: all 884 undefined symbols resolve against BootKC`
  - kext path:
    `Build/Debug/Tahoe/AirportItlwm.kext/Contents/MacOS/AirportItlwm`
  - kext sha256:
    `fb86f74f13543fac72ffa5cf8eb60e11abd4e7056b951df5036254d36de60f52`
  - kext UUID:
    `B2A2AA8A-02DA-31D7-98F6-7437B1F91EEA`
  - kext size: `16289520`
  - Build evidence file (preserved from CR-214):
    `commit-approval/build_evidence/CR-214-build-end-to-end-init-false.txt`
  - kext currently installed in `/Library/Extensions/AirportItlwm.kext`
    (root:wheel) — installed during CR-214 cycle, identity unchanged.
- static checks:
  - `git diff --check HEAD`: PASS.
  - No direct active pbufpool probe.
  - No `kIOSkywalkPacketTypeGeneric` differential path.
  - No retry / fallback / masking / forced state.
- before reproduction result: as documented in EVIDENCE FROM RUNTIME.
- after reproduction result: PENDING.

## STAGE RULES

- request_stage = STAGE_1_STRUCTURAL.
- after evidence: PENDING_HOST_REBOOT.
- after reproduction result: PENDING.
- request goal: reviewer structural approval to proceed with Stage 2
  after-fix runtime collection on the exact reviewed HEAD and exact
  reviewed CR-215 cumulative artifact.
- commit is NOT requested at Stage 1.

## RESIDUAL UNCERTAINTY

Same as CR-214 — see RESIDUAL UNCERTAINTY of
`commit-approval/requests/CR-214-end-to-end-init-false-branch-coverage.md`.

## FORBIDDEN ALTERNATIVES CONSIDERED AND REJECTED

- active direct pbufpool probes: REJECTED (CR-211 lesson).
- heuristic timing: not added.
- fallback path: not added.
- masking/suppression: not added.
- force callback / state / success: not added.
- forced sync / flush / barrier: not added.
- retry / reorder / poll loop: not added.
- alternate packet type A/B (CR-210 pattern): not added.
- modifying any `docs/*PROTOCOL*.md` or `*TEMPLATE*.md`: not added
  (per `feedback_no_modify_protocols`).
- modifying any prior CR's request/decision file: not added (per
  `feedback_no_delete_submitted_requests` and the rule against
  modifying audit-trail records).

## PROPOSED COMMIT MESSAGE

```
AirportItlwm: end-to-end branch coverage for initWithName=0 (resubmit)

Resubmit CR-214 with the missing CR-213 Stage 2 runtime evidence
filed under commit-approval/runtime_evidence and a structured
Stage 2 evidence document under commit-approval/stage2_evidence.

Source-side instrumentation is byte-identical to CR-214: enumerate
every slot the framework's IOSkywalkPacketBufferPool::initWithName
writes (chronological per KDK 0x9c5f..0x9f24), classify INIT_FALSE
into a named INIT_FALSE_<stage> branch, and render every pointer
through (uintptr_t) cast as 0x%llx so os_log default-private
redaction does not blind the runtime evidence.

The same 0x%llx unredaction is applied to controller-level
POOLTRACE[STEP8b]/STEP8c BEGIN/AFTER/FINAL/handoff lines so the
next fix CR can match factory-side pool addresses to controller-side
fTxPool / fRxPool.

Pure DIAGNOSTIC_INSTRUMENTATION: same new/initWithName/queues/
addEventSource/cleanup/return semantics; no active pbufpool probes;
no retry/fallback/masking/forced state. +0 BootKC undefs.

CR-215 / Stage 1.
```

## PATCH ARTIFACT

exact patch artifact:
`commit-approval/artifacts/CR-215-end-to-end-init-false-branch-coverage-resubmit.diff`

The artifact captures the cumulative staged/live diff at submission.
It is left **untracked** so it does not appear in `git diff --binary HEAD`
and matches that diff byte-for-byte under `cmp`.

The auditor may re-verify identity by:

```
git rev-parse HEAD
shasum -a 256 commit-approval/artifacts/CR-215-end-to-end-init-false-branch-coverage-resubmit.diff
wc -l           commit-approval/artifacts/CR-215-end-to-end-init-false-branch-coverage-resubmit.diff
git diff --binary HEAD | shasum -a 256
git diff --binary HEAD | wc -l
cmp -s <(git diff --binary HEAD) commit-approval/artifacts/CR-215-end-to-end-init-false-branch-coverage-resubmit.diff && echo OK
git diff --check HEAD
```

The two `shasum -a 256` outputs (artifact file and live diff) MUST
be identical. `cmp` is the binding identity gate.

## SUPERSEDES

supersedes (formal):
- CR-214 (REJECTED for missing Stage 2 evidence file). CR-214's
  source-side instrumentation is preserved verbatim; only the
  evidence-file gap is closed.

implicitly invalidates (per `approval_invalid_if_diff_changes: YES`):
- none beyond CR-214; the CR-214 chain (CR-213, CR-211 rejection,
  CR-210 rejection, CR-209 logging, CR-208 `poolFlags=1`) is preserved.

## EVIDENCE_FILE_INVENTORY

- `commit-approval/runtime_evidence/CR-213-stage2-boot-log-20260429.txt`
  (NEW)
- `commit-approval/runtime_evidence/CR-213-stage2-loaded-kext-20260429.txt`
  (NEW)
- `commit-approval/stage2_evidence/CR-213-stage2-evidence-20260429.md`
  (NEW)
- `commit-approval/decisions/COMMIT_DECISION_CR-213.md` (preserved)
- `commit-approval/decisions/COMMIT_DECISION_CR-214.md` (preserved)
- `commit-approval/build_evidence/CR-214-build-end-to-end-init-false.txt`
  (preserved; identical for CR-215)
- `commit-approval/requests/CR-214-end-to-end-init-false-branch-coverage.md`
  (preserved; CR-215 supersedes but does not delete)
- `commit-approval/artifacts/CR-214-end-to-end-init-false-branch-coverage.diff`
  (preserved)
