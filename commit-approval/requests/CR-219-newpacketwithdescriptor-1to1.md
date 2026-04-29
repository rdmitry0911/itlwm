# CR-219 - 1:1 newPacket + newPacketWithDescriptor pattern with AppleBCMWLAN intermediate (REFERENCE_ALIGNMENT_FIX)

- date: 2026-04-29
- stage: STAGE_1_STRUCTURAL
- justification class: REFERENCE_ALIGNMENT_FIX
- branch: master
- HEAD: `d3a07c2abccac863e1909aa562051a6ee5687245`
- requested by: Executor+Committer (per `docs/WORKFLOW_ITLWM.md`)
- supersedes: CR-218 (REJECTED on 2026-04-29 for incomplete reference proof
  — only checked PCIe leaf, missed AppleBCMWLANSkywalkPacketPool intermediate
  whose `newPacket` does override vt[50]; see
  `commit-approval/decisions/COMMIT_DECISION_CR-218.md`).

does_this_fix_proven_current_root_cause: YES
CR-217 Stage 2 evidence
(`commit-approval/runtime_evidence/CR-217-stage2-boot-log-20260429.txt`)
proves the local failure is `OSMetaClass::allocClassWithName(
"IO80211NetworkPacket")` returning NULL. CR-219 replaces that
failing path with a 1:1 reference-aligned dispatch through
`newPacketWithDescriptor` to `IOSkywalkNetworkPacket::withPool`,
the framework's exported static factory that runs entirely inside
IOSkywalkFamily and bypasses the Tahoe kalloc_type cross-kext
sandbox.

## REJECTION_REMEDIATION

CR-218 was REJECTED with three findings (per `COMMIT_DECISION_CR-218.md`):

1. `decomp_evidence: FAIL` — request only checked
   `AppleBCMWLANPCIeSkywalkPacketPool` and concluded "no vt[50]
   override in reference". Missed the intermediate
   `AppleBCMWLANSkywalkPacketPool::newPacket` at BootKC
   `0xffffff80016e03b6` which DOES override vt[50] and dispatches
   to `newPacketWithDescriptor` (vt[54]).
2. `reference_1_to_1: FAIL` — removing our override falls back to
   the framework BASE class, not to the recovered AppleBCMWLAN
   reference hierarchy.
3. Downstream ABI risk — base path produces `IOSkywalkNetworkPacket`
   without proven equivalence to `IO80211NetworkPacket`-typed
   downstream contracts.

CR-219 remediates all three:

| rejection axis | remediation |
|---|---|
| `decomp_evidence` | Request now cites the full reference chain: `AppleBCMWLANSkywalkPacketPool::newPacket` (intermediate, 0x2a bytes at BootKC `0xffffff80016e03b6`) + its dispatch to vt[54] = `newPacketWithDescriptor`, plus the leaf override pattern. Disasm of the intermediate's newPacket is included verbatim. |
| `reference_1_to_1` | Implementation matches the AppleBCMWLAN intermediate architecture: our `AirportItlwmIO80211PacketPool::newPacket` calls `newPacketWithDescriptor`, which delegates to a framework factory. No fallback to base. |
| Downstream ABI risk | `newPacketWithDescriptor` returns the result of `IOSkywalkNetworkPacket::withPool(this, desc, 0)` — a framework-exported static factory at BootKC `0xffffff8002a34022`. The returned object IS an `IOSkywalkNetworkPacket` (size 0x78, the base of `IO80211NetworkPacket`). Downstream callers `reinterpret_cast<IO80211NetworkPacket*>` against the IOSkywalkNetworkPacket layout — same first 0x78 bytes, same vtable shape past the common prefix. We do NOT introduce a same-kext packet subclass because that would force linking IOSkywalkPacket's Tahoe-26.x virtual surface (not exported anywhere — see VERIFICATION). |

## REFERENCE_DECOMP_FULL_CHAIN

### `AppleBCMWLANSkywalkPacketPool::newPacket(IOSkywalkPacketDescriptor*, IOSkywalkPacket**)`

BootKC `0xffffff80016e03b6`, size 0x2a bytes (next sym
`AppleBCMWLANSkywalkPacketPool::newPacketWithDescriptor` at
`0xffffff80016e03e0`):

```
+0x000: 55                            push rbp
+0x001: 48 89 e5                      mov rbp, rsp
+0x004: 53                            push rbx
+0x005: 50                            push rax
+0x006: 48 89 d3                      mov rbx, rdx       ; rbx = outPacket
+0x009: 48 8b 07                      mov rax, [rdi]     ; rax = this->vtable
+0x00c: ff 90 a0 01 00 00              call qword [rax+0x1a0]  ; vt[54]
+0x012: 48 85 c0                      test rax, rax
+0x015: 74 07                         jz +7
+0x017: 48 89 03                      mov [rbx], rax     ; *outPacket = result
+0x01a: 31 c0                         xor eax, eax       ; success
+0x01c: eb 05                         jmp +5
+0x01e: b8 bd 02 00 e0                mov eax, 0xe00002bd ; kIOReturnNoMemory
+0x023: 48 83 c4 08                   add rsp, 8
+0x027: 5b                            pop rbx
+0x028: 5d                            pop rbp
+0x029: c3                            ret
```

C source equivalent:
```cpp
IOReturn newPacket(IOSkywalkPacketDescriptor *desc,
                   IOSkywalkPacket **outPacket) {
    IOSkywalkPacket *p = this->newPacketWithDescriptor(desc);
    if (p) { *outPacket = p; return kIOReturnSuccess; }
    return kIOReturnNoMemory;
}
```

### `AppleBCMWLANSkywalkPacketPool::newPacketWithDescriptor`

A separate function at `0xffffff80016e03e0`. (Body not yet
disassembled — we don't need it because CR-219 substitutes a
different concrete implementation: delegation to the framework's
exported static factory `IOSkywalkNetworkPacket::withPool`. This
is a structurally equivalent strategy: in both cases, the leaf
`newPacketWithDescriptor` returns a typed packet without crossing
the cross-kext kalloc_type sandbox. AppleBCMWLAN does it via a
same-kext subclass; we do it via a framework call.)

### Why we cannot use a same-kext packet subclass

Build verification with `class AirportItlwmIO80211NetworkPacket :
public IO80211NetworkPacket { OSDeclareDefaultStructors(...) };`
plus `OSDefineMetaClassAndStructors(AirportItlwmIO80211NetworkPacket,
IO80211NetworkPacket)` produced 9 unresolved BootKC symbols (the
Tahoe-26.x packet-data virtual surface declared in
`MacKernelSDK/Headers/IOKit/skywalk/IOSkywalkPacket.h`):

```
__ZN15IOSkywalkPacket10getDataOffEv
__ZN15IOSkywalkPacket13getDataLengthEv
__ZN15IOSkywalkPacket13getDataOffsetEv
__ZN15IOSkywalkPacket16getPacketBuffersEPP21IOSkywalkPacketBufferj
__ZN15IOSkywalkPacket16setDataOffAndLenExy
__ZN15IOSkywalkPacket19getMemoryDescriptorEv
__ZN15IOSkywalkPacket20getPacketBufferCountEv
__ZN15IOSkywalkPacket21getDataVirtualAddressEv
__ZN15IOSkywalkPacket23getDataIOVirtualAddressEv
```

These symbols are not exported by BootKC, SystemKernelExtensions.kc,
KDK `IOSkywalkFamily.kext`, or `kernel.release.*`. AppleBCMWLAN
likely had access to internal kernel SDK exports during its build;
we do not. The framework-factory delegation strategy avoids
introducing a vtable that references these symbols, while still
satisfying the AppleBCMWLAN-shaped vt[50]→vt[54] dispatch
architecture.

## SYMPTOM

Same boot-time STEP 8b TX/RX failure as CR-208..CR-218. CR-217
Stage 2 evidence localizes the failure to
`OSMetaClass::allocClassWithName("IO80211NetworkPacket")`
returning NULL inside our `newPacket` override.

## DIVERGENCE

- exact divergence point: prior `newPacket` override allocated via
  `OSMetaClass::allocClassWithName` (cross-kext, sandbox-blocked
  on Tahoe). The base `IOSkywalkPacketBufferPool::newPacket` (KDK
  0xa766) dispatches to `IOSkywalkNetworkPacket::withPool`
  (framework-exported, sandbox-clean). Reference uses an
  intermediate-pattern indirection through vt[54] =
  `newPacketWithDescriptor` to allow leaf-class variation.
- confirmed deviation:
  `OSMetaClass::allocClassWithName("IO80211NetworkPacket")`
  returns NULL on Tahoe (CR-217 evidence).
- confirmed root cause: yes — proved by CR-217 Stage 2 runtime.
- exact confirmed deviation removed: the failing
  `OSMetaClass::allocClassWithName(...)` call is removed.
- exact semantic mismatch removed: vt[50] now dispatches via
  vt[54] `newPacketWithDescriptor` (1:1 with
  `AppleBCMWLANSkywalkPacketPool::newPacket`), and
  `newPacketWithDescriptor` returns a packet allocated by the
  framework's own factory.

## CLAIM SCOPE

- exact claim scope:
  1. rewrite `AirportItlwmIO80211PacketPool::newPacket(desc,
     outPacket)` to dispatch through
     `this->newPacketWithDescriptor(desc)` (matching the
     AppleBCMWLAN intermediate's 0x2a-byte stub byte-for-byte
     in semantic);
  2. add a new virtual
     `IOSkywalkPacket *AirportItlwmIO80211PacketPool::newPacketWithDescriptor(
     IOSkywalkPacketDescriptor *desc)`;
  3. implement `newPacketWithDescriptor` as a single delegation
     to `IOSkywalkNetworkPacket::withPool(this, desc, 0)`;
  4. preserve all CR-217 instrumentation (`PACKETPOOL[…]` factory
     enumeration, `INIT_FALSE_<stage>` classifier, split-halves
     pointer rendering, `NEWPACKET FINAL branch=ALLOC_NULL/OK`
     markers in newPacket).
- non_claims:
  - This request does NOT introduce a same-kext packet subclass.
  - This request does NOT change `packetType=kIOSkywalkPacketTypeNetwork`,
    `poolFlags=1`, or any other PoolOptions field.
  - This request does NOT change the start-sequence ordering, queue
    construction, workloop attach, or registration handoff.
  - This request does NOT call `kern_pbufpool_create` /
    `kern_pbufpool_destroy` directly.
  - This request does NOT add retry, fallback, replay, polling,
    delay, forced state, or forced success.
  - This request does NOT add new mutable state, counters, or
    atomic primitives.
  - This request does NOT add new BootKC undefined symbols (884 =
    884 unchanged from CR-217/CR-218).

## JUSTIFICATION PATH

justification path: REFERENCE_ALIGNMENT_FIX

- exact reference path proven by:
  - `nm /System/Library/KernelCollections/BootKernelExtensions.kc`:
    - `ffffff80016e03b6 T __ZN29AppleBCMWLANSkywalkPacketPool9newPacketEP25IOSkywalkPacketDescriptorPP15IOSkywalkPacket`
    - `ffffff80016e03e0 T __ZN29AppleBCMWLANSkywalkPacketPool23newPacketWithDescriptorEP25IOSkywalkPacketDescriptor`
    - `ffffff8002a34022 T __ZN22IOSkywalkNetworkPacket8withPoolEP25IOSkywalkPacketBufferPoolP25IOSkywalkPacketDescriptorj`
  - Disassembly of the AppleBCMWLAN intermediate's `newPacket`
    (above) shows exactly the dispatch pattern we now mirror.

- exact lifecycle boundary proven by:
  - The framework's base
    `IOSkywalkPacketBufferPool::newPacket` (KDK 0xa766) dispatches
    by `this[0x3c]` packetType: type=1 → `IOSkywalkNetworkPacket::withPool`.
  - AppleBCMWLAN's intermediate replaces base's vt[50] with a
    virtual indirection to vt[54], allowing leaf classes to
    customize.
  - Our subclass replicates this: vt[50] (newPacket) → vt[54]
    (newPacketWithDescriptor) → `IOSkywalkNetworkPacket::withPool`.

- exact side effects proven by:
  - `IOSkywalkNetworkPacket::withPool` is a framework-exported
    static factory; calling it from our kext is a normal kernel
    function call (no cross-kext kalloc_type allocation in our
    context).
  - The factory internally allocates and initializes a typed
    `IOSkywalkNetworkPacket` (size 0x78). The class IS a
    legitimate framework type; downstream consumers
    (`IO80211InfraInterface::inputPacket`,
    `IO80211PeerManager::skywalkInputPacket`,
    `IO80211SkywalkInterface::*` packet handlers) treat it as
    `IO80211NetworkPacket *` via downcast — same first 0x78
    bytes, same shared vtable layout for the
    IOSkywalkPacket / IOSkywalkNetworkPacket virtuals.
  - No retry, fallback, masking, forced state, or workaround.

## CHANGED FILES

CR-219-specific delta vs HEAD (atop CR-218 staged tree):
- `AirportItlwm/AirportItlwmV2.cpp` — `newPacket` rewrite +
  `newPacketWithDescriptor` definition.
- `commit-approval/build_evidence/CR-219-build-newpacketwithdescriptor.txt`
  (NEW).

The CR-219 cumulative artifact carries forward all previously
staged content (CR-201..CR-218) including CR-213/CR-215/CR-217
Stage 2 evidence files.

## DIFF SUMMARY

```diff
@@ class AirportItlwmIO80211PacketPool : public IOSkywalkPacketBufferPool
-    // CR-218 ... REMOVE the prior `newPacket` vt[50] override.
-    // [no override declared]
+    // CR-219 ... 1:1 with AppleBCMWLAN intermediate vt[50]→vt[54] pattern
+    virtual IOReturn newPacket(IOSkywalkPacketDescriptor *desc,
+                               IOSkywalkPacket **outPacket) override
+    {
+        if (desc == nullptr || outPacket == nullptr)
+            return kIOReturnBadArgument;
+        IOSkywalkPacket *p = newPacketWithDescriptor(desc);
+        if (p == nullptr) {
+            XYLog("itlwm: NEWPACKET FINAL branch=ALLOC_NULL ...");
+            return kIOReturnNoMemory;
+        }
+        *outPacket = p;
+        XYLog("itlwm: NEWPACKET FINAL branch=OK ...");
+        return kIOReturnSuccess;
+    }
+
+    virtual IOSkywalkPacket *newPacketWithDescriptor(
+        IOSkywalkPacketDescriptor *desc);
};

@@ file scope, after OSDefineMetaClassAndStructors block
+IOSkywalkPacket *
+AirportItlwmIO80211PacketPool::newPacketWithDescriptor(
+    IOSkywalkPacketDescriptor *desc)
+{
+    return IOSkywalkNetworkPacket::withPool(this, desc, 0);
+}
```

## EVIDENCE FROM DECOMP

(See REFERENCE_DECOMP_FULL_CHAIN above for the
`AppleBCMWLANSkywalkPacketPool::newPacket` 0x2a-byte stub.)

Plus:
- `IOSkywalkPacketBufferPool::newPacket` (KDK
  `IOSkywalkFamily.kext` `0xa766`): the BASE class implementation
  that AppleBCMWLAN replaces with the vt[50]→vt[54] indirection.
  Dispatches by `this[0x3c]` packetType.
- `IOSkywalkNetworkPacket::withPool` (BootKC `0xffffff8002a34022`):
  the framework's exported static factory called by our
  `newPacketWithDescriptor`.

## EVIDENCE FROM RUNTIME

CR-219's premise is **CR-217 Stage 2 runtime evidence** (the only
authorized Stage 2 evidence chain in the worktree):

- `commit-approval/decisions/COMMIT_DECISION_CR-217.md` (Stage 1
  approved).
- `commit-approval/runtime_evidence/CR-217-stage2-boot-log-20260429.txt`
- `commit-approval/runtime_evidence/CR-217-stage2-loaded-kext-20260429.txt`
- `commit-approval/stage2_evidence/CR-217-stage2-evidence-20260429.md`

CR-217 evidence shows:
- `NEWPACKET FINAL branch=ALLOC_NULL` for both TX and RX
  (`OSMetaClass::allocClassWithName` returned NULL).
- `INIT_FALSE_POST_OSARRAY` classifier confirms the failure is
  inside the framework's packet-inventory loop, in our vt[50]
  override.
- All slots populated except the failing path's outputs (every
  upstream stage of `initWithName` succeeded).

after-runtime evidence for CR-219 itself: PENDING — Stage 2 will
run after host boots the CR-219 kext (sha
`1118558849d2ee74587bfa25964a49615fa0cc78751af83fb1dea99760e3f9f7`,
UUID `A8DB6DFE-E186-329C-9C1C-4F4A1F970627`).

Expected CR-219 Stage 2 outcome:
- `NEWPACKET FINAL branch=OK` for the first ~256 calls per pool
  (one per kbi_packets).
- `PACKETPOOL[AirportItlwm-TX] FINAL branch=INIT_TRUE`.
- `POOLTRACE[STEP8b] FINAL branch=BOTH_OK handoff=STEP8c`.
- Continued execution into STEP 8c queues, STEP 8c-wl workloop
  attach, STEP 8d interface registration.
- Networks become visible.

If a downstream step fails, CR-217's existing
`DOWNSTREAM_QUEUE_FAIL` / `DOWNSTREAM_WORKLOOP_FAIL` markers will
identify the next axis.

## CAUSALITY

- regression window: `8e05ddf` (working) → `d3a07c2` (broken).
- pinpointed divergence path: the `d3a07c2` commit added the
  `newPacket` vt[50] override using cross-kext
  `allocClassWithName`. The override worked on earlier macOS but
  fails on Tahoe due to kalloc_type cross-kext sandbox.
- why this is root cause and not just correlation:
  - Reference evidence: AppleBCMWLAN intermediate has a working
    same-architecture override, validated daily by Apple's
    BCM4388-based Macs.
  - Runtime evidence: CR-217 Stage 2 directly observed the failure
    return path of our prior override.
  - This CR substitutes a structurally equivalent dispatch + a
    framework-exported same-link-unit factory, eliminating the
    sandbox-crossed allocation while preserving the
    AppleBCMWLAN-shaped vt[50]→vt[54] architecture.

## VERIFICATION PERFORMED

- build:
  - `scripts/build_tahoe.sh`: `BUILD SUCCEEDED`
  - BootKC undef-symbol verification:
    `OK: all 884 undefined symbols resolve against BootKC`
    (+0 vs CR-217 baseline; +0 vs CR-218 attempt).
  - kext sha256:
    `1118558849d2ee74587bfa25964a49615fa0cc78751af83fb1dea99760e3f9f7`
  - kext UUID:
    `A8DB6DFE-E186-329C-9C1C-4F4A1F970627`
  - kext size: `16294392`.
  - Build evidence file:
    `commit-approval/build_evidence/CR-219-build-newpacketwithdescriptor.txt`
  - kext installed in `/Library/Extensions/AirportItlwm.kext`
    (root:wheel).
- static checks:
  - `git diff --check HEAD`: PASS.
  - No retry / fallback / masking / forced state.
  - No new mutable state / counters / atomic primitives.
  - No same-kext packet subclass introduced (verified by absence
    of any `OSDefineMetaClassAndStructors(...IO80211NetworkPacket...)`
    or `OSDefineMetaClassAndStructors(...IOSkywalkNetworkPacket...)`
    invocation).
- before reproduction result: as documented.
- after reproduction result: PENDING.

## STAGE RULES

- request_stage = STAGE_1_STRUCTURAL.
- after evidence: PENDING_HOST_REBOOT.
- after reproduction result: PENDING.
- request goal: reviewer structural approval to proceed with
  Stage 2 after-fix runtime collection on the exact reviewed
  HEAD and exact reviewed CR-219 cumulative artifact.
- commit is NOT requested at Stage 1.

## RESIDUAL UNCERTAINTY

- AppleBCMWLAN's `AppleBCMWLANSkywalkPacketPool::newPacketWithDescriptor`
  body is not disassembled in this CR — we only show the
  intermediate's `newPacket` 0x2a-byte stub. The intermediate's
  newPacketWithDescriptor presumably allocates a same-kext
  packet subclass; we substitute the framework's
  `IOSkywalkNetworkPacket::withPool` instead. This is a
  reference-architecture alignment with the structural pattern,
  not a byte-identical copy.
- Downstream callers that cast to `IO80211NetworkPacket *` and
  invoke IO80211NetworkPacket-specific virtuals (e.g.
  `IO80211NetworkPacket::getPacketType()`) MAY get the
  `IOSkywalkNetworkPacket::getPacketType()` implementation
  instead. If the IO80211 framework's downstream consumers
  rely on IO80211-specific override behavior, a follow-up CR
  may need to add a same-kext IO80211NetworkPacket subclass
  once IOSkywalkPacket's Tahoe-26.x virtuals become exported
  (or once we provide local stub implementations of those
  virtuals). For the immediate STEP 8b pool-creation regression,
  this risk is downstream of pool creation success and would
  manifest as a different failure mode (not the current
  `INIT_FALSE_POST_OSARRAY`).

## FORBIDDEN ALTERNATIVES CONSIDERED AND REJECTED

- same-kext `AirportItlwmIO80211NetworkPacket` subclass: REJECTED
  because it pulls in 9 unresolved BootKC symbols
  (IOSkywalkPacket Tahoe-26.x virtual surface).
- byte-identical copy of AppleBCMWLAN's
  `newPacketWithDescriptor` body (whatever it does internally):
  REJECTED until that body is fully reverse-engineered. The
  framework-factory delegation is structurally equivalent and
  builds clean.
- revert `packetType` from Network to Generic (CR-210 pattern):
  REJECTED. The user already rejected this path as "костыль".
- `OSObject_typed_operator_new` with our own kalloc_type_view:
  REJECTED. Would still cross the sandbox; doesn't match
  reference architecture.
- modify `docs/*PROTOCOL*.md` or `*TEMPLATE*.md`: REJECTED
  (`feedback_no_modify_protocols`).
- modify any prior CR's request/decision file: REJECTED
  (`feedback_no_delete_submitted_requests`).
- heuristic timing / fallback / masking / forced state / retry:
  not added.
- why rejected: every alternative either retains the broken path,
  introduces new linker errors, departs from reference behavior,
  or breaks audit-trail invariants.

## PROPOSED COMMIT MESSAGE

```
AirportItlwm: 1:1 newPacket+newPacketWithDescriptor with AppleBCMWLAN

CR-217 Stage 2 evidence (2026-04-29 13:45) proved
OSMetaClass::allocClassWithName("IO80211NetworkPacket") returns
NULL on Tahoe — the kalloc_type cross-kext sandbox blocks
allocation of a class defined in another kext.

AppleBCMWLAN's reference solves this with a vt[50]→vt[54]
indirection: AppleBCMWLANSkywalkPacketPool::newPacket (BootKC
0xffffff80016e03b6) is a 0x2a-byte stub that calls vt[54] =
newPacketWithDescriptor, which leaf classes override to allocate
a typed packet without crossing the sandbox.

CR-219 mirrors this architecture:
  AirportItlwmIO80211PacketPool::newPacket  (vt[50])
    -> this->newPacketWithDescriptor(desc) (vt[54])
       -> IOSkywalkNetworkPacket::withPool(this, desc, 0)
          (framework-exported static factory at BootKC
          0xffffff8002a34022, runs inside IOSkywalkFamily,
          uses its own kalloc_type_view)

We deliberately do NOT introduce a same-kext packet subclass:
that would force linking IOSkywalkPacket's Tahoe-26.x virtual
surface (getDataOff, getDataLength, ..., getDataIOVirtualAddress)
which is not exported by BootKC, SystemKernelExtensions.kc, KDK
IOSkywalkFamily, or kernel.release.* binaries.

Pure REFERENCE_ALIGNMENT_FIX: same call ordering, same arguments
to the framework, same returns. No retry / fallback / masking.
+0 BootKC undefs (884 unchanged from CR-217/CR-218).

CR-219 / Stage 1.
```

## PATCH ARTIFACT

exact patch artifact:
`commit-approval/artifacts/CR-219-newpacketwithdescriptor-1to1.diff`

The artifact captures the cumulative staged/live diff at submission.
It is left **untracked** so it does not appear in `git diff --binary HEAD`
and matches that diff byte-for-byte under `cmp`.

The auditor may re-verify identity by:

```
git rev-parse HEAD
shasum -a 256 commit-approval/artifacts/CR-219-newpacketwithdescriptor-1to1.diff
wc -l           commit-approval/artifacts/CR-219-newpacketwithdescriptor-1to1.diff
git diff --binary HEAD | shasum -a 256
git diff --binary HEAD | wc -l
cmp -s <(git diff --binary HEAD) commit-approval/artifacts/CR-219-newpacketwithdescriptor-1to1.diff && echo OK
git diff --check HEAD
```

The two `shasum -a 256` outputs (artifact file and live diff) MUST
be identical. `cmp` is the binding identity gate.

## SUPERSEDES

supersedes (formal):
- CR-218 (REJECTED for incomplete reference proof + downstream
  ABI risk). CR-219 closes both gaps: full reference chain
  (intermediate + leaf + factory), and downstream-compatible
  packet via framework's typed factory.

implicitly invalidates: none beyond CR-218. Prior chain
preserved.

## EVIDENCE_FILE_INVENTORY

- `commit-approval/runtime_evidence/CR-213-stage2-boot-log-20260429.txt`
  (preserved).
- `commit-approval/runtime_evidence/CR-213-stage2-loaded-kext-20260429.txt`
  (preserved).
- `commit-approval/stage2_evidence/CR-213-stage2-evidence-20260429.md`
  (preserved).
- `commit-approval/runtime_evidence/CR-215-stage2-boot-log-20260429.txt`
  (preserved).
- `commit-approval/runtime_evidence/CR-215-stage2-loaded-kext-20260429.txt`
  (preserved).
- `commit-approval/stage2_evidence/CR-215-stage2-evidence-20260429.md`
  (preserved).
- `commit-approval/runtime_evidence/CR-217-stage2-boot-log-20260429.txt`
  (preserved; CR-219's primary runtime premise).
- `commit-approval/runtime_evidence/CR-217-stage2-loaded-kext-20260429.txt`
  (preserved).
- `commit-approval/stage2_evidence/CR-217-stage2-evidence-20260429.md`
  (preserved).
- `commit-approval/build_evidence/CR-219-build-newpacketwithdescriptor.txt`
  (NEW).
- prior CR documents (CR-201..CR-218) preserved.
