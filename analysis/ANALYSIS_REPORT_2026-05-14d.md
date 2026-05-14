# Analysis report — 2026-05-14d (host APSTA owner factory and role-7 acquisition wiring)

This entry records the Stage 1 layer that adds the host-owned APSTA
owner factory, the per-controller owner pointer storage, the role-7
(`APPLE80211_VIF_SOFT_AP`) acquisition dispatch routed through the new
factory, and the owner-derived `isHostApRunning` gate. The layer builds
directly on commit `ac40f18` (the dormant host APSTA owner type
introduction) and the net80211 producer bridge committed under the same
correlation in commit `a49b284`. The owner remains dormant in the
default Tahoe build because no HAL backend advertises AP/GO firmware
support; `startLowerIfReady()` therefore returns the fail-closed
`LowerBlocked` lifecycle state and the AP-up gate stays false.

## ANOMALY

- id: A-AIRPORTITLWM-APSTA-ROLE7-DISPATCH-UNWIRED-20260514
- status: CONFIRMED_OWNER_FACTORY_AND_ROLE7_DISPATCH_MISSING
- symptom: At HEAD `ac40f18`, the host APSTA owner type
  (`AirportItlwmAPSTAStage1Owner`) exists as a build-registered
  translation unit but no production call path allocates it. The role-7
  acquisition branch in `AirportItlwmSkywalkInterface::setVIRTUAL_IF_CREATE`
  still constructs a stack-local `AirportItlwmAPSTAInterface`
  skeleton, clears it, and returns the unchanged
  `kAirportItlwmAPSTACreateFailedReturn`. The recovered Apple
  AppleBCMWLAN APSTA contract instead expects role-7 create to
  allocate a per-controller host-owned APSTA owner that stores the
  carrier descriptor, prepares the APSTA state block, and is reachable
  by later selector/firmware layers. Because the controller has no
  owner pointer, `isHostApRunning` cannot consult the owner FSM and
  remains the prior structurally-false branch.
- expected system behavior: `AirportItlwm` owns one
  `AirportItlwmAPSTAStage1Owner *` controller field, allocated through
  a `ensureAPSTAOwner(create)` factory on role-7 create, torn down
  through `deleteAPSTAOwner()` during driver release. The role-7 branch
  in the Tahoe Skywalk `setVIRTUAL_IF_CREATE` handler validates the
  carrier through the factory, drives `startLowerIfReady()` (which is
  currently fail-closed at the HAL boundary), and returns
  `kIOReturnSuccess` for owner-present, AP-up-false. `isHostApRunning`
  consults the owner FSM (`isApRunning()`).
- actual behavior at HEAD `ac40f18`: No owner pointer exists on the
  controller; the role-7 branch does not allocate the new owner; the
  AP-up gate is owner-blind; the recovered Apple AppleBCMWLAN contract
  for "owner present, AP-up false" cannot be observed even though the
  owner translation unit links into the kext.
- first visible manifestation: Static inspection at HEAD `ac40f18`.
  `grep` over `AirportItlwm/` shows the owner symbol but no
  `fAPSTAOwner` field, no `ensureAPSTAOwner`/`deleteAPSTAOwner`
  members, and an unchanged role-7 branch that constructs the legacy
  stack-local skeleton and returns the failed-create code.

## DIVERGENCE

- exact divergence point: The recovered Apple FSM from the B12/B13
  closure package
  (`docs/reference/IWX_APGO_AP_CONTROL_PLANE_STAGE1_LAYER_CLOSURE_2026_05_13.md`,
  items 1, 2, 4, 5-V2) routes role-7 create through a per-controller
  host owner factory that initializes the owner from the
  `apple80211_virt_if_create_data` carrier descriptor and exposes the
  owner FSM to the AP-up gate. The local tree at HEAD `ac40f18` has
  no such factory, no controller storage, and no role-7 dispatch
  wiring; the AP-up gate is owner-blind.
- confirmed deviation: Factory and storage missing, role-7 dispatch
  unmodified, AP-up gate owner-blind.
- confirmed root cause: Implementation-only gap explicitly carried as
  the next layer in the closure document and the prior analysis report
  `ANALYSIS_REPORT_2026-05-14c.md` ("NEXT LAYER POINTER: factory plus
  controller lifetime storage plus role-7 create/delete dispatch
  wiring (closure-doc items 1 + 2 + 4 + 5-V2)").
- exact confirmed deviation removed: Adds three controller surface
  members and routes the role-7 branch through the factory:
  - `AirportItlwm::fAPSTAOwner` storage pointer initialized to `NULL`
    in `init()` and released in `releaseAll()` ahead of `fHalService`
    so the contractual `stopAPMode` invocation through the owner's
    `teardown()/stopLower()` path always sees a live HAL service.
  - `AirportItlwm::ensureAPSTAOwner(const apple80211_virt_if_create_data *)`
    idempotent factory that returns the existing owner if create has
    been called before, otherwise allocates a new owner, invokes
    `initWithController(this, create)`, and stores the result on
    `fAPSTAOwner`. Returns `nullptr` when the carrier descriptor is
    null or when `initWithController()` rejects the role or layout.
  - `AirportItlwm::deleteAPSTAOwner()` releases the owner, which
    invokes `free() -> teardown() -> stopLower()`, then nulls
    `fAPSTAOwner`.
  - `AirportItlwm::isHostApRunning()` consults
    `fAPSTAOwner->isApRunning()` when the owner is present, otherwise
    returns false (the prior fail-closed behavior).
  - `AirportItlwmSkywalkInterface::setVIRTUAL_IF_CREATE` role-7 branch
    routes the carrier descriptor through
    `instance->ensureAPSTAOwner(data)`, drives
    `owner->startLowerIfReady()` (currently fail-closed; the lifecycle
    transitions to `LowerBlocked` and the AP-up gate stays false), and
    returns `kIOReturnSuccess`. Carrier validation rejects (null
    instance or factory failure) still return
    `kAirportItlwmAPSTARawInvalidArgumentReturn` /
    `kIOReturnNotReady` per the recovered contract.
- exact semantic mismatch removed: The recovered Apple AppleBCMWLAN
  rule "role-7 create allocates the host owner; AP-up is decoupled
  from owner lifetime and remains false until the lower HAL backend
  starts AP mode" is now expressed in project-owned itlwm C++ and the
  owner FSM is reachable through the controller surface. No firmware
  AP backend, AP-up transition, or station association is implemented
  by this layer.

## DECOMP REFERENCE BASIS

- B12 reference closure: auditor-verified
  `AP_CONTROL_PLANE_CLOSURE_STATUS: FULL_LAYER_CLOSED_CODER_READY`,
  `REMAINING_DECOMP_TARGETS: EMPTY`,
  `REMAINING_DECOMP_TARGETS_REQUIRE_ADDITIONAL_DATA: NO`.
- B13 export: `itlwm-b13-export-b12-artifacts-20260513T0102`, result
  archive sha256
  `d94b7939e432f680beb943a3ff56be57d000c1ca28ff26a8ba35ed1a118fb9f1`.
- Closure document anchor:
  `docs/reference/IWX_APGO_AP_CONTROL_PLANE_STAGE1_LAYER_CLOSURE_2026_05_13.md`
  items 1 (factory), 2 (controller storage), 4 (role-7 create/delete
  dispatch), 5-V2 (owner-derived `isHostApRunning`). Items 5-V2 limits
  this layer to the AP-up gate only; SoftAP IE selector mirror,
  MIS_MAX_STA controller wiring, station-table runtime call sites,
  HOSTAP-panic preflight guards, `IEEE80211_STA_ONLY` opt-out, HAL
  surface extensions, and the firmware AP backend remain explicit
  follow-up layers as documented in the prior analysis report.
- Donor decomps: nine selector-352 producer-chain decompiles plus
  `airportd___hostapNetworkDataWithConfiguration_.c`,
  `CoreWLAN_updateAPModeConfiguration.c`,
  `IO80211_Apple80211{RawSet,SetWithIOCTL,IOCTLSetWrapper,startAPMode}.c`.
- Local source: project-owned, re-expressed in the local itlwm style;
  no proprietary pseudocode or comments are copied verbatim.
- Prior commit context: `a49b284` introduced the net80211 station-event
  producer bridge; `ac40f18` committed the dormant host APSTA owner
  type. This layer connects those two boundaries by routing role-7
  acquisition through the owner FSM while AP-up remains false.

## CLAIM SCOPE

- exact claim scope: Stage 1 structural acceptance of the per-controller
  host APSTA owner factory, storage, and role-7 acquisition wiring
  against base HEAD `ac40f18`. After this layer, role-7 create allocates
  the host owner, the owner's `startLowerIfReady()` returns the
  fail-closed `LowerBlocked` lifecycle in the current default Tahoe
  build, `isHostApRunning()` consults the owner FSM, and the
  `kIOReturnSuccess` return for role-7 create reflects the recovered
  Apple "owner present, AP-up false" rule. Default STA-only behavior is
  unchanged: no STA control path queries or calls into the owner, and
  the owner is allocated only when role-7 create is explicitly invoked.
- non-claims:
  - Does NOT claim working Intel AP/GO firmware behavior; the iwx and
    iwm HALs still do not advertise AP/GO and `startLowerIfReady()`
    returns the fail-closed `LowerBlocked` state.
  - Does NOT claim AP client association, DHCP, traffic, beacon
    emission, or station-table publication.
  - Does NOT claim CONTROL_STA_NETWORK client control success or lab AP control
    success.
  - Does NOT claim runtime AP-up; AP-up remains false through the
    owner FSM.
  - Does NOT change AP-mode selector dispatch beyond the role-7 create
    branch and `isHostApRunning`.
  - Does NOT add the SoftAP extended-capabilities IE selector mirror,
    MIS_MAX_STA controller wiring, station-table runtime call sites,
    HOSTAP-panic-iwx/iwm preflight guards, `IEEE80211_STA_ONLY`
    opt-out, ItlHalService AP/GO surface extensions, or the firmware
    AP backend; those remain explicit follow-up layers.
  - Does NOT modify `AirportItlwm/AirportItlwmAPSTAStage1Owner.{hpp,cpp}`;
    that translation unit was committed in `ac40f18` and is consumed
    unchanged.
  - Does NOT modify any net80211, iwx, iwm, iwn, ItlHalService, or
    Xcode project file in this Stage 1 patch.

## LAYER BOUNDARY

One system-visible boundary: per-controller host APSTA owner pointer,
the factory and cleanup accessors, the role-7 acquisition dispatch in
the Tahoe Skywalk handler, and the owner-derived `isHostApRunning()`.
The boundary is the controller surface plus the role-7 entry point.

## WHY THIS IS NOT A MICRO-SLICE

The recovered Apple AppleBCMWLAN rule decouples owner lifetime from
firmware AP-up, but the four touchpoints (factory, controller storage,
role-7 dispatch, AP-up gate) form one indivisible boundary: splitting
them would leave the owner unreachable, or the AP-up gate inconsistent
with the owner FSM, or the role-7 branch returning the legacy
failed-create even when the owner is present. The follow-up SoftAP IE
selector mirror, MIS_MAX_STA gate, station-table runtime, HOSTAP-panic
preflight, and firmware AP backend each constitute their own coherent
boundary and are explicitly deferred.

## REFERENCE_CAPABILITY_GAP

- detected: YES
- gap: Per-controller host APSTA owner factory, controller storage,
  role-7 acquisition dispatch, and owner-derived AP-up gate that
  preserve the recovered Apple AppleBCMWLAN contract while AP-up is
  fail-closed.
- route: IMPLEMENT_LOCAL.
- route_owner: auditor (route inherited from prior CR-472 owner
  introduction layer).
- why_route: Reusing Linux mac80211 `ieee80211_sub_if_data` or OpenBSD
  `ieee80211_ap` factory/storage would distort the Apple controller
  layout, the Apple `apple80211_virt_if_create_data` carrier
  descriptor, and the fail-closed-by-default HAL routing.

## VERIFICATION

- decomp/reference completeness: B12/B13 closure package covers the
  factory boundary, the per-controller owner storage rule, the role-7
  create dispatch contract, and the owner-derived AP-up gate. No
  competing hypothesis is in play; the closure document explicitly
  groups items 1 + 2 + 4 + 5-V2 as the next layer after `ac40f18`.
- build: `scripts/build_tahoe.sh
  /System/Library/KernelCollections/BootKernelExtensions.kc` must
  report `BUILD SUCCEEDED` and all undefined symbols resolved against
  the live BootKC; the candidate kext sha256 is captured in the Stage
  1 request.

## LIVE_RUNTIME_AFTER_LAYER_PLAN

After Stage 1 approval the coder will:

1. Re-build the exact reviewed diff against base HEAD `ac40f18` and
   confirm `BUILD SUCCEEDED` plus undefined-symbol resolution against
   the live Tahoe BootKC.
2. Record candidate kext sha256 and `LC_UUID` identity, then install
   per the protocol flow: remove old `/Library/Extensions/AirportItlwm.kext`,
   copy the new kext, set `root:wheel` ownership, `chmod go-w`,
   approve any system extension UI prompt through VNC `127.0.0.1:5901`.
3. Reboot with `sudo shutdown -r now`; require SSH on `127.0.0.1:3322`
   to return within 120 seconds, and if not, inspect VNC immediately
   and act per protocol.
4. Verify post-reboot identity: AuxKC refresh loaded the new kext,
   `LC_UUID` and sha256 match the candidate, `AirportItlwm` and
   `AirportItlwmSkywalkInterface` registered/matched/active, Wi-Fi
   interface present in STA mode.
5. Capture a five-minute stability window with no panic, no
   unsupported-opmode log, no driver unload, and no AP/HOSTAP/APSTA
   event marker. The new factory is dormant because no production
   caller exercises the role-7 path on default STA boot.
6. Record evidence under `commit-approval/runtime/CR-473/`:
   `pre_reboot_identity.txt`, `post_reboot_evidence.txt`,
   `airportitlwm_kernel_log.txt`, `io80211_corewifi_logs.txt`,
   `ioregistry_state.txt`, `wifi_state.txt`,
   `no_panic_evidence.txt`, `stability_window_evidence.txt`, plus
   the build/symcheck log and exact diff/request SHAs.
7. File CR-473 Stage 2 (`STAGE_2_AFTER_FIX_RUNTIME`) with the bounded
   claim `build/install/load/no-panic/default-STA-only behavior
   unchanged when role-7 acquisition is not invoked` and emit
   `COMMIT_REQUEST_SUBMITTED`.

## NEXT LAYER POINTER

After this layer, the natural follow-up layers in dependency order are:

- SoftAP extended-capabilities IE selector mirror wiring at the
  controller layer (closure-doc item 6) routed through the owner's
  `setSoftAPExtCaps()` entry point.
- MIS_MAX_STA controller wiring (closure-doc item 7) routed through
  the owner's `setMisMaxSta()` entry point.
- HOSTAP-panic-iwx and HOSTAP-panic-iwm preflight guards in the iwx
  and iwm HALs.
- `IEEE80211_STA_ONLY` opt-out and net80211 station-event consumer
  binding to the owner.
- `ItlHalService` AP/GO HAL surface extensions for
  `setBeaconTemplate`, `setCipherKey`, `triggerCSA`, and
  `sendAPStationCommand`.
- Stage 2 Intel iwx/iwm AP/GO firmware backend.

Each is a separate coherent layer and is not part of this Stage 1.
