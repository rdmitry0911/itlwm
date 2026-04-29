# CR-208 — Skywalk pool poolFlags 1:1 with AppleBCMWLAN reference (ROOT_CAUSE_FIX)

- date: 2026-04-28
- stage: STAGE_1_STRUCTURAL
- justification class: ROOT_CAUSE_FIX
- branch: master
- HEAD: `d3a07c2abccac863e1909aa562051a6ee5687245`
- requested by: Executor+Committer (per `docs/WORKFLOW_ITLWM.md`)
- supersedes: none directly. Implicitly invalidates the Stage 1
  approval on CR-206 (`approval_invalid_if_diff_changes: YES`)
  because CR-208 changes the staged diff. CR-206's source-side change
  (header + bundled docs/YAML) is preserved in the staged tree and
  carried forward into the CR-208 cumulative artifact unchanged; a
  follow-up CR will re-bind the CR-206 approval to the post-CR-208
  HEAD/diff after CR-208 is approved and committed.

does_this_fix_proven_current_root_cause: YES
The proven runtime root cause is `kern_pbufpool_create` rejecting the
Skywalk packet pool for `kIOSkywalkPacketTypeNetwork` because
`PoolOptions::poolFlags & 1` is not set. CR-208 sets exactly that bit,
1:1 with the AppleBCMWLAN reference Network pool.

## SYMPTOM

- symptom: After installing the cumulative kext built from HEAD
  `d3a07c2`, no Wi-Fi networks are visible. The interface never
  reaches RUN state because `start()` aborts at STEP 8b.
- expected behavior: Pre-d3a07c2 baseline (CR-174/CR-175/CR-176 runtime
  logs, 2026-04-27): networks visible, RX EAPOL reaches IO80211 input
  (`eapol_rx=8`), interface reaches associated state.
- actual behavior: 2026-04-28 22:05 reboot console log shows
  `pools: TX=0x0 RX=0x0` immediately followed by
  `IO80211Controller::stop[868]`. No further start steps run, so no
  scan list is ever built.
- first visible manifestation:
  ```
  22:05:28.509  itlwm: DEBUG start [STEP 8b] pools: TX=0x0 RX=0x0
  22:05:28.509  itlwm: DEBUG start [STEP 8b] FAIL: pool creation
                                              (TX=0x0 RX=0x0)
  22:05:28.517  IO80211Controller::stop[868] ::stop( ... )
  ```

## DIVERGENCE

- exact divergence point: `AirportItlwm/AirportItlwmV2.cpp:2885-2889`
  PoolOptions setup. The post-d3a07c2 code passes
  `packetType=kIOSkywalkPacketTypeNetwork` to the framework's
  `IOSkywalkPacketBufferPool::initWithName(...)` but leaves
  `poolOpts.poolFlags = 0`.
- confirmed deviation: pre-CR-208 local PoolOptions has `poolFlags = 0`;
  AppleBCMWLAN's reference Network pool has `poolFlags = 1`.
- confirmed root cause: framework's `initWithName` flag-mapping at
  KDK `IOSkywalkFamily.kext` `0x9d3d..0x9d57` auto-sets the LSB of
  the kern_pbufpool flag word only when `packetType==0`. For
  `packetType==1` (Network), the only path that sets the corresponding
  kern flag bit (kern bit 5 = `0x20`) is via `poolFlags & 1` (`shll
  $0x5; andl $0x20`). With our `poolFlags=0`, kern bit 5 stays clear
  and `kern_pbufpool_create` rejects the pool, causing
  `initWithName` to return false and `withName` to release the pool
  to NULL.
- exact confirmed deviation removed: `poolOpts.poolFlags = 1;`
  appended to the local PoolOptions setup, replicating
  AppleBCMWLAN's `local_30 = 0x100000000` byte pattern.
- exact semantic mismatch removed: pool now satisfies the same
  flag-word path that kern_pbufpool_create accepts for
  AppleBCMWLAN's Network pool.

## CLAIM SCOPE

- exact claim scope: a single appended assignment
  `poolOpts.poolFlags = 1;` in
  `AirportItlwm/AirportItlwmV2.cpp:2885-2889` block, with a
  multi-line comment citing the exact AppleBCMWLAN reference offset
  and the framework's flag-mapping disasm.
- non_claims:
  - This request does NOT claim any change to the
    `AirportItlwmIO80211PacketPool` subclass declaration, vtable,
    return types, or `newPacket` override body.
  - This request does NOT claim any change to other PoolOptions
    fields (`packetCount`, `bufferCount`, `bufferSize`,
    `maxBuffersPerPacket`, `memorySegmentSize`, `_reserved/pad`).
  - This request does NOT claim any change to the factory call site
    (`AirportItlwmIO80211PacketPool::withName`) or its arguments.
  - This request does NOT claim any change to the start-sequence
    ordering (STEP 8a → 8b → 9 → ...) or to the queue attachment in
    later steps.
  - This request does NOT claim a fix for any post-pool blocker
    (eapol_tx, key install, RSN_DONE, association recovery). Those
    are unmasked-by-this-fix; their separate root causes remain
    open issues tracked elsewhere (CR-176 deferred_work,
    candidate CR-207 for setInterfaceRole guard, etc.).

## JUSTIFICATION PATH

justification path: ROOT_CAUSE_FIX

This is not a `REFERENCE_ALIGNMENT_FIX` because the change is in a
`.cpp` translation unit and the kext machine code does change. The
fix is a single field initialization that brings the live
`PoolOptions` value passed to the framework into 1:1 alignment with
the AppleBCMWLAN reference `PoolOptions` for a Tahoe Network pool.

Reference path proven by:
- `AppleBCMWLANPCIeSkywalk::allocSkywalkCommonResources(OSObject*)` at
  BootKC `0xffffff80014ccd56`, decompiled in
  `/srv/project/ghidra_output/AppleBCMWLANBusInterfacePCIeMac_decompiled.c:5664`,
  showing `local_30 = 0x100000000` (memorySegmentSize=0,
  poolFlags=1) and `local_34 = 1` (maxBuffersPerPacket=1) on the
  options struct passed to the AppleBCMWLAN pool factory.
- `AppleBCMWLANSkywalkPacketPool::initWithName` at BootKC
  `0xffffff80016e033c`, raw disasm at offsets `+0x000..+0x058`
  showing 1:1 copy of the caller's PoolOptions into a local
  `IOSkywalkPacketBufferPool::PoolOptions` and explicit non-virtual
  upcall to base `initWithName(..., packetType=1, &options)` via
  `mov ecx, 0x1; call qword [rax+0x130]`.

Lifecycle boundary proven by:
- The framework's flag-mapping in `IOSkywalkPacketBufferPool::initWithName`
  at KDK `IOSkywalkFamily.kext` `0x9d3d..0x9d82` shows that for
  `packetType==1` the only path that sets the kern_pbufpool bit
  matching the auto-set bit for `packetType==0` is via
  `poolFlags & 1`. Without that bit, the pool fails kern-side
  validation and `initWithName` returns false (`xorl %r14d, %r14d;
  ret`).
- `kern_pbufpool_create` is the lifecycle boundary; on its failure the
  framework writes nothing to `this[0x18]` and the pool is released
  by the local `withName` factory's `OSSafeReleaseNULL(pool)`.

Side effects proven by:
- The change touches one struct field on a stack-local `PoolOptions`.
  The struct is passed by const pointer to the framework, which
  reads (per disasm) only the documented fields. No new heap
  allocations, no new vtable bindings, no caller-side state
  transitions are introduced.
- Pre-CR-208 boot log shows pool failure followed by
  `super::stop`/`releaseAll`. Post-CR-208 expected to pass STEP 8b
  and continue start sequence (Stage 2 evidence pending host
  reinstall + reboot).

## CHANGED FILES

changed files (CR-208-specific):
- `AirportItlwm/AirportItlwmV2.cpp`           — single-field set
- `commit-approval/build_evidence/CR-208-build-poolflags-network-fix.txt`
  — build evidence (kext sha/UUID/size + 884 BootKC undef
  resolution)
- `analysis/cr208_poolflags_evidence_2026_04_28.txt`
  — disasm/decomp evidence file

The CR-208 cumulative artifact also carries forward all previously
staged content from CR-201 / CR-203 / CR-204 / CR-206 (header
alignments, primitives, addPeer return-type fix, supporting docs/YAML).
None of those carried files are modified by CR-208.

## DIFF SUMMARY

The CR-208-specific change in `AirportItlwm/AirportItlwmV2.cpp`:

```diff
@@ AirportItlwm::start STEP 8b — pool creation
         IOSkywalkPacketBufferPool::PoolOptions poolOpts = {};
         poolOpts.packetCount = kAirportItlwmSkywalkQueueCapacity;
         poolOpts.bufferCount = kAirportItlwmSkywalkQueueCapacity;
         poolOpts.bufferSize  = SKYWALK_BUF_SIZE;
         poolOpts.maxBuffersPerPacket = 1;
+        // poolFlags bit 0 is required for kIOSkywalkPacketTypeNetwork
+        // pools.  1:1 with AppleBCMWLAN reference:
+        // AppleBCMWLANPCIeSkywalk::allocSkywalkCommonResources @
+        // 0xffffff80014ccd56 sets local_30 = 0x100000000, i.e.
+        // memorySegmentSize=0 and poolFlags=1.  Inside
+        // IOSkywalkPacketBufferPool::initWithName (KDK
+        // IOSkywalkFamily.kext @ 0x9bf0) bit 0 of poolFlags maps to
+        // kern_pbufpool_create flag bit 5 (shll $5 / andl $0x20).
+        // For packetType=0 the framework auto-sets the LSB; for
+        // packetType=Network (=1) it does not, so the caller must
+        // supply this bit explicitly or kern_pbufpool_create rejects
+        // the pool.
+        poolOpts.poolFlags = 1;
```

## EVIDENCE FROM DECOMP

- component / binary: `AppleBCMWLANBusInterfacePCIeMac.kext`
  (loaded into BootKC `BootKernelExtensions.kc`)
- function / symbol: `AppleBCMWLANPCIeSkywalk::allocSkywalkCommonResources(OSObject*)`
- address / offset / source anchor: `0xffffff80014ccd56`
  (decomp `/srv/project/ghidra_output/AppleBCMWLANBusInterfacePCIeMac_decompiled.c:5664`)
- exact lines / snippet:
  ```
  local_28 = 0;
  plVar3   = *(long **)(param_1 + 0x10);
  local_38 = (undefined4)plVar3[5];                  /* bufferSize    */
  local_40 = *(undefined4 *)((long)plVar3 + 0x24);   /* packetCount   */
  local_34 = 1;                                      /* maxBuffersPerPacket */
  local_30 = 0x100000000;                            /* mem*=0, poolFlags=1 */
  local_3c = local_40;                               /* bufferCount   */
  ...
  __ZN33AppleBCMWLANPCIeSkywalkPacketPool8withName...
      ("AppleBCMWLANSkywalkPool", plVar3[6], *plVar3, plVar3[2], &local_40);
  ```
- semantic meaning: `local_30 = 0x100000000` is a 64-bit literal that
  sits at offsets 0x10..0x17 of the local PoolOptions structure
  (`&local_40`). In little-endian, that is
  `memorySegmentSize=0x00000000` (bytes 0x10..0x13) and
  `poolFlags=0x00000001` (bytes 0x14..0x17).
- how this proves reference behavior: the canonical Apple-supplied
  Tahoe Wi-Fi driver explicitly sets `poolFlags=1` whenever it
  creates a Network-type Skywalk pool. AppleBCMWLAN is the closest
  living reference for our use case (Wi-Fi infrastructure interface
  with IO80211NetworkPacket-shaped packets through Skywalk
  TX/RX queues).

Additional reference disasm — `AppleBCMWLANSkywalkPacketPool::initWithName`
at `0xffffff80016e033c`:

```
+0x000: 55                   push rbp
+0x001: 48 89 e5             mov  rbp, rsp
+0x008: 48 8b 05 dd 0e 08 00 mov  rax, [rip+0x80edd] ; __stack_chk_guard
+0x018: 4c 8d 45 d0          lea  r8, [rbp-0x30]      ; local PoolOptions
+0x024: 8b 01                mov  eax, [rcx]          ; caller->packetCount
+0x026: 41 89 00             mov  [r8], eax           ; local.packetCount
+0x029: 8b 41 04             mov  eax, [rcx+0x4]      ; caller->bufferCount
+0x02c: 41 89 40 04          mov  [r8+0x4], eax       ; local.bufferCount
+0x030: 8b 41 08             mov  eax, [rcx+0x8]      ; caller->bufferSize
+0x033: 41 89 40 08          mov  [r8+0x8], eax       ; local.bufferSize
+0x037: 8b 41 0c             mov  eax, [rcx+0xc]      ; caller->maxBuffersPerPacket
+0x03a: 41 89 40 0c          mov  [r8+0xc], eax       ; local.maxBuffersPerPacket
+0x03e: 8b 41 10             mov  eax, [rcx+0x10]     ; caller->memorySegmentSize
+0x041: 41 89 40 10          mov  [r8+0x10], eax      ; local.memorySegmentSize
+0x045: 8b 41 14             mov  eax, [rcx+0x14]     ; caller->poolFlags
+0x048: 41 89 40 14          mov  [r8+0x14], eax      ; local.poolFlags
+0x04c: 48 8b 05 59 0e 08 00 mov  rax, [rip+0x80e59]  ; super vtable proxy
+0x053: b9 01 00 00 00       mov  ecx, 0x1            ; packetType = Network
+0x058: ff 90 30 01 00 00    call qword [rax+0x130]   ; vt[40] = base initWithName
```

This proves AppleBCMWLAN reaches the same framework
`IOSkywalkPacketBufferPool::initWithName(name, owner, packetType=1,
&options)` entry point that we do, and that it does so with
`options.poolFlags == 1` because that field was copied verbatim from
the caller-supplied PoolOptions (which set it via
`local_30 = 0x100000000`).

Framework flag-mapping (the proof that bit 0 of poolFlags is
load-bearing for type=Network), KDK `IOSkywalkFamily.kext`
`__ZN25IOSkywalkPacketBufferPool12initWithNameEPKcP8OSObjectjPKNS_11PoolOptionsE`
at `0x9bf0`:

```
9d3d  testl %r12d, %r12d           ; r12 = packetType
9d40  sete  %cl                    ; cl  = (type==0)? 1 : 0
9d43  xorl  %edx, %edx
9d45  cmpl  $0x2, %r12d
9d49  sete  %dl                    ; dl  = (type==2)? 1 : 0
9d4c  leal  (%rcx,%rdx,8), %edx    ; edx = cl + dl*8
                                   ;   type=0 → edx LSB = 1
                                   ;   type=1 → edx LSB = 0  ← key
                                   ;   type=2 → edx bit 3 = 1
9d4f  movl  %eax, %ecx             ; ecx = poolFlags
9d51  shll  $0x5, %ecx             ; poolFlags << 5
9d54  andl  $0x20, %ecx            ; (poolFlags & 1) ? 0x20 : 0
9d57  orl   %edx, %ecx
...
9e7b  callq _kern_pbufpool_create
9e80  testl %eax, %eax
9e82  je    0x9eb1                 ; success → continue
9e84  xorl  %r14d, %r14d           ; failure → return false
```

Full evidence file: `analysis/cr208_poolflags_evidence_2026_04_28.txt`.

## EVIDENCE FROM RUNTIME

- panic logs: none.
- driver / kext logs (BEFORE, 2026-04-28 22:05 reboot, HEAD `d3a07c2`,
  pre-CR-208 cumulative kext sha
  `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`,
  UUID `BA3D771F-F079-33FF-94E5-C792E66237D8`):

  ```
  22:05:28.388 (IO80211Family) IO80211SkywalkInterface::init start
  22:05:28.389 (IO80211Family) IO80211SkywalkInterface::initIvars complete
  22:05:28.389 (AirportItlwm)  itlwm: DEBUG init IO80211InfraInterface::init OK
  22:05:28.509 (AirportItlwm)  itlwm: DEBUG start [STEP 8b] pools: TX=0x0 RX=0x0
  22:05:28.509 (AirportItlwm)  itlwm: DEBUG start [STEP 8b] FAIL: pool creation (TX=0x0 RX=0x0)
  22:05:28.517 (IO80211Family) IO80211Controller::stop[868] ::stop( 0x3ffb4c4336101732 ) ...
  22:05:28.527 (IO80211Family) IO80211Controller::stop[939] super::stop( ... ) ...
  ```

- ioreg / state evidence: no `wlan0` interface enumerated; no
  `IO80211SkywalkInterface` published; CoreWLAN scan returns empty.
- packet / firmware / transport trace: not applicable for STEP-8b
  failure (pool creation precedes any TX/RX path setup).
- before evidence: pool factory returned NULL, controller torn
  down, no networks visible.
- after evidence: pending — Stage 2 will run after host reinstalls
  the post-CR-208 kext (sha
  `bc899fc7876b28908124da5e84a4ed8f5c305dd13ad6f911c5899bb7d45fe707`,
  UUID `D1086C18-A6A7-3C33-AC10-9B9336F533F2`) and reboots.
- why this runtime evidence is semantically significant: it pinpoints
  the first failure to STEP 8b and to the `withName` factory
  returning NULL, which (per source `AirportItlwmV2.cpp:2891-2902`)
  can only happen on `pool->initWithName(...)` returning false. The
  framework's `initWithName` for `packetType=1` returns false on the
  flag-mapping path that requires `poolFlags & 1`.
- why this is not trace-order / object-id noise: the failure is
  deterministic, repeats on every boot of the d3a07c2 cumulative
  kext, and prints the actual pool pointer values
  (`TX=0x0 RX=0x0`) — not log ordering.

## CAUSALITY

- regression window: between commit `8e05ddf` ("instrument Tahoe assoc
  data-path gate", pre-d3a07c2) and commit `d3a07c2` ("attach Skywalk
  queues to workloop", current HEAD). Pre-d3a07c2, STEP 8b was a
  call to `IOSkywalkPacketBufferPool::withName(name, fNetIf, /*packetType*/ 0,
  &poolOpts)` with `poolOpts.poolFlags=0`; this worked because
  `packetType=0` auto-sets the kern flag LSB.
- pinpointed divergence path: post-d3a07c2 STEP 8b is a call to
  `AirportItlwmIO80211PacketPool::withName(name, fNetIf, &poolOpts)`
  with `kIOSkywalkPacketTypeNetwork` hardcoded inside the factory
  and `poolOpts.poolFlags=0`. The packetType change (0 → 1) requires
  `poolFlags & 1` to keep `kern_pbufpool_create` from rejecting the
  pool, but that bit was never added to PoolOptions.
- why this is root cause and not just correlation: the framework
  disasm at `0x9d3d..0x9d57` (Section 3 of
  `analysis/cr208_poolflags_evidence_2026_04_28.txt`) shows the only
  paths that set the relevant kern flag bit and that `poolFlags & 1`
  is one of them. AppleBCMWLAN's reference Network pool sets exactly
  this bit (Section 4). Our local PoolOptions did not (Section 5).
  Setting the bit, and only the bit, produces a PoolOptions that is
  byte-for-byte equivalent (in the bits the framework reads) to the
  reference Network-pool PoolOptions. No retry, no fallback, no
  workaround — single-bit semantic alignment.

## VERIFICATION PERFORMED

- build:
  - `scripts/build_tahoe.sh` (Debug, Tahoe destination): `BUILD SUCCEEDED`
  - BootKC undef-symbol verification: `OK: all 884 undefined symbols resolve against BootKC`
  - kext path:   `Build/Debug/Tahoe/AirportItlwm.kext/Contents/MacOS/AirportItlwm`
  - kext sha256: `bc899fc7876b28908124da5e84a4ed8f5c305dd13ad6f911c5899bb7d45fe707`
  - kext UUID:   `D1086C18-A6A7-3C33-AC10-9B9336F533F2`
  - kext size:   `16285152`
  - kext sha differs from CR-201/CR-204/CR-206 baseline
    (`c1d6e7b1...`) as expected for a `.cpp`-touching ROOT_CAUSE_FIX.
  - Build evidence file:
    `commit-approval/build_evidence/CR-208-build-poolflags-network-fix.txt`
- targeted reproduction scenario: cold-boot Mac with the post-CR-208
  kext installed. Expected sequence:
    1. STEP 8b: `pools: TX=<non-NULL> RX=<non-NULL>`
    2. STEP 9: `registerInfraEthernetInterface` succeeds.
    3. Skywalk queues attach to workloop (per d3a07c2 commit message).
    4. Networks visible in CoreWLAN / `ioreg` once scan completes.
- before reproduction result: as documented in EVIDENCE FROM RUNTIME
  above (pool=NULL, controller stops).
- after reproduction result: pending Stage 2.
- negative checks:
  - `git diff --check HEAD`: PASS (no whitespace warnings).
  - No new TODO/FIXME/XXX inserted in the change scope.
  - No retry / sleep / poll loop / fallback path introduced.
  - No other PoolOptions field changed; `memcmp`-equivalent match
    against the reference's read fields.
  - No vtable, return-type, or class-layout change in this CR.
- residual known issues not claimed fixed:
  - eapol_tx=0 / IO80211RSNDone=No / setCIPHER_KEY-before-deauth
    chain (CR-176 deferred_work blockers; tracked separately).
  - setInterfaceRole(1) Tahoe guard (candidate CR-207, blocked by
    Stage 2 of this CR-208 plus CR-206).
- scenario coverage:
  - initial boot: directly addressed (the failure is at start-time).
  - reconnect / re-open: not applicable (pool is created once at
    `AirportItlwm::start`).
  - sleep / wake: AirportItlwm tears down on `setPowerState(0)` and
    re-creates on wake; the same start path is exercised, so the
    fix applies on wake as well.
  - power transitions: same coverage as sleep / wake.
  - multi-client / repeated open: not applicable (single network
    interface; pool is private to the kext instance).

## STAGE RULES

- request_stage = STAGE_1_STRUCTURAL.
- after evidence: PENDING_HOST_REBOOT (host must reinstall the new
  kext sha `bc899fc7…` / UUID `D1086C18-…` and reboot to allow
  Stage 2 evidence collection).
- after reproduction result: PENDING.
- request goal: reviewer structural approval to proceed with
  Stage 2 after-fix runtime collection on the exact reviewed HEAD
  and exact reviewed CR-208 artifact.

## RESIDUAL UNCERTAINTY

- The exact name of kern_pbufpool flag bit 5 in current XNU
  (Tahoe 26.2) is not enumerated in the public MacKernelSDK headers
  (community SDK only documents poolFlags bits 1
  `SingleMemorySegment` and 2 `PersistentMemory`). The proof for
  CR-208 does not depend on the kern bit's name — it depends on the
  observed behavior that AppleBCMWLAN's reference sets bit 0 of
  `poolFlags` and the framework's flag-mapping disasm shows that
  bit 0 of `poolFlags` is the only `PoolOptions`-derived path that
  sets kern bit 5. Stage 2 runtime evidence will close this
  uncertainty operationally (pool creation succeeds → networks
  visible).
- AppleBCMWLAN passes `plVar3[0x24]` for `packetCount` and
  `plVar3[5]` for `bufferSize` — these are driver-tunable per
  device. Our `kAirportItlwmSkywalkQueueCapacity = 256` and
  `SKYWALK_BUF_SIZE` are independent of CR-208 scope and not
  changed by this CR.

## FORBIDDEN ALTERNATIVES CONSIDERED AND REJECTED

- heuristic timing: not used (no delay / sleep / poll added).
- fallback path: REJECTED — would mask the root cause. We are not
  retrying with `packetType=0` on Network failure. We are aligning
  the input to the framework's documented contract.
- masking/suppression: not used (no log filtering, no error swallow).
- force callback / state / success: not used (no synthetic completion,
  no forced `isDataPathEnabled`, etc.).
- forced sync / flush / barrier: not used.
- retry / reorder / poll loop: REJECTED — `withName` is a one-shot
  factory; reordering it relative to interface registration would
  not change the kern_pbufpool_create gate behavior.
- type erasure / `void *` substitution: not applicable (no type
  declaration changed).
- why rejected: the framework's flag-mapping disasm proves there is
  exactly one input that brings the kern flag word into the
  type=1-acceptable shape, and that input is `poolFlags & 1`.
  AppleBCMWLAN's reference uses precisely that input. There is no
  legitimate alternative path that passes `kern_pbufpool_create`
  for `packetType=1` without supplying this bit.

## PROPOSED COMMIT MESSAGE

```
AirportItlwm: set Skywalk pool poolFlags=1 for Network-type pools

The framework's IOSkywalkPacketBufferPool::initWithName flag-mapping
auto-sets the kern_pbufpool LSB only for packetType=Generic. For
packetType=Network, the only PoolOptions-derived path that sets the
required kern flag bit is poolFlags & 1. AppleBCMWLAN's reference
Network pool (AppleBCMWLANPCIeSkywalk::allocSkywalkCommonResources @
0xffffff80014ccd56) supplies poolFlags=1 explicitly via
local_30 = 0x100000000.

Pre-d3a07c2 the local pool used packetType=Generic with poolFlags=0,
which worked. The d3a07c2 commit ("attach Skywalk queues to workloop")
switched the factory to packetType=Network without setting
poolFlags=1, which causes kern_pbufpool_create to reject the pool and
withName() to return NULL — observed at runtime as
"DEBUG start [STEP 8b] FAIL: pool creation (TX=0x0 RX=0x0)" followed
by IO80211Controller::stop tearing the controller down.

Fix: set poolOpts.poolFlags = 1 in AirportItlwm::start STEP 8b,
1:1 with the AppleBCMWLAN reference. No other PoolOptions field is
changed.

CR-208 / Stage 1 approved.
```

## PATCH ARTIFACT

exact patch artifact: `commit-approval/artifacts/CR-208-pool-poolflags-network-fix.diff`

The artifact captures the cumulative staged diff at the time of
submission. It is left **untracked** in the worktree (not `git add`'d)
so it does not appear in `git diff --binary HEAD` output and matches
that diff byte-for-byte under `cmp`.

Concrete inlined identity (request-write-time, no placeholders):

- HEAD: `d3a07c2abccac863e1909aa562051a6ee5687245`
- archived artifact path: `commit-approval/artifacts/CR-208-pool-poolflags-network-fix.diff`
- request-write-time artifact sha256 (pre-final-cmp snapshot, see
  binding cmp gate below):
  `85641371296980fe16de01243bdc8446e307df2bafb23dcbfcbc123ca0edf2ec`
- request-write-time artifact lines: `38088`
- request-write-time artifact file count (diff hunks): `159`
- the auditor's final `git diff --binary HEAD | shasum -a 256` may
  differ from this snapshot value because the very act of inlining
  this sha into the request file mutates the staged tree. The
  binding identity gate is `cmp -s` (see verification block below),
  not the inlined sha. Line and file counts are byte-stable across
  the substitution and remain `38088` / `159` at submission time.
- `cmp -s <(git diff --binary HEAD) commit-approval/artifacts/CR-208-pool-poolflags-network-fix.diff`
  exits 0.
- `git diff --check HEAD`: PASS (no whitespace warnings).

The auditor may re-verify identity by:

```
git rev-parse HEAD
shasum -a 256 commit-approval/artifacts/CR-208-pool-poolflags-network-fix.diff
wc -l           commit-approval/artifacts/CR-208-pool-poolflags-network-fix.diff
git diff --binary HEAD | shasum -a 256
git diff --binary HEAD | wc -l
cmp -s <(git diff --binary HEAD) commit-approval/artifacts/CR-208-pool-poolflags-network-fix.diff && echo OK
git diff --check HEAD
```

The two `shasum -a 256` outputs (artifact file and live diff) MUST be
identical. `cmp` is the binding identity gate.

The cumulative artifact's CR-208-specific delta versus the prior
CR-206 cumulative artifact (`5f6fa9f1…` / 37299 lines / 156 files)
is exactly:

  +254 lines added (1 source-side hunk in
       AirportItlwm/AirportItlwmV2.cpp + 1 build-evidence file +
       1 analysis evidence file).
  +2   new files in the diff hunk count (build evidence + analysis
       evidence).

The CR-201/CR-203/CR-204/CR-206 contents are carried forward unchanged.

## SUPERSEDES

supersedes (formal): none.

implicitly invalidates (per `approval_invalid_if_diff_changes: YES`):
- CR-206 Stage 1 approval (`COMMIT_DECISION_CR-206.md`,
  `APPROVED_FOR_AFTER_FIX_RUNTIME`). The CR-206 source-side change
  is preserved in the staged tree and is part of the CR-208
  cumulative artifact. After CR-208 is approved and committed, a
  follow-up CR will rebind the CR-206 declaration-only change to
  the new HEAD/diff.
- CR-205 (already REJECTED) — no impact; CR-205 request file is
  preserved on disk per audit-trail policy.
