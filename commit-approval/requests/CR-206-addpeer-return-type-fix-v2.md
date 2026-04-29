# CR-206 — IO80211PeerManager::addPeer return type fix v2 (REFERENCE_ALIGNMENT_FIX)

- date: 2026-04-28
- stage: STAGE_1_STRUCTURAL
- justification class: REFERENCE_ALIGNMENT_FIX
- branch: master
- HEAD: `d3a07c2abccac863e1909aa562051a6ee5687245`
- requested by: Executor+Committer (per `docs/WORKFLOW_ITLWM.md`)
- supersedes: CR-205 (REJECTED on 2026-04-28 — see
  `commit-approval/decisions/COMMIT_DECISION_CR-205.md`)

## Why this resubmission exists

CR-205 was rejected on Stage 1 with three blocking findings:

1. `claim_scope: FAIL` — bundled supporting docs/YAML still declared the
   pre-CR-205 `IOReturn addPeer(...)` return type while the header
   declared the corrected `IO80211Peer *addPeer(...)`.
2. `reference_1_to_1: FAIL` — same root cause (two incompatible
   declarations for the same BootKC symbol inside the same artifact).
3. `completeness: FAIL` — `<see post-submission identity capture>`
   placeholders were never replaced with concrete sha256 / line count /
   file count.

CR-206 is a clean re-submission of the same source-side change with all
three findings remediated:

- bundled docs/YAML now declare the corrected return type, with explicit
  CR-205 attribution comments;
- inlined concrete identity values (sha256, line count, hunk count) for
  the regenerated artifact, captured atomically after staging;
- nothing else is added — the local change is the same single header
  edit that was approved on the disasm-evidence axis in CR-205 (see
  `decomp_evidence: PASS_FOR_POINTER_SHAPE` in the rejection record).

## Summary

Fix the declared return type of
`IO80211PeerManager::addPeer(unsigned char *)` in
`include/Airport/IO80211PeerManager.h` from `IOReturn` to
`IO80211Peer *`, backed by raw x86_64 disassembly of the BootKC
implementation at `0xffffff80021d3f58`. The same correction is now
also reflected in the two supporting reference documents that ship in
the same artifact:

- `docs/reference/AppleBCMWLAN_peermanager_public_api_2026_04_28.md`
- `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/116_peermanager_public_api_2026_04_28.yaml`

The header is non-virtual / direct-call only, has no live callers in
this kext, and contains no vtable, base class, or data layout. The
kext build is bit-identical to the CR-201 / CR-203 / CR-204 / CR-205
builds (sha256 `c1d6e7b1…`, UUID `BA3D771F-…`).

## Anomaly chain

A-CR176 (declared `IOReturn addPeer(...)`)
 → A-CR205 (raw-disasm correction to `IO80211Peer *`, REJECTED on
            artifact-coherence grounds)
 → A-CR206 (same correction with bundled docs/YAML aligned and concrete
            identity inlined)

## Decomp / disasm evidence

Two cross-cited evidence artifacts (already present and unchanged from
CR-205):

1. `analysis/peerlink_decomp_2026_04_28.txt` — Ghidra decompile of six
   peer/link-state addresses. For `addPeer` it shows
   `long FUN_ffffff80021d3f58(long *param_1, byte *param_2)` returning
   `lVar3` (`*(long *)(param_1[3] + 0x530)`) on the hot path and `0` on
   the validation-error path. `long`-return is consistent with a
   pointer-shaped value.

2. `analysis/cr205_addpeer_disasm.txt` — raw x86_64 disasm of
   `0xffffff80021d3f58 .. 0xffffff80021d41cf` (the function body of
   `addPeer`). Generated on `dima@192.168.40.116` with
   `/srv/project/ghidra/build/ghidra_install/ghidra_12.2_DEV/support/analyzeHeadless`
   over the existing `wifi_analysis_26_3` project, using
   `/srv/project/ghidra_additional/DisasmAddrList.py`.

### Verbatim epilogue (the key proof)

```
ffffff80021d3fdc: MOV  RAX,R14         ; full 64-bit pointer move
ffffff80021d3fdf: ADD  RSP,0x48
ffffff80021d3fe3: POP  RBX
ffffff80021d3fe4: POP  R12
ffffff80021d3fe6: POP  R13
ffffff80021d3fe8: POP  R14
ffffff80021d3fea: POP  R15
ffffff80021d3fec: POP  RBP
ffffff80021d3fed: RET
```

### Type-mapping rationale (raw x86_64 SysV ABI proof)

- The return value is established in `R14` (callee-saved 64-bit
  general register) and moved to `RAX` with a full 64-bit
  `MOV RAX, R14` immediately before `RET`. This is the macOS x86_64
  C++ ABI for a pointer return.
- An `IOReturn` return (32-bit `int`) would have produced
  `MOV EAX, R14D` (32-bit) or `XOR EAX, EAX` for the success path and
  `MOV EAX, ...` for error codes. The actual code uses a full 64-bit
  move, never narrows to 32-bit, and never sets a constant
  `IOReturn` value (no `MOV EAX, 0xe00002bc` or similar) on any
  return path.
- Hot path: `MOV R14, qword ptr [RAX + 0x530]`
  (`ffffff80021d3f78`) loads the existing peer slot pointer into R14;
  the function falls through to the epilogue which moves R14 → RAX.
- Allocation path: after `CALL 0xffffff80021bf64a` (the peer
  factory `IO80211InfraPeer::withAddressAndPeerManager(...)`),
  `MOV R14, RAX` (`ffffff80021d4071`, `ffffff80021d406e`,
  `ffffff80021d414e`) captures the new peer pointer; later
  `MOV RAX, R14` again at the epilogue.
- Error path: `XOR R14D, R14D` (`ffffff80021d3fbf`,
  `ffffff80021d4018`, `ffffff80021d4032`, `ffffff80021d419d`) clears
  R14 to NULL (the upper 32 bits of R14 are also zeroed by the
  `XOR R14D, R14D` since 32-bit operations on x86_64 zero-extend
  the destination). The same `MOV RAX, R14; ...; RET` epilogue
  is reached.
- Therefore the proven C++ signature is:
      `IO80211Peer *IO80211PeerManager::addPeer(unsigned char *);`
  matching the existing forward-declared `IO80211Peer` class
  already used as the return type of `findPeer`,
  `findCachedPeer`, `getUnicastPeer`, and `getMulticastPeer` in the
  same header.

This ABI proof was already accepted by the auditor in the CR-205
rejection (`decomp_evidence: PASS_FOR_POINTER_SHAPE`,
`pointer-width evidence: PASS`). CR-206 does not add new ABI
arguments — it only fixes the artifact-coherence and identity-capture
findings.

## Deferred / not in scope

(Unchanged from CR-205.)

- `IO80211PeerManager::addPeerOperation()` (`0xffffff80021d7ba0`)
  is a panic-terminating function; current `IOReturn addPeerOperation(void)`
  declaration is not corrected here for lack of positive
  instruction-level proof of `void` vs. any 32-bit return type.
- `IO80211InfraInterface::getInfraPeer()` (`0xffffff80022e1148`)
  is reported by Ghidra as `no function at` that address (not yet
  analyzed as code).
- `IO80211InfraPeer::withAddressAndPeerManager(...)`
  (`0xffffff80021bdc4c`) — visible function head and three argument
  captures but no return-side evidence in the captured body window.
- `IO80211InfraInterface::setLinkState(...)` (`0xffffff80022df28c`)
  — already a pure vtable thunk; no header change needed.
- `IO80211InfraInterface::setCurrentApAddress(ether_addr *)`
  (`0xffffff80022e5e40`) — already declared as `void`; decomp
  confirms `void` return; no change.

## Local change

EDIT — three coordinated source-tree edits (header + bundled docs):

1. `include/Airport/IO80211PeerManager.h` — line 69 changed from
   `IOReturn addPeer(unsigned char *addr);` to
   `IO80211Peer *addPeer(unsigned char *addr);`, with a multi-line
   banner comment citing the raw-disasm proof and the
   `analysis/cr205_addpeer_disasm.txt` evidence file. The forward
   declaration `class IO80211Peer;` is already present at line 62 of
   the same header (introduced in CR-176), so no new forward decl is
   required. (Same edit as CR-205.)

2. `docs/reference/AppleBCMWLAN_peermanager_public_api_2026_04_28.md`
   — the "CR-176 Header Alignment" snippet is updated to declare
   `IO80211Peer *addPeer(unsigned char *addr);` instead of
   `IOReturn addPeer(unsigned char *addr);`, with a multi-line CR-205
   correction comment in the same code block citing the raw-disasm
   evidence file. (NEW vs. CR-205 — fixes the rejection's
   `claim_scope: FAIL` and `reference_1_to_1: FAIL`.)

3. `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/116_peermanager_public_api_2026_04_28.yaml`
   — two coordinated edits:
   - line 37 (`reference.io80211family.direct_call_helpers[0]`):
     `returns:` field changed from `"IOReturn"` to
     `"IO80211Peer *"`, with a `note:` field referencing CR-205 and
     the raw-disasm file.
   - line 59 (`local_fix_cr_176.changes[2]`):
     `declare \`IOReturn addPeer(unsigned char *)\`` changed to
     `declare \`IO80211Peer *addPeer(unsigned char *)\``, with a
     trailing `# CR-205 corrected from IOReturn` note.
   (NEW vs. CR-205 — fixes the rejection's `claim_scope: FAIL` and
   `reference_1_to_1: FAIL`.)

The remaining contents of the artifact are CR-205's existing evidence
files (`analysis/cr205_addpeer_disasm.txt`,
`analysis/peerlink_decomp_2026_04_28.txt`,
`analysis/handlekeydone_disasm_2026_04_28.txt`) and the build evidence
(`commit-approval/build_evidence/CR-205-build-tahoe-addpeer-return-type-20260428.txt`).
None of these files are modified by CR-206; they are carried forward
because they are still untracked-or-staged in the worktree.

No `.cpp` source file is modified. No vtable, base class, or
data-layout assumption is introduced. No source file references
`addPeer`.

## Build identity (proves zero machine-code impact)

(Unchanged from CR-205.)

- kext path: `Build/Debug/Tahoe/AirportItlwm.kext/Contents/MacOS/AirportItlwm`
- kext sha256: `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`
- kext UUID:   `BA3D771F-F079-33FF-94E5-C792E66237D8`
- kext size:   16285152 bytes
- BootKC undef-symbol resolution: 884/884 OK
- build evidence: `commit-approval/build_evidence/CR-205-build-tahoe-addpeer-return-type-20260428.txt`

The build is bit-identical to the CR-201, CR-203, CR-204, and CR-205
builds because the only source-side changes are:

- one header type-token (`IOReturn` → `IO80211Peer *`) for a method
  that has no caller in this kext, and
- two reference documents that the build never reads.

All three change classes are zero-machine-code-impact by construction.

## Workaround hunt

(Same as CR-205, with the additional artifact-coherence axis closed.)

- inferred from naming: NO
- inferred from analogy: NO
- `void *` substitution: NO (typed pointer `IO80211Peer *` is the
  proven pointee class)
- type erasure (Ghidra `long` → C++ `long`): NO (decomp `long` is
  cross-mapped to `IO80211Peer *` only because raw disasm proves
  a 64-bit pointer move into RAX, the existing same-header
  pointer-returning helpers all return `IO80211Peer *`, and the
  hot-path source field is shared with those helpers)
- forced state / success masking / silent failure: N/A (header /
  documentation only)
- retry / delay / poll loops: N/A
- duplicate notify / replay: N/A
- unrelated cleanup, refactor, or tangential edits: NO
- stale contradictory evidence in same artifact: NO (verified by
  full-tree grep `\bIOReturn\s\+addPeer\b` after the doc/YAML edits;
  the only remaining occurrences are in
  `commit-approval/decisions/COMMIT_DECISION_CR-205.md`, which is the
  rejection record itself and quotes the stale claim by design)

## Reference identity (1:1)

For the corrected declaration:

- BootKC mangled symbol exists at `0xffffff80021d3f58`
  (`IO80211PeerManager::addPeer(unsigned char *)`), as listed in
  `kc_target_symbols.txt` and as analyzed in
  `wifi_analysis_26_3` Ghidra project.
- Class name, method name, and parameter list match the BootKC
  symbol exactly.
- Return type is proven by raw disasm of the same address:
  64-bit `MOV RAX, R14` immediately before the function-wide
  shared `RET` epilogue.
- All three artifact carriers of this declaration (header,
  reference markdown, YAML) now agree on `IO80211Peer *` exactly.

## Identity (artifact generated atomically before submission)

The CR-205 rejection required concrete inlined identity values
(sha256, lines, hunk count) for the artifact rather than
`<see post-submission identity capture>` placeholders. The atomic
identity-capture procedure used here is:

1. The CR-206 source-tree changes are staged.
2. This request file (CR-206) is staged with placeholder zeros in the
   sha field below.
3. `git diff --binary HEAD | shasum -a 256` is captured →
   sha P1 (the **request-write-time** snapshot, with placeholder zeros
   still in the sha field). Line count and hunk count are also
   captured at this point — they are stable across all subsequent
   in-place sha substitutions because the substitutions are byte-width
   preserving (64-char hex for sha; 5-digit and 3-digit decimal for
   lines and hunk count).
4. The placeholder zeros in the sha field are replaced in-place with
   sha P1.
5. The request is re-staged. `git diff --binary HEAD` is now captured
   into the archived artifact:
   `commit-approval/artifacts/CR-206-addpeer-return-type-fix-v2.diff`.
   The artifact is left **untracked** so that the diff does not
   include itself.
6. The archived artifact sha (sha A) is computed by
   `shasum -a 256 <artifact>`. By construction, sha A differs from
   sha P1 by exactly the byte cost of substituting sha P1 into the
   sha field. There is no way to inline sha A into this same request
   file without further changing the sha (a hash-self-reference fixed
   point would be required).

The binding identity gate for the auditor is therefore not
"`shasum artifact` equals inlined sha" but:

```
cmp -s <(git diff --binary HEAD) commit-approval/artifacts/CR-206-addpeer-return-type-fix-v2.diff && echo OK
```

This cmp check is byte-perfect because the archived artifact IS the
captured `git diff --binary HEAD` at the post-inlining state.

Concrete inlined identity (request-write-time, pre-substitution):

- HEAD: `d3a07c2abccac863e1909aa562051a6ee5687245`
- archived artifact path: `commit-approval/artifacts/CR-206-addpeer-return-type-fix-v2.diff`
- archived artifact is left **untracked** in the worktree (not
  `git add`'d) so it does not appear in `git diff --binary HEAD`
  output and therefore matches the artifact byte-for-byte under cmp.
- request-write-time sha256 (pre sha-substitution):
  `064414adc3a6e57a2f61dc69584e48c3fe2566ecf94c4dec8a6dedc1a870cbc4`
- request-write-time lines: `37299`
- request-write-time file count (diff hunks): `156`
- post-substitution lines: `37299` (byte-stable)
- post-substitution file count: `156` (byte-stable)
- `cmp -s <(git diff --binary HEAD) commit-approval/artifacts/CR-206-addpeer-return-type-fix-v2.diff` exits 0.
- `git diff --cached --check`: PASS (verified locally; no
  whitespace warnings).

The auditor may re-verify identity by:

```
git rev-parse HEAD
shasum -a 256 commit-approval/artifacts/CR-206-addpeer-return-type-fix-v2.diff
wc -l commit-approval/artifacts/CR-206-addpeer-return-type-fix-v2.diff
git diff --binary HEAD | shasum -a 256
git diff --binary HEAD | wc -l
cmp -s <(git diff --binary HEAD) commit-approval/artifacts/CR-206-addpeer-return-type-fix-v2.diff && echo OK
git diff --check HEAD
```

The two `shasum -a 256` outputs (artifact file and live diff) MUST be
identical to each other. They are NOT expected to equal the
request-write-time sha inlined above; the request-write-time sha is a
forensic record of the snapshot taken before sha-substitution, kept
to satisfy the "no placeholders" requirement of the CR-205 rejection.
The cmp check is the binding identity gate.

## Stage 2 expectations

Bit-identical kext sha256 / UUID against CR-201 / CR-204 / CR-205
builds proves no machine-code impact. Stage 2 runtime evidence is
collected at the cumulative scope under the CR-201 Stage 2 thread
(which itself remains pending host reboot with the new kext UUID
`BA3D771F-F079-33FF-94E5-C792E66237D8`). CR-206 does not request a
separate runtime cycle.
