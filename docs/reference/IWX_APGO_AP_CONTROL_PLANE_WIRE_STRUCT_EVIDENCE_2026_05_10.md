# Apple AP control-plane wire-struct decomp evidence (research-only)

This document records direct Ghidra decomp evidence for a subset of the Apple AP/APSTA control-plane setters that the host APSTA owner consumes. It is research evidence for the open `RESEARCH_FIRST` route assignment after the closed-gate iwx AP/GO HAL boundary at basis commit 22414ff4. The content here is descriptive only and does not introduce any source typedef, source compilation unit change, build script change, or runtime path change.

The recovered evidence comes from direct decompilation of the Apple `AppleBCMWLANIO80211APSTAInterface` setters in `BootKernelExtensions.kc` on the Ghidra reference host `192.168.40.116` under `/srv/project/ghidra*`. Each per-selector section records the entry-point address, the field offsets observed in the decompiled body, the validation/defaulting branches, and the side effects into the recovered AP-mode private state block at `AppleBCMWLANIO80211APSTAInterface + 0x130`. Field names mirrored locally in `AirportItlwm/AirportItlwmAPSTAInterface.hpp` are referenced by their existing recovered names.

Wherever the cited Ghidra output contains decompiler warnings (`Control flow encountered bad instruction data`, `Bad instruction - Truncating control flow here`, `Could not recover jumptable`, `Subroutine does not return`, `Treating indirect jump as call`), this document marks the affected outputs explicitly. Warned outputs are treated as direct decompile output with residual uncertainty, not as a verified full body.

## Reproducer

- Host: `ssh 192.168.40.116`.
- Ghidra build: `/srv/project/ghidra/build/ghidra_install/ghidra_12.2_DEV/support/analyzeHeadless`.
- Ghidra project: `/srv/project/ghidra_output` project `wifi_analysis_26_3` program `BootKernelExtensions.kc`.
- Per-target script: `/srv/project/ghidra_output/CR464LayerADecompV2.py` (substring symbol search) and `/srv/project/ghidra_output/CR464DecompByAddrV3.py` (by-address fallback).
- Output directory: `/srv/project/ghidra_output/cr464_layer_a/` with one `.c` per target.
- Address-list survey: `/srv/project/ghidra_output/cr464_layer_a/all_apsta_funcs.txt` (175 functions in `0xffffff8001685000..0xffffff8001695000`).
- Host capacity at run time: `nproc=28`, `mem total=49152 MB`, `mem free=14628 MB`, `load 0.62 0.75 0.70`. Headless script paths set; no swap, no I/O bottleneck observed during the batch.

## Note on KC-vs-KDK address offsets

Symbol addresses in `kdk_symbols.txt` and the symbol addresses in the loaded `BootKernelExtensions.kc` differ by a constant `+0x50` for the `AppleBCMWLANIO80211APSTAInterface` setter range. All addresses below are the Ghidra-resolved BootKC addresses. The offset is consistent across `setHOST_AP_MODE`, `setHostApModeInternal`, `setSOFTAP_TRIGGER_CSA`, `setSOFTAP_WIFI_NETWORK_INFO_IE`, `setRSN_CONF`, `holdSoftAPPowerAssertion`, and `setCHANNEL`.

## Inventory of recovered functions and their decompile-output state

The "Output state" column records whether the cited Ghidra output is clean (no warnings) or warned (one or more decompiler warnings present). Warned outputs are still useful evidence for individually quoted reads/writes, but their bodies must not be treated as fully recovered without follow-up analysis.

| Symbol | BootKC entry | Output file | Output state |
|---|---|---|---|
| `setHOST_AP_MODE(apple80211_network_data*)` | `0xffffff80016884ae` | `01_setHOST_AP_MODE.c` | warned (multiple `Subroutine does not return`) |
| `setHostApModeInternal(apple80211_network_data*)` | `0xffffff8001688bc2` | `02_setHostApModeInternal.c` | warned (`Control flow encountered bad instruction data`, `Bad instruction - Truncating control flow here`, `Could not recover jumptable`, multiple `Subroutine does not return`) |
| `setSOFTAP_TRIGGER_CSA(apple80211_softap_csa_params*)` | `0xffffff800168e0ae` | `04_setSOFTAP_TRIGGER_CSA.c` | warned (multiple `Subroutine does not return`) |
| `setSOFTAP_WIFI_NETWORK_INFO_IE(apple80211_softap_wifi_network_info*)` | `0xffffff800168e602` | `06_setSOFTAP_WIFI_NETWORK_INFO_IE.c` | warned (two `Subroutine does not return`) |
| `setRSN_CONF(apple80211_rsn_conf_data*)` | `0xffffff800168e85c` | `07_setRSN_CONF.c` | warned (`Subroutine does not return`) |
| `getHOST_AP_MODE_HIDDEN(apple80211_host_ap_mode_hidden_t*)` | `0xffffff80016882ea` | `08_getHOST_AP_MODE_HIDDEN.c` | warned (`Control flow encountered bad instruction data`, `Subroutine does not return`, `Bad instruction - Truncating control flow here`) |
| `getSOFTAP_PARAMS(apple80211_softap_params*)` | `0xffffff800168e7f4` | `09_getSOFTAP_PARAMS.c` | clean |
| `reset()` | `0xffffff8001686cc6` | `12_resetAPSTA.c` | warned (one `Subroutine does not return`) |
| `free()` | `0xffffff8001687828` | `13_freeAPSTA.c` | warned (`Subroutine does not return`, `Could not recover jumptable`, `Treating indirect jump as call`) |
| `setMaxAssoc` (helper, anonymous in current Ghidra project) | `0xffffff800168c6ac` | `15_setMaxAssoc.c` | warned (two `Subroutine does not return`) |
| `setCHANNEL(apple80211_channel_data*)` | `0xffffff800168dcfa` | `19_setCHANNEL_APSTA.c` | warned (multiple `Subroutine does not return`) |
| `holdSoftAPPowerAssertion()` | `0xffffff800168dbc2` | `20_holdSoftAPPowerAssertion.c` | warned |
| `releaseSoftAPPowerAssertion()` | `0xffffff80016937c2` | `21_releaseSoftAPPowerAssertion.c` | warned |
| `setSTA_AUTHORIZE(apple80211_sta_authorize_data*)` | `0xffffff800168f016` | `25_setSTA_AUTHORIZE.c` | warned |
| `setSTA_DEAUTH(apple80211_sta_disassoc_data*)` | `0xffffff800168f14c` | `26_setSTA_DEAUTH.c` | warned |
| `handleEvent(wl_event_msg_t*)` | `0xffffff800168faa0` | `27_handleEvent_APSTA.c` | warned (many `Subroutine does not return`) |
| `getSOFTAP_STATS(apple80211_softap_stats*)` | `0xffffff800168e838` | `28_getSOFTAP_STATS.c` | clean |

The setters `setHOST_AP_MODE_HIDDEN`, `setSOFTAP_PARAMS`, `setSOFTAP_EXTENDED_CAPABILITIES_IE`, `initSoftAPParameters`, `setBeaconInterval`, `enableAPInterface`, `setSSID`, `setCIPHER_KEY`, and `configureManagementFrameProtectionForSoftAP` are not currently auto-discovered as separate function entries in the `wifi_analysis_26_3` Ghidra project's `0xffffff8001685000..0xffffff8001695000` range. Their entry points are present in `kdk_symbols.txt` (offset by `+0x50` from BootKC). Recovering them requires a follow-up Ghidra pass that explicitly creates functions at those addresses (overmerged-boundary classification per project rules).

## Selector 352 — `apple80211_softap_wifi_network_info`

Setter: `setSOFTAP_WIFI_NETWORK_INFO_IE` at `0xffffff800168e602`. The cited Ghidra output `06_setSOFTAP_WIFI_NETWORK_INFO_IE.c` has two `Subroutine does not return` annotations on diagnostic-emit branches; the success path between them is straight-line. The decompiled core is:

```c
char cVar1 = featureFlagIsBitSet(core, 0x46);
if (cVar1 != 0) {
    if (*(uint8_t *)(param_2 + 3) < 0x21) {       /* validation: byte at +3 < 0x21 (33) */
        memcpy(state + 0x2c, param_2, 0x24);      /* copy 36 bytes into state +0x2c */
    }
    /* fall-through diagnostics path; returns 0xe00002c2 = APSTA invalid SoftAP info */
}
return 0;
```

Direct facts proved by this output:

- The setter accepts a 36-byte (`0x24`) payload at `param_2`.
- The byte at `param_2 + 0x03` is gated against the upper bound `0x21`. Values greater than or equal to `0x21` cause the setter to skip the memcpy and return `0xe00002c2` (mirrored locally as `kAirportItlwmAPSTAInvalidSoftAPInfoReturn`).
- The full 36-byte payload is copied verbatim into APSTA private state at owner offset `+0x130 + 0x2c`. The 36-byte size matches the local mirror constant `kAirportItlwmAPSTAWifiNetworkInfoIESize == 0x24` already declared in `AirportItlwm/AirportItlwmAPSTAInterface.hpp`.

Direct facts NOT proved by this output (open):

- The semantics of bytes `0x00`, `0x01`, and `0x02` are not classified by this setter. The byte at `0x03` is consumed only as the upper-bound gate. The remaining 32 bytes (`0x04..0x23`) are written verbatim into APSTA private state but their inner producer-side meaning (which Apple host process emits the 36-byte payload, what each sub-field represents, what defaulting is applied at production time) is not present in this setter's output.
- The producer call chain from the Apple host space (airportd or the Wireless Radio Manager) is not yet decompiled. Without that, the wire layout cannot be expressed as a 1:1 field-named source typedef without speculation.
- The reset/teardown coverage of the `state + 0x2c..0x4f` region by `reset()` is open; the reset section below records only the clears that the cited `reset()` output proves directly.

This is documentation-only evidence. No source typedef is added.

## Selector 349 — `apple80211_softap_csa_params` (selected reads)

Setter: `setSOFTAP_TRIGGER_CSA` at `0xffffff800168e0ae`. Output `04_setSOFTAP_TRIGGER_CSA.c` has multiple `Subroutine does not return` annotations on diagnostic branches; the early validation and the helper-call site are straight-line.

Direct reads observed in this output:
- Pre-condition: state field at `state + 0x26c` (`resetState26c` mirrored locally) must be non-zero AND state byte at `state + 0x329` low bit set; otherwise return `6` (`kAirportItlwmAPSTASoftAPNotReadyReturn`).
- NULL-check: `param_2 == NULL` returns `0x16` (raw invalid argument).
- Field accessed: byte at `param_2 + 0x14` (boolean-like flag); when nonzero, owner triggers `featureFlagIsBitSet(core, 0x46)` then optionally calls `*(plVar1 + 0x222)(plVar1, 0)` on the AppleBCMWLANCore.
- Field accessed: pointer arithmetic `param_2 + 4` is passed as the channel descriptor argument to a CSA preflight helper (`FUN_ffffff8001602f74`, returns int; nonzero == ok).
- A 32-bit channel-encoded value is written by the helper; it is rejected if the encoded value is below `0x10000`.

Open: full field set at `param_2 + 0x04..0x14` and the CSA preflight helper's per-byte reads.

## Selector 77 — `apple80211_rsn_conf_data` (selected reads)

Setter: `setRSN_CONF` at `0xffffff800168e85c`. Output `07_setRSN_CONF.c` has one `Subroutine does not return` annotation on a diagnostic branch; the gate-check and counter loops are straight-line.

Direct reads observed in this output:
- Pre-condition: state byte at `state + 0x29b` low nibble bit `0x10` clear; otherwise short-circuit to `return 0xe00002d5` (`kAirportItlwmAPSTARSNConfRejectedReturn`).
- Field at `param_2 + 0x2c` (uint32) — pairwise cipher count; clamped to `1..8`.
- Field at `param_2 + 0x7c` (uint32) — group cipher count; clamped to `1..8`.
- Field at `param_2 + 0x08` (uint32) — pairwise version; valid range `1..7` (from inner loop bound 7).
- Field at `param_2 + 0x58` (uint32) — group version; valid range `1..7`.

Open: per-cipher sub-block layout at `param_2 + 0x30..0x57` and `param_2 + 0x80..0xbf`; per-cipher value semantics.

## Selector 25 — `apple80211_network_data` (selected reads, helper warned)

Setter: `setHOST_AP_MODE` at `0xffffff80016884ae`; helper: `setHostApModeInternal` at `0xffffff8001688bc2`.

The cited helper output `02_setHostApModeInternal.c` carries the warnings `Control flow encountered bad instruction data`, two `Bad instruction - Truncating control flow here` annotations, one `Could not recover jumptable`, and multiple `Subroutine does not return` annotations. Individual reads quoted below come from straight-line code prior to the warned regions and are quoted exactly. The reads are evidence; the helper body cannot be treated as fully recovered without follow-up analysis.

Direct reads observed in the helper output (control-plane parsing path):
- Field at `param_2 + 0x2dc` (uint32) — total length / payload-cap; rejected if `>= 0x101` (0x100 = 256 cap).
- Field at `param_2 + 0x1c` (uint32) — SSID length; rejected if `>= 0x21` (32-byte SSID cap); zero is short-circuited to a no-op disable path.
- Field at `param_2 + 0x20` — start of the SSID buffer (`memcpy(local + 8, param_2 + 0x20, 0x20)` after constructing a 32-byte iovar payload `bsscfg:ssid`).
- Side effect: the SSID + length is written into a `bsscfg:ssid` iovar via `FUN_ffffff80016e43f2(core, "bsscfg:ssid", &local_180, 0, 0)`.
- Pre-conditions: state field at `state + 0x26c` (`resetState26c`) and `state + 0x270` are read; both zero forces a virtual-call branch to the AppleBCMWLAN core (vtable slot at `*core + 0xcf8`) which performs a structured RegistrationInfo update; when `*(state + 0x30c) >= 5` the path taken is the alternate "owner already up" subroutine via `FUN_ffffff80022781bc`.
- Failure return paths use the diagnostic-emit pattern; the success return forwards through `setHOST_AP_MODE` which then optionally calls `bringup` on `AppleBCMWLANProximityInterface` / `AppleBCMWLANNANInterface` / `AppleBCMWLANNANDataInterface` if `featureFlagIsBitSet(core, 0x46) == 0`.

Open: additional fields beyond SSID (`ie` blob, `bssid`, `auth_type` set, `unicast/multicast cipher` set, `ssid_hidden` flag) are likely in the same helper but reside in the warned/truncated regions; their reads must be re-verified after a follow-up decomp pass that fixes the bad-instruction site.

## Selectors not yet covered by this evidence pass

- Selector 26 `APPLE80211_IOC_AP_MODE` (`apple80211_apmode`) — no dispatch hit was observed in the present `0xffffff8001685000..0xffffff8001695000` decompile range; classification requires a separate symbol search across `kdk_symbols.txt` for `apple80211_apmode`.
- Selector 336 `APPLE80211_IOC_HOST_AP_MODE_HIDDEN` (`apple80211_host_ap_mode_hidden_t`) — getter at `0xffffff80016882ea` writes `*param_2 = 1` and returns `0x16`; the getter output carries a `Control flow encountered bad instruction data` warning and a truncation annotation, so even the getter body cannot be claimed as fully recovered without follow-up analysis. The setter is not auto-discovered in the current project.
- Selector 347 `APPLE80211_IOC_SOFTAP_PARAMS` (`apple80211_softap_params`) — getter at `0xffffff800168e7f4` (output `09_getSOFTAP_PARAMS.c` is clean) reads `state+0x68/0xe/0x10/0x18/0x1c/0x20/0x24/0x28` into `param_2 + 0x14/0x17/0x16/0x04/0x08/0x0c/0x10/0x18`. Setter is not auto-discovered.
- `APPLE80211_IOC_SOFTAP_EXTENDED_CAPABILITIES_IE` (selector 351 in the local mirror) — not auto-discovered.

For each not-auto-discovered setter, the next research action is a Ghidra pass that explicitly creates functions at the predicted KC entry points (kdk_symbols `+0x50`) and then re-decompiles them.

## Side-effect map (state offsets confirmed by this pass)

The mirror of these offsets in the local `AirportItlwmAPSTAStateBlock` already exists with the recovered names. This pass adds the following selector-to-state-offset map (read/write direction observed in body):

| Apple state offset | Setter that writes/reads it | Direction | Local mirror name |
|---|---|---|---|
| `state + 0x00` (uint32) | `setMaxAssoc` reads (compares with limit) | read | `softapAssociatedStaCount00` |
| `state + 0x04` (uint32) | `setMaxAssoc` writes | write | `softapMaxAssoc04` |
| `state + 0x08` (uint32) | `setMaxAssoc` reads (limit) | read | `softapMaxAssocLimit08` |
| `state + 0x0e` (byte/bit) | `getSOFTAP_PARAMS` reads (low bit) | read | `softapParam0e` |
| `state + 0x10` (uint8) | `getSOFTAP_PARAMS` reads | read | `softapMode10` |
| `state + 0x18..0x28` (uint32 ×4 + uint8) | `getSOFTAP_PARAMS` reads | read | `softapParam18..softapParam28` |
| `state + 0x2c` (36 bytes) | `setSOFTAP_WIFI_NETWORK_INFO_IE` writes (byte-exact) | write | `softapWifiNetworkInfoIE` |
| `state + 0x68` (uint16) | `getSOFTAP_PARAMS` reads | read | additional, not yet mirrored locally |
| `state + 0x1b0..0x208` (88 bytes) | `getSOFTAP_STATS` reads | read | additional, not yet mirrored locally |
| `state + 0x26c` (uint32) | `setSOFTAP_TRIGGER_CSA` and `setHostApModeInternal` read; `reset()` writes 0 | read+write | `resetState26c` |
| `state + 0x29b` (byte) | `setRSN_CONF` reads bit `0x10` | read | additional, not yet mirrored locally |
| `state + 0x329` (byte) | `setSOFTAP_TRIGGER_CSA` reads low bit; `reset()` writes 0 | read+write | additional, not yet mirrored locally |

## Reset / teardown ownership (only what the cited outputs prove)

The cited `12_resetAPSTA.c` output of `AppleBCMWLANIO80211APSTAInterface::reset()` at `0xffffff8001686cc6` proves only the following clears and calls:
- `*(uint32_t *)(state + 0x26c) = 0` — sets the AP-up flag mirror to zero.
- `*(uint8_t *)(state + 0x329) = 0` — clears the byte whose low bit gates `setSOFTAP_TRIGGER_CSA`.
- Conditional call into a virtual cleanup path on the AppleBCMWLANCore (`FUN_ffffff80022781bc(this, 0xffffffff)`) when a feature-flag byte at `core + 0x288d` low bit `2` is clear.
- Call `FUN_ffffff800162deb6(core, 4, 0)` on the AppleBCMWLANCore.
- Call `FUN_ffffff8000101100(state + 0xb8, 0xf0)` — clears `0xf0` bytes (240) starting at `state + 0xb8`.

The cited output does not prove that `reset()` clears the `state + 0x2c..0x4f` region (the selector 352 copy region), nor does it prove that `reset()` clears any region outside the `state + 0xb8..0x1a7` range plus the two specific bytes/word at `state + 0x26c` and `state + 0x329`. Any broader reset coverage is open and must be re-derived from a follow-up Ghidra pass.

The cited `13_freeAPSTA.c` output of `AppleBCMWLANIO80211APSTAInterface::free()` at `0xffffff8001687828` proves only the following:
- When the state pointer at `this + 0x130` is non-null, call `freeResources()` and then `FUN_ffffff8000101100(state, 0x338)` — clear `0x338` bytes (824) starting at the state base. The end of that range is `state + 0x337`, which covers the selector 352 copy region at `state + 0x2c..0x4f`.
- A subsequent indirect call through `PTR_DAT_ffffff80017611b8 + 0xa0`, with output annotated `Could not recover jumptable` and `Treating indirect jump as call`.

So the selector 352 copy region is provably cleared at object teardown by `free()`, but is not provably cleared by `reset()` based on the cited outputs.

`releaseSoftAPPowerAssertion()` at `0xffffff80016937c2` and `holdSoftAPPowerAssertion()` at `0xffffff800168dbc2` toggle an IOPMrootDomain assertion handle; the precise state byte they read/write is not yet pinned by these outputs alone and is open research.

## Allowed-scope check

- No selector routing changes added.
- No host APSTA owner `.cpp` body added.
- No AP profile carrier or carrier-to-HAL mapper added.
- No iwx/iwm HOSTAP guard changes.
- No iwx AP/GO capability gate lifted.
- No source typedef added.
- No kext install/reboot/AP runtime/AP-up/project-completion claim.

## Follow-up bounded decomp pass — 2026-05-10

A bounded function-boundary pass against the same reference Ghidra project (`wifi_analysis_26_3` on `192.168.40.116`) produced direct decomp evidence for the overmerged-boundary setters listed in the prior section, the channel-spec helper called from `setSOFTAP_TRIGGER_CSA`, and a refreshed re-decomp of `setHostApModeInternal` and `setRSN_CONF` after correcting false `setNoReturn` flags on returning callees. The pass scope was strictly decomp/function-boundary; no source compilation unit, header, or build script is changed by the new evidence. Decomp/reference debt is narrowed but not closed.

The pass driver is the project script `CR465FollowUpDecompV2.py`. Its logic: disassemble the entry address (the overmerged entries had not been auto-disassembled), apply `CreateFunctionCmd` at the entry, refresh the body via flow-derived re-create when an existing 1- or 2-byte stub is detected, name the new function with the demangled symbol from `kdk_symbols.txt` (`AppleBCMWLANIO80211APSTAInterface::setX` style), iterate the called-functions list of `setHostApModeInternal` and `setRSN_CONF` and clear `setNoReturn` only when the callee's body still contains a `RET` instruction (callees with no `RET` retain the flag), then decompile every target. The Ghidra project was saved at the end of the headless run, so the new function entries, names, and corrected return flags persist on the reference host for any subsequent decomp cycle.

### Reproducer (follow-up)

- Host: `ssh 192.168.40.116`.
- Ghidra build: `/srv/project/ghidra/build/ghidra_install/ghidra_12.2_DEV/support/analyzeHeadless`.
- Ghidra project: `/srv/project/ghidra_output` project `wifi_analysis_26_3` program `BootKernelExtensions.kc` (project save persisted the new function entries, renames, and `setNoReturn` corrections).
- Driver script: `/srv/project/ghidra_output/CR465FollowUpDecompV2.py` (canonical follow-up driver) and `/srv/project/ghidra_output/CR465FollowUpDecomp.py` (V1 predecessor, retained for the `setNoReturn` audit log).
- Output directory: `/srv/project/ghidra_output/cr465_followup/` with one `.c` per target plus `csa_window_funcs.txt`, `function_creation_log.txt`, `noreturn_remediation.txt`, and `run.log`.
- Host capacity at submission: `nproc=28`, ~48 GiB RAM with ~14 GiB free, no swap, load 0.75 / 0.72 / 0.72; no concurrent Ghidra `analyzeHeadless` or `java` process. The twelve-target sequential headless pass completed under one minute end-to-end.
- Concurrency posture: a single sequential headless pass under one program-write lock is materially faster than `ParallelDecompiler`/concurrent-queue sharding for twelve mid-size targets, because the dominant cost is the program-write lock for `CreateFunctionCmd` / body refresh / `setNoReturn` mutation rather than the native decomp engine. No single function in the batch was a long-pole single-core decomp; the largest body (1159 bytes) carries truncation/jumptable warnings from real bad-instruction sites in the bytes, not from a single-core decompile-engine bottleneck. No `-max-cpu` adjustment was needed.

### Inventory of overmerged-boundary setters now recovered

The setters listed in the prior section as not-auto-discovered (overmerged-boundary classification) have been created as discrete functions at the BootKC entry points predicted by `kdk_symbols.txt + 0x50`. Body sizes and per-output decompiler-warning state are recorded below. Sizes are bytes of recovered function body. The "Output state" column reproduces literal Ghidra warnings present inside each `.c` output. Warnings of class `Subroutine does not return` on these outputs all originate from real noReturn calls into the logging-fatal callee `FUN_ffffff8000307340` (the noReturn remediation pass retained the flag because the callee body has no `RET` instruction); they are real dead-end branches, not false flags.

| Symbol | BootKC entry | Body size | Output file | Output state |
|---|---|---|---|---|
| `setHOST_AP_MODE_HIDDEN(apple80211_host_ap_mode_hidden_t*)` | `0xffffff800168d970` | 436 | `01_setHOST_AP_MODE_HIDDEN.c` | warned (two `Subroutine does not return` on logging-fatal branches) |
| `setSOFTAP_PARAMS(apple80211_softap_params*)` | `0xffffff800168e536` | 204 | `02_setSOFTAP_PARAMS.c` | clean |
| `setSOFTAP_EXTENDED_CAPABILITIES_IE(apple80211_softap_extended_capabilities_ie*)` | `0xffffff800168e7b8` | 59 | `03_setSOFTAP_EXTENDED_CAPABILITIES_IE.c` | clean |
| `setSSID(apple80211_ssid_data*)` | `0xffffff800168dc92` | 102 | `04_setSSID_APSTA.c` | warned (one `Subroutine does not return` on logging-fatal branch) |
| `setBeaconInterval(apple80211_beacon_interval_data*)` | `0xffffff8001687ae4` | 312 | `05_setBeaconInterval.c` | clean |
| `enableAPInterface()` | `0xffffff800168d310` | 626 | `06_enableAPInterface.c` | warned (two `Subroutine does not return` on logging-fatal branches) |
| `setMaxAssoc(apple80211_max_assoc_data*)` | `0xffffff800168c6ac` | 101 | `07_setMaxAssoc.c` | warned (one `Subroutine does not return` on logging-fatal branch) |
| `configureManagementFrameProtectionForSoftAP()` | `0xffffff800168c4fe` | 355 | `08_configureManagementFrameProtectionForSoftAP.c` | warned (two `Subroutine does not return` on logging-fatal branches) |
| `setCIPHER_KEY(apple80211_key*)` | `0xffffff800168f2b6` | 399 | `09_setCIPHER_KEY_APSTA.c` | warned (three `Subroutine does not return` on logging-fatal branches) |

### CSA helper window inventory and selector 26 owner classification

The function inventory of the requested CSA helper window `0xffffff8001602000..0xffffff8001604000` resolves nine functions, all in the `AppleBCMWLANCore` class (not `AppleBCMWLANIO80211APSTAInterface`):

| BootKC entry | Symbol | Body size |
|---|---|---|
| `0xffffff8001602206` | `FUN_ffffff8001602206` (anonymous helper) | 243 |
| `0xffffff8001602362` | `AppleBCMWLANCore::setREASSOCIATE_WITH_CORECAPTURE(apple80211_capture_debug_info_t*)` | 142 |
| `0xffffff800160243a` | `AppleBCMWLANCore::setRESET_CHIP(apple80211_reset_command*)` | 437 |
| `0xffffff8001602b3e` | `AppleBCMWLANCore::setCHANNEL(apple80211_channel_data*)` | 450 |
| `0xffffff8001602f74` | `csa_helper_FUN_ffffff8001602f74` (created/named in this pass) | 792 |
| `0xffffff80016034dc` | `AppleBCMWLANCore::setAP_MODE(apple80211_apmode_data*)` | 101 |
| `0xffffff8001603564` | `AppleBCMWLANCore::setDBG_GUARD_TIME_PARAMS(apple80211_dbg_guard_time_params*)` | 195 |
| `0xffffff8001603768` | `AppleBCMWLANCore::setTHERMAL_INDEX(apple80211_thermal_index_t*)` | 262 |
| `0xffffff8001603ca8` | `AppleBCMWLANCore::setTXPOWER(apple80211_txpower_data*)` | 299 |

This narrows the previously open selector-26 dispatch classification: the `apple80211_apmode_data` consumer is `AppleBCMWLANCore::setAP_MODE` at `0xffffff80016034dc`, not anything in the `AppleBCMWLANIO80211APSTAInterface` `0xffffff8001685000..0xffffff8001695000` range. Any future producer-side wire-struct decomp for selector 26 must walk callers of the `AppleBCMWLANCore::setAP_MODE` entry. Inner reads of `apple80211_apmode_data` were not the subject of this bounded pass and remain open research — bytes 0..(sizeof - 1) of the wire struct are not classified by the recovered owner alone.

### Selector 349 — CSA helper `csa_helper_FUN_ffffff8001602f74`

The 792-byte body recovered from `10_csa_helper_FUN_ffffff8001602f74.c` proves the following reads on the channel-descriptor argument that `setSOFTAP_TRIGGER_CSA` forwards as `param_2 + 4`:

- The early gate is `if (uVar2 - 0x100 < 0xffffff01) { return 0x16 / diagnostic-emit }` on `uVar2 = *(uint32_t *)(param_2 + 4)`. The TRUE branch is the helper's error-return path: it either returns `0x16` (`EINVAL`) directly or calls the owner's diagnostic-emit helper and returns its status. Under unsigned 32-bit semantics the TRUE branch fires for `uVar2 == 0` and for `uVar2 >= 0x100` (because `0 - 0x100 = 0xffffff00 < 0xffffff01` and any `uVar2 >= 0x100` produces `uVar2 - 0x100 >= 0` that satisfies `< 0xffffff01` once it is below the wrap point, while values in `0xffffff01..0xffffffff` map back through the wrap to values still strictly less than `0xffffff01` against `uVar2 - 0x100`); the FALSE branch (the normal/forward path) therefore fires only for `uVar2` in `0x01..0xff`. The accepted channel value at `param_2 + 4` is therefore a primary-channel value in `1..255`, stored in a 32-bit carrier. This is consistent with the helper's later byte-level comparison `uVar2 == bVar4` against `bVar4 = ChanSpecGetPrimaryChannel(...)`, which returns a byte primary-channel value, and corrects any earlier reading of the gate as a `0x100..0xffff` accepted range.
- `*(uint32_t *)(param_2 + 8)` is read as a flags word with bits at `0x2`, `0x4`, `>>10 & 1`, `>>11 & 1`, `0x8`, `0x10`, `>>13 & 1`. The first four bits map to bandwidth-class branches that select an `iVar9` value of 2, 3, 4, or 5; bit `0x8` and bit `0x10` select an additional legacy-class branch; bit `>>13 & 1` enters another classification edge. Per-bit semantic naming is not pinned by the helper alone.
- The helper consults the core's chanspec table at `*(...)(param_1[0x25] + 0x4550)` and the count at `*(uint16_t *)(param_1[0x25] + 0x4dcc)`, iterates over channel entries, calls `AppleBCMWLANChanSpec::getAppleChannelSpec` and `ChanSpecGetPrimaryChannel(AppleChannelSpec_t)`, compares `uVar2 == primary` with the bandwidth-class match `local_38 == ((uVar5 & 0xffff) >> 0xe)`, and writes the resulting `AppleChanSpec` 16-bit value into `*param_3` on success.

Open: inner sub-fields of the channel descriptor beyond `+0x04` (primary-channel byte in u32 carrier) and `+0x08` (flags word) are not visible in the helper alone; the selector-349 setter still only forwards `param_2 + 4` as a pointer, so any non-channel-descriptor sub-fields in the wire struct beyond that pointer remain open research.

### Selector 25 — `setHostApModeInternal` re-decomp (warned-improved)

The redecomp at `11_setHostApModeInternal_redecomp.c` is a strict improvement over the prior warned/truncated body. The new body size is 1159 bytes; the prior decomp ended early at the first warning site. The new output exposes additional surviving reads in post-call blocks but still carries `Control flow encountered bad instruction data`, four `Bad instruction - Truncating control flow here`, `Could not recover jumptable at 0xffffff800178bebf. Too many branches`, `Treating indirect jump as call`, and `Subroutine does not return` warnings; treat the body as warned-improved, not closed. The `setNoReturn` remediation pass cleared three false-noReturn flags on returning callees that had previously cut off the post-call control-flow recovery: `FUN_ffffff8003222728` at `0xffffff8003222728`, `IO80211SkywalkInterface::getInterfaceId()` at `0xffffff8002274c8e`, and `FUN_ffffff8000101050` at `0xffffff8000101050`. The remaining `Bad instruction - Truncating control flow here` sites are not callee-flag artefacts; closing them needs per-truncation-site `CreateFunction` annotations on the bad-instruction successors plus jumptable target enumeration. Vendor IE programming at `param_2 + 0x2dc` length and `+0x2e0` payload remains a predicted but unrecovered iovar emission.

### Selector 77 — `setRSN_CONF` re-decomp (no per-cipher slot reads visible)

The redecomp at `12_setRSN_CONF_redecomp.c` recovers a 524-byte body. Count and version reads at `param_2 + 0x2c`, `+0x7c`, `+0x08`, `+0x58` are confirmed; the RSN gate byte at `state + 0x29b` low-nibble bit `0x10` is confirmed. The redecomp does not surface inner per-slot reads at `param_2 + 0x30 + i*8` or `param_2 + 0x80 + i*8`; the slot-level wire layout is not visible in this output's blocks. Closing this needs a wider re-decomp that follows the call into the wrapper `apple80211setRSN_CONF` named in `kdk_symbols.txt`, not just the APSTA setter. The single `Subroutine does not return` annotation in this output is the genuinely-noReturn callee `FUN_ffffff8000307340`; the `setNoReturn` remediation pass retained that flag because the callee body has no `RET` instruction.

### Selector 351 — `setSOFTAP_EXTENDED_CAPABILITIES_IE` (clean)

The 59-byte body at `0xffffff800168e7b8` (output `03_setSOFTAP_EXTENDED_CAPABILITIES_IE.c`) zeroes 18 bytes at `state + 0x50..+0x61` (8 bytes via `*(uint64_t *)(state + 0x58) = 0`, 8 bytes via `*(uint64_t *)(state + 0x50) = 0`, and 2 bytes via `*(uint16_t *)(state + 0x60) = 0`), then copies 17 bytes from `param_2 + 0x00..+0x10` into `state + 0x50..+0x60` as: 1 byte at `state + 0x50` from `param_2 + 0x00` (`*(uint8_t *)(state + 0x50) = *param_2`), 8 bytes at `state + 0x51..+0x58` from `param_2 + 0x01..+0x08` (`*(uint64_t *)(state + 0x51) = *(uint64_t *)(param_2 + 1)`), and 8 bytes at `state + 0x59..+0x60` from `param_2 + 0x09..+0x10` (`*(uint64_t *)(state + 0x59) = *(uint64_t *)(param_2 + 9)`). State byte `+0x61` is left at zero after the clear/copy pass. The wire layout of the input carrier is therefore one length byte at `param_2 + 0x00` followed by sixteen IE octets at `param_2 + 0x01..+0x10`; the setter does not validate the length byte beyond the unconditional copy.

### Standalone `setSSID` setter (clean apart from logging branch)

The 102-byte body at `0xffffff800168dc92` (output `04_setSSID_APSTA.c`) is essentially a no-op: it reads the AP-up gate via the core's interface-up check (`(*core)[0xd08]`) and on the not-up path forwards to the logging-fatal helper. The setter never writes the SSID into APSTA private state and does not emit any `bsscfg:ssid` iovar. The actual SSID programming continues to flow through `setHostApModeInternal` via the `bsscfg:ssid` iovar emission documented in the prior section. This output corrects any expectation that selector 79 (`setSSID`) is a primary SSID producer.

### `setHOST_AP_MODE_HIDDEN` (closednet emission and hidden cache)

The 436-byte body at `0xffffff800168d970` (output `01_setHOST_AP_MODE_HIDDEN.c`) reads `*(uint32_t *)(param_2 + 4)` as the hidden-flag value, accepts only `< 2` (boolean), and emits the `closednet` iovar via the host-call helper `FUN_ffffff80016e47e8(stateHandle, ifid, "closednet", &valuePtr, 0, 0)`. On success the setter writes the boolean cache `*(bool *)(state + 0xd) = uStack_2c != 0`. When the transition is hidden=1 → hidden=0 with `state + 0x26c != 0`, the setter calls `FUN_ffffff8001686e62(this, 0, 9)`, clears `*(uint8_t *)(state + 0xe) = 0`, and then calls `holdSoftAPPowerAssertion()`. The pre-condition reproduces the `state + 0x26c == 0` gate observed in the other AP-mode setters: with the AP mirror not up, the setter falls to the diagnostic-emit branch and does not write the `closednet` iovar.

### `setMaxAssoc` cap-gate

The 101-byte body at `0xffffff800168c6ac` (output `07_setMaxAssoc.c`) reads the requested max-station count from `param_2` (signature inferred), reads `*piVar1` (current associated-station count) and `piVar1[2]` (system-wide cap) at `state + 0x00` and `state + 0x08` respectively, and writes the requested count to `piVar1[1]` (`state + 0x04`) only when `(uint)(*piVar1 + param_2) <= (uint)piVar1[2]` holds. Exceeding the gate falls through to a logging-fatal no-op branch via `FUN_ffffff8000307340`. This pins the wire as a `uint32_t` max-station count and the cap as `current + requested <= limit`. The state-offset map already records `state + 0x00` (associated count, read), `state + 0x04` (max, write), `state + 0x08` (limit, read).

### `setNoReturn` remediation log

The follow-up pass cleared three callee `setNoReturn` flags whose call sites had cut off control-flow recovery in `setHostApModeInternal`. The clears apply only to callees whose body still contains a `RET` instruction (heuristic guard). Cleared callees:

- `FUN_ffffff8003222728` at `0xffffff8003222728` — caller `setHostApModeInternal`.
- `IO80211SkywalkInterface::getInterfaceId()` at `0xffffff8002274c8e` — caller `setHostApModeInternal` and `setRSN_CONF`.
- `FUN_ffffff8000101050` at `0xffffff8000101050` — caller `setHostApModeInternal`.

Retained `setNoReturn` (correct, callee body has no `RET`):

- `FUN_ffffff8000307340` at `0xffffff8000307340` — logging-fatal terminator used by the diagnostic-emit branches across `01`, `04`, `06`, `07`, `08`, `09`, and `12`.

### Updated open-research list (post-pass)

The pass narrows but does not close decomp/reference debt for the AP control plane. The post-pass open list:

1. Selector 352 inner payload — bytes `0`, `1`, `2` and the 32-byte payload at `+0x04..+0x23` remain producer-side. The bounded pass did not target the producer; closing these needs a separate workpack against `airportd` / `WirelessRadioManager` / `Wireless Diagnostics`, not a `BootKernelExtensions.kc` Ghidra task.
2. Selector 77 per-cipher sub-blocks at `param_2 + 0x30 + i*8` and `+0x80 + i*8` remain open after the redecomp; the wrapper `apple80211setRSN_CONF` is the next decomp target if the auditor selects another decomp pass.
3. Selector 349 inner sub-fields beyond `+0x04` (primary-channel byte in u32 carrier accepted in `1..255`) and `+0x08` (flags word) of the channel descriptor remain open; the selector-349 setter only forwards `param_2 + 4` as a pointer.
4. Selector 25 fields beyond SSID — `setHostApModeInternal_redecomp` is warned-improved, not closed; closing the residual `Bad instruction - Truncating control flow here` sites and the unrecovered jumptable at `0xffffff800178bebf` needs per-truncation-site `CreateFunction` plus jumptable target enumeration.
5. Selector 26 inner field reads — owner classified to `AppleBCMWLANCore::setAP_MODE` at `0xffffff80016034dc`, but `apple80211_apmode_data` field reads were not the subject of this bounded pass and remain open.
6. APSTA reset coverage beyond the offsets already proved — the bounded pass did not target reset/free and the prior section's coverage is unchanged.

This open list bounds the residual decomp/reference debt; this document does not assert that the AP control plane is closed.

### Updated allowed-scope check (follow-up pass)

- No selector routing changes added by the follow-up evidence.
- No host APSTA owner `.cpp` body added.
- No AP profile carrier or carrier-to-HAL mapper added.
- No iwx/iwm HOSTAP guard changes.
- No iwx AP/GO capability gate lifted.
- No source typedef added by the follow-up evidence.
- No kext install/reboot/AP runtime/AP-up/project-completion claim.
- The Ghidra project save on `192.168.40.116` persists the new function entries, names, and corrected `setNoReturn` flags; the project save is reference-side state, not an itlwm source change.
