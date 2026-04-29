# CR-214 - End-to-end branch coverage for initWithName=0 hypothesis (DIAGNOSTIC_INSTRUMENTATION)

- date: 2026-04-29
- stage: STAGE_1_STRUCTURAL
- justification class: DIAGNOSTIC_INSTRUMENTATION
- branch: master
- HEAD: `d3a07c2abccac863e1909aa562051a6ee5687245`
- requested by: Executor+Committer (per `docs/WORKFLOW_ITLWM.md`)
- supersedes: CR-213. CR-213 collected the right Stage 2 runtime
  evidence to localize the failure to `INIT_FALSE` (factory) and
  `TX_RX failMask=0x3` (controller), but every pointer it logged was
  `<private>`-redacted by os_log on Tahoe (default-private `%p`),
  blocking the next axis: which framework-internal stage of
  `IOSkywalkPacketBufferPool::initWithName` was the last to complete
  before the failure. CR-214 satisfies the end-to-end criterion for
  this hypothesis: every framework-internal write that the
  initWithName flow performs (per KDK `IOSkywalkFamily.kext`
  `0x9c5f..0x9f24` disasm) is read after-the-fact, classified into a
  named `INIT_FALSE_<stage>` branch, and emitted with unredacted
  `0x%llx` pointer values.

does_this_fix_proven_current_root_cause: NO
This is diagnostic-only. It does not change the pool contract,
retry, fallback, packet type, PoolOptions, ownership, queue
construction, or registration behavior. It refines CR-213's narrow
slot18/slot20 reads into a full chronological enumeration of the
framework's internal init slots and renders pointer values without
os_log privacy redaction.

## REQUIRED COVERAGE CRITERION

The criterion for this request is the same as CR-213, applied to
the next-narrower hypothesis:

```
end-to-end instrumentation to final points across all branches of
this error/hypothesis
```

Specifically for the
`IOSkywalkPacketBufferPool::initWithName(name, owner, packetType=Network, options) -> false`
hypothesis, every internal stage at which the framework can give
up and return false has a named `INIT_FALSE_<stage>` final-point
marker driven by reads of the slots that stage writes:

- `INIT_FALSE_PRE_THCALL` — `thread_call_allocate_with_options`
  failed (KDK 0x9c89). slot[0xb0]=0.
- `INIT_FALSE_PRE_SEGSTATS` — `IOMallocTypeImpl(kalloc_type_view_127)`
  failed (KDK 0x9ca2). slot[0xb0]!=0, slot[0x78]=0.
- `INIT_FALSE_PRE_LOCK1` — first `IORecursiveLockAlloc` failed
  (KDK 0x9cb7). slot[0x78]!=0, slot[0x80]=0.
- `INIT_FALSE_PRE_LOCK2` — second `IORecursiveLockAlloc` failed
  (KDK 0x9ccc). slot[0x80]!=0, slot[0x88]=0.
- `INIT_FALSE_PRE_OWNER_CACHE` — never reached the post-IOBSD
  type/owner/poolFlags writes (KDK 0x9cea-0x9cf6). slot[0x88]!=0,
  slot[0x20]=0 or slot[0x3c]=0.
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

In addition, all `%p`-formatted pointer outputs in the local factory
and in the controller-level STEP 8b / STEP 8c traces are converted
to `0x%llx` through a `(uintptr_t)` cast, so every branch's evidence
line is fully readable without enabling system-wide private-data
logging.

The `INIT_TRUE`, `NEW_NULL`, `TX_ONLY` / `RX_ONLY` / `TX_RX` /
`BOTH_OK`, `DOWNSTREAM_QUEUE_FAIL` / `QUEUES_OK`,
`DOWNSTREAM_WORKLOOP_FAIL` / `WORKLOOPS_OK` end-points carried over
from CR-213 are preserved unchanged in semantic but with their
pointer values now unredacted.

## SYMPTOM

Same Stage 2 boot symptom as CR-208 / CR-209 / CR-213. CR-213 Stage 2
reboot 2026-04-29 09:55 (kext sha `9723ec2d…` UUID `3AC188DA-…`)
pinpointed the local outcome:

```
PACKETPOOL[AirportItlwm-TX] new=<private> ...
PACKETPOOL[AirportItlwm-TX] initWithName=0 (type=1 slot18=<private> slot20=<private>)
PACKETPOOL[AirportItlwm-TX] FINAL branch=INIT_FALSE preRelease pool=<private> slot18=<private> slot20=<private>
PACKETPOOL[AirportItlwm-TX] FINAL branch=INIT_FALSE return=<private>
[same for RX]
POOLTRACE[STEP8b] FINAL branch=TX_RX failMask=0x3 ...
```

`<private>` redaction blocks slot/pool numerics. CR-214 unredacts and
narrows the failure to a specific framework-internal stage.

## DIVERGENCE

- exact divergence point: still inside the framework
  `IOSkywalkPacketBufferPool::initWithName` flow (KDK
  `IOSkywalkFamily.kext` `0x9bf0`).
- confirmed deviation: `initWithName` returns false symmetrically for
  TX and RX after `new` succeeds (CR-209/CR-213 evidence).
- confirmed root cause: TO_BE_DETERMINED by CR-214 runtime evidence.
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
  4. classify INIT_FALSE into `INIT_FALSE_PRE_THCALL`,
     `_PRE_SEGSTATS`, `_PRE_LOCK1`, `_PRE_LOCK2`,
     `_PRE_OWNER_CACHE`, `_KPBP_REJECT`, `_OSARRAY_FIRST`,
     `_OSARRAY_SECOND`, `_POST_OSARRAY`;
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
    `kern_pbufpool_destroy` directly (CR-211 was rejected for that).
  - This request does NOT change any system-facing argument, return
    value, state transition, ownership, ordering, callback, queue,
    or registration contract.
  - This request does NOT add retry, fallback, replay, polling,
    delay, forced state, or forced success.

## JUSTIFICATION PATH

justification path: DIAGNOSTIC_INSTRUMENTATION

- exact hypotheses being disambiguated (one-of-N selector, all
  branches covered):
  1. thread_call_allocate failure → `INIT_FALSE_PRE_THCALL`.
  2. IOMallocTypeImpl failure → `INIT_FALSE_PRE_SEGSTATS`.
  3. lock1 / lock2 failure → `INIT_FALSE_PRE_LOCK1` /
     `INIT_FALSE_PRE_LOCK2`.
  4. waitForService("IOBSD") returning before owner cache wrote →
     `INIT_FALSE_PRE_OWNER_CACHE`.
  5. kern_pbufpool_create rejected our kpinit, or returned 0 with
     unpopulated handle → `INIT_FALSE_KPBP_REJECT`. **Most likely
     hypothesis given current evidence.**
  6. first OSArray::withCapacity failure →
     `INIT_FALSE_OSARRAY_FIRST`.
  7. second OSArray::withCapacity failure →
     `INIT_FALSE_OSARRAY_SECOND`.
  8. packet-inventory loop or later failure →
     `INIT_FALSE_POST_OSARRAY`.
  9. `INIT_TRUE` and `NEW_NULL` complete the hypothesis space.

- exact probe points:
  - `AirportItlwm/AirportItlwmV2.cpp` —
    `AirportItlwmIO80211PacketPool::withName`, immediately after
    `new` and immediately after `pool->initWithName(...)`. Reads
    are passive memory loads from our own subclass instance (we
    own the memory until OSSafeReleaseNULL).
  - `AirportItlwm/AirportItlwmV2.cpp` —
    `AirportItlwm::start` STEP 8b and STEP 8c boundaries:
    pointer-only format-string substitutions (`%p` → `0x%llx`).
    Same set of XYLog calls; same arguments; only render format
    changes.

- why instrumentation is behavior-neutral:
  - The same `new`, `initWithName(name, owner, packetType,
    options)`, queue constructors, workloop `addEventSource`
    calls, cleanup calls, and return values run in the same
    order with the same arguments.
  - The new slot reads are non-mutating loads from our own
    object's memory; they do not touch framework state.
  - The added `INIT_FALSE_<stage>` decision tree is a pure
    string select; it does not branch the production cleanup
    path, which still runs `OSSafeReleaseNULL(pool)` and returns
    the (now-NULL) pool pointer just like CR-213.
  - Pointer rendering changes (`%p` → `0x%llx`) do not change
    any value passed to a system call; they only change the
    log output format.
  - No retry, fallback, masking, forced state, or workaround.
  - +0 BootKC undefined symbols (no new external KPI).

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
  itlwm: PACKETPOOL[AirportItlwm-TX] FAIL: initWithName returned false; pool released to NULL
  itlwm: PACKETPOOL[AirportItlwm-TX] FINAL branch=INIT_FALSE_<STAGE> return=0x0
  ```

  Controller and downstream evidence: same lines as CR-213 with all
  pointer arguments now rendered as `0x%llx` rather than
  `<private>`-redacted `%p`.

## CHANGED FILES

changed files (CR-214-specific):
- `AirportItlwm/AirportItlwmV2.cpp`
- `commit-approval/build_evidence/CR-214-build-end-to-end-init-false.txt`

The CR-214 cumulative artifact carries forward all previously staged
content (CR-201/203/204/206/208/209/213).

## DIFF SUMMARY

```
AirportItlwmIO80211PacketPool::withName factory:
  - replace `void *slot18 = ...; void *slot20 = ...;` with full
    chronological enumeration of name(0x98), thCall(0xb0),
    segStats(0x78), lock1(0x80), lock2(0x88), owner(0x20),
    pbufpool(0x18), arr1(0x68), arr2(0x60), typeCache(0x3c),
    flagsCache(0x48), singleSeg(0xb8), disposed(0xba) reads.
  - replace `%p` outputs with `0x%llx` via `(uintptr_t)` cast.
  - classify INIT_FALSE into INIT_FALSE_<stage> branch label
    based on the highest-progressed non-zero slot.
  - same OSSafeReleaseNULL + return semantics; same arguments.

AirportItlwm::start STEP 8b / STEP 8c POOLTRACE lines:
  - replace `%p` outputs with `0x%llx` via `(uintptr_t)` cast,
    leaving call ordering and arguments unchanged.
  - branch-label semantics (TX_ONLY/RX_ONLY/TX_RX/BOTH_OK,
    DOWNSTREAM_QUEUE_FAIL/QUEUES_OK) preserved verbatim.
```

## EVIDENCE FROM DECOMP

- `_kern_pbufpool_create` body (BootKC `0xffffff80009f1d30`)
  including the call to `_pp_create` (BootKC `0xffffff80009ed050`)
  at offset `+0x2b4`. Carried forward from CR-211/CR-213.
- `IOSkywalkPacketBufferPool::initWithName` body (KDK
  `IOSkywalkFamily.kext` `0x9bf0`). Each of the named CR-214
  stages corresponds to a specific write/check pair in this
  function:
  - 0x9c5f: `OSString::withCString(name) -> this[0x98]`.
  - 0x9c7f: `thread_call_allocate_with_options(...) -> this[0xb0]`,
            then `0x9c89 testq+je 0x9e84` on NULL.
  - 0x9c9b: `IOMallocTypeImpl(kalloc_type_view_127) -> this[0x78]`,
            then `0x9ca2 testq+je 0x9e84`.
  - 0x9cad: `IORecursiveLockAlloc -> this[0x80]`, then
            `0x9cb7 testq+je 0x9e84`.
  - 0x9cc2: `IORecursiveLockAlloc -> this[0x88]`, then
            `0x9ccc testq+je 0x9e84`.
  - 0x9ce5: `IOService::waitForService("IOBSD") (blocking)`.
  - 0x9cea: `this[0x3c] = packetType`.
  - 0x9cee: `this[0x20] = owner`.
  - 0x9cf2-0x9cf6: `this[0x48] = poolFlags`.
  - 0x9d60: `this[0xb8] = 1` if `poolFlags & 2`.
  - 0x9e7b: `kern_pbufpool_create(&kpinit, &this[0x18], &mem_info)`,
            then `0x9e80 testl+je 0x9eb1` on success vs `je 0x9e84`
            on failure; plus `0x9eb1 cmpq+je 0x9e84` if the handle
            stayed NULL despite zero return.
  - 0x9ebe..0x9ee6: copy `mem_info` into `this[0x28..0x40]`.
  - 0x9ef3: `this[0xba] = 0` (mDisposed).
  - 0x9efa: `OSArray::withCapacity -> this[0x68]`, then
            `0x9f06 testq+je 0x9e84`.
  - 0x9f1b-0x9f24: `OSArray::withCapacity -> this[0x60]`, then
                   `0x9f27 testq+je 0x9e84`.
- The chronological order of writes guarantees that the
  highest-progressed non-zero slot value (read by CR-214 after
  initWithName returns false) is the last completed stage; the
  next stage's slot is therefore the failing one.

## EVIDENCE FROM RUNTIME

- panic logs: none.
- driver / kext logs (CR-213 Stage 2 reboot 2026-04-29 09:55, sha
  `9723ec2d7ef333a8e8f4fa1df2080875d26935a508d7c3c3fffeba8a0b2eaaa1`,
  UUID `3AC188DA-A10C-3541-97FD-861969956A44`): factory reaches
  `initWithName=0` and the controller emits
  `POOLTRACE[STEP8b] FINAL branch=TX_RX failMask=0x3`. All pointer
  values were `<private>`-redacted, blocking the next axis.
- ioreg / state evidence: no `wlan0` interface; no
  `IO80211SkywalkInterface`; CoreWLAN scan returns empty.
- packet / firmware / transport trace: N/A (failure precedes
  packet datapath).
- before evidence: as above.
- after evidence: PENDING — Stage 2 will run after host boots the
  CR-214 kext (sha
  `fb86f74f13543fac72ffa5cf8eb60e11abd4e7056b951df5036254d36de60f52`,
  UUID `B2A2AA8A-02DA-31D7-98F6-7437B1F91EEA`).
- why this runtime evidence is semantically significant: CR-214's
  output identifies exactly one of nine
  `INIT_FALSE_<stage>`/`INIT_TRUE`/`NEW_NULL` branches, with all
  pointer values readable. The next fix CR is then constrained
  to a single mechanism rather than the open-ended
  "kpbp_create or wrapper or anything else" axis.
- why this is not trace-order / object-id noise: each probe pair
  is deterministic per factory invocation; both TX and RX
  reproduce. Slot reads are independent loads with no aliasing.

## CAUSALITY

- regression window: `8e05ddf` → `d3a07c2` (unchanged).
- pinpointed divergence path: TBD by CR-214 runtime.
- why this is root-cause-discovery rather than guesswork:
  - CR-208's `poolFlags=1` hypothesis was insufficient (Stage 2 fail).
  - CR-209 / CR-213 narrowed to the framework-internal initWithName
    failure boundary.
  - Static disasm of the framework's initWithName + kern_pbufpool_create
    + pp_create enumerates ~9 mutually-exclusive failure points.
  - CR-214 reads the slot signature that uniquely identifies each
    failure point and emits a named branch label in one boot.

## VERIFICATION PERFORMED

- build:
  - `scripts/build_tahoe.sh`: `BUILD SUCCEEDED`
  - BootKC undef-symbol verification:
    `OK: all 884 undefined symbols resolve against BootKC`
    (no new external KPI vs CR-213; the read of own-instance
    memory is internal).
  - kext path:
    `Build/Debug/Tahoe/AirportItlwm.kext/Contents/MacOS/AirportItlwm`
  - kext sha256:
    `fb86f74f13543fac72ffa5cf8eb60e11abd4e7056b951df5036254d36de60f52`
  - kext UUID:
    `B2A2AA8A-02DA-31D7-98F6-7437B1F91EEA`
  - kext size: `16289520`
  - Build evidence file:
    `commit-approval/build_evidence/CR-214-build-end-to-end-init-false.txt`
  - kext installed in `/Library/Extensions/AirportItlwm.kext` (root:wheel).
- static checks:
  - `git diff --check HEAD`: PASS.
  - No direct active pbufpool probe (CR-211 lesson honored).
  - No `kIOSkywalkPacketTypeGeneric` differential path.
  - No retry / fallback / masking / forced state.
- before reproduction result: as documented in EVIDENCE FROM RUNTIME.
- after reproduction result: PENDING.

## STAGE RULES

- request_stage = STAGE_1_STRUCTURAL.
- after evidence: PENDING_HOST_REBOOT.
- after reproduction result: PENDING.
- request goal: reviewer structural approval to proceed with
  Stage 2 after-fix runtime collection on the exact reviewed
  HEAD and exact reviewed CR-214 cumulative artifact.
- commit is NOT requested at Stage 1.

## RESIDUAL UNCERTAINTY

- If Stage 2 emits `INIT_FALSE_KPBP_REJECT` (the most likely branch
  given existing evidence), the next fix CR will need to align our
  kpinit / PoolOptions with whatever AppleBCMWLAN's reference
  Network pool sets that we don't. CR-208 already aligned
  `poolFlags=1` (via the `IOSkywalkPacketBufferPool::initWithName`
  flag-mapping at `0x9d54`) but that proved insufficient. The next
  candidate axes are:
  - `kbi_buf_seg_size` (we pass 0; AppleBCMWLAN also passes 0 but
    may rely on a memorySegmentSize derived from device parameters).
  - `kbi_packets` / `kbi_bufsize` exact values (we pass 256/2048;
    AppleBCMWLAN passes device-driven values).
  - Owner type discipline: the framework expects owner to be a
    registered IOSkywalkInterface; ours is `fNetIf` which is the
    correct subclass but not yet published.
- If Stage 2 emits any other `INIT_FALSE_<stage>` branch, the next
  CR is constrained to that mechanism's specific symbols and
  parameters.

## FORBIDDEN ALTERNATIVES CONSIDERED AND REJECTED

- active direct pbufpool probes: REJECTED. CR-211 was rejected for
  this exact pattern; CR-214 stays strictly passive.
- heuristic timing: not added.
- fallback path: not added.
- masking/suppression: not added.
- force callback / state / success: not added.
- forced sync / flush / barrier: not added.
- retry / reorder / poll loop: not added.
- alternate packet type A/B (CR-210 pattern): not added; production
  packetType remains `kIOSkywalkPacketTypeNetwork`.
- modifying any `docs/*PROTOCOL*.md` or `*TEMPLATE*.md`: not added
  (per `feedback_no_modify_protocols` rule, protocols are immutable
  input).
- why rejected: this request must observe existing production
  branches through final points, not perturb kernel state or
  protocol contract.

## PROPOSED COMMIT MESSAGE

```
AirportItlwm: end-to-end branch coverage for initWithName=0

CR-213 ran the right Stage 2 reboot but every pointer it logged was
<private>-redacted by os_log on Tahoe (default-private %p). CR-214
satisfies the end-to-end criterion for the
"IOSkywalkPacketBufferPool::initWithName returns false" hypothesis:
read every internal slot the framework's initWithName writes
(chronological per KDK 0x9c5f..0x9f24), classify INIT_FALSE into a
named INIT_FALSE_<stage> branch, and render every pointer through
(uintptr_t) cast as 0x%llx so the runtime evidence is fully readable.

The same 0x%llx unredaction is applied to controller-level
POOLTRACE[STEP8b]/STEP8c BEGIN/AFTER/FINAL/handoff lines so the next
fix CR can match factory-side pool addresses to controller-side
fTxPool / fRxPool.

Pure DIAGNOSTIC_INSTRUMENTATION: same new/initWithName/queues/
addEventSource/cleanup/return semantics; no active pbufpool probes;
no retry/fallback/masking/forced state. +0 BootKC undefs.

CR-214 / Stage 1.
```

## PATCH ARTIFACT

exact patch artifact:
`commit-approval/artifacts/CR-214-end-to-end-init-false-branch-coverage.diff`

The artifact captures the cumulative staged/live diff at submission.
It is left **untracked** so it does not appear in `git diff --binary HEAD`
and matches that diff byte-for-byte under `cmp`.

The auditor may re-verify identity by:

```
git rev-parse HEAD
shasum -a 256 commit-approval/artifacts/CR-214-end-to-end-init-false-branch-coverage.diff
wc -l           commit-approval/artifacts/CR-214-end-to-end-init-false-branch-coverage.diff
git diff --binary HEAD | shasum -a 256
git diff --binary HEAD | wc -l
cmp -s <(git diff --binary HEAD) commit-approval/artifacts/CR-214-end-to-end-init-false-branch-coverage.diff && echo OK
git diff --check HEAD
```

The two `shasum -a 256` outputs (artifact file and live diff) MUST
be identical. `cmp` is the binding identity gate.

## SUPERSEDES

supersedes (formal):
- CR-213 (insufficient unredacted-pointer / sub-stage classification
  coverage for the `initWithName=0` hypothesis).

implicitly invalidates (per `approval_invalid_if_diff_changes: YES`):
- CR-213 Stage 1 approval. CR-213's branch-to-final-point coverage
  for the controller-level outcomes (TX_ONLY/RX_ONLY/TX_RX/BOTH_OK,
  DOWNSTREAM_QUEUE_FAIL/QUEUES_OK, DOWNSTREAM_WORKLOOP_FAIL/
  WORKLOOPS_OK) is preserved with format-string-only refinements
  (`%p` → `0x%llx`).
- CR-211 rejection remains final; its active pbufpool probes are
  not restored.
- CR-210 rejection remains final; its Generic differential is not
  restored.
- CR-209 / CR-208 source-level edits (factory probe layout,
  `poolFlags=1`) are preserved verbatim on the production path.
