# Analysis report — 2026-05-10 (Apple AP control-plane decomp evidence)

This entry records the partial Apple AP control-plane wire-struct decomp evidence accumulated since the closed CR-464 gap document at basis commit 22414ff4. The full direct-decomp evidence with per-output decompiler-warning state is in `docs/reference/IWX_APGO_AP_CONTROL_PLANE_WIRE_STRUCT_EVIDENCE_2026_05_10.md`. This entry summarises the scope and links the durable evidence into the project anomaly ledger.

This is a documentation-only research-evidence entry. No source compilation unit, build script, kernel symbol behaviour, runtime path, or AP-mode bring-up is changed. Every direct fact below is anchored to the cited Ghidra output file and either marked clean (no decompiler warnings) or warned (with the warning text reproduced); warned outputs are treated as direct decompile output with residual uncertainty rather than as a fully recovered body.

## ANOMALY

- id: A-IWX-APGO-WIRE-STRUCT-EVIDENCE-20260510
- status: PARTIAL_DECOMP
- symptom: the recovered Apple AP control-plane setters expose wire-struct payloads whose byte-exact field layouts are required for any future host APSTA owner setter, profile carrier, or carrier-to-HAL mapper. Without those layouts, no semantic source consumer can be written safely.
- first visible manifestation: the closed CR-464 listed Apple wire-struct recovery as `RESEARCH_FIRST` against `192.168.40.116`.

## DIVERGENCE POINTS

The new evidence document records 17 directly-decompiled Ghidra outputs in the `AppleBCMWLANIO80211APSTAInterface` `0xffffff8001685000..0xffffff8001695000` range. Output state per setter is recorded in the evidence document inventory; the warned outputs include `setHostApModeInternal` (`Control flow encountered bad instruction data`, two `Bad instruction - Truncating control flow here`, `Could not recover jumptable`, multiple `Subroutine does not return`), `getHOST_AP_MODE_HIDDEN` (`Control flow encountered bad instruction data`, `Bad instruction - Truncating control flow here`), `free()` (`Could not recover jumptable`, `Treating indirect jump as call`), and others. Two outputs are clean (`getSOFTAP_PARAMS`, `getSOFTAP_STATS`); the remaining outputs carry one or more `Subroutine does not return` annotations on diagnostic-emit branches that do not affect the field-read evidence quoted in this batch.

Direct facts proved by the cited outputs (selector-by-selector):

- Selector 352 (`apple80211_softap_wifi_network_info`): the setter at `0xffffff800168e602` validates `param_2 + 3 < 0x21` and copies 36 bytes verbatim into APSTA private state at `state + 0x2c`. Bytes 0..2 and the inner structure of the 32-byte payload are not classified by this output alone.
- Selector 349 (`apple80211_softap_csa_params`): the setter at `0xffffff800168e0ae` reads byte at `+0x14` and channel descriptor at `+0x4`. Per-byte sub-block layout is open.
- Selector 77 (`apple80211_rsn_conf_data`): the setter at `0xffffff800168e85c` reads pairwise count at `+0x2c`, group count at `+0x7c`, pairwise version at `+0x08`, group version at `+0x58`. Per-cipher sub-blocks are open.
- Selector 25 (`apple80211_network_data`): `setHOST_AP_MODE` at `0xffffff80016884ae` and helper `setHostApModeInternal` at `0xffffff8001688bc2` (helper output is warned) read SSID length at `+0x1c`, SSID buffer at `+0x20`, total length at `+0x2dc`. Additional fields beyond SSID are open and likely reside in the warned/truncated regions of the helper.

Setters whose entry points are present in `kdk_symbols.txt` but are not auto-discovered as separate functions in the current Ghidra project (overmerged-boundary classification): `setHOST_AP_MODE_HIDDEN`, `setSOFTAP_PARAMS`, `setSOFTAP_EXTENDED_CAPABILITIES_IE`, `setSSID`, `setBeaconInterval`, `enableAPInterface`, `setMaxAssoc` (anonymous helper FUN at `0xffffff800168c6ac` is the same function with no demangled name in the current project), `configureManagementFrameProtectionForSoftAP`, `setCIPHER_KEY`. Recovering these requires a follow-up Ghidra pass that explicitly creates functions at the predicted KC entry points (kdk_symbols `+0x50`) and re-decompiles them.

Selector 26 (`APPLE80211_IOC_AP_MODE`, `apple80211_apmode`) — no dispatch hit in the `0xffffff8001685000..0xffffff8001695000` range. Classification requires a separate symbol search across `kdk_symbols.txt`.

## CANDIDATE CAUSES

- confirmed: the `wifi_analysis_26_3` Ghidra project's auto-discovered function boundaries are partial in the APSTA range; many setters are merged into neighbouring functions or are not given function-entry annotations. Recovered setter addresses match the predicted (kdk_symbols + 0x50) offsets for the functions Ghidra did discover.
- confirmed: byte-exact size and one validation byte for `apple80211_softap_wifi_network_info` are recovered (`setSOFTAP_WIFI_NETWORK_INFO_IE` direct decompile shows `param_2 + 3 < 0x21` then a 36-byte memcpy into APSTA state).
- confirmed: per-selector partial field offsets for `apple80211_softap_csa_params`, `apple80211_rsn_conf_data`, and `apple80211_network_data` are observed in the cited outputs; per-byte completeness (every offset 0..N-1 named or accounted for) is not yet recovered for any of the four selectors.
- rejected: speculative wire-struct field naming for offsets not directly read by the decompiled setter. Per the project rules, partial-evidence selectors are deferred rather than guessed.

## CONFIRMED ROOT

The Apple AP/APSTA selector contract is partially documented by direct decomp evidence. No source typedef has been added in this batch because no decompiled setter alone provides a full byte-exact field-level contract, and the producer-side decomp (which would name bytes 0..2 of selector 352 and equivalent gaps in the other four setters) has not been performed.

## FIX PLAN

This batch adds two project-owned documentation files only:

- `docs/reference/IWX_APGO_AP_CONTROL_PLANE_WIRE_STRUCT_EVIDENCE_2026_05_10.md` (new) — per-selector evidence with reproducer, address inventory (with output-state column), per-selector field-offset map, side-effect map into APSTA state, and reset/teardown ownership stated only to the extent the cited outputs prove it.
- `analysis/ANALYSIS_REPORT_2026-05-10b.md` (this file).

No source compilation unit, build script, or header is changed.

## VERIFICATION FOR THIS BATCH

- `git diff --stat` against basis commit 22414ff4 reports two changed paths only: `analysis/ANALYSIS_REPORT_2026-05-10b.md` (new) and `docs/reference/IWX_APGO_AP_CONTROL_PLANE_WIRE_STRUCT_EVIDENCE_2026_05_10.md` (new).
- No build is required because no header or source file changes.
- `git diff --check` is expected to pass.
- The reset/teardown section of the evidence document is restricted to the clears actually shown in `12_resetAPSTA.c` (zero at `state + 0x26c`, zero at `state + 0x329`, the conditional `FUN_ffffff80022781bc(this, 0xffffffff)`, the `FUN_ffffff800162deb6(core, 4, 0)` call, and the `FUN_ffffff8000101100(state + 0xb8, 0xf0)` clear of 240 bytes). The `13_freeAPSTA.c` output proves the `state + 0x000..0x337` clear at object-teardown time after `freeResources()`. The selector 352 copy region at `state + 0x2c..0x4f` is provably cleared at `free()`-time but is not provably cleared at `reset()`-time by these outputs.

## NON-CLAIMS

- This batch does not claim recovery of any complete byte-level field-named wire struct.
- This batch does not add any source typedef, struct definition, or other source declaration.
- This batch does not claim that any decompiled body is fully recovered when the cited Ghidra output carries decompiler warnings.
- This batch does not claim a host APSTA owner `.cpp` implementation, AP profile carrier, carrier-to-HAL mapper, or beacon template builder.
- This batch does not claim any change to the iwx AP/GO HAL boundary committed in the closed CR-464.
- This batch does not claim AP-up, beaconing, AP probe-response template upload, AP time-event/session protection, AP client association, AP DHCP, AP traffic, role-7 success, CONTROL_STA_NETWORK success, lab AP success, or project completion.
- This batch does not claim resolution of the pre-existing STA post-RUN deauth root tracked under CR-285 / CR-291 / CR-294 / CR-295 / CR-296 / CR-297 / CR-298.

## RESIDUAL UNCERTAINTY

- Bytes 0..2 of `apple80211_softap_wifi_network_info` and the inner structure of the 32-byte payload at offsets 0x04..0x23 are not classified by the consumer setter alone; producer-side decomp is required.
- The remaining sub-block field semantics for `apple80211_softap_csa_params`, `apple80211_rsn_conf_data`, and `apple80211_network_data` are open.
- Setters listed under DIVERGENCE POINTS as overmerged require explicit `CreateFunction` plus re-decompile to recover their bodies in the current Ghidra project.
- The full reset coverage of the APSTA private state by `reset()` is open; only the specific clears listed in the evidence document are proven.
- Selector 26 dispatch classification is open.

## PROVENANCE

- Basis commit: `22414ff4775d327db6c77defb7c32cffe81856d9`.
- Reference host: `192.168.40.116`.
- Ghidra project: `/srv/project/ghidra_output` project `wifi_analysis_26_3` program `BootKernelExtensions.kc`.
- Ghidra build: `/srv/project/ghidra/build/ghidra_install/ghidra_12.2_DEV/support/analyzeHeadless`.
- Decomp scripts (saved on the reference host for reproducibility): `/srv/project/ghidra_output/CR464LayerADecompV2.py`, `/srv/project/ghidra_output/CR464DecompByAddrV3.py`, `/srv/project/ghidra_output/CR464ListFuncs.py`, `/srv/project/ghidra_output/CR464ProbeAddrV2.py`.
- Per-target outputs: `/srv/project/ghidra_output/cr464_layer_a/*.c`.
- Inventory: `/srv/project/ghidra_output/cr464_layer_a/all_apsta_funcs.txt` (175 functions in `0xffffff8001685000..0xffffff8001695000`).
