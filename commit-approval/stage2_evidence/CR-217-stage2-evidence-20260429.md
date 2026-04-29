# CR-217 STAGE 2 AFTER-FIX RUNTIME EVIDENCE

request_id: CR-217
review_stage: STAGE_2_AFTER_FIX_RUNTIME
referenced_stage_1_decision: commit-approval/decisions/COMMIT_DECISION_CR-217.md
referenced_stage_1_status: APPROVED_FOR_AFTER_FIX_RUNTIME
referenced_change_class: DIAGNOSTIC_INSTRUMENTATION
date: 2026-04-29

## CLAIM_SCOPE_RECAP

CR-217 is a Stage-1-approved DIAGNOSTIC_INSTRUMENTATION CR that
provides:
- Layer 1: stateless `INIT_FALSE_<stage>` classifier for all 9
  framework-internal stages of `IOSkywalkPacketBufferPool::initWithName`
  (PRE_THCALL, PRE_SEGSTATS, PRE_LOCK1, PRE_LOCK2, PRE_OWNER_CACHE,
  KPBP_REJECT, OSARRAY_FIRST, OSARRAY_SECOND, POST_OSARRAY).
- Layer 2: stateless `NEWPACKET_<branch>` final markers for all 4
  return paths of our `AirportItlwmIO80211PacketPool::newPacket`
  override (BAD_ARGS, ALLOC_NULL, INIT_FALSE, OK).
- Pointer-output redaction bypass via split-halves `0x%x_%x`
  rendering through pure helpers `ptrHi32 / ptrLo32 / slotHi32 /
  slotLo32`.

## EXACT_IDENTITY_EVIDENCE

### HEAD identity
- HEAD at boot: `d3a07c2abccac863e1909aa562051a6ee5687245` (matches
  Stage 1 reviewed HEAD).

### Loaded kext identity at boot
Per `kextstat` captured at 2026-04-29 13:45:
```
  143    0 0xffffff7f96620000 0xf01e84   0xf01e84   com.zxystd.AirportItlwm (2.4.0) B431DDC7-7A1A-3962-838D-9A17821D72C1
```
- UUID: `B431DDC7-7A1A-3962-838D-9A17821D72C1` matches Stage 1
  build identity exactly.
- See `commit-approval/runtime_evidence/CR-217-stage2-loaded-kext-20260429.txt`.

## RUNTIME_EVIDENCE_FILES

`commit-approval/runtime_evidence/CR-217-stage2-boot-log-20260429.txt`
— 39 lines covering both TX and RX factory invocations,
controller STEP 8b BEGIN/AFTER_TX/AFTER_RX/FINAL lines, the
existing pre-CR-213 `DEBUG start [STEP 8b]` lines, and
`IO80211Controller::stop` cleanup. Critically: the
`NEWPACKET FINAL branch=ALLOC_NULL` lines fire for both TX and
RX pool factory invocations.

## BRANCH_COVERAGE_OBSERVED

### Layer 2 (newPacket override) — ALLOC_NULL FIRES

Identical pattern for TX and RX (lines from boot log):

```
PACKETPOOL[AirportItlwm-TX] new=0xffffff90_20885a00
   poolVtable=0xffffff7f_9ff25678
   (size=200 opts=0xffffffca_496b7b40
    owner=0xffffffbc_da87c000 ownerVtable=0xffffff7f_9ff25920
    pktCount=256 bufCount=256 bufSize=2048 maxBPP=1 memSegSz=0
    poolFlags=0x1 type=1)

NEWPACKET FINAL branch=ALLOC_NULL
   this=0xffffff90_20885a00
   desc=0xffffffca_496b7260
   ret=0xe00002bd
```

`branch=ALLOC_NULL` ⇒ in our `AirportItlwmIO80211PacketPool::newPacket`
override, the call
`OSMetaClass::allocClassWithName("IO80211NetworkPacket")` returned
NULL, and we returned `kIOReturnNoMemory` (`0xe00002bd`) per the
override contract.

`this=0xffffff90_20885a00` matches the pool object that the
factory just constructed (`new=0xffffff90_20885a00` from the
preceding PACKETPOOL line). `desc=0xffffffca_496b7260` is a
non-NULL valid descriptor pointer passed by the framework.
`outPacket` was non-NULL too (otherwise BAD_ARGS would have
fired instead).

### Layer 1 (initWithName classifier) — POST_OSARRAY confirmed

Following the framework's continued cleanup, the classifier saw
all 9 chronological slots populated and emitted the
`INIT_FALSE_POST_OSARRAY` final marker:

```
PACKETPOOL[AirportItlwm-TX] initWithName=0 (type=1) slots:
   name=0xffffff94_eda0c3e0
   thCall=0xffffff86_8809fa00
   segStats=0xffffffa0_20844ac0
   lock1=0xffffff86_87611fd0
   lock2=0xffffff86_87611160
   owner=0xffffffbc_da87c000
   pbufpool=0xffffff90_20ebeef0
   arr1=0xffffff8b_5a8a7660
   arr2=0xffffff8b_5a8a7628
   typeCache=1 flagsCache=0x1 singleSeg=0 disposed=0
PACKETPOOL[AirportItlwm-TX] FINAL branch=INIT_FALSE_POST_OSARRAY
   preRelease pool=0xffffff90_20885a00
   pbufpool=0xffffff90_20ebeef0 owner=0xffffffbc_da87c000
   arr1=0xffffff8b_5a8a7660 arr2=0xffffff8b_5a8a7628 disposed=0
```

This confirms:
- Every framework-internal stage of `initWithName` BEFORE the
  packet-inventory loop completed successfully.
- `kern_pbufpool_create` succeeded (`pbufpool=0xffffff90_20ebeef0`).
- Both OSArray::withCapacity allocations succeeded.
- The failure is exactly at the `vt[50] newPacket` call inside the
  packet-inventory loop, ratified by the preceding `NEWPACKET FINAL
  branch=ALLOC_NULL` line.

### Controller branch — TX_RX

```
POOLTRACE[STEP8b] FINAL branch=TX_RX failMask=0x3 tx=0x0_0 rx=0x0_0
   cleanup=super_stop_releaseAll_disarm_return_false
```

Both pools failed identically at the same axis (Layer 2 ALLOC_NULL).

### Pointer redaction bypass — VERIFIED WORKS

Every pointer in the runtime log is rendered as readable
`0xHHHH_LLLL` rather than `<private>`:
- `pool=0xffffff90_20885a00` (pool object)
- `pbufpool=0xffffff90_20ebeef0` (kern_pbufpool handle)
- `desc=0xffffffca_496b7260` (descriptor)
- `owner=0xffffffbc_da87c000` (fNetIf)
- ... etc.

The split-halves redaction bypass is fully effective.

## STAGE_2_REQUIREMENTS_CHECKLIST_PER_STAGE_1_DECISION

Per Stage 1 decision STAGE_2_RUNTIME_REQUIREMENTS lines 70-77:

- HEAD unchanged: PASS
- staged diff sha unchanged at boot:
  `3ca1319c081f8b1c39c99ee37b3c6974397806bc8b974a39d1ad38907681e7cd`
  PASS
- loaded kext UUID matches build evidence
  (`B431DDC7-7A1A-3962-838D-9A17821D72C1`): PASS
- TX and RX `PACKETPOOL[…]` factory lines present: PASS
- Exactly one factory branch per pool (`INIT_FALSE_POST_OSARRAY`
  for both): PASS
- STEP 8b controller final branch (`TX_RX`): PASS
- `INIT_FALSE_POST_OSARRAY` reached → `NEWPACKET FINAL
  branch=ALLOC_NULL` markers required and present: PASS

## ROOT_CAUSE_IDENTIFICATION

CR-217's two-layer instrumentation produced the precise next-step
constraint:

> Our subclass override
> `AirportItlwmIO80211PacketPool::newPacket(...)` calls
> `OSMetaClass::allocClassWithName("IO80211NetworkPacket")`,
> which returns NULL on Tahoe. Our override then returns
> `kIOReturnNoMemory`, which causes the framework's
> packet-inventory loop to abort, which causes
> `IOSkywalkPacketBufferPool::initWithName` to return false,
> which causes our pool factory to release the pool to NULL,
> which causes STEP 8b to mark `TX_RX` and abort the start
> sequence.

The likely cause of `allocClassWithName` returning NULL is the
Tahoe kalloc_type sandbox / cross-kext typed-allocation rules,
which constrain `OSMetaClass::allocClassWithName` of a class
defined in a different kext (IO80211Family) when called from
our kext context.

## NEXT_CR_DEPENDENCY

The next CR (CR-218) is a `REFERENCE_ALIGNMENT_FIX` that removes
the `newPacket` vt[50] override entirely. With the override gone,
the framework's base `IOSkywalkPacketBufferPool::newPacket`
(KDK 0xa766) takes over and dispatches to
`IOSkywalkNetworkPacket::withPool` for `packetType=Network`,
which:
- runs in IOSkywalkFamily's kalloc_type context (no cross-kext
  sandbox issue);
- produces an `IOSkywalkNetworkPacket` (size 0x78, the supertype
  of our `IO80211NetworkPacket` per local headers; both are
  `static_assert(sizeof == 0x78)`).

This aligns us 1:1 with AppleBCMWLAN, whose pool subclass also
does NOT override vt[50] (it has only a separate
`newPacketWithDescriptor` helper that the framework never invokes
through the base vtable dispatch).

## EVIDENCE_FILE_INVENTORY

- `commit-approval/runtime_evidence/CR-217-stage2-boot-log-20260429.txt`
  (NEW; 39 raw lines from CR-217 boot 2026-04-29 13:45).
- `commit-approval/runtime_evidence/CR-217-stage2-loaded-kext-20260429.txt`
  (NEW; CR-217 kextstat identity).
- `commit-approval/build_evidence/CR-217-build-stateless-newpacket-coverage.txt`
  (Stage 1; preserved).
- `commit-approval/decisions/COMMIT_DECISION_CR-217.md`
  (Stage 1 APPROVED_FOR_AFTER_FIX_RUNTIME; preserved).
- `commit-approval/requests/CR-217-stateless-newpacket-coverage.md`
  (Stage 1 request; preserved).
- `commit-approval/artifacts/CR-217-stateless-newpacket-coverage.diff`
  (Stage 1 reviewed cumulative diff; preserved).
