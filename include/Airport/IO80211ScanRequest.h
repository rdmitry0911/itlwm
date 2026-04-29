//
//  IO80211ScanRequest.h
//  itlwm
//
//  Reference-aligned forward declaration of the public direct-call
//  IO80211ScanRequest surface exported by IO80211Family on macOS Tahoe.
//
//  All declared methods are non-virtual direct-call BootKC exports, so
//  the local class declaration deliberately omits any vtable or data
//  layout. Callers only ever hold an `IO80211ScanRequest *` returned by
//  the kernel; the local kext does not allocate, subclass, or take
//  `sizeof` of this class.
//
//  CR-203 — primitive-only batch (BootKC IO80211Family.kc, recovered
//  2026-04-28). This is the second-attempt resubmission of the
//  CR-202 ScanRequest header that was rejected with
//  `workaround_hunt: FAIL_TYPE_ERASURE`. The rejection identified five
//  helpers whose Ghidra C decompile return type `long` did not prove
//  the C++ semantic return type — three (`getChannels`, `getSSID`,
//  `getScanID`) were pointer-shaped (`return *(long *)(this + 0x10) +
//  <offset>`) and two (`has6EChannel`, `is2GScanRequest`) were
//  predicate-shaped through `CONCAT71(... , bVar1)` carrier returns.
//
//  CR-203 narrows the batch to **two helpers** whose return type is
//  proven by raw x86_64 disassembly of BootKernelExtensions.kc to be
//  one-byte AL-only — exactly the macOS x86_64 C++ ABI calling
//  convention for `bool`. Evidence is captured in
//  analysis/cr203_scanrequest_disasm.txt as the verbatim Ghidra raw
//  instruction listing for each function body. The three pointer-
//  shaped helpers from CR-202 are deferred until exact
//  pointer/reference return types are recovered from struct-type
//  metadata; the raw disasm in cr203_scanrequest_disasm.txt confirms
//  all three (including `getScanID`) compute a pointer rather than an
//  integer.
//
//  BootKC anchors (return types proven by raw x86_64 disasm):
//    ffffff8002269c9e  IO80211ScanRequest::has6EChannel()      // bool (AL-only)
//    ffffff8002269cda  IO80211ScanRequest::is2GScanRequest()   // bool (AL-only)
//
//  Raw disasm proof (from analysis/cr203_scanrequest_disasm.txt):
//
//  has6EChannel @ ffffff8002269c9e:
//    MOV AL, byte ptr [RCX + RSI*0x1 + 0x59]   ; AL := flag byte
//    AND AL, 0x20                              ; AL := flag bit
//    SHR AL, 0x5                               ; AL := 0 or 1
//    ...
//    XOR EAX, EAX                              ; loop-empty path: 0
//    POP RBP
//    RET
//
//  is2GScanRequest @ ffffff8002269cda:
//    CMP dword ptr [RCX + RSI*0x1 + 0x54], 0xe ; compare band index
//    SETC AL                                   ; AL := 1 if below 0xe, else 0
//    ...
//    XOR EAX, EAX                              ; loop-empty path: 0
//    POP RBP
//    RET
//
//  In both functions the return value is established in AL (low byte
//  only) and the upper 56 bits of RAX are not zero-extended on every
//  path. This is the macOS x86_64 C++ `bool` ABI: caller is required
//  to truncate to the low byte. A `long` return would have produced an
//  explicit `MOVZX EAX, AL` (zero-extend AL to RAX) or returned the
//  full 64-bit value in RAX. Therefore both helpers are `bool`.
//
//  Deferred from CR-202 / CR-203 candidate set (3 pointer-shaped
//  helpers — raw disasm proves pointer arithmetic, not integer):
//    ffffff800226a532  IO80211ScanRequest::getChannels()       // T* (offset +0x50 in inner struct)
//    ffffff800226a4fa  IO80211ScanRequest::getSSID()           // T* (offset +0x1c in inner struct)
//    ffffff800226a540  IO80211ScanRequest::getScanID()         // T* (offset +0x1554 in inner struct)
//

#ifndef IO80211ScanRequest_h
#define IO80211ScanRequest_h

class IO80211ScanRequest
{
public:
    bool has6EChannel();
    bool is2GScanRequest();
};

#endif /* IO80211ScanRequest_h */
