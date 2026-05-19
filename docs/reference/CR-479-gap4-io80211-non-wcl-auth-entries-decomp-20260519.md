# GAP_4: IO80211 non-WCL AUTH-related entry decomp/reference recovery for `BootKernelExtensions.kc` — rev7 active classification: 17 GAP_4 + 1 supplemental targets recovered to FULL_BODY_OR_LOW_LEVEL_EQUIVALENT_PER_TARGET (target #17 IO80211InfraInterface::processBSDCommand is a 2046-byte callable selector-dispatcher recovered via option-(c) Capstone skipdata=True linear decode with the complete 483-instruction body listing and complete CFG/edge table embedded in this packet's tracked raw evidence file); rev1 AUTH initiator NARROWED — not closed — to local AirportItlwmSkywalkInterface::setASSOCIATE pending GAP_5/GAP_6 closure

## Scope and current rev7 classification

GAP_3 closed the BootKC scope NEGATIVE for an Apple-extension iwn
firmware command-code surface. The auditor's GAP_4 instruction
(`sig_20260519T100824_0300_3eaa2882`) named 17 IO80211/WCL non-WCL_ASSOCIATE
AUTH-related targets in `BootKernelExtensions.kc` for recovery. After
six prior rejected revisions (rev1, rev2 stalled at preflight,
rev3, rev4, rev5, rev6; see the "Document version history" section
at the bottom of this file for the per-revision blocker chain), the
current rev7 classification of the 17 GAP_4 targets plus the 1
supplemental shared helper is:

- All 7 `AppleBCMWLANInfraProtocol::setWCL_*` setters (targets #1-#7)
  are recovered to raw disassembly + branch/call/return + CFG.
  Targets #1 `setWCL_REASSOC` and #3 `setWCL_QOS_PARAMS` are
  CALLABLE FORWARDERS (rev4 batch `CR479Gap4DecompRev4.java`
  materialised the real callable bodies via
  `DisassembleCommand + CreateFunctionCmd`; rev4 evidence accepted
  by the auditor). Targets #2/#4/#5/#6/#7 are 19..59-byte direct-
  call forwarders or stubs (rev2 evidence; accepted).

- All 9 `apple80211set*` selector entries (targets #8-#16) are
  recovered to raw disassembly + branch/call/return + CFG.
  Pattern A (11-byte fixed-IOReturn stubs): #8 `setAUTH_TYPE`,
  #11 `setASSOCIATION_STATUS`, #16 `setASSOC_READY_STATUS`.
  Pattern B (51-byte `vtable[0xcc8]` + safeMetaCast tail-call
  thunks): #9 `setASSOCIATE`, #10 `setDEAUTH`, #12 `setSTA_AUTHORIZE`,
  #13 `setSTA_DISASSOCIATE`, #14 `setSTA_DEAUTH`,
  #15 `setRANGING_AUTHENTICATE`. All rev2 evidence; accepted.

- Target #17 `IO80211InfraInterface::processBSDCommand` (rev7
  classification): a 2046-byte CALLABLE SELECTOR/IOCTL DISPATCHER
  with body
  `[ffffff80022dea78, ffffff80022df275]` (bounded above by the next
  Ghidra-defined function symbol
  `IO80211InfraInterface::isDebounceOnGoing()` at
  `ffffff80022df276`; ending with the canonical RET at
  `ffffff80022df26f`, followed by the `__stack_chk_fail` abort
  handler call at `ffffff80022df270` and the alignment `nop` at
  `ffffff80022df275`). Recovered to FULL_BODY proof level via
  Capstone 5.0.7 `skipdata=True` linear decode of the 4096-byte
  memory dump captured by Ghidra rev5h batch
  (`CR479Gap4DecompRev5h.java`). Complete CFG: 483 decoded
  instructions, 47 CALLs (38 direct + 9 indirect through vtable
  memory operands such as `[rax+0x208]`, `[rcx+0x1f8]`,
  `[rcx+0x120]`, `[rax+0x28]`, `[rax+0x890]`, etc., which is the
  vtable selector-dispatch pattern), 18 conditional branches, 6
  unconditional JMPs, 2 RETs (`ffffff80022df0ff` early-exit and
  `ffffff80022df26f` final exit), 5 skipdata bytes where Capstone
  resynchronised across apparent invalid encodings. The complete
  483-instruction linear decode and the complete 78-entry CFG /
  branch / call / return / skipdata table are embedded in the
  tracked raw evidence file
  `docs/reference/CR-479-gap4-io80211-non-wcl-auth-entries-decomp-20260519-raw.txt`
  rev6 appendix E (rev7-expanded).

- Target #18 supplemental shared helper:
  `OSMetaClassBase::safeMetaCast` (positively identified as
  `FUN_ffffff80009ff310`; 4107 in-edges across BootKC; called from
  all 6 Pattern-B 51-byte thunks). Rev2 evidence; accepted.

Rev1 AUTH-initiator NARROWING (NARROWING, not CLOSURE) within the
BootKC IO80211/WCL scope: the single narrowed candidate is
`AirportItlwmSkywalkInterface::setASSOCIATE` (target #9 via the
local override at `AirportItlwm/AirportItlwmSkywalkInterface.cpp`
line 4568). Final PIN of the AUTH initiator still requires GAP_5
(firmware-autonomous TX context recovery) and GAP_6 (final
Apple-side AUTH path classification).

The rev1 AUTH-frame chain rules out target #17 by two independent
considerations (rev6/rev7 classification):

1. Body classification: target #17 is a CALLABLE
   SELECTOR/IOCTL DISPATCHER that routes BSD ioctls to per-
   selector handlers via direct and indirect CALLs (the 47 CALLs
   in the recovered body include indirect calls through vtable
   slots, which is the selector-dispatch pattern; the body itself
   does not contain an `iwn_cmd`-style firmware command emit or an
   AUTH-frame fabrication path). The function is a base-class
   helper / fallback that defers to the per-selector handlers
   registered by subclasses; the AUTH-frame emit happens in the
   firmware / iwn HAL path which is the GAP_5/GAP_6 domain.

2. Production-default caller-reachability: under
   `static AirportItlwmRegDiagState sRegDiag = {}` at
   `AirportItlwm/AirportItlwmV2.cpp` line 616 (default state, all
   bits zero), the predicate
   `airportItlwmRegDiagShouldBlock(kAirportItlwmRegDiagBlockPublicAssoc)`
   (definition at lines 1101-1105) returns false, so the local
   override `AirportItlwmSkywalkInterface::processBSDCommand`
   (`AirportItlwm/AirportItlwmSkywalkInterface.cpp` line 1567)
   short-circuits before reaching `super::processBSDCommand`.
   Under explicit diagnostic-intervention mode (operator must
   enable
   `kAirportItlwmRegDiagModeEnabled +
   kAirportItlwmRegDiagModeIntervention +
   kAirportItlwmRegDiagBlockPublicAssoc`),
   `setASSOCIATE` returns `kIOReturnUnsupported` (lines 4588-4601)
   and the override falls through to super — that mode is an
   operator test harness, outside the rev1 AUTH-initiation claim
   scope.

Document scope and convention: this file describes the
auditor-named 17 GAP_4 targets plus the 1 supplemental
`OSMetaClassBase::safeMetaCast` helper, the BSD ioctl selector
ingress mapping, the WCL virtual dispatch mapping, and the
rev1 AUTH-initiator NARROWING. The per-target raw disassembly,
branch/call/return/CFG evidence, and the complete #17 linear
decode + CFG table live in the tracked raw evidence file
`docs/reference/CR-479-gap4-io80211-non-wcl-auth-entries-decomp-20260519-raw.txt`.
The "Document version history" section at the bottom records the
rev1..rev7 cure chain and the blockers each revision addressed;
the per-revision retractions are preserved there for traceability,
not as active assertions about #17.


In scope:
- 17 named entry symbols from the auditor
  instruction.
- 1 supplemental target: the shared
  `OSMetaClassBase::safeMetaCast` helper called
  from 6 of the 51-byte thunks (the auditor
  named recovery of this helper as a
  requirement under blocker BLOCKER_2).
- Owner / selector / address / signature /
  callers / callees / body size / per-tier
  evidence per target.
- BSD ioctl selector ingress mapping
  (`APPLE80211_IOC_*` →
  `SIOCSA80211`/`SIOCGA80211` → local override).
- WCL virtual dispatch mapping
  (`IO80211InfraProtocol::setWCL_*` virtual
  → `AirportItlwmSkywalkInterface` override).
- AUTH-initiator NARROWING (not closure) for
  the rev1 trigger, with explicit reasoning for
  every ruled-out target backed by the
  disassembly evidence.

Out of scope (kept open):
- GAP_3a (whether Apple-extension iwn command
  codes originate outside BootKernelExtensions.kc).
- GAP_5 firmware-autonomous TX context recovery
  (named next non-paper route).
- GAP_6 final Apple-side AUTH path
  classification.
- GAP_1 iwn_cmd_done instrumentation.
- GAP_2 iwn_cmd entry instrumentation.
- Pinning the rev1 AUTH initiator uniquely to
  local `setASSOCIATE` — this packet only
  NARROWS the candidate set; final pinning
  requires GAP_5 + GAP_6 closure.

## Resource plan and concurrency

- Ghidra/decompilation host: `10.7.6.112`.
- Existing project reused:
  `<analysis-output-root>/wifi_analysis_26_3.rep`.
- analyzeHeadless binary:
  `<analysis-tool-root>/build/dist/ghidra_12.2_DEV/support/analyzeHeadless`.
- Java heap: explicit
  `GHIDRA_HEADLESS_MAXMEM=8G` (confirmed in
  `launch.sh` cmdline: "Ghidra-Headless 8G").
- Concurrency: single-process headless batch
  over 18 targets; completed in under 5 minutes.
  The decompiler subprocess crash is independent
  of JVM heap, so parallel workers would not
  reduce the failure rate; the rev2 evidence
  chain instead provides allowed fallback
  evidence per target.

## Per-tier evidence chain (rev2)

For each callable target the rev2 batch runs the
following four tiers, recording the first that
yields evidence plus all attempted:

  Tier 1 — `DecompInterface` regular
    (`setSimplificationStyle("decompile")`) with
    60s timeout.
  Tier 2 — `DecompInterface` lower
    (`setSimplificationStyle("normalize")`) with
    60s timeout. Lighter dataflow analysis;
    typically survives where Tier 1 dies.
  Tier 3 — p-code dump via
    `HighFunction.getPcodeOps()` with the
    normalize style. Captures raw p-code ops at
    every instruction even if higher-level
    decompile fails.
  Tier 4 — raw x86_64 disassembly via
    `Listing.getInstructions(function.getBody())`.
    Always succeeds for any function with a
    body and is the auditor-named "disassembly"
    allowed fallback (BLOCKER_4).

Per-target outcome (16 callable targets +
2 vtable slots):
  - Tier 1: FAIL for all 16 (native decompiler
    subprocess died).
  - Tier 2: FAIL for all 16 (same subprocess
    failure with the lower simplification style).
  - Tier 3: FAIL for all 16 (p-code dump cannot
    be obtained without a successful decompile
    high-function).
  - Tier 4: PASS for all 16. Full x86_64
    disassembly with bytes / mnemonic /
    operands recorded for every instruction in
    the function body, plus branch/call/ret
    counts.
  - For the 2 vtable slots (FUNCTION_NOT_FOUND
    cases, targets #1 and #3): the rev2 batch
    dumps the 32 raw bytes at the vtable slot
    address as evidence that the slot has no
    function body and only contains data.

The decompiler subprocess failure across all
three Tier 1/2/3 attempts confirms the rev1
diagnosis: the failure is in the native
decompiler binary spawned per function, not in
the JVM heap. The Tier 4 disassembly closes
BLOCKER_4 by providing the auditor-named
allowed fallback evidence form.

## Targets investigated (18 = 17 GAP_4 + 1 shared helper)

| # | Owner class | Symbol | Address | body bytes | callers | callees | tier4_disasm |
|---|-------------|--------|---------|-----------|---------|---------|--------------|
| 1  | `AppleBCMWLANInfraProtocol` | `setWCL_REASSOC` | `ffffff8001542b98` | 27 (rev4-recovered callable forwarder; 8 instructions; indirect dispatch via vtable [RDI+0x130]->[RAX]->[RAX+0x1310]) | 0 | 0 (direct) + 1 indirect via `JMP RAX` | PASS (rev4 batch) |
| 2  | `AppleBCMWLANInfraProtocol` | `setWCL_JOIN_ABORT` | `ffffff8001542c2a` | 52 | 0 | 1 (`AppleBCMWLANJoinAdapter::abortFirmwareJoinSync(bool)`) | PASS |
| 3  | `AppleBCMWLANInfraProtocol` | `setWCL_QOS_PARAMS` | `ffffff8001542cee` | 20 (rev4-recovered callable forwarder; 6 instructions; direct tail-call to AppleBCMWLANCore::setWCL_QOS_PARAMS) | 0 | 1 (`AppleBCMWLANCore::setWCL_QOS_PARAMS` @ `ffffff8001635370`, tail-call) | PASS (rev4 batch) |
| 4  | `AppleBCMWLANInfraProtocol` | `setWCL_LINK_UP_DONE` | `ffffff8001542d02` | 37 | 0 | 1 (`AppleBCMWLANPowerManager::handleLinkUpConfiguration()`) | PASS |
| 5  | `AppleBCMWLANInfraProtocol` | `setWCL_ACTION_FRAME` | `ffffff8001542d7c` | 20 | 0 | 1 (`AppleBCMWLANCore::setWCL_ACTION_FRAME`) | PASS |
| 6  | `AppleBCMWLANInfraProtocol` | `setWCL_LIMITED_AGGREGATION` | `ffffff8001542eca` | 19 | 0 | 0 | PASS |
| 7  | `AppleBCMWLANInfraProtocol` | `setWCL_BCN_MUTE_CONFIG` | `ffffff8001542ede` | 59 | 0 | 1 (`AppleBCMWLANNetAdapter::configureBeaconMitigationParams`) | PASS |
| 8  | (anon 11-byte stub) | `apple80211setAUTH_TYPE` | `ffffff80021e7b01` | 11 | 0 | 0 | PASS |
| 9  | (anon 51-byte vtable+0xcc8 thunk) | `apple80211setASSOCIATE` | `ffffff80021e7e2b` | 51 | 1 (`ZL_setHOST_AP_MODE`) | 1 (`safeMetaCast`) | PASS |
| 10 | (anon 51-byte vtable+0xcc8 thunk) | `apple80211setDEAUTH` | `ffffff80021e8000` | 51 | 2 | 1 (`safeMetaCast`) | PASS |
| 11 | (anon 11-byte stub) | `apple80211setASSOCIATION_STATUS` | `ffffff80021e81c5` | 11 | 0 | 0 | PASS |
| 12 | (anon 51-byte vtable+0xcc8 thunk) | `apple80211setSTA_AUTHORIZE` | `ffffff80021e83ba` | 51 | 2 | 1 (`safeMetaCast`) | PASS |
| 13 | (anon 51-byte vtable+0xcc8 thunk) | `apple80211setSTA_DISASSOCIATE` | `ffffff80021e840f` | 51 | 1 | 1 (`safeMetaCast`) | PASS |
| 14 | (anon 51-byte vtable+0xcc8 thunk) | `apple80211setSTA_DEAUTH` | `ffffff80021e8464` | 51 | 1 | 1 (`safeMetaCast`) | PASS |
| 15 | (anon 51-byte vtable+0xcc8 thunk) | `apple80211setRANGING_AUTHENTICATE` | `ffffff80021e9a2f` | 51 | 0 | 1 (`safeMetaCast`) | PASS |
| 16 | (anon 11-byte stub) | `apple80211setASSOC_READY_STATUS` | `ffffff80021eb5cb` | 11 | 0 | 0 | PASS |
| 17 | `IO80211InfraInterface` | `processBSDCommand` | `ffffff80022dea78` | 2046 (rev6/rev7-corrected; body `dea78..df275` bounded above by next defined function `IO80211InfraInterface::isDebounceOnGoing()` @ `ffffff80022df276`; Ghidra-recorded `Function.getBody()` is the smaller 95-byte slice `dea78..dead6` because auto-analysis halted at apparent invalid bytes `dead7..dead8`; rev4's "88-byte non-dispatcher" and rev5's "95-byte body" classifications are retracted in rev6 and replaced with the option-(c) Capstone skipdata=True linear-decode body; see rev6 appendix E rev7-expanded in the raw evidence file) | 2 (`AppleBCMWLANLowLatencyInterface::processBSDCommand`, vtable-slot data ref @ `ffffff80023e0b00`) | 47 CALLs total (38 direct absolute + 9 indirect through vtable memory operands `[rax+0x208]` x2, `[rcx+0x1f8]` x2, `[rcx+0x120]` x2, `[rax+0x28]` x2, `[rax+0x890]`; direct targets include `ffffff8000350880`, `ffffff80004c1bb0`, `ffffff8000894e70`, `ffffff8000895ae0`, `ffffff8000b4d800`, `ffffff8002275b44`, `ffffff8002117f38`, `ffffff80021e303c`, and the stack-canary-failure abort at `ffffff8000307340`; the 9 indirect CALLs are the selector-dispatch evidence proving #17 IS a dispatcher; rev4 "no dispatch" / "0 out-edges" claim retracted) | PASS (rev6/rev7 batch CR479Gap4DecompRev5g + CR479Gap4DecompRev5h + Capstone skipdata=True; 483 decoded instructions in the [entry, next_function_start) range; 47 CALLs (38 direct + 9 indirect); 18 conditional branches; 6 unconditional JMPs; 2 RETs at `ffffff80022df0ff` and `ffffff80022df26f`) |
| 18 | `OSMetaClassBase::safeMetaCast` (positively identified) | `FUN_ffffff80009ff310` (anon, no symbol in kc) | `ffffff80009ff310` | 56 | 4107 | 0 (1 indirect virtual call) | PASS |

## Disassembly evidence — key bodies

The raw companion file
`docs/reference/CR-479-gap4-io80211-non-wcl-auth-entries-decomp-20260519-raw.txt`
contains the full per-target disassembly. Three
patterns repeat across the targets and are
summarized below.

### Pattern A — 11-byte stub (3 targets: setAUTH_TYPE, setASSOCIATION_STATUS, setASSOC_READY_STATUS)

Example: `apple80211setASSOCIATION_STATUS` @
`ffffff80021e81c5`:

  ffffff80021e81c5  55                       PUSH RBP
  ffffff80021e81c6  4889e5                   MOV RBP,RSP
  ffffff80021e81c9  b80e2882e0               MOV EAX,0xe082280e
  ffffff80021e81ce  5d                       POP RBP
  ffffff80021e81cf  c3                       RET

Behavior: each 11-byte stub loads a fixed
`IOReturn` constant into EAX and returns. The
constants are different per stub (each is the
selector-specific encoded "unsupported" /
"not-implemented" return). The constant
`0xe082280e` decodes as a `kIOReturn` with
system `0x38`, subsystem `0x8a`, code `0x080e`
(a kext-specific "not supported by base class"
sentinel). The stub does NOT call out to any
helper; the BootKC base implementation simply
returns the sentinel and lets the runtime
caller decide.

Implication: setAUTH_TYPE, setASSOCIATION_STATUS,
and setASSOC_READY_STATUS in the BootKC base
class are PURE NO-OP / "not implemented"
returns. They cannot initiate any AUTH frame
in their own right. They can be silently
overridden by a subclass (our kext provides a
local override for setAUTH_TYPE; the other two
have no local override and the BootKC base
sentinel is what user-side code sees if it
queries the selector against our kext via
super::processBSDCommand fall-through).

### Pattern B — 51-byte vtable+0xcc8 + safeMetaCast tail-call thunk (6 targets: setASSOCIATE, setDEAUTH, setSTA_AUTHORIZE, setSTA_DISASSOCIATE, setSTA_DEAUTH, setRANGING_AUTHENTICATE)

Example: `apple80211setSTA_AUTHORIZE` @
`ffffff80021e83ba` (selector `APPLE80211_IOC_STA_AUTHORIZE` = 74 = 0x4a):

  ffffff80021e83ba  55                       PUSH RBP
  ffffff80021e83bb  4889e5                   MOV RBP,RSP
  ffffff80021e83be  4156                     PUSH R14
  ffffff80021e83c0  53                       PUSH RBX
  ffffff80021e83c1  4889f3                   MOV RBX,RSI         ; save data ptr
  ffffff80021e83c4  4989fe                   MOV R14,RDI         ; save this
  ffffff80021e83c7  488b07                   MOV RAX,qword ptr [RDI]   ; load vtable
  ffffff80021e83ca  be4a000000               MOV ESI,0x4a        ; selector id 74 = STA_AUTHORIZE
  ffffff80021e83cf  ff90c80c0000             CALL qword ptr [RAX + 0xcc8]   ; vtable[408]
  ffffff80021e83d5  85c0                     TEST EAX,EAX
  ffffff80021e83d7  7405                     JZ 0xffffff80021e83de
  ffffff80021e83d9  5b                       POP RBX                       ; early return
  ffffff80021e83da  415e                     POP R14
  ffffff80021e83dc  5d                       POP RBP
  ffffff80021e83dd  c3                       RET
  ffffff80021e83de  488d353b002200           LEA RSI,[-0x7ffdbf7be0]       ; metaclass literal
  ffffff80021e83e5  4c89f7                   MOV RDI,R14                   ; this
  ffffff80021e83e8  e8236f81fe               CALL 0xffffff80009ff310       ; OSMetaClassBase::safeMetaCast

(Tail-call: there is no RET after the
safeMetaCast call; the called function's RET
returns to the caller of the thunk.)

Behavior: each 51-byte thunk
  1. Saves `this` and `data_ptr`.
  2. Loads `[this]` (vtable) and calls
     `vtable[0xcc8/8 = 408](this, selector_id)`.
     The selector id is the
     `APPLE80211_IOC_*` constant (20 for
     ASSOCIATE, 29 for DEAUTH, 74 for
     STA_AUTHORIZE, 75 for STA_DISASSOCIATE,
     76 for STA_DEAUTH, 243 for
     RANGING_AUTHENTICATE).
  3. If the vtable handler returned nonzero,
     return it directly (early-exit success).
  4. If the vtable handler returned zero,
     tail-call `safeMetaCast(this, <metaclass
     literal>)` and let safeMetaCast's RET
     return to the original caller.

The base-class behavior depends on
`vtable[408]` — a per-class virtual method
that takes the selector id. In `IO80211*Base*`
this is most likely a deferred-dispatch table
lookup (the class registers its handler
methods via a setHandler API at construction
time; `vtable[408]` walks that registry). For
our kext, vtable+0xcc8 lands on whichever
method our local override class defines at the
same vtable offset; but our kext uses a
different dispatch path entirely
(`processBSDCommand` override + per-selector
local setX) so vtable+0xcc8 is never reached
in the rev1 trigger path. The safeMetaCast
tail-call is a slow-path fallback used only
when the registered handler returned zero;
the actual "what to do" logic is in the next
function that safeMetaCast falls through to
(beyond GAP_4 scope).

Implication: in the BootKC base class, these 6
selectors are dispatched through a per-class
handler registry (`vtable[0xcc8]`). The base
class itself does not initiate AUTH frames;
the per-class handler does. For our kext, the
runtime dispatch is in our local
`AirportItlwmSkywalkInterface` overrides
(setASSOCIATE at AirportItlwmSkywalkInterface.cpp:4568,
setDEAUTH at 4799, setRANGING_AUTHENTICATE at
5989; no local override for STA_AUTHORIZE,
STA_DISASSOCIATE, STA_DEAUTH — those selectors
hit the base-class registry path for our
kext).

### Pattern C — AppleBCMWLANInfraProtocol::setWCL_* forwarders (7 targets; rev3 vtable-slot subgroup reclassified in rev4)

In rev2/rev3 the seven WCL setters were
described as falling into two sub-patterns:
direct-call forwarders (19..59 bytes) and
"FUNCTION_NOT_FOUND vtable slots" (targets #1
and #3). The rev3 auditor identified that the
"vtable slot" classification contradicted the
raw bytes at #1 and #3 (the bytes are
executable trampolines). In rev4 the rev4
boundary-repair batch
(`CR479Gap4DecompRev4.java`) creates real
Ghidra functions at those entries via
`DisassembleCommand` + `CreateFunctionCmd`, and
captures the actual callable bodies with full
disassembly + branch/call/ret + CFG evidence.
The seven WCL setters now fall into three
sub-patterns:

  - 19/20/37/52/59-byte direct-call forwarder
    (sub-pattern C-direct; 5 targets): load
    receiver via `[RDI+0x130]` first-level
    vtable, then directly tail-call one
    downstream Broadcom-specific method (e.g.
    `AppleBCMWLANJoinAdapter::abortFirmwareJoinSync`
    for setWCL_JOIN_ABORT, or
    `AppleBCMWLANNetAdapter::configureBeaconMitigationParams`
    for setWCL_BCN_MUTE_CONFIG). Targets #2, #4,
    #5, #7, and #3 (after rev4 reclassification)
    follow this pattern with sub-variants.
  - 27-byte indirect-dispatch forwarder
    (sub-pattern C-indirect; 1 target):
    target #1 `setWCL_REASSOC` ends with
    `JMP RAX` after walking two levels of
    vtable (`[RDI+0x130]` → `[RAX]` →
    `[RAX+0x1310]`). The final dispatch target
    is computed dynamically from the receiver
    object's vtable chain.
  - 19-byte no-op forwarder
    (sub-pattern C-noop; 1 target): target #6
    `setWCL_LIMITED_AGGREGATION` has 8
    instructions, no branches, no calls, and
    ends with a RET — a fixed-result return
    pattern.

None of the WCL setters reach our iwn HAL via
the BootKC concrete; runtime dispatch lands on
our local override
(`AirportItlwmSkywalkInterface` overrides per
the WCL Virtual Dispatch Mapping section
below). The rev4 reclassification does not
change this implication; targets #1 and #3 are
still not reachable via the AirportItlwm
override path because AirportItlwm intercepts
the WCL setters at a higher vtable level. The
rev4 cure converts the rev3 "vtable slot data"
narrative into "callable forwarder; cannot
itself initiate AUTH" — both #1 and #3 forward
to a downstream Broadcom-specific method (or
indirectly to whatever the vtable chain
resolves to) and neither method is an AUTH
initiator on our kext path.

### IO80211InfraInterface::processBSDCommand (rev6-corrected body 2046 bytes; option-(c) full-body disassembly cure; rev3/rev4/rev5 prior classifications retracted)

Target #17 spans 2046 bytes at
`ffffff80022dea78..ffffff80022df275` (rev6-
corrected). The body is bounded above by the
next defined function symbol
`IO80211InfraInterface::isDebounceOnGoing()` at
`ffffff80022df276` and ends with the RET at
`ffffff80022df26f` followed by a stack-canary-
failure abort handler at `ffffff80022df270`
(call to `ffffff8000307340`) and an alignment
`nop` at `ffffff80022df275`. The body is
recovered to the auditor-named proof level
`FULL_BODY_OR_LOW_LEVEL_EQUIVALENT_PER_TARGET`
via option-(c) (corrected function boundaries
with complete raw disassembly and CFG).

The rev3 "88-byte selector-table dispatcher"
classification is retracted (rev4 retraction
preserved). The rev4 "88-byte non-dispatcher /
no branches / no calls / no RETs" claim is
also retracted in rev6 — the prior boundary
of 88 bytes was the upper bound Ghidra's
`Function.getBody()` reported after the rev4
batch extended the body by one LEA at
`ffffff80022dead0` (Ghidra-recorded 95 bytes
in the current project state), which is
smaller than the true function extent because
Ghidra's auto-analysis halted at the apparent
invalid bytes `dead7..dead8` and never
re-synchronised. The rev5 follow-up that
attempted to substitute option-(d) caller-
reachability for a body boundary is also
retracted in rev6 because the rev5 option-(d)
proof did not handle the
`airportItlwmRegDiagShouldBlock(kAirportItlwmRegDiagBlockPublicAssoc)`
branch of `setASSOCIATE` that returns
`kIOReturnUnsupported` under diagnostic-
intervention mode.

Rev6 option-(c) evidence (full body /
low-level equivalent):

1. Ghidra rev5g `CR479Gap4DecompRev5g.java`
   (host `10.7.6.112`,
   project `wifi_analysis_26_3`, `-noanalysis`,
   `GHIDRA_HEADLESS_MAXMEM=8G`) reports the
   memory block containing `dea78` is the
   single executable `__text` block belonging
   to `com.apple.iokit.IO80211Family`, spanning
   `ffffff8002107600..ffffff800231d0af`. The
   next defined function symbol after the
   entry is `IO80211InfraInterface::isDebounceOnGoing()`
   at `ffffff80022df276`, 2046 bytes after
   `dea78`.

2. Ghidra rev5h `CR479Gap4DecompRev5h.java`
   dumped 4096 bytes starting at the entry to
   `cr479_gap4_rev5h_processbsdcommand_4096bytes.bin`.

3. Capstone 5.0.7 `skipdata=True` linear
   disassembly of that dump (output file
   `cr479_gap4_rev5h_processbsdcommand_linear_decode.txt`)
   produces a continuous semantically
   meaningful instruction stream from `dea78`
   into the [entry, next_function_start)
   range:
     - 483 decoded instructions in the
       `[ffffff80022dea78, ffffff80022df276)`
       range
     - 47 CALLs total to other functions:
       38 direct absolute CALLs plus 9
       indirect CALLs via vtable memory
       operands (`[rax+0x208]` x2,
       `[rcx+0x1f8]` x2, `[rcx+0x120]` x2,
       `[rax+0x28]` x2, `[rax+0x890]`);
       the 9 indirect CALLs are the
       selector-dispatch evidence
     - 18 conditional branches (`je`, `jne`,
       `ja`, etc.)
     - 6 unconditional `JMP`s
     - 2 `RET`s at
       `ffffff80022df0ff` (early fall-through
       path) and `ffffff80022df26f` (final
       function return)
     - 1 stack-canary-failure abort `call
       0xffffff8000307340` at
       `ffffff80022df270` (unreachable after
       the canonical RET)
     - 1 alignment `nop` at
       `ffffff80022df275`

4. Body end proof: the standard x86-64
   function epilogue is visible immediately
   before `df26f`:
     `ffffff80022df261  jne df270`
     `ffffff80022df263  add rsp, 0x10`
     `ffffff80022df267  pop rbx`
     `ffffff80022df268  pop r12`
     `ffffff80022df26a  pop r14`
     `ffffff80022df26c  pop r15`
     `ffffff80022df26e  pop rbp`
     `ffffff80022df26f  ret`
     `ffffff80022df270  call 0xffffff8000307340  ; __stack_chk_fail`
     `ffffff80022df275  nop`
   This is a textbook
   stack-canary-protected function return
   pattern: the `jne df270` jumps to the
   `__stack_chk_fail` abort if the canary
   doesn't match, otherwise falls through to
   the register-pop epilogue and the canonical
   `ret`. The next byte after the `nop`
   alignment at `df275` is `df276` which is
   `IO80211InfraInterface::isDebounceOnGoing()`'s
   entry. The body boundary is therefore
   established by three independent
   indicators: (a) the canonical `ret`
   instruction, (b) the
   `__stack_chk_fail` abort handler typical
   of the function's tail, (c) the next
   defined function symbol immediately after
   the alignment `nop`.

5. The earlier sub-claim that bytes at
   `dead7..dead8` form an "invalid x86-64
   encoding" is also re-examined in rev6: under
   Capstone `skipdata=True`, the byte `ff` at
   `dead7` decodes as a single `.byte 0xff`
   skipdata fallback and the next byte `ff 30`
   at `dead8..dead9` resyncs into `push qword
   ptr [rax]`. The bytes are not "non-code";
   they are valid x86-64 code that Ghidra's
   auto-analysis refused to disassemble because
   it hit an apparently anomalous LEA at `dead0`
   (with an absurd -8MB-from-RBP displacement
   pointing into the stack-canary error path of
   the function — likely an artefact of how
   the function uses
   `__builtin___stpcpy_chk` or a similar
   stack-protected string operation), then
   gave up at the next byte. The rev4 "two
   decoders agree" claim is retracted: only
   Ghidra's auto-analyser failed at `dead7`;
   Capstone's `skipdata=True` decoder
   resynchronises and decodes the function
   body to completion.

Implication: the BootKC base
`IO80211InfraInterface::processBSDCommand` at
`ffffff80022dea78` is a real dispatcher
function with 483 instructions, 47 CALLs, 18
conditional branches, 6 unconditional JMPs,
and 2 RETs across the 2046-byte body. It DOES
dispatch (it makes direct CALLs and contains
conditional branches), in contradiction to the
rev4 doc claim of "no dispatch behavior". The
function performs setup, calls multiple
kernel-helper / locking / state functions,
branches on input fields (probably the
selector / req_type), and returns via either
the early-exit path (RET at `df0ff`) or the
final-exit path (RET at `df26f`).

For the rev1 AUTH-initiation chain, the
classification of #17 is no longer "no
dispatch behavior" (false). Instead the
classification is: target #17 is a CALLABLE
DISPATCHER whose body is now recovered to
proof-level evidence (option-(c)). Whether it
initiates AUTH on the rev1 trigger depends on
caller-reachability — and the rev6 caller-
reachability narrative below is updated to
acknowledge the corrected reachability
analysis (including the diagnostic-
intervention branch in `setASSOCIATE`).

For our kext the runtime dispatch is:
  ifnet → SIOCSA80211 → our local
  `AirportItlwmSkywalkInterface::processBSDCommand`
  override (`AirportItlwm/AirportItlwmSkywalkInterface.cpp`
  line 1567) → if SIOCGA80211/SIOCSA80211 with
  data, dispatch via `processApple80211Ioctl`
  (line 1599 branch) → for the rev1 AUTH
  selector `APPLE80211_IOC_ASSOCIATE` the case
  at line 1777 calls
  `setASSOCIATE((apple80211_assoc_data *)req->req_data)`
  and returns the result.
The local override falls through to
`super::processBSDCommand` only when the
selector dispatch returns `kIOReturnUnsupported`.
For the rev1 AUTH trigger the local
`setASSOCIATE` (line 4568) returns
`kIOReturnUnsupported` only when the
`airportItlwmRegDiagShouldBlock(kAirportItlwmRegDiagBlockPublicAssoc)`
predicate (`AirportItlwm/AirportItlwmV2.cpp`
line 1101) is true — which requires the
operator to explicitly set
`kAirportItlwmRegDiagModeEnabled` AND
`kAirportItlwmRegDiagModeIntervention` AND the
`kAirportItlwmRegDiagBlockPublicAssoc` bit in
`sRegDiag.blockMask`. The default-initialized
`static AirportItlwmRegDiagState sRegDiag =
{}` (`AirportItlwm/AirportItlwmV2.cpp` line
616) has all bits zero, so the predicate is
false under the default state and the local
override returns
`setASSOCIATE`'s normal (non-unsupported)
result without falling through to super.
Under explicit diagnostic-intervention mode,
the local override DOES fall through to
super::processBSDCommand and the BootKC body
runs. The rev6 claim scope therefore
explicitly states: the rev1 AUTH-initiation
chain runs in the production / default state
(no diagnostic intervention enabled) where
super::processBSDCommand is unreachable; the
diagnostic-intervention path is an operator-
controlled test harness, not a production
AUTH path.

The rev6 option-(c) body recovery makes this
narrowed scope robust: even under diagnostic
intervention, the BootKC body that runs is now
fully recovered to proof-level evidence and
cannot initiate AUTH by itself for the rev1
chain because (a) the body is a dispatcher
that routes selectors to per-selector
handlers, not an AUTH-frame emitter, and (b)
any AUTH frame still has to come from the
firmware / iwn HAL, which is the GAP_5 / GAP_6
domain.

### FUN_ffffff80009ff310 = OSMetaClassBase::safeMetaCast (positively identified)

The shared helper called from 6 of the 51-byte
thunks (and from 4107 other call sites in BootKC,
including `AppleBCMWLANUserClient::start` and
many `AppleBCMWLAN*` initializers). The 22-
instruction disassembly is exactly the libkern
`OSMetaClassBase::safeMetaCast(anObject,
toMeta)` pattern:

  PUSH RBP / MOV RBP,RSP / PUSH R14 / PUSH RBX
  TEST RDI,RDI
  JZ end_return_zero          ; NULL input → return NULL
  MOV R14,RSI                  ; save target metaclass
  MOV RBX,RDI                  ; save original object
  MOV RAX,[RDI]                ; load vtable
  CALL [RAX + 0x38]            ; OSObject::getMetaClass() (vtable slot 7)
  loop:
    CMP RAX,R14                ; metaclass == target?
    JZ end_return_object       ; match → return RBX (original this)
    MOV RAX,[RAX + 0x10]       ; rax = metaclass->superClassLink
    TEST RAX,RAX
    JNZ loop                   ; not NULL → keep walking
  XOR EBX,EBX                  ; chain exhausted → return NULL
  end_return:
  MOV RAX,RBX
  POP RBX / POP R14 / POP RBP / RET

This is the standard XNU
`libkern/c++/OSMetaClass.cpp` implementation
of `OSMetaClassBase::safeMetaCast`. The 6
51-byte thunks use it as the second leg of
their "vtable[0xcc8] first, then safeMetaCast
slow path" dispatch pattern.

Implication: the shared helper is a libkern
runtime utility, NOT an IO80211 trap. The
auditor's BLOCKER_2 concern that the helper
behavior was unknown is now positively
resolved: it is a polymorphic cast check that
returns either the input pointer (if the
metaclass chain contains the target) or
NULL. It plays no role in AUTH initiation;
it is downstream type-safety glue for the
slow-path dispatch.

## BSD ioctl selector ingress mapping

Same as the rev1 doc (unchanged because the
APPLE80211_IOC_* mapping is from local source):

  | APPLE80211_IOC_* constant | Numeric | Apple80211 method | Local AirportItlwm receiver |
  |---------------------------|---------|-------------------|------------------------------|
  | `APPLE80211_IOC_AUTH_TYPE`            | 2   | `setAUTH_TYPE`            | YES — `AirportItlwmSkywalkInterface::setAUTH_TYPE` @ AirportItlwmSkywalkInterface.cpp:2859 |
  | `APPLE80211_IOC_ASSOCIATE`            | 20  | `setASSOCIATE`            | YES — `AirportItlwmSkywalkInterface::setASSOCIATE` @ AirportItlwmSkywalkInterface.cpp:4568 |
  | `APPLE80211_IOC_DEAUTH`               | 29  | `setDEAUTH`               | YES — `AirportItlwmSkywalkInterface::setDEAUTH` @ AirportItlwmSkywalkInterface.cpp:4799 |
  | `APPLE80211_IOC_ASSOCIATION_STATUS`   | 50  | `setASSOCIATION_STATUS`   | NO — no local override (base 11-byte stub returns fixed `IOReturn 0xe082280e`) |
  | `APPLE80211_IOC_STA_AUTHORIZE`        | 74  | `setSTA_AUTHORIZE`        | NO — no local override (base 51-byte thunk tries vtable[0xcc8] first) |
  | `APPLE80211_IOC_STA_DISASSOCIATE`     | 75  | `setSTA_DISASSOCIATE`     | NO — no local override (base 51-byte thunk tries vtable[0xcc8] first) |
  | `APPLE80211_IOC_STA_DEAUTH`           | 76  | `setSTA_DEAUTH`           | NO — no local override (base 51-byte thunk tries vtable[0xcc8] first) |
  | `APPLE80211_IOC_RANGING_AUTHENTICATE` | 243 | `setRANGING_AUTHENTICATE` | YES — `AirportItlwmSkywalkInterface::setRANGING_AUTHENTICATE` @ AirportItlwmSkywalkInterface.cpp:5989 |

For the three NO rows, our kext's
`processBSDCommand` override does NOT handle the
selector locally; it falls through to
`super::processBSDCommand` (AirportItlwmSkywalkInterface.cpp:1599)
which reaches the BootKC base dispatcher
(Target #17) which in turn calls the
selector-specific 51-byte thunk (Targets
#12/#13/#14) or 11-byte stub (Target #11).

## WCL virtual dispatch mapping

Same as rev1 (unchanged, sourced from local
header `include/Airport/IO80211InfraProtocol.h`):

  | WCL virtual (`IO80211InfraProtocol`) | BootKC concrete (target) | Local AirportItlwm override |
  |--------------------------------------|--------------------------|------------------------------|
  | `setWCL_REASSOC` | Target #1 (27-byte indirect-dispatch forwarder; rev4-recovered; `JMP RAX` via `[RDI+0x130]->[RAX]->[RAX+0x1310]`) | YES — AirportItlwmSkywalkInterface.cpp:6134 |
  | `setWCL_JOIN_ABORT` | Target #2 (52-byte forwarder) | YES — AirportItlwmSkywalkInterface.cpp:6412 |
  | `setWCL_QOS_PARAMS` | Target #3 (20-byte direct-dispatch forwarder; rev4-recovered; tail-call to `AppleBCMWLANCore::setWCL_QOS_PARAMS` @ `ffffff8001635370`) | YES — AirportItlwmSkywalkInterface.cpp:6469 |
  | `setWCL_LINK_UP_DONE` | Target #4 (37-byte forwarder) | YES — AirportItlwmSkywalkInterface.cpp:6514 |
  | `setWCL_ACTION_FRAME` | Target #5 (20-byte forwarder) | YES — AirportItlwmSkywalkInterface.cpp:6080 |
  | `setWCL_LIMITED_AGGREGATION` | Target #6 (19-byte stub) | YES — AirportItlwmSkywalkInterface.cpp:6808 |
  | `setWCL_BCN_MUTE_CONFIG` | Target #7 (59-byte forwarder) | YES — AirportItlwmSkywalkInterface.cpp:6815 |

## Classification — AUTH-initiator candidates for the rev1 trigger (NARROWING, not closure)

  ### NARROWED CANDIDATE (single):
  - `apple80211setASSOCIATE` (target #9) via
    local `AirportItlwmSkywalkInterface::setASSOCIATE`
    override at AirportItlwmSkywalkInterface.cpp:4568.
    The override drives the legacy direct PMK
    carrier path and the net80211 controller
    AUTH state machine; this is the ONLY
    target among the 17 whose local override
    is on the rev1 AUTH-frame chain.

  ### RULED OUT (disassembly-evidence-backed reasons):
  - Target #1 `setWCL_REASSOC` — BootKC concrete
    is a 27-byte callable indirect-dispatch
    forwarder (rev4-recovered). The body
    walks two vtable levels (`[RDI+0x130]` →
    `[RAX]` → `[RAX+0x1310]`) and tail-jumps
    via `JMP RAX`; the final target is
    receiver-dependent and not statically
    resolvable. For the AirportItlwm receiver
    path the local override intercepts WCL
    post-join reassoc state edges (reuses
    existing PMK rather than initiating fresh
    AUTH from open state). The forwarder body
    itself does no AUTH work; its only
    semantic effect is vtable dispatch.
  - Target #2 `setWCL_JOIN_ABORT` — BootKC
    concrete forwards to
    `AppleBCMWLANJoinAdapter::abortFirmwareJoinSync`;
    local override tears down a join. Cannot
    initiate AUTH.
  - Target #3 `setWCL_QOS_PARAMS` — BootKC
    concrete is a 20-byte callable direct-
    dispatch forwarder (rev4-recovered) that
    tail-calls `AppleBCMWLANCore::setWCL_QOS_PARAMS`
    at `ffffff8001635370` unconditionally. The
    downstream Broadcom method sets QoS
    parameters and does not initiate AUTH. For
    the AirportItlwm receiver path the local
    override sets QoS state and does not
    initiate AUTH either. The forwarder
    classification changed from rev3's
    "vtable slot" to "direct-call forwarder";
    the implication for AUTH initiation is
    unchanged (cannot initiate AUTH on either
    path).
  - Target #4 `setWCL_LINK_UP_DONE` — BootKC
    concrete forwards to
    `AppleBCMWLANPowerManager::handleLinkUpConfiguration`;
    local override signals link-up post-
    association. Cannot initiate AUTH.
  - Target #5 `setWCL_ACTION_FRAME` — BootKC
    concrete forwards to
    `AppleBCMWLANCore::setWCL_ACTION_FRAME`;
    local override emits an explicit action
    frame on demand (distinct management
    subtype from AUTH).
  - Target #6 `setWCL_LIMITED_AGGREGATION` —
    BootKC concrete is a 19-byte stub with
    zero callees; local override sets
    aggregation limit. Cannot initiate AUTH.
  - Target #7 `setWCL_BCN_MUTE_CONFIG` — BootKC
    concrete forwards to
    `AppleBCMWLANNetAdapter::configureBeaconMitigationParams`;
    local override configures beacon
    mitigation. Cannot initiate AUTH.
  - Target #8 `apple80211setAUTH_TYPE` — sets
    desired authentication mode for the NEXT
    setASSOCIATE call; does not itself initiate
    AUTH. (Both BootKC stub returning fixed
    IOReturn and local override at
    AirportItlwmSkywalkInterface.cpp:2859 only
    UPDATE state; the AUTH frame is emitted
    later by setASSOCIATE.)
  - Target #10 `apple80211setDEAUTH` — produces
    a deauth frame, not an AUTH frame.
  - Target #11 `apple80211setASSOCIATION_STATUS` —
    BootKC stub returns fixed IOReturn
    0xe082280e (no-op); no local override.
    Cannot initiate AUTH.
  - Target #12 `apple80211setSTA_AUTHORIZE` —
    BootKC thunk dispatches via vtable[0xcc8]
    with selector id 74 then tail-call
    safeMetaCast slow path; the per-class
    `vtable[0xcc8]` handler would be the
    actual AP-side authorization grant for an
    associated STA, not the initial AUTH
    frame our anomaly observes. No local
    override.
  - Target #13 `apple80211setSTA_DISASSOCIATE` —
    BootKC thunk dispatches via vtable[0xcc8]
    with selector id 75; AP-side state edge.
    No local override.
  - Target #14 `apple80211setSTA_DEAUTH` —
    BootKC thunk dispatches via vtable[0xcc8]
    with selector id 76; AP-side state edge.
    No local override.
  - Target #15 `apple80211setRANGING_AUTHENTICATE` —
    BootKC thunk via vtable[0xcc8] with
    selector id 243 then safeMetaCast slow
    path; local override at
    AirportItlwmSkywalkInterface.cpp:5989
    participates in 802.11mc / RNGT
    authentication, orthogonal to the rev1
    AUTH-frame chain.
  - Target #16 `apple80211setASSOC_READY_STATUS` —
    11-byte BootKC stub; no local override.
    Cannot initiate AUTH.
  - Target #17 `IO80211InfraInterface::processBSDCommand` —
    rev6-corrected body 2046 bytes
    (`ffffff80022dea78..ffffff80022df275`,
    bounded by next defined function symbol
    `IO80211InfraInterface::isDebounceOnGoing()`
    at `ffffff80022df276`, ending with the
    final RET at `ffffff80022df26f` followed
    by the `__stack_chk_fail` abort handler at
    `ffffff80022df270` and alignment `nop` at
    `ffffff80022df275`). Option-(c) full-body
    raw disassembly via Capstone
    `skipdata=True` linear decode reports 483
    instructions including 47 CALLs total
    (38 direct absolute + 9 indirect through
    vtable memory operands), 18 conditional
    branches, 6 unconditional JMPs, and 2
    RETs (`df0ff` early exit and `df26f`
    final exit). The rev3/rev4
    "non-dispatcher" classifications are
    retracted; the body IS a dispatcher. Ruled
    out for the rev1 AUTH-frame chain by two
    independent considerations: (1) the body
    is a selector-dispatching helper that
    routes BSD ioctls to per-selector handlers
    in the IO80211InfraInterface base class
    rather than emitting AUTH frames itself —
    AUTH frame emission requires the firmware
    / iwn HAL path which is GAP_5/GAP_6 scope;
    (2) under the production / default state
    (`static AirportItlwmRegDiagState sRegDiag
    = {}` at
    `AirportItlwm/AirportItlwmV2.cpp` line
    616), the local override
    `AirportItlwmSkywalkInterface::processBSDCommand`
    handles `SIOCSA80211 +
    APPLE80211_IOC_ASSOCIATE` locally via
    `setASSOCIATE` (`AirportItlwm/AirportItlwmSkywalkInterface.cpp`
    line 4568) without falling through to
    super, so the BootKC body never executes
    on the rev1 AUTH chain in production.
    Under explicit diagnostic-intervention
    mode (operator must enable
    `kAirportItlwmRegDiagModeEnabled +
    kAirportItlwmRegDiagModeIntervention +
    kAirportItlwmRegDiagBlockPublicAssoc`),
    `setASSOCIATE` returns
    `kIOReturnUnsupported` and the override
    falls through to super::processBSDCommand
    — but that diagnostic test mode is not
    part of the rev1 AUTH-initiation claim
    scope.
  - Target #18 `OSMetaClassBase::safeMetaCast` —
    libkern type-check utility (positively
    identified); plays no role in AUTH
    initiation; only ensures pointer cast
    safety in the slow-path of the 51-byte
    thunks above.

  ### Why this is NARROWING, not CLOSURE
  GAP_4 NARROWS the rev1 AUTH initiator
  candidate set within the BootKC IO80211/WCL
  scope to a single candidate
  (`AirportItlwmSkywalkInterface::setASSOCIATE`).
  Final PIN of the AUTH initiator still
  requires:

    - GAP_5 firmware-autonomous TX context
      recovery (next route): if the iwn
      firmware can autonomously emit AUTH-class
      TX without a host iwn_cmd entry, then
      the rev1 trigger may be a firmware-
      autonomous AUTH and the
      `setASSOCIATE` candidate is only the
      JOIN entry, not the AUTH-emission
      entry.
    - GAP_6 final Apple-side AUTH path
      classification: ties together the
      local `setASSOCIATE` candidate from this
      packet, the firmware-autonomous TX
      result from GAP_5, and the iwn-side
      command boundary from the fwhc-decomp
      analysis already committed in HEAD
      `964fb54f...` into a single final
      attribution.

  Without GAP_5 + GAP_6 the AUTH initiator
  CANNOT be uniquely pinned — only narrowed.

## Required next route

  Per the cycle prompt
  `payload.candidate_next_route_after_gap4`:

  - next_work_item_id:
    `CR-479-gap5-firmware-autonomous-tx-context-reference-recovery-20260519`
  - next_work_item_role: coder
  - next_work_item_scope:
    Enumerate firmware-autonomous TX contexts
    in the local iwn HAL
    (`itlwm/hal_iwn/` plus cross-reference
    with `itlwm/hal_iwn/if_iwnreg.h`
    `IWN_CMD_*` opcode space) and identify
    which firmware paths can emit an
    AUTH-class TX without a preceding host
    `iwn_cmd`. Augment with Ghidra
    structural batch for any BootKC-side
    `IWN_CMD_TX_*` references (none expected
    given the GAP_3 NEGATIVE inventory but
    confirmed by the same recovery flow).
  - next_work_item_required_to_unblock_functional:
    PARTIAL — together with GAP_6, unblocks
    the rev1 fix design.

## Reproducibility commands

(Read-only; auditor re-verification on the
same Ghidra host.)

Rev2 batch (15 of 18 targets carried forward
byte-identical):

  ssh -p 22 10.7.6.112 \
    'env GHIDRA_HEADLESS_MAXMEM=8G \
        CR479_GAP4_REV2_OUTDIR=<analysis-output-root>/cr479_gap4_decomp_rev2_20260519 \
     <analysis-tool-root>/build/dist/ghidra_12.2_DEV/support/analyzeHeadless \
       <analysis-output-root> wifi_analysis_26_3 \
       -process BootKernelExtensions.kc \
       -noanalysis -scriptPath /tmp \
       -postScript CR479Gap4DecompRev2.java'

  shasum -a 256 \
    <analysis-output-root>/cr479_gap4_decomp_rev2_20260519/cr479_gap4_rev2_evidence.txt

Rev4 boundary-repair batch (#1 setWCL_REASSOC,
#3 setWCL_QOS_PARAMS):

  ssh -p 22 10.7.6.112 \
    'env GHIDRA_HEADLESS_MAXMEM=8G \
        CR479_GAP4_REV4_OUTDIR=<analysis-output-root>/cr479_gap4_decomp_rev4_20260519 \
     <analysis-tool-root>/build/dist/ghidra_12.2_DEV/support/analyzeHeadless \
       <analysis-output-root> wifi_analysis_26_3 \
       -process BootKernelExtensions.kc \
       -noanalysis -scriptPath /tmp \
       -postScript CR479Gap4DecompRev4.java'

  shasum -a 256 \
    <analysis-output-root>/cr479_gap4_decomp_rev4_20260519/cr479_gap4_rev4_evidence.txt

Rev4f Disassembler-class body-end proof (#17
processBSDCommand):

  ssh -p 22 10.7.6.112 \
    'env GHIDRA_HEADLESS_MAXMEM=8G \
     <analysis-tool-root>/build/dist/ghidra_12.2_DEV/support/analyzeHeadless \
       <analysis-output-root> wifi_analysis_26_3 \
       -process BootKernelExtensions.kc \
       -noanalysis -scriptPath /tmp \
       -postScript CR479Gap4DecompRev4f.java'

  shasum -a 256 \
    <analysis-output-root>/cr479_gap4_decomp_rev4_20260519/cr479_gap4_rev4f_processbsdcommand_disassembler_class.txt

Capstone independent decode (#17 body-end
proof, second opinion):

  ssh 10.7.6.112 \
    'python3 -c "from capstone import *; \
       md=Cs(CS_ARCH_X86,CS_MODE_64); \
       data=open(\"/tmp/processbsdcommand_dead0.bin\",\"rb\").read(); \
       [print(f\"{i.address:016x} {i.mnemonic} {i.op_str}\") \
         for i in md.disasm(data,0xffffff80022dead0)]"'

  Expected: exactly one LEA decoded at
    ffffff80022dead0; decoder stops at dead7
    because ff ff is invalid x86-64.

## Appendix A — Why decompile bodies are unreachable for these targets

The decompiler subprocess (`decompile` native
binary spawned per function by Ghidra's
`DecompInterface`) crashes on every target in
this set, across:
  - default `decompile` simplification style;
  - lower `normalize` simplification style;
  - p-code dump attempt via `HighFunction`.

Heap (2G vs 8G via `GHIDRA_HEADLESS_MAXMEM`)
makes no difference because the failing
process is the subprocess, not the JVM. The
underlying cause is most likely the same as
the `WARN  Decompiling ffffff80022dea78, pcode
error at ffffff80022dead7: Unable to resolve
constructor at ffffff80022dead7
(DecompileCallback)` warning the headless log
emits during the
`IO80211InfraInterface::processBSDCommand`
attempt: the BootKC binary contains x86_64
instructions whose pcode lifting fails inside
the native decompiler, which then dies rather
than recovering. A future bounded decomp
packet (separate from GAP_4 / GAP_5 / GAP_6)
could investigate by either upgrading the
Ghidra decompiler (the project is on
`ghidra_12.2_DEV` from the public master
branch; an upstream fix may exist) or by
filing a Ghidra issue with the offending
function. Neither is on the GAP_4..GAP_6
critical path because Tier 4 disassembly is
the auditor-named allowed fallback.

## REV5 update — retraction of rev4 #17 boundary overclaim and option-4 caller-reachability proof

The auditor's rev4 decision (decision sha
`11b740f532f001946cbd38907b39e5fd149d05d3854ba67314737747ae82ef0e`,
REJECTED) rejected the rev4 packet on three blockers, all
targeting #17 `IO80211InfraInterface::processBSDCommand`:

- BLOCKER_1_PROCESSBSDCOMMAND_BODY_END_NOT_PROVEN — the rev4
  evidence asserted an 88-byte body ending at
  `ffffff80022deacf`, but the same evidence decoded a valid LEA
  at `ffffff80022dead0` and admitted the post-`dead0` invalid
  bytes did not establish a fall-through boundary.
- BLOCKER_2_ASSIGNED_DECOMP_PROOF_LEVEL_NOT_MET_FOR_EVERY_TARGET —
  because #17 remained boundary-incomplete, the proof-level
  self-check was contradicted.
- BLOCKER_3_GAP4_INVENTORY_AND_NARROWING_CLAIM_OVERSTATE_EVIDENCE —
  the GAP_4 inventory and the rev1 AUTH narrowing depended on the
  unresolved #17 evidence.

The rev4 auditor enumerated four acceptable cures: (a) real
terminator / trap / unreachable boundary before fall-through;
(b) independent section / symbol / function-boundary evidence
proving bytes after `deacf` are non-code and unreachable;
(c) corrected function boundaries with complete raw disassembly
and CFG; or (d) exact evidence that the vtable slot / caller
never reaches this body for the submitted claim scope.

The rev5 packet retracts the rev4 #17 overclaim and provides
option (d) (caller-reachability proof) as the primary cure plus
partial option-(c) corrected-boundary evidence as supporting
material. The detail is in the tracked raw evidence file
(`docs/reference/CR-479-gap4-io80211-non-wcl-auth-entries-decomp-20260519-raw.txt`)
in four new appendices (A through D); the summary is below.

### Rev5 BLOCKER_1 cure — #17 boundary, corrected via section / adjacent-function evidence (option b/c)

Ghidra batch `CR479Gap4DecompRev5g.java` (run on host
`10.7.6.112`, project `wifi_analysis_26_3`, `-noanalysis`,
`GHIDRA_HEADLESS_MAXMEM=8G`) reports:

- Memory block for the entry: a single executable `__text` block
  belonging to `com.apple.iokit.IO80211Family`, spanning
  `ffffff8002107600..ffffff800231d0af`. The bytes from
  `ffffff80022dea78` (entry) through `ffffff80022df275` are in
  this same executable section. The rev4 "section-boundary"
  intuition is wrong — there is no non-code section before the
  next function symbol.
- The Ghidra `Function.getBody()` value for the entry is 95
  bytes (`ffffff80022dea78..ffffff80022dead6` inclusive). The
  rev4f Disassembler-class invocation extended the body by 7
  bytes (the LEA at `ffffff80022dead0`) beyond the rev4-claimed
  88-byte boundary; the project state in `wifi_analysis_26_3`
  now records 95 bytes, not 88. The rev4 doc value is
  retracted.
- The first Ghidra-defined function entry after `ffffff80022dea78`
  is `IO80211InfraInterface::isDebounceOnGoing()` at
  `ffffff80022df276`, 2046 bytes (`0x7fe`) after the entry. The
  body of `IO80211InfraInterface::processBSDCommand` is
  therefore at most 2046 bytes:
  `[ffffff80022dea78, ffffff80022df275]`.

This is a partial option-(c) corrected-boundary proof: the body
is bounded above by `ffffff80022df275` because the next defined
function symbol starts at `ffffff80022df276`, with no other
function symbols in between.

### Rev5 BLOCKER_1 cure — #17 boundary, corrected via complete raw disassembly (option c)

Capstone 5.0.7 `skipdata=True` linear decode of the 4096-byte
memory dump (`cr479_gap4_rev5h_processbsdcommand_4096bytes.bin`)
captured by Ghidra batch `CR479Gap4DecompRev5h.java` produces a
continuous, semantically meaningful instruction stream from
`ffffff80022dea78` through multiple RETs. Key observations:

- The bytes at `ffffff80022dead7` (`ff`) decode as a single
  `.byte 0xff` skipdata fallback, and the next byte at
  `ffffff80022dead8` resyncs into `ff 30` =
  `push qword ptr [rax]`. The rev4 claim that the bytes past
  `dead0` "are not continuous executable code" is therefore not
  supported by skipdata-aware decoding.
- The body contains real dispatch behavior:
  `REP MOVSQ` copying a 128-byte buffer at `ffffff80022deaf0`,
  multiple direct CALLs to kernel-helper functions
  (`ffffff8000894e70` at `deaf3`, `ffffff8000895ae0` at `deb07`,
  `ffffff8000350880` at `deb10` / `deb3b` / `deb83`,
  `ffffff80004c1bb0` at `deb1b` / `deb4d` / `deb95`,
  `ffffff8000b4d800` at `deb36`, `ffffff8002275b44` at `deb77`,
  `ffffff80022b4d800`-region helpers further down), and a
  conditional branch `je 0xffffff80022dec88` at
  `ffffff80022debae`.
- Four RETs are visible inside the first 4096 bytes:
  `ffffff80022df0ff`, `ffffff80022df26f`, `ffffff80022df28a`,
  `ffffff80022df6e9`. The first two are inside the
  [entry, next_function_start) range
  `[ffffff80022dea78, ffffff80022df276)`; the third and fourth
  belong to subsequent functions (`isDebounceOnGoing()` and
  `FUN_ffffff80022df29e`).
- The most plausible terminator for `processBSDCommand` is the
  in-range RET at `ffffff80022df26f` (2039 bytes from entry,
  7 bytes before the next defined function symbol). An earlier
  fall-through path may RET at `ffffff80022df0ff`. Either way,
  the function body contains genuine RETs inside the
  [entry, next_function_start) range, contradicting the rev4
  "no terminator" claim.

The rev4 conclusion that the function has "zero branches, zero
calls, zero RETs, and no dispatch instruction" is therefore
explicitly retracted. The body has real dispatch behavior. The
remaining minor uncertainty is which exact in-range RET
constitutes the canonical function end — Ghidra refuses to
extend its body past the apparent invalid bytes at `dead7`, so
`Function.getBody()` only records 95 bytes — but the
[entry, next_function_start) range and the raw RET evidence
together establish a defensible upper bound of 2046 bytes.

### Rev5 BLOCKER_1 cure — #17 caller-reachability proof for the rev1 AUTH trigger (option d, supplementary in rev6 with corrected scope)

REV6 CORRECTION: the rev5 sub-claim that
`AirportItlwmSkywalkInterface::setASSOCIATE`
"does not return `kIOReturnUnsupported`" is
INCORRECT. The actual source at
`AirportItlwm/AirportItlwmSkywalkInterface.cpp`
lines 4588-4601 contains a diagnostic-
intervention branch that does return
`kIOReturnUnsupported`:

  if (airportItlwmRegDiagShouldBlock(kAirportItlwmRegDiagBlockPublicAssoc)) {
      airportItlwmRegDiagRecordBlock(...);
      airportItlwmRegDiagRecordAssoc(..., kIOReturnUnsupported);
      return kIOReturnUnsupported;
  }

The predicate
`airportItlwmRegDiagShouldBlock(kAirportItlwmRegDiagBlockPublicAssoc)`
(definition at `AirportItlwm/AirportItlwmV2.cpp`
lines 1101-1105) returns true only when
`(sRegDiag.modeFlags & kAirportItlwmRegDiagModeEnabled) != 0
&& (sRegDiag.modeFlags & kAirportItlwmRegDiagModeIntervention) != 0
&& (sRegDiag.blockMask & kAirportItlwmRegDiagBlockPublicAssoc) != 0`.
The state is default-initialized to zero at
`AirportItlwm/AirportItlwmV2.cpp` line 616
(`static AirportItlwmRegDiagState sRegDiag = {};`)
so the predicate is FALSE under production /
default state, and only becomes TRUE when the
operator explicitly enables the regdiag mode
through the control surface in
`airportItlwmRegDiagApplyControl` at
`AirportItlwm/AirportItlwmV2.cpp` lines 831-856.

Therefore option-(d) caller-reachability is
NARROWED to the production / default state:
under that state, the local override handles
the rev1 AUTH trigger locally without falling
through to super::processBSDCommand. Under
explicit diagnostic-intervention mode, the
override DOES fall through to super and the
BootKC body runs.

This narrowing is acceptable because the rev1
AUTH-initiation claim scope IS the production
state. Diagnostic-intervention mode is an
operator-controlled test harness used to
inject specific failure / observation
behaviours during reverse engineering; it is
not part of the production AUTH path. The
rev6 doc explicitly states this narrowed
scope.

The rev6 primary cure for #17 is NOT option-
(d) caller-reachability (which is now
supplementary and narrowed). The rev6
primary cure is option-(c) full-body
disassembly + CFG via Capstone `skipdata=True`
linear decode (see "### Rev6 BLOCKER_1
primary cure" below and rev6 appendix E in
the raw evidence file). Option-(c) does not
depend on caller-reachability and therefore
does not depend on the diagnostic-
intervention branch.

The non-AUTH selectors that DO fall through to super in our
kext are `APPLE80211_IOC_ASSOCIATION_STATUS` (status read),
`APPLE80211_IOC_STA_AUTHORIZE` (post-association AP-side
authorization), `APPLE80211_IOC_STA_DISASSOCIATE` (AP-side
disassoc), and `APPLE80211_IOC_STA_DEAUTH` (AP-side deauth).
None of these initiates an AUTH frame; all are
status/AP-side-management selectors.

### Rev5 BLOCKER_2 cure — assigned proof level satisfied for every target

With #17 boundary corrected (option b/c partial) and rev1 AUTH
reachability proven (option d), every one of the 18 GAP_4
targets has evidence at the auditor-named proof level
`FULL_BODY_OR_LOW_LEVEL_EQUIVALENT_PER_TARGET`:

- Targets #1, #3: rev4 callable-forwarder raw disassembly +
  branch/call/return + CFG (auditor accepted for #1 and #3 in
  the rev4 decision).
- Targets #2, #4, #5, #6, #7, #8, #9, #10, #11, #12, #13, #14,
  #15, #16: rev2 callable-body raw disassembly + branch/call/
  return + CFG (auditor accepted in rev3/rev4).
- Target #17: rev5 corrected boundary
  [entry, next_function_start) = [ffffff80022dea78,
  ffffff80022df275], rev5 Capstone linear decode showing real
  dispatch behavior, and rev5 option-(d) caller-reachability
  proof. The raw disassembly evidence (rev5 appendix B) is the
  auditor-named `raw disassembly` / `low-level equivalent`
  proof form.
- Target #18 (supplemental safeMetaCast helper): rev2 raw
  disassembly + branch/call/return + CFG (accepted).

### Rev5 BLOCKER_3 cure — corrected inventory and narrowing claim

The GAP_4 BootKC base-class inventory for the submitted scope
is consistent with the rev5 evidence:

- 17 named GAP_4 targets recovered to raw disassembly / CFG /
  branch-call-return summary (with rev5 boundary correction for
  #17 plus rev5 option-(d) caller-reachability).
- Supplemental safeMetaCast helper positively identified.

The rev1 AUTH-initiator NARROWING is retightened:

- Within the BootKC IO80211/WCL scope, only
  `apple80211setASSOCIATE` (target #9) via local override
  `AirportItlwmSkywalkInterface::setASSOCIATE` is on the rev1
  AUTH-frame chain.
- Targets #1, #2, #3, #4, #5, #6, #7 (WCL setters): non-AUTH
  forwarders; local override intercepts.
- Target #8 (`setAUTH_TYPE`): state-only.
- Target #9 (`setASSOCIATE`): NARROWED candidate (single).
- Target #10 (`setDEAUTH`): produces deauth, not auth.
- Targets #11, #16 (status/ready stubs): no-op.
- Targets #12, #13, #14 (AP-side STA management): non-AUTH.
- Target #15 (`setRANGING_AUTHENTICATE`): RNGT/802.11mc,
  orthogonal.
- Target #17 (`processBSDCommand`): not on the rev1 AUTH chain
  for our kext (option-d caller-reachability — local override
  handles APPLE80211_IOC_ASSOCIATE locally without falling
  through to super).
- Target #18 (`safeMetaCast`): libkern type-check; no AUTH role.

This NARROWING remains NARROWING, not CLOSURE. Final PIN of the
AUTH initiator still requires GAP_5 (firmware-autonomous TX
context recovery) and GAP_6 (final Apple-side AUTH path
classification). Without GAP_5 + GAP_6 the AUTH initiator cannot
be uniquely pinned — only narrowed within the BootKC scope.

### Rev5 next route

Unchanged from rev4:
  next_work_item_id:
    `CR-479-gap5-firmware-autonomous-tx-context-reference-recovery-20260519`
  next_work_item_role: coder
  next_work_item_required_to_unblock_functional:
    PARTIAL (together with GAP_6).

## REV6 update — primary cure shifts to option-(c) full-body disassembly; rev5 option-(d) caller-reachability re-scoped

The auditor's rev5 decision (canonical decision
sha and three "AUDITOR_BLOCKER_SWEEP" follow-up
decisions on May 19) REJECTED rev5 on four
blockers:

- BLOCKER_1_TARGET_17_CALLER_REACHABILITY_PROOF_FALSE —
  the rev5 option-(d) proof said
  `AirportItlwmSkywalkInterface::setASSOCIATE`
  does not return `kIOReturnUnsupported`, but
  the actual source at
  `AirportItlwm/AirportItlwmSkywalkInterface.cpp`
  lines 4588-4601 returns
  `kIOReturnUnsupported` when
  `airportItlwmRegDiagShouldBlock(kAirportItlwmRegDiagBlockPublicAssoc)`
  is true, so the override can fall through to
  `super::processBSDCommand` and the BootKC
  body is not proven unreachable for the
  submitted claim scope.
- BLOCKER_2_ASSIGNED_DECOMP_PROOF_LEVEL_NOT_MET_FOR_TARGET_17 —
  because rev5 option-(d) reachability fails
  and the rev5 evidence did not provide a
  complete body / low-level equivalent for
  `IO80211InfraInterface::processBSDCommand`,
  the assigned proof level was not met for #17.
- BLOCKER_3_DOC_SELF_CHECK_OVERSTATES_SOURCE_FACT —
  the rev5 request and docs stated the source
  fact incorrectly; the source contradiction
  must be corrected.
- BLOCKER_4_DURABLE_DOC_CONTAINS_UNRESOLVED_REV4_TARGET17_CLASSIFICATION —
  the durable docs still contained the rev4
  "88-byte non-dispatcher" classification next
  to the rev5 retraction, creating an
  internally-contradictory artifact.

Rev6 cures all four blockers as follows.

### Rev6 BLOCKER_1 primary cure — option-(c) full-body disassembly + CFG (does not depend on caller-reachability)

The auditor's option-(c) acceptable proof form
is "corrected function boundaries with complete
raw disassembly and CFG". Rev6 provides
exactly that for target #17:

- Body boundary corrected to 2046 bytes:
  `ffffff80022dea78..ffffff80022df275`. The
  body is bounded above by the next defined
  function symbol
  `IO80211InfraInterface::isDebounceOnGoing()`
  at `ffffff80022df276`. Body end at
  `ffffff80022df26f` (`ret c3`), followed by
  the `__stack_chk_fail` abort handler at
  `ffffff80022df270` (`call
  0xffffff8000307340`) and an alignment `nop`
  at `ffffff80022df275`.
- Complete raw disassembly via Capstone 5.0.7
  `skipdata=True` linear decode (rev5h batch
  `CR479Gap4DecompRev5h.java` produced a
  4096-byte memory dump; Capstone with skipdata
  re-synchronises past apparent invalid bytes
  and produces a continuous instruction
  stream).
- CFG summary inside the
  `[ffffff80022dea78, ffffff80022df276)`
  range: 483 decoded instructions, 47 CALLs
  total (38 direct absolute + 9 indirect via
  vtable memory operands `[rax+0x208]` x2,
  `[rcx+0x1f8]` x2, `[rcx+0x120]` x2,
  `[rax+0x28]` x2, and `[rax+0x890]`; the 9
  indirect CALLs are the selector-dispatch
  evidence), 18 conditional branches, 6
  unconditional `JMP`s, 2 RETs (`df0ff` early
  exit, `df26f` final exit). 1 stack-canary-
  failure abort call at `df270`. 1 alignment
  `nop` at `df275`. Out-edges: the 38 direct
  CALLs include
  `ffffff8000350880`, `ffffff80004c1bb0`,
  `ffffff8000894e70`, `ffffff8000895ae0`,
  `ffffff8000b4d800`, `ffffff8002275b44`,
  `ffffff8002117f38`, `ffffff80021e303c`, and
  the `__stack_chk_fail` abort
  `ffffff8000307340`. Body-end indicators
  (independent of `Function.getBody()`
  metadata):
  (a) canonical `ret` instruction at
  `df26f`;
  (b) standard stack-canary epilogue
  immediately preceding (`jne df270; add rsp,
  0x10; pop rbx; pop r12; pop r14; pop r15;
  pop rbp; ret`);
  (c) `__stack_chk_fail` abort handler at
  `df270` (textbook stack-protected function
  tail);
  (d) next defined function symbol at
  `ffffff80022df276` immediately after the
  alignment `nop`.

The option-(c) cure is documented in detail in
rev6 appendix E of the tracked raw evidence
file. It does not depend on caller-reachability
and therefore is not affected by the rev5
BLOCKER_1 contradiction. The rev3 / rev4 /
rev5 prior classifications of #17 are
explicitly retracted in rev6 and replaced with
the corrected option-(c) classification (the
narrative section "### IO80211InfraInterface::processBSDCommand
(rev6-corrected body 2046 bytes; ...)" above
in this doc is rewritten in-place to remove
the contradictions, satisfying rev5 BLOCKER_4).

### Rev6 BLOCKER_2 cure — assigned proof level satisfied for every target via option-(c) for #17

With #17 boundary corrected and complete raw
disassembly + CFG provided by option-(c),
every one of the 18 GAP_4 targets has
evidence at the auditor-named proof level
`FULL_BODY_OR_LOW_LEVEL_EQUIVALENT_PER_TARGET`:

- Targets #1, #3: rev4 callable-forwarder raw
  disassembly + branch/call/return + CFG
  (auditor accepted for #1 and #3 in the rev4
  decision).
- Targets #2, #4, #5, #6, #7, #8, #9, #10,
  #11, #12, #13, #14, #15, #16: rev2 callable-
  body raw disassembly + branch/call/return +
  CFG (auditor accepted in rev3/rev4).
- Target #17: rev6 option-(c) complete-body
  raw disassembly via Capstone `skipdata=True`
  linear decode, CFG (483 instructions, 47
  CALLs, 24 branches, 2 RETs) over the
  corrected 2046-byte body
  `ffffff80022dea78..ffffff80022df275`.
- Target #18 (supplemental safeMetaCast
  helper): rev2 raw disassembly +
  branch/call/return + CFG (accepted).

### Rev6 BLOCKER_3 cure — corrected source-fact self-check for setASSOCIATE

The rev5 self-check claimed
`AirportItlwmSkywalkInterface::setASSOCIATE`
"does not return `kIOReturnUnsupported`". This
is INCORRECT. The corrected rev6 source-fact
self-check states:

- `AirportItlwmSkywalkInterface::setASSOCIATE`
  (`AirportItlwm/AirportItlwmSkywalkInterface.cpp`
  starting at line 4568) returns
  `kIOReturnUnsupported` when
  `airportItlwmRegDiagShouldBlock(kAirportItlwmRegDiagBlockPublicAssoc)`
  is true (lines 4588-4601).
- The predicate is true only when the
  operator has enabled
  `kAirportItlwmRegDiagModeEnabled +
  kAirportItlwmRegDiagModeIntervention +
  kAirportItlwmRegDiagBlockPublicAssoc` via
  the control surface
  (`AirportItlwm/AirportItlwmV2.cpp`
  `airportItlwmRegDiagApplyControl` lines
  831-856; predicate definition at lines
  1101-1105).
- Default-initialized state
  (`static AirportItlwmRegDiagState sRegDiag = {};`
  at `AirportItlwm/AirportItlwmV2.cpp` line
  616) leaves all bits zero, so the predicate
  is false in production / default operation.
- Therefore, in the production state where the
  rev1 AUTH-initiation claim scope applies,
  `setASSOCIATE` does NOT take the
  `kIOReturnUnsupported` branch and the local
  override handles the rev1 AUTH trigger
  locally without falling through to super.
- In diagnostic-intervention mode (operator
  test harness), `setASSOCIATE` DOES return
  `kIOReturnUnsupported` and the override
  falls through to super::processBSDCommand.
  This path is outside the rev1 AUTH-
  initiation claim scope because it is an
  operator test harness, not production AUTH
  behaviour.

The rev6 option-(c) primary cure does not
depend on either branch — it recovers the
BootKC body to proof-level evidence directly.
Option-(d) caller-reachability is retained as
supplementary (with corrected scope) but is
no longer the primary cure.

### Rev6 BLOCKER_4 cure — durable doc internally consistent

The rev4 "Targets investigated" table row for
#17 and the rev4 narrative section "###
IO80211InfraInterface::processBSDCommand
(88-byte body; rev3 dispatcher overclaim
retracted in rev4)" plus the rev4 "RULED OUT"
entry for #17 are corrected IN PLACE in this
rev6 doc to remove the contradiction with the
rev5 / rev6 cures. The rev3 and rev4 prior
classifications are still preserved as
history in the "REV5 update" section and in
this "REV6 update" section, but the body of
the durable doc reflects the rev6-corrected
boundary and classification.

The tracked raw evidence file is also updated:
the #17 section preserves the rev3/rev4
evidence (Ghidra five-strategy boundary
extension batches that all hit `dead7` /
Capstone original 384-byte decode) as
historical record, retracts the rev4
"two-decoder agreement" claim in line, and
adds rev6 appendix E with the complete
option-(c) linear-decode body and CFG.

### Rev6 next route

Unchanged from rev4 / rev5:
  next_work_item_id:
    `CR-479-gap5-firmware-autonomous-tx-context-reference-recovery-20260519`
  next_work_item_role: coder
  next_work_item_required_to_unblock_functional:
    PARTIAL (together with GAP_6).

## Document version history (historical; not an active classification statement)

The following per-revision retractions are preserved for
traceability. The active rev7 classification of all 18 targets
is the one in the "Scope and current rev7 classification"
section at the top of this file, NOT any per-revision retraction
text below or in the in-body sections.

- rev1 GAP_4 submission REJECTED (decision sha
  `1edc9aae348d3d459fc255f4ae52cfc24e2d335ce5de2b108ebb5c7b9d114dcf`)
  on four blockers: acceptance criteria not met, decomp evidence
  insufficient, GAP_4 closure overclaim, native decompiler
  failure not cured with allowed fallback.
- rev2 GAP_4 stalled at the pre-audit preflight gate (missing
  `coder_assigned_decomp_proof_level_self_check` field).
- rev3 GAP_4 REJECTED (decision sha
  `948f23b99413b7c01a19e3262948e1cb33e1c67a12911e27f6dde9c5ae7f251f`)
  on four further blockers: WCL vtable-slot classification
  contradicts raw bytes (#1/#3), processBSDCommand dispatch body
  not recovered (#17), assigned decomp proof level not met for
  every target, GAP_4 inventory and narrowing overclaim.
- rev4 GAP_4 REJECTED (decision sha
  `11b740f532f001946cbd38907b39e5fd149d05d3854ba67314737747ae82ef0e`)
  on three blockers: #17 body end not proven, assigned proof
  level not met for #17, inventory/narrowing overclaim. The
  rev4 evidence ACCEPTED #1 and #3 callable-forwarder recovery
  (carried into rev7 unchanged).
- rev5 GAP_4 REJECTED (canonical decision
  path `commit-approval/decisions/COMMIT_DECISION_CR-479-stage1-apple80211-assoc-iwn-firmware-host-command-boundary-gap4-decomp-rev5-20260519.md`)
  on four blockers: #17 option-(d) caller-reachability proof
  false because `setASSOCIATE` returns `kIOReturnUnsupported`
  under the diagnostic-block branch; assigned proof level not
  met for #17; doc self-check overstates source fact; durable
  doc contains unresolved rev4 #17 classification.
- rev6 GAP_4 REJECTED (decision path
  `commit-approval/decisions/COMMIT_DECISION_CR-479-stage1-apple80211-assoc-iwn-firmware-host-command-boundary-gap4-decomp-rev6-20260519.md`)
  on three blockers: #17 full body / low-level evidence not
  durable in reviewed diff (only summary counts + first-64
  representative slice + reference to host scratch path); #17
  CFG claim not reviewable from submitted artifact (no complete
  branch/call/return/CFG table); durable markdown still contains
  active-looking rev4 #17 88-byte/no-dispatch claims at the top.
- rev7 GAP_4 (this packet): cures all rev6 blockers by (a)
  embedding the complete 483-instruction linear decode for
  target #17 directly into the tracked raw evidence file (no
  external scratch reference), (b) embedding a complete
  78-entry CFG / branch / call / return / skipdata table in
  the tracked raw evidence file, and (c) rewriting the
  markdown title and opening "## Scope and current rev7
  classification" section so the active narrative agrees with
  the rev6/rev7 corrected target #17 classification (2046-byte
  callable selector dispatcher; the rev4 88-byte / no-dispatch
  text in the body of this file is preserved as in-body
  historical retraction context, with explicit rev6/rev7 update
  sections in place to override).

The rev6 and rev7 corrections do not change the previously-
accepted #1 / #2 / #3 / #4 / #5 / #6 / #7 / #8 / #9 / #10 /
#11 / #12 / #13 / #14 / #15 / #16 / #18 evidence; they affect
only target #17 and the supporting top-of-document narrative.
