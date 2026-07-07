# IO80211BssManager primitive-only direct-call exports — CR-201

- date: 2026-04-28
- class: IO80211BssManager
- header: `include/Airport/IO80211BssManager.h`
- justification: REFERENCE_ALIGNMENT_FIX (NEW header file)
- batch_size: fourteen (14)
- supersedes: CR-200

## Overview

IO80211BssManager owns the in-driver state for the currently-associated
infra BSS on macOS Tahoe. CR-201 is the resubmission of CR-200 narrowed
to the fourteen helpers whose return type is unambiguously recovered by
Ghidra 12.2's C decompile against `BootKernelExtensions.kc`. The other
twenty-seven helpers from CR-200's surveyed forty-one-symbol candidate
set are deferred — either Ghidra produced an opaque placeholder return
type (`undefined4` / `undefined8`) that cannot be promoted to a C++
type without inference, or Ghidra failed to locate a function entry at
the recorded address (MISSING). The deferred set still includes
helpers whose signatures reference kernel-internal struct/enum types.

The class is declared with no base class and no data layout. The local
kext does not allocate, subclass, or take `sizeof` of it. None of the
14 helpers is referenced by any local code path — all existing
IO80211BssManager mentions in the kext are documentation comments (no
live calls).

2026-07-07 addendum: this document remains the historical CR-201
primitive-only batch record. Later CR-479 work adds live current-BSS writer
wiring for `setMCSIndexSet(apple80211_mcs_index_set_data&)` and
`setRateSet(apple80211_rate_set_data&)`, backed by AppleBCMWLANNetAdapter
caller-side disassembly and symbol metadata rather than by the CR-201
primitive-only return-type table.

## Decomp evidence

The full Ghidra decompile output is captured in
`analysis/cr201_bssmgr_decomp.c`. It was produced by running:

```
/srv/project/ghidra/build/ghidra_install/ghidra_12.2_DEV/support/analyzeHeadless \
    /srv/project/ghidra_output wifi_analysis_26_3 \
    -readOnly -process BootKernelExtensions.kc \
    -postScript /srv/project/ghidra_additional/DecompileAddrList.py \
    /tmp/cr201_bssmgr_decomp.c <41 candidate addresses>
```

The 41-address candidate list is the CR-200 anchor set; the 14 kept
helpers below are exactly those whose decompile begins with a concrete
C return type (`void`, `bool`, `byte`, `ulong`) and a recovered
mangled symbol (i.e. Ghidra's name is not `FUN_<addr>`). Decomp lines
quoted in the table below are copied verbatim from
`analysis/cr201_bssmgr_decomp.c`.

## Recovered exports (decomp-evidenced)

| # | Address              | Symbol                                                  | Decomp return | Header type  | Decomp evidence (verbatim) |
|---|----------------------|---------------------------------------------------------|---------------|--------------|----------------------------|
| 1 | `0xffffff80022665a0` | `IO80211BssManager::isAssociatedOnHighBand()`           | `byte`        | `unsigned char` | `byte IO80211BssManager__isAssociatedOnHighBand__(void)` |
| 2 | `0xffffff8002266722` | `IO80211BssManager::isAssociatedToAdhoc()`              | `bool`        | `bool`       | `bool IO80211BssManager__isAssociatedToAdhoc__(long param_1)` |
| 3 | `0xffffff8002266cfc` | `IO80211BssManager::resetRateAndIndexSet()`             | `void`        | `void`       | `void IO80211BssManager__resetRateAndIndexSet__(long param_1)` |
| 4 | `0xffffff8002266da8` | `IO80211BssManager::isAssociatedToiOSDevice()`          | `ulong`       | `unsigned long` | `ulong IO80211BssManager__isAssociatedToiOSDevice__(long param_1)` |
| 5 | `0xffffff8002268030` | `IO80211BssManager::setAdHocCreated(bool)`              | `void`        | `void`       | `void IO80211BssManager__setAdHocCreated_bool_(long param_1,undefined1 param_2)` |
| 6 | `0xffffff8002268054` | `IO80211BssManager::setSISOAssoc(bool)`                 | `void`        | `void`       | `void IO80211BssManager__setSISOAssoc_bool_(long param_1,undefined1 param_2)` |
| 7 | `0xffffff8002268066` | `IO80211BssManager::setPrivateMacJoinStatus(bool)`      | `void`        | `void`       | `void IO80211BssManager__setPrivateMacJoinStatus_bool_(long param_1,undefined1 param_2)` |
| 8 | `0xffffff8002268078` | `IO80211BssManager::setDeviceTypeInDhcpAllowStatus(bool)` | `void`      | `void`       | `void IO80211BssManager__setDeviceTypeInDhcpAllowStatus_bool_(long param_1,undefined1 param_2)` |
| 9 | `0xffffff800226808a` | `IO80211BssManager::getPrivateMacJoinStatus()`          | `ulong`       | `unsigned long` | `ulong IO80211BssManager__getPrivateMacJoinStatus__(long param_1)` |
| 10| `0xffffff800226809c` | `IO80211BssManager::getDeviceTypeInDhcpAllowStatus()`   | `ulong`       | `unsigned long` | `ulong IO80211BssManager__getDeviceTypeInDhcpAllowStatus__(long param_1)` |
| 11| `0xffffff80022680ae` | `IO80211BssManager::setAssociateToHotspotInWoWMode(bool)` | `void`      | `void`       | `void IO80211BssManager__setAssociateToHotspotInWoWMode_bool_(long param_1,undefined1 param_2)` |
| 12| `0xffffff80022680c0` | `IO80211BssManager::isAssociateToHotspotInWoWMode()`    | `ulong`       | `unsigned long` | `ulong IO80211BssManager__isAssociateToHotspotInWoWMode__(long param_1)` |
| 13| `0xffffff8002268106` | `IO80211BssManager::get6gStandAloneTopology()`          | `ulong`       | `unsigned long` | `ulong IO80211BssManager__get6gStandAloneTopology__(long param_1)` |
| 14| `0xffffff8002268118` | `IO80211BssManager::set6gStandAloneTopology(bool)`      | `void`        | `void`       | `void IO80211BssManager__set6gStandAloneTopology_bool_(long param_1,undefined1 param_2)` |

### Type-mapping rationale

- `void` → `void`. Direct match. No return value emitted by the
  decompile; `return;` statements present.
- `bool` → `bool`. Direct match — Ghidra explicitly typed the return
  as `bool` and the body returns `sVar1 == 1`.
- `byte` → `unsigned char`. `byte` is Ghidra's 8-bit unsigned integer
  placeholder. Declaring as `unsigned char` matches its width and
  signedness without inferring `bool` (which would require trusting
  the body's `return bVar1 ^ 1;` 0/1 invariant rather than the type
  the decompile literally emits).
- `ulong` → `unsigned long`. Direct width and signedness match. The
  bodies return `CONCAT71(... & 0xffffffffffffff01)`, but the decompile
  itself says `ulong`, so `unsigned long` is the verbatim type. We do
  *not* re-type these as `bool` based on the AND-mask, because that
  would be inference rather than decomp evidence.

## Deferred exports (carried over from CR-200)

### Deferred — Ghidra returned an opaque placeholder return type

Each entry below was decompiled successfully but Ghidra emitted
`undefined4` / `undefined8` for the return type. That is Ghidra's
"unknown 32-/64-bit value" placeholder; declaring a concrete C++
return type for these would be inference, which is what the CR-200
review rejected. They are deferred until an alternative recovery
path (caller-side type propagation, or BootKC type metadata) yields
a concrete type.

| Address              | Symbol                                                              | Decomp return |
|----------------------|---------------------------------------------------------------------|---------------|
| `0xffffff80022665ae` | `IO80211BssManager::isAssociatedOn2G()`                             | `undefined8`  |
| `0xffffff8002266624` | `IO80211BssManager::isAssociatedOn5G()`                             | `undefined8`  |
| `0xffffff800226669a` | `IO80211BssManager::isAssociatedOn6G()`                             | `undefined8`  |
| `0xffffff8002266710` | `IO80211BssManager::isAssociated()`                                 | `undefined8`  |
| `0xffffff800226682e` | `IO80211BssManager::setLastBSSRssi()`                               | `undefined8`  |
| `0xffffff8002266e62` | `IO80211BssManager::isAssociatedToLPHSCapableiOSHotspot()`          | `undefined8`  |
| `0xffffff8002266fcc` | `IO80211BssManager::get6GMode()`                                    | `undefined4`  |
| `0xffffff800226703e` | `IO80211BssManager::isEAPJoin()`                                    | `undefined8`  |
| `0xffffff800226709a` | `IO80211BssManager::is8021XJoin()`                                  | `undefined8`  |
| `0xffffff80022670de` | `IO80211BssManager::isDynamicWEP()`                                 | `undefined8`  |
| `0xffffff800226733c` | `IO80211BssManager::isLikelyOrbiMeshNetwork()`                      | `undefined8`  |
| `0xffffff8002268646` | `IO80211BssManager::isMloConnection()`                              | `undefined8`  |

### Deferred — Ghidra did not recover the mangled symbol name (`FUN_<addr>`)

These addresses decompiled successfully, but Ghidra's name for the
function is the placeholder `FUN_<addr>` — meaning the analyzer did
not associate the BootKC mangled symbol with the function entry in
this pass. Declaring a header export against an address whose
decompile does not even confirm the class/method name would be
weaker evidence than required.

| Address              | Symbol (per BootKC mangle)                                                  | Ghidra name           |
|----------------------|------------------------------------------------------------------------------|-----------------------|
| `0xffffff80022661a6` | `IO80211BssManager::set6GMode(unsigned int)`                                 | `FUN_ffffff80022661a6` |
| `0xffffff8002266fee` | `IO80211BssManager::setBandInfoBitmap(unsigned int)`                         | `FUN_ffffff8002266fee` |
| `0xffffff800226713c` | `IO80211BssManager::setAssocSSID(unsigned char const*, unsigned long)`       | `FUN_ffffff800226713c` |
| `0xffffff80022671de` | `IO80211BssManager::setOWEOpenSSID(unsigned char const*, unsigned long)`     | `FUN_ffffff80022671de` |
| `0xffffff8002267236` | `IO80211BssManager::setAssocColocatedNetworkScopeId(unsigned char const*, unsigned long)` | `FUN_ffffff8002267236` |
| `0xffffff8002267afa` | `IO80211BssManager::setAssocRSNIE(unsigned char const*, unsigned long)`      | `FUN_ffffff8002267afa` |
| `0xffffff8002268000` | `IO80211BssManager::isAssociatedTo11ax() const`                              | `FUN_ffffff8002268000` |
| `0xffffff800226801e` | `IO80211BssManager::isAdhocCreated() const`                                  | `FUN_ffffff800226801e` |
| `0xffffff8002268042` | `IO80211BssManager::isSISOAssoc() const`                                     | `FUN_ffffff8002268042` |

### Deferred — Ghidra reported MISSING at the recorded address

Ghidra could not locate a function entry at these addresses in this
pass. (Likely an artifact of how the BootKC was re-imported between
analysis runs.) These are deferred until a re-run with the correct
function-entry table.

| Address              | Symbol                                                                  |
|----------------------|--------------------------------------------------------------------------|
| `0xffffff80022662fe` | `IO80211BssManager::freeResources()`                                     |
| `0xffffff8002266ac2` | `IO80211BssManager::updateRxRate(int)`                                   |
| `0xffffff8002266d68` | `IO80211BssManager::dumpBSS(char*, unsigned int, unsigned int) const`    |
| `0xffffff8002266f9a` | `IO80211BssManager::setNetworkFlags(bool, unsigned int)`                 |
| `0xffffff8002267abc` | `IO80211BssManager::setAssociatedAuthType(unsigned char*, unsigned short)` |
| `0xffffff80022680e0` | `IO80211BssManager::isAssociatedToOpenBss()`                             |

### Deferred — kernel-internal type signatures (carried over from CR-200)

The kernel-internal-typed helpers identified in CR-200's deferred
section remain deferred. Their signatures reference `apple80211_*`,
`ether_addr*`, the `Bands` enum, `IO80211AuthContext&`,
`IO80211BSSBeacon*`, `IO80211ScanCacheStore*`, `CCLogStream*`,
`env_bss_info[_band]*`, `iOSIESubTypeCapabilities&`,
`iOSIESubTypePHInfo&`, `apple_mlo_context&`,
`apple80211_softap_wifi_network_info&`, `IO80211Logger*`, etc.,
which are not yet recovered.

## Non-claims

- No claim of structural layout (vtable / data members) for the class.
- No claim that any of these 14 helpers is reached from the running
  kext on master — they are header-only forward declarations.
- No claim about the deferred 27 helpers' return types; their decomp
  evidence is opaque and recovery is left to a future CR.

## Count consistency

- Header method declarations: **14**
- Header BootKC anchor lines: **14**
- YAML `batch_size`: **14**
- YAML `symbols:` entries: **14**
- This document's recovered-exports table rows: **14**
- analysis/ANALYSIS_REPORT_2026-04-28.md A-CR201 entry: **14**
- docs/tahoe_discrepancy_inventory.md item 201: **14**
- docs/tahoe_signal_chain_audit.md CR-201 closure: **14**
