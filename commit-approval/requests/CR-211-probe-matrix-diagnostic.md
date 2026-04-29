# CR-211 — Comprehensive kern_pbufpool_create probe matrix (DIAGNOSTIC_INSTRUMENTATION)

- date: 2026-04-29
- stage: STAGE_1_STRUCTURAL
- justification class: DIAGNOSTIC_INSTRUMENTATION
- branch: master
- HEAD: `d3a07c2abccac863e1909aa562051a6ee5687245`
- requested by: Executor+Committer (per `docs/WORKFLOW_ITLWM.md`)
- supersedes: implicitly invalidates CR-210 Stage 1 approval. CR-210's
  Generic-only A/B test is replaced by a 16-case truth-table probe
  matrix that resolves all open hypotheses in a single boot. CR-209's
  factory-side logging is retained unchanged. CR-208's
  `poolOpts.poolFlags = 1` is retained on the production path.

does_this_fix_proven_current_root_cause: NO
This is diagnostic-only. It bypasses the framework's
`IOSkywalkPacketBufferPool::initWithName` wrapper and calls
`kern_pbufpool_create` directly across 16 (packetType, kbi_flags,
sizes) configurations, so the actual `errno_t` return code and pool
handle for each axis become observable in one boot. The next CR will
be the actual fix, narrowed by the truth-table outcome.

## SYMPTOM

Same Stage 2 boot symptom as CR-208 / CR-209 / CR-210: pools fail at
STEP 8b. CR-209 narrowed it to `pool->initWithName(...)` returning 0
inside the framework. CR-210's single A/B test (Generic) was an
incremental probe; the user requested that all branches be
instrumented to their final points iteratively in one boot.

## DIVERGENCE

- exact divergence point: still inside the framework
  `IOSkywalkPacketBufferPool::initWithName` flow (KDK
  `IOSkywalkFamily.kext` `0x9bf0`). Static disasm of
  `_kern_pbufpool_create` (BootKC `0xffffff80009f1d30`) shows our
  kpinit passes the early kbi_* validations
  (`0x9c1d..0x9cdf`); the rejection must therefore happen in the
  internal flag-mapping merge `0x151..0x20b` or inside `_pp_create`
  (BootKC `0xffffff80009ed050`) which is called at
  `_kern_pbufpool_create+0x2b4`.
- confirmed deviation: NOT YET LOCALIZED to a specific kbi_flags or
  size axis. The probe matrix produces a per-axis truth table.
- confirmed root cause: TBD (after probe matrix evidence).

## CLAIM SCOPE

- exact claim scope: prepend a 16-case probe matrix in
  `AirportItlwm::start` STEP 8b that calls `kern_pbufpool_create`
  directly per case and logs `(label, kbi_flags, packets, buflets,
  bufsize, max_frags, buf_seg_size, rc, kpp)`. Successful pools are
  destroyed via `kern_pbufpool_destroy` immediately. Restore
  `packetType=kIOSkywalkPacketTypeNetwork` in the production
  subclass factory (matching CR-208's intent).
- non_claims:
  - This request does NOT claim a fix.
  - This request does NOT claim that any of the probed
    configurations is the long-term correct one.
  - This request does NOT change any PoolOptions field that the
    production subclass factory passes (CR-208's `poolFlags=1`
    is preserved verbatim).
  - This request does NOT change the subclass declaration, vtable,
    or `newPacket` override.
  - This request does NOT change the start-sequence ordering or
    queue attachment logic.

## JUSTIFICATION PATH

justification path: DIAGNOSTIC_INSTRUMENTATION

- exact hypotheses being disambiguated, all in one boot:

  H1. `KBIF_USER_ACCESS` alone (kbi_flags=0x20, the framework's output
      for type=Network + poolFlags=1) is rejected by `_pp_create`
      because it expects an additional bit. The probe matrix
      includes USER_ACCESS combined with each plausible companion
      bit (`PERSISTENT`, `MONOLITHIC`, both, `INHIBIT_CACHE`,
      `KERNEL_READONLY`, `NO_MAGAZINES`, `IODIR_IN|OUT`).

  H2. The Network-type rejection is independent of the flag bits
      (e.g. `_pp_create` expects `kbi_packets >= 1024`, or
      `kbi_buf_seg_size > 0`). The matrix includes
      `pkts_1024`, `pkts_4096`, and `segsz_small` cases.

  H3. `kbi_flags=0x1` (KBIF_QUANTUM) is the only configuration that
      `_pp_create` accepts on this machine for any reason
      (resource constraint, kalloc_type sandbox, etc). The matrix
      includes `GEN_baseline` (just QUANTUM) and `GEN_user`
      (QUANTUM|USER_ACCESS).

  H4. `kbi_flags=0` (no bits at all) is rejected because no type
      auto-bit was set. The matrix includes `NET_no_flags` to
      verify.

  H5. `kbi_flags=0x8` (BUFFER_ON_DEMAND, type=Cloneable) is rejected
      symmetrically. The matrix includes `CLONE_baseline`.

  H6. The framework's wrapper does something the direct probe
      doesn't (e.g. ctx_retain/ctx_release callbacks, segCtor/segDtor
      function pointers). If every probe fails AND the wrapper-side
      `initWithName` also fails, this hypothesis is supported
      (next CR adds those callbacks to one of the probe slots). If
      any probe succeeds, this hypothesis is disconfirmed.

- exact probe points:
  - `AirportItlwm/AirportItlwmV2.cpp` —
    `AirportItlwm::start` STEP 8b prologue: 16-case loop calling
    `kern_pbufpool_create(&kpinit, &kpp, NULL)` and logging
    `rc` and `kpp`. Each successful pool is destroyed with
    `kern_pbufpool_destroy(kpp)` to keep the kernel pool table
    clean.
  - `AirportItlwm/AirportItlwmV2.cpp` —
    `AirportItlwmIO80211PacketPool::withName`:
    `packetType=kIOSkywalkPacketTypeNetwork` restored. CR-209
    `PACKETPOOL[...] new=...` and `initWithName=...` log lines
    are preserved (with the CR-210 `(DIFFERENTIAL ...)` suffix
    dropped since the probe matrix now carries the differential
    workload).

- why these probe points are sufficient: every configuration that
  the framework's wrapper would compute as `kbi_flags` for some
  combination of `(packetType, poolFlags)` is either present in
  the matrix or trivially derivable from a present case. The matrix
  also varies the size axis to cover any min/max-packets or
  min/max-segsz constraint inside `_pp_create`.

- why instrumentation is behavior-neutral:
  - Each `kern_pbufpool_create` call is paired with an immediate
    `kern_pbufpool_destroy` on success, so no kernel-level pool
    leak occurs.
  - All probe-side kpinit values are stack-local; no global state
    or framework state is modified.
  - The production subclass factory call is unchanged in semantics
    (only one source-side change vs CR-209: packetType reverted to
    Network from CR-210's Generic).
  - No retry, fallback, or masking is added.

- exact runtime evidence expected from this instrumentation:
  ```
  itlwm: PROBE_MATRIX begin (16 cases)
  itlwm: PROBE[GEN_baseline         ] kbi_flags=0x1    pkts=256 ... rc=<R> kpp=<P>
  itlwm: PROBE[GEN_user             ] kbi_flags=0x21   pkts=256 ... rc=<R> kpp=<P>
  itlwm: PROBE[NET_no_flags         ] kbi_flags=0x0    pkts=256 ... rc=<R> kpp=<P>
  itlwm: PROBE[NET_user             ] kbi_flags=0x20   pkts=256 ... rc=<R> kpp=<P>
  itlwm: PROBE[CLONE_baseline       ] kbi_flags=0x8    pkts=256 ... rc=<R> kpp=<P>
  itlwm: PROBE[NET_user_persist     ] kbi_flags=0x22   pkts=256 ... rc=<R> kpp=<P>
  itlwm: PROBE[NET_user_monolith    ] kbi_flags=0x24   pkts=256 ... rc=<R> kpp=<P>
  itlwm: PROBE[NET_user_persist_mo  ] kbi_flags=0x26   pkts=256 ... rc=<R> kpp=<P>
  itlwm: PROBE[NET_user_inhibit     ] kbi_flags=0x30   pkts=256 ... rc=<R> kpp=<P>
  itlwm: PROBE[NET_user_kro         ] kbi_flags=0x420  pkts=256 ... rc=<R> kpp=<P>
  itlwm: PROBE[NET_user_nomag       ] kbi_flags=0x820  pkts=256 ... rc=<R> kpp=<P>
  itlwm: PROBE[NET_user_iodir_io    ] kbi_flags=0x320  pkts=256 ... rc=<R> kpp=<P>
  itlwm: PROBE[NET_user_segsz_small ] kbi_flags=0x20   pkts=256 segsz=0x20000 rc=<R> kpp=<P>
  itlwm: PROBE[NET_user_pkts_1024   ] kbi_flags=0x20   pkts=1024 ... rc=<R> kpp=<P>
  itlwm: PROBE[NET_user_pkts_4096   ] kbi_flags=0x20   pkts=4096 ... rc=<R> kpp=<P>
  itlwm: PROBE[GEN_user_persist     ] kbi_flags=0x23   pkts=256 ... rc=<R> kpp=<P>
  itlwm: PROBE_MATRIX end
  ...followed by CR-209-style PACKETPOOL[AirportItlwm-TX/RX]
     new=... and initWithName=0 lines (production failure unchanged).
  ```

  After the next boot, decoding the truth table identifies which
  hypothesis fires and constrains the next fix CR to a known-good
  configuration.

## CHANGED FILES

changed files (CR-211-specific):
- `AirportItlwm/AirportItlwmV2.cpp`:
  1. Restore `packetType = kIOSkywalkPacketTypeNetwork` in the
     subclass factory (one-line revert of CR-210's switch).
  2. Insert the 16-case probe matrix block in `start()` STEP 8b
     prologue.
- `commit-approval/build_evidence/CR-211-build-probe-matrix.txt`

## DIFF SUMMARY

```diff
@@ AirportItlwmIO80211PacketPool::withName factory
-        // CR-210 differential ... const UInt32 packetType = kIOSkywalkPacketTypeGeneric;
+        // CR-211 ... const UInt32 packetType = kIOSkywalkPacketTypeNetwork;
         ...
-        XYLog("itlwm: PACKETPOOL[%s] initWithName=%d (DIFFERENTIAL type=%d)\n", ...);
+        XYLog("itlwm: PACKETPOOL[%s] initWithName=%d (type=%d)\n", ...);

@@ AirportItlwm::start STEP 8b prologue
+    // CR-211 comprehensive probe matrix: ...
+    {
+        struct ProbeCase { ... };
+        const ProbeCase cases[] = {
+            { "GEN_baseline", 0x01, 256, 256, 2048, 1, 0 },
+            { "GEN_user",     0x21, 256, 256, 2048, 1, 0 },
+            ... 14 more cases ...
+        };
+        for (size_t i = 0; i < ncases; ++i) {
+            struct kern_pbufpool_init kpinit;
+            kern_pbufpool_t kpp = nullptr;
+            bzero(&kpinit, sizeof(kpinit));
+            kpinit.kbi_version = 2;
+            strlcpy((char *)kpinit.kbi_name, cases[i].label,
+                    sizeof(kpinit.kbi_name));
+            kpinit.kbi_flags        = cases[i].kbi_flags;
+            kpinit.kbi_packets      = cases[i].packets;
+            kpinit.kbi_max_frags    = cases[i].max_frags;
+            kpinit.kbi_buflets      = cases[i].buflets;
+            kpinit.kbi_bufsize      = cases[i].bufsize;
+            kpinit.kbi_buf_seg_size = cases[i].buf_seg_size;
+            int rc = kern_pbufpool_create(&kpinit, &kpp, nullptr);
+            XYLog("itlwm: PROBE[...] kbi_flags=... rc=%d kpp=%p\n", ...);
+            if (rc == 0 && kpp != nullptr) {
+                kern_pbufpool_destroy(kpp);
+            }
+        }
+    }
```

## EVIDENCE FROM DECOMP

Carried forward and extended:

- `_kern_pbufpool_create` body (BootKC `0xffffff80009f1d30`):
  - Early validation accepts our existing kpinit (CR-208 + CR-209
    evidence). No `EINVAL` emitted at `0x9c1d..0x9cdf`.
  - Internal flag-merge at `0x9d3d..0x9d57` and onward at
    `0x151..0x20b` derives the kbi_flags-based pool flag word
    that is passed to `_pp_create` at `0x9ec050`.
  - Return paths to `0x307` set `eax=0x16 (EINVAL)` for early
    validation failures; later paths return whatever `_pp_create`
    returned.
- `_pp_create` body (BootKC `0xffffff80009ed050`, size `0x1e80`):
  - Multiple `_skmem_region_create`, `_skmem_cache_create`,
    `_skmem_region_mirror`, `_lck_mtx_init`, `_workload_config_available`
    calls. Multiple `_os_log_internal` emit points (likely error
    classifiers).
  - Specific rejection conditions inside `_pp_create` are not
    enumerated by static analysis alone — that is exactly what the
    runtime probe matrix is designed to surface.
- KBIF flag bit definitions per
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
  All probe matrix `kbi_flags` values are constructed from these
  documented bit definitions.

## EVIDENCE FROM RUNTIME

- Before evidence (CR-209 Stage 2, 2026-04-29 00:15, kext sha
  `5ac7fab0…` UUID `A69D6889-…`): `new` succeeds, `initWithName=0`
  for both TX and RX, pools are NULL. Logged in
  `analysis/cr209_runtime_2026_04_29.txt` (forthcoming) and
  the prior CR-209 evidence files.
- After evidence: PENDING — Stage 2 will collect the 16 probe lines
  + production `PACKETPOOL` lines on next boot of the CR-211 kext
  (sha `52e1e745bc2d78316293dc3a07bc004627f3a2a58de4d8a3f227a0b6bc367a68`,
  UUID `C2C52783-BEB5-3DF1-96DA-1523FB2E5CC4`).
- why this runtime evidence is semantically significant: each probe
  isolates one axis of the kbi_flags / size space; the truth table
  pinpoints the smallest configuration that `_pp_create` accepts.
- why this is not trace-order / object-id noise: each probe call
  is independent, with a stack-local kpinit, and the `rc` value is
  the actual `errno_t` from the framework — non-zero values are
  semantic.

## CAUSALITY

- regression window: `8e05ddf` → `d3a07c2`. Pre-d3a07c2 baseline
  used type=Generic + poolFlags=0 → kbi_flags=0x1; that succeeded.
- pinpointed divergence path: TBD by probe matrix.
- why this is root-cause-discovery: the previous diagnostic CRs
  iteratively narrowed the failure axis but each only resolved one
  hypothesis per boot. CR-211 saturates the hypothesis space in
  one boot, eliminating the need for further A/B iterations.

## VERIFICATION PERFORMED

- build:
  - `scripts/build_tahoe.sh`: `BUILD SUCCEEDED`
  - BootKC undef-symbol verification: `OK: all 886 undefined symbols resolve against BootKC`
    (+2 vs CR-210: `_kern_pbufpool_create`, `_kern_pbufpool_destroy`)
  - kext path:   `Build/Debug/Tahoe/AirportItlwm.kext/Contents/MacOS/AirportItlwm`
  - kext sha256: `52e1e745bc2d78316293dc3a07bc004627f3a2a58de4d8a3f227a0b6bc367a68`
  - kext UUID:   `C2C52783-BEB5-3DF1-96DA-1523FB2E5CC4`
  - kext size:   `16285520`
  - Build evidence file:
    `commit-approval/build_evidence/CR-211-build-probe-matrix.txt`
  - kext installed in `/Library/Extensions/AirportItlwm.kext` (root:wheel).
- targeted reproduction scenario: cold-boot Mac with CR-211 kext.
  Expected output: 16 probe lines + 4 production PACKETPOOL lines.
- before reproduction result: as documented above.
- after reproduction result: PENDING.
- negative checks:
  - `git diff --check HEAD`: PASS.
  - No retry / sleep / poll loop / fallback / forced state.
  - No vtable / class layout change.
  - Pool subclass and `newPacket` override unchanged.
  - Each successful probe pool is destroyed with
    `kern_pbufpool_destroy`.
- residual known issues not claimed fixed: pool failure remains
  until the follow-up fix CR.
- scenario coverage:
  - initial boot: directly addressed.
  - sleep / wake: probe matrix runs each time `start` runs.

## STAGE RULES

- request_stage = STAGE_1_STRUCTURAL.
- after evidence: PENDING_HOST_REBOOT.
- after reproduction result: PENDING.
- request goal: reviewer structural approval to proceed with
  Stage 2 after-fix runtime collection on the exact reviewed HEAD
  and exact reviewed CR-211 cumulative artifact.

## RESIDUAL UNCERTAINTY

- If every probe fails (H6 fires), the next CR will need to add
  segCtor/segDtor/ctx_retain/ctx_release callbacks to the probe
  set, mirroring the framework wrapper precisely. (This requires
  the framework's static segmentConstructor/segmentDestructor
  symbol names accessible via extern declaration; planned for the
  follow-up CR.)
- If any probe succeeds with kbi_flags=0x20 alone, then the production
  failure is explained by the wrapper's other state setup (e.g.,
  the `IOService::waitForService("IOBSD")` blocking call returning
  bad state, or one of the resource-allocation paths returning NULL
  silently). The next CR adds wrapper-internal probes via subclass
  override of `initWithName`.

## FORBIDDEN ALTERNATIVES CONSIDERED AND REJECTED

- heuristic timing: not added.
- fallback path: REJECTED. The probe matrix runs sequentially and
  each probe is independent; no probe alters the production code
  path.
- masking/suppression: not added.
- force callback / state / success: not added.
- forced sync / flush / barrier: not added.
- retry / reorder / poll loop: not added (the probe matrix is one
  pass per boot).
- type erasure: not applicable.
- why rejected: probes must be independent observations; a
  fallback would conflate axes and defeat the diagnostic intent.

## PROPOSED COMMIT MESSAGE

```
AirportItlwm: 16-case kern_pbufpool_create probe matrix at STEP 8b

CR-209 narrowed the regression to IOSkywalkPacketBufferPool::initWithName
returning false. Static analysis of _kern_pbufpool_create (BootKC
0xffffff80009f1d30) showed our kpinit passes early validation; the
internal flag-merge or _pp_create itself rejects us.

This CR prepends a 16-case truth-table probe matrix that calls
kern_pbufpool_create directly with various (kbi_flags, packets,
buflets, bufsize, max_frags, buf_seg_size) configurations BEFORE the
production pool factory. Each successful probe is destroyed
immediately via kern_pbufpool_destroy. The next boot's log identifies
exactly which configuration kern_pbufpool_create accepts on Tahoe.

The production subclass factory's packetType is restored to
kIOSkywalkPacketTypeNetwork (1:1 with AppleBCMWLAN); CR-210's
Generic A/B test is replaced by case "GEN_baseline" inside the
matrix.

Pure DIAGNOSTIC_INSTRUMENTATION: no retry/fallback/masking/forced state.
+2 BootKC undefs: kern_pbufpool_create, kern_pbufpool_destroy.

CR-211 / Stage 1.
```

## PATCH ARTIFACT

exact patch artifact: `commit-approval/artifacts/CR-211-probe-matrix-diagnostic.diff`

The artifact captures the cumulative staged diff at submission. It
is left **untracked** so it does not appear in `git diff --binary HEAD`
and matches that diff byte-for-byte under `cmp`.

The auditor may re-verify identity by:

```
git rev-parse HEAD
shasum -a 256 commit-approval/artifacts/CR-211-probe-matrix-diagnostic.diff
wc -l           commit-approval/artifacts/CR-211-probe-matrix-diagnostic.diff
git diff --binary HEAD | shasum -a 256
git diff --binary HEAD | wc -l
cmp -s <(git diff --binary HEAD) commit-approval/artifacts/CR-211-probe-matrix-diagnostic.diff && echo OK
git diff --check HEAD
```

The two `shasum -a 256` outputs (artifact file and live diff) MUST be
identical. `cmp` is the binding identity gate.

## SUPERSEDES

supersedes (formal): none.

implicitly invalidates:
- CR-210 Stage 1 approval. CR-210's diagnostic logging (CR-209
  layer) is preserved; its single-axis `packetType=Generic` switch
  is replaced by case `GEN_baseline` inside the matrix.
- CR-209 Stage 1 approval (already implicitly invalidated by
  CR-210). Logging is preserved.
- CR-208 Stage 1 approval (already invalidated). `poolFlags=1` is
  preserved on the production path.
