# COMMIT REQUEST CR-222 — ACTIVE_ALLOCATION_PROBE

request_id: CR-222
review_stage: STAGE_1_STRUCTURAL
proposed_change_class: **ACTIVE_ALLOCATION_PROBE** (new explicit class, not
DIAGNOSTIC_INSTRUMENTATION; introduced per CR-221 reviewer's REQUIRED_CHANGES
bullet 2: "If active allocation probes are required, submit them under a
different explicit experimental/probe class …")
authorization_request: this CR explicitly requests Stage 1 authorization for
the non-passive side effects enumerated in SIDE_EFFECTS, FAILURE_SAFETY,
PERTURBATION_ANALYSIS below, BEFORE Stage 2 runtime evidence is collected.
parent_anomaly: STEP 8b TX/RX packet pool creation failure
runtime_basis: CR-217 Stage 2 evidence (NEWPACKET FINAL branch=ALLOC_NULL,
ret=0xe00002bd) — current root-cause is opaque: `OSMetaClass::allocClassWithName(
"IO80211NetworkPacket")` returns NULL, but it is unknown whether the failure
is at name lookup, at metaclass-`alloc()`, at the kalloc_type sandbox, or
some Tahoe-26.x gating new in this kernel build.
prior_rejections:
  - CR-218 / CR-219 / CR-220 — REJECTED, see decisions for details.
  - CR-221 (DIAGNOSTIC_INSTRUMENTATION) — REJECTED on:
    - diagnostic_neutrality FAIL: active allocations under the
      DIAGNOSTIC_INSTRUMENTATION class label.
    - workaround_hunt FAIL / best-effort: returning CR-220's `withPool` as
      the upstream packet preserved a rejected behavioral substitute.
    - claim_scope FAIL: in-source comment claimed "first non-NULL" return
      while code actually returned only `IOSkywalkNetworkPacket::withPool`.

reviewed_head_commit: d3a07c2abccac863e1909aa562051a6ee5687245
artifact: commit-approval/artifacts/CR-222-active-allocation-probe.diff
build_evidence: commit-approval/build_evidence/CR-222-build-active-allocation-probe.txt
kext_sha256: d2a052d59ebc41d554900109b5ab91bbdd94c90bf8298492f52fba38af762507
kext_uuid: B319AED0-95DF-30EC-93CC-227A44725AA7
kext_size: 16298920
bootkc_undef: 887

## CHANGES_VS_CR-221

CR-222 is the smallest possible CR-221 fix per the CR-221 reviewer's three
REQUIRED_CHANGES bullets:

1. **Class label**: changed from `DIAGNOSTIC_INSTRUMENTATION` to
   `ACTIVE_ALLOCATION_PROBE`. The probe set is unchanged.
2. **Return semantics**: `newPacketWithDescriptor` now returns `nullptr`,
   which makes the upstream `newPacket` log `NEWPACKET FINAL branch=ALLOC_NULL`
   — i.e., it preserves the CR-217 failing semantics required by
   the CR-221 reviewer's bullet 3.
3. **In-source rationale**: comment block rewritten to truthfully describe
   the actual return path (returns `nullptr`, not "first non-NULL"). The
   `BR_SNP_WP` probe now explicitly says the produced object is allocated,
   logged, and **released**, NOT returned upstream.

No other source changes vs CR-221. The probe set (BR_AC_*, BR_GMC_*,
BR_MA_*, BR_SNP_WP) and their logging are byte-identical with CR-221 minus
the rewritten comment and the changed return statement.

## CLAIM_SCOPE

This change is **active allocation probing only**. It does NOT:
- claim to fix STEP 8b
- satisfy any `IO80211NetworkPacket`-family contract
- preserve any rejected behavioral substitute
- change the upstream packet contract delivered by `newPacket`

What it does:

1. Replaces `AirportItlwmIO80211PacketPool::newPacketWithDescriptor`'s body
   with an end-to-end probe sequence exercising every plausible
   IO80211/IOSkywalk packet-allocation path on Tahoe-26.x.
2. Logs each branch's outcome (XYLog) using the existing
   CR-216 split-halves redaction-bypass helpers (`ptrHi32` / `ptrLo32`).
3. Releases every successful intermediate allocation immediately after
   logging.
4. Returns `nullptr` to preserve the CR-217 failure shape upstream.

## SIDE_EFFECTS (per CR-221 reviewer's required enumeration)

Side effects produced by the probe set, with explicit per-item disposition:

### S1. `OSMetaClass::allocClassWithName(const char *)` × 3
- **Lifetime**: each returned `OSObject *` (if non-NULL) is released via
  `OSSafeReleaseNULL` immediately after its log line, before the next
  branch executes.
- **Global side effect**: increments and decrements the metaclass's
  `instanceCount` counter (one increment + matching decrement per probe).
  No persistent state change to the global class registry.
- **Failure safety**: NULL return is the no-op outcome; `OSSafeReleaseNULL`
  is NULL-safe.
- **Why probe cannot perturb measurement**: the probe is the EXACT API
  CR-217 evidence already exercised at runtime. Repeating it on each
  newPacket call cannot change whether the same name still resolves;
  it can only confirm or contradict the prior CR-217 result.

### S2. `OSSymbol::withCStringNoCopy(const char *)` × 2
- **Lifetime**: each returned `const OSSymbol *` is released via
  `OSSafeReleaseNULL` after the matching `BR_GMC_*` / `BR_MA_*` block.
- **Global side effect**: `OSSymbol::withCStringNoCopy` deduplicates against
  the global symbol pool. The first call interns the symbol; subsequent
  calls return the cached interned instance and only bump the refcount.
  The retain/release pair leaves the pool in its pre-call state minus
  one reference; if the symbol was not previously interned and we are
  the only reference, it is reclaimed when our release drops the count
  to zero. Either outcome is benign.
- **Failure safety**: NULL return path is handled (`meta = nullptr`,
  branch logs `SKIPPED` for the dependent `BR_MA_*`).
- **Why probe cannot perturb measurement**: the symbol pool is content-
  keyed, not allocation-keyed. Two probes with the same string yield the
  same interned object; this does not change which classes are
  registered under that name.

### S3. `OSMetaClass::getMetaClassWithName(const OSSymbol *)` × 2
- **Lifetime**: returns a borrowed `const OSMetaClass *` reference; we do
  NOT retain or release. This is the documented contract for
  `getMetaClassWithName` (it returns a pointer to a static metaclass
  that is owned by the registering kext).
- **Global side effect**: none — pure read of the global registry.
- **Failure safety**: NULL return is the no-op outcome; the dependent
  `BR_MA_*` branch checks for NULL and logs `SKIPPED`.

### S4. `meta->alloc()` × up to 2
- **Lifetime**: each returned `OSObject *` (if non-NULL) is released via
  `OSSafeReleaseNULL` immediately after its log line.
- **Global side effect**: increments and decrements the metaclass's
  `instanceCount` counter (one increment + matching decrement per probe).
  This is the same balanced increment/decrement pair as S1.
- **Failure safety**: gated by `meta != nullptr`; NULL return is the
  no-op outcome.

### S5. `IOSkywalkNetworkPacket::withPool(this, desc, 0)`
- **Lifetime**: returned `IOSkywalkPacket *` (if non-NULL) is released via
  `OSSafeReleaseNULL` immediately after the log line. **Not** returned
  upstream.
- **Global side effect**: allocates from the IOSkywalkFamily-internal
  `kalloc_type_view` and runs IOSkywalkNetworkPacket's virtual init
  (vt[35]) which binds the packet to the supplied pool/descriptor. On
  release, the inverse virtual destruct (vt[5]) runs and the kalloc
  block is returned to the same kalloc_type zone. State delta to the
  pool's internal accounting is allocate→release, no net change.
- **Failure safety**: `OSSafeReleaseNULL` is NULL-safe; vptr read is
  conditioned on the non-NULL ptr.
- **Why probe cannot perturb measurement**: the only state inspected by
  this probe is `*reinterpret_cast<void**>(snp_wp)` (the vtable pointer
  of the freshly-allocated packet). That vtable pointer is determined
  at compile time by IOSkywalkFamily and is invariant across
  allocations; reading it cannot affect any future allocation's vtable.

## FAILURE_SAFETY

Every branch is independently failure-safe:

- All `OSSafeReleaseNULL` calls are NULL-safe.
- All vtable reads are gated on non-NULL.
- All `meta->alloc()` calls are gated on `meta != nullptr`.
- The function never panics on probe failure; on every branch's NULL
  outcome, the log line records the NULL and execution continues to the
  next branch.
- The function returns `nullptr` unconditionally, so any partial probe
  failure does not change the upstream return contract from the
  preserved ALLOC_NULL semantics.
- No allocations escape the function (all are released in-line).

## PERTURBATION_ANALYSIS

For each probe, the question "could running this probe change what a
subsequent probe (or a future framework call to allocClassWithName)
returns?":

- **S1 `allocClassWithName`**: alloc + release returns the metaclass
  instance counter to its pre-call value. The class registry's name
  binding is unchanged (cannot register/unregister names through this
  API). So a second invocation deterministically yields the same result.
- **S2 `withCStringNoCopy`**: the symbol pool is content-keyed. If the
  string was already interned, the first probe just retains; if it
  was not interned, the first probe interns it. After our matching
  release, the deduplicated entry either survives (other refs exist)
  or is reclaimed (we held the only ref). Either way the next probe
  with the same string yields the same interned content.
- **S3 `getMetaClassWithName`**: pure read.
- **S4 `meta->alloc()`**: same as S1 (alloc + release balances the
  counter; class registry is read-only here).
- **S5 `IOSkywalkNetworkPacket::withPool`**: the kalloc_type zone uses
  block reuse. After release, the same block may be returned by the
  next allocation from this zone. That is identical behavior to
  steady-state operation; it is not a perturbation in the sense of
  changing what an external observer would see.

In aggregate: the probe set is repeatable. Running it N times yields
N identical log dumps modulo non-determinism in pointer bit values
(which the split-halves logging captures exactly).

## HYPOTHESIS_TREE_AND_BRANCHES

CR-217 evidence proves the failure is at "OSMetaClass::allocClassWithName
(\"IO80211NetworkPacket\") returns NULL". That is a single API; the
underlying cause has at least four candidate shapes:

```
H0: name "IO80211NetworkPacket" is not in this kext's reachable class registry
H1: name is reachable, but metaclass alloc() is sandboxed
H2: name is reachable AND alloc() works, but only via direct metaclass access
H3: gating is class-family-specific (only IO80211 family is gated;
    IOSkywalk family works)
```

Branches in this build, with what each isolates:

```
BR_AC_IO80211       allocClassWithName("IO80211NetworkPacket")          | reproduces CR-217 baseline
BR_AC_IOSKYNET      allocClassWithName("IOSkywalkNetworkPacket")        | tests H3 with IOSkywalkFamily
BR_AC_IOSKY         allocClassWithName("IOSkywalkPacket")               | tests H3 with the IOSkywalk base
BR_GMC_IO80211      getMetaClassWithName(OSSymbol("IO80211NetworkPacket")) | isolates H0 (lookup) from H1 (alloc)
BR_MA_IO80211       BR_GMC_IO80211_meta->alloc()                        | tests H1 directly
BR_GMC_IOSKYNET     getMetaClassWithName(OSSymbol("IOSkywalkNetworkPacket"))| H0 control for IOSkywalkFamily
BR_MA_IOSKYNET      BR_GMC_IOSKYNET_meta->alloc()                       | H1 control
BR_SNP_WP           IOSkywalkNetworkPacket::withPool(this, desc, 0)     | known-working framework factory; vptr captured for CR-223
```

Outcome interpretation matrix:

| BR_AC_IO80211 | BR_GMC_IO80211 | BR_MA_IO80211 | conclusion |
|---|---|---|---|
| NULL | NULL    | (skipped) | H0 confirmed: name lookup itself fails for IO80211 family |
| NULL | non-NULL | NULL     | H1 confirmed: alloc() is sandboxed, lookup is fine |
| NULL | non-NULL | non-NULL | H2 confirmed: allocClassWithName has a wrapper bug; direct path works |
| non-NULL | -    | -        | CR-217 evidence stale; current state already allocates |

The IOSkywalk control row tells us whether the gating is class-family
specific or applies to all class lookups from this kext context.

## REFERENCE_DECOMP_FOR_FRAMEWORK_FACTORY

`IOSkywalkNetworkPacket::withPool` body recovered at BootKC
`0xffffff800297effa` (capstone disassembly):

```
push rbp
mov  rbp, rsp
push r15 r14 r13 r12 rbx rax
mov  r14d, edx                ; r14d = options
mov  r15, rsi                 ; r15  = desc
mov  r12, rdi                 ; r12  = pool
lea  rdi, [rip + 0x38668]     ; kalloc_type_view for IOSkywalkNetworkPacket
mov  esi, 0x78                ; sizeof(IOSkywalkNetworkPacket) (matches static_assert)
call kalloc_type_impl         ; 0xffffff8000a4ca30 — bypasses OSMetaClass class registry
mov  rbx, rax                 ; rbx = raw memory
lea  r13, [rip + 0x37974]
mov  rdi, rax
mov  rsi, r13
call IOSkywalkPacket_ctor     ; 0xffffff800297e0d2
lea  rax, [rip + 0x2ea3a]     ; vtable for IOSkywalkNetworkPacket
mov  qword ptr [rbx], rax     ; install vtable
mov  rdi, r13
call <metaclass tracking>     ; 0xffffff8000a4b2a0
mov  rax, qword ptr [rbx]
mov  rdi, rbx                 ; this
mov  rsi, r12                 ; pool
mov  rdx, r15                 ; desc
mov  ecx, r14d                ; options
call qword ptr [rax + 0x118]  ; virtual init slot vt[35] (pool, desc, options) -> bool
test al, al
jne  success
mov  rax, qword ptr [rbx]
mov  rdi, rbx
call qword ptr [rax + 0x28]   ; virtual destruct vt[5]
xor  ebx, ebx                 ; return NULL
success:
mov  rax, rbx
…
ret
```

This proves the framework factory uses an IOSkywalkFamily-internal
`kalloc_type_view` that completely bypasses `OSMetaClass::allocClassWithName`.
The probe captures the resulting IOSkywalkNetworkPacket vtable pointer so
CR-223 (planned same-kext IOSkywalkNetworkPacket subclass with overridden
content-aware `getPacketType`) can verify its own vtable identity differs
from this captured baseline.

## VERIFICATION

- Build : ** BUILD SUCCEEDED **
- Kext sha256: d2a052d59ebc41d554900109b5ab91bbdd94c90bf8298492f52fba38af762507
- Kext UUID  : B319AED0-95DF-30EC-93CC-227A44725AA7
- Kext size  : 16298920 bytes
- BootKC undef: 887 (all resolve)
- Installed kext sha256: identical (MATCH)
- Installed kext UUID  : identical (MATCH)
- `git diff --cached --check`: passes (verified post-stage)

## CHANGE_FOOTPRINT

- One source file modified: `AirportItlwm/AirportItlwmV2.cpp`
  - 2 #includes (`<libkern/c++/OSSymbol.h>`, `<libkern/c++/OSMetaClass.h>`) — same as CR-221.
  - Replaced body of `AirportItlwmIO80211PacketPool::newPacketWithDescriptor`
    with the end-to-end probe sequence + ALLOC_NULL preservation.
  - The `newPacket` override is unchanged (its existing
    `branch=ALLOC_NULL` log will fire as expected).
  - The `withName` factory and CR-217 instrumentation are unchanged.
  - No header file changes.
  - No other class is touched.

## STAGE_2_INTENT

If Stage 1 approves, the next step is to load the kext (reboot), capture
boot-log lines matching `NEWPKT_PROBE BR_*` plus the upstream
`NEWPACKET FINAL branch=ALLOC_NULL` lines, and submit them as Stage 2
runtime evidence. Expected publishable signals per branch:

```
itlwm: NEWPKT_PROBE ENTER this=0x????_???? desc=0x????_????
itlwm: NEWPKT_PROBE BR_AC_IO80211 ret=0x????_????
itlwm: NEWPKT_PROBE BR_AC_IOSKYNET ret=0x????_????
itlwm: NEWPKT_PROBE BR_AC_IOSKY ret=0x????_????
itlwm: NEWPKT_PROBE BR_GMC_IO80211 sym=0x????_???? meta=0x????_????
itlwm: NEWPKT_PROBE BR_MA_IO80211 ret=0x????_????             (or SKIPPED)
itlwm: NEWPKT_PROBE BR_GMC_IOSKYNET sym=0x????_???? meta=0x????_????
itlwm: NEWPKT_PROBE BR_MA_IOSKYNET ret=0x????_????            (or SKIPPED)
itlwm: NEWPKT_PROBE BR_SNP_WP ret=0x????_???? vptr=0x????_????
itlwm: NEWPKT_PROBE EXIT ret=0x0_0 (preserves ALLOC_NULL)
itlwm: NEWPACKET FINAL branch=ALLOC_NULL this=0x????_???? desc=0x????_???? ret=0xe00002bd
```

Each set is emitted twice (TX pool, RX pool) per CR-217 framing.

The next CR (CR-223, planned) will be authored on the basis of which
branches yielded non-NULL. The most likely outcome (per the BR_SNP_WP
known-success path) is a `REFERENCE_ALIGNMENT_FIX` introducing a same-kext
class derived from IOSkywalkNetworkPacket with an overridden
content-aware `getPacketType` mirroring the IO80211NetworkPacket
classifier (already decompiled to
`analysis/io80211networkpacket_getpackettype_decomp_2026_04_29.txt`).
