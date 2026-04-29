# CR-210 — Differential diagnostic Generic vs Network packet type (DIAGNOSTIC_INSTRUMENTATION)

- date: 2026-04-29
- stage: STAGE_1_STRUCTURAL
- justification class: DIAGNOSTIC_INSTRUMENTATION
- branch: master
- HEAD: `d3a07c2abccac863e1909aa562051a6ee5687245`
- requested by: Executor+Committer (per `docs/WORKFLOW_ITLWM.md`)
- supersedes: implicitly invalidates CR-209 Stage 1 approval
  (`approval_invalid_if_diff_changes: YES`). CR-209's diagnostic
  logging is preserved unchanged; CR-210 only flips the `packetType`
  argument passed to the framework's `IOSkywalkPacketBufferPool::initWithName(...)`
  from `kIOSkywalkPacketTypeNetwork` (=1) to
  `kIOSkywalkPacketTypeGeneric` (=0).

does_this_fix_proven_current_root_cause: NO
This is diagnostic-only. CR-209 Stage 2 evidence (host reboot
2026-04-29 00:15) confirmed the failure axis is the framework
`initWithName` boundary (`new` succeeds for both TX and RX,
`initWithName` returns 0 for both). CR-210 isolates whether the
local subclass infrastructure is sound by issuing the pre-d3a07c2
working baseline `packetType=Generic` while keeping all other
parameters (PoolOptions, owner, factory class) identical.

## SYMPTOM

Same boot-time symptom as CR-208 / CR-209: `pools: TX=0x0 RX=0x0`,
`FAIL: pool creation`, `IO80211Controller::stop`. Networks not visible.

CR-209 added evidence to the symptom:
```
PACKETPOOL[AirportItlwm-TX] new=<private> (size=200 ... poolFlags=0x1 type=1)
PACKETPOOL[AirportItlwm-TX] initWithName=0
PACKETPOOL[AirportItlwm-TX] FAIL: initWithName returned false; pool released to NULL
```

`new` succeeds (size 0xC8 accepted by kalloc_type for our subclass),
`initWithName` returns 0 — the framework rejects the pool inside its
own `IOSkywalkPacketBufferPool::initWithName(...)` at KDK
`IOSkywalkFamily.kext` `0x9bf0`.

## DIVERGENCE

- exact divergence point: still inside the framework
  `initWithName` flow. Static disasm of
  `kern_pbufpool_create` at `0xffffff80009f1d30` shows the early
  validations that we already pass (kbi_version=2, packets!=0,
  buflets>=packets, bufsize!=0, max_frags!=0, ctor/dtor pair,
  flags-mutex). The actual rejection must therefore happen inside
  `_pp_create` (BootKC `0xffffff80009ed050`) or in the post-create
  `*out_handle` consistency check at `0x9eb5`. Without further
  evidence we cannot pinpoint which.
- confirmed deviation: NOT YET CONFIRMED. CR-208's `poolFlags=1`
  hypothesis is necessary-for-AppleBCMWLAN-1:1 but insufficient.
  Some additional reference divergence remains.
- confirmed root cause: TO_BE_DETERMINED.
- exact confirmed deviation removed: NONE in this CR.
- exact semantic mismatch removed: NONE in this CR. CR-210 is a
  pure A/B diagnostic.

## CLAIM SCOPE

- exact claim scope: rebind the `packetType` argument passed to
  `pool->initWithName(...)` (and the `(int)` value printed in the
  `PACKETPOOL[...] new=...` log line) from
  `kIOSkywalkPacketTypeNetwork` (=1) to
  `kIOSkywalkPacketTypeGeneric` (=0). The diagnostic log line
  `initWithName=...` is suffixed with `(DIFFERENTIAL type=0)`
  for unambiguous identification at log-read time.
- non_claims:
  - This request does NOT claim a fix.
  - This request does NOT claim that
    `kIOSkywalkPacketTypeGeneric` is the long-term correct
    packet type for the Wi-Fi infrastructure pool — the long-term
    correct value is `kIOSkywalkPacketTypeNetwork` per the
    AppleBCMWLAN reference and the framework's `newPacket`
    type-dispatch (`IOSkywalkNetworkPacket::withPool` for type=1).
  - This request does NOT claim any change to PoolOptions,
    `poolFlags`, packet/buffer sizes, owner, or the subclass's
    `newPacket` override.
  - This request does NOT claim any change to the start-sequence
    ordering, queue attachment, interface registration, or any
    later layer.

## JUSTIFICATION PATH

justification path: DIAGNOSTIC_INSTRUMENTATION

- exact hypotheses being disambiguated:
  1. `initWithName=0` is type=Network-specific: the framework's
     `kern_pbufpool_create` (and downstream `_pp_create`) reject
     our kpinit on the type=1 codepath. With type=0 the pool
     would succeed (matching the pre-d3a07c2 runtime baseline).
  2. `initWithName=0` is independent of packetType: the failure
     persists with type=0, which means the issue is in our
     subclass infrastructure (vtable, kalloc_type signature,
     metaclass registration) or in the kpinit fields shared
     across types (packetCount, bufferSize, owner identity, etc.).

- exact probe points:
  - `AirportItlwm/AirportItlwmV2.cpp` —
    `AirportItlwmIO80211PacketPool::withName` factory: the
    `packetType` constant passed to `pool->initWithName(...)` is
    rebound; the same constant is also reflected in the existing
    CR-209 `PACKETPOOL[...] new=... type=...` log line, and the
    existing CR-209 `initWithName=...` line is suffixed with
    `(DIFFERENTIAL type=0)`.

- why these probe points are sufficient: the failure boundary is
  `initWithName`, exactly the call we re-execute with the new
  `packetType` value. The log lines emitted from CR-209 already
  capture the relevant runtime evidence; CR-210 only changes one
  argument and clarifies the log.

- why instrumentation is behavior-neutral:
  - The change is `kIOSkywalkPacketTypeNetwork` → `kIOSkywalkPacketTypeGeneric`
    in a single argument position. Both values are valid
    `enum IOSkywalkPacketTypes` constants accepted by the framework
    (the early validation at `0x9c1d` rejects only `packetType >= 3`).
  - With type=0 the pre-d3a07c2 runtime baseline is known to have
    succeeded (CR-174/175/176 reports), so this is not an untested
    direction.
  - No retry, fallback (we run only one type), masking, forced
    state, or workaround logic is introduced.
  - No vtable / class-layout / kpinit-field / owner change.
  - Build size delta vs CR-209: identical (only an additional log
    suffix adds a few bytes; same total kext size 16285216).

- exact runtime evidence expected from this instrumentation:
  - On next boot the log will print one of two patterns for both
    TX and RX:
    - `initWithName=1 (DIFFERENTIAL type=0)` →
      hypothesis 1 confirmed; the next CR will investigate
      Network-type-specific framework requirements
      (e.g., kbi_buf_seg_size, KBIF_USER_ACCESS pool prerequisites,
      AppleBCMWLAN's intermediate-class extra setup).
    - `initWithName=0 (DIFFERENTIAL type=0)` and
      `FAIL: initWithName returned false` →
      hypothesis 2 confirmed; the issue is not the packet type
      itself. The next CR will deepen probing into kpinit fields
      or the subclass / kalloc_type signature.

## CHANGED FILES

changed files (CR-210-specific):
- `AirportItlwm/AirportItlwmV2.cpp` — rebound `packetType`
  constant, reordered log to use the same constant.
- `commit-approval/build_evidence/CR-210-build-differential-generic.txt`

The CR-210 cumulative artifact also carries forward all previously
staged content from CR-201/CR-203/CR-204/CR-206/CR-208/CR-209.

## DIFF SUMMARY

```diff
@@ AirportItlwmIO80211PacketPool::withName factory
     {
+        // CR-210 differential diagnostic: temporarily issue
+        // kIOSkywalkPacketTypeGeneric (the pre-d3a07c2 working baseline)
+        // ...
+        const UInt32 packetType = kIOSkywalkPacketTypeGeneric;
+
         // CR-209 diagnostic instrumentation: ...
         AirportItlwmIO80211PacketPool *pool = new AirportItlwmIO80211PacketPool;
         XYLog("itlwm: PACKETPOOL[%s] new=%p ...
               poolFlags=0x%x type=%d)\n",
               ...
-              (int)kIOSkywalkPacketTypeNetwork);
+              (int)packetType);
-        bool ok = pool->initWithName(name, owner,
-                                     kIOSkywalkPacketTypeNetwork, options);
-        XYLog("itlwm: PACKETPOOL[%s] initWithName=%d\n",
-              name ? name : "(null)", ok ? 1 : 0);
+        bool ok = pool->initWithName(name, owner, packetType, options);
+        XYLog("itlwm: PACKETPOOL[%s] initWithName=%d (DIFFERENTIAL type=%d)\n",
+              name ? name : "(null)", ok ? 1 : 0, (int)packetType);
```

## EVIDENCE FROM DECOMP

Same evidence from CR-208 / CR-209 carried forward, plus:

- `_kern_pbufpool_create` (BootKC `0xffffff80009f1d30`): early
  argument validation at `0x9c1d..0x9cb7` accepts both `packetType=0`
  and `packetType=1` (the only rejection is `packetType >= 3`).
  The flag-mapping at `0x9d3d..0x9d57` differs by packet type:
    - `packetType=0` → KBIF_QUANTUM (`0x1`) auto-set in kbi_flags.
    - `packetType=1` → no auto-bit; `KBIF_USER_ACCESS` (`0x20`)
      must come from `poolFlags & 1`.
- `_pp_create` (BootKC `0xffffff80009ed050`) is the actual pool
  creator called from `kern_pbufpool_create+0x2b4`. Its rejection
  semantics depend on the type-derived flag word.
- `KBIF_*` flag-word bit definitions per
  `MacKernelSDK/Headers/skywalk/packet/os_packet.h:357-368`:
  ```
  #define KBIF_QUANTUM            0x1
  #define KBIF_PERSISTENT         0x2
  #define KBIF_MONOLITHIC         0x4
  #define KBIF_BUFFER_ON_DEMAND   0x8
  #define KBIF_INHIBIT_CACHE      0x10
  #define KBIF_USER_ACCESS        0x20
  #define KBIF_VIRTUAL_DEVICE     0x40
  #define KBIF_PHYS_CONTIGUOUS    0x80
  #define KBIF_IODIR_IN           0x100
  #define KBIF_IODIR_OUT          0x200
  #define KBIF_KERNEL_READONLY    0x400
  #define KBIF_NO_MAGAZINES       0x800
  ```

This confirms type=0 (Generic) yields KBIF_QUANTUM-with-poolFlags-derived
extras, while type=1 (Network) yields KBIF_USER_ACCESS-with-poolFlags-derived
extras. Different `_pp_create` validation paths.

## EVIDENCE FROM RUNTIME

- panic logs: none.
- driver / kext logs (CR-209 Stage 2 boot, 2026-04-29 00:15, sha
  `5ac7fab0…` UUID `A69D6889-…`):
  ```
  PACKETPOOL[AirportItlwm-TX] new=<private> (size=200 opts=<private>
                                              owner=<private>
                                              pktCount=256 bufCount=256
                                              bufSize=2048 maxBPP=1
                                              memSegSz=0 poolFlags=0x1
                                              type=1)
  PACKETPOOL[AirportItlwm-TX] initWithName=0
  PACKETPOOL[AirportItlwm-TX] FAIL: initWithName returned false; pool released to NULL
  [identical pattern for AirportItlwm-RX]
  DEBUG start [STEP 8b] pools: TX=0x0 RX=0x0
  DEBUG start [STEP 8b] FAIL: pool creation (TX=0x0 RX=0x0)
  IO80211Controller::stop[868]
  ```
  Both `new` calls succeeded (allocator works for our subclass);
  both `initWithName` calls returned `0`.
- ioreg / state evidence: no `wlan0` / `IO80211SkywalkInterface`.
- before/after evidence: before is documented above; after is
  pending Stage 2 reboot of CR-210 kext.
- why this runtime evidence is semantically significant: pinpoints
  the failure boundary to the framework `initWithName` flow with
  `packetType=1`. CR-210 directly tests the next axis.
- why this is not trace-order / object-id noise: each CR-209 probe
  pair (`new=...` then `initWithName=...`) is deterministic per
  factory invocation; both TX and RX pairs reproduce identically.

## CAUSALITY

- regression window: bounded between commit `8e05ddf` (pre-d3a07c2
  baseline working) and HEAD `d3a07c2`. CR-208 + CR-209 narrow the
  failure axis to `initWithName(... packetType=Network ...)`.
- pinpointed divergence path: NOT YET PINPOINTED for the type=1
  case. CR-210 will tell us whether the failure is type=Network-
  specific (hypothesis 1) or independent of type (hypothesis 2).
- why this is root-cause-discovery: the static reference disasm
  alone (CR-208 evidence) does not predict runtime success. We
  need a live differential before any further fix is defensible.

## VERIFICATION PERFORMED

- build:
  - `scripts/build_tahoe.sh` (Debug, Tahoe destination): `BUILD SUCCEEDED`
  - BootKC undef-symbol verification: `OK: all 884 undefined symbols resolve against BootKC`
  - kext path:   `Build/Debug/Tahoe/AirportItlwm.kext/Contents/MacOS/AirportItlwm`
  - kext sha256: `bead053c4cc9ad67fb81be2bb14824200ad9bd63c3a17922d5b19f9ad9b21523`
  - kext UUID:   `299D2140-4EE2-32DE-88C7-971AD9E78EC0`
  - kext size:   `16285216`
  - Build evidence file:
    `commit-approval/build_evidence/CR-210-build-differential-generic.txt`
  - kext installed in `/Library/Extensions/AirportItlwm.kext`
    (sha verified post-install).
- targeted reproduction scenario: cold-boot Mac with CR-210 kext.
  Expected log lines printed twice (TX and RX). Each will say
  `type=0` in the `new=` line and either `initWithName=1
  (DIFFERENTIAL type=0)` or `initWithName=0 (DIFFERENTIAL type=0)`.
- before reproduction result: as documented in EVIDENCE FROM RUNTIME.
- after reproduction result: PENDING.
- negative checks:
  - `git diff --check HEAD`: PASS.
  - No retry / sleep / poll loop / fallback / forced state.
  - No vtable / class layout change.
  - Pool subclass `AirportItlwmIO80211PacketPool` and its `newPacket`
    override are unchanged.
- residual known issues not claimed fixed: pool failure remains
  until follow-up CR with the actual root-cause fix.
- scenario coverage:
  - initial boot: directly addressed.
  - sleep / wake: same factory path runs on resume; probe fires.
  - reconnect / re-open / multi-client: not applicable (one-shot
    factory).

## STAGE RULES

- request_stage = STAGE_1_STRUCTURAL.
- after evidence: PENDING_HOST_REBOOT.
- after reproduction result: PENDING.
- request goal: reviewer structural approval to proceed with
  Stage 2 after-fix runtime collection on the exact reviewed HEAD
  and exact reviewed CR-210 cumulative artifact.

## RESIDUAL UNCERTAINTY

- If hypothesis 1 fires (`initWithName=1` with type=0), the next CR
  must drill into the Network-type kpinit setup. Candidates:
  - the actual kbi_flags word the framework computes for
    AppleBCMWLAN's pool vs ours (need raw runtime read or deeper
    disasm of `kern_pbufpool_create` flag-construction);
  - kbi_buf_seg_size = 0 may be invalid for Network pools that
    require an explicit segment plan; AppleBCMWLAN may rely on
    `MONOLITHIC` (poolFlags bit 1) or a non-zero segment size when
    the device is not present at pool-create time.
- If hypothesis 2 fires (`initWithName=0` with type=0 also), the
  next CR will deepen probes inside our subclass: dump the `pool`
  object's vtable pointer post-`new` (compare to the framework's
  `__ZTV25IOSkywalkPacketBufferPool` known offset), test with the
  framework's plain `IOSkywalkPacketBufferPool::withName` factory
  to bypass our subclass entirely, and surface any kalloc_type
  sandbox messages the kernel may be emitting silently.

## FORBIDDEN ALTERNATIVES CONSIDERED AND REJECTED

- heuristic timing: not added.
- fallback path: REJECTED. CR-210 issues exactly ONE pool-creation
  attempt per call (with type=0). It does NOT try type=1 first and
  fall back. It does NOT silently retry on failure. Whatever the
  outcome of the single attempt, the diagnostic log records it and
  control flow proceeds (failure path still leads to STEP 8b
  failure and `IO80211Controller::stop`).
- masking/suppression: not added.
- force callback / state / success: not added.
- forced sync / flush / barrier: not added.
- retry / reorder / poll loop: not added.
- type erasure: not applicable.
- why rejected: the goal is to produce one clean evidence point on
  the type=0 axis, not to paper over the type=1 failure. Any
  fallback would conflate the two axes and defeat the diagnostic
  purpose.

## PROPOSED COMMIT MESSAGE

```
AirportItlwm: differential diagnostic — try Generic packet type at STEP 8b

CR-209 Stage 2 evidence pinpointed the pool creation failure to
`pool->initWithName(... packetType=kIOSkywalkPacketTypeNetwork ...)`
returning 0. CR-210 issues `kIOSkywalkPacketTypeGeneric` (the
pre-d3a07c2 working baseline) as a one-shot A/B test. With type=0
the framework's flag-mapping auto-sets KBIF_QUANTUM and routes
through a different `_pp_create` path that previously succeeded;
the next boot's log will tell us whether our subclass /
kalloc_type / kpinit-field setup is sound or whether a deeper issue
is at play.

Pure DIAGNOSTIC_INSTRUMENTATION: no retry, no fallback, no masking.
The packetType argument is the only behavioral change.
The CR-209 PACKETPOOL[...] log lines are preserved with a clear
`(DIFFERENTIAL type=0)` suffix on the `initWithName=` line so the
intent is unambiguous at log-read time.

CR-210 / Stage 1.
```

## PATCH ARTIFACT

exact patch artifact: `commit-approval/artifacts/CR-210-differential-generic-diagnostic.diff`

The artifact captures the cumulative staged diff at submission. It
is left **untracked** in the worktree so it does not appear in
`git diff --binary HEAD` and matches that diff byte-for-byte under
`cmp`.

The auditor may re-verify identity by:

```
git rev-parse HEAD
shasum -a 256 commit-approval/artifacts/CR-210-differential-generic-diagnostic.diff
wc -l           commit-approval/artifacts/CR-210-differential-generic-diagnostic.diff
git diff --binary HEAD | shasum -a 256
git diff --binary HEAD | wc -l
cmp -s <(git diff --binary HEAD) commit-approval/artifacts/CR-210-differential-generic-diagnostic.diff && echo OK
git diff --check HEAD
```

The two `shasum -a 256` outputs (artifact file and live diff) MUST be
identical. `cmp` is the binding identity gate.

## SUPERSEDES

supersedes (formal): none.

implicitly invalidates (per `approval_invalid_if_diff_changes: YES`):
- CR-209 Stage 1 approval. CR-209's diagnostic logging is preserved.
- CR-208 (already implicitly invalidated by CR-209). CR-208's
  `poolFlags = 1` is preserved on disk.
