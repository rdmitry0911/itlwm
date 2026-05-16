# 2026-05-16 Stage 1 design: Tahoe Skywalk CUR_PMK / CIPHER_KEY external PMK ingestion layer

correlation_id: CR-479-stage1-tahoe-skywalk-CUR_PMK-CIPHER_KEY-external-pmk-ingestion-20260516
review_stage: STAGE_1_STRUCTURAL
anomaly_id: STA_TAHOE_SKYWALK_WPA2_PSK_4WAY_INCOMPLETE_NO_PMK_DELIVERY
implementation_route_decision: IMPLEMENT_LOCAL
decomp_reference_closure_status: FULL_DECOMP_CLOSED
reference_decomp_first_for_capability_gap: YES
coder_decomp_completeness_self_check: YES
coder_payload_field_lifecycle_completeness_self_check: YES

## Authoritative reference contract

The Apple Tahoe BootKC delivers the host-supplicant PSK PMK on Skywalk-managed
STA associations through two convergent carriers and a separate association
candidate carrier. The accepted reference-decomp closure for this layer
recovers:

- `apple80211setCIPHER_KEY` at the public IOCTL `APPLE80211_IOC_CIPHER_KEY=3`,
  with `key_cipher_type==APPLE80211_CIPHER_PMK=6` selecting the PMK install
  branch. Case 9 (`APPLE80211_CIPHER_MSK`) shares the same PMK owner helper.
- `apple80211setCUR_PMK` at BootKC label `0xffffff80021eb3b9`. The
  trampoline loads selector `0x168` (=`360`), passes the command-gate at
  vtable offset `+0xcc8`, safe-casts to the Skywalk interface, and invokes
  the virtual at vtable offset `+0x1770` with `(this, apple80211_pmk*)`. The
  matching getter `apple80211getCUR_PMK` uses selector `0x16a` and Skywalk
  virtual slot `+0xff0`; the AppleBCMWLANInfraProtocol receiver
  `AppleBCMWLANInfraProtocol::getCUR_PMK @ 0xffffff80015424b4` loads the
  owner at `this + 0x130` and tail-calls the AppleBCMWLANCore getter
  `FUN_ffffff80016318c2`.
- `apple80211setWCL_ASSOCIATE` is the hidden association candidate carrier
  with a separate selector gate and virtual receiver. It may legally carry
  `externalPmkOwner=true` with `key_len=0`; the PMK bytes for that edge
  arrive through CIPHER_KEY(PMK) or CUR_PMK, not through the WCL_ASSOCIATE
  candidate.

The `apple80211_pmk` carrier laid down by `apple80211setCUR_PMK` has the
recovered field layout shown below. The setter source key material lives
at offset `+0x10` and the validated key length at offset `+0x04`; the
getter snapshots owner bytes starting at `+0x08` and stamps status and
metadata at `+0x48`, `+0x4c`, and `+0x54`:

| Offset | Size      | Field                  | Producer               | Consumer / lifetime |
| ------ | --------- | ---------------------- | ---------------------- | ------------------- |
| +0x00  | 4         | header / reserved      | -                      | not consumed        |
| +0x04  | 4         | key_len                | setter caller          | accepted when < 0x41 |
| +0x08  | up to 64  | getter output prefix   | `FUN_ffffff80016318c2` | getter snapshot     |
| +0x10  | up to 64  | setter source bytes    | setter caller          | copied by `FUN_ffffff8001631a1a` into owner core + 0xdf |
| +0x48  | 4         | status / version tag   | getter (writes 0x10)   | caller status check |
| +0x4c  | 8         | metadata cookie 0      | getter / setter clears | caller metadata     |
| +0x54  | 8         | metadata cookie 1      | getter / setter clears | caller metadata     |

The AppleBCMWLANCore PMK owner stores the PMK bytes at `core + 0xdf` and the
owner length at `core + 0x120`. The zeroizer `FUN_ffffff8001631d50` clears
the 64-byte PMK store and the owner length; this is the documented
cleanup/reset boundary. PMK byte regions are credential material and may
only be reported through credential-safe structural markers, never as
plain bytes in any log.

## Local-to-Apple mapping

| Apple owner / field                           | Local mapping                                                 |
| --------------------------------------------- | ------------------------------------------------------------- |
| AppleBCMWLANCore PMK bytes `core + 0xdf`      | `ieee80211com::ic_psk`                                        |
| AppleBCMWLANCore PMK length `core + 0x120`    | `IEEE80211_PMK_LEN` length validation on the helper input     |
| AppleBCMWLANCore PMK ready                    | `ic->ic_flags & IEEE80211_F_PSK` plus refreshed WPA/RSN state |
| `apple80211setCIPHER_KEY` case 6 / case 9     | `AirportItlwmSkywalkInterface::setCIPHER_KEY` `APPLE80211_CIPHER_PMK` branch |
| `apple80211setCUR_PMK` selector 0x168 / vtable +0x1770 | active SET-side path is the trampoline at `0xffffff80021eb3b9` -> command gate vtable `+0xcc8` -> Skywalk virtual receiver `+0x1770`; this bypasses the V2 `handleCardSpecific` bridge. Local override at `+0x1770` is the bounded decomp-only follow-up scoped on `docs/reference/CR-479-cur-pmk-userclient-dispatch-evidence-20260516.md`. |
| `WCLJoinRequest` private key body +0x18, candidate body +0x20 | `associateSSID(... externalPmkOwner=true, key_len=0)`; no copy |
| WCL leave/disassoc/PMKSA clear/reassoc reset  | `clearExternalPmkEligibilityLocked(...)`                      |
| First M1 / PAE consumer                       | `ieee80211_recv_4way_msg1` reading `ic_psk` into `ni_pmk`     |
| `apple80211getCUR_PMK` selector 0x16a          | local `AirportItlwmSkywalkInterface::getCUR_PMK` reachable through the V2 `handleCardSpecific` allow-list (BSDCommand `getCurPmk` static helper at `0xffffff80022a7149` exists); returns credential-safe Apple failure `0xe00002c7` without snapshotting `ic_psk` |
| `programPMK` (BCM firmware-only)              | not implemented; out of itlwm scope                           |

## Submitted bounded implementation

This Stage 1 is a decomp / documentation-only submission. The patch
artifact carries only the analysis and reference documents that
record the recovered Apple Tahoe Skywalk CUR_PMK / CIPHER_KEY
external-PMK carrier contract, the AppleBCMWLAN owner / lifecycle
map, and the IO80211Family CUR_PMK dispatch architecture. No
semantic source-tree change is included in this Stage 1. The
submitted documentation files are:

- `analysis/CR-479-cur-pmk-carrier-ghidra-20260516.md` and
  `docs/reference/CR-479-cur-pmk-carrier-ghidra-20260516.md` -
  recovered Apple carrier contract (selector dispatch, carrier
  field layout, owner / lifecycle map, AppleBCMWLAN setKey case
  mapping, credential safety rules).
- `analysis/CR-479-cur-pmk-userclient-dispatch-evidence-20260516.md`
  and `docs/reference/CR-479-cur-pmk-userclient-dispatch-evidence-20260516.md`
  - recovered IO80211Family CUR_PMK dispatch architecture from the
  KDK symbol map: GET has BSDCommand `getCurPmk` and UserClient
  `getCUR_PMK` static helpers; SET has only the trampoline at
  `0xffffff80021eb3b9`. The active SET-side delivery path is the
  trampoline -> command gate `+0xcc8` -> Skywalk virtual receiver
  `+0x1770`; the BSDCommand surface is structurally not the SET
  ingress on Tahoe.
- `analysis/CR-479-set-cur-pmk-vtable-slot-ordinal-20260516.md`
  and `docs/reference/CR-479-set-cur-pmk-vtable-slot-ordinal-20260516.md`
  - recovered Apple absolute vtable slot ordinal for the SET-side
  Skywalk virtual receiver at `+0x1770`. Base slot is
  `_RESERVED_IO80211SkywalkInterface_11` at absolute zero-based
  slot index `750` in `__ZTV23IO80211SkywalkInterface`
  (`0xffffff80023e5950`); the Apple Bcom production override on
  `AppleBCMWLANIO80211APSTAInterface` resolves the slot to
  `0xffffff8000b72960` with the live Tahoe `setCUR_PMK` body.
  This closes the named bounded follow-up at the ordinal
  granularity and identifies the local-header gap between current
  slot `[663]` (`setTX_MODE_CONFIG`) and target slot `[750]`
  (`setCUR_PMK`) that the next semantic Stage 1 must close.
- `analysis/ANALYSIS_REPORT_2026-05-16.md` - this document. Records
  the recovered local-to-Apple mapping for the carrier layer, the
  named bounded follow-up tasks needed to close the SET-side
  ingress and the lifecycle integration, and the runtime acceptance
  markers that a future semantic Stage 1 must exercise.
- `analysis/ANALYSIS_REPORT_2026-05-14j.md`,
  `analysis/ANALYSIS_REPORT_2026-05-15a.md`,
  `analysis/ANALYSIS_REPORT_2026-05-15b.md`,
  `analysis/ANALYSIS_REPORT_2026-05-15c.md` - prior reference and
  link-state analysis carried forward into the baseline.

The named bounded follow-ups that a future semantic Stage 1 must
implement, each backed by the documentation in this submission, are:

- Override the IO80211SkywalkInterface vtable slot at offset
  `+0x1770` (the SET-side Skywalk virtual receiver for
  `apple80211setCUR_PMK`) so the trampoline reaches a local
  credential-safe external-PMK ingestion helper. The Apple
  absolute slot ordinal for that position is now recovered as
  slot `[750]` (`_RESERVED_IO80211SkywalkInterface_11` in the
  base class, `setCUR_PMK` after subclass override on
  `AppleBCMWLANIO80211APSTAInterface`); see
  `analysis/CR-479-set-cur-pmk-vtable-slot-ordinal-20260516.md`.
  Landing the matching local pure-virtual / override declaration
  in `include/Airport/IO80211InfraProtocol.h` requires extending
  the header from its current last declared slot `[663]`
  (`setTX_MODE_CONFIG`) up to slot `[750]` (`setCUR_PMK`),
  including 75 concrete Apple virtuals in slots `[664]`-`[738]`
  and 11 reserved-slot stubs in slots `[739]`-`[749]`. That
  header-extension layer is a separate bounded Stage 1 of its own
  and is explicitly out of scope for this documentation-only
  submission.
- Wire the local `installExternalPmkLocked(pmk_bytes, key_len,
  source_tag)` credential-safe helper from
  `setCIPHER_KEY(APPLE80211_CIPHER_PMK)`, the 32-byte case of
  `setCIPHER_KEY(APPLE80211_CIPHER_MSK)`, and the new
  `setCUR_PMK(apple80211_pmk *)` override into `ieee80211com::ic_psk`
  with refreshed WPA/RSN PSK auth state; longer 8021X MSK payloads
  fall back to the existing `ieee80211_pmksa_add(...,
  IEEE80211_AKM_8021X, ...)` path.
- Add `clearExternalPmkEligibilityLocked(reason)` at every recovered
  Apple lifecycle reset edge: `setDISASSOCIATE` (entry, covering
  AUTH/ASSOC early-return sub-paths), `setCLEAR_PMKSA_CACHE`,
  `setWCL_LEAVE_NETWORK`, `setWCL_REASSOC`, `setWCL_JOIN_ABORT`, and
  the `associateSSID` RSN-disable path.
- Add `APPLE80211_IOC_CUR_PMK = 360` to `apple80211_ioctl.h` and a
  matching dispatcher case in
  `AirportItlwmSkywalkInterface::processApple80211Ioctl`. Update the
  V2 `shouldRouteTahoeSkywalkIoctlReq(...)` allow-list to forward
  the GET direction through the card-specific bridge to the
  credential-safe `getCUR_PMK` (SIOCGA80211); the SET direction does
  not reach this bridge on Tahoe per the dispatch evidence above
  and is wired through the `+0x1770` override.
- Add `struct apple80211_pmk` to `apple80211_var.h` with the
  recovered field layout so the carrier is type-safe.
- Add a credential-safe `ni_pmk` nonzero-bytes structural marker in
  `ieee80211_recv_4way_msg1` to expose the consumer-side observation
  without logging PMK material.
- Preserve the existing PSK-gated `associateSSID` `disable_rsn`
  skip so a PMK delivered before `setWCL_ASSOCIATE` is not zeroed
  at the association edge.
- Keep `getCUR_PMK` credential-safe with the documented Apple
  failure `0xe00002c7`.

Each follow-up item is named with the exact local function /
header touchpoints, the recovered Apple semantic contract, and the
credential-safe markers that runtime evidence will exercise. A
future semantic Stage 1 will implement them as a coherent layer
with a concrete after-fix runtime plan against the exact diff.

## After-fix runtime acceptance markers (future semantic Stage 1)

The structural markers below answer the layer discriminator and are
already present in the submitted diff. None of them log raw PMK bytes;
the install and clear markers report only the carrier source tag, the
post-action nonzero-byte count, and the RSN policy state.

- `setCipherKey_pmk_install_count`: bumps on every successful external
  PMK install reached through the `setCIPHER_KEY` dispatcher (case 6
  PMK, case 9 MSK with 32-byte payload, and the shared helper used by
  `setCUR_PMK`). Preserves backward compatibility with the prior
  CIPHER_KEY install counter.
- `setCUR_PMK_pmk_install_count`: bumps only when the install entered
  through the CUR_PMK carrier (source tag `CUR_PMK`).
- `external_pmk_eligibility_clear_count`: bumps on each lifecycle clear
  edge: `setDISASSOCIATE`, `setCLEAR_PMKSA_CACHE`, `setWCL_LEAVE_NETWORK`,
  `setWCL_REASSOC`, `setWCL_JOIN_ABORT`, and the `associateSSID`
  RSN-disable path.
- `install_external_pmk INSTALLED source=<CIPHER_KEY|CIPHER_KEY_MSK|CUR_PMK> ...`
  non-secret marker, with `ic_psk_nonzero_bytes`, `ic_flags`, and RSN
  policy state.
- `clear_external_pmk CLEARED reason=<setDISASSOCIATE|setCLEAR_PMKSA_CACHE|setWCL_LEAVE_NETWORK|setWCL_REASSOC|setWCL_JOIN_ABORT|associateSSID_disable_rsn>`
  non-secret marker with post-clear nonzero-byte count `0/<sz>`.
- `ieee80211_recv_4way_msg1_pmk_check` consumer marker: must observe
  `ni_pmk_nonzero_bytes >= 16/32` on the association edge that is
  expected to complete the 4-way handshake.

## Failed hypothesis ledger (CR-479 same-root lineage)

After three consecutive same-root Stage 2 outcomes (rev2 broad RSN
preservation, rev3 PMK carrier direction, rev4 PSK-gated preservation +
CIPHER_KEY(PMK) install) failed to reach `allow_commit_now=YES`, the
reference-decomp closure stepped one level back to "how Tahoe airportd
actually delivers or owns PSK PMK material across the Apple80211 /
Skywalk / WCL boundary". The closure proved that CUR_PMK is the
alternate active carrier and that both CIPHER_KEY(PMK) and CUR_PMK
converge on the same Apple-owner / itlwm-`ic_psk` PMK sink. This Stage 1
implements that closed contract; it is not a fourth same-layer guess but
the bounded `IMPLEMENT_LOCAL` carrier-merge layer named by the auditor's
import review.

## CUR_PMK dispatch architecture and active local ingress

Static symbol-table evidence extracted from the Tahoe BootKC KDK
(`/srv/project/ghidra_output/kdk_symbols.txt`) and cross-referenced
under `docs/reference/CR-479-cur-pmk-userclient-dispatch-evidence-20260516.md`
confirms that selector 0x168 / IOC 360 has an asymmetric dispatch
architecture in IO80211Family:

- GET (`apple80211getCUR_PMK`, selector 0x16a): both the UserClient
  static helper `getCUR_PMK(IO80211Controller *, IO80211SkywalkInterface *,
  IO80211APIUserClient *, apple80211req *)` at `0xffffff80022003a1` and
  the BSDCommand static helper `getCurPmk(IO80211Controller *,
  IO80211SkywalkInterface *, apple80211req *, bool)` at
  `0xffffff80022a7149` exist. The BSDCommand path reaches the V2
  controller `handleCardSpecific` bridge.
- SET (`apple80211setCUR_PMK`, selector 0x168): only the trampoline at
  `0xffffff80021eb3b9` exists. There is no `setCurPmk` BSDCommand
  static helper in IO80211Family, and no UserClient static helper with
  the `(IO80211Controller *, IO80211SkywalkInterface *,
  IO80211APIUserClient *, apple80211req *)` signature. By contrast,
  CIPHER_KEY has the full set: BSDCommand `setCipherKey` at
  `0xffffff80022d411e` and UserClient `setCIPHER_KEY` at
  `0xffffff8002206ea8`.

The absence of the SET-side static dispatcher means Tahoe userspace
delivery of selector 0x168 SET goes only through the trampoline:
`IO80211APIUserClient::externalMethod` -> `apple80211setCUR_PMK` ->
command gate at vtable `+0xcc8` -> Skywalk virtual receiver at vtable
`+0x1770` with `(this, apple80211_pmk *)`. This path bypasses
`AirportItlwm::handleCardSpecific`, `routeTahoeSkywalkIoctl`,
`shouldRouteTahoeSkywalkIoctlReq`, and
`AirportItlwmSkywalkInterface::processApple80211Ioctl`.

Local consequences in the submitted diff:

- The V2 `handleCardSpecific` allow-list entry that lists
  `APPLE80211_IOC_CUR_PMK` is defensible for the GET direction (the
  `getCurPmk` BSDCommand dispatcher routes SIOCGA80211 traffic into
  the V2 bridge) and is harmless on the SET direction (no SET
  traffic reaches the bridge through BSDCommand, so the dispatcher
  case is never triggered for the SET direction).
- The `setCUR_PMK(apple80211_pmk *)` method on
  `AirportItlwmSkywalkInterface` and `installExternalPmkLocked(...)`
  are in place as the local PMK-owner handler, ready to be wired to
  the Skywalk virtual receiver at vtable `+0x1770` when the local
  SET-side vtable layout is recovered.

The remaining work to close the active SET-side CUR_PMK ingress is to
override the IO80211SkywalkInterface vtable slot at offset `+0x1770`
in the local driver so the trampoline reaches
`installExternalPmkLocked`. The Apple absolute vtable slot ordinal
for that position is now recovered as slot `[750]`
(`_RESERVED_IO80211SkywalkInterface_11` in the base class, and
`setCUR_PMK` after subclass override on
`AppleBCMWLANIO80211APSTAInterface`) by direct chained-fixup decode
of `__ZTV23IO80211SkywalkInterface` at `0xffffff80023e5950`; the
reproducible PyGhidra evidence is recorded in
`analysis/CR-479-set-cur-pmk-vtable-slot-ordinal-20260516.md` and
its `docs/reference/` twin. Adding the local override requires
extending `include/Airport/IO80211InfraProtocol.h` from its current
last declared slot `[663]` (`setTX_MODE_CONFIG`) up to slot `[750]`
(`setCUR_PMK`), including 75 concrete Apple virtuals in slots
`[664]`-`[738]` and 11 reserved-slot stubs in slots `[739]`-`[749]`
without shifting other already-tested SET-side overrides
(`setCIPHER_KEY`, `setRSN_IE`, `setWCL_*`, and others). That
header-extension layer is a separate bounded Stage 1 of its own and
is explicitly out of scope for this documentation-only submission;
this rev9 closes only the ordinal-granularity follow-up authorized by
the rev8 decision.

## Rev11 semantic Stage 1: bounded source/header/docs integration

After the rev10 decision accepted the documentation-only baseline and
the auditor routed the broad header-extension reference layer through
the project web-AI provider workflow, the imported decomp result for
task `cr479_skywalk_slots_664_750_supplement_project_sources_decomp_20260516T0616`
(result SHA-256 `4fdcc13616044c485d6dab4b2217c5ad4a95d7f87a54abc3ad57ab2c1603667d`)
returned `DECOMP_REFERENCE_CLOSURE_STATUS: FULL_DECOMP_CLOSED` and
`REMAINING_DECOMP_TARGETS: NONE`. The accepted project sources
supplement `cr479_skywalk_slots_664_750_supplement_01.md` (SHA-256
`e0f82778eaebaa2711168f3563927147cce067c32fdb5291ef285b7a5ef0c601`)
plus the underlying static-analysis batch archive
`cr479_skywalk_slots_664_750_20260516T0600.tar.zst` (SHA-256
`49c731ff2f7fd26489f408571694fda2d85a67f57dfbb6fc869ceda02b025084`)
provide the per-slot semantic identity for every IO80211SkywalkInterface
vtable slot in the range `[664]`..`[750]`. The closure is consolidated
in `analysis/CR-479-skywalk-slots-664-750-closure-20260516.md` and its
byte-identical `docs/reference/` twin.

One semantic correction relative to the rev10 narrative is recorded by
the closure: slots `[739]`..`[749]` are not reserved-slot stubs. Slot
`[746]` was the only row flagged unresolved by the reserved-slot
adjacency proof, and the baseline KDK symbol map resolves it to the
concrete IO80211VirtualInterface virtual `logTxPacket(IO80211NetworkPacket*,
PacketSkywalkScratch*, apple80211_wme_ac, bool)`. The full window
`[739]`..`[749]` therefore declares 11 non-reserved concrete virtuals,
not reserved-slot stubs; together with the 75 concrete virtuals in
`[664]`..`[738]` this yields 86 non-pure placeholder declarations to
preserve vtable layout before the pure virtual `setCUR_PMK` at
slot `[750]`.

This rev11 Stage 1 submits the bounded coder integration that the
auditor named as `SMALL_BOUNDED_CODER_REQUEUE` after accepting that
closure. The exact submitted diff contains:

- `include/Airport/IO80211InfraProtocol.h`: extend the local
  `IO80211InfraProtocol` declarations by 87 entries to span Apple
  absolute slots `[664]`..`[750]`. Slots `[664]`..`[749]` are
  non-pure placeholder virtuals whose name suffix preserves the
  exact recovered target hex (e.g.
  `_TahoeSlot664_ffffff8002a28f14()`), and slot `[750]` is the pure
  virtual `setCUR_PMK(apple80211_pmk *) = 0`. Slot positions for
  every prior local override (`setCIPHER_KEY`, `setRSN_IE`,
  `setWCL_*`, etc.) are preserved unchanged because the new
  declarations are appended after slot `[663]`.
- `include/Airport/apple80211_var.h`: add `struct apple80211_pmk`
  with the recovered field layout (header `+0x00`, validated
  `key_len` at `+0x04`, getter output prefix `+0x08`..`+0x0F`,
  setter source bytes `+0x10`..`+0x47`, status at `+0x48`, metadata
  cookies at `+0x4c` and `+0x54`).
- `include/Airport/apple80211_ioctl.h`: define
  `APPLE80211_IOC_CUR_PMK = 360` plus a corrected 32-byte
  `apple80211_link_changed_event_data` layout that places
  `voluntary_down` at `+0x1c`, `voluntary_up` at `+0x1d`, and unions
  `rssi`/`reason` at `+0x04`, matched by `static_assert`s for size
  and field offsets.
- `AirportItlwm/AirportItlwmSkywalkInterface.{hpp,cpp}`: declare and
  implement the slot `[750]` override `setCUR_PMK(apple80211_pmk *)`,
  the credential-safe shared helper `installExternalPmkLocked(...)`,
  the lifecycle reset helper `clearExternalPmkEligibilityLocked(...)`,
  the CIPHER_KEY case 6 / case 9 convergence on the shared helper,
  the IOCTL dispatcher case `APPLE80211_IOC_CUR_PMK`, the PSK-gated
  `associateSSID` `disable_rsn` skip with paired lifecycle reset,
  the credential-safe `getCUR_PMK` returning Apple failure
  `0xe00002c7`, and the Tahoe Skywalk `APPLE80211_M_LINK_CHANGED`
  32-byte event publisher driven by the Skywalk
  `setLinkStateInternal` override on the parent-success edge.
- `AirportItlwm/AirportItlwmV2.cpp`: add `APPLE80211_IOC_CUR_PMK` to
  the `shouldRouteTahoeSkywalkIoctlReq(...)` allow-list (the GET
  direction reaches the V2 card-specific bridge through the
  BSDCommand `getCurPmk` static helper recorded in the rev10
  dispatch evidence; the SET direction never reaches the bridge on
  Tahoe and is wired through the `+0x1770` override). Stop the
  legacy zero-length `APPLE80211_M_LINK_CHANGED` and
  `APPLE80211_M_BSSID_CHANGED` publications in
  `setLinkStateGated(...)` on the Tahoe build path so the recovered
  32-byte payload owner remains the single publisher per accepted
  transition.
- `AirportItlwm/AirportSTAIOCTL.cpp`: mirror the corrected 32-byte
  `apple80211_link_changed_event_data` shape on the V1 controller
  IOCTL responder so V1 and V2 read the same fields at the same
  offsets regardless of which controller class is bound.
- `itl80211/openbsd/net80211/ieee80211_pae_input.c`: emit a
  credential-safe structural observation point in
  `ieee80211_recv_4way_msg1` that reports `ni_pmk_nonzero_bytes` and
  the RSN AKM mask before the PTK derivation consumes `ni_pmk`; no
  raw PMK bytes are logged.
- `analysis/CR-479-skywalk-slots-664-750-closure-20260516.md` and
  its byte-identical `docs/reference/` twin: consolidate the imported
  web-AI closure (slot-indexed semantic table, slot 750 closure
  proof, local declaration impact, provenance and audit trail,
  credential safety rules) as the durable evidence backing the
  source diff.
- `analysis/ANALYSIS_REPORT_2026-05-16.md`: this rev11 framing
  section.

The runtime acceptance markers expected from a future Stage 2 after
guest install/reboot/runtime remain those already recorded in the
"After-fix runtime acceptance markers" section above:
`install_external_pmk INSTALLED source=<CIPHER_KEY|CIPHER_KEY_MSK|CUR_PMK>`,
`clear_external_pmk CLEARED reason=...`, and
`ieee80211_recv_4way_msg1_pmk_check ni_pmk_nonzero_bytes>=16/32`.
This rev11 Stage 1 is structural-review only: no install, reboot,
kext-load, client/AP runtime, AP-mode test, or commit is requested.

## Rev12 semantic Stage 1: packed apple80211_pmk layout correction and Stage 1 runtime authorization gate

The rev11 Stage 1 decision (`commit-approval/decisions/COMMIT_DECISION_CR-479-stage1-tahoe-skywalk-CUR_PMK-CIPHER_KEY-external-pmk-ingestion-rev11.md`) accepted the source/header/docs integration scope and confirmed the exact patch hash, the rebuild against the Tahoe BootKC, and the dispatch / vtable / slot 750 boundary evidence, but rejected the request on two structural points that rev12 corrects directly inside the submitted diff:

- `struct apple80211_pmk` as submitted by rev11 was not packed. Natural C++ 64-bit alignment pushed `apple_pmk_metadata_0` to `+0x50`, `apple_pmk_metadata_1` to `+0x58`, and `sizeof(struct apple80211_pmk)` to `0x60`. The request text and the inline header comments claimed metadata `+0x4c` / `+0x54` and `sizeof 0x5c`, so the submitted carrier type did not encode the field-level layout asserted by the rev11 evidence.
- The rev11 request explicitly refused after-fix runtime authorization and deferred the first runtime gate to a future Stage 2. Under the Stage 1 protocol used in this project, a Stage 1 approval is itself the authorization for the coder to build/install/run the exact reviewed diff and collect after-fix evidence on the PSK association edge; Stage 2 reviews the collected runtime evidence and does not act as the first runtime gate.

Rev12 corrects both blockers inside the submitted diff and re-files the same bounded source/header/docs integration on top of the rev11 evidence base. The carrier-type correction lives in `include/Airport/apple80211_var.h`: `struct apple80211_pmk` is now declared `__attribute__((packed))`, an inline comment records that natural 64-bit alignment would push the metadata fields to `+0x50`/`+0x58` and grow the size to `0x60` and break the IOCTL ABI, and a contiguous block of `static_assert`s pins the exact field layout the rev11 evidence asserted:

```
static_assert(sizeof(struct apple80211_pmk) == 0x5c, ...);
static_assert(__offsetof(struct apple80211_pmk, apple_pmk_header)         == 0x00, ...);
static_assert(__offsetof(struct apple80211_pmk, apple_pmk_key_len)        == 0x04, ...);
static_assert(__offsetof(struct apple80211_pmk, apple_pmk_getter_window)  == 0x08, ...);
static_assert(__offsetof(struct apple80211_pmk, apple_pmk_setter_source)  == 0x10, ...);
static_assert(__offsetof(struct apple80211_pmk, apple_pmk_status)         == 0x48, ...);
static_assert(__offsetof(struct apple80211_pmk, apple_pmk_metadata_0)     == 0x4c, ...);
static_assert(__offsetof(struct apple80211_pmk, apple_pmk_metadata_1)     == 0x54, ...);
```

The compile-time checks fail closed if anyone ever drops `packed`, reorders the fields, or changes their widths. The guest Tahoe build with the packed type plus the eight asserts in place succeeds (`** BUILD SUCCEEDED **`, `OK: all 927 undefined symbols resolve against BootKC`); no other source file required adjustment because every consumer reads `apple_pmk_key_len` and `apple_pmk_setter_source` through the struct field names, not through hard-coded offsets, and no consumer takes the address of a packed sub-field. The C++ rule that taking a reference/pointer to a packed sub-field is undefined behaviour is therefore preserved.

The Stage 1 runtime authorization correction is purely in the request and analysis text. Rev12 explicitly asks the auditor for normal Stage 1 structural approval that authorizes the coder to build the exact reviewed diff, install the resulting kext, reboot the guest, and collect after-fix runtime evidence on the PSK association edge. The submitted diff itself does not perform any of those actions; no install, reboot, kext-load, client/AP runtime, AP-mode test, or commit happens before auditor approval of rev12. After approval, the coder will collect the same credential-safe markers already documented in this report under "After-fix runtime acceptance markers (future semantic Stage 1)":

- `install_external_pmk INSTALLED source=<CIPHER_KEY|CIPHER_KEY_MSK|CUR_PMK> key_len=32 nonzero_bytes=N`
- `clear_external_pmk CLEARED reason=<setDISASSOCIATE|setCLEAR_PMKSA_CACHE|setWCL_LEAVE_NETWORK|setWCL_REASSOC|setWCL_JOIN_ABORT|associateSSID_disable_rsn>`
- `ieee80211_recv_4way_msg1_pmk_check ni_pmk_nonzero_bytes=N/32 rsnakms=0xMASK` on the consumer side before `ieee80211_derive_ptk`
- One `APPLE80211_M_LINK_CHANGED` publication per accepted parent-success edge with the recovered 32-byte payload (no legacy zero-length publication during the same accepted transition)

The rev12 documentation update therefore removes the "Stage 1 is structural-review only" stance from the active CR-479 narrative and replaces it with the normal Stage 1 gate: structural approval authorizes the exact after-fix runtime collection that produces the Stage 2 evidence. The structural claims of the rev11 narrative (slot 750 boundary proof, slot 664-750 closure, AppleBCMWLAN owner / lifecycle map, carrier field layout, dispatch architecture) remain valid and are inherited unchanged. The only field-level claim that rev12 re-asserts is the packed `apple80211_pmk` carrier; every offset in that claim is now encoded by the submitted type and pinned by a `static_assert`, so the claim does not exceed what the diff supports.


## Residual uncertainty

- `programPMK` is BCM firmware-only; the local driver intentionally
  does not implement it. AP mode, CONTROL_STA_NETWORK control-plane completion,
  DHCP/IP, and post-association stability remain out of scope.
- Slots `[664]`..`[749]` are declared as non-pure placeholder
  virtuals to preserve vtable layout for the slot `[750]` override;
  no semantic behaviour is implemented for those placeholder slots.
  Apple invocation of any of those slots through the base
  `IO80211SkywalkInterface` vtable will reach an empty-body local
  default; the rev10 dispatch evidence and the slot 664-750 closure
  do not show any active Tahoe code path reaching the local subclass
  through those slots on the PSK association edges in scope. If a
  future runtime indicates a missed slot, the placeholder is replaced
  by a real override in a separate bounded layer; this rev11 does
  not claim semantic coverage of the placeholder window beyond
  vtable layout preservation.

## Rev13 bounded layer: APPLE80211_IOC_CIPHER_KEY routing and externalPmkOwner key_len=0 ic_psk preservation

### Why this section exists

The rev12 candidate built, installed, loaded, and ran on the live Tahoe
guest without panic, but the Stage 2 review rejected commit because every
local instrumentation marker the rev12 Stage 1 decision required at Stage
2 stayed at zero on the CONTROL_STA_NETWORK PSK association edge. The
decisive negative result falsifies the narrow rev12 hypothesis that
APPLE80211_IOC_CUR_PMK alone is the active live Tahoe PSK PMK delivery
selector. The accepted static-closure synthesis result for the active
Tahoe PSK PMK producer/carrier/order recommends IMPLEMENT_LOCAL on a
bounded local layer that closes the routing and ingestion gaps the rev12
runtime exposed.

### Gaps identified by the static closure and addressed in this iteration

1. The Tahoe Skywalk card-specific bridge `shouldRouteTahoeSkywalkIoctlReq`
   in AirportItlwmV2.cpp listed `APPLE80211_IOC_CUR_PMK` but did not list
   `APPLE80211_IOC_CIPHER_KEY = 3`. When Apple userspace delivers a PMK or
   MSK via that IOCTL on the Skywalk path, the request never reached the
   local `setCIPHER_KEY` handler; case 6 / case 9 convergence on
   `installExternalPmkLocked` was present in the built kext but unreachable
   from the live edge. The rev13 patch adds `APPLE80211_IOC_CIPHER_KEY` as a
   SET-side allowed selector so the local handler is invoked.

2. `AirportItlwmSkywalkInterface::associateSSID(...)` PSK branch
   unconditionally `memcpy`-ed the caller key into `ic->ic_psk` when the
   caller signalled `importLocalPmk = true`, regardless of the supplied
   `key_len`. The live Tahoe IOC_ASSOCIATE / WCL_ASSOCIATE carrier may
   legally arrive with `key == nullptr` or `key_len = 0` because Apple
   delivers the PSK PMK through `APPLE80211_IOC_CIPHER_KEY` cases 6 / 9 or
   `APPLE80211_IOC_CUR_PMK`. The previous memcpy destroyed any prior PMK
   install before the host supplicant first M1 consumer. The rev13 patch
   guards the memcpy on `key != nullptr && key_len >= sizeof(ic->ic_psk)`
   and routes any caller that asked for a local import without usable key
   bytes into the external-owner branch so the prior PMK install survives.

### What this iteration does not change

- The rev12 source / header / docs integration for slot `[664]..[750]`,
  packed `apple80211_pmk`, CIPHER_KEY case 6 / case 9 convergence on
  `installExternalPmkLocked`, the 32-byte `apple80211_link_changed_event_data`
  ABI, the Skywalk `APPLE80211_M_LINK_CHANGED` publisher, the legacy
  zero-length link/BSSID duplicate suppression on Tahoe, the credential-safe
  PAE supplicant marker, and the lifecycle clears at `setDISASSOCIATE`,
  `setCLEAR_PMKSA_CACHE`, `setWCL_LEAVE_NETWORK`, `setWCL_REASSOC`,
  `setWCL_JOIN_ABORT`, and `associateSSID_disable_rsn` are unchanged.
- No new diagnostic instrumentation, no new timing/retry/forced-state/
  fallback/masking/suppression, no new layer-widening claim is added.

### Bounded after-fix runtime plan

After Stage 1 approval the coder will build the exact reviewed diff,
install `/Library/Extensions/AirportItlwm.kext`, approve the kext UI through
VNC if macOS prompts, reboot, require SSH return within 120 seconds, and
collect runtime evidence on the CONTROL_STA_NETWORK PSK association edge.
The credential-safe rev12 markers `install_external_pmk INSTALLED source=
<CIPHER_KEY|CIPHER_KEY_MSK|CUR_PMK> key_len=32`, `ieee80211_recv_4way_msg1_
pmk_check ni_pmk_nonzero_bytes=N/32`, `clear_external_pmk CLEARED reason=`,
and the 32-byte `APPLE80211_M_LINK_CHANGED` publication remain the Stage 2
acceptance markers. Stage 2 also reuses CONTROL_STA_NETWORK ASSOC / DHCP /
stability / panic checks with credential redaction.

### Residual uncertainty

If after the rev13 routing fix the `install_external_pmk` marker still
stays at zero on the live edge, the next-step research must investigate the
`apple80211setWCL_ASSOCIATE` selector `0x1b3` carrier path and whether
Tahoe airportd uses an alternative WCL-only PMK delivery selector that
does not enter `APPLE80211_IOC_CIPHER_KEY` or `APPLE80211_IOC_CUR_PMK`.
