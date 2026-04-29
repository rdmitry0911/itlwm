# CR-215 STAGE 2 AFTER-FIX RUNTIME EVIDENCE

request_id: CR-215
review_stage: STAGE_2_AFTER_FIX_RUNTIME
referenced_stage_1_request: commit-approval/requests/CR-215-end-to-end-init-false-branch-coverage-resubmit.md
referenced_change_class: DIAGNOSTIC_INSTRUMENTATION
date: 2026-04-29

## CLAIM_SCOPE_RECAP

CR-215 is the resubmission of CR-214 with the missing CR-213
Stage 2 evidence files filed. Source-side instrumentation is
byte-identical to CR-214: enumerate every internal slot the
framework's `IOSkywalkPacketBufferPool::initWithName` writes
(chronological per KDK 0x9c5f..0x9f24), classify INIT_FALSE into
named `INIT_FALSE_<stage>` branches, and render every pointer
through `(uintptr_t)` cast as `0x%llx`.

## EXACT_IDENTITY_EVIDENCE

### HEAD identity (unchanged after Stage 1 submission)
- HEAD at boot: `d3a07c2abccac863e1909aa562051a6ee5687245`

### Loaded kext identity at boot
Per `kextstat` captured at 2026-04-29 12:32:
```
  144    0 0xffffff7f96620000 0xf00d24   0xf00d24   com.zxystd.AirportItlwm (2.4.0) B2A2AA8A-02DA-31D7-98F6-7437B1F91EEA
```
- UUID: `B2A2AA8A-02DA-31D7-98F6-7437B1F91EEA` matches Stage 1 build identity.
- See `commit-approval/runtime_evidence/CR-215-stage2-loaded-kext-20260429.txt`.

## RUNTIME_EVIDENCE_FILES

`commit-approval/runtime_evidence/CR-215-stage2-boot-log-20260429.txt`
— 35 lines covering both TX/RX factory invocations, controller
STEP 8b BEGIN/AFTER_TX/AFTER_RX/FINAL lines, the existing
pre-CR-213 `DEBUG start [STEP 8b]` lines, and `IO80211Controller::stop`
cleanup.

## BRANCH_COVERAGE_OBSERVED

For TX and RX symmetrically (lines from boot log):
```
PACKETPOOL[AirportItlwm-TX] new=0x<private> poolVtable=0x<private>
   (size=200 opts=0x<private> owner=0x<private> ownerVtable=0x<private>
    pktCount=256 bufCount=256 bufSize=2048 maxBPP=1 memSegSz=0
    poolFlags=0x1 type=1)
PACKETPOOL[AirportItlwm-TX] initWithName=0 (type=1) slots:
   name=0x<private> thCall=0x<private> segStats=0x<private>
   lock1=0x<private> lock2=0x<private> owner=0x<private>
   pbufpool=0x<private> arr1=0x<private> arr2=0x<private>
   typeCache=1 flagsCache=0x1 singleSeg=0 disposed=0
PACKETPOOL[AirportItlwm-TX] FINAL branch=INIT_FALSE_POST_OSARRAY
   preRelease pool=0x<private> pbufpool=0x<private>
   owner=0x<private> arr1=0x<private> arr2=0x<private> disposed=0
PACKETPOOL[AirportItlwm-TX] FAIL: initWithName returned false; pool released to NULL
PACKETPOOL[AirportItlwm-TX] FINAL branch=INIT_FALSE_POST_OSARRAY return=0x0
[same pattern for AirportItlwm-RX]
POOLTRACE[STEP8b] FINAL branch=TX_RX failMask=0x3 tx=0x0 rx=0x0
   cleanup=super_stop_releaseAll_disarm_return_false
```

### Branch ⇒ `INIT_FALSE_POST_OSARRAY` (BOTH TX AND RX)

Per CR-215 source's classifier, this label fires when:
- slot[0xb0] != 0 (thread_call_allocate succeeded)
- slot[0x78] != 0 (IOMallocTypeImpl SegmentStats succeeded)
- slot[0x80] != 0 (first IORecursiveLockAlloc succeeded)
- slot[0x88] != 0 (second IORecursiveLockAlloc succeeded)
- slot[0x20] != 0 (mProvider written)
- slot[0x18] != 0 (`kern_pbufpool_create` SUCCEEDED — pool handle stored)
- slot[0x68] != 0 (first OSArray::withCapacity succeeded)
- slot[0x60] != 0 (second OSArray::withCapacity succeeded)

Therefore the framework's `initWithName` reaches and completes
**every internal stage up to and including the second OSArray
allocation**. The failure must be in a stage AFTER both OSArray
allocations.

### Disasm of post-OSArray flow (KDK IOSkywalkFamily.kext initWithName)

Past `+0x32b` (second OSArray::withCapacity), the framework runs:
```
+0x373: call qword [rax+0x180]      ; vt slot 50 = newPacket
+0x396: call qword [rax+0x1b8]      ; vt slot 57
+0x3d6: call IOMallocTypeVarImpl
+0x426: call qword [rax+0x190]      ; vt slot 52 = newMemorySegment
+0x441: call qword [rax+0x1b8]      ; vt slot 57
... etc ...
+0x4eb: call qword [rax+0x128]      ; vt slot 39 = allocatePacket(IOSkywalkPacket**, uint)
+0x506: call qword [rax+0x730]      ; vt slot 232
+0x650: call IORecursiveLockLock
+0x676: call IOSkywalkPacketBufferPool::disposeAllPackets
+0x687: call kern_pbufpool_destroy
... cleanup paths ...
```

**vt slot 50 = `newPacket`** is our subclass's overridden
`AirportItlwmIO80211PacketPool::newPacket`. The framework calls
this in a packet-inventory loop (one call per `kbi_packets` =
256). Our override has four mutually-exclusive return paths:

1. `desc == NULL || outPacket == NULL` → return `kIOReturnBadArgument`
2. `OSMetaClass::allocClassWithName("IO80211NetworkPacket")` returned NULL → return `kIOReturnNoMemory`
3. `packet->initWithPool(this, desc, 0)` returned `false` → return `kIOReturnError`
4. Success → return `kIOReturnSuccess`

Any non-success return stops the framework's loop and returns
false from `IOSkywalkPacketBufferPool::initWithName`.

### Privacy redaction observed

Despite CR-215's `(uintptr_t)` cast and `0x%llx` format, every
pointer-derived 64-bit value is redacted as `<private>`. Numeric
fields (`%lu`, `%u`, `%d`, `0x%x`) remain public:

- `size=200` (`%lu` from `sizeof()`) — public
- `pktCount=256`, `bufCount=256`, `bufSize=2048`, `maxBPP=1`,
  `memSegSz=0` (`%u`) — public
- `poolFlags=0x1` (`0x%x`) — public
- `type=1` (`%d`) — public
- `typeCache=1`, `flagsCache=0x1`, `singleSeg=0`, `disposed=0`
  (read directly via `*reinterpret_cast<uint32_t*>` /
  `*reinterpret_cast<uint8_t*>`, formatted with `%u`/`0x%x`) —
  public
- `new`, `poolVtable`, `opts`, `owner`, `ownerVtable`,
  `pool` (factory args), `name`, `thCall`, `segStats`, `lock1`,
  `lock2`, `owner` (slot read), `pbufpool`, `arr1`, `arr2`
  (read via `*reinterpret_cast<void**>` then `(uintptr_t)`-cast
  to `unsigned long long`, formatted with `0x%llx`) — REDACTED.

The cast through `(uintptr_t)` does NOT bypass os_log's privacy
filter. The os_log infrastructure appears to track pointer-derived
values by lineage and redact them regardless of the format
specifier.

The redaction is not blocking the macro-level inference (the
named branch label encodes the slot pattern), but it does block
fine-grained verification of each slot's exact non-zero value.
The next CR (CR-216) addresses both:
- adds end-to-end branch coverage for the
  `newPacket` override (sub-branches of `INIT_FALSE_POST_OSARRAY`);
- works around the redaction by reading values as 32-bit
  half-words (split into hi/lo) and formatting via `0x%x` which
  is verified-public.

## STAGE_2_REQUIREMENTS_CHECKLIST

- HEAD не изменился: PASS.
- diff не изменился: PASS.
- request text не изменился: PASS.
- build прошёл на exact reviewed diff: PASS.
- after-fix runtime scenario выполнен: PASS — cold-boot Mac with
  CR-215 kext.
- before/after evidence соответствует claim scope: PARTIAL — the
  claim covers branch-to-final-point classification, which is
  correctly observed (`INIT_FALSE_POST_OSARRAY`); the
  privacy-redaction limitation is documented but does not break
  the classification.
- residual uncertainty не противоречит final approval scope: PASS
  — the named branch label is sufficient to advance the
  hypothesis; sub-branches inside `newPacket` are out of CR-215
  scope and are the subject of CR-216.

## NEXT_CR_DEPENDENCY

CR-216 will:
- preserve CR-215's INIT_FALSE_<stage> classification verbatim;
- add end-to-end branch coverage for the four mutually-exclusive
  return paths of our `AirportItlwmIO80211PacketPool::newPacket`
  override: `NEWPACKET_BAD_ARGS` / `NEWPACKET_ALLOC_NULL` /
  `NEWPACKET_INIT_FALSE` / `NEWPACKET_OK`;
- bypass the os_log privacy redaction observed here by reading
  selected slot values as paired 32-bit half-words (`0x%x_%x`
  format) which is verified-public.
