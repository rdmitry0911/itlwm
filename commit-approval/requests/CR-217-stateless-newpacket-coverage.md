# CR-217 - Stateless newPacket end-to-end branch coverage + os_log redaction bypass (DIAGNOSTIC_INSTRUMENTATION)

- date: 2026-04-29
- stage: STAGE_1_STRUCTURAL
- justification class: DIAGNOSTIC_INSTRUMENTATION
- branch: master
- HEAD: `d3a07c2abccac863e1909aa562051a6ee5687245`
- requested by: Executor+Committer (per `docs/WORKFLOW_ITLWM.md`)
- supersedes: CR-216 (REJECTED on 2026-04-29 for diagnostic
  neutrality + improper evidence chain; see
  `commit-approval/decisions/COMMIT_DECISION_CR-216.md`).
- preserves: CR-213 Stage 1 approval (the only authorized Stage 2
  runtime evidence chain to date) as the reviewable premise.

does_this_fix_proven_current_root_cause: NO
This is diagnostic-only. Stateless: no new mutable state, no
counters, no atomic primitives. Same call ordering, same
arguments, same returns — only XYLog branch markers.

## REJECTION_REMEDIATION

CR-216 was REJECTED with three blockers (per `COMMIT_DECISION_CR-216.md`):

1. `runtime_evidence: FAIL` — CR-216 referenced CR-215 Stage 2
   runtime as its narrowing premise. CR-215 was never granted
   Stage 1 approval (it was superseded by CR-216 before review),
   so its runtime evidence cannot be treated as authorized
   Stage 2 evidence.
2. `diagnostic_neutrality: FAIL` —
   `static volatile SInt32 s_callSeq`, `static volatile SInt32
   s_okSeq`, and `OSIncrementAtomic` calls created new mutable
   diagnostic state and a new BootKC undefined symbol
   (`_OSIncrementAtomic` 884 → 885), exceeding behavior-neutral
   passive instrumentation.
3. `verification: PASS_WITH_NOTE` — CR-216's symbol-delta
   explanation incorrectly cited `__ZTV20IO80211NetworkPacket`
   when the actual `+1` symbol was `_OSIncrementAtomic`.

CR-217 remediates all three:

| rejection axis | remediation |
|---|---|
| `runtime_evidence` | Premise re-rooted on CR-213's approved Stage 2 evidence (filed under `commit-approval/runtime_evidence/CR-213-stage2-*` / `commit-approval/stage2_evidence/CR-213-stage2-evidence-20260429.md`). CR-217 does NOT reference CR-215 evidence as a gate. CR-217 produces both the `INIT_FALSE_<stage>` classifier output AND the `NEWPACKET_<branch>` markers in its own Stage 2 runtime, so the dependency is self-contained. |
| `diagnostic_neutrality` | Removed `static volatile SInt32 s_callSeq` and `s_okSeq`. Removed both `OSIncrementAtomic` calls. Removed `verbose` / `OK_TICK` conditional gating. Each of the four `newPacket` return paths emits exactly one stateless XYLog FINAL marker. No new mutable state, no atomic primitives. |
| symbol-delta explanation | Corrected build evidence file `commit-approval/build_evidence/CR-217-build-stateless-newpacket-coverage.txt` records `884` undefs unchanged from CR-215. The `+1` regression seen in CR-216 (`_OSIncrementAtomic`) is explicitly attributed and explicitly removed. |

## REQUIRED COVERAGE CRITERION

Same as CR-213/214/215/216 (per `feedback_diagnostic_end_to_end_criterion`):

```
end-to-end instrumentation to final points across all branches of
this error/hypothesis
```

CR-217 satisfies this in one boot for two layered hypotheses:

**Layer 1 (carried from CR-215)** — `IOSkywalkPacketBufferPool::initWithName`
returns false. Nine mutually-exclusive `INIT_FALSE_<stage>`
classifications based on which framework-internal slot the
framework completed last:

- `INIT_FALSE_PRE_THCALL`, `_PRE_SEGSTATS`, `_PRE_LOCK1`,
  `_PRE_LOCK2`, `_PRE_OWNER_CACHE`, `_KPBP_REJECT`,
  `_OSARRAY_FIRST`, `_OSARRAY_SECOND`, `_POST_OSARRAY`.

**Layer 2 (CR-217-specific)** — when Layer 1 fires
`INIT_FALSE_POST_OSARRAY`, the next-deeper hypothesis is "our
`AirportItlwmIO80211PacketPool::newPacket` override returns a
non-success IOReturn". Four mutually-exclusive
`NEWPACKET_<branch>` final markers:

- `NEWPACKET_BAD_ARGS` (`desc==NULL || outPacket==NULL` →
  `kIOReturnBadArgument`).
- `NEWPACKET_ALLOC_NULL`
  (`OSMetaClass::allocClassWithName("IO80211NetworkPacket")` →
  NULL → `kIOReturnNoMemory`).
- `NEWPACKET_INIT_FALSE` (`packet->initWithPool(this, desc, 0)`
  → `false` → `kIOReturnError`).
- `NEWPACKET_OK` (success → `kIOReturnSuccess`).

The framework's packet-inventory loop calls `newPacket` up to
`kbi_packets` (256) times per pool. With four stateless markers
and no per-call counters, each call emits exactly one FINAL
marker, bounding TX+RX log volume at ~512 lines. No call
ordering, scheduling, or pool-state side effect is introduced.

Plus retain CR-213's controller-level branch markers
(`TX_ONLY`/`RX_ONLY`/`TX_RX`/`BOTH_OK`,
`DOWNSTREAM_QUEUE_FAIL`/`QUEUES_OK`,
`DOWNSTREAM_WORKLOOP_FAIL`/`WORKLOOPS_OK handoff=STEP8d`).

## SYMPTOM

Same boot-time symptom: `pools: TX=0x0 RX=0x0`, controller stops.
Backed by **CR-213's** authorized Stage 2 evidence:

- `commit-approval/runtime_evidence/CR-213-stage2-boot-log-20260429.txt`
  — 35 lines covering both TX/RX factory invocations,
  `INIT_FALSE` × 2, `TX_RX failMask=0x3`, controller stop.
- `commit-approval/runtime_evidence/CR-213-stage2-loaded-kext-20260429.txt`
  — `kextstat` line confirming UUID
  `3AC188DA-A10C-3541-97FD-861969956A44` matches CR-213 Stage 1
  build identity.
- `commit-approval/stage2_evidence/CR-213-stage2-evidence-20260429.md`
  — structured Stage 2 evidence document.

CR-213's evidence shows the `INIT_FALSE` outcome but not the
sub-stage (CR-213's instrumentation predates the
`INIT_FALSE_<stage>` classifier). CR-217 provides both layers
in its own Stage 2 runtime.

## DIVERGENCE

- exact divergence point: still inside the framework
  `IOSkywalkPacketBufferPool::initWithName` flow (KDK
  `IOSkywalkFamily.kext` `0x9bf0`). Layer 2 narrowing depends
  on CR-217 Stage 2 runtime.
- confirmed deviation: `initWithName` returns false symmetrically
  for TX and RX (CR-213 evidence).
- confirmed root cause: TBD by CR-217 runtime.
- exact confirmed deviation removed: NONE.
- exact semantic mismatch removed: NONE.

## CLAIM SCOPE

- exact claim scope:
  1. keep production `packetType = kIOSkywalkPacketTypeNetwork`;
  2. keep production PoolOptions unchanged (`poolFlags=1`);
  3. add `static inline ptrHi32(p) / ptrLo32(p)` and
     `slotHi32(base,off) / slotLo32(base,off)` pure helpers
     (no state, no side effects);
  4. read every internal-state slot the framework's initWithName
     writes (chronological per KDK 0x9c5f..0x9f24): name(0x98),
     thCall(0xb0), segStats(0x78), lock1(0x80), lock2(0x88),
     owner(0x20), pbufpool(0x18), arr1(0x68), arr2(0x60),
     typeCache(0x3c), flagsCache(0x48), singleSeg(0xb8),
     disposed(0xba);
  5. classify INIT_FALSE into the named `INIT_FALSE_<stage>`
     branches via stateless predicate evaluation;
  6. emit `0x%x_%x` (split halves) for every pointer in the
     factory PACKETPOOL[…] lines and in the controller
     POOLTRACE[STEP8b] lines, so os_log default-private
     redaction does not blind the evidence;
  7. add four stateless `NEWPACKET_<branch>` final markers in
     the `newPacket` override: `BAD_ARGS`, `ALLOC_NULL`,
     `INIT_FALSE`, `OK`. Each marker is one XYLog call with no
     counters and no atomic primitives.
- non_claims:
  - This request does NOT claim a fix.
  - This request does NOT instrument private closed-source
    framework branches directly.
  - This request does NOT call `kern_pbufpool_create` /
    `kern_pbufpool_destroy` directly (CR-211 lesson honored).
  - This request does NOT change any system-facing argument,
    return value, state transition, ownership, ordering,
    callback, queue, or registration contract.
  - This request does NOT add retry, fallback, replay, polling,
    delay, forced state, or forced success.
  - This request does NOT add new mutable state, counters, or
    atomic primitives (CR-216 lesson honored).
  - This request does NOT add any new BootKC undefined symbol.

## JUSTIFICATION PATH

justification path: DIAGNOSTIC_INSTRUMENTATION

- exact hypotheses being disambiguated, layered:

  **Layer 1**: which framework-internal stage of `initWithName`
  failed last? Nine mutually-exclusive labels (PRE_THCALL …
  POST_OSARRAY).

  **Layer 2** (only fires under Layer 1 == POST_OSARRAY): which
  return path of our `newPacket` override fired? Four
  mutually-exclusive labels (BAD_ARGS, ALLOC_NULL, INIT_FALSE,
  OK).

  Both layers have stateless markers and ALL branches have a
  final-point log line. End-to-end criterion satisfied in one
  boot.

- exact probe points:
  - `AirportItlwm/AirportItlwmV2.cpp` —
    `AirportItlwmIO80211PacketPool::withName`: factory pre-init
    log + post-init slot enumeration + `INIT_FALSE_<stage>`
    classifier + `INIT_TRUE` / `NEW_NULL` markers.
  - `AirportItlwm/AirportItlwmV2.cpp` —
    `AirportItlwmIO80211PacketPool::newPacket`: four stateless
    `NEWPACKET_<branch>` final markers.
  - `AirportItlwm/AirportItlwmV2.cpp` —
    `AirportItlwm::start` STEP 8b POOLTRACE lines: pointer
    format-string substitutions (`0x%llx` → `0x%x_%x`).

- why instrumentation is behavior-neutral:
  - Same `OSMetaClass::allocClassWithName(...)` call with same
    argument. Same `packet->initWithPool(this, desc, 0)` call
    with same arguments. Same `packet->release()` on init
    failure. Same `*outPacket = packet` on success. Same
    return values.
  - No counters, no atomic primitives, no static mutable state.
    Helpers (`ptrHi32`, `ptrLo32`, `slotHi32`, `slotLo32`) are
    pure functions of their inputs; the slot reads are
    non-mutating loads from our own subclass instance.
  - No new BootKC undefined symbol vs CR-215 baseline (verified
    `884 = 884`, `_OSIncrementAtomic` not added).
  - No retry, fallback, masking, forced state, or workaround.

- exact runtime evidence expected from this instrumentation:

  Factory branch evidence per TX/RX:
  ```
  itlwm: PACKETPOOL[AirportItlwm-TX] new=0x<HI>_<LO> poolVtable=0x<HI>_<LO>
        (size=200 opts=0x<HI>_<LO> owner=0x<HI>_<LO> ownerVtable=0x<HI>_<LO>
         pktCount=256 bufCount=256 bufSize=2048 maxBPP=1 memSegSz=0
         poolFlags=0x1 type=1)
  itlwm: PACKETPOOL[AirportItlwm-TX] initWithName=0 (type=1) slots:
        name=0x<HI>_<LO> thCall=0x<HI>_<LO> segStats=0x<HI>_<LO>
        lock1=0x<HI>_<LO> lock2=0x<HI>_<LO> owner=0x<HI>_<LO>
        pbufpool=0x<HI>_<LO> arr1=0x<HI>_<LO> arr2=0x<HI>_<LO>
        typeCache=1 flagsCache=0x1 singleSeg=0 disposed=0
  itlwm: PACKETPOOL[AirportItlwm-TX] FINAL branch=INIT_FALSE_<STAGE>
        preRelease pool=0x<HI>_<LO> pbufpool=0x<HI>_<LO>
        owner=0x<HI>_<LO> arr1=0x<HI>_<LO> arr2=0x<HI>_<LO>
        disposed=0
  ```

  newPacket per-call evidence (one of four markers per call):
  ```
  itlwm: NEWPACKET FINAL branch=OK this=0x<HI>_<LO> packet=0x<HI>_<LO>
  ...
  itlwm: NEWPACKET FINAL branch=<BAD_ARGS|ALLOC_NULL|INIT_FALSE> ...
  ```

  Controller / STEP 8b POOLTRACE evidence: same lines as CR-213
  with all pointer arguments now rendered as `0x%x_%x` (verified
  unredacted in CR-215 evidence for `%x` of `uint32_t`-typed
  values).

## CHANGED FILES

CR-217-specific delta vs HEAD (atop the prior staged tree):
- `AirportItlwm/AirportItlwmV2.cpp` — factory + newPacket
  rewrite + STEP 8b pointer-format conversion.
- `commit-approval/build_evidence/CR-217-build-stateless-newpacket-coverage.txt`
  (NEW; correct symbol-delta).

The CR-217 cumulative artifact carries forward all previously
staged content (CR-201 / 203 / 204 / 206 / 208 / 209 / 213 /
214 / 215 / 216) plus CR-213 Stage 2 evidence files filed under
`runtime_evidence/` and `stage2_evidence/`.

## DIFF SUMMARY

```
AirportItlwmIO80211PacketPool::newPacket override:
  - REMOVED `static volatile SInt32 s_callSeq` (CR-216 counter).
  - REMOVED `static volatile SInt32 s_okSeq`   (CR-216 counter).
  - REMOVED both OSIncrementAtomic() calls.
  - REMOVED conditional `verbose` / OK_TICK gating.
  - Each of the 4 mutually-exclusive return paths now emits
    a single stateless XYLog FINAL marker:
      * BAD_ARGS  (this=, desc=, outPacket=, ret=)
      * ALLOC_NULL (this=, desc=, ret=)
      * INIT_FALSE (this=, desc=, packet=, objectVtable=, ret=)
      * OK         (this=, packet=)

AirportItlwmIO80211PacketPool::withName factory: unchanged from
  CR-216 (split-halves redaction bypass already in place).

AirportItlwm::start STEP 8b POOLTRACE: unchanged from CR-216
  (split-halves redaction bypass already in place).
```

## EVIDENCE FROM DECOMP

Carried verbatim from CR-215/216, plus the post-OSArray flow
showing `vt[50] = newPacket` is the framework's first call after
the second OSArray (KDK initWithName +0x373).

`IO80211NetworkPacket::MetaClass::C2Ev` (BootKC
`0xffffff80022cf5a2`) registers `IO80211NetworkPacket` with
size 0x78 (120 bytes) — class IS available in BootKC via
IO80211Family kext.

## EVIDENCE FROM RUNTIME

CR-217's narrowing premise is **CR-213's authorized Stage 2
evidence** (the only Stage 2 evidence in the worktree backed
by a Stage 1 `APPROVED_FOR_AFTER_FIX_RUNTIME` decision):

- `commit-approval/decisions/COMMIT_DECISION_CR-213.md` (Stage 1
  approved, `APPROVED_FOR_AFTER_FIX_RUNTIME`,
  `allow_after_fix_runtime: YES`).
- `commit-approval/runtime_evidence/CR-213-stage2-boot-log-20260429.txt`
  (the runtime authorized to be collected by Stage 1, captured
  on the exact reviewed kext sha
  `9723ec2d7ef333a8e8f4fa1df2080875d26935a508d7c3c3fffeba8a0b2eaaa1`,
  UUID `3AC188DA-A10C-3541-97FD-861969956A44`).
- `commit-approval/runtime_evidence/CR-213-stage2-loaded-kext-20260429.txt`.
- `commit-approval/stage2_evidence/CR-213-stage2-evidence-20260429.md`.

CR-217 deliberately does NOT reference CR-215's runtime evidence
(unauthorized per CR-216 rejection) or CR-216's own runtime
(rejected before any boot occurred). CR-217's Stage 2 runtime
will be collected per the standard Stage 1 → Stage 2 sequence
and will provide both the `INIT_FALSE_<stage>` classifier output
and the `NEWPACKET_<branch>` markers as a self-contained
evidence chain.

after-runtime evidence for CR-217 itself: PENDING — Stage 2 will
run after host boots the CR-217 kext (sha
`2a5237012a531fa19e79f12b31b9c3a25b63b8f161bba50bc023e77eb50d5724`,
UUID `B431DDC7-7A1A-3962-838D-9A17821D72C1`).

why this runtime evidence is semantically significant: stateless
Layer 1 + Layer 2 markers identify exactly one
`INIT_FALSE_<stage>` AND, when Layer 1 == POST_OSARRAY, exactly
one `NEWPACKET_<branch>` for the failing call. The next fix CR
is constrained to a single mechanism rather than the open-ended
"newPacket or anything else" axis.

why this is not trace-order / object-id noise: every probe is
deterministic per call/pool; no counters or scheduling
dependencies are introduced.

## CAUSALITY

- regression window: `8e05ddf` → `d3a07c2` (unchanged).
- pinpointed divergence path: TBD by CR-217 Stage 2.
- why this is root-cause-discovery rather than guesswork:
  CR-213's authorized evidence shows `INIT_FALSE` × 2 +
  `TX_RX failMask=0x3` symmetrically. CR-217 narrows in one
  boot to one of (9 + 4) mutually-exclusive labels,
  exhausting the local hypothesis space.

## VERIFICATION PERFORMED

- build:
  - `scripts/build_tahoe.sh`: `BUILD SUCCEEDED`
  - BootKC undef-symbol verification:
    `OK: all 884 undefined symbols resolve against BootKC`
  - kext path:
    `Build/Debug/Tahoe/AirportItlwm.kext/Contents/MacOS/AirportItlwm`
  - kext sha256:
    `2a5237012a531fa19e79f12b31b9c3a25b63b8f161bba50bc023e77eb50d5724`
  - kext UUID:
    `B431DDC7-7A1A-3962-838D-9A17821D72C1`
  - kext size: `16294192`
  - Build evidence file:
    `commit-approval/build_evidence/CR-217-build-stateless-newpacket-coverage.txt`
  - kext installed in `/Library/Extensions/AirportItlwm.kext` (root:wheel).
- static checks:
  - `git diff --check HEAD`: PASS.
  - No direct active pbufpool probe.
  - No `kIOSkywalkPacketTypeGeneric` differential path.
  - No retry / fallback / masking / forced state.
  - No new mutable state / counters / atomic primitives:
    verified by `nm -u` showing `_OSIncrementAtomic` is NOT in
    the undefined-symbol set; total undef = 884 (matches
    CR-215 baseline; +0 vs CR-215).
- before reproduction result: as documented.
- after reproduction result: PENDING.

## STAGE RULES

- request_stage = STAGE_1_STRUCTURAL.
- after evidence: PENDING_HOST_REBOOT.
- after reproduction result: PENDING.
- request goal: reviewer structural approval to proceed with
  Stage 2 after-fix runtime collection on the exact reviewed
  HEAD and exact reviewed CR-217 cumulative artifact.
- commit is NOT requested at Stage 1.

## RESIDUAL UNCERTAINTY

- If Stage 2 fires `INIT_FALSE_POST_OSARRAY` AND
  `NEWPACKET_INIT_FALSE` (most likely), the next fix CR will
  investigate what `IOSkywalkPacket::initWithPool` requires of
  the pool argument that our subclass does not satisfy.
- If Stage 2 fires `NEWPACKET_ALLOC_NULL`, the next fix CR will
  investigate IO80211NetworkPacket metaclass registration timing
  or kalloc_type sandbox state.
- If Stage 2 fires any other Layer-1 stage (not POST_OSARRAY),
  the failure is earlier in initWithName and `newPacket` is
  not even called — the Layer-2 markers will be silent.

## FORBIDDEN ALTERNATIVES CONSIDERED AND REJECTED

- active direct pbufpool probes: REJECTED (CR-211 lesson).
- modifying any `docs/*PROTOCOL*.md` or `*TEMPLATE*.md`: REJECTED
  (`feedback_no_modify_protocols`).
- modifying any prior CR's request/decision file: REJECTED
  (`feedback_no_delete_submitted_requests`).
- new mutable state / counters / atomic primitives: REJECTED
  (CR-216 lesson; this is the precise neutrality boundary).
- referencing unauthorized prior runtime as Stage 2 premise:
  REJECTED (CR-216 lesson; CR-217 only references CR-213's
  Stage-1-approved evidence chain).
- minimal-diff one-signal instrumentation: REJECTED
  (`feedback_diagnostic_end_to_end_criterion`; CR-217 covers
  every branch of both layered hypotheses end-to-end).
- heuristic timing / fallback / masking / forced state / retry /
  alternate packet type A/B: not added.
- why rejected: every alternative would either perturb kernel
  resource state, break audit-trail invariants, exceed the
  passive diagnostic scope, or under-deliver the end-to-end
  branch coverage criterion.

## PROPOSED COMMIT MESSAGE

```
AirportItlwm: stateless newPacket coverage + redaction bypass

Resubmit CR-216 with diagnostic-neutrality blockers fixed: drop
the static counters and OSIncrementAtomic primitives that the
auditor rejected as new mutable diagnostic state. The four
mutually-exclusive return paths of AirportItlwmIO80211PacketPool::
newPacket now emit exactly one stateless XYLog FINAL marker each
(BAD_ARGS / ALLOC_NULL / INIT_FALSE / OK). No new BootKC
undefined symbols (884 = 884 vs CR-215).

Premise re-rooted on CR-213's authorized Stage 2 evidence
chain (the only Stage 2 evidence backed by an APPROVED_FOR_AFTER_FIX_RUNTIME
Stage 1 decision). CR-217 produces both the INIT_FALSE_<stage>
classifier output and the NEWPACKET_<branch> markers in its own
runtime, satisfying the end-to-end criterion in one boot.

Pure DIAGNOSTIC_INSTRUMENTATION: same
allocClassWithName/initWithPool/release/return semantics; no
counters, no atomics, no fallback. Split-halves pointer
rendering (0x%x_%x) carried over from CR-216 to bypass os_log
privacy redaction.

CR-217 / Stage 1.
```

## PATCH ARTIFACT

exact patch artifact:
`commit-approval/artifacts/CR-217-stateless-newpacket-coverage.diff`

The artifact captures the cumulative staged/live diff at submission.
It is left **untracked** so it does not appear in `git diff --binary HEAD`
and matches that diff byte-for-byte under `cmp`.

The auditor may re-verify identity by:

```
git rev-parse HEAD
shasum -a 256 commit-approval/artifacts/CR-217-stateless-newpacket-coverage.diff
wc -l           commit-approval/artifacts/CR-217-stateless-newpacket-coverage.diff
git diff --binary HEAD | shasum -a 256
git diff --binary HEAD | wc -l
cmp -s <(git diff --binary HEAD) commit-approval/artifacts/CR-217-stateless-newpacket-coverage.diff && echo OK
git diff --check HEAD
```

The two `shasum -a 256` outputs (artifact file and live diff) MUST
be identical. `cmp` is the binding identity gate.

## SUPERSEDES

supersedes (formal):
- CR-216 (REJECTED for diagnostic-neutrality + improper evidence
  chain). CR-216's source-side counters and atomic primitives are
  REMOVED. Split-halves pointer-format and Layer-1 classifier are
  preserved verbatim.

implicitly invalidates: none beyond CR-216; the prior chain
(CR-211 rejection, CR-210 rejection, CR-213 supersession,
CR-214 rejection, CR-215 supersession) is preserved.

## EVIDENCE_FILE_INVENTORY

- `commit-approval/runtime_evidence/CR-213-stage2-boot-log-20260429.txt`
  (preserved; the only authorized Stage 2 boot log).
- `commit-approval/runtime_evidence/CR-213-stage2-loaded-kext-20260429.txt`
  (preserved; CR-213 kextstat identity).
- `commit-approval/stage2_evidence/CR-213-stage2-evidence-20260429.md`
  (preserved; CR-213 structured Stage 2 evidence).
- `commit-approval/build_evidence/CR-217-build-stateless-newpacket-coverage.txt`
  (NEW; CR-217 build evidence with corrected symbol-delta).
- prior CR documents (CR-201..CR-216) preserved.
