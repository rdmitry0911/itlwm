# Analysis report — 2026-05-14e (controller SoftAP extended-capabilities IE selector mirror routed through host APSTA owner)

This entry records the next Stage 1 layer after commit `9a07093`
(host APSTA owner factory and role-7 acquisition wiring): the
`AirportItlwm` controller-layer
`setSOFTAP_EXTENDED_CAPABILITIES_IE` selector forwards the recovered
Apple-body field writes through
`AirportItlwmAPSTAStage1Owner::setSoftAPExtCaps`, and the
private controller-side mirror state is retired. The owner's state
block already pins the recovered `+0x50/+0x51/+0x59` field offsets
through compile-time `static_asserts` on
`AirportItlwmAPSTAStateBlock::softapAppleVendorIEExtra50`,
`softapAppleVendorIETail51` and `softapAppleVendorIETail59`. The
layer is the controller-surface mirror only; no firmware backend,
no AP-up gate change, and no other selector wiring is touched.

## ANOMALY

- id: A-AIRPORTITLWM-SOFTAP-EXTCAPS-OWNER-UNWIRED-20260514
- status: CONFIRMED_CONTROLLER_SELECTOR_MIRROR_BYPASSES_OWNER
- symptom: At HEAD `9a07093`, the host APSTA owner exists, is
  allocated through `ensureAPSTAOwner()` on role-7 create, and
  exposes the offset-pinned `setSoftAPExtCaps` entry point. The
  controller-layer `setSOFTAP_EXTENDED_CAPABILITIES_IE` selector
  still writes the recovered Apple field set into a separate
  controller-private mirror struct
  (`AirportItlwm::fAPSTASoftApExtCapsState`) and never reaches the
  owner. The controller-private mirror has no consumer, so the
  recovered Apple "input bytes land in the APSTA state region at
  `+0x50/+0x51/+0x59`" contract is not observable on the live
  owner state once role-7 create has allocated it.
- expected system behavior: When the host APSTA owner has been
  allocated by role-7 create, the controller selector forwards
  the input through the owner's `setSoftAPExtCaps`, which mirrors
  `flag00`/`value01`/`value09` into the offset-pinned state-block
  fields `softapAppleVendorIEExtra50`, `softapAppleVendorIETail51`
  and `softapAppleVendorIETail59`. When the owner is absent (the
  default STA boot path before any role-7 create), the recovered
  Apple body still returns success without firmware interaction,
  so the controller selector returns `kIOReturnSuccess` without
  touching driver state.
- actual behavior at HEAD `9a07093`: The selector writes into the
  controller-private `fAPSTASoftApExtCapsState` mirror. The owner's
  `setSoftAPExtCaps` is never invoked from the controller surface;
  the owner state block's `softapAppleVendorIEExtra50/Tail51/Tail59`
  remain zero even after role-7 create has allocated the owner and
  a HostAP setup has driven the selector.
- first visible manifestation: Static inspection at HEAD `9a07093`.
  `grep` over `AirportItlwm/AirportItlwmV2.{hpp,cpp}` shows the
  selector body assigning to `fAPSTASoftApExtCapsState` rather than
  forwarding through `fAPSTAOwner->setSoftAPExtCaps`.

## DIVERGENCE

- exact divergence point: The recovered Apple AppleBCMWLAN HostAP
  contract for `setSOFTAP_EXTENDED_CAPABILITIES_IE` (closure
  document `docs/reference/IWX_APGO_AP_CONTROL_PLANE_STAGE1_LAYER_CLOSURE_2026_05_13.md`,
  item 6, recovered from the B12 closure package) routes the
  selector input through the host APSTA owner's offset-pinned
  state block. The local tree at HEAD `9a07093` mirrors the same
  recovered field layout in the controller surface instead, so
  the owner-side state block remains untouched by the selector.
- confirmed deviation: Controller-layer selector writes into a
  separate private mirror struct; the owner's `setSoftAPExtCaps`
  is not called.
- confirmed root cause: Implementation-only gap explicitly carried
  as the next layer in
  `docs/reference/IWX_APGO_AP_CONTROL_PLANE_STAGE1_LAYER_CLOSURE_2026_05_13.md`
  item 6 and the prior analysis report
  `analysis/ANALYSIS_REPORT_2026-05-14d.md`
  (NEXT LAYER POINTER: "SoftAP extended-capabilities IE selector
  mirror wiring at the controller layer routed through the
  owner's `setSoftAPExtCaps()` entry point").
- exact confirmed deviation removed: The controller surface drops
  the private `fAPSTASoftApExtCapsState` member, and
  `setSOFTAP_EXTENDED_CAPABILITIES_IE` forwards the input through
  `fAPSTAOwner->setSoftAPExtCaps(in)` when the owner is present,
  returning `kIOReturnSuccess` without touching driver state when
  the owner is absent (so the boot-time HostAP probe still
  completes without producing a fake AP-mode side effect).
- exact semantic mismatch removed: The recovered Apple rule
  "`setSOFTAP_EXTENDED_CAPABILITIES_IE` input bytes land in the
  APSTA state region at `+0x50/+0x51/+0x59` with unaligned qwords
  at `+0x51`/`+0x59`" is now expressed by routing the controller
  selector through the owner's offset-pinned state block, which
  carries the recovered layout through compile-time
  `static_asserts` on
  `AirportItlwmAPSTAStateBlock::softapAppleVendorIEExtra50`,
  `softapAppleVendorIETail51` and `softapAppleVendorIETail59`.

## DECOMP REFERENCE BASIS

- B12 reference closure: auditor-verified
  `AP_CONTROL_PLANE_CLOSURE_STATUS: FULL_LAYER_CLOSED_CODER_READY`,
  `REMAINING_DECOMP_TARGETS: EMPTY`,
  `REMAINING_DECOMP_TARGETS_REQUIRE_ADDITIONAL_DATA: NO`.
- B13 export: `itlwm-b13-export-b12-artifacts-20260513T0102`,
  result archive sha256
  `d94b7939e432f680beb943a3ff56be57d000c1ca28ff26a8ba35ed1a118fb9f1`.
- Closure document anchor:
  `docs/reference/IWX_APGO_AP_CONTROL_PLANE_STAGE1_LAYER_CLOSURE_2026_05_13.md`
  item 6 (SoftAP extended-capabilities IE selector mirror routed
  through the owner). The remaining closure items
  (MIS_MAX_STA controller wiring, station-table runtime,
  HOSTAP-panic preflight, `IEEE80211_STA_ONLY` opt-out,
  `ItlHalService` AP/GO surface extensions, firmware AP backend)
  remain explicit follow-up layers; this layer only wires item 6.
- Donor decomps: inherited from the CR-472 / CR-473 Stage 1
  approval evidence (nine selector-352 producer-chain decompiles
  plus `airportd___hostapNetworkDataWithConfiguration_.c`,
  `CoreWLAN_updateAPModeConfiguration.c`, and
  `IO80211_Apple80211{RawSet,SetWithIOCTL,IOCTLSetWrapper,startAPMode}.c`).
  The recovered Apple field-offset layout (`+0x50/+0x51/+0x59`,
  unaligned qword writes) is reflected in the existing
  `AirportItlwmAPSTAStateBlock` static_asserts and the existing
  `AirportItlwmAPSTAStage1Owner::setSoftAPExtCaps` implementation.
- Local source: project-owned, re-expressed in the local itlwm
  style. No proprietary pseudocode or comments are copied
  verbatim.
- Prior commit context: `ac40f18` committed the dormant host APSTA
  owner type with `setSoftAPExtCaps` and the offset-pinned state
  block; `9a07093` wired the owner factory into role-7 acquisition.
  This layer wires the controller-layer SoftAP IE selector mirror
  through the owner's existing entry point.

## CLAIM SCOPE

- exact claim scope: Stage 1 structural acceptance of the
  controller-layer `setSOFTAP_EXTENDED_CAPABILITIES_IE` selector
  mirror forwarding through `AirportItlwmAPSTAStage1Owner::
  setSoftAPExtCaps` and the retirement of the controller-side
  private `fAPSTASoftApExtCapsState` mirror, against base HEAD
  `9a07093`. After this layer, the controller selector forwards
  input through the owner's offset-pinned state block when the
  owner is present, and returns `kIOReturnSuccess` without
  touching driver state when the owner is absent. Default
  STA-only behavior is unchanged: no STA control path invokes
  the selector and the owner is allocated only when role-7 create
  is invoked.
- non-claims:
  - This request does NOT claim working Intel AP/GO firmware
    behavior; the iwx and iwm HALs still do not advertise AP/GO
    and `startLowerIfReady()` returns the fail-closed
    `LowerBlocked` state.
  - This request does NOT claim AP client association, DHCP,
    traffic, beacon emission, or station-table publication.
  - This request does NOT claim CONTROL_STA_NETWORK client control success or
    lab AP control success.
  - This request does NOT claim runtime AP-up; AP-up remains
    false through the owner FSM.
  - This request does NOT change `isHostApRunning()`, the role-7
    create dispatch, or the owner factory; those remain as
    committed in `ac40f18` and `9a07093`.
  - This request does NOT add MIS_MAX_STA controller wiring,
    station-table runtime call sites, HOSTAP-panic-iwx/iwm
    preflight guards, `IEEE80211_STA_ONLY` opt-out, net80211
    station-event consumer binding, `ItlHalService` AP/GO surface
    extensions, or the firmware AP backend.
  - This request does NOT modify
    `AirportItlwm/AirportItlwmAPSTAStage1Owner.{hpp,cpp}`,
    `AirportItlwm/AirportItlwmAPSTAInterface.hpp`,
    `AirportItlwm/AirportItlwm.{hpp,cpp}`,
    `AirportItlwm/AirportSTAIOCTL.cpp`,
    `AirportItlwm/AirportItlwmSkywalkInterface.{hpp,cpp}`, or any
    net80211, iwx, iwm, iwn, `ItlHalService`, `apple80211_*`
    header, or `itlwm.xcodeproj` project file.
  - This request does NOT modify the legacy `AirportItlwm.hpp`
    (V1) controller surface or its
    `AirportSTAIOCTL::setSOFTAP_EXTENDED_CAPABILITIES_IE` body;
    the V1 controller path has no host APSTA owner and remains
    out of scope until a separate layer migrates it.

## LAYER BOUNDARY

One system-visible boundary: the controller surface for the
`AirportItlwm::setSOFTAP_EXTENDED_CAPABILITIES_IE` selector mirror
in the Tahoe (V2) controller. The boundary covers the retirement
of the controller-private `fAPSTASoftApExtCapsState` mirror member
and the forward to the existing owner entry point
`AirportItlwmAPSTAStage1Owner::setSoftAPExtCaps`.

## WHY THIS IS NOT A MICRO-SLICE

The recovered Apple `setSOFTAP_EXTENDED_CAPABILITIES_IE` contract
is a single field-set assignment into the offset-pinned APSTA
state region. The local owner already exposes the offset-pinned
state block and its `setSoftAPExtCaps` entry point; the only
remaining controller-layer touchpoint is the selector body. The
prior controller-side mirror has no consumer and cannot be left
in place after the owner is reachable: leaving it would either
duplicate the recovered field layout in two places (drifting
risk) or hide a divergence where the offset-pinned state remains
zero after a HostAP setup driving the selector. Splitting "drop
the controller mirror" from "forward to the owner" would leave
the selector silently dropping the input. The other layer-6/7/8
items in the closure document (MIS_MAX_STA controller wiring,
station-table runtime, HOSTAP-panic preflight, AP/GO HAL surface
extensions, firmware AP backend) are independent boundaries and
remain explicit follow-up layers.

## REFERENCE_CAPABILITY_GAP

- detected: YES
- gap: Controller-layer
  `setSOFTAP_EXTENDED_CAPABILITIES_IE` selector mirror routed
  through the host APSTA owner's offset-pinned state block,
  matching the recovered Apple `+0x50/+0x51/+0x59` field layout
  while AP firmware is fail-closed.
- route: IMPLEMENT_LOCAL.
- route_owner: auditor (route inherited from the CR-472 / CR-473
  owner-class introduction and the closure-document item 6).
- why_route: The owner's state block, the field offsets, and the
  selector body are per-controller Apple-specific boundaries.
  Reusing Linux `mac80211` or OpenBSD `net80211` AP storage would
  distort the offset-pinned layout; the existing owner already
  expresses the recovered layout through project-owned C++ with
  compile-time static_asserts.

## VERIFICATION

- decomp/reference completeness: The B12/B13 closure package
  covers the `setSOFTAP_EXTENDED_CAPABILITIES_IE` field-set
  semantics and the offset-pinned state-block contract. No
  competing hypothesis is in play; the closure document
  explicitly names item 6 as the next layer after the role-7
  acquisition wiring.
- build: `scripts/build_tahoe.sh
  /System/Library/KernelCollections/BootKernelExtensions.kc`
  must report `BUILD SUCCEEDED` and all undefined symbols
  resolved against the live BootKC. The candidate kext sha256 is
  captured in the Stage 1 request.

## LIVE_RUNTIME_AFTER_LAYER_PLAN

After Stage 1 approval the coder will:

1. Re-build the exact reviewed diff against base HEAD `9a07093`
   and confirm `BUILD SUCCEEDED` plus undefined-symbol resolution
   against the live Tahoe BootKC.
2. Record candidate kext sha256 and `LC_UUID` identity, then
   install per the protocol flow: remove old
   `/Library/Extensions/AirportItlwm.kext`, copy the new kext,
   set `root:wheel` ownership, `chmod go-w`, approve any system
   extension UI prompt through VNC `127.0.0.1:5901`.
3. Reboot with `sudo shutdown -r now`; require SSH on
   `127.0.0.1:3322` to return within 120 seconds, and if not,
   inspect VNC immediately and act per protocol.
4. Verify post-reboot identity: AuxKC refresh loaded the new
   kext, `LC_UUID` and sha256 match the candidate, `AirportItlwm`
   and `AirportItlwmSkywalkInterface` registered/matched/active,
   Wi-Fi interface present in STA mode.
5. Capture a five-minute stability window with no panic, no
   unsupported-opmode log, no driver unload, and no
   AP/HOSTAP/APSTA event marker. The controller selector remains
   unreached on the default STA boot path because no production
   caller drives `setSOFTAP_EXTENDED_CAPABILITIES_IE`.
6. Record evidence under `commit-approval/runtime/CR-474/`:
   `pre_reboot_identity.txt`, `post_reboot_evidence.txt`,
   `airportitlwm_kernel_log.txt`, `io80211_corewifi_logs.txt`,
   `ioregistry_state.txt`, `wifi_state.txt`,
   `no_panic_evidence.txt`, `stability_window_evidence.txt`,
   plus the build/symcheck log and exact diff/request SHAs.
7. File CR-474 Stage 2 (`STAGE_2_AFTER_FIX_RUNTIME`) with the
   bounded claim `build/install/load/no-panic/default-STA-only
   behavior unchanged when the SoftAP IE selector is not
   invoked` and emit `COMMIT_REQUEST_SUBMITTED`.

## NEXT LAYER POINTER

After this layer, the natural follow-up layers in dependency
order remain:

- MIS_MAX_STA controller wiring (closure-doc item 7) routed
  through the owner's `setMisMaxSta()` entry point.
- HOSTAP-panic-iwx and HOSTAP-panic-iwm preflight guards in the
  iwx and iwm HALs.
- `IEEE80211_STA_ONLY` opt-out and net80211 station-event
  consumer binding to the owner.
- `ItlHalService` AP/GO HAL surface extensions for
  `setBeaconTemplate`, `setCipherKey`, `triggerCSA`, and
  `sendAPStationCommand`.
- Stage 2 Intel iwx/iwm AP/GO firmware backend.
- Legacy `AirportItlwm.hpp` (V1) controller migration to the
  same owner-routed selector mirror (kept out of the present
  layer because the V1 path has no host APSTA owner).

Each is a separate coherent layer and is not part of this
Stage 1.
