# Analysis report — 2026-05-10c (Apple AP control-plane decomp follow-up)

This entry records the bounded follow-up Apple AP control-plane decomp evidence accumulated against the closed-CR-465 evidence document at basis commit `4d86c86e2e2ea5d31a8c677f89fccdb0a3c474a6`. The full per-selector evidence — overmerged-boundary setter recovery with sizes and per-output decompiler-warning state, CSA helper window inventory and selector 26 owner classification, CSA helper inner reads, `setHostApModeInternal` warned-improved redecomp, `setRSN_CONF` redecomp without per-cipher slot reads, `setSOFTAP_EXTENDED_CAPABILITIES_IE` wire layout, standalone `setSSID` no-op, `setHOST_AP_MODE_HIDDEN` `closednet` emission, `setMaxAssoc` cap-gate, and the `setNoReturn` remediation log — is in `docs/reference/IWX_APGO_AP_CONTROL_PLANE_WIRE_STRUCT_EVIDENCE_2026_05_10.md` under the new "Follow-up bounded decomp pass — 2026-05-10" section appended in this batch.

This is a documentation-only research-evidence entry. No source compilation unit, build script, kernel symbol behaviour, runtime path, AP-mode bring-up, kext install, reboot, or AP/STA runtime is changed. Every direct fact below is anchored to the cited Ghidra output and either marked clean (no decompiler warnings) or warned (with the warning text reproduced); warned outputs are treated as direct decompile output with residual uncertainty rather than as a fully recovered body.

## ANOMALY

- id: A-IWX-APGO-WIRE-STRUCT-EVIDENCE-FOLLOWUP-20260510
- status: PARTIAL_DECOMP
- symptom: the closed-CR-465 evidence document recorded partial Apple AP control-plane setter contracts with several selectors and overmerged-boundary setters listed as open research. Without those bodies and without the dispatch classification of selector 26, no host APSTA owner setter or carrier-to-HAL mapper can be written safely.
- first visible manifestation: the closed CR-465 evidence document explicitly listed the overmerged-boundary setters, the CSA helper, the warned `setHostApModeInternal`, the slot-level RSN_CONF reads, and selector 26 dispatch as open follow-up items.

## DIVERGENCE POINTS

The follow-up pass produced 12 directly-decompiled Ghidra outputs against the same `wifi_analysis_26_3` project on `192.168.40.116`, plus a function inventory of the `0xffffff8001602000..0xffffff8001604000` CSA helper window, a function-creation log, and a `setNoReturn` remediation log. Output-state per setter is recorded in the evidence document inventory; warning state per output is summarised here:

- Clean: `02_setSOFTAP_PARAMS.c`, `03_setSOFTAP_EXTENDED_CAPABILITIES_IE.c`, `05_setBeaconInterval.c`, `10_csa_helper_FUN_ffffff8001602f74.c`.
- Warned with logging-fatal `Subroutine does not return` only (real noReturn callees, not false flags): `01_setHOST_AP_MODE_HIDDEN.c`, `04_setSSID_APSTA.c`, `06_enableAPInterface.c`, `07_setMaxAssoc.c`, `08_configureManagementFrameProtectionForSoftAP.c`, `09_setCIPHER_KEY_APSTA.c`, `12_setRSN_CONF_redecomp.c`.
- Warned with control-flow truncation: `11_setHostApModeInternal_redecomp.c` carries `Control flow encountered bad instruction data`, four `Bad instruction - Truncating control flow here`, `Could not recover jumptable at 0xffffff800178bebf. Too many branches`, `Treating indirect jump as call`, and `Subroutine does not return` annotations.

Direct facts proved by the cited outputs (selector-by-selector, follow-up):

- Selector 26 owner classification: dispatch resolves to `AppleBCMWLANCore::setAP_MODE(apple80211_apmode_data*)` at `0xffffff80016034dc`, surfaced by the CSA window inventory at `0xffffff8001602000..0xffffff8001604000`. Inner reads of `apple80211_apmode_data` were not the subject of this bounded pass and remain open.
- Selector 349 channel descriptor: the helper at `csa_helper_FUN_ffffff8001602f74` reads `*(uint32_t *)(param_2 + 4)` with the early gate `if (uVar2 - 0x100 < 0xffffff01) { return 0x16 / diagnostic-emit }`. The TRUE branch is the helper's error-return path; under unsigned 32-bit semantics that condition fires for `uVar2 == 0` and for `uVar2 >= 0x100`, so the FALSE branch (the normal/forward path) fires only for `uVar2` in `0x01..0xff`. The accepted channel value is therefore a primary-channel value in `1..255`, stored in a 32-bit carrier, consistent with the helper's later byte-level compare `uVar2 == bVar4` against `bVar4 = ChanSpecGetPrimaryChannel(...)`. The helper also reads `*(uint32_t *)(param_2 + 8)` as a flags word with bits `0x2`, `0x4`, `>>10 & 1`, `>>11 & 1`, `0x8`, `0x10`, `>>13 & 1`; per-bit semantic naming is not pinned by the helper alone.
- Selector 77 per-cipher slots: the redecomp at `12_setRSN_CONF_redecomp.c` retains the count/version reads at `+0x2c`, `+0x7c`, `+0x08`, `+0x58` and the gate byte at `state + 0x29b` low-nibble bit `0x10`, but does not surface inner per-slot reads at `+0x30 + i*8` or `+0x80 + i*8`. Slot-level wire layout remains open.
- Selector 25 fields beyond SSID: the redecomp at `11_setHostApModeInternal_redecomp.c` is warned-improved (1159 bytes, more post-call blocks visible) but still carries truncation/jumptable warnings; vendor-IE programming at `param_2 + 0x2dc` length and `+0x2e0` payload remains predicted but unrecovered.
- `setSOFTAP_EXTENDED_CAPABILITIES_IE` (selector 351): the setter zeroes 18 bytes at `state + 0x50..+0x61` (8 bytes at `+0x58`, 8 bytes at `+0x50`, 2 bytes at `+0x60`), then copies 17 bytes from `param_2 + 0x00..+0x10` into `state + 0x50..+0x60` (1 length byte at `+0x50` from `param_2 + 0x00`, 8 bytes at `+0x51..+0x58` from `param_2 + 0x01..+0x08`, 8 bytes at `+0x59..+0x60` from `param_2 + 0x09..+0x10`); state `+0x61` is left zero. Wire layout of the input carrier is therefore one length byte at `+0x00` followed by sixteen IE octets at `+0x01..+0x10`.
- `setMaxAssoc`: u32 max-station wire; cap-gate is `current + requested <= limit` against `state + 0x00` (current), `state + 0x04` (max, write), `state + 0x08` (limit). Cap violation falls to a logging-fatal no-op branch.
- Standalone `setSSID` (selector 79): is essentially no-op apart from interface-up gate and logging-fatal branch; never writes SSID into APSTA state and does not emit `bsscfg:ssid`. Actual SSID programming continues to flow through `setHostApModeInternal`.
- `setHOST_AP_MODE_HIDDEN`: u32 hidden-flag wire (`< 2` accepted); emits the `closednet` iovar via `FUN_ffffff80016e47e8(..., "closednet", ...)`; writes the boolean cache at `state + 0xd`; the hidden=1→hidden=0 transition with `state + 0x26c != 0` triggers `FUN_ffffff8001686e62(this, 0, 9)`, clears `state + 0xe`, and calls `holdSoftAPPowerAssertion()`.
- Overmerged setters previously marked overmerged-boundary in the closed CR-465 evidence are now created as discrete functions with body sizes 59..626 bytes (see the new evidence document table). The Ghidra project save on `192.168.40.116` persists the new function entries, names, and the corrected `setNoReturn` flags so subsequent cycles do not re-run the boundary fix.

The overall debt narrows but does not close. The post-pass open list (also in the evidence document):

1. Selector 352 inner payload bytes 0..2 and the 32-byte payload at `+0x04..+0x23` remain producer-side.
2. Selector 77 per-cipher sub-block reads remain open.
3. Selector 349 inner sub-fields beyond `+0x04` and `+0x08` of the channel descriptor remain open.
4. Selector 25 fields beyond SSID remain warned/truncated; the four bad-instruction sites and the jumptable at `0xffffff800178bebf` are not closed.
5. Selector 26 inner field reads of `apple80211_apmode_data` remain open.
6. APSTA reset coverage beyond the offsets already proved in the closed CR-465 section is unchanged by this pass.

## CANDIDATE CAUSES

- confirmed: the `wifi_analysis_26_3` Ghidra project's auto-discovered function boundaries were partial in the APSTA range; the bounded pass added explicit `CreateFunction` annotations at the predicted KC entry points (`kdk_symbols.txt + 0x50`) for the nine overmerged setters and the CSA helper, and the body refresh produced flow-derived bodies of 59..792 bytes per target.
- confirmed: false `setNoReturn` flags on `FUN_ffffff8003222728`, `IO80211SkywalkInterface::getInterfaceId()`, and `FUN_ffffff8000101050` had been cutting off post-call control-flow recovery in `setHostApModeInternal`; clearing them under the heuristic guard "callee body still contains a `RET` instruction" exposed additional post-call blocks.
- confirmed: the residual `Bad instruction - Truncating control flow here` sites in `setHostApModeInternal` are not callee-flag artefacts; closing them needs per-truncation-site `CreateFunction` annotations on the bad-instruction successors plus jumptable target enumeration.
- rejected: speculative wire-struct field naming for offsets not directly read by a decompiled setter. Per the project rules, partial-evidence selectors are deferred rather than guessed.

## CONFIRMED ROOT

The Apple AP/APSTA selector contract is partially documented by direct decomp evidence; the follow-up pass narrows the open list from the closed-CR-465 shape to the post-pass shape above. No source typedef has been added in this batch because no decompiled setter alone provides a full byte-exact field-level contract for any selector still listed open, and the producer-side decomp (which would name bytes 0..2 of selector 352 and equivalent gaps) has not been performed.

## FIX PLAN

This batch adds one project-owned documentation file and updates one project-owned reference document only:

- `analysis/ANALYSIS_REPORT_2026-05-10c.md` (new) — this file.
- `docs/reference/IWX_APGO_AP_CONTROL_PLANE_WIRE_STRUCT_EVIDENCE_2026_05_10.md` (modified) — appended new "Follow-up bounded decomp pass — 2026-05-10" section with reproducer, inventory of recovered overmerged setters, CSA helper window inventory, per-selector follow-up findings, `setNoReturn` remediation log, updated open-research list, and the updated allowed-scope check.

No source compilation unit, build script, or header is changed.

## VERIFICATION FOR THIS BATCH

- `git diff --stat` against basis commit `4d86c86` reports two changed paths only: `analysis/ANALYSIS_REPORT_2026-05-10c.md` (new) and `docs/reference/IWX_APGO_AP_CONTROL_PLANE_WIRE_STRUCT_EVIDENCE_2026_05_10.md` (modified).
- No build is required because no header or source file changes.
- `git diff --check` is expected to pass.
- The follow-up section in the evidence document does not assert reset/teardown coverage beyond what the closed-CR-465 section already proved; the closed coverage there (`state + 0x26c`, `state + 0x329`, `state + 0xb8..0x1a7` at reset(); `state + 0x000..0x337` at free()) is preserved and unchanged.
- The follow-up section explicitly calls out residual warnings on `11_setHostApModeInternal_redecomp.c` and the genuinely-noReturn `FUN_ffffff8000307340` retained on `12_setRSN_CONF_redecomp.c`.

## NON-CLAIMS

- This batch does not claim recovery of any complete byte-level field-named wire struct.
- This batch does not add any source typedef, struct definition, or other source declaration.
- This batch does not claim that any decompiled body is fully recovered when the cited Ghidra output carries decompiler warnings (especially `11_setHostApModeInternal_redecomp.c`, which is warned-improved, not closed).
- This batch does not claim a host APSTA owner `.cpp` implementation, AP profile carrier, carrier-to-HAL mapper, or beacon template builder.
- This batch does not claim any change to the iwx AP/GO HAL boundary committed in the closed CR-464.
- This batch does not claim AP-up, beaconing, AP probe-response template upload, AP time-event/session protection, AP client association, AP DHCP, AP traffic, role-7 success, CONTROL_STA_NETWORK success, lab AP success, or project completion.
- This batch does not claim that AP control-plane decomp/reference debt is closed; the post-pass open list above bounds the residual debt.
- This batch does not claim resolution of the pre-existing STA post-RUN deauth root tracked under CR-285 / CR-291 / CR-294 / CR-295 / CR-296 / CR-297 / CR-298.

## RESIDUAL UNCERTAINTY

- Bytes 0..2 of `apple80211_softap_wifi_network_info` and the inner structure of the 32-byte payload at offsets `0x04..0x23` are not classified by this pass; producer-side decomp is required.
- Per-cipher sub-block reads at `param_2 + 0x30 + i*8` and `+0x80 + i*8` of `apple80211_rsn_conf_data` remain open after the redecomp.
- Inner sub-fields of the channel descriptor beyond `+0x04` (primary-channel byte in u32 carrier) and `+0x08` (flags word) remain open.
- Fields of `apple80211_network_data` beyond SSID remain in the warned/truncated regions of `setHostApModeInternal`; recovering them needs per-truncation-site `CreateFunction` annotations plus jumptable target enumeration.
- Selector 26 inner reads of `apple80211_apmode_data` remain open; only the owner is classified.
- Reset coverage of the APSTA private state beyond the closed-CR-465 proven offsets remains open.

## PROVENANCE

- Basis commit: `4d86c86e2e2ea5d31a8c677f89fccdb0a3c474a6`.
- Reference host: `192.168.40.116`.
- Ghidra project: `/srv/project/ghidra_output` project `wifi_analysis_26_3` program `BootKernelExtensions.kc` (project save persisted the new function entries, names, and corrected `setNoReturn` flags).
- Ghidra build: `/srv/project/ghidra/build/ghidra_install/ghidra_12.2_DEV/support/analyzeHeadless`.
- Decomp scripts (saved on the reference host for reproducibility): `/srv/project/ghidra_output/CR465FollowUpDecompV2.py` (canonical) and `/srv/project/ghidra_output/CR465FollowUpDecomp.py` (V1 predecessor).
- Per-target outputs: `/srv/project/ghidra_output/cr465_followup/*.c` plus `csa_window_funcs.txt`, `function_creation_log.txt`, `noreturn_remediation.txt`, `run.log`, and the headless run log `cr465_followup_v2.headless.log`.
