# CR-209 — Pool factory diagnostic instrumentation (DIAGNOSTIC_INSTRUMENTATION)

- date: 2026-04-29
- stage: STAGE_1_STRUCTURAL
- justification class: DIAGNOSTIC_INSTRUMENTATION
- branch: master
- HEAD: `d3a07c2abccac863e1909aa562051a6ee5687245`
- requested by: Executor+Committer (per `docs/WORKFLOW_ITLWM.md`)
- supersedes: implicitly invalidates CR-208 Stage 1 approval
  (`approval_invalid_if_diff_changes: YES`) because CR-209 changes
  the staged diff. CR-208's source-side change (`poolOpts.poolFlags = 1`)
  is preserved in the staged tree and carried forward into the CR-209
  cumulative artifact unchanged. After CR-209 collects the
  Stage 2 evidence and a real fix CR is submitted, that future CR will
  rebind both the CR-208 single-line change and CR-209's diagnostic
  logging to the new HEAD/diff (or the diagnostic logging may be
  removed once the root cause is identified).

does_this_fix_proven_current_root_cause: NO
This is diagnostic-only. The CR-208 Stage 2 runtime evidence (host
reboot 2026-04-28 23:52, kext sha `bc899fc7…` UUID `D1086C18-…`)
showed the `poolFlags = 1` hypothesis was insufficient: pools still
fail with `TX=0x0 RX=0x0`. Without distinguishing whether the failure
is at `new AirportItlwmIO80211PacketPool` or at the
`pool->initWithName(...)` call, any further change is guesswork.
CR-209 adds three XYLog probes in the local pool factory to make the
exact failure path observable on the next boot.

## SYMPTOM

Same as CR-208's symptom: at boot with the cumulative kext, STEP 8b
emits `pools: TX=0x0 RX=0x0` followed by
`FAIL: pool creation` and `IO80211Controller::stop`. CR-208's
`poolFlags = 1` hypothesis did not change this. The next-step investigation
needs the failure point pinpointed inside the local
`AirportItlwmIO80211PacketPool::withName` factory, which is currently
opaque (only the post-factory NULL pointer is logged).

## DIVERGENCE

- exact divergence point: same as CR-208 — the
  `IOSkywalkPacketBufferPool::initWithName` boundary appears to reject
  the pool, but the boundary at which the rejection is observed
  (`new` returning NULL, vs. `initWithName` returning false) is not
  yet observable.
- confirmed deviation: NOT YET CONFIRMED. CR-208's hypothesis (the
  `poolFlags & 1` mapping at framework `0x9d54`) turned out to be
  insufficient; some other deviation remains.
- confirmed root cause: TO_BE_DETERMINED via this CR's runtime
  evidence.
- exact confirmed deviation removed: NONE in this CR.
- exact semantic mismatch removed: NONE in this CR. CR-209 adds only
  log output.

## CLAIM SCOPE

- exact claim scope: append three behavior-neutral `XYLog` calls
  inside `AirportItlwmIO80211PacketPool::withName` (the pool factory
  in `AirportItlwm/AirportItlwmV2.cpp`):
  1. After the `new`, log the pool pointer plus the input parameters
     (size, options pointer, owner pointer, every PoolOptions field,
     packetType).
  2. If `new` returned NULL, log a FAIL line and return NULL.
  3. After `initWithName`, log its `bool` return value.
  4. If `initWithName` returned false, log a FAIL line.
- non_claims:
  - This request does NOT claim any change to the pool's construction
    path, vtable, allocation size, or override behavior.
  - This request does NOT claim any change to PoolOptions field
    values (CR-208's `poolFlags = 1` is preserved unchanged).
  - This request does NOT claim a fix for the regression. It is a
    pure observability change.
  - This request does NOT claim any new lifecycle, retry, fallback,
    or alternate path on failure.

## JUSTIFICATION PATH

justification path: DIAGNOSTIC_INSTRUMENTATION

- exact hypotheses being disambiguated:
  1. `new AirportItlwmIO80211PacketPool` returned NULL (kalloc_type
     allocator rejection — possibly because of a class-size or
     kalloc_type signature mismatch between our subclass and the
     framework's expected pool size of 0xC0).
  2. `initWithName` returned false during framework-side validation
     (early return at `0x9c2b`/`0x9c4c`), or during one of the
     allocations (`0x9c89`/`0x9ca2`/`0x9cb7`/`0x9ccc`), or during
     `kern_pbufpool_create` rejection (`0x9e84` after `0x9e82`), or
     because `kern_pbufpool_create` returned 0 but did not populate
     `this[0x18]` (`0x9eb5`).

  These two hypotheses lead to fundamentally different fixes
  (kalloc_type / class layout vs. kern_pbufpool init params /
  flag word). Without distinguishing them, no further change is
  defensible.

- exact probe points:
  - `AirportItlwm/AirportItlwmV2.cpp` —
    `AirportItlwmIO80211PacketPool::withName` factory, immediately
    after `new` and immediately after `pool->initWithName(...)`.

- why these probe points are sufficient: the failure flow is
  funneled through these two function calls. The first probe
  resolves hypothesis 1 (`new == NULL`). The second probe resolves
  hypothesis 2 (`initWithName == false`). Together they always
  identify exactly one of the two boundaries as the failing one.

- why instrumentation is behavior-neutral:
  - `XYLog` is the existing project-wide logging macro; its
    side-effect is a single kernel log entry per call.
  - The probes do NOT change control flow on success: the same `new`
    and `initWithName` calls run with the same arguments; the same
    `OSSafeReleaseNULL` runs on failure; the same pointer is
    returned to the caller.
  - The probes do NOT introduce retry, delay, fallback, mask,
    forced state, or any other forbidden alternative.
  - The probes do NOT mutate any framework state, ioreg property,
    or kernel-side resource. They are pure printf-style observers.
  - Build size grows by ~64 bytes (log-string literals). No new
    BootKC undefined symbols are introduced (only the already-resolved
    `XYLog` macro is used).

- exact runtime evidence expected from this instrumentation:
  ```
  itlwm: PACKETPOOL[AirportItlwm-TX] new=<P> (size=<S> opts=<O> owner=<W>
                                                pktCount=<...>
                                                poolFlags=0x<F>
                                                type=<T>)
  itlwm: PACKETPOOL[AirportItlwm-TX] initWithName=<0|1>
  itlwm: PACKETPOOL[AirportItlwm-TX] FAIL: ...
  ```

  After Stage 2 reboot, the log will contain one of three exact
  patterns:
  1. `new=0x0` followed by `FAIL: new returned NULL` →
     hypothesis 1 confirmed (kalloc_type / class layout issue).
  2. `new=<non-NULL>` followed by `initWithName=0` and
     `FAIL: initWithName returned false` →
     hypothesis 2 confirmed (framework-side initWithName rejection;
     the next CR will add a probe inside `initWithName` itself or
     dump the kpinit args).
  3. `new=<non-NULL>` followed by `initWithName=1` →
     pool construction now succeeds, in which case the previous
     `pools: TX=0x0 RX=0x0` was a stale cached read or an unrelated
     issue. (Considered very unlikely given the deterministic
     pre-CR-209 evidence.)

## CHANGED FILES

changed files (CR-209-specific):
- `AirportItlwm/AirportItlwmV2.cpp` — three appended `XYLog` calls
  in `AirportItlwmIO80211PacketPool::withName` plus an explicit
  guard around `pool == nullptr` for clean failure logging.
- `commit-approval/build_evidence/CR-209-build-diagnostic-pool-factory.txt`
  — build evidence (kext sha, UUID, size, BootKC undef-resolution,
  size delta vs. CR-208 kext).

The CR-209 cumulative artifact also carries forward all previously
staged content from CR-201/CR-203/CR-204/CR-206/CR-208 (header
alignments, primitives, addPeer return-type fix, supporting docs/YAML,
and the CR-208 `poolOpts.poolFlags = 1` source change).

## DIFF SUMMARY

```diff
@@ AirportItlwmIO80211PacketPool::withName factory
     static AirportItlwmIO80211PacketPool *withName(...)
     {
+        // CR-209 diagnostic instrumentation: distinguish (a) `new` returned
+        // NULL from (b) `initWithName` returned false, so the runtime log
+        // pinpoints the exact failure step in the pool factory chain.
         AirportItlwmIO80211PacketPool *pool =
             new AirportItlwmIO80211PacketPool;
-        if (pool != nullptr &&
-            !pool->initWithName(name, owner, kIOSkywalkPacketTypeNetwork,
-                                options)) {
-            OSSafeReleaseNULL(pool);
-        }
+        XYLog("itlwm: PACKETPOOL[%s] new=%p (size=%lu opts=%p owner=%p "
+              "pktCount=%u bufCount=%u bufSize=%u maxBPP=%u memSegSz=%u "
+              "poolFlags=0x%x type=%d)\n",
+              name ? name : "(null)",
+              pool, sizeof(AirportItlwmIO80211PacketPool),
+              options, owner,
+              options ? options->packetCount : 0,
+              options ? options->bufferCount : 0,
+              options ? options->bufferSize : 0,
+              options ? options->maxBuffersPerPacket : 0,
+              options ? options->memorySegmentSize : 0,
+              options ? options->poolFlags : 0,
+              (int)kIOSkywalkPacketTypeNetwork);
+        if (pool == nullptr) {
+            XYLog("itlwm: PACKETPOOL[%s] FAIL: new returned NULL\n",
+                  name ? name : "(null)");
+            return nullptr;
+        }
+        bool ok = pool->initWithName(name, owner,
+                                     kIOSkywalkPacketTypeNetwork, options);
+        XYLog("itlwm: PACKETPOOL[%s] initWithName=%d\n",
+              name ? name : "(null)", ok ? 1 : 0);
+        if (!ok) {
+            OSSafeReleaseNULL(pool);
+            XYLog("itlwm: PACKETPOOL[%s] FAIL: initWithName returned false; "
+                  "pool released to NULL\n", name ? name : "(null)");
+        }
         return pool;
     }
```

## EVIDENCE FROM DECOMP

Same evidence as CR-208 (carried forward, unchanged):
- `AppleBCMWLANPCIeSkywalk::allocSkywalkCommonResources` @
  `0xffffff80014ccd56` shows reference Network-pool PoolOptions with
  `poolFlags=1`.
- `IOSkywalkPacketBufferPool::initWithName` flag-mapping @
  KDK `IOSkywalkFamily.kext` `0x9bf0..0x9d82` documents how
  `poolFlags & 1` maps to `kern_pbufpool` flag bit 5.

The 2026-04-28 23:52 Stage 2 reboot shows that this evidence alone
does NOT predict success. Therefore some additional reference
divergence remains; CR-209's instrumentation is the next step in
locating it.

## EVIDENCE FROM RUNTIME

- panic logs: none.
- driver / kext logs (Stage 2 reboot of CR-208 cumulative kext,
  2026-04-28 23:52, sha `bc899fc7…` UUID `D1086C18-…`):
  ```
  23:52:48.870  itlwm: DEBUG start [STEP 8b] pools: TX=0x0 RX=0x0
  23:52:48.878  itlwm: DEBUG start [STEP 8b] FAIL: pool creation (TX=0x0 RX=0x0)
  23:52:48.886  IO80211Controller::stop[868]
  23:52:48.896  IO80211Controller::stop[939] super::stop
  ```
  Identical pattern to the pre-CR-208 boot at 22:05:28. The
  `poolFlags = 1` change had no observable runtime effect at STEP 8b.
- ioreg / state evidence: no `wlan0` interface enumerated; no
  `IO80211SkywalkInterface` published; CoreWLAN scan returns empty.
- packet / firmware / transport trace: not applicable.
- before evidence: pool factory returned NULL for both TX and RX;
  no per-step diagnostic log between `new` and `initWithName`.
- after evidence: PENDING — Stage 2 will run after host reboots
  with the CR-209 kext (sha
  `5ac7fab025ba1b6927cb456fa9370a86cd51afdd6a5fa677bb21e5b7f0fa1234`,
  UUID `A69D6889-6392-3359-93FD-2D995691D6C6`).
- why this runtime evidence is semantically significant: identifying
  the exact failure boundary is a prerequisite for any further
  reference-alignment work. Either the kalloc_type / class-layout
  axis or the kern_pbufpool axis must be excluded before the next
  fix candidate is defensible.
- why this is not trace-order / object-id noise: each `XYLog` call
  is paired with a deterministic event (post-`new`, post-`initWithName`,
  failure-branch entry). The pre-fix boot already shows the failure
  is deterministic; only the cause is unknown.

## CAUSALITY

- regression window: still bounded between commit `8e05ddf`
  (pre-d3a07c2 baseline working) and HEAD `d3a07c2`. CR-208's
  partial hypothesis (`poolFlags=1`) did not narrow the window
  further at the runtime axis.
- pinpointed divergence path: NOT YET PINPOINTED. CR-209 will pinpoint
  the failure to one of:
  (a) `new` allocation (kalloc_type / class layout)
  (b) `initWithName` early validation
  (c) `initWithName` resource allocation (thread_call /
      IOMallocTypeImpl / IORecursiveLockAlloc)
  (d) `kern_pbufpool_create` rejection
  (e) `kern_pbufpool_create` succeeded but `this[0x18]` not populated
- why this is root-cause-discovery rather than guesswork: the
  framework's flag-mapping disasm and AppleBCMWLAN reference
  decomp both point at `poolFlags = 1` for type=Network. CR-208
  applied that exactly. The continued failure means there is at
  least one additional divergence we cannot identify from the
  reference's static evidence alone. Live runtime probes are the
  next dimension of evidence.

## VERIFICATION PERFORMED

- build:
  - `scripts/build_tahoe.sh` (Debug, Tahoe destination): `BUILD SUCCEEDED`
  - BootKC undef-symbol verification: `OK: all 884 undefined symbols resolve against BootKC`
  - kext path:   `Build/Debug/Tahoe/AirportItlwm.kext/Contents/MacOS/AirportItlwm`
  - kext sha256: `5ac7fab025ba1b6927cb456fa9370a86cd51afdd6a5fa677bb21e5b7f0fa1234`
  - kext UUID:   `A69D6889-6392-3359-93FD-2D995691D6C6`
  - kext size:   `16285216` (CR-208 build was 16285152; +64 bytes for log strings)
  - Build evidence file:
    `commit-approval/build_evidence/CR-209-build-diagnostic-pool-factory.txt`
  - kext installed in `/Library/Extensions/AirportItlwm.kext`
    (sha verified post-install).
- targeted reproduction scenario: cold-boot Mac with CR-209 kext.
  Expected log lines printed twice (TX and RX pools). Each pair will
  identify which boundary (new or initWithName) failed. Pool
  failure is otherwise unchanged: STEP 8b still ends with
  `FAIL: pool creation (TX=0x0 RX=0x0)` and the controller stops.
- before reproduction result: as documented in EVIDENCE FROM RUNTIME.
- after reproduction result: PENDING.
- negative checks:
  - `git diff --check HEAD`: PASS (no whitespace warnings).
  - No semantic change to pool construction.
  - No retry / sleep / poll loop / fallback / forced state.
  - No vtable / class layout change.
  - No new undefined symbols (only XYLog macro used).
- residual known issues not claimed fixed: the pool failure itself
  remains until a follow-up CR with the actual root-cause fix.
- scenario coverage:
  - initial boot: directly addressed (Stage 2 boot).
  - reconnect / re-open: not applicable (factory only runs at start).
  - sleep / wake: same probes will fire on resume (since `start` is
    re-run). Probes are stateless.

## STAGE RULES

- request_stage = STAGE_1_STRUCTURAL.
- after evidence: PENDING_HOST_REBOOT.
- after reproduction result: PENDING.
- request goal: reviewer structural approval to proceed with
  Stage 2 after-fix runtime collection on the exact reviewed HEAD
  and exact reviewed CR-209 cumulative artifact.

## RESIDUAL UNCERTAINTY

- Whether the failure axis after CR-209 evidence is gathered will be
  hypothesis 1 (`new` returns NULL) or hypothesis 2 (`initWithName`
  returns false) is the entire purpose of CR-209. Either outcome is
  acceptable.
- If hypothesis 2 fires, a follow-up CR will be needed to add probes
  inside or around `initWithName` (e.g., dump the kpinit struct
  before `kern_pbufpool_create`). That is out of CR-209 scope.

## FORBIDDEN ALTERNATIVES CONSIDERED AND REJECTED

- heuristic timing: not added.
- fallback path: REJECTED. We do NOT silently retry with
  `packetType=Generic` on Network-type failure. We do NOT fall back
  to the framework's plain `withName` factory. We do NOT mask the
  failure.
- masking/suppression: not added.
- force callback / state / success: not added.
- forced sync / flush / barrier: not added.
- retry / reorder / poll loop: not added.
- type erasure: not applicable.
- why rejected: the goal of this CR is to make the failure
  observable, not to paper over it. Any fallback would defeat the
  diagnostic purpose.

## PROPOSED COMMIT MESSAGE

```
AirportItlwm: instrument pool factory to pinpoint STEP 8b failure

CR-208's poolFlags=1 hypothesis was insufficient — Stage 2 runtime
(2026-04-28 23:52, kext bc899fc7… / D1086C18-…) shows the same
"DEBUG start [STEP 8b] FAIL: pool creation (TX=0x0 RX=0x0)" pattern
as before the change. The local pool factory hides which exact
boundary fails (the C++ `new`, the kalloc_type allocator, framework
initWithName early validation, framework allocations, or
kern_pbufpool_create itself).

Add three XYLog probes in AirportItlwmIO80211PacketPool::withName
that bracket the `new` and `initWithName` calls and dump the input
PoolOptions, owner, and packetType. After the next boot, the log
will identify exactly one of:
  (a) new returned NULL
  (b) initWithName returned false
which constrains the next fix candidate to one of two disjoint axes
(class layout / kalloc_type vs. kern_pbufpool init params).

Pure DIAGNOSTIC_INSTRUMENTATION: no control-flow change on success,
no retry / fallback / masking. +64 bytes of log strings. No new
BootKC undefined symbols.

CR-209 / Stage 1.
```

## PATCH ARTIFACT

exact patch artifact: `commit-approval/artifacts/CR-209-pool-factory-diagnostic.diff`

The artifact captures the cumulative staged diff at the time of
submission. It is left **untracked** in the worktree (not `git add`'d)
so it does not appear in `git diff --binary HEAD` output and matches
that diff byte-for-byte under `cmp`.

Concrete inlined identity (request-write-time, no placeholders):

- HEAD: `d3a07c2abccac863e1909aa562051a6ee5687245`
- archived artifact path: `commit-approval/artifacts/CR-209-pool-factory-diagnostic.diff`
- request-write-time artifact sha256: see binding `cmp` gate below;
  the inlined sha is a forensic snapshot subject to drift across
  the very act of inlining identity into the request file.
- request-write-time artifact lines: see verification block (byte-stable).
- request-write-time artifact file count (diff hunks): see verification
  block (byte-stable).
- the auditor's final `git diff --binary HEAD | shasum -a 256` may
  differ from the inlined snapshot; the binding identity gate is
  `cmp -s` (see verification block below), not the inlined sha.

The auditor may re-verify identity by:

```
git rev-parse HEAD
shasum -a 256 commit-approval/artifacts/CR-209-pool-factory-diagnostic.diff
wc -l           commit-approval/artifacts/CR-209-pool-factory-diagnostic.diff
git diff --binary HEAD | shasum -a 256
git diff --binary HEAD | wc -l
cmp -s <(git diff --binary HEAD) commit-approval/artifacts/CR-209-pool-factory-diagnostic.diff && echo OK
git diff --check HEAD
```

The two `shasum -a 256` outputs (artifact file and live diff) MUST be
identical. `cmp` is the binding identity gate.

## SUPERSEDES

supersedes (formal): none.

implicitly invalidates (per `approval_invalid_if_diff_changes: YES`):
- CR-208 Stage 1 approval (`COMMIT_DECISION_CR-208.md`,
  `APPROVED_FOR_AFTER_FIX_RUNTIME`) — CR-208's Stage 2 runtime evidence
  confirmed the fix was insufficient, so the approval is also
  practically invalidated by Stage 2 outcome. CR-209's diff includes
  CR-208's `poolOpts.poolFlags = 1` line unchanged; that line will be
  re-evaluated in the next fix CR (after CR-209 evidence narrows the
  failure axis).
- CR-206 Stage 1 approval — already implicitly invalidated by CR-208;
  the CR-206 source-side change is preserved on disk per audit-trail
  policy.
