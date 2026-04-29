# CR-203 — IO80211ScanRequest primitive-only batch v2 (raw-disasm-evidenced, NEW header)

- date: 2026-04-28
- stage: STAGE_1_STRUCTURAL
- justification class: REFERENCE_ALIGNMENT_FIX
- supersedes: CR-202 (rejected `workaround_hunt: FAIL_TYPE_ERASURE`)
- branch: master
- HEAD: `d3a07c2abccac863e1909aa562051a6ee5687245`
- requested by: Executor+Committer (per `docs/WORKFLOW_ITLWM.md`)

## Summary

Resubmission of CR-202 with the rejection guidance applied. CR-202
declared five `IO80211ScanRequest` helpers — three pointer-shaped
(`getChannels`, `getSSID`, `getScanID`) and two predicate-shaped
(`has6EChannel`, `is2GScanRequest`) — using Ghidra's C decompile
return type `long` for all five. The reviewer rejected the batch as
`workaround_hunt: FAIL_TYPE_ERASURE` because Ghidra's `long` does not
prove the C++ semantic return type for either pointer returns or
boolean returns; both are commonly carried as 64-bit registers in
Ghidra's intermediate type system.

CR-203 narrows the batch to **two helpers** whose return type is
proven by **raw x86_64 disassembly** of `BootKernelExtensions.kc` to
be one-byte `AL`-only — exactly the macOS x86_64 C++ ABI calling
convention for `bool`. The three pointer-shaped helpers from CR-202
are deferred until exact pointer/reference types can be recovered
from struct-type metadata; the raw disasm in
`analysis/cr203_scanrequest_disasm.txt` confirms all three (including
`getScanID`, which CR-202's reviewer flagged as the strongest
candidate for a `long` return) compute a pointer (base + offset),
not an integer.

The class is declared with no base class and no data layout; the
local kext does not allocate, subclass, or take `sizeof` of it. The
two helpers are not called by any local code path. The kext built
from the CR-203 diff is bit-identical to the CR-201 build (sha256
and UUID unchanged), confirming this is purely a structural
reference-alignment introduction with zero machine-code impact.

## Anomaly chain (carried forward)

A-CR165 → … → A-CR196 → A-CR197 → A-CR198 → A-CR199 → A-CR200 →
A-CR201 → A-CR202 (rejected) → A-CR203

## Decomp / disasm evidence

The raw x86_64 instruction listing for the 5 candidate addresses is
captured in `analysis/cr203_scanrequest_disasm.txt`. It was produced
by running, on `dima@192.168.40.116`:

```
/srv/project/ghidra/build/ghidra_install/ghidra_12.2_DEV/support/analyzeHeadless \
    /srv/project/ghidra_output wifi_analysis_26_3 \
    -readOnly -process BootKernelExtensions.kc \
    -postScript /srv/project/ghidra_additional/DisasmAddrList.py \
    /tmp/cr203_scanrequest_disasm.txt \
    0xffffff8002269c9e 0xffffff8002269cda \
    0xffffff800226a532 0xffffff800226a4fa 0xffffff800226a540
```

For each of the 2 kept helpers below, the *Disasm evidence* column
quotes the operative instructions verbatim from that listing.

## Confirmed exported symbols (raw-disasm-evidenced) — 2 rows

| # | Address              | Symbol                                          | Header type | Disasm evidence (verbatim)                                               |
|---|----------------------|-------------------------------------------------|-------------|--------------------------------------------------------------------------|
| 1 | `0xffffff8002269c9e` | `IO80211ScanRequest::has6EChannel()`            | `bool`      | `MOV AL,byte ptr [RCX + RSI*0x1 + 0x59]; AND AL,0x20; SHR AL,0x5; RET`   |
| 2 | `0xffffff8002269cda` | `IO80211ScanRequest::is2GScanRequest()`         | `bool`      | `CMP dword ptr [RCX + RSI*0x1 + 0x54],0xe; SETC AL; RET`                 |

### Type-mapping rationale (raw x86_64 ABI proof)

In **both** functions the return value is established in `AL` (low
byte only) and the upper 56 bits of `RAX` are not zero-extended on
any path:

- `has6EChannel @ 0xffffff8002269c9e`:
  - hot path: `MOV AL, byte ptr [RCX + RSI*0x1 + 0x59]; AND AL, 0x20;
    SHR AL, 0x5; ...; POP RBP; RET`
  - empty-loop path: `XOR EAX, EAX; POP RBP; RET`
- `is2GScanRequest @ 0xffffff8002269cda`:
  - hot path: `CMP dword ptr [RCX + RSI*0x1 + 0x54], 0xe; SETC AL;
    JC end; ...; POP RBP; RET`
  - empty-loop path: `XOR EAX, EAX; POP RBP; RET`

This is the macOS x86_64 C++ `bool` ABI: caller is required to
truncate the return to the low byte. A `long` return would have
produced an explicit `MOVZX EAX, AL` (zero-extend `AL` to `RAX`) on
the hot path, or returned a full 64-bit value in `RAX` directly.
Therefore both helpers are `bool`.

## Deferred from CR-202 / CR-203 candidate set — 3 pointer-shaped helpers

Raw disasm proves these compute a pointer (base + constant offset),
not an integer arithmetic result:

| Address              | Symbol                                  | Body (verbatim)                                       | Inferred shape                  |
|----------------------|-----------------------------------------|-------------------------------------------------------|----------------------------------|
| `0xffffff800226a532` | `IO80211ScanRequest::getChannels()`     | `MOV RAX,[RDI+0x10]; ADD RAX,0x50; RET`               | `T*` at offset `+0x50`           |
| `0xffffff800226a4fa` | `IO80211ScanRequest::getSSID()`         | `MOV RAX,[RDI+0x10]; ADD RAX,0x1c; RET`               | `T*` at offset `+0x1c`           |
| `0xffffff800226a540` | `IO80211ScanRequest::getScanID()`       | `MOV EAX,0x1554; ADD RAX,[RDI+0x10]; RET`             | `T*` at offset `+0x1554`         |

In all three cases the return is `[RDI+0x10] + <const>`, i.e. a
pointer into an inner struct. Without recovered struct-type metadata
naming the pointee, declaring these as `void *` would itself be type
erasure. They are deferred until a separate evidence pass can recover
exact `T*` types.

## Local change

NEW source file (single header, additive only):

- `include/Airport/IO80211ScanRequest.h`

Class declaration:

```cpp
class IO80211ScanRequest
{
public:
    bool has6EChannel();
    bool is2GScanRequest();
};
```

The header carries verbatim raw-disasm proof and ABI rationale in its
banner comment block; future readers can re-derive the type proof
without leaving the header.

NEW evidence artifact:

- `analysis/cr203_scanrequest_disasm.txt`

No other files in the repository are modified. No `.cpp` source file
references any of the 2 declared symbols. No vtable, base class, or
data-layout assumption is introduced.

## Build identity (proves zero machine-code impact)

Layered on the CR-201 staged tree (`include/Airport/IO80211BssManager.h`
+ 143 cumulative reference-alignment files), the CR-203 build is
**bit-identical** to the CR-201 build:

- kext path: `Build/Debug/Tahoe/AirportItlwm.kext/Contents/MacOS/AirportItlwm`
- kext sha256: `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`
- kext UUID:   `BA3D771F-F079-33FF-94E5-C792E66237D8`
- kext size:   16285152 bytes
- BootKC undef-symbol resolution: 884/884 OK
- build evidence: `commit-approval/build_evidence/CR-203-build-tahoe-scanrequest-primitive-batch-v2-20260428.txt`

The bit-identical match against CR-201 (which had no
`IO80211ScanRequest.h`) confirms the new header has zero live call
sites and produces no machine-code change.

## Workaround hunt

- `inferred from naming`: NO — `has6EChannel` and `is2GScanRequest`
  could plausibly have been declared from naming, but type proof in
  this batch comes exclusively from raw disasm.
- `inferred from analogy`: NO.
- `void *` substitution: NO — the three pointer-shaped helpers that
  would have required `void *` are explicitly deferred.
- type erasure (Ghidra `long` accepted as C++ `long`): NO — both
  declared helpers proved one-byte `AL`-only; integer-shape helpers
  excluded.
- forced state / success masking / silent failure: N/A (header-only).
- retry / delay / poll loops: N/A (header-only).
- duplicate notify / replay: N/A (header-only).
- unrelated cleanup, refactor, or tangential edits: NONE.

## Reference identity (1:1)

For each of the 2 declared symbols:

- BootKC mangled symbol exists at the cited address.
- Class name, method name, and parameter list match the BootKC
  symbol exactly.
- Return type is proven by raw disasm of the same address.

No declaration is introduced for any address that does not have
both raw disasm evidence and a BootKC export.

## Stage 2 expectations

Bit-identical kext sha256 / UUID / size against CR-201 build evidence
proves no machine-code change. Stage 2 runtime scenario is the same
as CR-201 (header-only no-call-site change cannot regress). Loaded-
kext post-reboot evidence will be collected at the cumulative scope
under the CR-201 Stage 2 thread; CR-203 does not request a separate
runtime cycle.

## Diff artifact

`commit-approval/artifacts/CR-203-scanrequest-primitive-batch-v2.diff`
will be regenerated atomically against this exact HEAD before review.
