# Analysis report — 2026-05-14f — V2 MIS_MAX_STA controller-to-owner forwarding

## Layer scope

This Stage 1 layer reroutes the Tahoe (V2) controller selector
`AirportItlwm::setMIS_MAX_STA` through the host APSTA owner entry
point `AirportItlwmAPSTAStage1Owner::setMisMaxSta`, and retires the
V2 controller-private maxassoc helper `AirportItlwm::setMaxAssoc`
together with its declaration in `AirportItlwmV2.hpp`. After the
preceding CR-474 commit, that V2 helper is reachable only from the
V2 `setMIS_MAX_STA` body; rerouting the selector through the owner
leaves the V2 helper without any caller in the V2 target. The
legacy V1 controller path (`AirportSTAIOCTL.cpp`) retains its own
`setMaxAssoc`/`setMIS_MAX_STA` bodies and is explicitly out of
scope.

## Auditor-verified reference closure inherited

The B12/B13 AP control-plane decomp closure remains binding. The
prior auditor-verified closure recorded
`AP_CONTROL_PLANE_CLOSURE_STATUS: FULL_LAYER_CLOSED_CODER_READY`,
`REMAINING_DECOMP_TARGETS: EMPTY`, and
`REMAINING_DECOMP_TARGETS_REQUIRE_ADDITIONAL_DATA: NO`. The B13
result archive
`itlwm-b13-export-b12-artifacts-20260513T0102-result.tar.zst`
(sha256 `d94b7939e432f680beb943a3ff56be57d000c1ca28ff26a8ba35ed1a118fb9f1`)
already supplies the recovered Apple
`setMIS_MAX_STA` contract together with the
`softapMaxAssoc04`/`softapMaxAssocLimit08` APSTA state-block
offsets that the owner's `setMaxAssoc` body writes. No new decomp
or web-AI cycle is required for this layer; this request is the
bounded integration of closure-doc item 7.

## Recovered Apple `setMIS_MAX_STA` contract

The recovered controller body has three points:

1. Null input → `kIOReturnBadArgument`.
2. AP-up gate: when the APSTA owner is AP-up
   (lifecycle == `Running` and the lower HAL's `startAPMode` has
   reported success), forward the input `value00` to the
   maxassoc backend and ignore the helper's return value.
3. Otherwise (no owner, or owner exists but AP is down): silently
   return `kIOReturnSuccess` without touching driver state.

The owner-side `AirportItlwmAPSTAStage1Owner::setMisMaxSta` already
implements points 1–2 (null check, AP-up gate, owner
`setMaxAssoc` forward, ignored result) and the owner-side
`AirportItlwmAPSTAStage1Owner::setMaxAssoc` writes
`softapMaxAssoc04`, `softapMaxAssocLimit08`, and `ic->ic_max_aid`
while clamping to `[1, IEEE80211_AID_DEF]` to preserve the AID/TIM
bitmap invariant documented in `ieee80211_node_attach`.
Point 3 is split between the controller and the owner: when the
controller has no APSTA owner the controller body returns success
without consulting the owner; when an owner exists but is AP-down
the owner body returns success.

## What is in this layer

The Stage 1 patch in this request changes, against HEAD `3adc3d0`:

- `AirportItlwm/AirportItlwmV2.cpp`:
  - Updates the `AP-mode HostAP selector wiring (Tahoe
    IO80211Family parity)` doc comment to record that the V2
    `setMIS_MAX_STA` selector now routes through the host APSTA
    owner, that the AP-up gate / clamp / state-block writes /
    `ic_max_aid` mutation live inside the owner, and that
    owner-absent default-STA boot still returns success without
    firmware interaction.
  - Updates the `isHostApRunning` inline comment to drop the
    obsolete mention of a controller-side `setMaxAssoc` callee
    that no longer exists in the V2 target.
  - Removes the now-dead `AirportItlwm::setMaxAssoc(uint32_t)`
    body. The only V2 caller was the prior `setMIS_MAX_STA`
    body; with that caller rerouted to the owner the V2
    controller has no remaining consumer of this helper.
  - Replaces the `AirportItlwm::setMIS_MAX_STA` body with a
    short forward: null input → `kIOReturnBadArgument`; no APSTA
    owner → `kIOReturnSuccess` without driver state mutation;
    otherwise return the owner's `setMisMaxSta` result.
- `AirportItlwm/AirportItlwmV2.hpp`:
  - Removes the now-dead `IOReturn setMaxAssoc(uint32_t value);`
    declaration. The legacy V1 header `AirportItlwm/AirportItlwm.hpp`
    keeps its independent declaration because the V1 controller
    body in `AirportSTAIOCTL.cpp` still hosts a `setMaxAssoc`.

## What is NOT in this layer

The following items are explicitly deferred to follow-up Stage 1
layers, mirroring the AP control-plane closure document and the
prior CR-474 Stage 1 / Stage 2 decisions:

- Station-table runtime call sites that populate the recovered
  five-entry APSTA station table.
- `HOSTAP-panic-iwx` and `HOSTAP-panic-iwm` preflight guards in
  `itlwm/hal_iwx/ItlIwx.cpp` and `itlwm/hal_iwm/mac80211.cpp`.
- `IEEE80211_STA_ONLY` opt-out and the net80211 station-event
  consumer binding to the host APSTA owner.
- `ItlHalService` AP/GO HAL surface extensions for
  `setBeaconTemplate`, `setCipherKey`, `triggerCSA`, and
  `sendAPStationCommand`.
- Stage 2 Intel iwx/iwm AP/GO firmware backend (any reachable
  `startAPMode`/beacon/key/CSA/station-command machinery).
- Legacy V1 controller migration of the maxassoc helper or the
  `setMIS_MAX_STA` selector. The V1 controller path has no host
  APSTA owner and is kept on its prior in-controller helper. A
  separate Stage 1 layer will migrate that path once a V1 owner
  is introduced or a follow-up audit explicitly retires the V1
  controller.
- AP association, AP DHCP, AP traffic, beacon emission, station
  table runtime publication, CONTROL_STA_NETWORK client/control success, lab
  AP success, or any other functional AP/STA bring-up evidence.

## Non-claims

This Stage 1 layer does not claim:

- working Intel AP/GO firmware behavior;
- AP client association, DHCP, traffic, or beacon emission;
- CONTROL_STA_NETWORK control or lab AP control success;
- runtime AP-up; AP-up remains false in the present runtime
  because no HAL backend advertises AP/GO and the owner's
  `isApRunning` returns false;
- station-table runtime visibility, HOSTAP-panic-guard
  activation, `IEEE80211_STA_ONLY` opt-out, net80211
  station-event consumer binding, or `ItlHalService` AP/GO HAL
  extensions;
- V1 controller `setMIS_MAX_STA`/`setMaxAssoc` migration;
- any source change in `AirportItlwm/AirportItlwm.{hpp,cpp}`,
  `AirportItlwm/AirportItlwmAPSTAInterface.hpp`,
  `AirportItlwm/AirportItlwmAPSTAStage1Owner.{hpp,cpp}`,
  `AirportItlwm/AirportItlwmRegDiag.hpp`,
  `AirportItlwm/AirportItlwmSkywalkInterface.{hpp,cpp}`,
  `AirportItlwm/AirportSTAIOCTL.cpp`, or
  `AirportItlwmRegDiag/airport_itlwm_regdiag.c`;
- any source change in `include/HAL/ItlHalService.hpp`,
  `itl80211/openbsd/net80211/*`, `itlwm/hal_iwx/*`, or
  `itlwm/hal_iwm/*`;
- runtime evidence; Stage 2 after-fix runtime is a separate
  request and remains bounded to default-STA build/install/load/
  no-panic behavior because no production caller exercises the
  rerouted selector on the default-STA path.

## Layer boundary

One system-visible boundary: the V2 controller surface for the
`AirportItlwm::setMIS_MAX_STA` selector. The boundary covers the
controller-private `setMaxAssoc` helper that the selector body
previously called, and the rerouted selector body that now
forwards through `AirportItlwmAPSTAStage1Owner::setMisMaxSta`.
Splitting "drop the controller helper" from "forward to the owner"
would either leave the selector calling a removed helper or leave
a dead controller-side helper after the forward lands, which is
why the two edits stay in the same atomic boundary.

## Why this is not a micro-slice

The closure-document item 7 boundary is exactly the V2
`setMIS_MAX_STA` controller-to-owner forwarding. The recovered
contract is concrete (null check → bad-argument; AP-up gate; AP-up
maxassoc forward; AP-down / owner-absent success), the owner-side
entry point and the owner-side state-block writes already exist
(`setMisMaxSta`, `setMaxAssoc`, `softapMaxAssoc04`,
`softapMaxAssocLimit08`, `ic_max_aid`), and the controller-side
helper has no remaining consumer once the selector reroutes.
Leaving the dead helper in place would duplicate the clamp logic
in two places and create drift risk; splitting the routing change
from the helper retirement would either be a one-line CR or
require a temporary "dead but defined" intermediate state. The
adjacent closure-document items (HOSTAP-panic guards,
`IEEE80211_STA_ONLY` opt-out, net80211 station-event binding,
`ItlHalService` AP/GO extensions, firmware backend, V1 migration)
each have their own system-visible boundaries and are explicit
follow-up layers.

## Reference capability gap

- detected: YES
- gap: controller-layer `setMIS_MAX_STA` selector routed through
  the host APSTA owner's AP-up gate and owner-side `setMaxAssoc`
  state-block / `ic_max_aid` writes, matching the recovered Apple
  body while AP firmware remains fail-closed in the iwx/iwm
  HALs.
- route: `IMPLEMENT_LOCAL`.
- route_owner: auditor (route inherited from CR-472 / CR-473
  owner introduction and CR-474 selector-mirror precedent).
- why_route: the owner state-block layout (offsets +0x04 / +0x08
  for `softapMaxAssoc04` / `softapMaxAssocLimit08`) and the
  `[1, IEEE80211_AID_DEF]` clamp tied to the net80211 AID/TIM
  bitmap are Apple-specific and net80211-specific. Reusing Linux
  `mac80211` or OpenBSD `net80211` AP storage as a donor would
  distort the offset-pinned layout already chosen by the existing
  owner. The local owner's `setMaxAssoc` body already expresses
  the recovered contract in project-owned C++ with the clamp
  invariant explicitly documented in the local net80211 attach
  path.

## Verification plan

- decomp/reference completeness: the B12/B13 closure package
  covers the recovered `setMIS_MAX_STA` field-set semantics, the
  AP-up gate, and the APSTA state-block offsets. No competing
  hypothesis is in play; the closure document explicitly names
  item 7 as the next layer after the SoftAP-extcaps selector
  mirror.
- build: `scripts/build_tahoe.sh
  /System/Library/KernelCollections/BootKernelExtensions.kc`
  must report `BUILD SUCCEEDED` and all undefined symbols
  resolved against the live BootKC. The candidate kext sha256
  and LC_UUID are captured in the Stage 1 request.

## Live runtime after-layer plan

After Stage 1 approval the coder will:

1. Rebuild the exact reviewed diff against base HEAD `3adc3d0`
   in an isolated worktree and confirm `BUILD SUCCEEDED` plus
   undefined-symbol resolution against the live Tahoe BootKC.
2. Record candidate kext sha256 and `LC_UUID` identity, then
   install per the protocol flow: remove old
   `/Library/Extensions/AirportItlwm.kext`, copy the new kext,
   set `root:wheel` ownership, approve any system extension UI
   prompt through VNC `127.0.0.1:5901`.
3. Reboot with `sudo shutdown -r now`; require SSH on
   `127.0.0.1:3322` to return within 120 seconds, and inspect
   VNC immediately if not.
4. Verify post-reboot identity: AuxKC refresh loaded the new
   kext, `LC_UUID` and sha256 match the candidate,
   `AirportItlwm` matched in IORegistry, Wi-Fi interface
   present in STA mode.
5. Capture a five-minute stability window with no panic, no
   unsupported-opmode log, no driver unload, and no
   AP/HOSTAP/APSTA/role-7/setMIS_MAX_STA/setMisMaxSta marker on
   the default STA boot path. No production caller drives
   `setMIS_MAX_STA` on a default STA boot, so the rerouted
   selector body remains unreached.
6. Record evidence under `commit-approval/status/runtime/CR-475/`:
   build/symcheck log, pre-reboot/post-reboot identity, boot
   sequence, ioregistry state, Wi-Fi state, panic check, opmode
   /unload/marker scan, and stability window.
7. File CR-475 Stage 2 (`STAGE_2_AFTER_FIX_RUNTIME`) with the
   bounded claim "build/install/load/no-panic/default-STA-only
   behavior unchanged when the rerouted V2 `setMIS_MAX_STA`
   selector is not invoked" and emit
   `COMMIT_REQUEST_SUBMITTED`.

## Next layer pointer

After this layer the natural follow-up layers in dependency order
remain:

- HOSTAP-panic-iwx and HOSTAP-panic-iwm preflight guards in the
  iwx and iwm HALs.
- `IEEE80211_STA_ONLY` opt-out plus net80211 station-event
  consumer binding to the host APSTA owner.
- `ItlHalService` AP/GO HAL surface extensions for
  `setBeaconTemplate`, `setCipherKey`, `triggerCSA`, and
  `sendAPStationCommand`.
- Stage 2 Intel iwx/iwm AP/GO firmware backend.
- Legacy V1 controller migration of the same owner-routed
  `setMIS_MAX_STA` / `setMaxAssoc` selector wiring once a V1
  owner is introduced or the V1 controller is retired.

Each is a separate coherent layer and is not part of this
Stage 1.
