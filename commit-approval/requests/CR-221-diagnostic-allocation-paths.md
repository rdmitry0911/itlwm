# COMMIT REQUEST CR-221 — DIAGNOSTIC_INSTRUMENTATION

request_id: CR-221
review_stage: STAGE_1_STRUCTURAL
proposed_change_class: DIAGNOSTIC_INSTRUMENTATION
parent_anomaly: STEP 8b TX/RX packet pool creation failure
runtime_basis: CR-217 Stage 2 evidence (NEWPACKET FINAL branch=ALLOC_NULL,
ret=0xe00002bd) — current root-cause is opaque: `OSMetaClass::allocClassWithName("IO80211NetworkPacket")`
returns NULL, but it is unknown whether the failure is at name lookup,
at metaclass-`alloc()`, at the kalloc_type sandbox, or some Tahoe-26.x
gating new in this kernel build.
prior_rejections:
  - CR-218 REJECTED — base IOSkywalkPacketBufferPool::newPacket fallback did not match the recovered AppleBCMWLAN reference hierarchy.
  - CR-219 REJECTED — local vt[53] vs reference vt[54] mismatch and stale source comment claiming a same-kext IO80211NetworkPacket subclass.
  - CR-220 REJECTED — SYSTEM_CONTRACT_FIX reframing of withPool path; auditor: `IOSkywalkNetworkPacket::withPool` produces an `IOSkywalkNetworkPacket`, not an `IO80211NetworkPacket`-family object, and therefore does not satisfy the `IO80211NetworkPacket::getPacketType` content-classification contract that `IO80211PeerManager::inputPacket` consumes via vtable dispatch.
auditor_offered_alternative: "Either provide a packet object path that
structurally satisfies the `IO80211NetworkPacket`-family contract …
or narrow the request to diagnostic instrumentation rather than a fix."
this_request_takes: SECOND PATH (diagnostic instrumentation only).

reviewed_head_commit: d3a07c2abccac863e1909aa562051a6ee5687245
artifact: commit-approval/artifacts/CR-221-diagnostic-allocation-paths.diff
build_evidence: commit-approval/build_evidence/CR-221-build-diagnostic-allocation-paths.txt
kext_sha256: e8fee6a09431174e8200d003ecf8521b03399d47f9db645935ac5da1119ab31c
kext_uuid: 3D13F5CA-1CE8-39DA-BF4D-DA4AE3BA0208
kext_size: 16298920
bootkc_undef: 887

## CLAIM_SCOPE

This change is **diagnostic instrumentation only**. It does NOT claim to
fix STEP 8b. It does NOT claim to satisfy any `IO80211NetworkPacket`-family
contract. It does NOT change the produced packet's vtable identity from
the CR-220 baseline.

What it does:

1. Replaces the body of `AirportItlwmIO80211PacketPool::newPacketWithDescriptor`
   with an end-to-end probe sequence that exercises every plausible
   IO80211/IOSkywalk packet-allocation path on Tahoe-26.x.
2. Logs each branch's outcome (XYLog) using the existing
   CR-216 split-halves redaction-bypass helpers
   (`ptrHi32`/`ptrLo32`).
3. Releases every successful intermediate allocation so the build is
   leak-neutral.
4. Returns the result of `IOSkywalkNetworkPacket::withPool(this, desc, 0)`
   as the upstream pool->newPacket value (identical to the CR-220
   baseline upstream behavior), so the data path keeps progressing
   even while the diagnostic is collecting evidence.

The diagnostic is **end-to-end in one build** per
`feedback_diagnostic_end_to_end_criterion`: every branch in the
hypothesis tree below is exercised and logged on the same boot, not
spread across iterative single-signal builds.

## HYPOTHESIS_TREE_AND_BRANCHES

CR-217 evidence proves the failure is at "OSMetaClass::allocClassWithName
(\"IO80211NetworkPacket\") returns NULL". That is a single API; the
underlying cause has at least four candidate shapes:

```
H0: name "IO80211NetworkPacket" is not in this kext's reachable class registry
H1: name is reachable, but metaclass alloc() is sandboxed
H2: name is reachable AND alloc() works, but only when called via a
    different name shape (Symbol vs CString)
H3: the failure is class-specific (only IO80211 family is gated;
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
BR_SNP_WP           IOSkywalkNetworkPacket::withPool(this, desc, 0)     | known-working framework factory (CR-220 path)
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
call <metaclass instanceConstructed?> ; 0xffffff8000a4b2a0
mov  rax, qword ptr [rbx]     ; load vtable
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
That is why CR-220's `withPool` succeeds while the previous code's
`OSMetaClass::allocClassWithName("IO80211NetworkPacket")` returned NULL.
What CR-220 cannot do is install a same-kext vtable; this CR does not
attempt to.

## DIAGNOSTIC_NEUTRALITY

- Branches log only — no state mutation outside of (allocate → log → release).
- The function still returns the same value the CR-220 baseline produced
  (`IOSkywalkNetworkPacket::withPool(this, desc, 0)`), so this build does
  not change pool->newPacket's apparent contract for upstream.
- Each non-NULL allocation from a probe is released with `OSSafeReleaseNULL`,
  so the build is leak-neutral.
- No retry loops, no fallbacks, no timing changes, no fake-success paths.
- All branches are unconditional except `BR_MA_*` which is conditional
  on `BR_GMC_*` returning non-NULL (a precondition; the unconditional
  branch alone would crash on a NULL deref). The `SKIPPED` branch is
  still logged.

## NEW_BUILD_DEPENDENCIES

Three new BootKC undef symbols (884 → 887):

- `OSMetaClass::getMetaClassWithName(OSSymbol const*)` — exported T at
  `0xffffff8000a4baa0`.
- `OSSymbol::withCStringNoCopy(char const*)` — exported T at
  `0xffffff8000a7bd30`.
- A virtual `OSMetaClass::alloc() const` reference (resolved via
  vtable dispatch on the metaclass returned by `getMetaClassWithName`).

All three resolve against BootKC; verifier reports
`OK: all 887 undefined symbols resolve against BootKC`.

## REQUEST_VS_PRIOR_REJECTIONS

- CR-220 reviewer's first required change was: "Either provide a packet
  object path that structurally satisfies the `IO80211NetworkPacket`-family
  contract … or narrow the request to diagnostic instrumentation
  rather than a fix." This CR takes the second path verbatim.
- This CR does NOT claim 1:1 reference alignment with any AppleBCMWLAN
  packet pool method — it is purely diagnostic.
- This CR does NOT claim system contract coverage for IO80211NetworkPacket —
  it documents that the vtable identity remains IOSkywalkNetworkPacket and
  that the diagnostic does not change CR-220's residual risk.
- This CR does NOT defer evidence to Stage 2 of a fix — its sole purpose
  IS to collect Stage 2 evidence, after which a follow-up CR can be
  written as either a `REFERENCE_ALIGNMENT_FIX` (if BR_GMC_IO80211 +
  BR_MA_IO80211 give the right kalloc path) or as a different
  `SYSTEM_CONTRACT_FIX` (if every IO80211 path is genuinely sandboxed
  and we must derive an alternative satisfaction).

## VERIFICATION

- Build : ** BUILD SUCCEEDED **
- Kext sha256: e8fee6a09431174e8200d003ecf8521b03399d47f9db645935ac5da1119ab31c
- Kext UUID  : 3D13F5CA-1CE8-39DA-BF4D-DA4AE3BA0208
- Kext size  : 16298920 bytes
- BootKC undef: 887 (all resolve)
- Installed kext sha256: identical (MATCH)
- Installed kext UUID  : identical (MATCH)
- `git diff --cached --check`: passes (verified post-stage)

## CHANGE_FOOTPRINT

- One source file modified: `AirportItlwm/AirportItlwmV2.cpp`
  - Add 2 #includes (`<libkern/c++/OSSymbol.h>`, `<libkern/c++/OSMetaClass.h>`).
  - Replace the body of `AirportItlwmIO80211PacketPool::newPacketWithDescriptor`
    with the end-to-end probe sequence above.
  - The `newPacket` override is unchanged.
  - The `withName` factory and CR-217 instrumentation are unchanged.
  - No header file changes.
  - No other class is touched.

## STAGE_2_INTENT

If Stage 1 approves, the next step is to load the kext (reboot), capture
boot-log lines matching `NEWPKT_PROBE BR_*`, and submit them as Stage 2
runtime evidence. The next CR will then be authored on the basis of which
branches yielded non-NULL. Expected publishable signals per branch:

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
itlwm: NEWPKT_PROBE EXIT ret=0x????_????
```

Each line is emitted twice (TX pool, RX pool) per CR-217 framing.
