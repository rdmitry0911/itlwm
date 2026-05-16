# CR-479 IO80211SkywalkInterface vtable slot 664-750 closure (2026-05-16)

correlation_id: CR-479-stage1-tahoe-skywalk-CUR_PMK-CIPHER_KEY-external-pmk-ingestion-20260516
review_stage: REFERENCE_DECOMP_RECOVERY
provenance: Reference-decomp closure imported through the project web-AI
provider workflow against the prepared exact-scope project sources
supplement `cr479_skywalk_slots_664_750_supplement_01.md` (supplement
SHA-256 `e0f82778eaebaa2711168f3563927147cce067c32fdb5291ef285b7a5ef0c601`)
and the corresponding executable Ghidra/static-analysis batch archive
`cr479_skywalk_slots_664_750_20260516T0600.tar.zst` (archive SHA-256
`49c731ff2f7fd26489f408571694fda2d85a67f57dfbb6fc869ceda02b025084`);
imported web-AI task `cr479_skywalk_slots_664_750_supplement_project_sources_decomp_20260516T0616`
(result SHA-256 `4fdcc13616044c485d6dab4b2217c5ad4a95d7f87a54abc3ad57ab2c1603667d`)
with `DECOMP_REFERENCE_CLOSURE_STATUS: FULL_DECOMP_CLOSED` and
`REMAINING_DECOMP_TARGETS: NONE`.

## Scope

Closes the broad header-extension reference layer named by the
`CR-479-stage1-tahoe-skywalk-CUR_PMK-CIPHER_KEY-external-pmk-ingestion-rev10`
decision: every IO80211SkywalkInterface vtable slot from absolute index
`[664]` through absolute index `[750]` inclusive, including the prior
boundary slot `[750]` proof previously recorded in the rev9/rev10
ordinal-granularity follow-up. After this closure, no additional
decomp/static-analysis material is required before extending the local
header and implementing the slot `[750]` SET-side override.

## Vtable identification (recovered)

| Item                                     | Value                                |
| ---                                      | ---                                  |
| Base vtable                              | `__ZTV23IO80211SkywalkInterface`     |
| Base vtable address                      | `0xffffff80023e8248`                 |
| Slot range covered                       | `[664]`..`[750]` inclusive           |
| First slot entry address                 | `0xffffff80023e9708`                 |
| Last slot entry address (slot 750)       | `0xffffff80023e99b8`                 |
| Slot 750 base function                   | `0xffffff8002277d2e`                 |
| Slot 750 semantic identity (base)        | `_RESERVED_IO80211SkywalkInterface_11` (forwarding thunk through owner `[this+0x120]->[+0x28]->vtable+0x310`) |
| Slot 750 semantic identity (after override) | `setCUR_PMK(apple80211_pmk *)`     |
| Apple Bcom subclass override class       | `AppleBCMWLANIO80211APSTAInterface`  |
| Subclass vtable address                  | `0xffffff8001777508`                 |
| Subclass `+0x1770` target (live body)    | `0xffffff8000b72960`                 |

## Slot-indexed semantic table

The Ghidra/static-analysis batch produced the per-slot mapping below
through direct vtable reads at the base, symbol/function lookup
against the baseline KDK symbol map, pointer scans for sparse
targets, and disassembly/p-code fallback where required. Slots that
lack a baseline semantic identity preserve the recovered FUN_*/LAB_*
target hex in the local placeholder name rather than inventing a
method name.

- `[664]..[686]`: concrete Apple virtuals at FUN_* targets in the
  Bcom-private code range `0xffffff8002a28f14..0xffffff8002a38478`,
  with several entries reaching local thunks at FUN_* targets in the
  IO80211Skywalk private range `0xffffff80022b0532..0xffffff800227711a`.
- `[687]` and `[699]`: LAB_*-only targets at `0xffffff8002a386da` and
  `0xffffff8002a3895e`, preserved as symbol-only concrete entries.
- `[705]` (`0xffffff8002a38914`): baseline-enriched semantic identity
  `getBSDUnitNumber`; base behaviour returns `0` or `-1` per
  IOSkywalk internals; placeholder name preserves the target hex.
- `[721]` (`0xffffff800227711a`): related to symbol
  `initialPowerStateForDomainState` at `0xffffff800227714c` in the
  same private range; placeholder name preserves the target hex.
- `[722]..[738]`: trailing IO80211Skywalk/Bcom-private virtuals at
  FUN_* targets in `0xffffff8002a3dcf4..0xffffff8002a3f300`.
- `[739]..[742]`: non-reserved concrete functions at `0xffffff8002a3f316`,
  `0xffffff80022af2de`, `0xffffff80022768a2`, `0xffffff80022b1bf4`
  (correction: prior reserved-slot adjacency proof had marked the
  whole `[739]..[749]` window as reserved stubs; baseline symbols
  resolve these four as non-reserved virtuals).
- `[743]` (`0xffffff8002277a7a`): `IO80211SkywalkInterface::cachePeer`;
  matching `removePacketQueue(const IO80211FlowQueueHash*)` evidence
  is recorded nearby in the static batch.
- `[744]` (`0xffffff8002277ce4`): `IO80211SkywalkInterface::updateLinkStatus()`.
- `[745]` (`0xffffff8002277cc6`): `IO80211SkywalkInterface::setLQM(unsigned long long)`.
- `[746]` (`0xffffff80022b29ca`): `IO80211VirtualInterface::logTxPacket(IO80211NetworkPacket *, PacketSkywalkScratch *, apple80211_wme_ac, bool)`;
  corrected from the prior reserved-row assumption. Slot 746 is the
  only row the reserved-slot proof had marked unresolved, and the
  baseline symbol evidence resolves it to a concrete non-reserved
  IO80211VirtualInterface virtual.
- `[747]` (`0xffffff80022b2a56`): `IO80211VirtualInterface::postAWDLRtgStatistics(apple80211_awdl_rtg_statistics *)`.
- `[748]` (`0xffffff8002277d28`): `IO80211SkywalkInterface::setInterfaceCCA(apple80211_channel, int)`.
- `[749]` (`0xffffff80022b26c8`): non-reserved concrete function at the
  recovered target hex; placeholder name preserves the target hex.
- `[750]` (`0xffffff8002277d2e`): `_RESERVED_IO80211SkywalkInterface_11`
  forwarding thunk in the base vtable; the live `setCUR_PMK` body is
  reached only through the AppleBCMWLANIO80211APSTAInterface subclass
  override target `0xffffff8000b72960` per the prior boundary proof.

The full per-slot table with addresses, offsets, and target hex lives
in the project sources supplement `cr479_skywalk_slots_664_750_supplement_01.md`
and the executable static-analysis batch at
`cr479_skywalk_slots_664_750_20260516T0600/02_vtable/io80211skywalkinterface_slots_664_750.tsv`,
`02_vtable/io80211skywalkinterface_reserved_739_749_proof.tsv`,
`02_vtable/slot_750_boundary.tsv`, `05_symbols/matching_symbols.tsv`,
and `04_xrefs/`.

## Slot 750 closure proof (carried forward)

The slot 750 boundary proof previously recorded in the rev9/rev10
ordinal-granularity follow-up is corroborated by this batch through an
independent base-vtable scan, slot-boundary cross-reference, and APSTA
override pointer scan:

- Base vtable entry: `0xffffff80023e99b8`, offset `+0x1770`, target
  `0xffffff8002277d2e` (`_RESERVED_IO80211SkywalkInterface_11`).
- Setter dispatch wrapper `apple80211setCUR_PMK` at
  `0xffffff80021eb3b9` checks selector/command allowance, loads
  vtable slot `+0x1770`, passes `RDI = this` and
  `RSI = apple80211_pmk*`, and tail-jumps into the slot target.
- Apple Bcom subclass `AppleBCMWLANIO80211APSTAInterface` vtable
  `0xffffff8001777508` overrides slot `+0x1770` to live target
  `0xffffff8000b72960`, the production `setCUR_PMK` body that
  iterates the registered-manager table at `[0xffffff800132a6f0]`
  and calls the per-manager callback at `[[mgr+0x20]+0x788](mgr, pmk_arg, ..., 2)`.

## Local declaration impact

The local header `include/Airport/IO80211InfraProtocol.h` previously
declared 194 pure virtuals spanning Apple absolute slots `[470]`..`[663]`,
ending at `setTX_MODE_CONFIG`. Landing a credential-safe local
`setCUR_PMK(apple80211_pmk *)` override at Apple absolute slot
`[750]` therefore requires preserving the intermediate vtable
positions. Per the recovered per-slot semantic table above, the
intermediate range `[664]`..`[749]` contains 86 concrete Apple
virtuals (no reserved-slot stubs after the slot 746 correction), so
the local extension must declare 86 non-pure placeholder virtuals
with a recovered-name suffix (`_TahoeSlot664_ffffff8002a28f14()` and
friends) plus the pure-virtual `setCUR_PMK(apple80211_pmk *)` at slot
`[750]`. The placeholder names intentionally do not invent semantic
method identities for FUN_*/LAB_*-only targets; the suffix is the
exact target hex so the local header continues to be a recovered
artifact and not a speculative naming exercise.

## Provenance and audit trail

| Field                                    | Value                                |
| ---                                      | ---                                  |
| Web-AI provider                          | `chatgpt-cdp`                        |
| Web-AI task ID                           | `cr479_skywalk_slots_664_750_supplement_project_sources_decomp_20260516T0616` |
| Web-AI result SHA-256                    | `4fdcc13616044c485d6dab4b2217c5ad4a95d7f87a54abc3ad57ab2c1603667d` |
| Project sources supplement file          | `cr479_skywalk_slots_664_750_supplement_01.md` |
| Project sources supplement SHA-256       | `e0f82778eaebaa2711168f3563927147cce067c32fdb5291ef285b7a5ef0c601` |
| Underlying static-analysis batch archive | `cr479_skywalk_slots_664_750_20260516T0600.tar.zst` |
| Static-analysis archive SHA-256          | `49c731ff2f7fd26489f408571694fda2d85a67f57dfbb6fc869ceda02b025084` |
| Ghidra host                              | `192.168.40.116`                     |
| Ghidra project                           | `wifi_analysis_26_3_cr368_full`      |
| Ghidra program                           | `BootKernelExtensions.kc`            |
| Decomp closure status                    | `FULL_DECOMP_CLOSED`                 |
| Remaining decomp targets                 | `NONE`                               |
| Implementation route                     | `SMALL_BOUNDED_CODER_REQUEUE`        |

## Credential safety

None of the recovered slot evidence touches credential material. The
slot 750 `setCUR_PMK` semantic body operates on the externally-owned
PMK byte buffer `apple80211_pmk::apple_pmk_setter_source` at carrier
offset `+0x10`; the recovered Apple owner stores the buffer at
`core + 0xdf` and clears it through the zeroizer at
`FUN_ffffff8001631d50`. The local override implementation must
report only non-secret structural markers (length, non-zero byte
count, source tag) and must not log raw PMK bytes; that contract is
preserved in the local helpers `installExternalPmkLocked` and
`clearExternalPmkEligibilityLocked`.
