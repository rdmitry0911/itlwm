# Analysis report — 2026-05-14c (host-owned APSTA owner introduction)

This entry records the Stage 1 layer that introduces the host-owned
APSTA owner as a standalone compiled translation unit and registers it
with the Xcode project. The owner is the consumer side of the net80211
station-event producer bridge already committed under the same
correlation in commit `a49b284`. The owner is reachable only as a
type/symbol at this stage; no production call path allocates or frees
it, the role-7 create/delete dispatch in `AirportSTAIOCTL` and
`AirportItlwmSkywalkInterface` remains unchanged at its prior
failed-create behavior, and AP-up remains false because no HAL backend
advertises AP-mode capability in the default Tahoe build.

## ANOMALY

- id: A-AIRPORTITLWM-APSTA-OWNER-MISSING-20260514
- status: CONFIRMED_HOST_OWNER_TYPE_MISSING
- symptom: At HEAD `a49b284`, the recovered Apple APSTA owner contract
  expects a per-controller host-owned APSTA owner with role-7
  create/delete lifetime, an APSTA state block matching the recovered
  five-entry station-table layout, a SoftAP extended-capabilities IE
  state mirror, a MIS_MAX_STA owner gate, and a fail-closed HAL routing
  for `setBeaconTemplate`/`setCipherKey`/`triggerCSA`/`sendAPStation`
  command surfaces. The local `AirportItlwm/` tree exposes no such
  owner type; the producer bridge committed in `a49b284` therefore has
  no consumer symbol to register against in a follow-up layer.
- expected system behavior: A compiled translation unit defines
  `AirportItlwmAPSTAStage1Owner` as an `OSObject` subclass holding the
  recovered Apple APSTA state block, the five-entry station table, the
  AP-up gate, and the lifecycle method surface. The owner builds into
  the loaded kext, links cleanly against the existing HAL surface, and
  remains dormant under default STA-only behavior because no production
  caller allocates it.
- actual behavior: No owner translation unit exists at HEAD; the
  recovered consumer contract is unreachable by any subsequent layer.
- first visible manifestation: Static inspection at HEAD `a49b284`.
  `grep` over `AirportItlwm/` shows no `AirportItlwmAPSTAStage1Owner`
  symbol and no Xcode `PBXFileReference` for an owner translation unit.

## DIVERGENCE

- exact divergence point: The recovered Apple FSM and contract from
  the B12/B13 closure package (`AP_CONTROL_PLANE_CLOSURE_STATUS:
  FULL_LAYER_CLOSED_CODER_READY`) define a host-owned APSTA owner with
  the lifecycle states `Unallocated → Allocated → Created →
  LowerBlocked → Running → Terminal → Freed`, a five-entry station
  table at the recovered offset/stride, a SoftAP IE state mirror, a
  MIS_MAX_STA gate, and a HAL fail-closed routing for AP-mode beacon,
  key, CSA, and station-command requests. No such owner type exists at
  HEAD.
- confirmed deviation: Owner class missing as standalone compiled unit.
- confirmed root cause: Implementation-only gap explicitly carried as
  the leading layer of the AP control-plane closure document's
  intended Stage 1 introduction (`docs/reference/IWX_APGO_AP_CONTROL_PLANE_STAGE1_LAYER_CLOSURE_2026_05_13.md`).
- exact confirmed deviation removed: Adds `AirportItlwm/AirportItlwmAPSTAStage1Owner.hpp`
  and `AirportItlwm/AirportItlwmAPSTAStage1Owner.cpp` and registers the
  new translation unit in `itlwm.xcodeproj/project.pbxproj` for all
  three AirportItlwm targets that already compile
  `AirportItlwmV2.cpp`.
- exact semantic mismatch removed: The recovered APSTA state block
  layout, station-table layout, lifecycle states, SoftAP IE state
  mirror offsets, MIS_MAX_STA gate, and HAL fail-closed routing are
  now expressed as project-owned C++ in the local style. Method
  bodies route through the existing
  `ItlHalService::supportsAPMode/startAPMode/stopAPMode/updateAPBeacon/setAPKey/triggerAPCSA/get80211Controller`
  surface and return `kIOReturnNotReady`/`kIOReturnUnsupported`
  whenever AP-mode is not supported or the owner is not running.

## DECOMP REFERENCE BASIS

- B12 reference closure: auditor-verified
  `AP_CONTROL_PLANE_CLOSURE_STATUS: FULL_LAYER_CLOSED_CODER_READY`,
  `REMAINING_DECOMP_TARGETS: EMPTY`,
  `REMAINING_DECOMP_TARGETS_REQUIRE_ADDITIONAL_DATA: NO`.
- B13 export: `itlwm-b13-export-b12-artifacts-20260513T0102`,
  result archive sha256
  `d94b7939e432f680beb943a3ff56be57d000c1ca28ff26a8ba35ed1a118fb9f1`.
- Reviewed paths: `FSM_LIFECYCLE.md`, `CONTRACT_RECOVERY.md`,
  `IMPLEMENTATION_ROUTE.md`, `COVERAGE_LEDGER.tsv`,
  `final_selector_closure_matrix.tsv`, `final_layer_closure_matrix.tsv`.
- Donor decomps: nine selector-352 producer-chain decompiles plus
  `airportd___hostapNetworkDataWithConfiguration_.c`,
  `CoreWLAN_updateAPModeConfiguration.c`,
  `IO80211_Apple80211{RawSet,SetWithIOCTL,IOCTLSetWrapper,startAPMode}.c`.
- Local source: project-owned, re-expressed in the local style; no
  proprietary pseudocode or comments are copied verbatim.

## CLAIM SCOPE

- exact claim scope: Stage 1 structural acceptance of the host-owned
  APSTA owner introduction as a standalone compiled unit registered
  with the Xcode project against base HEAD `a49b284`. The new
  translation unit links cleanly, the loaded kext gains the owner
  symbol but no production call path allocates or frees it, and
  default Tahoe STA-only behavior is unchanged.
- non-claims:
  - Does NOT claim working Intel AP/GO firmware behavior.
  - Does NOT claim AP client association, DHCP, traffic, or beacon
    emission success.
  - Does NOT claim CONTROL_STA_NETWORK client control success or lab AP control
    success.
  - Does NOT claim runtime AP-up; AP-up remains the prior
    pre-owner-derived false.
  - Does NOT change any production call path or selector dispatch.
  - Does NOT change `AirportItlwm/AirportItlwm.{hpp,cpp}`,
    `AirportItlwm/AirportItlwmAPSTAInterface.hpp`,
    `AirportItlwm/AirportItlwmRegDiag.hpp`,
    `AirportItlwm/AirportItlwmSkywalkInterface.{hpp,cpp}`,
    `AirportItlwm/AirportItlwmV2.{hpp,cpp}`,
    `AirportItlwm/AirportSTAIOCTL.cpp`, or any iwx/iwm HAL file.
  - Does NOT add the `ensureAPSTAOwner`/`deleteAPSTAOwner` factory,
    controller storage, role-7 create/delete wiring,
    owner-derived `isHostApRunning`, station-table runtime call
    sites, SoftAP IE selector mirror, MIS_MAX_STA controller wiring,
    HOSTAP-panic guards, or the firmware AP backend; those remain
    explicit follow-up layers.

## LAYER BOUNDARY

One system-visible boundary: a new `OSObject` subclass
`AirportItlwmAPSTAStage1Owner` plus its declaration header and Xcode
build registration. The boundary is the type and symbol surface, not
yet any controller/dispatch integration.

## WHY THIS IS NOT A MICRO-SLICE

The recovered Apple APSTA owner is a multi-method class with a
lifecycle state machine, a structured state block, a station table,
SoftAP IE state mirroring, a MIS_MAX_STA gate, and a HAL fail-closed
routing surface. Splitting it further (e.g. just the header, just the
state block, or just the station table) would leave half-defined
types that cannot link as a translation unit. The producer bridge
already committed in `a49b284` has no consumer symbol to point at
until this layer lands; subsequent integration layers (factory,
controller storage, role-7 dispatch, owner-derived
`isHostApRunning`, station-table runtime call sites) all require the
owner type to exist as a compiled symbol.

## REFERENCE_CAPABILITY_GAP

- detected: YES
- gap: A per-controller host-owned APSTA owner class with role-7
  create/delete lifetime, recovered APSTA state block, five-entry
  station table, SoftAP IE state mirror, MIS_MAX_STA gate, and HAL
  fail-closed routing for beacon/key/CSA/station-command requests.
- route: IMPLEMENT_LOCAL.
- route_owner: auditor.
- why_route: The recovered owner contract is per-controller with
  cookie-identity discipline, an Apple-specific state block layout,
  and a fail-closed routing rule keyed on `supportsAPMode()`.
  Linux mac80211 `ieee80211_sub_if_data` and OpenBSD `ieee80211_ap`
  models do not match the recovered Apple layout or the
  fail-closed-by-default HAL routing; reusing either would distort
  the observable contract.

## VERIFICATION

- decomp/reference completeness: B12/B13 closure package covers the
  owner FSM, state block, station table, IE state mirror, MIS_MAX_STA
  gate, and HAL routing rules used in this patch. No competing
  hypothesis is in play.
- build: `scripts/build_tahoe.sh /System/Library/KernelCollections/BootKernelExtensions.kc`
  must report `BUILD SUCCEEDED` and all undefined symbols resolved
  against the live BootKC; the candidate kext sha256 is captured in
  the Stage 1 request.

## LIVE_RUNTIME_AFTER_LAYER_PLAN

After Stage 1 approval the coder will:

1. Re-build the exact reviewed diff against base HEAD `a49b284` and
   confirm `BUILD SUCCEEDED` plus undefined-symbol resolution.
2. Record candidate kext sha256 and LC_UUID identity, then install
   per the protocol install flow: remove old `/Library/Extensions/AirportItlwm.kext`,
   copy the new kext, set ownership to `root:wheel`, chmod `go-w`,
   approve any system extension UI prompt through VNC `127.0.0.1:5901`.
3. Reboot the guest with `sudo shutdown -r now`; require SSH on
   `127.0.0.1:3322` to return within 120 seconds, and if not,
   immediately inspect VNC and act per protocol.
4. Verify post-reboot identity: AuxKC refresh loaded the new kext,
   LC_UUID and sha256 match the candidate, `AirportItlwm` and
   `AirportItlwmSkywalkInterface` registered/matched/active, Wi-Fi
   interface present in STA mode.
5. Capture a five-minute stability window with no panic, no
   unsupported-opmode log, no driver unload, and no AP/HOSTAP/APSTA
   event marker. The new owner type is dormant because no production
   caller allocates it.
6. Record evidence under `commit-approval/runtime/CR-472/`:
   `pre_reboot_identity.txt`, `post_reboot_evidence.txt`,
   `airportitlwm_kernel_log.txt`, `io80211_corewifi_logs.txt`,
   `ioregistry_state.txt`, `wifi_state.txt`,
   `no_panic_evidence.txt`, `stability_window_evidence.txt`, plus
   the build/symcheck log and exact diff/request SHAs.
7. File CR-472 Stage 2 (`STAGE_2_AFTER_FIX_RUNTIME`) with the bounded
   claim `build/install/load/no-panic/default-STA-only behavior
   unchanged` and emit `COMMIT_REQUEST_SUBMITTED`.

## NEXT LAYER POINTER

The natural next coder layer after this one is the factory plus
controller lifetime storage plus role-7 create/delete dispatch wiring
(closure-doc items 1 + 2 + 4 + 5-V2), filed as a separate Stage 1
once this owner type is in HEAD. That layer must remain bounded to
the dormant-by-default owner integration; firmware AP backend remains
a separate Stage 2 firmware layer.

## V3 LIFECYCLE-SAFE INIT FAILURE FIX

The Stage 1 v2 packaging was rejected by independent structural review
because `initWithController()` could return false after `OSObject::init()`
succeeded without initializing the members read by the failed-init
release path through `free()` -> `teardown()` -> `stopLower()`. The
recovered Apple APSTA owner lifecycle requires bad role, null
controller, and null create-data failures to release safely and return
the create failure rather than corrupting the lifetime cleanup.

v3 reorders `initWithController()` so the owner pointer, lifecycle
state, role byte, APSTA state block, MAC address, and BSD-name storage
are all zeroed and the lifecycle is set to `Terminal` before any path
that can return false, including before `OSObject::init()`. This makes
`teardown()` observe `lifecycle == Terminal` and skip `stopLower()` on
the failed-init path, makes `free()` safe to call after any failed
`initWithController()` return, and keeps the dormant-by-default
invariant intact for the successful construction path because every
member is overwritten with its production initial value before
`initWithController()` returns true.

No other method bodies, the header surface, the Xcode registration, or
the analysis narrative for the recovered Apple contract change between
v2 and v3; the lifecycle fix is local to the constructor's pre-return
state and an explanatory source comment that describes the invariant
without referencing review history or service artifacts.
