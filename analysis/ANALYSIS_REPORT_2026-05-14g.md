# Analysis report — 2026-05-14g — iwx/iwm HOSTAP-panic preflight guards

## Layer scope

This Stage 1 layer adds operating-mode admission gates to the
wrapper firmware-command entry points
`ItlIwx::iwx_mac_ctxt_cmd` and `ItlIwm::iwm_mac_ctxt_cmd` in the
two shipped HALs. Each gate refuses the firmware MAC-context
command with `ENOTSUP` and an `XYLog` notice when the net80211
operating mode is neither `IEEE80211_M_MONITOR` nor
`IEEE80211_M_STA`, so the void helper
`*_mac_ctxt_cmd_common(struct *_softc *, struct *_node *,
struct *_mac_ctx_cmd *, uint32_t action)` is never reached with an
opmode value that would trigger the existing
`panic("unsupported operating mode %d\n", ic->ic_opmode)`
fail-closed branch inside the helper.

The change is a pure precondition check; it does not modify any
firmware command, the helper signature, the callers of
`iwx_mac_ctxt_cmd` / `iwm_mac_ctxt_cmd`, or the existing
opmode-conditional branches further down the wrapper. The two
HALs share the identical defense pattern and the same fail-safe
return value, matching the existing wrapper-level error-return
convention used elsewhere in those HALs (e.g. the
`MAC already added` / `MAC already removed` early returns
immediately above the new guard).

## Auditor-verified reference closure inherited

The B12/B13 AP control-plane decomp closure remains binding for
the AP/GO surface this guard protects. The prior auditor-verified
closure recorded
`AP_CONTROL_PLANE_CLOSURE_STATUS: FULL_LAYER_CLOSED_CODER_READY`,
`REMAINING_DECOMP_TARGETS: EMPTY`, and
`REMAINING_DECOMP_TARGETS_REQUIRE_ADDITIONAL_DATA: NO`. The B13
result archive
`itlwm-b13-export-b12-artifacts-20260513T0102-result.tar.zst`
(sha256 `d94b7939e432f680beb943a3ff56be57d000c1ca28ff26a8ba35ed1a118fb9f1`)
together with the prior CR-471 / CR-472 / CR-473 / CR-474 / CR-475
reviewed evidence supplies the recovered Apple AP/GO MAC-context
owner contract that the iwx HAL has begun to wire (see commits
`bc782bc itlwm: add iwx AP/GO firmware TLV support gate`,
`03ce039 itlwm: wire iwx AP/GO capability gate through helper`,
and `43ef057 itlwm: introduce iwx AP/GO MAC-context command owner,
HAL boundary, and attach-time HAL boundary self-test`). No new
decomp or web-AI cycle is required for this guard layer; this
request is the bounded integration of the closure-document item
that names the iwx and iwm HOSTAP-panic preflight guards as the
next dependency-order step.

## Recovered behavior contract for the guard

`*_mac_ctxt_cmd_common` returns `void` and contains an
unconditional `panic("unsupported operating mode %d\n",
ic->ic_opmode)` for any opmode value other than
`IEEE80211_M_MONITOR` or `IEEE80211_M_STA`. There is no error
channel through which the helper can refuse the command; the only
two pre-existing escape paths are `IEEE80211_M_MONITOR` (sets
`MAC_TYPE_LISTENER`) and `IEEE80211_M_STA` (sets
`MAC_TYPE_BSS_STA`). Any other opmode, including
`IEEE80211_M_HOSTAP`, `IEEE80211_M_IBSS`, and `IEEE80211_M_AHDEMO`,
reaches the panic.

`*_mac_ctxt_cmd` is the only caller of `*_mac_ctxt_cmd_common` in
each HAL (verified by `grep -n` over the entire HAL source tree).
That single-caller property is what makes the wrapper-level guard
sufficient: once the wrapper rejects unsupported opmodes, the void
helper is unreachable through that path. If a future change
introduces a new caller of `_mac_ctxt_cmd_common`, that caller
will need its own admission gate; the comment on the new guard
records the helper's failure mode so a future reader has the
context.

## Why a wrapper guard rather than a helper signature change

`*_mac_ctxt_cmd_common` returns `void` and writes its result into
a caller-allocated `struct *_mac_ctx_cmd`; the wrapper
unconditionally assumes the helper produced a usable command after
returning. Changing the helper to return an `int` error code would
require updating every output-side reference in the wrapper that
runs after the helper call, including the
`IWX_FW_CTXT_ACTION_REMOVE` early send-and-return path, the
`MONITOR` filter-flag setup, the `assoc / !ni_associd` beacon
filter path, the `iwx_mac_ctxt_cmd_fill_sta` call, the HE filter
flag, and the final `_send_cmd_pdu` invocation. That signature
change is wider than the bounded HOSTAP-panic preflight guard
described in the AP control-plane closure document item and would
require a separate review of every value path the helper writes
on every supported opmode. The wrapper guard preserves the
existing helper semantics, keeps the diff bounded to the wrapper
function, and matches the closure document's "preflight" wording.

## Why iwn HAL is out of scope

`itlwm/hal_iwn/` does not implement the iwx/iwm-style
`*_mac_ctxt_cmd` / `*_mac_ctxt_cmd_common` MAC-context command
surface (verified by `grep -nE "mac_ctxt_cmd|FW_CTXT_ACTION"
itlwm/hal_iwn/*.cpp` returning no hits). The legacy iwn HAL has
its own historical command sequencing and does not need the
specific HOSTAP-panic preflight guard described by the closure
document item; if a follow-up audit identifies an analogous
fail-closed iwn entry point, it would be a separate layer.

## What is in this layer

The Stage 1 patch in this request changes, against HEAD `8d0ddbd`:

- `itlwm/hal_iwx/ItlIwx.cpp`:
  - In `ItlIwx::iwx_mac_ctxt_cmd`, immediately after the existing
    `IWX_FW_CTXT_ACTION_REMOVE && !active` early-return block and
    before `memset(&cmd, ...)`, insert a doc comment plus an
    `if (ic->ic_opmode != IEEE80211_M_MONITOR &&
    ic->ic_opmode != IEEE80211_M_STA)` admission gate that calls
    `XYLog("%s: refusing MAC context cmd for unsupported
    ic_opmode %d\n", __FUNCTION__, ic->ic_opmode)` and returns
    `ENOTSUP`. No other lines change.
- `itlwm/hal_iwm/mac80211.cpp`:
  - In `ItlIwm::iwm_mac_ctxt_cmd`, the same admission-gate insert
    using the iwm spelling (`IWM_*`) and the iwm helper name in
    the comment. No other lines change.
- `analysis/ANALYSIS_REPORT_2026-05-14g.md`: this analysis report.

## What is NOT in this layer

The following items are explicitly deferred to follow-up Stage 1
layers, mirroring the AP control-plane closure document and the
prior CR-472 / CR-473 / CR-474 / CR-475 Stage 1 / Stage 2
decisions:

- `IEEE80211_STA_ONLY` opt-out and the net80211 station-event
  consumer binding to the host APSTA owner.
- `ItlHalService` AP/GO HAL surface extensions for
  `setBeaconTemplate`, `setCipherKey`, `triggerCSA`, and
  `sendAPStationCommand`.
- The Intel iwx/iwm AP/GO firmware backend (`startAPMode`,
  beacon, key, CSA, station-command machinery).
- Any additional in-HAL preflight guards beyond the
  `*_mac_ctxt_cmd` wrapper (other functions that branch on
  `ic_opmode` and currently have no HOSTAP-aware path: scan,
  power, TX, RX, phy/binding, regulatory, and similar). Those
  branches do not currently call `panic()` on HOSTAP today, so
  they are not part of this preflight-guard layer; the
  `IEEE80211_STA_ONLY` opt-out layer will be the first to make
  any of them reachable on a HOSTAP opmode and is the natural
  place to add the per-branch handling needed there.
- Helper-side signature changes to `*_mac_ctxt_cmd_common` (see
  rationale above).
- Legacy `itlwm/hal_iwn/` HAL changes (no analogous command
  surface, see above).
- AP association, AP DHCP, AP traffic, beacon emission, station
  table runtime publication, CONTROL_STA_NETWORK client/control success, lab
  AP success, or any other functional AP/STA bring-up evidence.
- Stage 2 after-fix runtime; that is a separate request and
  remains bounded to default-STA build/install/load/no-panic
  behavior because the iwx and iwm wrappers are reached on the
  default-STA path with `ic->ic_opmode == IEEE80211_M_STA`, which
  the new guard explicitly admits, so the only observable runtime
  change is identical to HEAD on the default-STA boot.

## Non-claims

This Stage 1 layer does not claim:

- working Intel AP/GO firmware behavior;
- AP client association, DHCP, traffic, or beacon emission;
- CONTROL_STA_NETWORK control or lab AP control success;
- runtime AP-up; AP-up remains false in the present runtime
  because no HAL backend advertises AP/GO and the host APSTA
  owner's `isApRunning` returns false;
- any change to the `*_mac_ctxt_cmd_common` helper, the
  `*_mac_ctxt_cmd_fill_sta` helper, the `*_mac_ctxt_cmd_fill_ap`
  / `*_mac_ctxt_cmd_fill_go` / `*_mac_ctxt_cmd_ap_send` AP/GO
  helpers, or any other firmware command path;
- any change in `AirportItlwm/`, `include/HAL/`,
  `itl80211/openbsd/net80211/`, `itlwm/hal_iwn/`,
  `itlwm/hal_intersil/`, `itlwm/hal_iwn/`, or `itlwm.xcodeproj`;
- coverage of the `IEEE80211_STA_ONLY` opt-out, the net80211
  station-event consumer binding to the host APSTA owner, the
  `ItlHalService` AP/GO HAL surface extensions, or the firmware
  AP backend; those remain explicit follow-up layers.

## Layer boundary

One system-visible boundary on each shipped HAL: the firmware
MAC-context command wrapper entry point. The change is symmetric
across `iwx_mac_ctxt_cmd` and `iwm_mac_ctxt_cmd` because the two
helpers share the same fail-closed `panic("unsupported operating
mode")` invariant. Splitting the iwx and iwm guards into separate
Stage 1 requests would leave one HAL with a panic site reachable
from a HOSTAP opmode after the upcoming `IEEE80211_STA_ONLY`
removal lands, while the other HAL would already be safe; that
asymmetry is exactly the kind of partially-applied parity the
"layer-sized implementation" rule forbids.

## Why this is not a micro-slice

The closure-document item names the iwx and iwm HOSTAP-panic
preflight guards together as the dependency-order successor to
the V2 `setMIS_MAX_STA` controller-to-owner forwarding (CR-475
Stage 2 closed). The two wrapper guards are the smallest atomic
unit that closes the named layer: each guard is a single
conditional + log + return, but skipping one HAL would defeat the
panic-elimination goal on the upcoming `IEEE80211_STA_ONLY`
removal. The other follow-up items in the closure document
(`IEEE80211_STA_ONLY` opt-out, `ItlHalService` AP/GO HAL surface
extensions, firmware AP backend, V1 controller migration) each
have their own system-visible boundaries and are explicit
follow-up layers, not part of this request.

## Reference capability gap

- detected: YES
- gap: wrapper-layer admission gate refusing the firmware
  MAC-context command for opmodes other than
  `IEEE80211_M_MONITOR` / `IEEE80211_M_STA` on both shipped HALs
  (iwx and iwm). The recovered Apple AP/GO MAC-context owner
  expects the iwx HAL eventually to process AP/GO MAC contexts;
  until the firmware backend lands, the wrapper guard converts
  the void helper's panic into a controlled `ENOTSUP` so the
  driver remains panic-safe as the host-side opmode propagation
  changes (planned `IEEE80211_STA_ONLY` removal).
- route: `IMPLEMENT_LOCAL`.
- route_owner: auditor (route inherited from CR-472 / CR-473
  owner introduction and CR-474 / CR-475 selector-mirror
  precedent; the helper signature constraints are local to itlwm
  and have no Linux/BSD donor that maps cleanly).
- why_route: the void helper signature and the wrapper's reliance
  on the helper having produced a usable command after return are
  itlwm-local invariants; reusing Linux `mac80211`
  `iwl_mvm_mac_ctx_cmd` or OpenBSD `iwx_mac_ctxt_cmd_common`
  donor code would require either reverting recent itlwm-local
  refactors or reshaping the wrapper return contract. The
  smallest correct local change is the admission gate at the
  wrapper, which preserves all existing semantics for the two
  supported opmodes.

## Verification plan

- decomp/reference completeness: the B12/B13 closure package
  covers the recovered Apple AP/GO MAC-context owner contract.
  The local helper's panic invariant is verified by direct
  reading of the void helper at HEAD `8d0ddbd`
  (`itlwm/hal_iwx/ItlIwx.cpp:8415`,
  `itlwm/hal_iwm/mac80211.cpp:2016`). No competing hypothesis is
  in play.
- single-caller property: verified by `grep -n
  "iwx_mac_ctxt_cmd_common\|iwm_mac_ctxt_cmd_common"
  itlwm/hal_iwx/ItlIwx.cpp itlwm/hal_iwm/mac80211.cpp` returning
  exactly one definition site and one call site per HAL, both
  inside the corresponding `*_mac_ctxt_cmd` wrapper.
- iwn out-of-scope: verified by `grep -nE
  "mac_ctxt_cmd|FW_CTXT_ACTION" itlwm/hal_iwn/*.cpp` returning
  no hits.
- build: `scripts/build_tahoe.sh
  /System/Library/KernelCollections/BootKernelExtensions.kc`
  must report `BUILD SUCCEEDED` and all undefined symbols
  resolved against the live BootKC. The candidate kext sha256
  and `LC_UUID` are captured in the Stage 1 request.

## Live runtime after-layer plan

After Stage 1 approval the coder will:

1. Rebuild the exact reviewed diff against base HEAD `8d0ddbd`
   in an isolated worktree and confirm `BUILD SUCCEEDED` plus
   undefined-symbol resolution against the live Tahoe BootKC.
2. Record candidate kext sha256 and `LC_UUID` identity, then
   install per the protocol flow (remove old
   `/Library/Extensions/AirportItlwm.kext`, copy the new kext,
   set `root:wheel` ownership, approve any system extension UI
   prompt through VNC `127.0.0.1:5901`).
3. Reboot with `sudo shutdown -r now`; require SSH on
   `127.0.0.1:3322` to return within 120 seconds, and inspect
   VNC immediately if not. If the loaded `LC_UUID` does not
   match the candidate after the first reboot (the AuxKC blob
   may have been stale because the kext bundle id and version
   are unchanged across builds), clear
   `/Library/KernelCollections/AuxiliaryKernelExtensions.kc`
   and reboot once more so `kernelmanagerd` rebuilds AuxKC from
   the on-disk staged kext (precedent: CR-475 Stage 2 evidence
   `stage2_runtime_start.txt`).
4. Verify post-reboot identity: `kmutil showloaded` reports the
   new candidate `LC_UUID`, `system_profiler SPAirPortDataType`
   reports `Firmware Version: itlwm: 2.4.0 (8d0ddbd) fw: ...`
   with the seven-character reviewed-HEAD short hash,
   `AirportItlwm` is registered in IORegistry, Wi-Fi interface
   present in STA mode.
5. Capture a five-minute stability window with no panic, no
   `unsupported.opmode` log, no driver unload, and no
   `refusing MAC context cmd for unsupported ic_opmode`
   marker on the default STA boot path. The default STA boot
   reaches `iwx_mac_ctxt_cmd` with `ic_opmode ==
   IEEE80211_M_STA`, which the new guard admits unchanged, so
   no marker should fire.
6. Record evidence under `commit-approval/status/runtime/CR-476/`:
   build/symcheck log, pre-reboot/post-reboot identity, boot
   sequence, IORegistry state, Wi-Fi state, panic check,
   opmode/unload/marker scan, and stability window.
7. File CR-476 Stage 2 (`STAGE_2_AFTER_FIX_RUNTIME`) with the
   bounded claim "build/install/load/no-panic/default-STA-only
   behavior unchanged when ic_opmode is IEEE80211_M_STA" and
   emit `COMMIT_REQUEST_SUBMITTED`.

## Next layer pointer

After this layer the natural follow-up layers in dependency
order remain (per the AP control-plane closure document):

- `IEEE80211_STA_ONLY` opt-out plus net80211 station-event
  consumer binding to the host APSTA owner. This is the first
  layer that allows opmode values other than `MONITOR`/`STA` to
  reach the iwx and iwm wrappers, at which point this guard
  becomes load-bearing on the default-STA boot path of an
  AP-mode-capable build.
- `ItlHalService` AP/GO HAL surface extensions for
  `setBeaconTemplate`, `setCipherKey`, `triggerCSA`, and
  `sendAPStationCommand`.
- Stage 2 Intel iwx/iwm AP/GO firmware backend (the
  `*_mac_ctxt_cmd` wrappers will need their conditional branches
  extended to cover AP/GO once the backend lands; today the
  guard refuses the command for anything other than MONITOR/STA,
  including AP/GO).
- Legacy V1 controller migration of the V2 owner-routed selector
  wiring once a V1 owner is introduced or the V1 controller is
  retired.

Each is a separate coherent layer and is not part of this
Stage 1.
