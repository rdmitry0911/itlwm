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
