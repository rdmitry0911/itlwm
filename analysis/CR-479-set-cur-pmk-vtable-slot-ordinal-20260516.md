# CR-479 - Tahoe IO80211SkywalkInterface vtable slot ordinal for `setCUR_PMK` (`+0x1770`)

Document date: 2026-05-16
Anomaly: STA_TAHOE_SKYWALK_WPA2_PSK_4WAY_INCOMPLETE_NO_PMK_DELIVERY
Scope: closes the named bounded follow-up authorized by the
`COMMIT_DECISION_CR-479-stage1-tahoe-skywalk-CUR_PMK-CIPHER_KEY-external-pmk-ingestion-rev8`
review record. Documentation / decomp-only; no source change is
proposed by this evidence record.

## Summary

The Tahoe `apple80211setCUR_PMK` ingress trampoline at
`0xffffff80021eb3b9` delivers the userspace `apple80211_pmk *` payload
to the Skywalk virtual receiver at vtable offset `+0x1770` of the
target `IO80211SkywalkInterface` instance after passing the command
gate at vtable `+0xcc8`. This document records the exact Apple
absolute vtable slot ordinal that corresponds to that offset, the
recovered semantic identity of the slot in the base
`IO80211SkywalkInterface` vtable, the recovered concrete subclass
override on the Apple Bcom path, and the structural gap that a future
semantic Stage 1 will have to close in `include/Airport/IO80211InfraProtocol.h`
before any local override of `setCUR_PMK` can land at the matching
offset.

## Vtable identification

| Item                                | Value                                         |
| ---                                 | ---                                           |
| Base vtable                         | `__ZTV23IO80211SkywalkInterface`              |
| Base vtable address                 | `0xffffff80023e5950`                          |
| SET-side trampoline                 | `apple80211setCUR_PMK @ 0xffffff80021eb3b9`   |
| Command gate vtable offset          | `+0xcc8`                                      |
| Skywalk virtual receiver offset     | `+0x1770`                                     |
| Absolute zero-based slot index      | `750` (`0x1770 / 0x8`)                        |
| Concrete subclass override class    | `AppleBCMWLANIO80211APSTAInterface`           |
| Subclass vtable address             | `0xffffff8001777508`                          |
| Subclass `+0x1770` target           | `0xffffff8000b72960`                          |

The base-vtable address matches the symbol `__ZTV23IO80211SkywalkInterface`
shown by Ghidra's primary symbol lookup against the recovered KDK
symbol map (`/srv/project/ghidra_output/kdk_symbols.txt`). The slot
offset `+0x1770` and the underlying command-gate-then-Skywalk-receiver
path were recorded in the prior
`docs/reference/CR-479-cur-pmk-userclient-dispatch-evidence-20260516.md`.

## Slot semantic identity: `_RESERVED_IO80211SkywalkInterface_11`

The base `IO80211SkywalkInterface` slot `+0x1770` (slot 750) points to
a 32-byte stub function at `0xffffff8000a01240` whose body is the
canonical OSMetaClass reserved-slot panic thunk:

```
ffffff8000a01240 PUSH RBP
ffffff8000a01241 MOV  RBP,RSP
ffffff8000a01244 LEA  RDI,[RIP + class-name-table]
ffffff8000a0124b MOV  ESI,0xb            ; reserved-slot index 11
ffffff8000a01250 CALL 0xffffff8000a00a00 ; OSMetaClass reserved-slot panic
```

The trap helper at `0xffffff8000a00a00` reads `[this+0x18]` (the
OSObject metaclass pointer), dispatches through the metaclass vtable
slot `+0x148` to recover the class name, then calls into the kernel
panic / OSReport path at `0xffffff8000b8bf6c` with the slot index
preserved in `ECX = ESI`. This is the C++ OSMetaClass reserved-slot
trap pattern used in Apple's IOKit ABI to keep extension slots
ABI-stable across kernel releases.

The slot index `0xb = 11` carried in `ESI` identifies the slot as
`_RESERVED_IO80211SkywalkInterface_11`. The reserved-slot numbering
is verified to be contiguous across adjacent vtable positions:

| vtable slot | vtable offset | stub address              | `ESI` value | reserved index |
| ---         | ---           | ---                       | ---         | ---            |
| 739         | `+0x1718`     | `0xffffff8000a010e0`      | `0x0`       | `0`            |
| 748         | `+0x1760`     | `0xffffff8000a01200`      | `0x9`       | `9`            |
| 749         | `+0x1768`     | `0xffffff8000a01220`      | `0xa`       | `10`           |
| **750**     | **`+0x1770`** | **`0xffffff8000a01240`**  | **`0xb`**   | **`11`**       |
| 751         | `+0x1778`     | `0xffffff8000a01260`      | `0xc`       | `12`           |
| 752         | `+0x1780`     | `0xffffff8000a01280`      | `0xd`       | `13`           |

Each stub is exactly `0x20` bytes long with identical instruction
shape; only the immediate value in `MOV ESI, imm32` changes. The
first reserved slot (`ESI = 0`) sits at vtable slot `739`
(offset `+0x1718`), confirming that the reserved-slot block of
`IO80211SkywalkInterface` starts at slot `739` in the absolute
zero-based numbering used by Apple's C++ ABI.

## Concrete subclass override on the Apple Bcom path

The Apple Bcom production driver vtable for
`AppleBCMWLANIO80211APSTAInterface` at `0xffffff8001777508` overrides
slot `+0x1770` with the function at `0xffffff8000b72960`. That
function is the live Tahoe `setCUR_PMK` body delivered by the
`apple80211setCUR_PMK` trampoline on Apple hardware. Its prologue
takes the `apple80211_pmk *` argument in `RSI`, preserves it in `RBX`,
then iterates through the global registered-manager table at
`[0xffffff800132a6f0]`. For each non-null manager entry it dereferences
the manager object pointer at `[mgr + 0x20]`, checks that the manager
vtable callback slot at `+0x788` is non-null, and calls
`[[mgr + 0x20] + 0x788](mgr, pmk_arg, ..., RDX = 2)` to deliver the
PMK to per-radio hardware logic. The function exits normally without
returning a value (`void` semantics consumed inside the trampoline).

This recovery establishes that:

- the Apple base `IO80211SkywalkInterface` does not provide a default
  implementation for slot `750`; calling it on a bare base instance
  panics through the reserved-slot trap;
- Tahoe binds the userspace `apple80211setCUR_PMK` SET ioctl to
  reserved slot `11` of `IO80211SkywalkInterface`;
- the concrete driver class that exposes the SET behavior on Apple
  hardware (`AppleBCMWLANIO80211APSTAInterface`) overrides the slot
  with a real implementation through normal C++ vtable inheritance;
- there is no BSDCommand-side `setCurPmk` helper in IO80211Family on
  Tahoe (confirmed in
  `docs/reference/CR-479-cur-pmk-userclient-dispatch-evidence-20260516.md`),
  so the only SET-side delivery path is the vtable receiver at
  `+0x1770`.

## Local-header alignment: gap between current `[663]` and target `[750]`

The local header `include/Airport/IO80211InfraProtocol.h` declares
194 pure virtual methods spanning the Apple absolute slot range
`[470]`-`[663]` (75 GET methods from slot `[470]` through slot
`[544]` and 119 SET methods from slot `[545]` through slot `[663]`).
The slot markers used in the header comments are Apple's absolute
zero-based slot numbers in the
`IO80211SkywalkInterface` / `IO80211InfraInterface` / `IO80211InfraProtocol`
combined vtable. Validation probes against the BootKC 26.3 vtable
confirmed:

| local marker          | Apple slot index | Apple vtable offset |
| ---                   | ---              | ---                 |
| `[470]` (first GET)   | `470`            | `+0x0eb0`           |
| `[517]` `getCUR_PMK`  | `517`            | `+0x1028`           |
| `[545]` `setCIPHER_KEY` | `545`          | `+0x1108`           |
| `[663]` `setTX_MODE_CONFIG` (last) | `663` | `+0x14b8`         |

`setCUR_PMK` lives at slot index `750`, which is `87` slots beyond the
current end of the local header. The future semantic Stage 1 that
implements the local override has to extend
`include/Airport/IO80211InfraProtocol.h` from `[663]` to `[750]` so
that the matching local C++ vtable position lines up byte-for-byte
with the Apple vtable position the trampoline dereferences. The
extension splits into three contiguous Apple-slot regions:

- **Slots `[664]`-`[738]` (75 slots)**: concrete Apple-provided
  virtuals that need name + signature recovery from BootKC decomp
  evidence before being declared as pure virtuals in the local
  header. Validation probes confirmed that base
  `IO80211SkywalkInterface` provides non-stub function bodies for
  many of these slots; e.g. slot `[700]` resolves to
  `0xffffff800237c970` inside the `IO80211SkywalkInterface` text
  region (`0xffffff800227xxxx`-`0xffffff800228xxxx`).
- **Slots `[739]`-`[749]` (11 slots)**: reserved-slot stubs
  `_RESERVED_IO80211SkywalkInterface_0` through `_10`. These should
  be declared as opaque pure virtuals (e.g.
  `virtual IOReturn _RESERVED_IO80211SkywalkInterface_N(void *) = 0;`)
  purely to preserve vtable layout; their bodies are not used by the
  Apple SET-side delivery path.
- **Slot `[750]`**: the new
  `virtual IOReturn setCUR_PMK(apple80211_pmk *) = 0;` declaration.
  The matching `setCUR_PMK` override in `AirportItlwmSkywalkInterface`
  is what the future semantic Stage 1 will wire to the local
  credential-safe `installExternalPmkLocked(...)` helper described in
  `analysis/ANALYSIS_REPORT_2026-05-16.md`.

This means the named bounded follow-up authorized by the rev8 review
is closed at the granularity of "what is slot `+0x1770`"
(`_RESERVED_IO80211SkywalkInterface_11`, absolute slot index `750`,
intended `setCUR_PMK` ingress on Tahoe), but the local
`include/Airport/IO80211InfraProtocol.h` extension that converts that
ordinal recovery into an actual local override is a layer-sized
bounded follow-up of its own that will require additional reference
decomp for the 75 concrete virtuals in slots `[664]`-`[738]`. That
header-extension layer must be the next semantic Stage 1 in this
correlation; it is not part of this documentation-only record.

## Reproducer (Ghidra host)

All evidence above is reproducible from the Ghidra host
(`ssh 192.168.40.116`) with the provisioned PyGhidra environment.

- PyGhidra Python interpreter:
  `/srv/project/ghidra_additional/pyghidra_venv_20260516/bin/python`.
- Ghidra install (PyGhidra 3.1.0 / Ghidra `12.2_DEV`):
  `/srv/project/ghidra/build/dist/ghidra_12.2_DEV`.
- Ghidra project: `wifi_analysis_26_3_cr368_full` under
  `/srv/project/ghidra_output`.
- Program: `BootKernelExtensions.kc`.

Minimal PyGhidra reproducer:

```python
import pyghidra
pyghidra.start(install_dir="/srv/project/ghidra/build/dist/ghidra_12.2_DEV")
from ghidra.base.project import GhidraProject
prj = GhidraProject.openProject(
    "/srv/project/ghidra_output",
    "wifi_analysis_26_3_cr368_full",
    True)
prog = prj.openProgram("/", "BootKernelExtensions.kc", True)
af = prog.getAddressFactory(); mem = prog.getMemory()
vt = af.getAddress("ffffff80023e5950")  # __ZTV23IO80211SkywalkInterface
for off in (0x1718, 0x1760, 0x1768, 0x1770, 0x1778, 0x1780):
    print(hex(off), hex(mem.getLong(vt.add(off)) & 0xFFFFFFFFFFFFFFFF))
```

The output matches the reserved-stub run reported in this document.
Disassembly of `0xffffff8000a01240`, `0xffffff8000a01220`,
`0xffffff8000a01260`, the trap target `0xffffff8000a00a00`, and the
APSTA override target `0xffffff8000b72960` is reproducible with the
`getFunctionAt` / `listing.getInstructions` API in the same
PyGhidra session.

## Cross-references

- `docs/reference/CR-479-cur-pmk-carrier-ghidra-20260516.md` -
  recovered Apple carrier contract for the
  `apple80211_pmk` payload structure, AppleBCMWLAN setKey
  case mapping, owner / lifecycle map and credential-safety rules.
- `docs/reference/CR-479-cur-pmk-userclient-dispatch-evidence-20260516.md` -
  recovered IO80211Family CUR_PMK GET / SET dispatch architecture,
  including the trampoline at `0xffffff80021eb3b9`, the absence of a
  BSDCommand `setCurPmk` helper, and the requirement that the only
  SET-side ingress is the Skywalk virtual receiver at `+0x1770`.
- `analysis/ANALYSIS_REPORT_2026-05-16.md` - bounded follow-up
  inventory; this record closes the
  `IO80211SkywalkInterface_SET_vtable_slot_ordinal_for_plus_0x1770`
  entry at the ordinal granularity and identifies the
  header-extension layer that must be the next semantic Stage 1.
