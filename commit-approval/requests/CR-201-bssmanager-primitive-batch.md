# CR-201 — IO80211BssManager primitive-only batch (decomp-evidenced, NEW header)

- date: 2026-04-28
- stage: STAGE_1_STRUCTURAL
- justification class: REFERENCE_ALIGNMENT_FIX
- supersedes: CR-200
- branch: master
- HEAD: `d3a07c2abccac863e1909aa562051a6ee5687245`
- requested by: Executor+Committer (per `docs/WORKFLOW_ITLWM.md`)

## Summary

Resubmission of CR-200 with the rejection guidance applied. CR-200
declared forty-one helpers' return types from naming convention
(`set*` → `void`, `is*` → `bool`, paired-setter inference, prior-batch
analogy). The reviewer rejected those as
`workaround_hunt: FAIL_EVIDENCE_SHORTCUT` — *"BootKC symbol names
prove the class/method/parameter spelling, but do not prove ordinary
non-template C++ return types."*

CR-201 narrows the batch to **fourteen helpers** whose return type is
unambiguously recovered by Ghidra 12.2's C decompile of
`BootKernelExtensions.kc`. The remaining twenty-seven helpers from
CR-200's candidate set are deferred (twelve with opaque
`undefined4`/`undefined8` decompile placeholders, nine where Ghidra
did not recover the mangled symbol name, six where Ghidra reported
MISSING at the recorded address).

The class is declared with no base class and no data layout; the
local kext does not allocate, subclass, or take `sizeof` of it. None
of the 14 helpers is referenced by any local code path — pre-existing
kext mentions of `IO80211BssManager` are documentation comments only.

The kext is bit-identical to CR-189..CR-200 (sha256 / UUID
unchanged), confirming the change is purely a structural reference
alignment introduction.

## Anomaly chain (carried forward)

A-CR165 → … → A-CR196 → A-CR197 → A-CR198 → A-CR199 → A-CR200 → A-CR201

## Decomp evidence

The full Ghidra decompile output for the 41 candidate addresses is
captured in `analysis/cr201_bssmgr_decomp.c`. It was produced by
running, on `dima@192.168.40.116`:

```
/srv/project/ghidra/build/ghidra_install/ghidra_12.2_DEV/support/analyzeHeadless \
    /srv/project/ghidra_output wifi_analysis_26_3 \
    -readOnly -process BootKernelExtensions.kc \
    -postScript /srv/project/ghidra_additional/DecompileAddrList.py \
    /tmp/cr201_bssmgr_decomp.c <41 candidate addresses>
```

For each of the 14 kept helpers below, the *Decomp evidence* column
quotes the function header from that decompile output verbatim. The
declared header type is the verbatim-matching C++ type (`void` →
`void`, `bool` → `bool`, `byte` → `unsigned char`, `ulong` →
`unsigned long`).

## Confirmed exported symbols (decomp-evidenced) — 14 rows

| # | Address              | Symbol                                                  | Decomp return | Header type     | Decomp evidence (verbatim) |
|---|----------------------|---------------------------------------------------------|---------------|-----------------|----------------------------|
| 1 | `0xffffff80022665a0` | `IO80211BssManager::isAssociatedOnHighBand()`           | `byte`        | `unsigned char` | `byte IO80211BssManager__isAssociatedOnHighBand__(void)` |
| 2 | `0xffffff8002266722` | `IO80211BssManager::isAssociatedToAdhoc()`              | `bool`        | `bool`          | `bool IO80211BssManager__isAssociatedToAdhoc__(long param_1)` |
| 3 | `0xffffff8002266cfc` | `IO80211BssManager::resetRateAndIndexSet()`             | `void`        | `void`          | `void IO80211BssManager__resetRateAndIndexSet__(long param_1)` |
| 4 | `0xffffff8002266da8` | `IO80211BssManager::isAssociatedToiOSDevice()`          | `ulong`       | `unsigned long` | `ulong IO80211BssManager__isAssociatedToiOSDevice__(long param_1)` |
| 5 | `0xffffff8002268030` | `IO80211BssManager::setAdHocCreated(bool)`              | `void`        | `void`          | `void IO80211BssManager__setAdHocCreated_bool_(long param_1,undefined1 param_2)` |
| 6 | `0xffffff8002268054` | `IO80211BssManager::setSISOAssoc(bool)`                 | `void`        | `void`          | `void IO80211BssManager__setSISOAssoc_bool_(long param_1,undefined1 param_2)` |
| 7 | `0xffffff8002268066` | `IO80211BssManager::setPrivateMacJoinStatus(bool)`      | `void`        | `void`          | `void IO80211BssManager__setPrivateMacJoinStatus_bool_(long param_1,undefined1 param_2)` |
| 8 | `0xffffff8002268078` | `IO80211BssManager::setDeviceTypeInDhcpAllowStatus(bool)` | `void`      | `void`          | `void IO80211BssManager__setDeviceTypeInDhcpAllowStatus_bool_(long param_1,undefined1 param_2)` |
| 9 | `0xffffff800226808a` | `IO80211BssManager::getPrivateMacJoinStatus()`          | `ulong`       | `unsigned long` | `ulong IO80211BssManager__getPrivateMacJoinStatus__(long param_1)` |
| 10| `0xffffff800226809c` | `IO80211BssManager::getDeviceTypeInDhcpAllowStatus()`   | `ulong`       | `unsigned long` | `ulong IO80211BssManager__getDeviceTypeInDhcpAllowStatus__(long param_1)` |
| 11| `0xffffff80022680ae` | `IO80211BssManager::setAssociateToHotspotInWoWMode(bool)` | `void`      | `void`          | `void IO80211BssManager__setAssociateToHotspotInWoWMode_bool_(long param_1,undefined1 param_2)` |
| 12| `0xffffff80022680c0` | `IO80211BssManager::isAssociateToHotspotInWoWMode()`    | `ulong`       | `unsigned long` | `ulong IO80211BssManager__isAssociateToHotspotInWoWMode__(long param_1)` |
| 13| `0xffffff8002268106` | `IO80211BssManager::get6gStandAloneTopology()`          | `ulong`       | `unsigned long` | `ulong IO80211BssManager__get6gStandAloneTopology__(long param_1)` |
| 14| `0xffffff8002268118` | `IO80211BssManager::set6gStandAloneTopology(bool)`      | `void`        | `void`          | `void IO80211BssManager__set6gStandAloneTopology_bool_(long param_1,undefined1 param_2)` |

### Type-mapping rationale

- `void` → `void`. Ghidra emits no return value; bodies have explicit
  `return;` statements.
- `bool` → `bool`. Ghidra explicitly typed the return as `bool`; the
  body returns `sVar1 == 1`.
- `byte` → `unsigned char`. Ghidra's 8-bit unsigned-integer
  placeholder. We deliberately do *not* re-type to `bool` based on
  the body's `return bVar1 ^ 1;` 0/1 invariant — that would be
  inference rather than the literal decompile type.
- `ulong` → `unsigned long`. Width and signedness match. Bodies
  return `CONCAT71(... & 0xffffffffffffff01)` but the decompile
  itself emits `ulong`, so we stay with `unsigned long` rather than
  re-typing as `bool` based on the AND-mask.

## Local change

NEW source files:

- `include/Airport/IO80211BssManager.h`
  - declares `class IO80211BssManager` with no base class
  - 14 public non-virtual method declarations
  - 14 BootKC address anchors in the leading comment block, each
    with the decompile return type in a trailing inline comment
  - no `void *` return types
  - no `undefined*`-placeholder-derived return types
- `analysis/cr201_bssmgr_decomp.c` — full Ghidra C decompile output
  for all 41 CR-200 candidate addresses; supports the per-symbol
  evidence column above
- `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/137_bssmanager_primitive_batch_2026_04_28.yaml`
  — REWRITTEN (`cr: CR-201`, `supersedes: CR-200`, `batch_size: 14`,
  per-symbol `decomp_type` field, full deferred-set listing)
- `docs/reference/AppleBCMWLAN_bssmanager_primitive_batch_2026_04_28.md`
  — REWRITTEN (CR-201 header, decomp-evidenced 14-row table, three
  deferred-reason sections)

Supporting docs updated:

- `analysis/ANALYSIS_REPORT_2026-04-28.md` — A-CR200 marked
  superseded; new A-CR201 section
- `docs/tahoe_discrepancy_inventory.md` — item 200 marked
  superseded; new item 201
- `docs/tahoe_signal_chain_audit.md` — CR-200 closure replaced with
  REJECTED note; new CR-201 closure

This batch is layered on top of the CR-199 IO80211BSSBeacon changes
(the artifact contains both the carry-forward IO80211BSSBeacon.h
header from CR-199 and the new IO80211BssManager.h header).

## Build evidence

- `commit-approval/build_evidence/CR-201-build-tahoe-bssmanager-primitive-batch-20260428.txt`
- `commit-approval/build_evidence/CR-201-build-regdiag-bssmanager-primitive-batch-20260428.txt`

### Tahoe build

- command: `scripts/build_tahoe.sh`
- result: `** BUILD SUCCEEDED **`
- BootKC undef-symbol verification: OK — all 884 undefined symbols resolve
- kext sha256 pre  = `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`
- kext sha256 post = `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`
- UUID pre/post   = `BA3D771F-F079-33FF-94E5-C792E66237D8`
- binary invariance: PASS

### regdiag build

- command: `scripts/build_regdiag.sh`
- regdiag sha256: `6915020cdd70a07c4b77b2946dd5605bc378fc0677119506ae691a7968f01fad`
- binary invariance: PASS

## Diff

- artifact: `commit-approval/artifacts/CR-201-bssmanager-primitive-batch.diff`
- files changed: 144
- byte-for-byte identical to the live `git diff --binary HEAD`
  (verifiable via `cmp commit-approval/artifacts/CR-201-bssmanager-primitive-batch.diff <(git diff --binary HEAD)` returning exit 0)
- `git diff --check HEAD` exit 0 (clean)

## Count consistency

request "14" / table 14 rows / header 14 decls / header 14 BootKC
anchors / YAML batch_size 14 / YAML 14 `symbols:` entries / analysis
A-CR201 "14" / inventory item 201 batch_size 14 / signal audit CR-201
closure "14" / reference doc "fourteen (14)" / build evidence "14".

## Non-claims

CR-201 does **not**:

- synthesize `AppleBCMWLANPCIeSkywalkPacket`;
- write `packet+0x78`;
- change packet allocation, queueing, input, or output behavior;
- force EAPOL TX, key install, RSN, DHCP, link, or data success;
- add retry, replay, delay, poll loop, packet synthesis, or deauth masking;
- call any of the 14 BssManager methods from any local code path;
- declare `IO80211BssManager` with any base class or data layout;
- include kernel-internal types whose definitions are not yet recovered;
- substitute `void *` for any unrecovered kernel-internal return type;
- promote any `undefined4`/`undefined8` Ghidra placeholder to a
  concrete C++ return type (those helpers are deferred, not
  rewritten);
- claim coverage for the twenty-seven deferred CR-200 helpers.

## Verification plan

Structural (already executed):

- `git diff --check HEAD` — exit 0 (clean)
- `cmp` between artifact and live diff — exit 0 (byte-for-byte identical)
- `scripts/build_tahoe.sh` — bit-identical
- `scripts/build_regdiag.sh` — bit-identical
- BootKC undef-symbol verification — all 884 undefined symbols resolve
- Internal count consistency: see "Count consistency" section.
- Per-symbol decomp evidence: every row in the 14-row table cites the
  verbatim Ghidra C decompile function header from
  `analysis/cr201_bssmgr_decomp.c`.

Runtime: not applicable for CR-201. The kext is bit-identical to
CR-189..CR-200; no runtime change is expected.

## Deferred work

### Deferred from CR-200 candidate set (this CR's deferred subset, 27 entries)

- Twelve helpers with opaque `undefined4`/`undefined8` decompile
  placeholder return type:
  `isAssociatedOn2G`, `isAssociatedOn5G`, `isAssociatedOn6G`,
  `isAssociated`, `setLastBSSRssi`,
  `isAssociatedToLPHSCapableiOSHotspot`, `get6GMode`, `isEAPJoin`,
  `is8021XJoin`, `isDynamicWEP`, `isLikelyOrbiMeshNetwork`,
  `isMloConnection`.
- Nine helpers where Ghidra did not recover the mangled symbol name
  (function present at the address but emitted as `FUN_<addr>`):
  `set6GMode`, `setBandInfoBitmap`, `setAssocSSID`,
  `setOWEOpenSSID`, `setAssocColocatedNetworkScopeId`,
  `setAssocRSNIE`, `isAssociatedTo11ax`, `isAdhocCreated`,
  `isSISOAssoc`.
- Six helpers where Ghidra reported MISSING at the recorded address:
  `freeResources`, `updateRxRate`, `dumpBSS`, `setNetworkFlags`,
  `setAssociatedAuthType`, `isAssociatedToOpenBss`.

### Deferred from earlier surveys (carry-forward)

- Sixty nine IO80211BssManager helpers whose signatures reference
  kernel-internal types (`apple80211_*`, `ether_addr *`, `Bands &`,
  `IO80211AuthContext &`, `IO80211BSSBeacon *`,
  `IO80211ScanCacheStore *`, `CCLogStream *`, `env_bss_info[_band]&`,
  `iOSIESubTypeCapabilities &`, `iOSIESubTypePHInfo &`,
  `apple_mlo_context &`, `apple80211_softap_wifi_network_info &`,
  `IO80211Logger *`, block-pointer-typed callbacks).
- All out-parameter primitive getters: `getCurrentRSSI(int&)`,
  `getCurrentSNR(short&)`, `getCurrentNoise(short&)`,
  `getCurrentCCA(signed char&)`, `getCurrentRxRate(unsigned int&)`,
  `getCurrentDTIMPeriod(unsigned int*)`,
  `getCurrentBeaconPeriod(unsigned int*)`,
  `getCurrentSSID(unsigned char*, unsigned char&)`,
  `getCurrentIEList(unsigned char*, unsigned int&)`,
  `getCurrentColocatedNetworkScopeId(unsigned char*, unsigned int&)`,
  `getAssocSSID(unsigned char*, unsigned long long&) const`,
  `getOWEOpenSSID(unsigned char*, unsigned long long&) const`,
  `getAssocColocatedNetworkScopeId(unsigned char*, unsigned long long&) const`,
  `getAssocRSNIE(unsigned char*, unsigned long long&) const`.
- Helpers with unrecovered return type: `getOPMode()`,
  `getBSSPreference()`, `getNetworkFlags() const`,
  `getCurrentAuthType()`, `getCurrentBSSAKMs()`,
  `getCurrentBSS() const`, `getLogger() const`.
- IO80211BssManager `getMetaClass()`, `MetaClass::*`, ctors, dtors,
  `operator new` / `operator delete`, `free()` virtual override —
  out of scope for a no-base no-data-layout standalone-stub class.
- Carry-forward deferrals from CR-194..CR-199 (Peer kernel-typed
  helpers, ambiguous-return primitive helpers, Apple80211_*-typed
  InfraInterface helpers, IO80211SkywalkInterface non-virtual
  collisions, IO80211PeerManager kernel-typed helpers,
  IO80211LinkQualityMonitor kernel-typed helpers, IO80211BSSBeacon
  void*-typed helpers, Peer constructor/init body, gate context for
  per-band RSSI setters and `clearCacheState`, writer of
  `IO80211InfraInterface this[0x24]+0x58 == 1` precondition,
  `handleKeyDone` body recovery via raw disasm at
  `0xffffff80022e6f9c`).
