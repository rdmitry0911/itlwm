# CR-222 STAGE 2 AFTER-FIX RUNTIME EVIDENCE

request_id: CR-222
review_stage: STAGE_2_AFTER_FIX_RUNTIME
referenced_stage_1_decision: commit-approval/decisions/COMMIT_DECISION_CR-222.md
referenced_stage_1_status: APPROVED_FOR_AFTER_FIX_RUNTIME
referenced_change_class: ACTIVE_ALLOCATION_PROBE
date: 2026-04-29

## CLAIM_SCOPE_RECAP

CR-222 is a Stage-1-approved ACTIVE_ALLOCATION_PROBE that runs eight
allocation/lookup branches inside `AirportItlwmIO80211PacketPool::newPacketWithDescriptor`
on every framework call to `pool->newPacket`. Every branch logs its
result via XYLog using the CR-216 split-halves redaction-bypass
helpers. Successful allocations are released in-line. The function
unconditionally returns `nullptr`, preserving the CR-217 ALLOC_NULL
upstream failure shape.

## EXACT_IDENTITY_EVIDENCE

### HEAD identity
- HEAD at boot: `d3a07c2abccac863e1909aa562051a6ee5687245` (matches Stage 1 reviewed HEAD).

### Loaded kext identity at boot
Per `kextstat` captured at 2026-04-29 16:42:58:
```
  149    0 0xffffff7f96620000 0xf02584   0xf02584   com.zxystd.AirportItlwm (2.4.0) B319AED0-95DF-30EC-93CC-227A44725AA7
```
- UUID: `B319AED0-95DF-30EC-93CC-227A44725AA7` matches Stage 1 build identity exactly.
- See `commit-approval/runtime_evidence/CR-222-stage2-loaded-kext-2026-04-29.txt`.

### Boot timestamp
- `kern.boottime`: `Wed Apr 29 16:42:58 2026` (sec=1777470178).

## RUNTIME_EVIDENCE_FILES

`commit-approval/runtime_evidence/CR-222-stage2-boot-log-2026-04-29.txt`
— 67 lines covering both TX and RX factory invocations. Each
NEWPKT_PROBE line is emitted twice in os_log because of standard
log-stream duplication; the captured ptr values are identical
between paired emissions, so the de-duplicated branch counts below
are correct.

## BRANCH_COVERAGE_OBSERVED

Each pool (TX `this=0xffffff98_9ffabc00`, RX `this=0xffffff98_9ffabd00`)
exercised the full eight-branch probe set in one boot.

### TX pool (`this=0xffffff98_9ffabc00`, `desc=0xffffff90_26e7f260`)

```
NEWPKT_PROBE ENTER       this=0xffffff98_9ffabc00 desc=0xffffff90_26e7f260
NEWPKT_PROBE BR_AC_IO80211    ret=0x0_0                       <-- NULL
NEWPKT_PROBE BR_AC_IOSKYNET   ret=0xffffffa7_6ce8e80          <-- non-NULL
NEWPKT_PROBE BR_AC_IOSKY      ret=0xffffffa7_6ce8f00          <-- non-NULL
NEWPKT_PROBE BR_GMC_IO80211   sym=0xffffff9d_6d639e20 meta=0xffffff80_a8089f0   <-- meta non-NULL
NEWPKT_PROBE BR_MA_IO80211    ret=0x0_0                       <-- NULL  (lookup OK, alloc NULL)
NEWPKT_PROBE BR_GMC_IOSKYNET  sym=0xffffff9d_6c7da128 meta=0xffffff80_ae6d9c0   <-- meta non-NULL
NEWPKT_PROBE BR_MA_IOSKYNET   ret=0xffffffa7_6ce8f80          <-- non-NULL
NEWPKT_PROBE BR_SNP_WP        ret=0xffffffa7_6ce8000 vptr=0xffffff80_ae64a78
NEWPKT_PROBE EXIT             ret=0x0_0 (preserves ALLOC_NULL)
NEWPACKET FINAL branch=ALLOC_NULL this=0xffffff98_9ffabc00 desc=0xffffff90_26e7f260 ret=0xe00002bd
```

### RX pool (`this=0xffffff98_9ffabd00`, `desc=0xffffff90_26e7f260`)

```
NEWPKT_PROBE ENTER       this=0xffffff98_9ffabd00 desc=0xffffff90_26e7f260
NEWPKT_PROBE BR_AC_IO80211    ret=0x0_0                       <-- NULL
NEWPKT_PROBE BR_AC_IOSKYNET   ret=0xffffffa7_6ce8100          <-- non-NULL
NEWPKT_PROBE BR_AC_IOSKY      ret=0xffffffa7_6ce8280          <-- non-NULL
NEWPKT_PROBE BR_GMC_IO80211   sym=0xffffff9d_6d639e20 meta=0xffffff80_a8089f0   <-- meta non-NULL
NEWPKT_PROBE BR_MA_IO80211    ret=0x0_0                       <-- NULL  (lookup OK, alloc NULL)
NEWPKT_PROBE BR_GMC_IOSKYNET  sym=0xffffff9d_6c7da128 meta=0xffffff80_ae6d9c0   <-- meta non-NULL
NEWPKT_PROBE BR_MA_IOSKYNET   ret=0xffffffa7_6ce8300          <-- non-NULL
NEWPKT_PROBE BR_SNP_WP        ret=0xffffffa7_6ce8380 vptr=0xffffff80_ae64a78
NEWPKT_PROBE EXIT             ret=0x0_0 (preserves ALLOC_NULL)
NEWPACKET FINAL branch=ALLOC_NULL this=0xffffff98_9ffabd00 desc=0xffffff90_26e7f260 ret=0xe00002bd
```

## HYPOTHESIS_DECISION

Outcome interpretation matrix from CR-222 request:

| BR_AC_IO80211 | BR_GMC_IO80211 | BR_MA_IO80211 | conclusion |
|---|---|---|---|
| NULL | NULL    | (skipped) | H0 confirmed: name lookup itself fails |
| NULL | non-NULL | NULL     | **H1 confirmed: alloc() is sandboxed, lookup is fine** |
| NULL | non-NULL | non-NULL | H2 confirmed |
| non-NULL | -    | -        | CR-217 evidence stale |

**Observed**: `BR_AC_IO80211 = NULL`, `BR_GMC_IO80211 meta != NULL`, `BR_MA_IO80211 = NULL`.
=> **H1 confirmed for both TX and RX**: the IO80211NetworkPacket OSMetaClass
is reachable from our kext (the metaclass pointer at `0xffffff80_a8089f0`
is identical for TX and RX, consistent with a single global metaclass
instance), but `meta->alloc()` returns NULL specifically for this class.

**Class-family contrast** (IOSkywalkNetworkPacket controls):
- `BR_AC_IOSKYNET` non-NULL: name + alloc both work for IOSkywalkNetworkPacket.
- `BR_MA_IOSKYNET` non-NULL: direct meta->alloc() works.
- `BR_AC_IOSKY` non-NULL: works for the IOSkywalkPacket base.

So Tahoe-26.x's gating is **class-specific**, not kext-context-specific:
the `IO80211NetworkPacket::MetaClass::alloc()` implementation contains
internal logic that returns NULL while `IOSkywalkNetworkPacket::MetaClass::alloc()`
does not.

`BR_SNP_WP` (`IOSkywalkNetworkPacket::withPool`) returned non-NULL on
both invocations; the produced object's vtable pointer
`vptr=0xffffff80_ae64a78` is the IOSkywalkNetworkPacket vtable address,
captured for use as a baseline against which a future same-kext
subclass's vtable pointer can be compared.

## UPSTREAM_FAILURE_PRESERVATION

Both invocations log:

```
itlwm: NEWPACKET FINAL branch=ALLOC_NULL
       this=0xffffff98_9ffabc00 (or _9ffabd00 for RX)
       desc=0xffffff90_26e7f260
       ret=0xe00002bd
```

after the probe `EXIT ret=0x0_0`. This proves the CR-220 `withPool`
substitute packet was **not** returned upstream. The CR-217-era
`branch=ALLOC_NULL` failure shape is preserved as required by Stage 1
decision constraint `approved_for_active_probe_runtime_only: YES`.

The downstream `IOSkywalkPacketBufferPool::initWithName` completion
log line confirms the pool factory then proceeds to the
`INIT_FALSE_POST_OSARRAY` failure path:

```
PACKETPOOL[AirportItlwm-TX] initWithName=0 (type=1) slots: ...
PACKETPOOL[AirportItlwm-TX] FINAL branch=INIT_FALSE_POST_OSARRAY ...
PACKETPOOL[AirportItlwm-TX] FAIL: initWithName returned false; pool released to NULL
```

This is the same failure shape as CR-217 Stage 2 evidence and
confirms the CR-222 probe set did not perturb the
post-newPacket pool teardown sequence.

## SIDE_EFFECT_ACCOUNTING

Each invocation produced (one per pool, twice total):
- 3× `OSMetaClass::allocClassWithName(<name>)` calls with paired
  `OSSafeReleaseNULL` (counter delta = 0).
- 2× `OSSymbol::withCStringNoCopy(<name>)` calls with paired
  `OSSafeReleaseNULL` (counter delta = 0).
- 2× `OSMetaClass::getMetaClassWithName(OSSymbol*)` calls (read-only,
  no retain).
- 1× successful `IOSkywalkNetworkPacket::MetaClass::alloc()` call
  with paired `OSSafeReleaseNULL` (counter delta = 0). The
  IO80211NetworkPacket `meta->alloc()` returned NULL so no release
  was needed.
- 1× successful `IOSkywalkNetworkPacket::withPool(this, desc, 0)`
  call with paired `OSSafeReleaseNULL` (counter delta = 0).

No probe allocation escaped the function. No NULL dereference fired;
all dependent branches that needed a non-NULL precondition observed
it (or correctly skipped, although none did SKIP in this evidence
set because both `meta` lookups returned non-NULL).

## ABSENCE_OF_REGRESSION_VS_CR-217_BASELINE

- CR-217 Stage 2: `NEWPACKET FINAL branch=ALLOC_NULL ret=0xe00002bd`
  for both TX and RX.
- CR-222 Stage 2: identical `NEWPACKET FINAL branch=ALLOC_NULL ret=0xe00002bd`
  for both TX and RX.

The probe set is additive; the upstream contract is unchanged.

## NEXT_STEP_INTENT

Per Stage 1 constraint
`approved_for_active_probe_runtime_only: YES`, **no commit is
authorized for CR-222**. The next CR (CR-223, planned) will be a
separate Stage 1 request with a new exact artifact, authoring a
**REFERENCE_ALIGNMENT_FIX** (or `SYSTEM_CONTRACT_FIX` if reviewer
prefers) introducing a **same-kext class derived from
`IOSkywalkNetworkPacket`** with:
- `OSDefineMetaClassAndStructors(AirportItlwmIO80211Packet,
  IOSkywalkNetworkPacket)` — same-kext metaclass + kalloc_type_view
  bypassing the IO80211NetworkPacket gated alloc().
- An override of `getPacketType()` mirroring the
  `IO80211NetworkPacket::getPacketType()` content classifier
  recovered offline at
  `analysis/io80211networkpacket_getpackettype_decomp_2026_04_29.txt`
  (EAPOL `0x888e` → 2, IEEE-1722 `0x88b4` → 3, IPv4 + UDP src/dst
  port 67/68 → 1 DHCP, else 0).
- Allocation via the framework's `IOSkywalkNetworkPacket::withPool`
  pattern (kalloc_type_impl + vt[35] init dispatch from
  CR-222 Reference decomp), but bound to our same-kext class so
  the produced object's vtable identity carries our overridden
  `getPacketType()`.

The CR-223 vtable identity proof: produced object's `vptr` will
not equal CR-222's captured `vptr=0xffffff80_ae64a78`
(IOSkywalkNetworkPacket); it will be the address of
`AirportItlwmIO80211Packet`'s same-kext vtable.
