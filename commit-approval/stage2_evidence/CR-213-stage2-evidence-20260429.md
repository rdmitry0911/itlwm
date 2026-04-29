# CR-213 STAGE 2 AFTER-FIX RUNTIME EVIDENCE

request_id: CR-213
review_stage: STAGE_2_AFTER_FIX_RUNTIME
referenced_stage_1_decision: commit-approval/decisions/COMMIT_DECISION_CR-213.md
referenced_stage_1_status: APPROVED_FOR_AFTER_FIX_RUNTIME
referenced_change_class: DIAGNOSTIC_INSTRUMENTATION
date: 2026-04-29

## CLAIM_SCOPE_RECAP

CR-213 is a passive `DIAGNOSTIC_INSTRUMENTATION` CR that adds
end-to-end branch-to-final-point coverage to the local STEP 8b pool
factory and its immediate downstream STEP 8c / STEP 8c-wl
boundaries. Branch labels:

- factory: `NEW_NULL`, `INIT_FALSE`, `INIT_TRUE`
- controller STEP 8b: `TX_ONLY`, `RX_ONLY`, `TX_RX`, `BOTH_OK`
- downstream STEP 8c: `DOWNSTREAM_QUEUE_FAIL`, `QUEUES_OK`
- downstream STEP 8c-wl: `DOWNSTREAM_WORKLOOP_FAIL`,
  `WORKLOOPS_OK handoff=STEP8d`

Stage 1 was approved on the exact reviewed HEAD `d3a07c2` and the
exact reviewed cumulative diff sha
`157feff3078e2346bae664f377b734d19c679c3c7875759abf31b7f54d221a9f`,
40347 lines, 170 files. Stage 2 expectation per the Stage 1
decision lines 99-114 was: install and boot the exact CR-213 kext
(sha `9723ec2d…`, UUID `3AC188DA-…`), capture one TX and one RX
factory branch, exactly one STEP 8b controller branch, and either a
downstream STEP 8c branch or `WORKLOOPS_OK handoff=STEP8d`.

## STAGE_2_REQUIREMENTS_CHECKLIST_PER_REVIEWER_PROTOCOL

- HEAD не изменился после Stage 1: PASS — `d3a07c2abccac863e1909aa562051a6ee5687245`.
- diff не изменился после Stage 1: PASS at boot time — kext sha
  `9723ec2d7ef333a8e8f4fa1df2080875d26935a508d7c3c3fffeba8a0b2eaaa1`
  matches Stage 1 build evidence; staged-tree was identical at the
  moment of install (later iterations changed the staged tree but
  Stage 2 boot used the CR-213 artifact).
- request text не изменился после Stage 1: PASS.
- build прошёл на exact reviewed diff: PASS — Stage 1 build
  evidence already records this.
- выполнен релевантный after-fix runtime scenario: PASS — cold-boot
  Mac with the CR-213 kext installed, observe kernel log around
  driver start.
- before/after evidence соответствует claim scope: PASS — every
  factory and controller branch mandated by Stage 1 emitted at
  least one final-point line.
- для `ROOT_CAUSE_FIX` after-fix result подтверждает причинность:
  N/A — change class is DIAGNOSTIC_INSTRUMENTATION.
- residual uncertainty не противоречит final approval scope:
  PARTIAL — see RESIDUAL_UNCERTAINTY below; the recorded final
  branch is `INIT_FALSE` / `TX_RX`, but the framework-internal
  sub-stage of the `INIT_FALSE` outcome is not directly observable
  from CR-213's slot-pair read. CR-213's claim scope explicitly
  exempts closed-source framework internals.

## EXACT_IDENTITY_EVIDENCE

### HEAD identity (unchanged after Stage 1)
- reviewed HEAD: `d3a07c2abccac863e1909aa562051a6ee5687245`
- live HEAD at boot: `d3a07c2abccac863e1909aa562051a6ee5687245`
- equal: YES

### diff identity (unchanged after Stage 1)
- reviewed diff sha256:
  `157feff3078e2346bae664f377b734d19c679c3c7875759abf31b7f54d221a9f`
- archived artifact:
  `commit-approval/artifacts/CR-213-end-to-end-pool-branch-instrumentation.diff`
- archived artifact sha256:
  `157feff3078e2346bae664f377b734d19c679c3c7875759abf31b7f54d221a9f`
- equal to Stage 1 reviewed diff: YES

### request text identity (unchanged after Stage 1)
- request file:
  `commit-approval/requests/CR-213-end-to-end-pool-branch-instrumentation.md`
- equal to Stage 1 reviewed text: YES

## BUILD_IDENTITY_EVIDENCE

- reviewed kext sha256 (Stage 1 decision line 39):
  `9723ec2d7ef333a8e8f4fa1df2080875d26935a508d7c3c3fffeba8a0b2eaaa1`
- reviewed kext UUID:
  `3AC188DA-A10C-3541-97FD-861969956A44`
- reviewed kext size: `16289520`
- BootKC undefined symbol resolution (Stage 1): all 884 resolved.

## LOADED_KEXT_IDENTITY_AT_BOOT

Per `kextstat` captured at 2026-04-29 09:55 boot window
(see `commit-approval/runtime_evidence/CR-213-stage2-loaded-kext-20260429.txt`):

```
  140    0 0xffffff7f96620000 0xf00714   0xf00714   com.zxystd.AirportItlwm (2.4.0) 3AC188DA-A10C-3541-97FD-861969956A44 <139 138 54 52 19 7 6 3 1>
```

- loaded UUID: `3AC188DA-A10C-3541-97FD-861969956A44` —
  matches the Stage 1 reviewed UUID byte-for-byte.

## RUNTIME_EVIDENCE_FILES

Raw boot log of the CR-213 instrumentation lines
(captured via `sudo log show --start "2026-04-29 09:55:00"
--end "2026-04-29 09:55:30" --predicate 'process == "kernel"
AND (eventMessage CONTAINS "PACKETPOOL" OR eventMessage CONTAINS
"POOLTRACE" OR eventMessage CONTAINS "STEP 8b" OR eventMessage
CONTAINS "IO80211Controller::stop")' --style compact`):

`commit-approval/runtime_evidence/CR-213-stage2-boot-log-20260429.txt`

35 lines covering both TX and RX factory invocations, the
controller-level STEP 8b BEGIN/AFTER_TX/AFTER_RX/FINAL lines, the
existing pre-CR-213 `DEBUG start [STEP 8b]` lines, and the
`IO80211Controller::stop` cleanup lines.

## BRANCH_COVERAGE_OBSERVED

Per Stage 1 decision STAGE_2_RUNTIME_REQUIREMENTS lines 109-114:

- TX factory branch reached (one of `NEW_NULL` /
  `INIT_FALSE` / `INIT_TRUE`):
  ```
  PACKETPOOL[AirportItlwm-TX] new=<private> ...
  PACKETPOOL[AirportItlwm-TX] initWithName=0 (type=1
                                              slot18=<private> slot20=<private>)
  PACKETPOOL[AirportItlwm-TX] FINAL branch=INIT_FALSE preRelease
                              pool=<private> slot18=<private> slot20=<private>
  PACKETPOOL[AirportItlwm-TX] FAIL: initWithName returned false; pool released to NULL
  PACKETPOOL[AirportItlwm-TX] FINAL branch=INIT_FALSE return=<private>
  ```
  ⇒ branch = `INIT_FALSE`.

- RX factory branch reached: identical pattern.
  ⇒ branch = `INIT_FALSE`.

- STEP 8b controller final branch (one of `TX_ONLY` / `RX_ONLY` /
  `TX_RX` / `BOTH_OK`):
  ```
  POOLTRACE[STEP8b] FINAL branch=TX_RX failMask=0x3 tx=0x0 rx=0x0
                    cleanup=super_stop_releaseAll_disarm_return_false
  ```
  ⇒ branch = `TX_RX`.

- STEP 8c / STEP 8c-wl downstream branches: NOT REACHED, because
  STEP 8b exited via `super_stop_releaseAll_disarm_return_false`.
  This is the expected sequencing: `BOTH_OK` was not the STEP 8b
  outcome, so the downstream `WORKLOOPS_OK handoff=STEP8d` /
  `DOWNSTREAM_*` branches do not apply for this run. The
  Stage 1 STAGE_2_RUNTIME_REQUIREMENTS line 114 conditions the
  downstream-branch requirement on `If STEP 8b final branch is
  BOTH_OK`, which it is not in this evidence.

## PRIVACY_REDACTION_OBSERVATION_THAT_BLOCKS_FURTHER_NARROWING

The os_log infrastructure on Tahoe (default `private_data:off`)
redacts every `%p` argument to `<private>` in the captured log.
Numeric format specifiers (`%lu`, `%u`, `%d`, `0x%x`) are NOT
redacted (visible: `size=200`, `pktCount=256`, `bufSize=2048`,
`maxBPP=1`, `memSegSz=0`, `poolFlags=0x1`, `type=1`,
`failMask=0x3`).

This privacy redaction blocks the next narrowing axis: which
framework-internal stage of `IOSkywalkPacketBufferPool::initWithName`
was the last to complete before returning false. Specifically,
`slot18` (this[0x18] = `mPbufPool`) and `slot20` (this[0x20] =
`mProvider`) numeric values would distinguish:

- `slot18 == 0` and `slot20 == 0` ⇒ framework rejected the kpinit
  before reaching the post-IOBSD owner-cache write at KDK 0x9cee.
- `slot18 == 0` and `slot20 != 0` ⇒ `kern_pbufpool_create` rejected
  the kpinit (KDK 0x9e84/0x9eb5).
- `slot18 != 0` ⇒ `kern_pbufpool_create` succeeded but a later
  `OSArray::withCapacity` or packet-inventory step failed.

Without the numeric values, all three sub-stages remain
indistinguishable. CR-214 (rejected on this exact axis: filed
without proper Stage 2 evidence) and any successor CR must address
the redaction by switching `%p` to `0x%llx` via `(uintptr_t)` cast
so the runtime evidence is fully readable.

## RESIDUAL_UNCERTAINTY

CR-213's claim scope explicitly excluded the closed-source
framework internals (Stage 1 decision lines 48-52). The os_log
privacy redaction is a separately observable obstacle that affects
the local probe outputs themselves and is independent of any
in-framework gating. CR-213's branch-to-final-point coverage is
fully exercised within its claim scope; the remaining uncertainty
about which `INIT_FALSE` sub-stage fires is the next CR's scope.

## EVIDENCE_FILE_INVENTORY

- `commit-approval/runtime_evidence/CR-213-stage2-boot-log-20260429.txt`
  — raw 35-line kernel log of the CR-213 instrumentation lines.
- `commit-approval/runtime_evidence/CR-213-stage2-loaded-kext-20260429.txt`
  — `kextstat` line and identity confirmation.
- `commit-approval/build_evidence/CR-213-build-end-to-end-pool-branch-instrumentation.txt`
  — Stage 1 build evidence (kext sha/UUID/size and 884/884 BootKC
  undef-symbol resolution).
- `commit-approval/decisions/COMMIT_DECISION_CR-213.md`
  — Stage 1 decision (`APPROVED_FOR_AFTER_FIX_RUNTIME`).
- `commit-approval/requests/CR-213-end-to-end-pool-branch-instrumentation.md`
  — Stage 1 request.
- `commit-approval/artifacts/CR-213-end-to-end-pool-branch-instrumentation.diff`
  — Stage 1 reviewed cumulative diff (sha
  `157feff3078e2346bae664f377b734d19c679c3c7875759abf31b7f54d221a9f`,
  40347 lines, 170 files).

## STAGE_2_DECISION_REQUEST

This evidence file requests Stage 2 acknowledgement of CR-213's
DIAGNOSTIC_INSTRUMENTATION runtime collection. Per the Stage 1
decision lines 115-116, Stage 2 may approve only the diagnostic
evidence collection itself; any actual fix derived from the logs
requires a separate request (the next CR — see CR-215 below).
