# CR-212 - Passive pool object-state diagnostic (DIAGNOSTIC_INSTRUMENTATION)

- date: 2026-04-29
- stage: STAGE_1_STRUCTURAL
- justification class: DIAGNOSTIC_INSTRUMENTATION
- branch: master
- HEAD: `d3a07c2abccac863e1909aa562051a6ee5687245`
- requested by: Executor+Committer (per `docs/WORKFLOW_ITLWM.md`)
- supersedes: CR-211 (rejected). CR-211's active
  `kern_pbufpool_create` / `kern_pbufpool_destroy` probe matrix is
  removed. CR-209 factory-side logging is retained and extended only
  with passive object-state reads. CR-208's production
  `poolOpts.poolFlags = 1` and Network packet type remain unchanged.

does_this_fix_proven_current_root_cause: NO
This is diagnostic-only. It does not attempt another A/B configuration
or create synthetic kernel resources. It observes the object/input state
around the existing production `AirportItlwmIO80211PacketPool::withName`
path so the next runtime can distinguish whether the framework failure
is before or after publication of the internal pool handles.

## SYMPTOM

Same Stage 2 boot symptom as CR-208 / CR-209: STEP 8b pool creation
fails. CR-209 showed `new AirportItlwmIO80211PacketPool` succeeds and
the existing `pool->initWithName(...)` returns false for the production
TX/RX pools. CR-211 tried to expand that diagnosis with direct pbufpool
probes, but was rejected because those calls are system-facing side
effects under `DIAGNOSTIC_INSTRUMENTATION`.

## DIVERGENCE

- exact divergence point: still the existing
  `IOSkywalkPacketBufferPool::initWithName` call reached through
  `AirportItlwmIO80211PacketPool::withName`.
- confirmed deviation: `initWithName` returns false after `new`
  succeeds.
- confirmed root cause: TO_BE_DETERMINED by passive runtime evidence.
- exact confirmed deviation removed: NONE in this CR.
- exact semantic mismatch removed: NONE in this CR.

## CLAIM SCOPE

- exact claim scope: remove CR-211's active STEP 8b pbufpool probe
  matrix and add passive `XYLog` fields to the existing pool factory:
  1. after `new`, log the allocated pool pointer and its first object
     word (`poolVtable`);
  2. after `new`, log the owner pointer and its first object word
     (`ownerVtable`);
  3. after the existing `initWithName(name, owner, packetType, options)`
     call returns, log the pool object words at offsets `0x18` and
     `0x20`;
  4. leave all existing CR-209 fail logs, release behavior, return
     value, and call ordering unchanged.
- non_claims:
  - This request does NOT claim a fix.
  - This request does NOT call `kern_pbufpool_create` or
    `kern_pbufpool_destroy` directly.
  - This request does NOT change `packetType`, `PoolOptions`, owner,
    name, queue attachment, start ordering, class layout, vtable, or
    `newPacket` behavior.
  - This request does NOT add retry, fallback, replay, polling, delay,
    forced state, or forced success.

## JUSTIFICATION PATH

justification path: DIAGNOSTIC_INSTRUMENTATION

- exact hypotheses being disambiguated:
  1. `initWithName` returns false before publishing the internal
     pool handles into the object. Expected evidence:
     `slot18=0x0 slot20=0x0` after `initWithName=0`.
  2. `initWithName` creates or partially publishes internal state and
     then fails a later consistency / wrapper-side condition. Expected
     evidence: at least one of `slot18` / `slot20` is non-NULL after
     `initWithName=0`.
  3. The local subclass object allocation or owner binding is malformed
     before the framework call. Expected evidence: unexpected
     `poolVtable` / `ownerVtable` values or a NULL owner/object word
     while the pointer itself is non-NULL.

- exact probe points:
  - `AirportItlwm/AirportItlwmV2.cpp` -
    `AirportItlwmIO80211PacketPool::withName`, immediately after the
    existing `new` and immediately after the existing
    `pool->initWithName(...)` call.

- why these probe points are sufficient for this diagnostic step:
  the failing control flow is already known to pass through these two
  boundaries. The new reads do not try to replace pbufpool internals;
  they only record whether the existing framework call left observable
  object state behind before returning false. That is enough to choose
  the next structural fix path without adding active kernel resource
  probes.

- why instrumentation is behavior-neutral:
  - The only new operations are reads from objects already present on
    the existing path and `XYLog` output.
  - The same `new` call runs.
  - The same `initWithName(name, owner, packetType, options)` call runs
    with identical arguments.
  - The same `OSSafeReleaseNULL(pool)` path runs when `ok == false`.
  - The same pointer is returned.
  - No framework state, ioreg property, queue state, packet pool state,
    or kernel resource is created/destroyed by the instrumentation.

- exact runtime evidence expected from this instrumentation:
  ```
  itlwm: PACKETPOOL[AirportItlwm-TX] new=<P> poolVtable=<VT>
        (size=<S> opts=<O> owner=<W> ownerVtable=<OVT>
         pktCount=<...> poolFlags=0x1 type=1)
  itlwm: PACKETPOOL[AirportItlwm-TX] initWithName=0
        (type=1 slot18=<A> slot20=<B>)
  itlwm: PACKETPOOL[AirportItlwm-TX] FAIL: initWithName returned false;
        pool released to NULL
  ```

  The same pattern is expected for RX. After Stage 2 reboot:
  - `slot18=0x0 slot20=0x0` means the failure happens before internal
    pool-handle publication.
  - a non-NULL `slot18` or `slot20` means the framework made it past
    an earlier internal creation step and failed later.

## CHANGED FILES

changed files (CR-212-specific):
- `AirportItlwm/AirportItlwmV2.cpp`
  - Remove rejected CR-211 active direct pbufpool probe matrix from
    `AirportItlwm::start` STEP 8b.
  - Keep `packetType = kIOSkywalkPacketTypeNetwork`.
  - Extend CR-209 `PACKETPOOL` logging with `poolVtable`,
    `ownerVtable`, `slot18`, and `slot20`.
- `commit-approval/build_evidence/CR-212-build-passive-pool-object-state.txt`

## DIFF SUMMARY

```diff
@@ AirportItlwmIO80211PacketPool::withName factory
-        // CR-211 ... direct kpinit probe matrix carried in start()
+        // CR-212 passive diagnostic instrumentation: keep the production
+        // Network packet type unchanged and log only object/input state around
+        // the existing initWithName call.
         const UInt32 packetType = kIOSkywalkPacketTypeNetwork;
         AirportItlwmIO80211PacketPool *pool =
             new AirportItlwmIO80211PacketPool;
+        void *poolVtable = pool ? *reinterpret_cast<void **>(pool) : nullptr;
+        void *ownerVtable = owner ? *reinterpret_cast<void **>(owner) : nullptr;
+        XYLog("... poolVtable=%p ... ownerVtable=%p ...", ...);
         ...
         bool ok = pool->initWithName(name, owner, packetType, options);
+        void *slot18 = *reinterpret_cast<void **>(
+            reinterpret_cast<uint8_t *>(pool) + 0x18);
+        void *slot20 = *reinterpret_cast<void **>(
+            reinterpret_cast<uint8_t *>(pool) + 0x20);
+        XYLog("... initWithName=%d (type=%d slot18=%p slot20=%p)", ...);

@@ AirportItlwm::start STEP 8b prologue
-    // CR-211 comprehensive probe matrix...
-    {
-        struct ProbeCase { ... };
-        ...
-        int rc = kern_pbufpool_create(&kpinit, &kpp, nullptr);
-        ...
-        kern_pbufpool_destroy(kpp);
-    }
```

## EVIDENCE FROM DECOMP

Carried forward:
- `AppleBCMWLANPCIeSkywalk::allocSkywalkCommonResources` shows the
  reference Network-pool path uses Network packet pools with
  `poolFlags=1`.
- `IOSkywalkPacketBufferPool::initWithName` is the framework boundary
  currently returning false.
- CR-209 runtime already established that local object allocation
  succeeds and `initWithName` is the observed failing call.

CR-212 does not add new decomp claims. It is a narrower passive
diagnostic to determine whether the known framework boundary fails
before or after internal object-state publication.

## VERIFICATION PERFORMED

- build:
  - `scripts/build_tahoe.sh`: `BUILD SUCCEEDED`
  - BootKC undef-symbol verification:
    `OK: all 884 undefined symbols resolve against BootKC`
  - kext path:
    `Build/Debug/Tahoe/AirportItlwm.kext/Contents/MacOS/AirportItlwm`
  - kext sha256:
    `23df3c9204a3469fb48151abcb65b78ccd8ebd941c81bba75b71f3422b484bed`
  - kext UUID:
    `8E888F45-92BE-3879-AC2E-35DC729F625D`
  - kext size: `16285216`
  - Build evidence file:
    `commit-approval/build_evidence/CR-212-build-passive-pool-object-state.txt`
- static checks:
  - `git diff --check HEAD`: PASS.
  - Active `PROBE_MATRIX` / `ProbeCase` code removed.
  - No direct `kern_pbufpool_create` / `kern_pbufpool_destroy` calls
    remain in code; only explanatory comments around the existing
    PoolOptions mapping mention `kern_pbufpool_create`.
  - `kIOSkywalkPacketTypeGeneric` differential path removed.
- before reproduction result: CR-209/CR-208 evidence says pool factory
  reaches `initWithName=0`.
- after reproduction result: PENDING.

## STAGE RULES

- request_stage = STAGE_1_STRUCTURAL.
- after evidence: PENDING_HOST_REBOOT.
- after reproduction result: PENDING.
- request goal: reviewer structural approval to proceed with
  Stage 2 after-fix runtime collection on the exact reviewed HEAD
  and exact reviewed CR-212 cumulative artifact.
- commit is NOT requested at Stage 1. Commit may only be requested
  after successful after-fix runtime evidence.

## RESIDUAL UNCERTAINTY

- If both `slot18` and `slot20` are NULL after `initWithName=0`, the
  next fix must target pre-publication framework input/contract shape.
- If either slot is non-NULL after `initWithName=0`, the next fix must
  target the later consistency / wrapper-side path that returns false
  after partial state publication.
- If object words are unexpected, the next fix must target subclass
  layout / owner binding before trying more pbufpool parameters.

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
- why rejected: this request must only observe existing production
  state, not perturb kernel resource/accounting/timing state.

## PROPOSED COMMIT MESSAGE

```
AirportItlwm: passively log pool object state around initWithName

CR-211's direct kern_pbufpool_create probe matrix was rejected because
it actively created/destroyed kernel pool resources. Replace it with
passive diagnostic logging in the existing pool factory path.

The production packet type remains kIOSkywalkPacketTypeNetwork and
PoolOptions are unchanged. The new logs record the allocated pool and
owner object words after new, plus pool object slots 0x18 and 0x20
after the existing initWithName call returns. This identifies whether
the framework failed before or after internal state publication without
changing arguments, ordering, ownership, retry/fallback behavior, or
kernel resource state.

CR-212 / Stage 1.
```

## PATCH ARTIFACT

exact patch artifact:
`commit-approval/artifacts/CR-212-passive-pool-object-state-diagnostic.diff`

The artifact captures the cumulative staged/live diff at submission. It
is left **untracked** so it does not appear in `git diff --binary HEAD`
and matches that diff byte-for-byte under `cmp`.

The auditor may re-verify identity by:

```
git rev-parse HEAD
shasum -a 256 commit-approval/artifacts/CR-212-passive-pool-object-state-diagnostic.diff
wc -l           commit-approval/artifacts/CR-212-passive-pool-object-state-diagnostic.diff
git diff --binary HEAD | shasum -a 256
git diff --binary HEAD | wc -l
cmp -s <(git diff --binary HEAD) commit-approval/artifacts/CR-212-passive-pool-object-state-diagnostic.diff && echo OK
git diff --check HEAD
```

The two `shasum -a 256` outputs (artifact file and live diff) MUST be
identical. `cmp` is the binding identity gate.

## SUPERSEDES

supersedes (formal):
- CR-211 (rejected).

implicitly invalidates:
- CR-210 rejection remains final; its Generic differential path is not
  restored.
- CR-209 Stage 1 approval is superseded by the extended diagnostic
  diff. CR-209 logging is preserved.
- CR-208 Stage 1 approval remains superseded; its `poolFlags=1` source
  change is preserved on the production path.
