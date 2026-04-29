# CR-216 - newPacket end-to-end branch coverage + os_log redaction bypass (DIAGNOSTIC_INSTRUMENTATION)

- date: 2026-04-29
- stage: STAGE_1_STRUCTURAL
- justification class: DIAGNOSTIC_INSTRUMENTATION
- branch: master
- HEAD: `d3a07c2abccac863e1909aa562051a6ee5687245`
- requested by: Executor+Committer (per `docs/WORKFLOW_ITLWM.md`)
- supersedes: CR-215 (the resubmission of CR-214). CR-215 Stage 2
  evidence pinpointed the failure to `INIT_FALSE_POST_OSARRAY`
  (framework's `IOSkywalkPacketBufferPool::initWithName` reaches
  the post-OSArray packet-inventory loop and aborts) but every
  pointer output was still `<private>`-redacted by os_log despite
  CR-215's `(uintptr_t)` cast. CR-216 satisfies the end-to-end
  coverage criterion by:
  1. enumerating the four mutually-exclusive return paths of our
     `AirportItlwmIO80211PacketPool::newPacket` override and emitting
     a `NEWPACKET_<branch>` final-point marker per path;
  2. replacing every `0x%llx` pointer output (factory + STEP 8b
     trace) with split-halves `0x%x_%x` rendered through
     `uint32_t`-typed memory reads, which CR-215 evidence proves
     bypass os_log privacy redaction.

does_this_fix_proven_current_root_cause: NO
This is diagnostic-only. No fix to pool creation. CR-216 narrows
the next axis (newPacket failure path) under the end-to-end
criterion.

## REQUIRED COVERAGE CRITERION

Same as CR-213/214/215 (per `feedback_diagnostic_end_to_end_criterion`):

```
end-to-end instrumentation to final points across all branches of
this error/hypothesis
```

For the next-deeper hypothesis "framework's POST_OSARRAY loop calls
our `newPacket` override which returns non-zero IOReturn", CR-216
emits a `NEWPACKET_<branch>` final-point marker for each of the
four mutually-exclusive return paths:

- `NEWPACKET_BAD_ARGS` — `desc == NULL || outPacket == NULL` →
  return `kIOReturnBadArgument` (= `0xe00002c2`).
- `NEWPACKET_ALLOC_NULL` —
  `OSMetaClass::allocClassWithName("IO80211NetworkPacket")`
  returned NULL → return `kIOReturnNoMemory` (= `0xe00002bd`).
- `NEWPACKET_INIT_FALSE` — `packet->initWithPool(this, desc, 0)`
  returned `false` → release packet, return `kIOReturnError`.
- `NEWPACKET_OK` — success path, `*outPacket = packet`, return
  `kIOReturnSuccess`.

Plus retain CR-215's full `INIT_FALSE_<stage>` classification of the
framework-internal initWithName flow (carried verbatim, only
pointer-format change applied).

Plus retain CR-213's controller-level branch markers
(`TX_ONLY`/`RX_ONLY`/`TX_RX`/`BOTH_OK`,
`DOWNSTREAM_QUEUE_FAIL`/`QUEUES_OK`,
`DOWNSTREAM_WORKLOOP_FAIL`/`WORKLOOPS_OK handoff=STEP8d`).

## SYMPTOM

Same boot-time symptom: `pools: TX=0x0 RX=0x0`, controller stops.
Now backed by both filed Stage 2 evidence files:

- CR-213 evidence
  (`commit-approval/runtime_evidence/CR-213-stage2-boot-log-20260429.txt`):
  `INIT_FALSE` × 2 + `TX_RX failMask=0x3`.
- CR-215 evidence
  (`commit-approval/runtime_evidence/CR-215-stage2-boot-log-20260429.txt`,
  `commit-approval/stage2_evidence/CR-215-stage2-evidence-20260429.md`):
  `INIT_FALSE_POST_OSARRAY` × 2 + `typeCache=1 flagsCache=0x1
  singleSeg=0 disposed=0`. All 9 chronologically-ordered slots
  (name, thCall, segStats, lock1, lock2, owner, pbufpool, arr1,
  arr2) read as non-zero (otherwise the classifier would have
  fired an earlier-stage label), confirming the framework
  completes every internal stage up to and including the second
  OSArray allocation. Failure happens AFTER both OSArrays — in
  the packet-inventory loop that calls our `newPacket` override.

## DIVERGENCE

- exact divergence point: inside our subclass's `newPacket`
  override at `AirportItlwm/AirportItlwmV2.cpp:265+`, called by
  the framework's initWithName at KDK
  `IOSkywalkPacketBufferPool::initWithName + 0x373` (vt slot 50)
  in the post-OSArray packet-inventory loop.
- confirmed deviation: framework's loop receives a non-success
  return from our override. The exact branch of our override is
  not yet observable.
- confirmed root cause: TO_BE_DETERMINED by CR-216 runtime.
- exact confirmed deviation removed: NONE.
- exact semantic mismatch removed: NONE.

## CLAIM SCOPE

- exact claim scope:
  1. keep production `packetType = kIOSkywalkPacketTypeNetwork`;
  2. keep production PoolOptions unchanged (`poolFlags=1`);
  3. add `static inline ptrHi32(p) / ptrLo32(p)` helpers and
     `slotHi32(base,off) / slotLo32(base,off)` helpers to read
     pointer-derived 64-bit values as paired `uint32_t` halves;
  4. replace every `0x%llx` pointer output in the factory and
     STEP 8b POOLTRACE lines with `0x%x_%x` split halves rendered
     through these helpers;
  5. add end-to-end branch coverage in `newPacket` override:
     - `NEWPACKET_BAD_ARGS` final-point marker;
     - `NEWPACKET_ALLOC_NULL` final-point marker;
     - `NEWPACKET_INIT_FALSE` final-point marker;
     - `NEWPACKET_OK` final-point marker;
  6. preserve the existing `OSSafeReleaseNULL` / packet release /
     return semantics of all four paths.
- non_claims:
  - This request does NOT claim a fix.
  - This request does NOT change the kIOReturnBadArgument /
    kIOReturnNoMemory / kIOReturnError / kIOReturnSuccess return
    values, the `desc==NULL || outPacket==NULL` validation, the
    `OSMetaClass::allocClassWithName("IO80211NetworkPacket")`
    call, the `packet->initWithPool(this, desc, 0)` call, or
    `*outPacket = packet`.
  - This request does NOT call `kern_pbufpool_create` /
    `kern_pbufpool_destroy` directly (CR-211 lesson honored;
    CR-216 stays strictly passive).
  - This request does NOT add retry, fallback, replay, polling,
    delay, forced state, or forced success.

## JUSTIFICATION PATH

justification path: DIAGNOSTIC_INSTRUMENTATION

- exact hypotheses being disambiguated, all in one boot:
  1. `desc==NULL || outPacket==NULL` → `NEWPACKET_BAD_ARGS`.
     Extremely unlikely (framework guarantees both non-NULL),
     but covered for completeness.
  2. `OSMetaClass::allocClassWithName("IO80211NetworkPacket")`
     returned NULL → `NEWPACKET_ALLOC_NULL`. Possible if
     IO80211NetworkPacket's metaclass isn't registered or its
     kalloc_type sandbox rejects allocation.
  3. `packet->initWithPool(this, desc, 0)` returned false →
     `NEWPACKET_INIT_FALSE`. The most likely root cause: our pool
     subclass may not satisfy whatever IO80211NetworkPacket
     expects of its pool argument (e.g., pool's metaclass type,
     mPbufPool kern handle properties, packet-info buffer tag
     wiring).
  4. Success → `NEWPACKET_OK`. Counter `okSeq` records how many
     successful calls preceded the eventual failure.

- exact probe points:
  - `AirportItlwm/AirportItlwmV2.cpp` —
    `AirportItlwmIO80211PacketPool::newPacket` override (vt slot
    50): four `XYLog` markers, one per return path. Verbose
    state on first 4 calls per pool; OK_TICK every 64 calls and
    on calls 254/255 to bound the 256-packet inventory loop
    without log floods.
  - `AirportItlwm/AirportItlwmV2.cpp` —
    `AirportItlwmIO80211PacketPool::withName` factory and
    `AirportItlwm::start` STEP 8b POOLTRACE lines: pointer-only
    format-string substitutions (`0x%llx` → `0x%x_%x`).

- why these probe points are sufficient: the failure boundary is
  exactly the function we instrument, and the probe matrix covers
  the entire return-path enumeration of that function.

- why instrumentation is behavior-neutral:
  - Same `OSMetaClass::allocClassWithName(...)` call with same
    argument runs in the same place. Same
    `packet->initWithPool(this, desc, 0)` call with same
    arguments. Same `packet->release()` on init failure. Same
    `*outPacket = packet` on success. Same return values
    (`kIOReturnBadArgument` / `kIOReturnNoMemory` /
    `kIOReturnError` / `kIOReturnSuccess`).
  - Static `s_callSeq` / `s_okSeq` counters use lock-free
    `OSIncrementAtomic` to avoid altering call ordering. The
    counters are read-only diagnostics.
  - Pointer rendering (`0x%llx` → `0x%x_%x`) only changes the log
    output format, not any value passed to a system call.
  - No retry, fallback, masking, forced state, or workaround.

- exact runtime evidence expected from this instrumentation:

  Factory branch evidence per TX/RX:
  ```
  itlwm: PACKETPOOL[AirportItlwm-TX] new=0x<HI>_<LO>
        poolVtable=0x<HI>_<LO> ...
  itlwm: PACKETPOOL[AirportItlwm-TX] initWithName=0 (type=1) slots:
        name=0x<HI>_<LO> thCall=0x<HI>_<LO> segStats=0x<HI>_<LO>
        lock1=0x<HI>_<LO> lock2=0x<HI>_<LO> owner=0x<HI>_<LO>
        pbufpool=0x<HI>_<LO> arr1=0x<HI>_<LO> arr2=0x<HI>_<LO>
        typeCache=1 flagsCache=0x1 singleSeg=0 disposed=0
  itlwm: PACKETPOOL[AirportItlwm-TX] FINAL branch=INIT_FALSE_POST_OSARRAY
        preRelease pool=0x<HI>_<LO> pbufpool=0x<HI>_<LO>
        owner=0x<HI>_<LO> arr1=0x<HI>_<LO> arr2=0x<HI>_<LO>
        disposed=0
  ```

  newPacket per-call evidence (first 4 calls verbose, then summary):
  ```
  itlwm: NEWPACKET[0] AFTER_ALLOC this=0x<HI>_<LO>
        desc=0x<HI>_<LO> object=0x<HI>_<LO>
        objectVtable=0x<HI>_<LO>
  itlwm: NEWPACKET[0] FINAL branch=OK this=0x<HI>_<LO>
        packet=0x<HI>_<LO> okSeq=1
  ...
  itlwm: NEWPACKET[N] FINAL branch=<INIT_FALSE|ALLOC_NULL|BAD_ARGS> ...
  ```

  After Stage 2 reboot, the log identifies exactly one
  `NEWPACKET_<branch>` for the failing call plus a count of
  successful preceding calls. The next CR's fix is then
  constrained to whichever sub-path fired.

## CHANGED FILES

CR-216-specific delta vs HEAD (atop the CR-215 staged tree):
- `AirportItlwm/AirportItlwmV2.cpp` — factory and newPacket
  rewrite, plus STEP 8b pointer-format conversion.
- `commit-approval/build_evidence/CR-216-build-newpacket-coverage.txt`
  (NEW).
- `commit-approval/runtime_evidence/CR-215-stage2-boot-log-20260429.txt`
  (NEW; CR-215 Stage 2 raw log).
- `commit-approval/runtime_evidence/CR-215-stage2-loaded-kext-20260429.txt`
  (NEW; CR-215 loaded-kext identity).
- `commit-approval/stage2_evidence/CR-215-stage2-evidence-20260429.md`
  (NEW; CR-215 structured Stage 2 evidence).

The CR-216 cumulative artifact carries forward all previously
staged content (CR-201 / 203 / 204 / 206 / 208 / 209 / 213 /
214 / 215).

## DIFF SUMMARY

```
AirportItlwmIO80211PacketPool::withName factory:
  - replace `unsigned long long ...LL = (unsigned long long)(uintptr_t)...`
    plus `0x%llx` outputs with `ptrHi32 / ptrLo32 / slotHi32 /
    slotLo32` helpers and `0x%x_%x` outputs.
  - same OSSafeReleaseNULL, same return semantics.

AirportItlwmIO80211PacketPool::newPacket override:
  - add OSIncrementAtomic-driven s_callSeq / s_okSeq counters.
  - add NEWPACKET_BAD_ARGS / _ALLOC_NULL / _INIT_FALSE / _OK
    branch markers, each with split-halves pointer rendering.
  - first 4 calls per pool log verbose state; later successful
    calls emit OK_TICK every 64 calls and on 254/255.

AirportItlwm::start STEP 8b POOLTRACE lines:
  - replace `0x%llx` outputs with `0x%x_%x` via helpers.
  - same call ordering, same arguments, same branch labels.
```

## EVIDENCE FROM DECOMP

Full chain carried from CR-213/214/215, plus the new post-OSArray
disasm window:

- `IOSkywalkPacketBufferPool::initWithName` (KDK
  `IOSkywalkFamily.kext` `0x9bf0`): every internal stage write
  documented (slots `0x98`, `0xb0`, `0x78`, `0x80`, `0x88`, `0x20`,
  `0x18`, `0x68`, `0x60`, `0x3c`, `0x48`, `0xb8`, `0xba`).
- Post-OSArray flow (`+0x300..+0x800` of initWithName, this CR):
  - `+0x32b`: second `OSArray::withCapacity` (KDK 0x9f24).
  - `+0x373`: `call qword [rax+0x180]` = vt slot 50 = our
    `AirportItlwmIO80211PacketPool::newPacket` override.
  - `+0x396`: `call qword [rax+0x1b8]` = vt slot 57 (subclass
    extension slot; we don't own one, so this falls through to
    base-class behavior or to the OSMetaClass reserved slot).
  - `+0x3d6`: `_IOMallocTypeVarImpl` for variable-size kalloc.
  - `+0x426`: `call qword [rax+0x190]` = vt slot 52 =
    `newMemorySegment` (we do NOT override; falls through to
    base `IOSkywalkPacketBufferPool::newMemorySegment`).
  - `+0x650`: `_IORecursiveLockLock`.
  - `+0x676`: `IOSkywalkPacketBufferPool::disposeAllPackets`.
  - `+0x687`: `_kern_pbufpool_destroy` — cleanup path on failure.
- `IO80211NetworkPacket::MetaClass::C2Ev` (BootKC
  `0xffffff80022cf5a2`): `mov ecx, 0x78` registers
  `IO80211NetworkPacket` with size 0x78 (120 bytes). The class
  IS registered in BootKC (via IO80211Family kext).
- `IO80211NetworkPacket::initWithPool` is inherited from
  `IOSkywalkPacket::initWithPool` (signature
  `bool initWithPool(IOSkywalkPacketBufferPool *, IOSkywalkPacketDescriptor *, IOOptionBits)`).

## EVIDENCE FROM RUNTIME

- panic logs: none.
- driver / kext logs (CR-215 Stage 2 boot 2026-04-29 12:32, kext
  sha `fb86f74f…` UUID `B2A2AA8A-…`, see
  `commit-approval/runtime_evidence/CR-215-stage2-boot-log-20260429.txt`):
  `INIT_FALSE_POST_OSARRAY` for both TX and RX, controller emits
  `TX_RX failMask=0x3`. All pointer values redacted to `<private>`
  but numeric fields visible (`typeCache=1 flagsCache=0x1
  singleSeg=0 disposed=0`).
- before evidence: as above.
- after evidence: PENDING — Stage 2 will run after host boots
  the CR-216 kext (sha
  `9ce9feb7e479d2d929a315cb5b090804ecfd28a5b55ac2a74d8fd63a29413f32`,
  UUID `07A5527B-65EA-3659-9DC7-BA1CCD688381`).
- why this runtime evidence is semantically significant:
  unredacted slot values + `NEWPACKET_<branch>` final marker
  pinpoint the exact return path of our newPacket override.
- why this is not trace-order / object-id noise: probe markers
  are deterministic per-call; counters use atomic increments;
  TX and RX reproduce symmetrically per CR-215 evidence.

## CAUSALITY

- regression window: `8e05ddf` → `d3a07c2` (unchanged).
- pinpointed divergence path: TBD by CR-216 Stage 2.
- why this is root-cause-discovery: CR-215 narrowed the failure
  axis to `INIT_FALSE_POST_OSARRAY`. CR-216 narrows the next
  axis to one of four mutually-exclusive `NEWPACKET_<branch>`
  outcomes. After CR-216 evidence, the next CR is the actual
  fix on a single sub-axis.

## VERIFICATION PERFORMED

- build:
  - `scripts/build_tahoe.sh`: `BUILD SUCCEEDED`
  - BootKC undef-symbol verification:
    `OK: all 885 undefined symbols resolve against BootKC`
  - kext path:
    `Build/Debug/Tahoe/AirportItlwm.kext/Contents/MacOS/AirportItlwm`
  - kext sha256:
    `9ce9feb7e479d2d929a315cb5b090804ecfd28a5b55ac2a74d8fd63a29413f32`
  - kext UUID:
    `07A5527B-65EA-3659-9DC7-BA1CCD688381`
  - kext size: `16298648`
  - Build evidence file:
    `commit-approval/build_evidence/CR-216-build-newpacket-coverage.txt`
  - kext installed in `/Library/Extensions/AirportItlwm.kext` (root:wheel).
- static checks:
  - `git diff --check HEAD`: PASS.
  - No direct active pbufpool probe.
  - No `kIOSkywalkPacketTypeGeneric` differential path.
  - No retry / fallback / masking / forced state.
- before reproduction result: as documented.
- after reproduction result: PENDING.

## STAGE RULES

- request_stage = STAGE_1_STRUCTURAL.
- after evidence: PENDING_HOST_REBOOT.
- after reproduction result: PENDING.
- request goal: reviewer structural approval to proceed with Stage 2
  after-fix runtime collection on the exact reviewed HEAD and exact
  reviewed CR-216 cumulative artifact.
- commit is NOT requested at Stage 1.

## RESIDUAL UNCERTAINTY

- If `NEWPACKET_INIT_FALSE` fires (most likely), the fix CR will
  need to identify what `IOSkywalkPacket::initWithPool` requires
  of the pool argument that our subclass does not satisfy.
  Candidates:
  - pool's mPbufPool (this[0x18]) handle properties expected by
    the IO80211NetworkPacket buflet-tracking machinery;
  - pool's metaclass typeinfo (a kalloc_type-wrapped check);
  - pool's vtable entry for some other slot the framework
    resolves at packet-init time.
- If `NEWPACKET_ALLOC_NULL` fires, the fix CR will investigate
  IO80211NetworkPacket metaclass registration timing or
  kalloc_type sandbox state.
- If `NEWPACKET_BAD_ARGS` fires, the framework is passing NULL
  unexpectedly — would be its own bug and is extremely unlikely.

## FORBIDDEN ALTERNATIVES CONSIDERED AND REJECTED

- active direct pbufpool probes: REJECTED (CR-211 lesson).
- modifying any `docs/*PROTOCOL*.md` or `*TEMPLATE*.md`: REJECTED
  (per `feedback_no_modify_protocols`).
- modifying any prior CR's request/decision file: REJECTED
  (per `feedback_no_delete_submitted_requests`).
- heuristic timing / fallback / masking / forced state / retry /
  alternate packet type A/B: not added.
- minimal-diff one-signal instrumentation: REJECTED
  (per `feedback_diagnostic_end_to_end_criterion`). CR-216 covers
  every branch of the `newPacket` return-path hypothesis to its
  final point in one build.
- why rejected: every alternative would either perturb kernel
  resource state, break audit-trail invariants, or under-deliver
  the end-to-end branch coverage criterion.

## PROPOSED COMMIT MESSAGE

```
AirportItlwm: end-to-end newPacket coverage + os_log redaction bypass

CR-215 Stage 2 evidence (2026-04-29 12:32) pinpointed the regression
to INIT_FALSE_POST_OSARRAY: framework's IOSkywalkPacketBufferPool::
initWithName completes every internal stage up to and including the
second OSArray, then aborts in the post-OSArray packet-inventory loop
that calls our subclass's newPacket override (vt slot 50). All
pointer values were <private>-redacted by os_log despite (uintptr_t)
cast.

CR-216 satisfies the end-to-end criterion for the next-deeper
hypothesis: every return path of AirportItlwmIO80211PacketPool::
newPacket emits a NEWPACKET_<branch> final marker (BAD_ARGS /
ALLOC_NULL / INIT_FALSE / OK), and every pointer-derived 64-bit
value in the factory + newPacket + STEP 8b traces is rendered as
two uint32_t halves (0x%x_%x) which the os_log infrastructure
leaves unredacted.

Pure DIAGNOSTIC_INSTRUMENTATION: same allocClassWithName call,
same initWithPool call, same packet release on failure, same
*outPacket = packet on success, same return values. No retry,
fallback, masking, or forced state. Static OSIncrementAtomic
counters bound the 256-packet log volume to ~12 lines per pool.

CR-216 / Stage 1.
```

## PATCH ARTIFACT

exact patch artifact:
`commit-approval/artifacts/CR-216-newpacket-coverage-redaction-bypass.diff`

The artifact captures the cumulative staged/live diff at submission.
It is left **untracked** so it does not appear in `git diff --binary HEAD`
and matches that diff byte-for-byte under `cmp`.

The auditor may re-verify identity by:

```
git rev-parse HEAD
shasum -a 256 commit-approval/artifacts/CR-216-newpacket-coverage-redaction-bypass.diff
wc -l           commit-approval/artifacts/CR-216-newpacket-coverage-redaction-bypass.diff
git diff --binary HEAD | shasum -a 256
git diff --binary HEAD | wc -l
cmp -s <(git diff --binary HEAD) commit-approval/artifacts/CR-216-newpacket-coverage-redaction-bypass.diff && echo OK
git diff --check HEAD
```

The two `shasum -a 256` outputs (artifact file and live diff) MUST
be identical. `cmp` is the binding identity gate.

## SUPERSEDES

supersedes (formal):
- CR-215 (resubmission of CR-214). CR-215's source-side changes are
  preserved as the `INIT_FALSE_<stage>` classification block; only
  the format-string render is updated to bypass redaction. The
  CR-215 Stage 2 evidence files filed under
  `commit-approval/runtime_evidence/` and
  `commit-approval/stage2_evidence/` remain in place as the
  premise.

implicitly invalidates:
- none beyond CR-215; the prior chain (CR-211 rejection, CR-210
  rejection, CR-213 supersession, CR-214 rejection) is preserved.

## EVIDENCE_FILE_INVENTORY

- `commit-approval/runtime_evidence/CR-213-stage2-boot-log-20260429.txt`
  (CR-215 filed; preserved).
- `commit-approval/runtime_evidence/CR-213-stage2-loaded-kext-20260429.txt`
  (CR-215 filed; preserved).
- `commit-approval/stage2_evidence/CR-213-stage2-evidence-20260429.md`
  (CR-215 filed; preserved).
- `commit-approval/runtime_evidence/CR-215-stage2-boot-log-20260429.txt`
  (NEW; 35 raw lines from CR-215 boot 2026-04-29 12:32).
- `commit-approval/runtime_evidence/CR-215-stage2-loaded-kext-20260429.txt`
  (NEW; CR-215 kextstat identity).
- `commit-approval/stage2_evidence/CR-215-stage2-evidence-20260429.md`
  (NEW; CR-215 structured Stage 2 evidence document).
- `commit-approval/build_evidence/CR-216-build-newpacket-coverage.txt`
  (NEW; CR-216 build evidence).
- prior CR documents (CR-201..CR-215) preserved.
