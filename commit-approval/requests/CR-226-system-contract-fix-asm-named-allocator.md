# COMMIT REQUEST CR-226 — SYSTEM_CONTRACT_FIX (asm-named IO80211NetworkPacket operator new)

request_id: CR-226
review_stage: STAGE_1_STRUCTURAL
proposed_change_class: SYSTEM_CONTRACT_FIX
parent_anomaly: STEP 8b TX/RX packet pool creation failure
runtime_basis: CR-222 Stage 2 evidence (alloc gate proven at runtime).
prior_rejections:
  - CR-218..CR-221 — REJECTED (alignment/contract/probe-classification gaps).
  - CR-222 (ACTIVE_ALLOCATION_PROBE) — Stage 2 evidence collected.
  - CR-223 (IOSkywalkNetworkPacket subclass) — REJECTED.
  - CR-224 (IO80211NetworkPacket same-kext subclass) — Stage 1 APPROVED
    but kmutil rejected with vtable-size mismatch.
  - CR-225 (direct IO80211NetworkPacket allocation) — REJECTED. Auditor:
    C++ call `IO80211NetworkPacket::operator new(0x78)` resolved via
    name lookup to the inherited `__ZN8OSObjectnwEm` (OSObject zone),
    not to BootKC `__ZN20IO80211NetworkPacketnwEm`. Local
    IO80211NetworkPacket header doesn't redeclare operator new.
    Allocation contract diverged from the request's stated reference.

reviewed_head_commit: d3a07c2abccac863e1909aa562051a6ee5687245
artifact: commit-approval/artifacts/CR-226-system-contract-fix-asm-named-allocator.diff
build_evidence: commit-approval/build_evidence/CR-226-build-asm-named-allocator.txt
disasm_proof: analysis/cr226_disasm_proof_2026_04_29.txt
kext_sha256: 6390fe6bfee478127e80bc0ad6002df2b9389af4f2a208a60d1ab0b6598cbacf
kext_uuid: 4DBE481A-E3EE-388B-8535-214A70BE3FA3
kext_size: 16294640
bootkc_undef: 886

## CHANGES_VS_CR-225

CR-226 addresses every CR-225 reviewer-required change:

1. **Force the IO80211NetworkPacket-specific operator new symbol**:
   replaced the C++ call `IO80211NetworkPacket::operator new(0x78)`
   with a call through an `extern "C"` declaration with explicit
   `__asm("__ZN20IO80211NetworkPacketnwEm")` mangled name. This
   mirrors the existing CR-225 technique for the C1 constructor and
   bypasses C++ name lookup entirely:

   ```cpp
   extern "C" void *
   AirportItlwm_IO80211NetworkPacket_operatorNew(unsigned long size)
       __asm("__ZN20IO80211NetworkPacketnwEm");

   ...
   void *mem = AirportItlwm_IO80211NetworkPacket_operatorNew(0x78);
   ```

2. **Binary symbol evidence** (CR-225 reviewer's bullet 1 + 2):
   - `nm -u` of the reviewed binary now shows
     `__ZN20IO80211NetworkPacketnwEm` as an undefined external
     reference.
   - Local disassembly of `AirportItlwm_newIO80211NetworkPacket` at
     file offset 0x4f3a0 shows `callq __ZN20IO80211NetworkPacketnwEm`
     immediately after `movl $0x78, %edi`. Full disasm:
     `analysis/cr226_disasm_proof_2026_04_29.txt`.
   - The CR-225 path (`__ZN8OSObjectnwEm`) is no longer called from
     this allocator function.

3. **Build evidence accuracy** (CR-225 reviewer's bullet 4): the
   build evidence file now lists only symbols actually present in
   the reviewed binary, with an explicit `nm -u`-confirmed list and
   a quoted disasm snippet that matches the binary.

4. **Stale comment cleanup** (CR-225 reviewer's
   `source_comment_staleness` note): historical CR-220 comment block
   in front of the `newPacket` override and historical CR-224 comment
   block in front of `newPacketWithDescriptor` replaced with concise
   CR-226 descriptions matching the actual reviewed code.

## CLAIM_SCOPE

Replace `AirportItlwmIO80211PacketPool::newPacketWithDescriptor`'s body
to allocate a vanilla IO80211NetworkPacket in 1:1 correspondence with
`IOSkywalkNetworkPacket::withPool` (BootKC 0xffffff800297effa decomp),
substituting only the IOSkywalkNetworkPacket-specific
operator-new+vtable-install steps with IO80211NetworkPacket-specific
ones:

```cpp
// asm-named declarations (force-emit BootKC mangled symbols)
extern "C" void *
AirportItlwm_IO80211NetworkPacket_operatorNew(unsigned long size)
    __asm("__ZN20IO80211NetworkPacketnwEm");
extern "C" void
AirportItlwm_IO80211NetworkPacket_C1(void *self, OSMetaClass const *meta)
    __asm("__ZN20IO80211NetworkPacketC1EPK11OSMetaClass");

static IO80211NetworkPacket *
AirportItlwm_newIO80211NetworkPacket(IOSkywalkPacketBufferPool *pool,
                                      IOSkywalkPacketDescriptor *desc,
                                      UInt32 options)
{
    void *mem = AirportItlwm_IO80211NetworkPacket_operatorNew(0x78);
    if (mem == nullptr) return nullptr;

    AirportItlwm_IO80211NetworkPacket_C1(mem, IO80211NetworkPacket::metaClass);
    IO80211NetworkPacket *p = static_cast<IO80211NetworkPacket *>(mem);

    void **vtbl = *reinterpret_cast<void ***>(p);
    typedef bool (*InitFn)(IOSkywalkPacket*, IOSkywalkPacketBufferPool*,
                           IOSkywalkPacketDescriptor*, UInt32);
    if (!reinterpret_cast<InitFn>(vtbl[35])(p, pool, desc, options)) {
        typedef void (*DestructFn)(IOSkywalkPacket*);
        reinterpret_cast<DestructFn>(vtbl[5])(p);
        return nullptr;
    }
    return p;
}
```

Pool factory:
```cpp
IOSkywalkPacket *
AirportItlwmIO80211PacketPool::newPacketWithDescriptor(
    IOSkywalkPacketDescriptor *desc)
{
    return AirportItlwm_newIO80211NetworkPacket(this, desc, 0);
}
```

## REFERENCE_DECOMP

(Identical to CR-225 reference decomp; no changes to BootKC
addresses or function bodies. Repeating for completeness.)

### IO80211NetworkPacket::operator new — actually called by CR-226

BootKC `__ZN20IO80211NetworkPacketnwEm` at 0xffffff80022c591c, 16-byte thunk:
```
push rbp; mov rbp, rsp
mov rsi, rdi                ; size
lea rdi, [rip + 0x13f116]   ; IO80211NetworkPacket kalloc_type_view
pop rbp
jmp 0xffffff8000a4ca30      ; tail call to kalloc_type_impl
```

### IO80211NetworkPacket::MetaClass::alloc — proven NULL by Apple
BootKC 0xffffff80022c5914: `xor eax, eax; ret`. Hardcoded.

### IO80211NetworkPacket(OSMetaClass*) C1 ctor

BootKC 0xffffff80022c5840:
```
push rbp; mov rbp, rsp; push rbx; push rax
mov rbx, rdi
call IOSkywalkNetworkPacket(OSMetaClass*)  ; chains parent
lea rax, [rip + 0x11d0f3]                  ; IO80211NetworkPacket vtable
mov [rbx], rax                             ; install vtable
ret
```

### IOSkywalkNetworkPacket::withPool — reference allocate-then-init

BootKC 0xffffff800297effa: kalloc_type_impl + ctor + vt[35] init +
vt[5] failure-destruct. CR-226 mirrors this 1:1 with
IO80211NetworkPacket-specific operator new + C1 ctor; vt[35] and
vt[5] are inherited from IOSkywalkNetworkPacket via
IO80211NetworkPacket and dispatch to the same implementations.

## LOCAL_DISASSEMBLY_PROOF (CR-225 reviewer's bullet 2)

`otool -tV /Library/Extensions/AirportItlwm.kext/Contents/MacOS/AirportItlwm`
on the reviewed binary at the `AirportItlwm_newIO80211NetworkPacket`
function body (file offset 0x4f3a0):

```
0x4f3b3  movl  $0x78, %edi                       ; size = 0x78
0x4f3b8  callq __ZN20IO80211NetworkPacketnwEm    <-- ✓ IO80211NetworkPacket-specific
0x4f3bd  movq  %rax, -0x28(%rbp)                 ; mem
0x4f3c1  cmpq  $0x0, -0x28(%rbp)
0x4f3c6  jne   0x4f3d2                           ; if mem != 0 -> ctor
0x4f3c8  movq  $0x0, -0x8(%rbp)                  ; return nullptr
0x4f3d0  jmp   0x4f440
0x4f3d2  movq  -0x28(%rbp), %rdi                  ; this
0x4f3d6  movq  0xeb2c6b(%rip), %rax               ; &IO80211NetworkPacket::metaClass
0x4f3dd  movq  (%rax), %rsi
0x4f3e0  callq __ZN20IO80211NetworkPacketC1EPK11OSMetaClass  <-- ✓ C1 ctor
... vt[35] dispatch via [vptr+0x118] ...
... vt[5]  dispatch via [vptr+0x28] on failure ...
```

`__ZN8OSObjectnwEm` is NOT called from this function path.

`nm -u` confirms (filtered):
```
__ZN20IO80211NetworkPacketnwEm                 ✓ present
__ZN20IO80211NetworkPacketC1EPK11OSMetaClass   ✓ present
__ZN20IO80211NetworkPacket9metaClassE          ✓ present
```

Full disasm artifact: `analysis/cr226_disasm_proof_2026_04_29.txt`.

## SYSTEM_CONTRACT_COVERAGE

The produced object IS a real IO80211NetworkPacket allocated from the
class's own kalloc_type_view (not the generic OSObject zone). This
gives:

- Allocation/free pairing (CR-225 reviewer's bullet 3): the allocation
  and the eventual virtual destruct + delete-via-operator-delete will
  both go through IO80211NetworkPacket-specific paths, since the
  vtable's `vt[5]` slot is `IO80211NetworkPacket::~IO80211NetworkPacket()
  (D0)` (which calls `operator delete(this, sizeof(IO80211NetworkPacket))`
  → IO80211NetworkPacket::operator delete → returns to the same
  kalloc_type_view used at allocation). Symmetry preserved.
- Class-specific kalloc_type_view: required by IO80211NetworkPacket's
  internal type-tagging — using a different zone could trigger
  Tahoe-26.x kalloc_type_assert checks at free time.
- vtable identity: 72 entries, IO80211Family-exported, no local-class
  vtable involved. kmutil cross-kext vtable validation has nothing to
  flag.

## WORKAROUND_HUNT (self-audit)

- heuristic timing : NO
- fallback behavior : NO
- masking/suppression : NO
- forced callback : NO
- forced state transition : NO
- forced success / fake success : NO
- forced sync/flush/barrier without reference basis : NO
- guessed state correction : NO
- retry/reorder/poll loop : NO
- replay/re-emit/duplicate publish : NO
- temporary stabilization logic : NO
- best effort behavior : NO
- vtable-swap workaround : NO
- incomplete allocation contract substitute : NO. Allocation now goes
  through `IO80211NetworkPacket::operator new` exactly as the
  reference uses (with binary-disasm proof).
- raw-symbol asm trick : the `extern "C" __asm("...")` declarations
  are the only direct-symbol calls. Standard C++ technique for naming
  exported functions with C++-mangled symbols.

## VERIFICATION

- Build : ** BUILD SUCCEEDED **
- Kext sha256 : 6390fe6bfee478127e80bc0ad6002df2b9389af4f2a208a60d1ab0b6598cbacf
- Kext UUID   : 4DBE481A-E3EE-388B-8535-214A70BE3FA3
- Kext size   : 16294640 bytes
- BootKC undef : 886 (delta +1 vs CR-225's 885 — the new ref is
  `__ZN20IO80211NetworkPacketnwEm`)
- nm -u : `__ZN20IO80211NetworkPacketnwEm` PRESENT
- Disasm  : `0x4f3b8 callq __ZN20IO80211NetworkPacketnwEm` PRESENT
- kmutil load : passes vtable check (gets to user-approval gate, identical
  to CR-225's local result).
- `git diff --cached --check`: passes (verified post-stage).

## CHANGE_FOOTPRINT

- `AirportItlwm/AirportItlwmV2.cpp`:
  - Add `extern "C" __asm("__ZN20IO80211NetworkPacketnwEm")` declaration
    for `AirportItlwm_IO80211NetworkPacket_operatorNew`.
  - Replace `IO80211NetworkPacket::operator new(0x78)` C++ call with
    `AirportItlwm_IO80211NetworkPacket_operatorNew(0x78)`.
  - Replace stale CR-220 comment block in front of `newPacket` override.
  - Replace stale CR-224 comment block in front of `newPacketWithDescriptor`.
- `analysis/cr226_disasm_proof_2026_04_29.txt`: NEW — local disasm proof.
- No header changes.
- No build-script changes.
- No other file is touched.

## STAGE_2_INTENT

If Stage 1 approves, Stage 2 will collect runtime evidence with the
existing CR-217 instrumentation:

```
NEWPACKET FINAL branch=OK
   this=0x????_????
   packet=0x????_????          <-- non-NULL, vptr = canonical
                                   IO80211NetworkPacket vtable address
                                   installed by C1 ctor (BootKC
                                   `lea rax, [rip+0x11d0f3]`).
```

Pool factory `INIT_FALSE_POST_OSARRAY` should no longer fire for the
TX/RX pool creation; pool init should reach a successful state and
STEP 8b should observe non-NULL TX/RX pool pointers.

## NOTE_TO_REVIEWER_ON_USER_REBOOT_STATUS

CR-226 binary is installed at `/Library/Extensions/AirportItlwm.kext`
(install MATCH verified). The user previously noted that the previous
kext (CR-224) failed AuxKC rebuild and the system did not present a
re-approval prompt. CR-226 passes the AuxKC vtable check (verified
locally via `kmutil load -p`), so the next user reboot should
trigger the standard Tahoe approval flow.
