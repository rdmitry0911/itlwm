# Analysis report — 2026-05-14h — net80211 station-event consumer binding

## Layer scope

This Stage 1 layer wires the host APSTA owner introduced by the
prior CR-471 / CR-472 / CR-473 / CR-474 / CR-475 chain into the
net80211 station-event producer bridge added in
`a49b284 net80211: introduce AP station-event producer bridge
and STA_ONLY opt-out`. The producer bridge functions
`ieee80211_apsta_event_register`, `ieee80211_apsta_event_unregister`,
and `ieee80211_apsta_event_publish` are already present in
`itl80211/openbsd/net80211/ieee80211_node.c`; the existing
`AirportItlwmAPSTAStage1Net80211Event` callback already maps the
producer-side event identifiers to
`AirportItlwmAPSTAStage1Owner::publishStationEventFromNet80211`.
What is missing today is the controller-layer binding that
registers the owner as the single consumer for the lifetime of
each role-7 (APPLE80211_VIF_SOFT_AP) APSTA owner.

The change has two parts:

1. The existing extern "C" callback
   `AirportItlwmAPSTAStage1Net80211Event` is renamed parameter
   order so its signature matches the producer-bridge typedef
   `ieee80211_apsta_event_cb_t`
   (`void (*)(struct ieee80211com *, struct ieee80211_node *,
   int event, void *arg)`). The previous parameter order
   `(ieee80211com *, void *arg, uint32_t eventType, const
   ieee80211_node *ni)` was a residual scaffolding shape
   inherited from the CR-472 owner introduction that landed
   before the producer-bridge ABI was fixed in CR-471.
2. `AirportItlwm::ensureAPSTAOwner` calls
   `ieee80211_apsta_event_register(ic, callback, owner)`
   after a successful owner allocation, where `ic` is
   `fHalService->get80211Controller()`. Both
   `AirportItlwm::releaseAll` (the driver release teardown
   path) and `AirportItlwm::deleteAPSTAOwner` (the explicit
   per-role-7 teardown path that is wired but not yet reached
   in the Tahoe Skywalk dispatch surface) call
   `ieee80211_apsta_event_unregister(ic, fAPSTAOwner)` before
   releasing the owner reference.

The change is structurally inert on the default Tahoe build
because the publish call sites in `ieee80211_node_join` and
`ieee80211_node_leave` sit inside the
`#ifndef IEEE80211_STA_ONLY` ... `#endif` block that wraps the
AP-side of `itl80211/openbsd/net80211/ieee80211_node.c`. With
the default `IEEE80211_STA_ONLY` enabler still on (no caller
defines `IEEE80211_OPT_OUT_STA_ONLY`), those publish sites are
not compiled, so the producer never invokes the registered
callback at runtime. The owner-side binding is nonetheless the
correct shape: it lands the controller→producer-bridge
boundary so the subsequent `IEEE80211_STA_ONLY` opt-out layer
can compile the publish path without requiring a second
revision of the role-7 owner lifecycle.

## Auditor-verified reference closure inherited

The B12/B13 AP control-plane decomp closure remains binding for
the recovered Apple AP/GO station-event owner contract this
binding implements. The prior auditor-verified closure recorded
`AP_CONTROL_PLANE_CLOSURE_STATUS: FULL_LAYER_CLOSED_CODER_READY`,
`REMAINING_DECOMP_TARGETS: EMPTY`, and
`REMAINING_DECOMP_TARGETS_REQUIRE_ADDITIONAL_DATA: NO`. The B13
result archive
`itlwm-b13-export-b12-artifacts-20260513T0102-result.tar.zst`
(sha256 `d94b7939e432f680beb943a3ff56be57d000c1ca28ff26a8ba35ed1a118fb9f1`)
together with the prior CR-471 / CR-472 / CR-473 / CR-474 /
CR-475 / CR-476 reviewed evidence supplies the recovered Apple
APSTA station-event owner contract that the controller-layer
binding now wires to. The producer-bridge ABI, lifecycle, and
concurrency invariants are reviewer-approved in the closing
decision for CR-471
(`commit-approval/decisions/COMMIT_DECISION_CR-471-stage2-net80211-station-event-producer-bridge-clean-head-v2.md`),
including the single-consumer cookie-identity contract used by
`ieee80211_apsta_event_unregister`. No new decompilation or
web-AI cycle is required for this binding layer; this request
is the bounded integration of the next item in the AP
control-plane closure document.

## Recovered behavior contract for the binding

The producer-bridge contract at
`itl80211/openbsd/net80211/ieee80211_var.h:470-490` is:

- `ic_apsta_event_cb` is a non-retaining callback registered
  by a single host-owned APSTA consumer through
  `ieee80211_apsta_event_register` and cleared on role-7
  delete or controller teardown through
  `ieee80211_apsta_event_unregister`.
- `ic_apsta_event_arg` is the opaque consumer-owned cookie
  passed back to the callback and used by the unregister
  identity check so a stale teardown that arrives after a new
  owner has registered cannot silently clear the new
  consumer's callback.

The bridge implementation in
`itl80211/openbsd/net80211/ieee80211_node.c:3580-3658`
guarantees:

- `register` rejects a NULL callback with `EINVAL`; rejects a
  different `(cb, arg)` pair with `EBUSY` when one is already
  registered; accepts idempotent re-registration of the same
  `(cb, arg)` pair.
- `unregister` is a no-op when no consumer is currently bound
  and a no-op when `arg` does not match the registered cookie.
- `publish` snapshots `cb` and `arg` to local stack slots
  before invocation, so a concurrent unregister cannot tear
  the values out from under an in-flight callback.
- The producer holds no extra reference on the `struct
  ieee80211_node *` passed to the callback; the consumer must
  take its own reference if it needs the node beyond the
  callback frame. The existing
  `AirportItlwmAPSTAStage1Net80211Event` body reads only
  `ni->ni_macaddr` inside the callback frame, so no node
  reference is acquired.

The event identifiers in
`itl80211/openbsd/net80211/ieee80211_var.h:790-792`
(`IEEE80211_APSTA_EVENT_LEAVE = 5`,
`IEEE80211_APSTA_EVENT_ASSOC = 8`,
`IEEE80211_APSTA_EVENT_REASSOC = 10`) are numerically aligned
with the recovered Apple owner enumerators
(`kAirportItlwmAPSTAEventDeauth = 5`,
`kAirportItlwmAPSTAEventAssocInd = 8`,
`kAirportItlwmAPSTAEventReassocInd = 10`) in
`AirportItlwm/AirportItlwmAPSTAInterface.hpp:599-604`. The
existing `publishStationEventFromNet80211` switch dispatches on
those values without translation.

The publish call sites are at
`itl80211/openbsd/net80211/ieee80211_node.c:3226` (publish
`ASSOC`/`REASSOC` from `ieee80211_node_join`) and
`itl80211/openbsd/net80211/ieee80211_node.c:3404` (publish
`LEAVE` from `ieee80211_node_leave`). Both call sites sit
inside the surrounding `#ifndef IEEE80211_STA_ONLY` block that
opens at `itl80211/openbsd/net80211/ieee80211_node.c:2974` and
closes at `itl80211/openbsd/net80211/ieee80211_node.c:3557`.
With the default `IEEE80211_STA_ONLY` enabler still on, those
call sites are not compiled and the producer never fires.

## Why a controller-layer binding rather than a net80211 self-binding

The producer bridge intentionally exposes an explicit register/
unregister API so the owner's lifecycle drives consumer
registration, mirroring the recovered Apple APSTA owner that
holds an explicit registration window tied to role-7 owner
creation and teardown. A self-binding inside
`ieee80211_ifattach` / `ieee80211_ifdetach` would require
net80211 to know about `AirportItlwmAPSTAStage1Owner`, which
violates the existing layering: net80211 must remain
controller-agnostic. The controller-layer wiring keeps the
boundary between net80211 and the host APSTA owner one-way:
net80211 publishes, the controller binds and unbinds.

## Why the callback signature change is required

The previous extern "C" declaration of
`AirportItlwmAPSTAStage1Net80211Event` predates the
producer-bridge ABI. The producer-bridge function pointer type
`ieee80211_apsta_event_cb_t` is `void (*)(struct ieee80211com *,
struct ieee80211_node *, int event, void *arg)` and is the
signature `ieee80211_apsta_event_publish` invokes. Registering
a callback whose pointer-to-function type differs from the
producer's invocation type is undefined behavior in standard
C/C++ and the compiler/linker will reject the
`ieee80211_apsta_event_register(ic, callback, owner)` call site
with a type mismatch. Aligning the local extern "C"
declaration with `ieee80211_apsta_event_cb_t` is the smallest
correct change. The function body is unchanged in behavior:
the same `arg`/`ni`/`event` triple still drives
`publishStationEventFromNet80211(event, ni->ni_macaddr, 0)`.

## What is in this layer

The Stage 1 patch in this request changes, against HEAD
`2b98075`:

- `AirportItlwm/AirportItlwmAPSTAStage1Owner.hpp`: extern "C"
  declaration of `AirportItlwmAPSTAStage1Net80211Event` is
  reshaped to the producer-bridge `ieee80211_apsta_event_cb_t`
  signature. No other declaration is touched.
- `AirportItlwm/AirportItlwmAPSTAStage1Owner.cpp`: the
  matching `extern "C"` definition is reshaped to the same
  signature. The body now passes
  `static_cast<uint32_t>(event)` to
  `publishStationEventFromNet80211`; the numeric values of
  `IEEE80211_APSTA_EVENT_LEAVE/ASSOC/REASSOC` are unchanged
  from the producer-bridge constants, so the dispatch
  semantics are preserved.
- `AirportItlwm/AirportItlwmV2.cpp`:
  - `AirportItlwm::ensureAPSTAOwner` calls
    `ieee80211_apsta_event_register(ic,
    AirportItlwmAPSTAStage1Net80211Event, owner)` after a
    successful owner allocation, where `ic` is
    `fHalService->get80211Controller()`. The call is guarded
    by null checks on `fHalService` and `ic`; the return is
    deliberately discarded with `(void)` because the owner is
    functional even if the producer-bridge bind fails (a
    different consumer would be present, which today is
    impossible because no other call site registers, but the
    fail-soft contract documents the boundary).
  - `AirportItlwm::releaseAll` calls
    `ieee80211_apsta_event_unregister(ic, fAPSTAOwner)`
    before `fAPSTAOwner->release()`, using the same null
    checks. The unregister is placed before the release so
    `fAPSTAOwner` is still a valid pointer for the cookie
    identity check, and `fHalService` is still alive because
    the existing release order tears down the APSTA owner
    before `fHalService->release()`.
  - `AirportItlwm::deleteAPSTAOwner` calls the same
    unregister before `fAPSTAOwner->release()` for the
    symmetric explicit teardown path that will become live
    when the Skywalk role-7 delete entry point is wired in a
    follow-up layer.
- `analysis/ANALYSIS_REPORT_2026-05-14h.md`: this analysis
  report.

## What is NOT in this layer

The following items are explicitly deferred to follow-up Stage
1 layers, mirroring the AP control-plane closure document and
the prior CR-471 / CR-472 / CR-473 / CR-474 / CR-475 / CR-476
Stage 1 / Stage 2 decisions:

- Flipping `IEEE80211_STA_ONLY` off (defining
  `IEEE80211_OPT_OUT_STA_ONLY` in the build) to compile the
  AP-side of `ieee80211_node.c`, including the publish call
  sites in `ieee80211_node_join` and `ieee80211_node_leave`.
  That is a separate layer because the AP-side compilation
  introduces a much larger set of new symbols to verify
  against the BootKC and exercises previously dead net80211
  paths; the iwx/iwm preflight guards from CR-476 are the
  panic-safety prerequisite for that opt-out, and this
  binding is the consumer-side prerequisite, but the actual
  build-flag flip belongs to its own atomic layer.
- `ItlHalService` AP/GO HAL surface extensions for
  `setBeaconTemplate`, `setCipherKey`, `triggerCSA`, and
  `sendAPStationCommand`.
- The Intel iwx/iwm AP/GO firmware backend (`startAPMode`,
  beacon, key, CSA, station-command machinery).
- The Skywalk role-7 delete dispatch wiring that will reach
  `AirportItlwm::deleteAPSTAOwner`; today that function is
  reached only through `AirportItlwm::releaseAll` during
  driver release.
- Legacy V1 controller migration of the V2 owner-routed
  selector wiring once a V1 owner is introduced or the V1
  controller is retired.

## Non-claims

This Stage 1 layer does not claim:

- working Intel AP/GO firmware behavior;
- AP client association, DHCP, traffic, or beacon emission;
- CONTROL_STA_NETWORK control or lab AP control success;
- runtime AP-up; AP-up remains false in the present runtime
  because no HAL backend advertises AP/GO and the host APSTA
  owner's `isApRunning` returns false;
- any net80211-side behavior change in default
  IEEE80211_STA_ONLY builds; the publish call sites remain
  uncompiled and the registered callback is never invoked at
  runtime;
- any change in `itl80211/openbsd/net80211/`,
  `itlwm/hal_iwx/`, `itlwm/hal_iwm/`, `itlwm/hal_iwn/`,
  `itlwm/hal_intersil/`, `itlwm.xcodeproj`, `scripts/`,
  `include/HAL/`, or `AirportItlwm/` outside the three
  touched files (the two APSTA owner files and
  `AirportItlwmV2.cpp`);
- coverage of the `IEEE80211_STA_ONLY` opt-out, the
  `ItlHalService` AP/GO HAL surface extensions, the firmware
  AP backend, or the Skywalk role-7 delete dispatch; those
  remain explicit follow-up layers.

## Layer boundary

One system-visible boundary: the controller-side binding of
the host APSTA owner as the single net80211 station-event
consumer for the lifetime of the role-7 APSTA owner. The
binding is symmetric (register on owner allocation, unregister
on owner release in both the explicit and driver-release
teardown paths). The signature alignment on the extern "C"
callback is part of the same boundary because the binding call
site requires it.

## Why this is not a micro-slice

The AP control-plane closure document names the
`IEEE80211_STA_ONLY` opt-out and the consumer binding together
as the next dependency-order step after the iwx/iwm preflight
guards (CR-476 Stage 2 closed). The opt-out itself is a
separately verifiable build-flag flip that exposes a much
larger AP-side net80211 surface; the consumer binding is the
controller-side prerequisite that must already be present
before the opt-out is safe to flip, so a producer-bridge
publish call lands in a registered consumer rather than a
NULL callback. Splitting the binding from the opt-out is the
exact "one system-visible boundary" required by the
layer-batch rule: every step the binding touches
(ensureAPSTAOwner allocation, releaseAll/deleteAPSTAOwner
teardown, the callback signature) is owner-lifecycle local
and cannot be partitioned further without leaving the
producer-bridge ABI exposed to a non-matching callback type
or leaving the unregister path missing on one of the two
teardown sites.

## Reference capability gap

- detected: YES
- gap: controller-layer binding of the host APSTA owner as
  the single net80211 station-event consumer over the role-7
  APSTA owner lifetime, plus alignment of the existing
  consumer callback signature to the producer-bridge ABI.
  The recovered Apple APSTA owner expects a per-controller,
  single-consumer station-event publication seam that the
  owner registers on role-7 create and unregisters on role-7
  delete or controller teardown; until that binding lands,
  the producer-bridge functions exist but no consumer is
  ever bound, so an AP-opt-out build would publish into a
  NULL callback.
- route: `IMPLEMENT_LOCAL`.
- route_owner: auditor (route inherited from CR-471 producer
  bridge approval and CR-472 / CR-473 / CR-474 / CR-475 /
  CR-476 owner introduction and selector-mirror precedent;
  the consumer-side glue is itlwm-local and has no Linux/BSD
  donor that maps cleanly to the
  `AirportItlwmAPSTAStage1Owner` shape).
- why_route: the producer-bridge ABI, owner lifecycle, and
  controller-side allocation surface are itlwm-local
  inventions that have no Linux/BSD donor; the only
  reference-derived facts the binding needs are the event
  identifiers and the owner's enum mapping, both of which are
  already documented in the prior CR-471 / CR-472 closure
  evidence.

## Verification plan

- decomp/reference completeness: the B12/B13 closure package
  plus the prior CR-471 producer bridge / CR-472 / CR-473 /
  CR-474 / CR-475 / CR-476 reviewed evidence covers the
  recovered Apple APSTA owner contract and the producer-bridge
  ABI; the local owner's enum mapping is verified by direct
  reading of
  `AirportItlwm/AirportItlwmAPSTAInterface.hpp:599-604` and
  `AirportItlwm/AirportItlwmAPSTAStage1Owner.cpp:312-357`. No
  competing hypothesis is in play.
- callback-signature alignment: verified by direct reading of
  `itl80211/openbsd/net80211/ieee80211_var.h:795-797`
  (`ieee80211_apsta_event_cb_t` typedef) and the reshaped
  declaration in
  `AirportItlwm/AirportItlwmAPSTAStage1Owner.hpp`.
- single-binding sites: verified by `grep -nE
  "ieee80211_apsta_event_(register|unregister)"
  AirportItlwm/`, which after this patch returns exactly one
  register call site (`AirportItlwm::ensureAPSTAOwner`) and
  two unregister call sites (`AirportItlwm::releaseAll`,
  `AirportItlwm::deleteAPSTAOwner`). Both unregister sites
  pair with the single allocation/release path via
  `fAPSTAOwner`.
- producer-side guard: verified by `grep -nE
  "^#ifndef IEEE80211_STA_ONLY|^#endif.*STA_ONLY"
  itl80211/openbsd/net80211/ieee80211_node.c` showing that
  the publish call sites at lines 3226 and 3404 sit inside
  the `#ifndef IEEE80211_STA_ONLY` block that opens at line
  2974 and closes at line 3557, so default-STA builds do not
  compile the publish path.
- build: `scripts/build_tahoe.sh
  /System/Library/KernelCollections/BootKernelExtensions.kc`
  must report `BUILD SUCCEEDED` and all undefined symbols
  resolved against the live BootKC. The candidate kext
  sha256 and `LC_UUID` are captured in the Stage 1 request.

## Live runtime after-layer plan

After Stage 1 approval the coder will:

1. Rebuild the exact reviewed diff against base HEAD
   `2b98075` in an isolated worktree (or directly on the
   already-clean canonical worktree if its index/worktree
   remain clean) and confirm `BUILD SUCCEEDED` plus
   undefined-symbol resolution against the live Tahoe BootKC.
2. Record candidate kext sha256 and `LC_UUID` identity, then
   install per the protocol flow (remove old
   `/Library/Extensions/AirportItlwm.kext`, copy the new
   kext, set `root:wheel` ownership, approve any system
   extension UI prompt through VNC `127.0.0.1:5901`).
3. Reboot with `sudo shutdown -r now`; require SSH on
   `127.0.0.1:3322` to return within 120 seconds, and inspect
   VNC immediately if not. If the loaded `LC_UUID` does not
   match the candidate after the first reboot (the AuxKC
   blob may have been stale because the kext bundle id and
   version are unchanged across builds), clear
   `/Library/KernelCollections/AuxiliaryKernelExtensions.kc`
   and reboot once more so `kernelmanagerd` rebuilds AuxKC
   from the on-disk staged kext (precedent: CR-475 Stage 2
   evidence `stage2_runtime_start.txt`).
4. Verify post-reboot identity: `kmutil showloaded` reports
   the new candidate `LC_UUID`, `system_profiler
   SPAirPortDataType` reports a `Firmware Version: itlwm:
   2.4.0 (...)` string with the reviewed-HEAD short hash,
   `AirportItlwm` is registered in IORegistry, the Wi-Fi
   interface is present in STA mode.
5. Capture a five-minute stability window with no panic, no
   `unsupported.opmode` log, no driver unload, and no
   `refusing MAC context cmd for unsupported ic_opmode`
   marker on the default STA boot path. Because the publish
   call sites are not compiled, no AP station-event marker
   should fire either; the absence of any new log marker is
   itself part of the runtime claim.
6. Record evidence under
   `commit-approval/status/runtime/CR-477/`: build/symcheck
   log, pre-reboot/post-reboot identity, boot sequence,
   IORegistry state, Wi-Fi state, panic check,
   opmode/unload/marker scan, and stability window.
7. File CR-477 Stage 2 (`STAGE_2_AFTER_FIX_RUNTIME`) with
   the bounded claim "build/install/load/no-panic/default-
   STA-only behavior unchanged; consumer binding registered
   over the role-7 APSTA owner lifetime when allocated, and
   unregistered on owner teardown" and emit
   `COMMIT_REQUEST_SUBMITTED`. The default STA boot does not
   allocate a role-7 APSTA owner, so the register call site
   is not reached at runtime; the binding is verified by
   source review and the build's symbol resolution.

## Next layer pointer

After this layer the natural follow-up layers in dependency
order remain (per the AP control-plane closure document):

- `IEEE80211_STA_ONLY` opt-out: define
  `IEEE80211_OPT_OUT_STA_ONLY` in the build (and reconcile
  any newly exposed BootKC undefined symbols), so the AP
  side of `ieee80211_node.c` and the producer-bridge publish
  call sites compile. With the iwx/iwm preflight guards from
  CR-476 already in place and this consumer binding already
  in place, the opt-out becomes a self-contained build-flag
  flip whose runtime claim is "default-STA boot still safe;
  no AP-only path triggered at runtime because role-7 owner
  is not allocated".
- `ItlHalService` AP/GO HAL surface extensions for
  `setBeaconTemplate`, `setCipherKey`, `triggerCSA`, and
  `sendAPStationCommand`.
- Stage 2 Intel iwx/iwm AP/GO firmware backend (the
  `*_mac_ctxt_cmd` wrappers will need their conditional
  branches extended to cover AP/GO once the backend lands;
  today the CR-476 guard refuses the command for anything
  other than MONITOR/STA, including AP/GO).
- Skywalk role-7 delete dispatch wiring so
  `AirportItlwm::deleteAPSTAOwner` is reached without the
  driver-release teardown, exercising the explicit
  unregister path.
- Legacy V1 controller migration of the V2 owner-routed
  selector wiring once a V1 owner is introduced or the V1
  controller is retired.

Each is a separate coherent layer and is not part of this
Stage 1.
